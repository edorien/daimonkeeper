// kfx_sim: roomspace.c's pure-geometry and affordability helpers, per
// docs/refactor/testing/comprehensive/stage-08-comprehensive-library-
// passes.md's kfx_sim row. The covered half of roomspace.c so far
// (roomspace_test.cpp) exercised update_slab_grid's drag-mode grid fill;
// these are the *other* entry points: box construction math, direction
// detection, bridge-shape detection, and the slab-level bridge
// blockability queries the liquid-bridge build check uses.
//
// can_afford_roomspace had no header declaration (only ever called from
// within roomspace.c itself) -- added to roomspace.h, the same "add the
// missing declaration" fix used for magic_powers.h earlier in this plan.
//
// The wall/bridgeable slab cases key off two real config-driven checks:
// slab_is_wall() counts a slab as wall when its probed subtiles' columns
// each report floor-filled >= COLUMN_WALL_HEIGHT (the top nibble of
// Column::bitfields), and slab_kind_is_bridgeable() is purely
// kfx_config_state.conf.slab_conf.slab_cfgstats[].wlb_type (neither
// WlbT_None nor WlbT_Bridge) -- so both are driven by writing the real
// arrays, no stubs.
#include <catch2/catch_test_macros.hpp>

#include "globals.h"
#include "slab_data.h"
#include "roomspace.h"
#include "map_columns.h"
#include "map_data.h"
#include "player_data.h"
#include "dungeon_data.h"
#include "kfx_sim_state.h"
#include "kfx_config_state.h"
#include "config_terrain.h"

#include "kfx_sim_test_fixtures.h"

namespace {
// Column's top nibble is its floor-filled subtile count; COLUMN_WALL_HEIGHT
// is 5, so 0x50 marks a fully solid (wall) column.
constexpr unsigned char kWallColumnBitfields = COLUMN_WALL_HEIGHT << 4;

// Marks every column of the slab's STL_PER_SLB x STL_PER_SLB subtiles at
// (slb_x, slb_y) as wall (or back to plain air), which covers what
// slab_is_wall()'s small_around probe of the slab's centre sees. Every map
// block's col_idx defaults to 0 (ResetSimAndConfig's zero-fill), which is
// the reserved "invalid" sentinel slot (INVALID_COLUMN ==
// &columns_data[0], per map_columns.h) -- writing straight through
// get_column_at() on an unwired subtile is therefore a silent no-op, so
// this wires a real, non-zero column index into each subtile first (same
// pattern as map_columns_test.cpp's wire_column helper).
void set_slab_all_wall(MapSlabCoord slb_x, MapSlabCoord slb_y, bool wall) {
    long col_idx = 1 + slb_x + slb_y * MAX_ROOMSPACE_WIDTH;
    struct Column *col = get_column(col_idx);
    col->bitfields = wall ? kWallColumnBitfields : 0;
    for (MapSlabCoord sy = 0; sy < STL_PER_SLB; ++sy) {
        for (MapSlabCoord sx = 0; sx < STL_PER_SLB; ++sx) {
            get_map_block_at(slab_subtile(slb_x, sx), slab_subtile(slb_y, sy))->col_idx = col_idx;
        }
    }
}
} // namespace

TEST_CASE_METHOD(kfx_test::ResetSimAndConfig, "calc_distance_from_roomspace_centre is (d-1)/2 for odd widths and asymmetric for even", "[kfx_sim][roomspace]") {
    // odd: offset is irrelevant, (d-1)/2 either way
    CHECK(calc_distance_from_roomspace_centre(5, false) == 2);
    CHECK(calc_distance_from_roomspace_centre(5, true) == 2);
    CHECK(calc_distance_from_roomspace_centre(1, false) == 0);
    // even: offset 0 gives (d-1)/2 (floor), offset 1 gives d/2 -- the
    // caller passes (d % 2 == 0) as offset, so an even width's box
    // extends one further to the right/bottom than to the left/top
    CHECK(calc_distance_from_roomspace_centre(4, false) == 1);
    CHECK(calc_distance_from_roomspace_centre(4, true) == 2);
    CHECK(calc_distance_from_roomspace_centre(2, false) == 0);
    CHECK(calc_distance_from_roomspace_centre(2, true) == 1);
}

