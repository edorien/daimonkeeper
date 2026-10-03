// kfx_sim coverage: a broad-coverage pass over map_columns.c's
// Column/cube accessor and predicate family -- the same shape as
// map_data_test.cpp's pass. A Column is reached from a Map block via
// col_idx, so the fixture wires a real kfx_sim_state.columns_data[] slot
// (index 0 is reserved/always-invalid, same convention as things_data[0]
// and rooms[0]) into the fixture's map block by hand, then sets its
// bitfields/cubes/floor_texture fields directly rather than going through
// column allocation (create_column et al, deliberately deferred below).
// cube_is_lava/_water/_sacrificial/_unclaimed_path are plain
// CubeConfigStats::properties_flags lookups indexed directly by cube_id
// (no SlabMap lookup in between, unlike get_slab_stats()).
//
// Deliberately deferred: make_solidmask/find_column_height/
// column_is_equivalent/find_column/create_column/clear_columns/
// init_columns/init_whole_blocks/init_top_texture_to_cube_table (column
// table allocation/(re)initialization machinery, not gameplay-logic
// predicates) and subtile_is_unclaimed_path's Room-aware branch (only the
// "no room here" path is covered; the full case needs the same Room
// fixture machinery as map_data_test.cpp's deferred subtile_is_sellable_room).
#include <catch2/catch_test_macros.hpp>

#include "map_columns.h"
#include "map_data.h"
#include "config_cubes.h"
#include "kfx_sim_test_fixtures.h"

using namespace kfx_test;

namespace {
// Wires columns_data[col_idx] into the map block at (stl_x, stl_y) and
// returns the column for direct field manipulation. col_idx must be >= 1
// (index 0 is the reserved "invalid" sentinel).
struct Column *wire_column(MapSubtlCoord stl_x, MapSubtlCoord stl_y, int64_t col_idx)
{
    get_map_block_at(stl_x, stl_y)->col_idx = col_idx;
    return get_column(col_idx);
}
}

TEST_CASE_METHOD(ResetSimAndConfig, "get_column/column_invalid reserve index 0 and bounds-check against COLUMNS_COUNT", "[kfx_sim][map_columns]") {
    CHECK(column_invalid(get_column(0)));
    CHECK(column_invalid(get_column(-1)));
    CHECK(column_invalid(get_column(COLUMNS_COUNT)));
    CHECK_FALSE(column_invalid(get_column(1)));
    CHECK(column_invalid(NULL));
    CHECK(column_invalid(INVALID_COLUMN));
}

TEST_CASE_METHOD(ResetSimAndConfig, "get_column_at/get_map_column resolve through a map block's col_idx", "[kfx_sim][map_columns]") {
    CHECK(column_invalid(get_column_at(2, 2))); // col_idx defaults to 0

    struct Column *col = wire_column(2, 2, 5);
    CHECK(get_column_at(2, 2) == col);
    CHECK(get_map_column(get_map_block_at(2, 2)) == col);

    CHECK(column_invalid(get_column_at(-1, 0))); // invalid map block
}

TEST_CASE_METHOD(ResetSimAndConfig, "get/set_column_floor_filled_subtiles pack a 0..15 value into the top nibble of bitfields", "[kfx_sim][map_columns]") {
    struct Column *col = wire_column(2, 2, 5);
    set_column_floor_filled_subtiles(col, 3);
    CHECK(get_column_floor_filled_subtiles(col) == 3);
    CHECK(get_map_floor_filled_subtiles(get_map_block_at(2, 2)) == 3);
    CHECK(get_floor_filled_subtiles_at(2, 2) == 3);

    // Invalid column/map block: both wrapper accessors report 0 rather than crashing.
    CHECK(get_map_floor_filled_subtiles(get_map_block_at(3, 3)) == 0); // col_idx still 0 there
    CHECK(get_floor_filled_subtiles_at(-1, 0) == 0);
}

