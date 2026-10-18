// kfx_net: net_input_lag.c -- the adaptive multiplayer input lag (upstream #5462, "Rework input lag
// calculation"). Driven here as a pure state machine: a fake LbTimerClock and gameturn source, two
// network users mapped to players (host 0 = this machine, user 1 = the peer), and netstate.sp left
// NULL so input_lag_update() returns before the host's max-over-peers aggregation (which needs a
// live session). Packet history is the real one (store_packet_history()/initialize_packet_history(),
// net_exchange_gameplay.c), since which turns may be dropped is read from it.
#include <catch2/catch_test_macros.hpp>

#include "net_input_lag.h"
#include "net_exchange_gameplay.h"
#include "net_game.h"
#include "net_main.h"
#include "packet_data.h"
#include "player_data.h"
#include "bflib_datetm.h"
#include "kfx_sim_state.h"
#include "kfx_net_state.h"

#include <cstring>
#include <vector>

namespace {
struct ResetSimState {
    ResetSimState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        local_system_flags = 0; // out of kfx_sim_state since 1ec821fa4, so the memset no longer clears it
    }
};

GameTurn g_turn = 0;
TbClockMSec g_clock = 0;
TbClockMSec fake_clock() { return g_clock; }

constexpr int64_t TPS = 20; // 50 ms a turn
constexpr int64_t TURN_MS = 1000 / TPS;

// A two-user network game: this machine is the host (user 0), user 1 the peer.
struct LagFixture {
    TbClockMSec (*saved_clock)(void) = nullptr;
    struct NetState saved_netstate;

    explicit LagFixture(int64_t start_lag = 0) {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_net_state, 0, sizeof(kfx_net_state));
        std::memset(net_user_info, 0, sizeof(net_user_info));
        std::memcpy(&saved_netstate, &netstate, sizeof(netstate));
        std::memset(&netstate, 0, sizeof(netstate));
        netstate.my_id = SERVER_ID;
        local_system_flags = GSF_NetworkActive;
        net_user_info[SERVER_ID].network_user_active = 1;
        net_user_info[1].network_user_active = 1;
        my_player_number = 0;
        setup_network_player_numbers();
        kfx_sim_state.turns_per_second = TPS;
        g_turn = 200;
        g_clock = 100000;
        set_gameturn_source(&g_turn);
        saved_clock = LbTimerClock;
        LbTimerClock = &fake_clock;
        input_lag_set_fixed(-1);
        kfx_net_state.input_lag_turns = start_lag;
        initialize_packet_history(); // also input_lag_reset(): request = target = start_lag
    }
    ~LagFixture() {
        initialize_packet_history();
        input_lag_set_fixed(-1);
        LbTimerClock = saved_clock;
        set_gameturn_source(nullptr);
        std::memcpy(&netstate, &saved_netstate, sizeof(netstate));
        std::memset(net_user_info, 0, sizeof(net_user_info));
        local_system_flags = 0;
    }

    // A non-empty packet for `turn` with no action and no clicks: droppable.
    static struct Packet plain_packet(GameTurn turn) {
        struct Packet p;
        std::memset(&p, 0, sizeof(p));
        p.turn = turn;
        p.checksum = 0x1234;
        return p;
    }
    static void store(NetUserId user, const struct Packet &p) { store_packet_history(user, &p); }

    // What the host sends: the synchronized target in input_lag_turns.
    static void host_says(int64_t target) {
        struct Packet p = plain_packet(get_gameturn() - kfx_net_state.input_lag_turns);
        p.input_lag_turns = (int8_t)target;
        input_lag_observe_host_packet(&p);
    }
};

struct LagFixture3 : LagFixture { LagFixture3() : LagFixture(3) {} };
}

TEST_CASE_METHOD(ResetSimState, "input_lag_skips_processing is false when no network session is active", "[kfx_net][net_input_lag]") {
    // local_system_flags left at 0: GSF_NetworkActive unset.
    CHECK_FALSE(input_lag_skips_processing());
}

TEST_CASE_METHOD(ResetSimState, "input_lag_needs_lookahead is false when no network session is active", "[kfx_net][net_input_lag]") {
    CHECK_FALSE(input_lag_needs_lookahead());
}