TEST_CASE_METHOD(kfx_test::ResetSimAndConfig, "create_box_roomspace centres an odd width/height symmetrically around the centre", "[kfx_sim][roomspace]") {
    struct RoomSpace rs{};
    rs = create_box_roomspace(rs, 5, 3, 5, 5);
    CHECK(rs.left == 3);
    CHECK(rs.right == 7);
    CHECK(rs.top == 4);
    CHECK(rs.bottom == 6);
    CHECK(rs.width == 5);
    CHECK(rs.height == 3);
    CHECK(rs.slab_count == 15);
    CHECK(rs.centreX == 5);
    CHECK(rs.centreY == 5);
    CHECK(rs.is_roomspace_a_box);
    CHECK(rs.render_roomspace_as_box);
    CHECK(!rs.is_roomspace_a_single_subtile);
    CHECK(!rs.drag_mode);
    // the grid starts blank
    CHECK(!rs.slab_grid[0][0]);
    CHECK(!rs.slab_grid[4][2]);
}

TEST_CASE_METHOD(kfx_test::ResetSimAndConfig, "create_box_roomspace for an even width extends one slab further right/bottom than left/top", "[kfx_sim][roomspace]") {
    struct RoomSpace rs{};
    rs = create_box_roomspace(rs, 4, 4, 5, 5);
    // left = 5 - (4-1)/2 = 4, right = 5 + (4-1+1)/2 = 7
    CHECK(rs.left == 4);
    CHECK(rs.right == 7);
    CHECK(rs.top == 4);
    CHECK(rs.bottom == 7);
    CHECK(rs.slab_count == 16);
}

TEST_CASE_METHOD(kfx_test::ResetSimAndConfig, "can_afford_roomspace compares slab_count * room cost against the dungeon's gold", "[kfx_sim][roomspace]") {
    kfx_test::make_player_active(0);
    kfx_config_state.conf.slab_conf.room_types_count = 1;
    kfx_config_state.conf.slab_conf.room_cfgstats[0].cost = 10;

    kfx_sim_state.dungeon[0].total_money_owned = 49;
    CHECK_FALSE(can_afford_roomspace(0, 0, 5)); // 5 * 10 = 50 > 49

    kfx_sim_state.dungeon[0].total_money_owned = 50;
    CHECK(can_afford_roomspace(0, 0, 5)); // exactly affordable

    kfx_sim_state.dungeon[0].total_money_owned = 1000;
    CHECK(can_afford_roomspace(0, 0, 0)); // zero slabs is always affordable
}

TEST_CASE_METHOD(kfx_test::ResetSimAndConfig, "can_build_fancy_roomspace returns the slab count when affordable and 0 when not", "[kfx_sim][roomspace]") {
    kfx_test::make_player_active(0);
    kfx_config_state.conf.slab_conf.room_types_count = 1;
    kfx_config_state.conf.slab_conf.room_cfgstats[0].cost = 10;

    struct RoomSpace rs{};
    rs = create_box_roomspace(rs, 3, 3, 2, 2);

    kfx_sim_state.dungeon[0].total_money_owned = 89;
    CHECK(can_build_fancy_roomspace(0, 0, rs) == 0); // 9 * 10 = 90 > 89

    kfx_sim_state.dungeon[0].total_money_owned = 90;
    CHECK(can_build_fancy_roomspace(0, 0, rs) == 9);
}

