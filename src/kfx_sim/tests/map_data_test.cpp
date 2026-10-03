// kfx_sim coverage: a broad-coverage pass over map_data.c's coordinate
// math / Map-block accessor family -- the same shape as slab_data_test.cpp
// and dungeon_data_test.cpp's passes. Most of this file is pure
// arithmetic over kfx_sim_state.map_subtiles_x/_y or direct struct Map
// field access, so it needs nothing beyond ResetSimAndConfig's 10x10
// subtile / 4x4 tile map.
//
// Deliberately deferred: set_coords_with_range_check (and its
// set_coords_to_cylindric_shift/set_coords_add_velocity callers) route
// through slab_is_liquid/_door/_wall and get_floor_height/
// get_ceiling_height_at_subtile -- a fuller column/slab-config fixture
// than is worth building here; map_pos_is_lava/lava_at_position/
// valid_dig_position (need subtile_has_lava_on_top's cube lookup);
// subtile_is_sellable_room/_sellable_door_or_trap (need a full Room +
// door/trap-thing fixture on top of the slabmap); subtile_is_diggable_for_player
// (config-only; slab_kind_is_door reads slab_cfgstats[slb->kind] and
// get_slab_stats(slb) the same entry -- left for a dedicated increment); the slabs_reveal_slab_and_corners/slabs_change_*
// MaxCoordFilterParam callbacks and the reveal/conceal *_rect/*_area/
// clear_slab_dig family (rectangle iteration wrapping the above, plus
// ui_panel_map_update()); and set_map_size/
// init_map_size (full map (re)initialization).
#include <catch2/catch_test_macros.hpp>

#include "map_data.h"
#include "slab_data.h"
#include "config_terrain.h"
#include "kfx_sim_test_fixtures.h"
#include "ports/ui_port.h"

using namespace kfx_test;

TEST_CASE_METHOD(ResetSimAndConfig, "subtile_has_slab bounds-checks against 3*map_tiles_x/_y", "[kfx_sim][map_data]") {
    CHECK(subtile_has_slab(0, 0));
    CHECK(subtile_has_slab(11, 11)); // 3*4 - 1
    CHECK_FALSE(subtile_has_slab(-1, 0));
    CHECK_FALSE(subtile_has_slab(12, 0)); // == 3*map_tiles_x
}

TEST_CASE_METHOD(ResetSimAndConfig, "subtile_coords_invalid bounds-checks against map_subtiles_x/_y inclusive", "[kfx_sim][map_data]") {
    CHECK_FALSE(subtile_coords_invalid(0, 0));
    CHECK_FALSE(subtile_coords_invalid(10, 10)); // == map_subtiles_x/_y, inclusive
    CHECK(subtile_coords_invalid(-1, 0));
    CHECK(subtile_coords_invalid(11, 0));
}

TEST_CASE_METHOD(ResetSimAndConfig, "get_map_block_at/get_map_block_at_pos/map_block_invalid guard out-of-range coords", "[kfx_sim][map_data]") {
    CHECK(map_block_invalid(get_map_block_at(-1, 0)));
    CHECK(map_block_invalid(get_map_block_at(0, 11)));
    CHECK_FALSE(map_block_invalid(get_map_block_at(5, 5)));
    CHECK(get_map_block_at(5, 5) == get_map_block_at_pos(get_subtile_number(5, 5)));

    CHECK(map_block_invalid(get_map_block_at_pos(-1)));
    CHECK(map_block_invalid(NULL));
    CHECK(map_block_invalid(INVALID_MAP_BLOCK));
}

TEST_CASE_METHOD(ResetSimAndConfig, "get_ceiling_height scales filled_subtiles by COORD_PER_STL", "[kfx_sim][map_data]") {
    struct Coord3d pos = {};
    pos.x.stl.num = 3;
    pos.y.stl.num = 4;
    get_map_block_at(3, 4)->filled_subtiles = 5;

    CHECK(get_ceiling_height(&pos) == 5 * COORD_PER_STL);
}

TEST_CASE_METHOD(ResetSimAndConfig, "get/set_mapblk_column_index round-trips col_idx", "[kfx_sim][map_data]") {
    struct Map *mapblk = get_map_block_at(2, 2);
    set_mapblk_column_index(mapblk, 42);
    CHECK(get_mapblk_column_index(mapblk) == 42);
}

TEST_CASE_METHOD(ResetSimAndConfig, "get/set_mapblk_filled_subtiles clamps to 0..15", "[kfx_sim][map_data]") {
    struct Map *mapblk = get_map_block_at(2, 2);
    set_mapblk_filled_subtiles(mapblk, 7);
    CHECK(get_mapblk_filled_subtiles(mapblk) == 7);

    set_mapblk_filled_subtiles(mapblk, -1);
    CHECK(get_mapblk_filled_subtiles(mapblk) == 0);

    set_mapblk_filled_subtiles(mapblk, 99);
    CHECK(get_mapblk_filled_subtiles(mapblk) == 15);
}

