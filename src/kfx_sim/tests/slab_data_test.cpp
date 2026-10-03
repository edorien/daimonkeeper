// kfx_sim coverage, per docs/refactor/testing/comprehensive/
// stage-08b-kfx-sim-clusters.md's "still open" list: a broad-coverage pass
// over slab_data.c's accessor/coordinate/kind-check family -- the same
// shape as dungeon_data_test.cpp's pass. slab_is_safe_land/
// slab_good_for_computer_dig_path/is_valid_hug_subtile are not covered
// yet: they route through get_slab_stats(), which until refactor pass 2's
// S15 always resolved to slab_cfgstats[0] in this binary (it reached the
// slab through an unwired port). It now reads the real slab kind, so they
// can be tested with a per-kind slab_cfgstats fixture.
#include <catch2/catch_test_macros.hpp>

#include "slab_data.h"
#include "dungeon_data.h"
#include "thing_data.h"
#include "map_columns.h"
#include "map_data.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"

#include <cstring>

namespace {
struct ResetState {
    ResetState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        kfx_sim_state.map_tiles_x = 4;
        kfx_sim_state.map_tiles_y = 4;
        kfx_sim_state.map_subtiles_x = 10;
        kfx_sim_state.map_subtiles_y = 10;
    }
};
}

TEST_CASE_METHOD(ResetState, "get_slab_number/slb_num_decode_x/_y clamp and round-trip slab coordinates", "[kfx_sim][slab_data]") {
    CHECK(get_slab_number(1, 2) == 2 * 4 + 1);
    CHECK(get_slab_number(-1, -1) == 0); // clamped up to 0
    CHECK(get_slab_number(99, 99) == get_slab_number(4, 4)); // clamped down to map_tiles_x/_y

    SlabCodedCoords slb_num = get_slab_number(2, 3);
    CHECK(slb_num_decode_x(slb_num) == 2);
    CHECK(slb_num_decode_y(slb_num) == 3);
}

TEST_CASE_METHOD(ResetState, "get_slabmap_direct/_block/_for_subtile bounds-check and agree on the same slot", "[kfx_sim][slab_data]") {
    CHECK(get_slabmap_direct(kfx_sim_state.map_tiles_x * kfx_sim_state.map_tiles_y) == INVALID_SLABMAP_BLOCK);
    CHECK(get_slabmap_block(-1, 0) == INVALID_SLABMAP_BLOCK);
    CHECK(get_slabmap_block(0, -1) == INVALID_SLABMAP_BLOCK);
    CHECK(get_slabmap_for_subtile(-1, 0) == INVALID_SLABMAP_BLOCK);

    struct SlabMap *by_block = get_slabmap_block(1, 2);
    CHECK(get_slabmap_direct(get_slab_number(1, 2)) == by_block);
    CHECK(get_slabmap_for_subtile(slab_subtile_center(1), slab_subtile_center(2)) == by_block);
}

TEST_CASE_METHOD(ResetState, "get_slabmap_thing_is_on/get_slab_owner_thing_is_on resolve through the thing's mappos", "[kfx_sim][slab_data]") {
    kfx_config_state.neutral_player_num = PLAYER_NEUTRAL;
    struct Thing *thing = thing_get(1);

    CHECK(get_slabmap_thing_is_on(thing) == get_slabmap_block(0, 0)); // default mappos is (0,0)
    CHECK(get_slab_owner_thing_is_on(thing) == 0); // reads the real (fresh, owner-0) slab
    get_slabmap_block(0, 0)->owner = 3;
    CHECK(get_slab_owner_thing_is_on(thing) == 3);

    struct Thing *invalid_thing = nullptr;
    CHECK(get_slabmap_thing_is_on(invalid_thing) == INVALID_SLABMAP_BLOCK);
    CHECK(get_slab_owner_thing_is_on(invalid_thing) == PLAYER_NEUTRAL); // the actual invalid-thing fallback
}

TEST_CASE_METHOD(ResetState, "slabmap_block_invalid/slab_coords_invalid bounds-check pointer identity and coordinates", "[kfx_sim][slab_data]") {
    CHECK(slabmap_block_invalid(nullptr));
    CHECK(slabmap_block_invalid(INVALID_SLABMAP_BLOCK));
    CHECK_FALSE(slabmap_block_invalid(&kfx_sim_state.slabmap[0]));

    CHECK(slab_coords_invalid(-1, 0));
    CHECK(slab_coords_invalid(0, kfx_sim_state.map_tiles_y));
    CHECK_FALSE(slab_coords_invalid(0, 0));
}

TEST_CASE_METHOD(ResetState, "slabmap_owner falls back to PLAYER_NEUTRAL for an invalid slab, else reads slb->owner", "[kfx_sim][slab_data]") {
    CHECK(slabmap_owner(INVALID_SLABMAP_BLOCK) == PLAYER_NEUTRAL);
    struct SlabMap *slb = get_slabmap_block(0, 0);
    slb->owner = 4;
    CHECK(slabmap_owner(slb) == 4);
}