TEST_CASE_METHOD(kfx_test::ResetSimAndConfig, "detect_roomspace_direction classifies the drag rectangle's four corner orientations", "[kfx_sim][roomspace]") {
    struct RoomSpace rs{};

    // dragged from top-left corner to bottom-right corner
    rs.drag_start_x = 1; rs.drag_start_y = 1;
    rs.drag_end_x = 4;   rs.drag_end_y = 4;
    detect_roomspace_direction(&rs);
    CHECK(rs.drag_direction == top_left_to_bottom_right);

    // from bottom-right to top-left (the reversed drag)
    rs.drag_start_x = 4; rs.drag_start_y = 4;
    rs.drag_end_x = 1;   rs.drag_end_y = 1;
    detect_roomspace_direction(&rs);
    CHECK(rs.drag_direction == bottom_right_to_top_left);

    // from top-right to bottom-left
    rs.drag_start_x = 4; rs.drag_start_y = 1;
    rs.drag_end_x = 1;   rs.drag_end_y = 4;
    detect_roomspace_direction(&rs);
    CHECK(rs.drag_direction == top_right_to_bottom_left);

    // from bottom-left to top-right
    rs.drag_start_x = 1; rs.drag_start_y = 4;
    rs.drag_end_x = 4;   rs.drag_end_y = 1;
    detect_roomspace_direction(&rs);
    CHECK(rs.drag_direction == bottom_left_to_top_right);

    // a zero-size (click, no drag) stays top_left_to_bottom_right
    rs.drag_start_x = 2; rs.drag_start_y = 2;
    rs.drag_end_x = 2;   rs.drag_end_y = 2;
    detect_roomspace_direction(&rs);
    CHECK(rs.drag_direction == top_left_to_bottom_right);
}

TEST_CASE_METHOD(kfx_test::ResetSimAndConfig, "detect_bridge_shape sets horizontal-first and L-shape from the drag rectangle", "[kfx_sim][roomspace]") {
    struct PlayerInfo *player = kfx_test::make_player_active(0);

    // a strictly horizontal drag: horizontal-first, and (no y delta) the
    // l_shape is left as-is by the function
    player->roomspace_horizontal_first = false;
    player->roomspace_l_shape = 7; // sentinel: must survive unchanged
    player->render_roomspace.drag_start_x = 1; player->render_roomspace.drag_start_y = 1;
    player->render_roomspace.drag_end_x = 4;   player->render_roomspace.drag_end_y = 1;
    detect_bridge_shape(0);
    CHECK(player->roomspace_horizontal_first);
    CHECK(player->roomspace_l_shape == 7);

    // a horizontal-then-down L: horizontal-first stays, l_shape becomes 0
    player->render_roomspace.drag_end_y = 3;
    detect_bridge_shape(0);
    CHECK(player->roomspace_horizontal_first);
    CHECK(player->roomspace_l_shape == 0);

    // a strictly vertical drag: the else-if clears horizontal-first and
    // the final else sets l_shape 1
    player->roomspace_horizontal_first = true;
    player->render_roomspace.drag_start_x = 1; player->render_roomspace.drag_start_y = 1;
    player->render_roomspace.drag_end_x = 1;   player->render_roomspace.drag_end_y = 4;
    detect_bridge_shape(0);
    CHECK_FALSE(player->roomspace_horizontal_first);
    CHECK(player->roomspace_l_shape == 1);

    // a zero-size drag: neither branch fires -- horizontal-first keeps its
    // stale value (documenting the actual behavior), and l_shape takes the
    // else-branch value because horizontal-first is still true
    player->roomspace_horizontal_first = true;
    player->render_roomspace.drag_start_x = 2; player->render_roomspace.drag_start_y = 2;
    player->render_roomspace.drag_end_x = 2;   player->render_roomspace.drag_end_y = 2;
    detect_bridge_shape(0);
    CHECK(player->roomspace_horizontal_first); // stale value survives
}

TEST_CASE_METHOD(kfx_test::ResetSimAndConfig, "roomspace_slab_blocks_bridge reports a wall slab as blocking", "[kfx_sim][roomspace]") {
    kfx_test::make_player_active(0);
    set_slab_all_wall(1, 1, true);
    CHECK(roomspace_slab_blocks_bridge(0, 1, 1));
    // walls block regardless of the asking player
    CHECK(roomspace_slab_blocks_bridge(1, 1, 1));
}