TEST_CASE_METHOD(ResetSimAndConfig, "get/set_mapblk_wibble_value round-trips wibble_value", "[kfx_sim][map_data]") {
    struct Map *mapblk = get_map_block_at(2, 2);
    set_mapblk_wibble_value(mapblk, 3);
    CHECK(get_mapblk_wibble_value(mapblk) == 3);
}

TEST_CASE_METHOD(ResetSimAndConfig, "reveal_map_block/conceal_map_block/map_block_revealed_directly toggle a single player's revealed bit", "[kfx_sim][map_data]") {
    struct Map *mapblk = get_map_block_at(2, 2);
    CHECK_FALSE(map_block_revealed_directly(mapblk, 1));

    reveal_map_block(mapblk, 1);
    CHECK(map_block_revealed_directly(mapblk, 1));
    CHECK_FALSE(map_block_revealed_directly(mapblk, 2)); // other players unaffected

    conceal_map_block(mapblk, 1);
    CHECK_FALSE(map_block_revealed_directly(mapblk, 1));
}

TEST_CASE_METHOD(ResetSimAndConfig, "map_block_revealed with allies_share_vision off behaves like map_block_revealed_directly", "[kfx_sim][map_data]") {
    struct Map *mapblk = get_map_block_at(2, 2);
    kfx_config_state.conf.rules[1].gameplay.allies_share_vision = 0;
    CHECK_FALSE(map_block_revealed(mapblk, 1));

    reveal_map_block(mapblk, 1);
    CHECK(map_block_revealed(mapblk, 1));
    CHECK_FALSE(map_block_revealed(NULL, 1)); // invalid mapblk guard
}

TEST_CASE_METHOD(ResetSimAndConfig, "reveal_map_subtile/subtile_revealed/subtile_revealed_directly wrap the by-pointer variants for a subtile", "[kfx_sim][map_data]") {
    CHECK_FALSE(subtile_revealed(2, 2, 1));
    CHECK_FALSE(subtile_revealed_directly(2, 2, 1));

    reveal_map_subtile(2, 2, 1);
    CHECK(subtile_revealed(2, 2, 1));
    CHECK(subtile_revealed_directly(2, 2, 1));
}

TEST_CASE_METHOD(ResetSimAndConfig, "get_subtile_number/stl_num_decode_x/_y round-trip and clamp coordinates", "[kfx_sim][map_data]") {
    SubtlCodedCoords num = get_subtile_number(3, 4);
    CHECK(stl_num_decode_x(num) == 3);
    CHECK(stl_num_decode_y(num) == 4);

    // Too-large coords are clamped to map_subtiles_x+1 before encoding.
    CHECK(get_subtile_number(999, 0) == get_subtile_number(kfx_sim_state.map_subtiles_x + 1, 0));

    // Negative coords are clamped to 0.
    CHECK(get_subtile_number(-1, 0) == get_subtile_number(0, 0));
}

TEST_CASE_METHOD(ResetSimAndConfig, "get_subtile_number_at_slab_center resolves to the center subtile of the given slab", "[kfx_sim][map_data]") {
    CHECK(get_subtile_number_at_slab_center(1, 1) == get_subtile_number(STL_PER_SLB + 1, STL_PER_SLB + 1));
}

TEST_CASE_METHOD(ResetSimAndConfig, "stl_slab_center/_starting/_ending_subtile resolve a subtile's slab to its center/first/last subtile", "[kfx_sim][map_data]") {
    // Subtile 4 is in slab 1 (subtiles 3..5, given STL_PER_SLB == 3).
    MapSubtlCoord stl_v = STL_PER_SLB + 1;
    CHECK(stl_slab_center_subtile(stl_v) == STL_PER_SLB * 1 + 1);
    CHECK(stl_slab_starting_subtile(stl_v) == STL_PER_SLB * 1);
    CHECK(stl_slab_ending_subtile(stl_v) == STL_PER_SLB * 1 + STL_PER_SLB - 1);
}

TEST_CASE_METHOD(ResetSimAndConfig, "clear_mapwho zeroes every map block's mapwho field", "[kfx_sim][map_data]") {
    get_map_block_at(2, 2)->mapwho = 5;
    get_map_block_at(7, 7)->mapwho = 9;

    clear_mapwho();

    CHECK(get_map_block_at(2, 2)->mapwho == 0);
    CHECK(get_map_block_at(7, 7)->mapwho == 0);
}