// Fork bug 1: a decrease used to drop the turn gameturn - lag unconditionally, losing whatever
// action or click anyone made in it. Only a turn nobody acted in may be dropped.
TEST_CASE_METHOD(LagFixture3, "a decrease never drops a turn in which someone acted or clicked", "[kfx_net][net_input_lag]") {
    const GameTurn acted_turn = g_turn - 3;
    struct Packet p = plain_packet(acted_turn);
    SECTION("a packet action") { p.action = PckA_SetPlyrState; }
    SECTION("a left click") { p.control_flags = PCtr_LBtnClick; }
    SECTION("a right release") { p.control_flags = PCtr_RBtnRelease; }
    SECTION("a tilt reset") { p.control_flags = PCtr_ViewTiltReset; }
    store(1, p);
    store(SERVER_ID, plain_packet(acted_turn));
    host_says(2);

    CHECK_FALSE(input_lag_needs_lookahead());
    CHECK_FALSE(input_lag_skips_processing());
    CHECK(kfx_net_state.input_lag_turns == 3); // the acted-in turn is processed, not discarded

    // the next turn is idle: that one is dropped instead
    g_turn += 1;
    store(1, plain_packet(g_turn - 3));
    store(SERVER_ID, plain_packet(g_turn - 3));
    CHECK(input_lag_needs_lookahead());
    CHECK_FALSE(input_lag_skips_processing());
    CHECK(kfx_net_state.input_lag_turns == 2);
}

TEST_CASE_METHOD(LagFixture3, "a decrease drops an idle turn, held buttons and camera moves included", "[kfx_net][net_input_lag]") {
    struct Packet p = plain_packet(g_turn - 3);
    p.control_flags = PCtr_LBtnHeld | PCtr_MoveUp | PCtr_MapCoordsValid;
    store(1, p);
    store(SERVER_ID, plain_packet(g_turn - 3));
    host_says(2);
    CHECK_FALSE(input_lag_skips_processing());
    CHECK(kfx_net_state.input_lag_turns == 2);
}

// Fork bug 2: load_old_packets() hands input_lag_observe_host_packet() a zeroed host packet when
// there is no history packet for the turn (after a wait timeout); that read as "target 0" and
// started dropping turns towards no lag at all.
TEST_CASE_METHOD(LagFixture3, "an empty host packet does not change the input lag target", "[kfx_net][net_input_lag]") {
    struct Packet empty;
    std::memset(&empty, 0, sizeof(empty));
    input_lag_observe_host_packet(&empty);
    CHECK_FALSE(input_lag_needs_lookahead());
    CHECK_FALSE(input_lag_skips_processing());
    CHECK(kfx_net_state.input_lag_turns == 3);
}

// ---- the adaptive request: per-turn packet delays, measured on arrival ----
namespace {
struct DelayFixture : LagFixture {
    explicit DelayFixture(int64_t start_lag = 0) : LagFixture(start_lag) {}

    // One turn: send our packet (input_lag_update() stamps it and re-evaluates the request from the
    // delays seen so far), then the peer's packet for the same turn arrives `delay_ms` later.
    // Returns the request input_lag_update() wrote into our packet.
    int64_t turn(int64_t delay_ms) {
        g_clock = 100000 + (TbClockMSec)g_turn * TURN_MS;
        struct Packet pkt;
        std::memset(&pkt, 0, sizeof(pkt));
        input_lag_update(&pkt);
        g_clock += delay_ms;
        input_lag_note_packet_arrival(g_turn);
        g_turn += 1;
        return pkt.input_lag_turns;
    }
    int64_t turns(int64_t count, int64_t delay_ms) {
        int64_t request = 0;
        for (int64_t i = 0; i < count; i++)
            request = turn(delay_ms);
        return request;
    }
    static struct InputLagStats stats() {
        struct InputLagStats s;
        input_lag_get_stats(&s);
        return s;
    }
};
struct DelayFixture2 : DelayFixture { DelayFixture2() : DelayFixture(2) {} };

// ceil((delay - 35 ms tolerated) * 20 turns/s / 1000)
constexpr int64_t SPIKE_MS = 500;
constexpr int64_t SPIKE_TURNS = (SPIKE_MS - INPUT_LAG_TOLERATED_WAIT_MS) * TPS / 1000 + 1; // 465 -> 9.3 -> 10
}

TEST_CASE_METHOD(DelayFixture, "the worst two delays in the window are ignored as spikes", "[kfx_net][net_input_lag]") {
    REQUIRE(SPIKE_TURNS == 10);
    CHECK(turns(30, 0) == 0);
    turn(SPIKE_MS);
    CHECK(turns(5, 0) == 0);
    turn(SPIKE_MS);
    CHECK(turns(5, 0) == 0); // two spikes in 20 turns: ignored
    CHECK(stats().increase_level == 0);
    turn(SPIKE_MS);          // a third one inside the 20-turn window counts
    CHECK(turn(0) == SPIKE_TURNS);
    CHECK(stats().packet_delay == SPIKE_MS);
    CHECK(stats().increase_level == SPIKE_TURNS);
}

