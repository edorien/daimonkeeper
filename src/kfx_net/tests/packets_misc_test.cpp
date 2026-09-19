// kfx_net: packets_misc.c -- closes two of the remaining gaps in
// docs/Architecture/testing-harness.md §9/§10's accepted-symbol-residual
// table: get_packet and set_players_packet_action (plus their siblings
// get_players_packet_action, set_players_packet_control,
// unset_players_packet_control) had zero test coverage anywhere -- every
// existing kfx_sim/kfx_render call site into these accepted-residual
// symbols was covered, but never the accessors themselves.
//
// All pattern A: kfx_sim_state.players[]/sim_packets[] are
// plain arrays, get_player_f()/get_packet() are simple index
// checks, no callback fakes needed. Declared in kfx_sim's packet_data.h
// (see that header's own comment) but implemented here in kfx_net's
// packets_misc.c -- a higher-ranked library implementing a lower-ranked
// interface, not a violation (packet_data.h's own file comment explains
// why this split exists). get_packet used to be called get_packet_direct
// and take a plain long index, and a separate get_packet(PlayerNumber)
// resolved through a player's packet_num; the two collapsed into one
// get_packet(NetUserId) index accessor as part of upstream's User/Player
// refactor (merge commit 79b0e2096), with player->user_id now the
// explicit lookup key at call sites instead of implicit indirection.
#include <catch2/catch_test_macros.hpp>

#include "packets.h"
#include "kfx_sim_state.h"
#include "kfx_net_state.h"

#include <cstring>

namespace {
struct ResetStates {
    ResetStates() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_net_state, 0, sizeof(kfx_net_state));
    }
};
}

TEST_CASE_METHOD(ResetStates, "get_packet returns the packet at the given NetUserId index", "[kfx_net][packets_misc]") {
    sim_packets[3].action = 42;
    CHECK(get_packet(3) == &sim_packets[3]);
    CHECK(get_packet(3)->action == 42);
}

TEST_CASE_METHOD(ResetStates, "get_packet returns INVALID_PACKET for an out-of-range index", "[kfx_net][packets_misc]") {
    CHECK(get_packet(-1) == INVALID_PACKET);
    CHECK(get_packet(PACKETS_COUNT) == INVALID_PACKET);
}

// get_packet(NetUserId) is a direct sim_packets[] index -- it no longer
// resolves through a PlayerInfo itself (that indirection collapsed away
// in the User/Player refactor, see packet_data.h's file comment and
// PlayerInfo::user_id). Resolving a specific player's packet is now a
// two-step get_packet(player->user_id) at the call site, as production
// code (packets_misc.c/packets_input.c/packet_data.c) does throughout.
TEST_CASE_METHOD(ResetStates, "get_packet(player->user_id) resolves a player's packet through their assigned network-user slot", "[kfx_net][packets_misc]") {
    kfx_sim_state.players[2].user_id = 5;
    sim_packets[5].action = 7;
    struct PlayerInfo* player = &kfx_sim_state.players[2];
    CHECK(get_packet(player->user_id) == &sim_packets[5]);
    CHECK(get_packet(player->user_id)->action == 7);
}

TEST_CASE_METHOD(ResetStates, "get_packet(player->user_id) returns INVALID_PACKET when the player's user_id is out of range", "[kfx_net][packets_misc]") {
    kfx_sim_state.players[0].user_id = PACKETS_COUNT; // one past the last valid slot
    CHECK(get_packet(kfx_sim_state.players[0].user_id) == INVALID_PACKET);
}

TEST_CASE_METHOD(ResetStates, "set_players_packet_action writes the action kind and all four parameters via the player's packet", "[kfx_net][packets_misc]") {
    kfx_sim_state.players[1].user_id = 4;
    struct PlayerInfo* player = &kfx_sim_state.players[1];
    set_players_packet_action(player, 9, 11, 22, 33, 44);
    struct Packet* pckt = &sim_packets[4];
    CHECK(pckt->action == 9);
    CHECK(pckt->actn_par1 == 11);
    CHECK(pckt->actn_par2 == 22);
    CHECK(pckt->actn_par3 == 33);
    CHECK(pckt->actn_par4 == 44);
}

TEST_CASE_METHOD(ResetStates, "get_players_packet_action reads back the action kind written by set_players_packet_action", "[kfx_net][packets_misc]") {
    kfx_sim_state.players[6].user_id = 0;
    struct PlayerInfo* player = &kfx_sim_state.players[6];
    set_players_packet_action(player, 13, 0, 0, 0, 0);
    CHECK(get_players_packet_action(player) == 13);
}

TEST_CASE_METHOD(ResetStates, "set_players_packet_control ORs the flag into the player's packet without clearing existing flags", "[kfx_net][packets_misc]") {
    kfx_sim_state.players[0].user_id = 2;
    sim_packets[2].control_flags = 0x01;
    set_players_packet_control(&kfx_sim_state.players[0], 0x04);
    CHECK(sim_packets[2].control_flags == 0x05);
}

TEST_CASE_METHOD(ResetStates, "unset_players_packet_control clears only the given flag", "[kfx_net][packets_misc]") {
    kfx_sim_state.players[0].user_id = 2;
    sim_packets[2].control_flags = 0x07;
    unset_players_packet_control(&kfx_sim_state.players[0], 0x02);
    CHECK(sim_packets[2].control_flags == 0x05);
}

