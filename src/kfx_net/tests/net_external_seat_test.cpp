// kfx_net: External seats -- human-shaped players in a local (non-networked) game whose packets
// are written by an outside process (docs/refactor/AI/LLM/04-seat-and-action-api.md section 1,
// 06-lifecycle-and-robustness.md section 1).
//  * get_net_user_player_number() used to map only SOLO_HUMAN_ID in a local game, so a second
//    seat's packet could never be dispatched;
//  * the mapping is process-global and never saved, so net_restore_external_seats_after_load()
//    rebuilds it from the saved PlaF_ExternalSeat flag and user_id.
#include <catch2/catch_test_macros.hpp>

#include "net_game.h"
#include "player_data.h"
#include "dungeon_data.h"
#include "player_computer.h"
#include "kfx_sim_state.h"
#include "kfx_net_state.h"
#include "kfx_config_state.h"

#include <cstring>

namespace {
struct LocalGameFixture {
    LocalGameFixture() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_net_state, 0, sizeof(kfx_net_state));
        kfx_config_state.neutral_player_num = PLAYER_NEUTRAL;
        kfx_sim_state.game_kind = GKind_LocalGame;
        my_player_number = 0;
        for (int64_t i = 0; i < PLAYERS_COUNT; i++) {
            kfx_sim_state.players[i].id_number = (PlayerNumber)i;
            kfx_sim_state.players[i].user_id = -1;
        }
        make_human(0);
        net_clear_external_seats();
    }
    ~LocalGameFixture() { net_clear_external_seats(); }
    void make_human(int64_t i) {
        struct PlayerInfo *p = &kfx_sim_state.players[i];
        p->allocflags |= PlaF_Allocated;
        p->is_active = 1;
        p->user_id = (i == 0) ? SOLO_HUMAN_ID : -1;
    }
    void make_computer(int64_t i) {
        struct PlayerInfo *p = &kfx_sim_state.players[i];
        p->allocflags |= PlaF_Allocated | PlaF_CompCtrl;
        p->is_active = 1;
        get_dungeon(i)->computer_enabled |= 0x01;
        kfx_sim_state.computer[i].task_state = 3; // non-zero so a reset is visible
    }
};
}

TEST_CASE_METHOD(LocalGameFixture, "a local game maps only the human until a seat is added", "[kfx_net][external_seat]") {
    CHECK(get_net_user_player_number(SOLO_HUMAN_ID) == 0);
    for (NetUserId u = 1; u < MAX_NET_USERS; u++)
        CHECK(get_net_user_player_number(u) == -1);
    CHECK_FALSE(user_present(1));
}

TEST_CASE_METHOD(LocalGameFixture, "an unclaimed slot becomes an External seat on the first free user id", "[kfx_net][external_seat]") {
    const NetUserId user = net_add_external_seat(2);
    REQUIRE(user == 1);
    CHECK(get_net_user_player_number(1) == 2);
    CHECK(user_present(1));
    CHECK(get_net_user_player_number(SOLO_HUMAN_ID) == 0); // the human is untouched
    const struct PlayerInfo *p = &kfx_sim_state.players[2];
    CHECK(p->user_id == 1);
    CHECK(flag_is_set(p->allocflags, PlaF_ExternalSeat));
    CHECK(flag_is_set(p->allocflags, PlaF_Allocated));
    CHECK_FALSE(flag_is_set(p->allocflags, PlaF_CompCtrl));
    CHECK(p->is_active == 1);
}

TEST_CASE_METHOD(LocalGameFixture, "further seats take the next ids until the user ids run out", "[kfx_net][external_seat]") {
    CHECK(net_add_external_seat(1) == 1);
    CHECK(net_add_external_seat(2) == 2);
    CHECK(net_add_external_seat(3) == 3);
    CHECK(net_add_external_seat(4) == -1); // MAX_NET_USERS == 4: the human plus three seats
    CHECK(get_net_user_player_number(3) == 3);
    CHECK_FALSE(flag_is_set(kfx_sim_state.players[4].allocflags, PlaF_ExternalSeat));
}

TEST_CASE_METHOD(LocalGameFixture, "a computer slot is converted and stops running the built-in AI", "[kfx_net][external_seat]") {
    make_computer(1);
    REQUIRE(net_add_external_seat(1) == 1);
    const struct PlayerInfo *p = &kfx_sim_state.players[1];
    CHECK_FALSE(flag_is_set(p->allocflags, PlaF_CompCtrl));
    CHECK(flag_is_set(p->allocflags, PlaF_ExternalSeat));
    CHECK((get_dungeon(1)->computer_enabled & 0x01) == 0);
    // M10: the conversion now reinitializes Computer2 via setup_a_computer_player() (a real process
    // list) instead of a bare memset(0) -- the old memset left null process function pointers that
    // crashed process_computer_players2() if computer_enabled was ever turned back on for this player.
    // task_state ends up CTaskSt_Select (setup_a_computer_player's own normal starting value) rather
    // than 0, which is fine: computer_enabled is cleared above, so process_computer_players2() skips
    // this player regardless of task_state. action_status_flag == 1 is the telltale that real setup
    // ran rather than a bare memset.
    CHECK(kfx_sim_state.computer[1].task_state == CTaskSt_Select);
    CHECK(kfx_sim_state.computer[1].action_status_flag == 1);
}

