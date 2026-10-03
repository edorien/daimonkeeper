// kfx_sim "creature" cluster, hard-tail depth increment: the first test
// for creature_states_prisn.c (previously untouched, per
// stage-08b's "13 other creature_states_*.c files remain completely
// untouched" note). jailbreak_possible is the one function in this file
// that isn't a process_state/cleanup_state entry point needing a full
// Room+job fixture -- it walks a real room->slabs_list chain (the same
// get_next_slab_number_in_room mechanism room_lair_test.cpp/
// tasks_list_test.cpp already exercise) and calls slab_by_players_land.
//
// slab_by_players_land's slab_is_safe_land call routes through
// get_slab_stats(). The fixture's slabs are kind 0, so is_safe_land is set
// via slab_cfgstats[0], while slabmap_owner/slab_is_liquid (reading
// slb->kind/slb->owner directly) reflect the per-slab state set up here.
//
// The escaping player is tested as player 3, not 5: PLAYER_NEUTRAL is 5,
// and the fixture sets neutral_player_num to that same value (needed so
// room.owner==0 below doesn't itself collide with a zeroed
// neutral_player_num) -- an earlier draft used player 5 as the escaping
// creature's owner too and silently hit jailbreak_possible's own
// neutral-player early return, caught by a CAPTURE probe showing
// slab_by_players_land itself returning true while the wrapping function
// still returned false, before switching to player 3.
#include <catch2/catch_test_macros.hpp>

#include "creature_states_prisn.h"
#include "room_data.h"
#include "slab_data.h"
#include "globals.h"
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
        kfx_config_state.neutral_player_num = PLAYER_NEUTRAL;
        // A safe-land slab kind, uniformly applied via slab_cfgstats[0]
        // regardless of which real slab is queried (see file banner).
        kfx_config_state.conf.slab_conf.slab_cfgstats[0].is_safe_land = 1;
    }
};
}

TEST_CASE_METHOD(ResetState, "jailbreak_possible is false for the neutral player or the room's own owner", "[kfx_sim][creature_states_prisn]") {
    struct Room room{};
    room.owner = 2;

    CHECK_FALSE(jailbreak_possible(&room, PLAYER_NEUTRAL));
    CHECK_FALSE(jailbreak_possible(&room, 2)); // creature_owner == room.owner
}

TEST_CASE_METHOD(ResetState, "jailbreak_possible is false when no neighboring slab of the prison is enemy-owned safe land", "[kfx_sim][creature_states_prisn]") {
    struct Room room{};
    room.owner = 2;
    room.slabs_list = get_slab_number(1, 1);
    get_slabmap_block(1, 1)->next_in_room = 0; // single-slab room

    CHECK_FALSE(jailbreak_possible(&room, 3)); // no neighbor owned by player 3 at all
}

TEST_CASE_METHOD(ResetState, "jailbreak_possible is true once a neighboring slab is owned by the escaping player, safe, and not liquid", "[kfx_sim][creature_states_prisn]") {
    struct Room room{};
    room.owner = 2;
    room.slabs_list = get_slab_number(1, 1);
    get_slabmap_block(1, 1)->next_in_room = 0;

    // Slab (2,1) is one of small_around's neighbors of (1,1).
    struct SlabMap *neighbor = get_slabmap_block(2, 1);
    neighbor->owner = 3;
    neighbor->kind = SlbT_CLAIMED; // not liquid

    CHECK(jailbreak_possible(&room, 3));
}

TEST_CASE_METHOD(ResetState, "jailbreak_possible ignores a neighboring slab that's liquid, even if owned and safe", "[kfx_sim][creature_states_prisn]") {
    struct Room room{};
    room.owner = 2;
    room.slabs_list = get_slab_number(1, 1);
    get_slabmap_block(1, 1)->next_in_room = 0;

    struct SlabMap *neighbor = get_slabmap_block(2, 1);
    neighbor->owner = 3;
    neighbor->kind = SlbT_WATER;

    CHECK_FALSE(jailbreak_possible(&room, 3));
}