TEST_CASE_METHOD(ResetStates, "set_players_packet_position sets coordinates, the map-valid flag, and the context bits", "[kfx_net][packets_misc]") {
    struct Packet pckt{};
    set_players_packet_position(&pckt, 123, 456, 3);
    CHECK(pckt.pos_x == 123);
    CHECK(pckt.pos_y == 456);
    CHECK((pckt.control_flags & PCtr_MapCoordsValid) != 0);
    CHECK(((pckt.additional_packet_values & PCAdV_ContextMask) >> 1) == 3);
}

TEST_CASE_METHOD(ResetStates, "set_players_packet_position replaces a previous context rather than accumulating it", "[kfx_net][packets_misc]") {
    struct Packet pckt{};
    set_players_packet_position(&pckt, 0, 0, 5);
    set_players_packet_position(&pckt, 0, 0, 1);
    CHECK(((pckt.additional_packet_values & PCAdV_ContextMask) >> 1) == 1);
}

// is_mouse_on_map (packets_input.c) had no header declaration anywhere --
// only called from within packets_input.c itself -- added to kfx_net's
// own packets.h, same "add the missing declaration" fix used repeatedly
// across this plan. pos_x/pos_y are stored (subtile << 8)*3, hence the
// >>8)/3 in the production code; map_tiles_x/y are in slabs, one fewer
// than the subtile count, so the last valid tile column/row is
// map_tiles_x-1/map_tiles_y-1.
TEST_CASE_METHOD(ResetStates, "is_mouse_on_map is true for a position away from the map edges", "[kfx_net][packets_misc]") {
    kfx_sim_state.map_tiles_x = 10;
    kfx_sim_state.map_tiles_y = 10;
    struct Packet pckt{};
    pckt.pos_x = 5 * 3 * 256;
    pckt.pos_y = 5 * 3 * 256;
    CHECK(is_mouse_on_map(&pckt));
}

TEST_CASE_METHOD(ResetStates, "is_mouse_on_map is false at the left/top edge (tile 0)", "[kfx_net][packets_misc]") {
    kfx_sim_state.map_tiles_x = 10;
    kfx_sim_state.map_tiles_y = 10;
    struct Packet pckt{};
    pckt.pos_x = 0;
    pckt.pos_y = 5 * 3 * 256;
    CHECK_FALSE(is_mouse_on_map(&pckt));
}

TEST_CASE_METHOD(ResetStates, "is_mouse_on_map is false at the right/bottom edge (map_tiles-1)", "[kfx_net][packets_misc]") {
    kfx_sim_state.map_tiles_x = 10;
    kfx_sim_state.map_tiles_y = 10;
    struct Packet pckt{};
    pckt.pos_x = 5 * 3 * 256;
    pckt.pos_y = 9 * 3 * 256;
    CHECK_FALSE(is_mouse_on_map(&pckt));
}

// remember_cursor_subtile (packets_input.c) had no header declaration
// anywhere either -- same fix, same packets.h. Resolves the player's
// packet via get_packet(player->user_id), one of the
// accepted-symbol-residual family's own call sites (packets_misc.c),
// exercised here from kfx_net's own side rather than a kfx_sim/kfx_render
// caller.
TEST_CASE_METHOD(ResetStates, "remember_cursor_subtile updates cursor_subtile_x/y from the player's packet position", "[kfx_net][packets_misc]") {
    struct PlayerInfo* player = &kfx_sim_state.players[0];
    player->user_id = 1;
    sim_packets[1].pos_x = 5 * 256;
    sim_packets[1].pos_y = 7 * 256;
    remember_cursor_subtile(1);
    CHECK(get_player_user_state(player)->cursor_subtile_x == 5);
    CHECK(get_player_user_state(player)->cursor_subtile_y == 7);
}

TEST_CASE_METHOD(ResetStates, "remember_cursor_subtile carries the old position into previous_cursor_subtile_x/y when not interpolating", "[kfx_net][packets_misc]") {
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

TEST_CASE_METHOD(ResetStates, "remember_cursor_subtile snaps previous_cursor_subtile_x/y to the new position on an LBtnHeld click when not already interpolating", "[kfx_net][packets_misc]") {
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

TEST_CASE_METHOD(ResetStates, "remember_cursor_subtile sets interpolated_tagging when the mouse is on the map and a left button click/hold is active", "[kfx_net][packets_misc]") {
    struct PlayerInfo* player = &kfx_sim_state.players[0];
    player->user_id = 1;
    get_player_user_state(player)->mouse_on_map = true;
    sim_packets[1].control_flags = PCtr_LBtnClick;
    remember_cursor_subtile(1);
    CHECK(get_user_state(1)->interpolated_tagging);
}

TEST_CASE_METHOD(ResetStates, "remember_cursor_subtile clears interpolated_tagging when the mouse is off the map", "[kfx_net][packets_misc]") {
    struct PlayerInfo* player = &kfx_sim_state.players[0];
    player->user_id = 1;
    get_player_user_state(player)->mouse_on_map = false;
    get_user_state(1)->interpolated_tagging = true;
    sim_packets[1].control_flags = PCtr_LBtnClick;
    remember_cursor_subtile(1);
    CHECK_FALSE(get_user_state(1)->interpolated_tagging);
}