TEST_CASE_METHOD(ResetState, "slabmap_kind reads slb->kind directly, with no invalid-pointer guard", "[kfx_sim][slab_data]") {
    struct SlabMap *slb = get_slabmap_block(0, 0);
    slb->kind = SlbT_CLAIMED;
    CHECK(slabmap_kind(slb) == SlbT_CLAIMED);
}

TEST_CASE_METHOD(ResetState, "set_slab_owner writes owner, picks slab_ext_data from the new owner's texture_pack, and no-ops on an out-of-range slab", "[kfx_sim][slab_data]") {
    struct Dungeon *dungeon = get_dungeon(2);
    dungeon->texture_pack = 7;

    set_slab_owner(1, 1, 2);

    struct SlabMap *slb = get_slabmap_block(1, 1);
    CHECK(slb->owner == 2);
    CHECK(kfx_config_state.slab_ext_data[get_slab_number(1, 1)] == 7);

    set_slab_owner(-1, -1, 5); // out of range -- must not crash or touch anything
    CHECK(true);
}

TEST_CASE_METHOD(ResetState, "set_slab_owner falls back to the initial ext data when the new owner has no texture_pack", "[kfx_sim][slab_data]") {
    kfx_config_state.slab_ext_data_initial[get_slab_number(1, 1)] = 9;
    // dungeon(0).texture_pack defaults to 0

    set_slab_owner(1, 1, 0);

    CHECK(kfx_config_state.slab_ext_data[get_slab_number(1, 1)] == 9);
}

TEST_CASE_METHOD(ResetState, "slabmap_wlb/slabmap_set_wlb round-trip through an invalid-pointer guard", "[kfx_sim][slab_data]") {
    CHECK(slabmap_wlb(INVALID_SLABMAP_BLOCK) == WlbT_None);
    struct SlabMap *slb = get_slabmap_block(0, 0);
    slabmap_set_wlb(slb, 3);
    CHECK(slabmap_wlb(slb) == 3);
}

TEST_CASE_METHOD(ResetState, "get_next_slab_number_in_room reads next_in_room, or errors out past the map's slab count", "[kfx_sim][slab_data]") {
    struct SlabMap *slb = get_slabmap_block(1, 1);
    slb->next_in_room = 42;
    CHECK(get_next_slab_number_in_room(get_slab_number(1, 1)) == 42);
    CHECK(get_next_slab_number_in_room(kfx_sim_state.map_tiles_x * kfx_sim_state.map_tiles_y) == 0);
}

TEST_CASE_METHOD(ResetState, "slab_is_door/slab_is_liquid read the slab's own kind", "[kfx_sim][slab_data]") {
    kfx_config_state.conf.slab_conf.slab_types_count = 2;
    kfx_config_state.conf.slab_conf.slab_cfgstats[1].block_flags = SlbAtFlg_IsDoor;
    struct SlabMap *slb = get_slabmap_block(0, 0);

    slb->kind = 1;
    CHECK(slab_is_door(0, 0));
    slb->kind = SlbT_CLAIMED;
    CHECK_FALSE(slab_is_door(0, 0));

    slb->kind = SlbT_WATER;
    CHECK(slab_is_liquid(0, 0));
    slb->kind = SlbT_LAVA;
    CHECK(slab_is_liquid(0, 0));
    slb->kind = SlbT_CLAIMED;
    CHECK_FALSE(slab_is_liquid(0, 0));
}

TEST_CASE_METHOD(ResetState, "is_slab_type_walkable/slab_kind_is_animated read the slab kind's config flags", "[kfx_sim][slab_data]") {
    kfx_config_state.conf.slab_conf.slab_types_count = 2;

    kfx_config_state.conf.slab_conf.slab_cfgstats[1].block_flags = 0;
    CHECK(is_slab_type_walkable(1));
    kfx_config_state.conf.slab_conf.slab_cfgstats[1].block_flags = SlbAtFlg_Blocking;
    CHECK_FALSE(is_slab_type_walkable(1));

    kfx_config_state.conf.slab_conf.slab_cfgstats[1].animated = 0;
    CHECK_FALSE(slab_kind_is_animated(1));
    kfx_config_state.conf.slab_conf.slab_cfgstats[1].animated = 1;
    CHECK(slab_kind_is_animated(1));
}

TEST_CASE_METHOD(ResetState, "clear_slabs resets every slab within map_tiles_x/_y to SlbT_ROCK", "[kfx_sim][slab_data]") {
    struct SlabMap *slb = get_slabmap_block(1, 1);
    slb->kind = SlbT_CLAIMED;
    slb->owner = 3;

    clear_slabs();

    CHECK(slb->kind == SlbT_ROCK);
    CHECK(slb->owner == 0);
}
