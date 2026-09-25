// kfx_sim "room" cluster: the first test that actually exercises
// kfx_sim's get_packet() (packet_data.c -- real implementation, not a
// stub; the old kfx_packet_test_stubs residual was removed once
// get_packet/get_packet_direct/set_packet_action/set_players_packet_action
// moved down into kfx_sim itself, see c817a9c77) by calling a code path
// that uses it, not just linking against it -- flagged as open in
// docs/refactor/testing/comprehensive/stage-08b-kfx-sim-clusters.md.
//
// get_dungeon_sell_user_roomspace()'s single_subtile_mode branch is the
// simplest of its four roomspace_mode branches: it calls
// get_packet(player->user_id) (index 0 by default after ResetSimState's
// memset) but doesn't read any of the returned packet's fields in this
// particular branch -- only drag_placement_mode does that (needs a
// fuller PCtr_* control-flags setup, not attempted here). Still a real
// exercise of that call path, and a legitimate behavior to assert on:
// this mode just copies player->render_roomspace through.
#include <catch2/catch_test_macros.hpp>

#include "globals.h"
#include "roomspace.h"
#include "player_data.h"
#include "kfx_sim_state.h"
#include "packet_data.h"

#include <cstring>

namespace {
struct ResetSimState {
    ResetSimState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        get_packet(0)->control_flags = 0; // the shared stub packet persists across tests
    }
};
}

TEST_CASE_METHOD(ResetSimState, "get_dungeon_sell_user_roomspace copies render_roomspace through in single_subtile_mode", "[kfx_sim][roomspace]") {
    struct PlayerInfo *player = get_player(0);
    player->roomspace_mode = single_subtile_mode;
    get_user_state(0)->ignore_next_PCtr_LBtnRelease = false;
    player->render_roomspace.slab_count = 7;

    struct RoomSpace result{};
    get_dungeon_sell_user_roomspace(&result, 0, 10, 10);

    CHECK(result.slab_count == 7);
    CHECK(get_player_user_state(player)->boxsize == 7);
}

// box_placement_mode: same call to get_packet() as every other
// mode, but doesn't read any of its fields -- unlike single_subtile_mode
// it does real geometry work (create_box_roomspace +
// check_roomspace_for_sellable_slabs), still safe against a fully-zeroed
// map (subtile_is_sellable_room/_door_or_trap both short-circuit to
// false via map_block_invalid() on an unclaimed/zero-sized map, verified
// by reading both bodies -- no crash risk, just "nothing is sellable
// here").
TEST_CASE_METHOD(ResetSimState, "get_dungeon_sell_user_roomspace builds a box of the configured size in box_placement_mode", "[kfx_sim][roomspace]") {
    struct PlayerInfo *player = get_player(0);
    player->roomspace_mode = box_placement_mode;
    get_user_state(0)->ignore_next_PCtr_LBtnRelease = false;
    player->roomspace_width = 3;
    player->roomspace_height = 3;

    struct RoomSpace result{};
    get_dungeon_sell_user_roomspace(&result, 0, 30, 30); // slab (10, 10)

    CHECK(result.width == 3);
    CHECK(result.height == 3);
    CHECK(result.slab_count == 0); // nothing sellable on an empty map
    CHECK(result.is_roomspace_a_box); // still true -- the "empty red box" case
    CHECK(get_player_user_state(player)->boxsize == 0);
}

// drag_placement_mode: the one branch that actually reads
// pckt->control_flags -- flagged in docs/refactor/testing/comprehensive/
// stage-08b-kfx-sim-clusters.md as the still-open piece of this
// accepted kfx_sim -> kfx_net residual (kfx_sim/tests/packet_test_stubs.cpp).
// Without a held-button flag, the drag box degenerates to 1x1 at the
// current slab; with PCtr_LBtnHeld set, it spans from
// player->render_roomspace's stored drag_start back to the current
// slab -- a real behavioral difference driven entirely by the packet's
// field contents, not just the call.
TEST_CASE_METHOD(ResetSimState, "get_dungeon_sell_user_roomspace collapses to a 1x1 box in drag_placement_mode with no button held", "[kfx_sim][roomspace]") {
    struct PlayerInfo *player = get_player(0);
    player->roomspace_mode = drag_placement_mode;
    get_user_state(0)->ignore_next_PCtr_LBtnRelease = false;
    player->render_roomspace.drag_mode = true; // otherwise drag_start gets reset to the current slab anyway
    get_packet(0)->control_flags = 0; // no button held

    struct RoomSpace result{};
    get_dungeon_sell_user_roomspace(&result, 0, 30, 30); // slab (10, 10)

    CHECK(result.width == 1);
    CHECK(result.height == 1);
    CHECK(result.left == 10);
    CHECK(result.top == 10);
}

TEST_CASE_METHOD(ResetSimState, "get_dungeon_sell_user_roomspace drags from the stored start slab when PCtr_LBtnHeld is set", "[kfx_sim][roomspace]") {
    struct PlayerInfo *player = get_player(0);
    player->roomspace_mode = drag_placement_mode;
    get_user_state(0)->ignore_next_PCtr_LBtnRelease = false;
    player->render_roomspace.drag_mode = true;
    player->render_roomspace.drag_start_x = 5;
    player->render_roomspace.drag_start_y = 5;
    get_packet(0)->control_flags = PCtr_LBtnHeld;

    struct RoomSpace result{};
    get_dungeon_sell_user_roomspace(&result, 0, 30, 30); // slab (10, 10)

    CHECK(result.left == 5);
    CHECK(result.top == 5);
    CHECK(result.right == 10);
    CHECK(result.bottom == 10);
    CHECK(result.width == 6);
    CHECK(result.height == 6);
    CHECK(get_user_state(0)->one_click_lock_cursor); // set only on the held-button path
}

// get_user_player_number(): kfx_sim-side stand-in for kfx_net's
// get_net_user_player_number(), used by the UserState-keyed roomspace/cursor
// code that sits below kfx_net. Reads PlayerInfo::user_id, the same 1-1 mapping.
TEST_CASE_METHOD(ResetSimState, "get_user_player_number maps a user to the player that owns it", "[kfx_sim][roomspace][user_state]") {
    for (int64_t i = 0; i < PLAYERS_COUNT; i++) {
        kfx_sim_state.players[i].id_number = (PlayerNumber)i;
        kfx_sim_state.players[i].user_id = -1;
    }
    kfx_sim_state.players[3].user_id = 2;
    kfx_sim_state.players[5].user_id = 0;

    CHECK(get_user_player_number(2) == 3);
    CHECK(get_user_player_number(0) == 5);
    CHECK(get_user_player_number(1) == -1); // no player for this user
    CHECK(get_user_player_number(-1) == -1); // invalid user never matches a player with no user
}
