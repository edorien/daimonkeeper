// kfx_game: game_commands_input.c's is_mouse_on_map() and
// remember_cursor_subtile(). Moved with the code from kfx_net's
// packets_misc_test.cpp in refactor pass 2 (S12,
// docs/refactor-pass2/stage-12-net-split.md), where packets_input.c became
// kfx_game's game_commands_input.c. Pattern A: plain kfx_sim_state arrays.
#include <catch2/catch_test_macros.hpp>

#include "game_commands.h"
#include "packets.h"
#include "player_data.h"
#include "kfx_sim_state.h"
#include "kfx_net_state.h"
#include "net_game.h"
#include "packet_data.h"
#include "config_players.h"
#include "roomspace.h"

#include <cstdlib>
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

// In an emergency the human watching an agent play their keeper may use the cheat menu (cheat mode on, a local game):
// its one-shot cheats, and its placement modes -- entering a cheat work state and leaving it back to dungeon control,
// not choosing an ordinary control state. Cheats acting on a possessed creature are not a watcher's to use.
TEST_CASE_METHOD(ResetStates, "a watcher may use the cheat menu on an External seat's player when cheats are on", "[kfx_game][game_commands_input]") {
    clear_flag(local_system_flags, GSF_NetworkActive);
    replay.load_enable = 0;
    kfx_sim_state.game_kind = GKind_LocalGame;
    struct PlayerInfo* seat = &kfx_sim_state.players[1];
    seat->allocflags = PlaF_Allocated | PlaF_ExternalSeat;
    seat->id_number = 1;
    seat->user_id = 1;
    seat->work_state = PSt_CtrlDungeon;
    net_restore_external_seats_after_load();
    my_player_number = 1;
    struct Packet probe;
    std::memset(&probe, 0, sizeof(probe));
    // Cheat mode off: nothing.
    kfx_sim_state.easter_eggs_enabled = 0;
    set_packet_action(&probe, PckA_CheatAllRooms, 0, 0, 0, 0);
    CHECK_FALSE(cheat_action_allowed_for_seat_watcher(seat, &probe));
    struct Packet* watcher = get_packet(SOLO_HUMAN_ID);
    set_packet_action(watcher, PckA_SetPlyrState, PSt_MkGoodCreatr, 0, 0, 0);
    CHECK(process_user_global_packet_action(SOLO_HUMAN_ID));
    CHECK(seat->work_state == PSt_CtrlDungeon);
    // Cheat mode on.
    kfx_sim_state.easter_eggs_enabled = 1;
    for (int64_t ok : {PckA_CheatAllRooms, PckA_CheatAllMagic, PckA_CheatRevealMap, PckA_CheatWinLevel, PckA_CheatLoseLevel,
                       PckA_CheatAllTraps, PckA_CheatSwitchCreature, PckA_CheatSwitchPlayer}) {
        set_packet_action(&probe, ok, 0, 0, 0, 0);
        CHECK(cheat_action_allowed_for_seat_watcher(seat, &probe));
    }
    for (int64_t no : {PckA_CheatKillCreature, PckA_CheatLevelUp, PckA_CheatLevelDown, PckA_CheatApplySpell,
                       PckA_CheatCtrlCrtrSetInstnc, PckA_UsePwrHandPick, PckA_ToggleTendency}) {
        set_packet_action(&probe, no, 0, 0, 0, 0);
        CHECK_FALSE(cheat_action_allowed_for_seat_watcher(seat, &probe));
    }
    // A placement mode: in, and back out to dungeon control.
    set_packet_action(watcher, PckA_SetPlyrState, PSt_MkGoodCreatr, 0, 0, 0);
    process_user_global_packet_action(SOLO_HUMAN_ID);
    CHECK(seat->work_state == PSt_MkGoodCreatr);
    set_packet_action(watcher, PckA_SetPlyrState, PSt_CtrlDungeon, 0, 0, 0);
    process_user_global_packet_action(SOLO_HUMAN_ID);
    CHECK(seat->work_state == PSt_CtrlDungeon);
    // Not an ordinary control state, and not "back to dungeon control" when it never left it for a cheat.
    set_packet_action(watcher, PckA_SetPlyrState, PSt_PlaceDoor, 1, 0, 0);
    CHECK(process_user_global_packet_action(SOLO_HUMAN_ID));
    CHECK(seat->work_state == PSt_CtrlDungeon);
    set_packet_action(&probe, PckA_SetPlyrState, PSt_CtrlDungeon, 0, 0, 0);
    CHECK_FALSE(cheat_action_allowed_for_seat_watcher(seat, &probe));
    kfx_sim_state.easter_eggs_enabled = 0;
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

// P5-F34: the local dig prediction ends a drag with PckA_ApplyRoomspaceDigTag (par2 the 1x1 brush width). It shared
// SetRoomspaceHighlight's handler, whose "width 1: leave click-and-drag mode" ran first, so the drag area the
// dungeon-control step then works out was only the slab under the release (a multiplayer game's drag-dig tagged one
// slab). Setting the highlight still leaves drag mode; a dig tag must not.
TEST_CASE_METHOD(ResetStates, "a dig tag keeps click-and-drag mode; setting the highlight leaves it", "[kfx_game][game_commands_input]") {
    clear_flag(local_system_flags, GSF_NetworkActive);
    replay.load_enable = 0;
    struct PlayerInfo* player = &kfx_sim_state.players[0];
    player->allocflags = PlaF_Allocated;
    player->id_number = 0;
    player->user_id = SOLO_HUMAN_ID;
    my_player_number = 0;
    struct UserState* ustate = get_user_state(SOLO_HUMAN_ID);
    struct Packet* pckt = get_packet(SOLO_HUMAN_ID);

    player->render_roomspace.drag_mode = true;
    ustate->one_click_lock_cursor = true;
    set_packet_action(pckt, PckA_ApplyRoomspaceDigTag, drag_placement_mode, 1, 0, 0);
    process_user_global_packet_action(SOLO_HUMAN_ID);
    CHECK(player->render_roomspace.drag_mode);
    CHECK(ustate->one_click_lock_cursor);
    CHECK(player->roomspace_highlight_mode == drag_placement_mode);

    set_packet_action(pckt, PckA_SetRoomspaceHighlight, drag_placement_mode, 1, 0, 0);
    process_user_global_packet_action(SOLO_HUMAN_ID);
    CHECK_FALSE(player->render_roomspace.drag_mode);
    CHECK_FALSE(ustate->one_click_lock_cursor);
}

// Upstream #5383: the host sends its clock as a packet now and then (Lua's GetGameTimestamp); everyone, a replay
// included, reads the same time from it. Only the host's packet counts.
TEST_CASE_METHOD(ResetStates, "the host's timestamp packet sets the game's timestamp; another user's is ignored", "[kfx_game][game_commands_input]") {
    clear_flag(local_system_flags, GSF_NetworkActive);
    replay.load_enable = 0;
    for (int64_t u = 0; u < 2; u++) {
        struct PlayerInfo* player = &kfx_sim_state.players[u];
        player->allocflags = PlaF_Allocated;
        player->id_number = u;
        player->user_id = u;
    }
    my_player_number = 0; // the local user is the host
    kfx_sim_state.play_gameturn = 128; // set_pending_timestamp_packet_action() sends on turn % 256 == 128
    set_gameturn_source(&kfx_sim_state.play_gameturn);
    struct Packet host{};
    set_pending_timestamp_packet_action(&host);
    REQUIRE(host.action == PckA_SetTimestamp);

    // the packet carries the sender's clock in 21 + 32 bits and the zone in minutes: sent as it is
    *get_packet(1) = host;
    CHECK(process_user_global_packet_action(1) == 0);
    CHECK(kfx_sim_state.timestamp == 0); // user 1 isn't the host

    *get_packet(SERVER_ID) = host;
    process_user_global_packet_action(SERVER_ID);
    CHECK(kfx_sim_state.timestamp > 1700000000); // a real clock reading, past 2023
    CHECK(kfx_sim_state.timestamp_turn == 128);
    CHECK(llabs(kfx_sim_state.timestamp_tz) <= 14 * 3600);
    CHECK(kfx_sim_state.timestamp_tz % 60 == 0);

    // a time zone west of UTC survives the 11-bit field
    struct Packet west{};
    set_packet_action(&west, PckA_SetTimestamp, 1000000000, (uint32_t)((-300) & 0x7FF) << 21, 7, 0);
    *get_packet(SERVER_ID) = west;
    process_user_global_packet_action(SERVER_ID);
    CHECK(kfx_sim_state.timestamp == 1000000000);
    CHECK(kfx_sim_state.timestamp_tz == -300 * 60);
    CHECK(kfx_sim_state.timestamp_turn == 7);
    set_gameturn_source(nullptr);
}

// A replay plays the recorded quits too: my player there is only the one being watched, so a recorded Alt-F4
// closed the program and a recorded quit saved a continue game; and the watched player's leaving ended the replay
// rather than the recording player's (upstream #5452).
TEST_CASE_METHOD(ResetStates, "a replay's recorded quits neither close the program nor end it for the watched player", "[kfx_game][game_commands_input]") {
    clear_flag(local_system_flags, GSF_NetworkActive);
    kfx_sim_state.game_kind = GKind_MultiGame;
    for (int64_t u = 0; u < 3; u++) {
        struct PlayerInfo* player = &kfx_sim_state.players[u];
        player->allocflags = PlaF_Allocated;
        player->id_number = u;
        player->user_id = u;
        set_net_user_player_number(u, u);
    }
    replay.load_enable = 1;
    replay.head.recording_user = 0;
    my_player_number = 1; // watching player 1
    exit_keeper = 0;
    quit_game = 0;

    struct Packet close{};
    set_packet_action(&close, PckA_ForceApplicationClose, 0, 0, 0, 0);
    *get_packet(1) = close;
    process_user_global_packet_action(1);
    CHECK(exit_keeper == 0);
    CHECK(quit_game == 0); // the watched player left; the recording goes on
    CHECK((kfx_sim_state.players[1].display_flags & PlaF6_PlyrHasQuit) != 0);

    struct Packet quit{};
    set_packet_action(&quit, PckA_QuitToMainMenu, 0, 0, 0, 0);
    *get_packet(0) = quit;
    process_user_global_packet_action(0);
    CHECK(exit_keeper == 0);
    CHECK(quit_game == 1); // the recording player left: the replay ends
    replay.load_enable = 0;
    quit_game = 0;
    for (int64_t u = 0; u < 3; u++)
        set_net_user_player_number(u, -1);
}
