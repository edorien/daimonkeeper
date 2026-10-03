// kfx_sim coverage: a first pass over map_blocks.c's slab-neighbor and
// map-revealing helpers.
//
// block_has_diggable_side/block_count_diggable_sides both route through
// get_slab_stats(slb). The fixture's neighbors are all one slab kind, so
// every one of the 4 small_around neighbors reads the same is_safe_land
// flag; the tests below exercise the "all 4 count" and "none count" cases,
// not per-neighbor variation. (Before refactor pass 2's S15,
// get_slab_stats() always resolved to slab_cfgstats[0] in this binary.)
//
// set_slab_explored_flags/torch_flags_for_slab weren't declared in
// map_blocks.h (only forward-declared inside map_blocks.c) -- added
// alongside set_slab_explored's existing declaration.
#include <catch2/catch_test_macros.hpp>

#include "map_blocks.h"
#include "map_data.h"
#include "slab_data.h"
#include "config_terrain.h"
#include "kfx_sim_test_fixtures.h"

using namespace kfx_test;

TEST_CASE_METHOD(ResetSimAndConfig, "block_has_diggable_side/block_count_diggable_sides read is_safe_land through slab_cfgstats[0] for every neighbor", "[kfx_sim][map_blocks]") {
    CHECK_FALSE(block_has_diggable_side(1, 1));
    CHECK(block_count_diggable_sides(1, 1) == 0);

    kfx_config_state.conf.slab_conf.slab_cfgstats[0].is_safe_land = 1;
    CHECK(block_has_diggable_side(1, 1));
    CHECK(block_count_diggable_sides(1, 1) == 4); // SMALL_AROUND_SLAB_LENGTH
}

TEST_CASE_METHOD(ResetSimAndConfig, "set_slab_explored reveals every subtile of a slab once, refusing the neutral player or an already-revealed slab", "[kfx_sim][map_blocks]") {
    CHECK_FALSE(set_slab_explored(PLAYER_NEUTRAL, 1, 1)); // neutral player never gets reveals
    CHECK_FALSE(subtile_revealed_directly(slab_subtile_center(1), slab_subtile_center(1), PLAYER_NEUTRAL));

    CHECK(set_slab_explored(0, 1, 1));
    for (MapSubtlCoord x = 0; x < STL_PER_SLB; x++) {
        for (MapSubtlCoord y = 0; y < STL_PER_SLB; y++) {
            CHECK(subtile_revealed_directly(slab_subtile(1, x), slab_subtile(1, y), 0));
        }
    }

    CHECK_FALSE(set_slab_explored(0, 1, 1)); // center subtile already revealed: nothing left to do
}

TEST_CASE_METHOD(ResetSimAndConfig, "set_slab_explored_flags stamps every map block of a slab with the player's revealed bit", "[kfx_sim][map_blocks]") {
    set_slab_explored_flags(1, 1, 1); // slab (1,1) covers subtiles 3..5
    for (MapSubtlCoord x = 3; x <= 5; x++) {
        for (MapSubtlCoord y = 3; y <= 5; y++) {
            CHECK(get_map_block_at(x, y)->revealed == to_flag(1));
        }
    }
    CHECK(get_map_block_at(0, 0)->revealed == 0); // untouched outside the slab
}

TEST_CASE_METHOD(ResetSimAndConfig, "torch_flags_for_slab sets 0x01 when a slab on a 5-slab X gridline borders claimed ground", "[kfx_sim][map_blocks]") {
    // slb_x == 0 is on the gridline (0 % 5 == 0); slb_y == 2 is not.
    CHECK(torch_flags_for_slab(0, 2) == 0);

    get_slabmap_block(0, 3)->kind = SlbT_CLAIMED; // (slb_x, slb_y+1)
    CHECK(torch_flags_for_slab(0, 2) == 0x01);
}

TEST_CASE_METHOD(ResetSimAndConfig, "torch_flags_for_slab sets 0x02 when a slab on a 5-slab Y gridline borders claimed ground", "[kfx_sim][map_blocks]") {
    // slb_y == 0 is on the gridline; slb_x == 2 is not.
    CHECK(torch_flags_for_slab(2, 0) == 0);

    get_slabmap_block(3, 0)->kind = SlbT_CLAIMED; // (slb_x+1, slb_y)
    CHECK(torch_flags_for_slab(2, 0) == 0x02);
}
