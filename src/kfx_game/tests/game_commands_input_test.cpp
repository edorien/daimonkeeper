// kfx_game: game_commands_input.c's is_mouse_on_map() and
// remember_cursor_subtile(). Moved with the code from kfx_net's
// packets_misc_test.cpp in refactor pass 2 (S12,
// docs/refactor-pass2/stage-12-net-split.md), where packets_input.c became
// kfx_game's game_commands_input.c. Pattern A: plain kfx_sim_state arrays.
#include <catch2/catch_test_macros.hpp>

#include "game_commands.h"
#include "player_data.h"
#include "kfx_sim_state.h"
#include "kfx_net_state.h"
#include "net_game.h"
#include "packet_data.h"
#include "config_players.h"

#include <cstring>

namespace {
struct ResetStates {
    ResetStates() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_net_state, 0, sizeof(kfx_net_state));
        // Out of PLAYERS_COUNT's range on purpose: these tests exercise the raw get_packet(player->user_id)
        // path with synthetic players that never bother setting id_number, so it stays at the memset default
        // (0) -- matching my_player_number's own default unless this is set to something no real player can
        // ever have. Without this, get_players_own_packet's (packet_data.c) "am I the local player" redirect
        // to get_local_packet() would kick in for every one of these player-index-0 players, same as it should
        // for the real my_player_number, defeating the point of testing the raw path.
        my_player_number = 255;
    }
};
}

// is_mouse_on_map: pos_x/pos_y are stored (subtile << 8)*3, hence the
// >>8)/3 in the production code; map_tiles_x/y are in slabs, one fewer
// than the subtile count, so the last valid tile column/row is
// map_tiles_x-1/map_tiles_y-1.
TEST_CASE_METHOD(ResetStates, "is_mouse_on_map is true for a position away from the map edges", "[kfx_game][game_commands_input]") {
    kfx_sim_state.map_tiles_x = 10;
    kfx_sim_state.map_tiles_y = 10;
    struct Packet pckt{};
    pckt.pos_x = 5 * 3 * 256;
    pckt.pos_y = 5 * 3 * 256;
    CHECK(is_mouse_on_map(&pckt));
}

TEST_CASE_METHOD(ResetStates, "is_mouse_on_map is false at the left/top edge (tile 0)", "[kfx_game][game_commands_input]") {
    kfx_sim_state.map_tiles_x = 10;
    kfx_sim_state.map_tiles_y = 10;
    struct Packet pckt{};
    pckt.pos_x = 0;
    pckt.pos_y = 5 * 3 * 256;
    CHECK_FALSE(is_mouse_on_map(&pckt));
}

TEST_CASE_METHOD(ResetStates, "is_mouse_on_map is false at the right/bottom edge (map_tiles-1)", "[kfx_game][game_commands_input]") {
    kfx_sim_state.map_tiles_x = 10;
    kfx_sim_state.map_tiles_y = 10;
    struct Packet pckt{};
    pckt.pos_x = 5 * 3 * 256;
    pckt.pos_y = 9 * 3 * 256;
    CHECK_FALSE(is_mouse_on_map(&pckt));
}

// remember_cursor_subtile resolves the player's packet via
// get_packet(player->user_id).
TEST_CASE_METHOD(ResetStates, "remember_cursor_subtile updates cursor_subtile_x/y from the player's packet position", "[kfx_game][game_commands_input]") {
    struct PlayerInfo* player = &kfx_sim_state.players[0];
    player->user_id = 1;
    sim_packets[1].pos_x = 5 * 256;
    sim_packets[1].pos_y = 7 * 256;
    remember_cursor_subtile(1);
    CHECK(get_player_user_state(player)->cursor_subtile_x == 5);
    CHECK(get_player_user_state(player)->cursor_subtile_y == 7);
}

TEST_CASE_METHOD(ResetStates, "remember_cursor_subtile carries the old position into previous_cursor_subtile_x/y when not interpolating", "[kfx_game][game_commands_input]") {
    struct PlayerInfo* player = &kfx_sim_state.players[0];
    player->user_id = 1;
    get_player_user_state(player)->cursor_subtile_x = 2;
    get_player_user_state(player)->cursor_subtile_y = 3;
    get_user_state(1)->interpolated_tagging = false;
    sim_packets[1].pos_x = 9 * 256;
    sim_packets[1].pos_y = 9 * 256;
    sim_packets[1].control_flags = 0; // no LBtnHeld/LBtnRelease
    remember_cursor_subtile(1);
    CHECK(get_player_user_state(player)->previous_cursor_subtile_x == 2);
    CHECK(get_player_user_state(player)->previous_cursor_subtile_y == 3);
}

TEST_CASE_METHOD(ResetStates, "remember_cursor_subtile snaps previous_cursor_subtile_x/y to the new position on an LBtnHeld click when not already interpolating", "[kfx_game][game_commands_input]") {
    struct PlayerInfo* player = &kfx_sim_state.players[0];
    player->user_id = 1;
    get_player_user_state(player)->cursor_subtile_x = 2;
    get_player_user_state(player)->cursor_subtile_y = 3;
    get_user_state(1)->interpolated_tagging = false;
    sim_packets[1].pos_x = 9 * 256;
    sim_packets[1].pos_y = 9 * 256;
    sim_packets[1].control_flags = PCtr_LBtnHeld;
    remember_cursor_subtile(1);
    CHECK(get_player_user_state(player)->previous_cursor_subtile_x == 9);
    CHECK(get_player_user_state(player)->previous_cursor_subtile_y == 9);
}