TEST_CASE_METHOD(DelayFixture, "a delay raises the request within the 20-turn increase window, then stops counting", "[kfx_net][net_input_lag]") {
    turns(30, 0);
    const GameTurn s = g_turn;
    turns(3, SPIKE_MS);                   // turns s, s+1, s+2
    // the update at turn u sees the delays of turns u-20 .. u-1
    turns((int64_t)(s + 20 - g_turn), 0); // up to and including the update at s+19
    CHECK(turn(0) == SPIKE_TURNS);        // update at s+20: turn s still inside
    CHECK(stats().increase_level == SPIKE_TURNS);
    turn(0);                              // update at s+21: turn s has left the window
    CHECK(stats().increase_level == 0);
    CHECK(stats().packet_delay == 0);
    // the request does not follow it down: that is the 300-turn window's job
    CHECK(turn(0) == SPIKE_TURNS);
}

TEST_CASE_METHOD(DelayFixture, "a decrease waits for the delays to leave the 300-turn window", "[kfx_net][net_input_lag]") {
    turns(30, 0);
    const GameTurn s = g_turn;
    turns(3, SPIKE_MS);
    turns((int64_t)(s + 300 - g_turn), 0);
    CHECK(stats().sample_count == INPUT_LAG_DECREASE_WINDOW_TURNS);
    CHECK(turn(0) == SPIKE_TURNS);  // update at s+300: turn s still inside the long window
    CHECK(stats().decrease_level == SPIKE_TURNS);
    CHECK(turn(0) == 0);            // update at s+301: it has left, and the request drops in one step
    CHECK(stats().decrease_level == 0);
}

TEST_CASE_METHOD(DelayFixture2, "no decrease before 300 turns have been measured", "[kfx_net][net_input_lag]") {
    // 70 ms: 1 turn either way (35 ms over the tolerance -> 0.7; 45 ms over the decrease's -> 0.9)
    CHECK(turns(300, 70) == 2);     // 300 updates, the first of which had nothing to measure
    CHECK(stats().sample_count == INPUT_LAG_DECREASE_WINDOW_TURNS);
    CHECK(turn(70) == 1);
}

TEST_CASE_METHOD(DelayFixture2, "a decrease needs 10 ms of headroom over the tolerated wait", "[kfx_net][net_input_lag]") {
    // 80 ms: raises to 1 turn (45 ms over 35 -> 0.9), but a decrease measures against 25 ms
    // (55 ms -> 1.1 -> 2), so a lag of 2 stays put instead of oscillating between 1 and 2.
    CHECK(turns(400, 80) == 2);
    CHECK(stats().increase_level == 1);
    CHECK(stats().decrease_level == 2);
}

TEST_CASE_METHOD(DelayFixture, "a pinned input lag measures but never changes its request", "[kfx_net][net_input_lag]") {
    input_lag_set_fixed(0);
    turns(30, 0);
    turns(3, SPIKE_MS);
    CHECK(turn(0) == 0);
    CHECK(stats().increase_level == SPIKE_TURNS);
}

TEST_CASE_METHOD(DelayFixture, "only the first copy of a peer's packet is timed", "[kfx_net][net_input_lag]") {
    turns(30, 0);
    for (int64_t i = 0; i < 3; i++) {
        g_clock = 100000 + (TbClockMSec)g_turn * TURN_MS;
        struct Packet own;
        std::memset(&own, 0, sizeof(own));
        input_lag_update(&own);
        own.turn = g_turn;
        const struct Packet peer = plain_packet(g_turn);
        g_clock += 60;
        store(1, peer);           // first arrival: 60 ms
        g_clock += 400;
        store(1, peer);           // a redundant resend of the same turn: not a new arrival
        store(SERVER_ID, own);    // our own packet: never timed
        g_turn += 1;
    }
    turn(0);
    CHECK(stats().packet_delay == 60);
}

// The increase is applied the moment the host's packet says so; the input turn then steps back, and
// the turns already processed are skipped (input_lag_processed_turn), never processed twice.
TEST_CASE_METHOD(DelayFixture2, "an increase skips the input turns already processed", "[kfx_net][net_input_lag]") {
    std::vector<GameTurn> processed;
    int64_t skipped = 0;
    auto process_turn = [&]() {
        if (input_lag_skips_processing())
            skipped++;
        else
            processed.push_back(get_gameturn() - kfx_net_state.input_lag_turns);
    };
    process_turn();                // turn 200: input turn 198
    g_turn += 1;
    process_turn();                // turn 201: input turn 199
    host_says(4);                  // load_old_packets() of turn 201 reads the new target
    CHECK(kfx_net_state.input_lag_turns == 4);
    host_says(4);                  // the same target again changes nothing
    CHECK(kfx_net_state.input_lag_turns == 4);
    for (int64_t i = 0; i < 4; i++) {
        g_turn += 1;
        process_turn();            // 202, 203: input turns 198, 199 again -> skipped; 204, 205 -> 200, 201
    }
    CHECK(skipped == 2);
    CHECK(processed == std::vector<GameTurn>{198, 199, 200, 201});
}