TEST_CASE_METHOD(kfx_test::ResetSimAndConfig, "roomspace_slab_blocks_bridge lets liquid slabs through without consulting ownership", "[kfx_sim][roomspace]") {
    kfx_test::make_player_active(0);
    // make slab kind 1 a liquid (WlbT_Lava): bridgeable means
    // wlb_type set and not WlbT_Bridge
    kfx_config_state.conf.slab_conf.slab_types_count = 2;
    kfx_config_state.conf.slab_conf.slab_cfgstats[1].wlb_type = WlbT_Lava;
    get_slabmap_block(1, 1)->kind = 1;

    // ownership is deliberately left neutral: a bridgeable slab is never
    // owner-checked, so an enemy-owned liquid slab is still bridgeable
    get_slabmap_block(1, 1)->owner = 1;
    CHECK_FALSE(roomspace_slab_blocks_bridge(0, 1, 1));
}

TEST_CASE_METHOD(kfx_test::ResetSimAndConfig, "roomspace_slab_blocks_bridge checks ownership only for non-liquid slabs", "[kfx_sim][roomspace]") {
    kfx_test::make_player_active(0);
    // slab kind 0's wlb_type stays WlbT_None -> not bridgeable -> owner matters
    get_slabmap_block(1, 1)->kind = 0;
    get_slabmap_block(1, 1)->owner = 0;
    CHECK_FALSE(roomspace_slab_blocks_bridge(0, 1, 1)); // own ground

    get_slabmap_block(1, 1)->owner = 1;
    CHECK(roomspace_slab_blocks_bridge(0, 1, 1)); // enemy ground
}

TEST_CASE_METHOD(kfx_test::ResetSimAndConfig, "roomspace_liquid_path_is_blocked is false across a fully bridgeable run of slabs", "[kfx_sim][roomspace]") {
    kfx_test::make_player_active(0);
    kfx_config_state.conf.slab_conf.slab_types_count = 2;
    kfx_config_state.conf.slab_conf.slab_cfgstats[1].wlb_type = WlbT_Lava;
    // slabs 0..3 along y=1 are all liquid
    for (MapSlabCoord x = 0; x < 4; ++x) {
        get_slabmap_block(x, 1)->kind = 1;
    }
    CHECK_FALSE(roomspace_liquid_path_is_blocked(0, 0, 3, 1, false)); // horizontal, low to high
    CHECK_FALSE(roomspace_liquid_path_is_blocked(0, 3, 0, 1, false)); // reversed direction

    // a fully liquid vertical run at x=2
    for (MapSlabCoord y = 0; y < 4; ++y) {
        get_slabmap_block(2, y)->kind = 1;
    }
    CHECK_FALSE(roomspace_liquid_path_is_blocked(0, 1, 3, 2, true));  // vertical, low to high
    CHECK_FALSE(roomspace_liquid_path_is_blocked(0, 3, 1, 2, true));  // reversed vertical
}

TEST_CASE_METHOD(kfx_test::ResetSimAndConfig, "roomspace_liquid_path_is_blocked reports a blocking slab in the middle of the run", "[kfx_sim][roomspace]") {
    kfx_test::make_player_active(0);
    kfx_config_state.conf.slab_conf.slab_types_count = 2;
    kfx_config_state.conf.slab_conf.slab_cfgstats[1].wlb_type = WlbT_Lava;
    // slabs 0..3 along y=1: liquid, liquid, enemy solid, liquid
    get_slabmap_block(0, 1)->kind = 1;
    get_slabmap_block(1, 1)->kind = 1;
    get_slabmap_block(2, 1)->kind = 0;
    get_slabmap_block(2, 1)->owner = 1; // not player 0's ground -> blocks
    get_slabmap_block(3, 1)->kind = 1;

    CHECK(roomspace_liquid_path_is_blocked(0, 0, 3, 1, false));
    CHECK(roomspace_liquid_path_is_blocked(0, 3, 0, 1, false)); // scan direction doesn't matter
}