TEST_CASE_METHOD(ResetSimAndConfig, "get_column_ceiling_filled_subtiles reads bits 1-3 of bitfields via CLF_CEILING_MASK", "[kfx_sim][map_columns]") {
    struct Column *col = wire_column(2, 2, 5);
    col->bitfields = (col->bitfields & ~CLF_CEILING_MASK) | (3 << 1);
    CHECK(get_column_ceiling_filled_subtiles(col) == 3);
    CHECK(get_map_ceiling_filled_subtiles(get_map_block_at(2, 2)) == 3);
}

TEST_CASE_METHOD(ResetSimAndConfig, "map_pos_solid_at_ceiling is true when the slab is blocking or the column has ceiling cubes", "[kfx_sim][map_columns]") {
    struct Column *col = wire_column(2, 2, 5);
    CHECK_FALSE(map_pos_solid_at_ceiling(2, 2));

    col->bitfields = (col->bitfields & ~CLF_CEILING_MASK) | (1 << 1);
    CHECK(map_pos_solid_at_ceiling(2, 2));

    col->bitfields &= ~CLF_CEILING_MASK;
    get_map_block_at(2, 2)->flags |= SlbAtFlg_Blocking;
    CHECK(map_pos_solid_at_ceiling(2, 2));
}

TEST_CASE_METHOD(ResetSimAndConfig, "get_top_cube_at/_at_pos read the topmost filled cube, or top_cube[floor_texture] when the column is empty", "[kfx_sim][map_columns]") {
    struct Column *col = wire_column(2, 2, 5);

    // Empty column (floor_filled_subtiles == 0): falls back to top_cube[floor_texture].
    col->floor_texture = 7;
    kfx_sim_state.top_cube[7] = 42;
    int64_t cube_pos = -1;
    CHECK(get_top_cube_at(2, 2, &cube_pos) == 42);
    CHECK(cube_pos == 0);
    CHECK(get_top_cube_at_pos(get_subtile_number(2, 2)) == 42);

    // Non-empty column: reads cubes[floor_filled_subtiles - 1].
    set_column_floor_filled_subtiles(col, 2);
    col->cubes[1] = 99;
    CHECK(get_top_cube_at(2, 2, &cube_pos) == 99);
    CHECK(cube_pos == 2);
}

TEST_CASE_METHOD(ResetSimAndConfig, "cube_is_lava/_water/_sacrificial/_unclaimed_path read CubeConfigStats::properties_flags", "[kfx_sim][map_columns]") {
    kfx_config_state.conf.cube_conf.cube_cfgstats[3].properties_flags = CPF_IsLava;
    CHECK(cube_is_lava(3));
    CHECK_FALSE(cube_is_water(3));

    kfx_config_state.conf.cube_conf.cube_cfgstats[3].properties_flags = CPF_IsWater | CPF_IsSacrificial;
    CHECK(cube_is_water(3));
    CHECK(cube_is_sacrificial(3));
    CHECK_FALSE(cube_is_lava(3));

    kfx_config_state.conf.cube_conf.cube_cfgstats[3].properties_flags = CPF_IsUnclaimedPath;
    CHECK(cube_is_unclaimed_path(3));
}

TEST_CASE_METHOD(ResetSimAndConfig, "subtile_has_lava_on_top/_water_on_top/_sacrificial_on_top/subtile_is_liquid check the subtile's top cube", "[kfx_sim][map_columns]") {
    struct Column *col = wire_column(2, 2, 5);
    set_column_floor_filled_subtiles(col, 1);
    col->cubes[0] = 3;
    kfx_config_state.conf.cube_conf.cube_cfgstats[3].properties_flags = CPF_IsLava;

    CHECK(subtile_has_lava_on_top(2, 2));
    CHECK_FALSE(subtile_has_water_on_top(2, 2));
    CHECK(subtile_is_liquid(2, 2));

    kfx_config_state.conf.cube_conf.cube_cfgstats[3].properties_flags = CPF_IsSacrificial;
    CHECK(subtile_has_sacrificial_on_top(2, 2)); // cube_pos (1) < 4
    CHECK_FALSE(subtile_is_liquid(2, 2));

    // sacrificial only counts as "on top" for cube_pos < 4 (low ground).
    set_column_floor_filled_subtiles(col, 5);
    col->cubes[4] = 3;
    CHECK_FALSE(subtile_has_sacrificial_on_top(2, 2)); // cube_pos == 5
}

