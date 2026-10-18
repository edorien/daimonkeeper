// kfx_script: api_urgent_hold.c -- time to think, where pausing is fair (docs/plans/llm-bridge-realtime.md, Phase 6). An
// urgent decision holds the game (paused, or slowed) until the seat answers, the agent takes the pause or the speed
// itself, or a wall-clock cap passes; never in a game a human plays (unless the setup allowed the agent to pause).
#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "api_urgent_hold.h"
#include "bflib_datetm.h"
#include "external_seat.h"
#include "game_legacy.h"
#include "kfx_net_state.h"
#include "kfx_sim_state.h"
#include "net_game.h"
#include "player_data.h"

namespace {
GameTurn g_turn = 100;
TbClockMSec g_clock = 1000;
TbClockMSec fake_clock() { return g_clock; }

struct HoldFixture {
    TbClockMSec (*saved_clock)(void) = nullptr;
    HoldFixture() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_net_state, 0, sizeof(kfx_net_state));
        kfx_sim_state.game_kind = GKind_LocalGame;
        kfx_sim_state.turns_per_second = 20;
        for (int64_t i = 0; i < PLAYERS_COUNT; i++) {
            kfx_sim_state.players[i].id_number = (PlayerNumber)i;
            kfx_sim_state.players[i].user_id = -1;
        }
        net_clear_external_seats();
        net_add_external_seat(2);          // no human keeper: the game is the agent's to pause
        g_turn = 100;
        g_clock = 1000;
        set_gameturn_source(&g_turn);
        saved_clock = LbTimerClock;
        LbTimerClock = &fake_clock;
        extseat_reset();
        extseat_allow_pause_with_human(false);
        api_urgent_hold_reset();
    }
    ~HoldFixture() {
        api_urgent_hold_reset();
        LbTimerClock = saved_clock;
        set_gameturn_source(nullptr);
        net_clear_external_seats();
    }
    static bool paused() { return flag_is_set(kfx_sim_state.operation_flags, GOF_Paused); }
};
}

TEST_CASE_METHOD(HoldFixture, "with the policy off nothing is held", "[kfx_script][urgent_hold]") {
    api_urgent_hold_begin(2, 1);
    CHECK_FALSE(paused());
    CHECK_FALSE(api_urgent_hold_waiting(2, nullptr, nullptr));
}

TEST_CASE_METHOD(HoldFixture, "pause: an urgent decision pauses the game until the seat answers", "[kfx_script][urgent_hold]") {
    REQUIRE_FALSE(extseat_human_playing());
    api_urgent_hold_set_policy(UHM_Pause, 4, 60000);
    api_urgent_hold_begin(2, 1);
    CHECK(paused());
    CHECK(extseat_agent_owns_pause());
    enum UrgentHoldMode mode = UHM_None;
    int64_t left = 0;
    g_clock += 2500;
    REQUIRE(api_urgent_hold_waiting(2, &mode, &left));
    CHECK(mode == UHM_Pause);
    CHECK(left == 57500);
    CHECK_FALSE(api_urgent_hold_waiting(3, nullptr, nullptr));
    api_urgent_hold_answered(3, -1);                  // another seat's answer is not this one's
    CHECK(paused());
    api_urgent_hold_answered(2, -1);
    CHECK_FALSE(paused());
    CHECK_FALSE(api_urgent_hold_waiting(2, nullptr, nullptr));
}

TEST_CASE_METHOD(HoldFixture, "pause: a hold that interrupts an advance gives the rest of it back", "[kfx_script][urgent_hold]") {
    api_urgent_hold_set_policy(UHM_Pause, 4, 60000);
    extseat_pause();
    extseat_advance(100);
    extseat_tick();                               // the advance starts: the game runs
    g_turn += 30;
    REQUIRE_FALSE(paused());
    REQUIRE(extseat_advance_left() == 70);
    api_urgent_hold_begin(2, 1);
    CHECK(paused());
    CHECK_FALSE(extseat_advancing());
    api_urgent_hold_answered(2, -1);
    CHECK(extseat_advancing());                   // the 70 turns left are run, then it pauses again
    CHECK(extseat_advance_left() == 70);
}