TEST_CASE_METHOD(ResetStates, "remember_cursor_subtile sets interpolated_tagging when the mouse is on the map and a left button click/hold is active", "[kfx_game][game_commands_input]") {
    struct PlayerInfo* player = &kfx_sim_state.players[0];
    player->user_id = 1;
    get_player_user_state(player)->mouse_on_map = true;
    sim_packets[1].control_flags = PCtr_LBtnClick;
    remember_cursor_subtile(1);
    CHECK(get_user_state(1)->interpolated_tagging);
}

TEST_CASE_METHOD(ResetStates, "remember_cursor_subtile clears interpolated_tagging when the mouse is off the map", "[kfx_game][game_commands_input]") {
    struct PlayerInfo* player = &kfx_sim_state.players[0];
    player->user_id = 1;
    get_player_user_state(player)->mouse_on_map = false;
    get_user_state(1)->interpolated_tagging = true;
    sim_packets[1].control_flags = PCtr_LBtnClick;
    remember_cursor_subtile(1);
    CHECK_FALSE(get_user_state(1)->interpolated_tagging);
}

// clear_users_button_state (game_commands.c) runs on every paused turn. An External seat's held button
// must survive it: advance_turns pauses between advances, and clearing it there cut a mark_dig sweep
// down to its first row (ai_bridge_smoke, after the 2026-10-03 upstream merge of #5370).
TEST_CASE_METHOD(ResetStates, "clear_users_button_state releases a human's held button but keeps an External seat's", "[kfx_game][game_commands_input]") {
    clear_flag(local_system_flags, GSF_NetworkActive);
    replay.load_enable = 0;
    struct PlayerInfo* seat = &kfx_sim_state.players[1];
    seat->allocflags = PlaF_Allocated | PlaF_ExternalSeat;
    seat->id_number = 1;
    seat->user_id = 1;
    net_restore_external_seats_after_load();
    for (NetUserId user : {(NetUserId)SOLO_HUMAN_ID, (NetUserId)1}) {
        get_user_state(user)->cursor_button_down = 1;
        get_user_state(user)->interpolated_tagging = true;
    }
    clear_users_button_state();
    CHECK(get_user_state(SOLO_HUMAN_ID)->cursor_button_down == 0);
    CHECK_FALSE(get_user_state(SOLO_HUMAN_ID)->interpolated_tagging);
    CHECK(get_user_state(1)->cursor_button_down == 1);
    CHECK(get_user_state(1)->interpolated_tagging);
    net_clear_external_seats();
}

// An agent playing the local human's own keeper (net_add_external_seat's exception): the human's panel clicks
// arrive as global packet actions for the same player, and one landing between the seat's steps (a door chosen
// from the workshop panel while the seat places another) silently lost the seat's order. Only the seat's own user
// may change the player's control state; the watcher keeps pause, chat, event boxes and the camera.
TEST_CASE_METHOD(ResetStates, "a watcher's control-state action on an External seat's player is ignored", "[kfx_game][game_commands_input]") {
    clear_flag(local_system_flags, GSF_NetworkActive);
    replay.load_enable = 0;
    struct PlayerInfo* seat = &kfx_sim_state.players[1];
    seat->allocflags = PlaF_Allocated | PlaF_ExternalSeat;
    seat->id_number = 1;
    seat->user_id = 1;
    seat->work_state = PSt_CtrlDungeon;
    net_restore_external_seats_after_load();
    my_player_number = 1; // the seat is the local human's own keeper: user 0 resolves to it too
    struct Packet* watcher = get_packet(SOLO_HUMAN_ID);
    set_packet_action(watcher, PckA_SetPlyrState, PSt_PlaceDoor, 1, 0, 0);
    CHECK(process_user_global_packet_action(SOLO_HUMAN_ID)); // consumed, not acted on
    CHECK(seat->work_state == PSt_CtrlDungeon);
    net_clear_external_seats();
}

TEST_CASE("global_action_allowed_for_seat_watcher keeps pause, chat and camera, refuses control actions", "[kfx_game][game_commands_input]") {
    for (int64_t ok : {PckA_TogglePause, PckA_UpdatePause, PckA_QuitToMainMenu, PckA_PlyrMsgEnd, PckA_PlyrFastMsg,
                       PckA_ZoomToEvent, PckA_ZoomToPosition, PckA_SwitchView, PckA_EventBoxClose}) {
        CHECK(global_action_allowed_for_seat_watcher(ok));
    }
    for (int64_t no : {PckA_SetPlyrState, PckA_UsePwrHandPick, PckA_UsePwrHandDrop, PckA_GenericLevelPower,
                       PckA_ToggleTendency, PckA_PlyrToggleAlly, PckA_SetViewType, PckA_SetRoomspaceDrag,
                       PckA_ApplyRoomspaceDigTag, PckA_DumpHeldThingToOldPos}) {
        CHECK_FALSE(global_action_allowed_for_seat_watcher(no));
    }
}