TEST_CASE_METHOD(ResetSimAndConfig, "subtile_is_unclaimed_path is false when a room occupies the subtile, otherwise reads the top cube's flag", "[kfx_sim][map_columns]") {
    struct Column *col = wire_column(2, 2, 5);
    set_column_floor_filled_subtiles(col, 1);
    col->cubes[0] = 3;
    kfx_config_state.conf.cube_conf.cube_cfgstats[3].properties_flags = CPF_IsUnclaimedPath;

    CHECK(subtile_is_unclaimed_path(2, 2)); // no room here, cube flagged

    make_room_at_slab(1, 0, 0, RoK_LAIR, 0); // slab (0,0) covers subtiles 0..2 for STL_PER_SLB==3
    CHECK_FALSE(subtile_is_unclaimed_path(2, 2)); // a room now occupies this subtile
}

TEST_CASE_METHOD(ResetSimAndConfig, "subtile_is_wall is true once a column's floor_filled_subtiles reaches COLUMN_WALL_HEIGHT", "[kfx_sim][map_columns]") {
    struct Column *col = wire_column(2, 2, 5);
    set_column_floor_filled_subtiles(col, COLUMN_WALL_HEIGHT - 1);
    CHECK_FALSE(subtile_is_wall(2, 2));

    set_column_floor_filled_subtiles(col, COLUMN_WALL_HEIGHT);
    CHECK(subtile_is_wall(2, 2));
}

TEST_CASE_METHOD(ResetSimAndConfig, "get_map_floor_height/get_floor_height(_at) scale floor_filled_subtiles into map coordinates", "[kfx_sim][map_columns]") {
    struct Column *col = wire_column(2, 2, 5);
    set_column_floor_filled_subtiles(col, 3);

    CHECK(get_floor_height(2, 2) == 3 * COORD_PER_STL);
    CHECK(get_map_floor_height(get_map_block_at(2, 2)) == 3 * COORD_PER_STL);

    struct Coord3d pos = {};
    pos.x.val = 2 * COORD_PER_STL;
    pos.y.val = 2 * COORD_PER_STL;
    CHECK(get_floor_height_at(&pos) == 3 * COORD_PER_STL);
}

TEST_CASE_METHOD(ResetSimAndConfig, "get_map_ceiling_height/get_ceiling_height_at(_subtile) use the ceiling cube count, or filled_subtiles when there are no ceiling cubes", "[kfx_sim][map_columns]") {
    struct Map *mapblk = get_map_block_at(2, 2);
    struct Column *col = wire_column(2, 2, 5);

    // No ceiling cubes: falls back to mapblk's own filled_subtiles.
    mapblk->filled_subtiles = 6;
    CHECK(get_ceiling_height_at_subtile(2, 2) == 6 * COORD_PER_STL);

    col->bitfields = (col->bitfields & ~CLF_CEILING_MASK) | (2 << 1);
    CHECK(get_map_ceiling_height(mapblk) == (8 - 2) * COORD_PER_STL);

    struct Coord3d pos = {};
    pos.x.val = 2 * COORD_PER_STL;
    pos.y.val = 2 * COORD_PER_STL;
    CHECK(get_ceiling_height_at(&pos) == (8 - 2) * COORD_PER_STL);
}

TEST_CASE_METHOD(ResetSimAndConfig, "subtile_is_unsafe is true for lava, or for sacrificial ground on low ground", "[kfx_sim][map_columns]") {
    struct Column *col = wire_column(2, 2, 5);
    set_column_floor_filled_subtiles(col, 1);
    col->cubes[0] = 3;

    kfx_config_state.conf.cube_conf.cube_cfgstats[3].properties_flags = 0;
    CHECK_FALSE(subtile_is_unsafe(2, 2));

    kfx_config_state.conf.cube_conf.cube_cfgstats[3].properties_flags = CPF_IsLava;
    CHECK(subtile_is_unsafe(2, 2));

    kfx_config_state.conf.cube_conf.cube_cfgstats[3].properties_flags = CPF_IsSacrificial;
    CHECK(subtile_is_unsafe(2, 2)); // cube_pos (1) < 4
}