TEST_CASE_METHOD(ResetSimAndConfig, "set_coords_to_subtile_center centers coordinates and clamps out-of-range subtiles", "[kfx_sim][map_data]") {
    struct Coord3d pos = {};
    CHECK(set_coords_to_subtile_center(&pos, 2, 3, 1));
    CHECK(pos.x.stl.num == 2);
    CHECK(pos.y.stl.num == 3);
    CHECK(pos.x.stl.pos == COORD_PER_STL / 2);

    // Clamped rather than rejected: always returns true.
    CHECK(set_coords_to_subtile_center(&pos, -5, 999, -5));
    CHECK(pos.x.stl.num == 0);
    CHECK(pos.y.stl.num == kfx_sim_state.map_subtiles_y + 1);
}

TEST_CASE_METHOD(ResetSimAndConfig, "set_coords_to_slab_center centers on the given slab's center subtile", "[kfx_sim][map_data]") {
    struct Coord3d pos = {};
    CHECK(set_coords_to_slab_center(&pos, 1, 1));
    CHECK(pos.x.stl.num == slab_subtile_center(1));
    CHECK(pos.y.stl.num == slab_subtile_center(1));
}

TEST_CASE_METHOD(ResetSimAndConfig, "subtile_is_room/subtile_is_player_room read the SlbAtFlg_IsRoom map flag and slab owner", "[kfx_sim][map_data]") {
    CHECK_FALSE(subtile_is_room(2, 2));
    CHECK_FALSE(subtile_is_player_room(0, 2, 2));

    get_map_block_at(2, 2)->flags |= SlbAtFlg_IsRoom;
    CHECK(subtile_is_room(2, 2));
    CHECK(subtile_is_player_room(0, 2, 2)); // slab owner defaults to 0
    CHECK_FALSE(subtile_is_player_room(3, 2, 2));

    get_slabmap_for_subtile(2, 2)->owner = 3;
    CHECK(subtile_is_player_room(3, 2, 2));
    CHECK_FALSE(subtile_is_player_room(0, 2, 2));
}

TEST_CASE_METHOD(ResetSimAndConfig, "subtile_is_door reads the SlbAtFlg_IsDoor map flag", "[kfx_sim][map_data]") {
    CHECK_FALSE(subtile_is_door(2, 2));
    get_map_block_at(2, 2)->flags |= SlbAtFlg_IsDoor;
    CHECK(subtile_is_door(2, 2));
}

// reveal_map_area()/conceal_map_area() refresh the minimap by calling
// ui_panel_map_update(x, y, w, h) -- a position plus a
// WIDTH/HEIGHT, not an end corner (upstream #5302 fixed the calls that passed
// end coordinates). The rect they touch is half-open ([start, end)), so
// w = end - start.
namespace {
struct PanelMapUpdateSpy {
    static inline int64_t calls;
    static inline int64_t x, y, w, h;
    static void record(int64_t px, int64_t py, int64_t pw, int64_t ph) { calls++; x = px; y = py; w = pw; h = ph; }

    struct UiPort callbacks;
    PanelMapUpdateSpy() {
        callbacks = *ui_port; // keep the default no-ops for everything else
        calls = 0; x = y = w = h = -1;
        callbacks.panel_map_update = &PanelMapUpdateSpy::record;
        set_ui_port(&callbacks);
    }
    ~PanelMapUpdateSpy() { set_ui_port(nullptr); }
};
}

TEST_CASE_METHOD(ResetSimAndConfig, "reveal_map_area reports its refreshed rect to panel_map_update as x,y,w,h", "[kfx_sim][map_data]") {
    PanelMapUpdateSpy spy;
    // slabs 1..1 (subtiles 3..5) on a 10x10-subtile map -> half-open rect [3,6) x [3,6)
    reveal_map_area(0, 3, 5, 3, 5);
    REQUIRE(PanelMapUpdateSpy::calls >= 1); // per-subtile helpers may refresh too; the area call is the last one
    CHECK(PanelMapUpdateSpy::x == 3);
    CHECK(PanelMapUpdateSpy::y == 3);
    CHECK(PanelMapUpdateSpy::w == 3);
    CHECK(PanelMapUpdateSpy::h == 3);
}

TEST_CASE_METHOD(ResetSimAndConfig, "conceal_map_area reports its refreshed rect to panel_map_update as x,y,w,h", "[kfx_sim][map_data]") {
    PanelMapUpdateSpy spy;
    conceal_map_area(0, 3, 5, 3, 5, true);
    REQUIRE(PanelMapUpdateSpy::calls >= 1); // per-subtile helpers may refresh too; the area call is the last one
    CHECK(PanelMapUpdateSpy::x == 3);
    CHECK(PanelMapUpdateSpy::y == 3);
    CHECK(PanelMapUpdateSpy::w == 3);
    CHECK(PanelMapUpdateSpy::h == 3);
}