TEST_CASE_METHOD(HoldFixture, "pause: a game the agent already holds paused is not held again", "[kfx_script][urgent_hold]") {
    api_urgent_hold_set_policy(UHM_Pause, 4, 60000);
    extseat_pause();
    api_urgent_hold_begin(2, 1);
    CHECK_FALSE(api_urgent_hold_waiting(2, nullptr, nullptr));
    api_urgent_hold_answered(2, -1);
    CHECK(paused());                              // and an answer does not resume what the agent paused
}

TEST_CASE_METHOD(HoldFixture, "slow: the game runs slowly until the answer, then at its own pace again", "[kfx_script][urgent_hold]") {
    api_urgent_hold_set_policy(UHM_Slow, 5, 60000);
    api_urgent_hold_begin(2, 1);
    CHECK(kfx_sim_state.turns_per_second == 5);
    CHECK_FALSE(paused());
    api_urgent_hold_answered(2, -1);
    CHECK(kfx_sim_state.turns_per_second == 20);
    // The agent setting the speed itself during a hold: the hold ends and leaves its speed alone.
    api_urgent_hold_begin(2, 1);
    kfx_sim_state.turns_per_second = 10;
    api_urgent_hold_cancel();
    api_urgent_hold_answered(2, -1);
    CHECK(kfx_sim_state.turns_per_second == 10);
}

TEST_CASE_METHOD(HoldFixture, "the cap ends a hold nobody answers, and a load forgets it", "[kfx_script][urgent_hold]") {
    api_urgent_hold_set_policy(UHM_Pause, 4, 5000);
    api_urgent_hold_begin(2, 1);
    REQUIRE(paused());
    g_clock += 4000;
    api_urgent_hold_tick();
    CHECK(paused());
    g_clock += 2000;
    api_urgent_hold_tick();
    CHECK_FALSE(paused());
    CHECK_FALSE(api_urgent_hold_waiting(2, nullptr, nullptr));
    // A load (the turn goes back) while held: that game is gone, nothing is restored.
    api_urgent_hold_set_policy(UHM_Slow, 4, 5000);
    api_urgent_hold_begin(2, 1);
    REQUIRE(kfx_sim_state.turns_per_second == 4);
    g_turn = 50;
    api_urgent_hold_tick();
    CHECK_FALSE(api_urgent_hold_waiting(2, nullptr, nullptr));
}

TEST_CASE_METHOD(HoldFixture, "never in a game a human plays, unless its setup allowed the agent to pause", "[kfx_script][urgent_hold]") {
    kfx_sim_state.players[0].allocflags |= PlaF_Allocated;
    kfx_sim_state.players[0].user_id = SOLO_HUMAN_ID;
    my_player_number = 0;
    REQUIRE(extseat_human_playing());
    api_urgent_hold_set_policy(UHM_Pause, 4, 60000);
    api_urgent_hold_begin(2, 1);
    CHECK_FALSE(paused());
    extseat_allow_pause_with_human(true);         // the Skirmish page's LLM Pause
    api_urgent_hold_begin(2, 1);
    CHECK(paused());
    extseat_allow_pause_with_human(false);
}

TEST_CASE_METHOD(HoldFixture, "an answer to an earlier decision leaves the latest urgent one held; a trigger's order answers none", "[kfx_script][urgent_hold]") {
    api_urgent_hold_set_policy(UHM_Pause, 4, 60000);
    api_urgent_hold_begin(2, 5);
    api_urgent_hold_begin(2, 6);                  // a second urgent decision while held
    api_urgent_hold_answered(2, 0);               // a trigger's order
    CHECK(paused());
    api_urgent_hold_answered(2, 5);               // the answer to the first
    CHECK(paused());
    api_urgent_hold_answered(2, 6);
    CHECK_FALSE(paused());
}
