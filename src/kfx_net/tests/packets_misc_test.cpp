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