TEST_CASE_METHOD(LocalGameFixture, "seats are refused for another human, an existing seat, a bad slot, or a network game; the local player's own is the one exception (M10)", "[kfx_net][external_seat]") {
    REQUIRE(net_add_external_seat(0) >= 0);           // the local human's own slot: claimable on purpose
    CHECK(net_release_external_seat(0));
    CHECK_FALSE(flag_is_set(kfx_sim_state.players[0].allocflags, PlaF_CompCtrl));
    CHECK_FALSE(flag_is_set(kfx_sim_state.players[0].allocflags, PlaF_ExternalSeat));
    make_human(2);
    CHECK(net_add_external_seat(2) == -1);            // another human
    REQUIRE(net_add_external_seat(3) == 1);
    CHECK(net_add_external_seat(3) == -1);            // already a seat
    CHECK(net_add_external_seat(-1) == -1);
    CHECK(net_add_external_seat(PLAYERS_COUNT) == -1);
    kfx_sim_state.system_flags |= GSF_NetworkActive;
    CHECK(net_add_external_seat(5) == -1);
}

TEST_CASE_METHOD(LocalGameFixture, "restoring after a load rebuilds the mapping from the saved flag and user id", "[kfx_net][external_seat]") {
    REQUIRE(net_add_external_seat(2) == 1);
    REQUIRE(net_add_external_seat(4) == 2);
    // A load restores kfx_sim_state (players) from the save but not the process-global mapping.
    net_clear_external_seats();
    CHECK(get_net_user_player_number(1) == -1);
    CHECK(get_net_user_player_number(2) == -1);

    net_restore_external_seats_after_load();

    CHECK(get_net_user_player_number(1) == 2);
    CHECK(get_net_user_player_number(2) == 4);
    CHECK(get_net_user_player_number(SOLO_HUMAN_ID) == 0);
    CHECK(get_net_user_player_number(3) == -1);
}

TEST_CASE_METHOD(LocalGameFixture, "restoring drops a mapping the save no longer has and ignores non-seat users", "[kfx_net][external_seat]") {
    REQUIRE(net_add_external_seat(2) == 1);
    // A save from a networked game leaves user ids on human players; they are not External seats
    // and must stay unmapped in a local game, exactly as before this feature.
    make_human(3);
    kfx_sim_state.players[3].user_id = 2;
    // The save being loaded has no seat at all.
    kfx_sim_state.players[2].allocflags &= ~PlaF_ExternalSeat;

    net_restore_external_seats_after_load();

    CHECK(get_net_user_player_number(1) == -1);
    CHECK(get_net_user_player_number(2) == -1);
}

TEST_CASE_METHOD(LocalGameFixture, "restoring warns about and skips a seat with an unusable user id", "[kfx_net][external_seat]") {
    REQUIRE(net_add_external_seat(2) == 1);
    kfx_sim_state.players[2].user_id = SOLO_HUMAN_ID; // would shadow the human
    net_restore_external_seats_after_load();
    CHECK(get_net_user_player_number(SOLO_HUMAN_ID) == 0);
    CHECK(get_net_user_player_number(1) == -1);
}

TEST_CASE_METHOD(LocalGameFixture, "a networked game never consults the External seat mapping", "[kfx_net][external_seat]") {
    REQUIRE(net_add_external_seat(2) == 1);
    kfx_sim_state.system_flags |= GSF_NetworkActive;
    CHECK(get_net_user_player_number(1) != 2); // answered from the network table, not the seat table
    kfx_sim_state.system_flags &= ~GSF_NetworkActive;
    CHECK(get_net_user_player_number(1) == 2);
}

TEST_CASE_METHOD(LocalGameFixture, "the pending External slots dedupe, ignore bad indices, and are consumed by the claim", "[kfx_net][external_seat]") {
    net_pending_external_seats_clear();
    net_pending_external_seats_add(2);
    net_pending_external_seats_add(2);
    net_pending_external_seats_add(-1);
    net_pending_external_seats_add(PLAYERS_COUNT);
    CHECK(net_pending_external_seats_count() == 1);
    // No keeper with a dungeon heart exists for slot 2 in this fixture: skipped, and the list is emptied either way.
    CHECK(net_claim_pending_external_seats() == 0);
    CHECK(net_pending_external_seats_count() == 0);
    CHECK(get_net_user_player_number(1) == -1);
}

TEST_CASE_METHOD(LocalGameFixture, "releasing refuses what is not an External seat", "[kfx_net][external_seat]") {
    CHECK_FALSE(net_release_external_seat(0));      // the human
    CHECK_FALSE(net_release_external_seat(2));      // nothing there
    CHECK_FALSE(net_release_external_seat(-1));
    CHECK_FALSE(net_release_external_seat(PLAYERS_COUNT));
    CHECK(net_release_all_external_seats() == 0);
}
