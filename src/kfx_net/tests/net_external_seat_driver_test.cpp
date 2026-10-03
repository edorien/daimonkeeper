// kfx_net: the External seat driver (external_seat.h) -- a per-seat step queue written into the seat's packet once
// per simulated turn, plus the agent-owned pause: advance, the stuck-pause watchdog, disconnect.
// Regression note: an advance must start in extseat_tick(), not in the API poll, or the first simulated turn of an
// advance runs before any step can be written (an "advance 1" once applied nothing).
#include <catch2/catch_test_macros.hpp>

#include "external_seat.h"
#include "net_game.h"
#include "packet_data.h"
#include "player_data.h"
#include "bflib_datetm.h"
#include "kfx_sim_state.h"
#include "kfx_net_state.h"
#include "kfx_config_state.h"

#include <cstring>

namespace {
GameTurn g_turn = 100;
TbClockMSec g_clock = 1000;
GameTurn fake_turn() { return g_turn; }
TbClockMSec fake_clock() { return g_clock; }
TbClockMSec (*saved_clock)(void) = nullptr;

struct DriverFixture {
    DriverFixture() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_net_state, 0, sizeof(kfx_net_state));
        std::memset(sim_packets, 0, sizeof(sim_packets));
        kfx_config_state.neutral_player_num = PLAYER_NEUTRAL;
        kfx_sim_state.game_kind = GKind_LocalGame;
        my_player_number = 0;
        for (int64_t i = 0; i < PLAYERS_COUNT; i++) {
            kfx_sim_state.players[i].id_number = (PlayerNumber)i;
            kfx_sim_state.players[i].user_id = -1;
        }
        kfx_sim_state.players[0].allocflags |= PlaF_Allocated;
        kfx_sim_state.players[0].is_active = 1;
        kfx_sim_state.players[0].user_id = SOLO_HUMAN_ID;
        net_clear_external_seats();
        seat_user = net_add_external_seat(2);
        g_turn = 100;
        g_clock = 1000;
        set_get_gameturn_provider(&fake_turn);
        saved_clock = LbTimerClock;
        LbTimerClock = &fake_clock;
        extseat_reset();
        extseat_set_watchdog_ms(EXTSEAT_DEFAULT_WATCHDOG_MS);
    }
    ~DriverFixture() {
        LbTimerClock = saved_clock;
        set_get_gameturn_provider(nullptr);
        net_clear_external_seats();
    }
    static bool paused() { return flag_is_set(kfx_sim_state.operation_flags, GOF_Paused); }
    static ExtSeatStep action_step(unsigned char action, int64_t p1 = 0) {
        ExtSeatStep s; std::memset(&s, 0, sizeof(s)); s.action = action; s.par1 = p1; return s;
    }
    static ExtSeatStep click_step(int64_t x, int64_t y, uint64_t flags) {
        ExtSeatStep s; std::memset(&s, 0, sizeof(s)); s.has_pos = true; s.pos_x = x; s.pos_y = y; s.control_flags = flags; return s;
    }
    NetUserId seat_user = -1;
};
}

TEST_CASE_METHOD(DriverFixture, "a queued gesture blocks a second one until it has been written out", "[kfx_net][extseat]") {
    REQUIRE(seat_user == 1);
    CHECK(extseat_idle(1));
    ExtSeatStep steps[2] = { action_step(PckA_SetPlyrState, PSt_Slap), click_step(300, 400, PCtr_LBtnRelease) };
    CHECK(extseat_enqueue(1, steps, 2));
    CHECK_FALSE(extseat_idle(1));
    CHECK_FALSE(extseat_enqueue(1, steps, 1));
    extseat_tick();
    CHECK_FALSE(extseat_idle(1)); // one step left
    extseat_tick();
    CHECK(extseat_idle(1));
    CHECK(extseat_enqueue(1, steps, 1)); // free again
}

TEST_CASE_METHOD(DriverFixture, "enqueue rejects the local human, a bad user id, and empty or oversized gestures", "[kfx_net][extseat]") {
    ExtSeatStep s = action_step(PckA_SetPlyrState);
    CHECK_FALSE(extseat_enqueue(SOLO_HUMAN_ID, &s, 1));
    CHECK_FALSE(extseat_enqueue(-1, &s, 1));
    CHECK_FALSE(extseat_enqueue(MAX_NET_USERS, &s, 1));
    CHECK_FALSE(extseat_enqueue(1, &s, 0));
    ExtSeatStep many[EXTSEAT_MAX_STEPS + 1];
    for (auto &m : many) m = s;
    CHECK_FALSE(extseat_enqueue(1, many, EXTSEAT_MAX_STEPS + 1));
}

TEST_CASE_METHOD(DriverFixture, "tick writes one step per turn into the seat's own packet only", "[kfx_net][extseat]") {
    ExtSeatStep steps[2] = { action_step(PckA_SetPlyrState, 9), click_step(640, 384, PCtr_LBtnClick) };
    REQUIRE(extseat_enqueue(1, steps, 2));

    extseat_tick();
    CHECK(sim_packets[1].action == PckA_SetPlyrState);
    CHECK(sim_packets[1].actn_par1 == 9);
    CHECK((sim_packets[1].control_flags & PCtr_MapCoordsValid) == 0);
    CHECK(sim_packets[0].action == PckA_None); // the human's packet is untouched

    std::memset(sim_packets, 0, sizeof(sim_packets)); // process_packets() clears every turn
    extseat_tick();
    CHECK(sim_packets[1].action == PckA_None);
    CHECK(sim_packets[1].pos_x == 640);
    CHECK(sim_packets[1].pos_y == 384);
    CHECK((sim_packets[1].control_flags & PCtr_MapCoordsValid) != 0);
    CHECK((sim_packets[1].control_flags & PCtr_LBtnClick) != 0);

    std::memset(sim_packets, 0, sizeof(sim_packets));
    extseat_tick();
    CHECK(sim_packets[1].action == PckA_None);
    CHECK(sim_packets[1].control_flags == 0); // nothing left
}

TEST_CASE_METHOD(DriverFixture, "nothing is written while the game is paused, and the step waits", "[kfx_net][extseat]") {
    ExtSeatStep s = action_step(PckA_UsePwrHandPick, 77);
    REQUIRE(extseat_enqueue(1, &s, 1));
    set_flag(kfx_sim_state.operation_flags, GOF_Paused);
    extseat_tick();
    extseat_tick();
    CHECK(sim_packets[1].action == PckA_None);
    CHECK_FALSE(extseat_idle(1));
    clear_flag(kfx_sim_state.operation_flags, GOF_Paused);
    extseat_tick();
    CHECK(sim_packets[1].action == PckA_UsePwrHandPick);
    CHECK(sim_packets[1].actn_par1 == 77);
}

TEST_CASE_METHOD(DriverFixture, "an advance unpauses in the tick, so the first turn of the advance carries the step", "[kfx_net][extseat]") {
    extseat_pause();
    CHECK(paused());
    CHECK(extseat_agent_owns_pause());
    ExtSeatStep s = action_step(PckA_UsePwrHandPick, 5);
    REQUIRE(extseat_enqueue(1, &s, 1));

    extseat_advance(1);
    CHECK(extseat_advancing());
    CHECK(paused());               // still paused: the tick starts it
    extseat_poll();
    CHECK(paused());               // the poll does not start it either

    extseat_tick();
    CHECK_FALSE(paused());
    CHECK(sim_packets[1].action == PckA_UsePwrHandPick); // the regression: this used to be lost
    g_turn += 1;                    // the turn ran
    extseat_poll();
    CHECK(paused());
    CHECK_FALSE(extseat_advancing());
    CHECK(extseat_agent_owns_pause());
}

TEST_CASE_METHOD(DriverFixture, "an advance runs the requested number of turns and then pauses again", "[kfx_net][extseat]") {
    extseat_pause();
    extseat_advance(3);
    extseat_tick();
    for (int i = 0; i < 2; i++) {
        extseat_poll();
        CHECK_FALSE(paused());
        g_turn += 1;
        extseat_tick();
    }
    extseat_poll();
    CHECK_FALSE(paused());
    g_turn += 1; // third turn done
    extseat_poll();
    CHECK(paused());
    CHECK_FALSE(extseat_advancing());
}

TEST_CASE_METHOD(DriverFixture, "a step is not written on the tick where a finished advance is about to pause", "[kfx_net][extseat]") {
    extseat_pause();
    extseat_advance(1);
    extseat_tick();
    g_turn += 1;               // advance is over, the next poll will pause
    ExtSeatStep s = action_step(PckA_UsePwrHandPick, 5);
    REQUIRE(extseat_enqueue(1, &s, 1));
    extseat_tick();            // still unpaused here, but must not write into a turn that will not run
    CHECK(sim_packets[1].action == PckA_None);
    CHECK_FALSE(extseat_idle(1));
}

TEST_CASE_METHOD(DriverFixture, "losing the client resumes the game only if the agent held the pause", "[kfx_net][extseat]") {
    set_flag(kfx_sim_state.operation_flags, GOF_Paused); // a human's pause
    extseat_on_client_lost();
    CHECK(paused());

    extseat_pause();
    CHECK(paused());
    extseat_on_client_lost();
    CHECK_FALSE(paused());
    CHECK_FALSE(extseat_agent_owns_pause());
}

TEST_CASE_METHOD(DriverFixture, "the watchdog resumes a pause the agent has held too long without a command", "[kfx_net][extseat]") {
    extseat_pause();
    g_clock += EXTSEAT_DEFAULT_WATCHDOG_MS - 1000;
    extseat_poll();
    CHECK(paused());
    extseat_note_activity();          // a command arrives: the clock restarts
    g_clock += EXTSEAT_DEFAULT_WATCHDOG_MS - 1000;
    extseat_poll();
    CHECK(paused());
    g_clock += 2000;
    extseat_poll();
    CHECK_FALSE(paused());
    CHECK_FALSE(extseat_agent_owns_pause());
}

TEST_CASE_METHOD(DriverFixture, "the watchdog limit is configurable and zero disables it", "[kfx_net][extseat]") {
    extseat_set_watchdog_ms(500);
    extseat_pause();
    g_clock += 400;
    extseat_poll();
    CHECK(paused());
    g_clock += 200;
    extseat_poll();
    CHECK_FALSE(paused());

    extseat_set_watchdog_ms(0);
    extseat_pause();
    g_clock += 10000000;
    extseat_poll();
    CHECK(paused());
}

TEST_CASE_METHOD(DriverFixture, "the watchdog leaves a human's pause alone", "[kfx_net][extseat]") {
    set_flag(kfx_sim_state.operation_flags, GOF_Paused);
    g_clock += 10 * EXTSEAT_DEFAULT_WATCHDOG_MS;
    extseat_poll();
    CHECK(paused());
}

TEST_CASE_METHOD(DriverFixture, "a queue whose seat has gone away is dropped instead of written", "[kfx_net][extseat]") {
    ExtSeatStep s = action_step(PckA_UsePwrHandPick, 5);
    REQUIRE(extseat_enqueue(1, &s, 1));
    net_clear_external_seats(); // e.g. a load without the seat; also resets the driver
    CHECK(extseat_idle(1));
    extseat_tick();
    CHECK(sim_packets[1].action == PckA_None);
}

TEST_CASE_METHOD(DriverFixture, "reset forgets queued steps and the agent's pause", "[kfx_net][extseat]") {
    extseat_pause();
    ExtSeatStep s = action_step(PckA_UsePwrHandPick, 5);
    REQUIRE(extseat_enqueue(1, &s, 1));
    extseat_advance(5);
    extseat_reset();
    CHECK(extseat_idle(1));
    CHECK_FALSE(extseat_agent_owns_pause());
    CHECK_FALSE(extseat_advancing());
}

TEST_CASE_METHOD(DriverFixture, "verbs are validated against the seat before anything is queued", "[kfx_net][extseat]") {
    ExtSeatVerb v; std::memset(&v, 0, sizeof(v));
    v.kind = ESV_PlaceTrap;
    CHECK(std::string(extseat_submit_verb(SOLO_HUMAN_ID, 0, &v, nullptr)) == "NOT_A_VALID_SEAT");
    CHECK(std::string(extseat_submit_verb(1, 3, &v, nullptr)) == "NOT_A_VALID_SEAT"); // user 1 is player 2's seat
    CHECK(std::string(extseat_submit_verb(1, 2, &v, nullptr)) == "MISSING_POSITION");
    v.has_pos = true; v.stl_x = 100000; v.stl_y = 5;
    kfx_sim_state.map_subtiles_x = 255; kfx_sim_state.map_subtiles_y = 255;
    CHECK(std::string(extseat_submit_verb(1, 2, &v, nullptr)) == "POSITION_OFF_MAP");
    v.kind = ESV_None;
    CHECK(std::string(extseat_submit_verb(1, 2, &v, nullptr)) == "UNKNOWN_VERB");
    CHECK(extseat_idle(1)); // nothing was queued by any of the above

    ExtSeatStep s = action_step(PckA_UsePwrHandPick, 5);
    REQUIRE(extseat_enqueue(1, &s, 1));
    v.kind = ESV_PlaceTrap;
    CHECK(std::string(extseat_submit_verb(1, 2, &v, nullptr)) == "ACTION_ALREADY_QUEUED");
}

// ---- M4: the area verbs' step geometry, mid-gesture tracking and cancel ---------------------------------------------
namespace {
constexpr uint64_t PRESS = PCtr_LBtnClick | PCtr_LBtnHeld;
int64_t slab_of(const ExtSeatStep &s, bool y) { return (y ? s.pos_y : s.pos_x) / (COORD_PER_STL * STL_PER_SLB); }
}

TEST_CASE("a dig sweep visits every row of the rectangle with a step down between rows", "[kfx_net][extseat]") {
    ExtSeatStep out[EXTSEAT_MAX_STEPS];
    const int64_t n = extseat_plan_dig_sweep(out, EXTSEAT_MAX_STEPS, 2, 5, 5, 7); // 4 wide, 3 tall
    REQUIRE(n == 10);
    CHECK(out[0].action == PckA_SetPlyrState);
    CHECK(out[0].par1 == PSt_CtrlDungeon);
    CHECK(out[1].action == PckA_SetRoomspaceMan);
    CHECK(out[1].par1 == 1);                     // a 1x1 brush
    CHECK_FALSE(out[2].has_pos);                 // the idle turn between the mode and the press
    CHECK(out[2].action == PckA_None);
    // The cursor path: press at the corner, sweep the row, step down, sweep back, step down, sweep on, release.
    const int64_t want[7][2] = { {2,5}, {5,5}, {5,6}, {2,6}, {2,7}, {5,7}, {5,7} };
    for (int i = 0; i < 7; i++) {
        const ExtSeatStep &s = out[3 + i];
        CAPTURE(i);
        CHECK(s.has_pos);
        CHECK(slab_of(s, false) == want[i][0]);
        CHECK(slab_of(s, true) == want[i][1]);
        CHECK(s.context == CSt_PickAxe);
    }
    CHECK(out[3].control_flags == PRESS);
    for (int i = 4; i < 9; i++) CHECK(out[i].control_flags == PCtr_LBtnHeld);
    CHECK(out[9].control_flags == PCtr_LBtnRelease);
}

TEST_CASE("a dig sweep accepts the corners in any order and a single row", "[kfx_net][extseat]") {
    ExtSeatStep a[EXTSEAT_MAX_STEPS], b[EXTSEAT_MAX_STEPS];
    const int64_t na = extseat_plan_dig_sweep(a, EXTSEAT_MAX_STEPS, 2, 5, 5, 7);
    const int64_t nb = extseat_plan_dig_sweep(b, EXTSEAT_MAX_STEPS, 5, 7, 2, 5);
    REQUIRE(na == nb);
    for (int64_t i = 0; i < na; i++) { CHECK(a[i].pos_x == b[i].pos_x); CHECK(a[i].pos_y == b[i].pos_y); }
    CHECK(extseat_plan_dig_sweep(a, EXTSEAT_MAX_STEPS, 4, 4, 9, 4) == 6); // one row: setup 3, press, sweep, release
}

TEST_CASE("a dig sweep refuses an area taller than the cap or one that does not fit", "[kfx_net][extseat]") {
    ExtSeatStep out[EXTSEAT_MAX_STEPS];
    CHECK(extseat_plan_dig_sweep(out, EXTSEAT_MAX_STEPS, 0, 0, 3, EXTSEAT_MAX_DIG_ROWS) == 0); // one row too many
    CHECK(extseat_plan_dig_sweep(out, EXTSEAT_MAX_STEPS, 0, 0, 3, EXTSEAT_MAX_DIG_ROWS - 1) > 0);
    CHECK(extseat_plan_dig_sweep(out, 5, 0, 0, 3, 3) == 0);                                   // output too small
    static_assert(3 + 1 + 2 * EXTSEAT_MAX_DIG_ROWS + 1 <= EXTSEAT_MAX_STEPS, "the tallest dig must fit a queue");
}

TEST_CASE("selling clicks each slab once: press then release, in the sell tool", "[kfx_net][extseat]") {
    ExtSeatStep out[EXTSEAT_MAX_STEPS];
    const int64_t xy[] = { 4, 9, 5, 9, 6, 9 };
    const int64_t n = extseat_plan_slab_clicks(out, EXTSEAT_MAX_STEPS, xy, 3);
    REQUIRE(n == 3 + 6);
    CHECK(out[0].action == PckA_SetPlyrState);
    CHECK(out[0].par1 == PSt_Sell);
    CHECK(out[1].action == PckA_SetRoomspaceMan);
    CHECK(out[1].par1 == 1);
    for (int i = 0; i < 3; i++) {
        const ExtSeatStep &down = out[3 + 2 * i], &up = out[4 + 2 * i];
        CAPTURE(i);
        CHECK(down.control_flags == PRESS);
        CHECK(up.control_flags == PCtr_LBtnRelease);
        CHECK(slab_of(down, false) == xy[2 * i]);
        CHECK(slab_of(down, true) == 9);
        CHECK(down.pos_x == up.pos_x);
        CHECK(down.pos_y == up.pos_y);
        CHECK(down.context == 0);
    }
    static_assert(3 + 2 * EXTSEAT_MAX_SELL_SLABS <= EXTSEAT_MAX_STEPS, "the biggest sell must fit a queue");
    CHECK(extseat_plan_slab_clicks(out, EXTSEAT_MAX_STEPS, xy, 0) == 0);
    CHECK(extseat_plan_slab_clicks(out, 6, xy, 3) == 0);
}

TEST_CASE_METHOD(DriverFixture, "the seat is mid-gesture between the press and the release", "[kfx_net][extseat]") {
    ExtSeatStep steps[3] = { click_step(300, 300, PRESS), click_step(400, 300, PCtr_LBtnHeld), click_step(400, 300, PCtr_LBtnRelease) };
    REQUIRE(extseat_enqueue(1, steps, 3));
    CHECK_FALSE(extseat_mid_gesture(1));
    extseat_tick();
    CHECK(extseat_mid_gesture(1));
    extseat_tick();
    CHECK(extseat_mid_gesture(1));
    extseat_tick();
    CHECK_FALSE(extseat_mid_gesture(1));
    CHECK(extseat_idle(1));
}

TEST_CASE_METHOD(DriverFixture, "cancelling mid-drag drops the rest and queues a safe way out", "[kfx_net][extseat]") {
    ExtSeatStep steps[5] = { action_step(PckA_SetPlyrState, PSt_BuildRoom), click_step(300, 300, PRESS), click_step(400, 300, PCtr_LBtnHeld),
                             click_step(500, 300, PCtr_LBtnHeld), click_step(500, 300, PCtr_LBtnRelease) };
    REQUIRE(extseat_enqueue(1, steps, 5));
    extseat_tick(); extseat_tick(); extseat_tick(); // state, press, hold: the button is down
    REQUIRE(extseat_mid_gesture(1));

    CHECK(extseat_cancel(1) == 2);                 // the second hold and the release were dropped
    CHECK_FALSE(extseat_idle(1));                  // ...and the way out is queued instead

    std::memset(sim_packets, 0, sizeof(sim_packets));
    extseat_tick();
    CHECK(sim_packets[1].action == PckA_SetPlyrState);   // leave the tool: this is what aborts the drag
    CHECK(sim_packets[1].actn_par1 == PSt_CtrlDungeon);
    std::memset(sim_packets, 0, sizeof(sim_packets));
    extseat_tick();
    CHECK((sim_packets[1].control_flags & PCtr_LBtnRelease) != 0); // and clear the button-down bookkeeping
    CHECK_FALSE(extseat_mid_gesture(1));
    CHECK(extseat_idle(1));
}

TEST_CASE_METHOD(DriverFixture, "cancelling when no button is down only drops what is queued", "[kfx_net][extseat]") {
    ExtSeatStep steps[3] = { action_step(PckA_SetPlyrState, PSt_Sell), action_step(PckA_SetRoomspaceMan, 1), click_step(300, 300, PRESS) };
    REQUIRE(extseat_enqueue(1, steps, 3));
    extseat_tick();
    CHECK_FALSE(extseat_mid_gesture(1));
    CHECK(extseat_cancel(1) == 2);
    CHECK(extseat_idle(1));                        // nothing queued to clean up
    CHECK(extseat_cancel(1) == 0);
    CHECK(extseat_cancel(SOLO_HUMAN_ID) == 0);
    CHECK(extseat_cancel(-1) == 0);
}

TEST_CASE_METHOD(DriverFixture, "the cancel verb works while a gesture is queued and needs no rectangle", "[kfx_net][extseat]") {
    ExtSeatStep s = action_step(PckA_UsePwrHandPick, 5);
    REQUIRE(extseat_enqueue(1, &s, 1));
    ExtSeatVerb v; std::memset(&v, 0, sizeof(v));
    v.kind = ESV_Cancel;
    int64_t dropped = -1;
    CHECK(extseat_submit_verb(1, 2, &v, &dropped) == nullptr);
    CHECK(dropped == 1);
    CHECK(extseat_idle(1));
    // ...but still only for the right seat
    CHECK(std::string(extseat_submit_verb(1, 3, &v, nullptr)) == "NOT_A_VALID_SEAT");
}

TEST_CASE_METHOD(DriverFixture, "the area verbs need a rectangle, on the map, within the caps", "[kfx_net][extseat]") {
    kfx_sim_state.map_tiles_x = 85; kfx_sim_state.map_tiles_y = 85;
    ExtSeatVerb v; std::memset(&v, 0, sizeof(v));
    for (auto kind : { ESV_MarkDig, ESV_Sell, ESV_BuildRoom }) {
        v.kind = kind;
        CHECK(std::string(extseat_submit_verb(1, 2, &v, nullptr)) == "MISSING_RECT");
    }
    v.has_rect = true; v.slab_x0 = 0; v.slab_y0 = 0; v.slab_x1 = 200; v.slab_y1 = 3;
    for (auto kind : { ESV_MarkDig, ESV_Sell, ESV_BuildRoom }) {
        v.kind = kind;
        CHECK(std::string(extseat_submit_verb(1, 2, &v, nullptr)) == "POSITION_OFF_MAP");
    }
    v.kind = ESV_MarkDig; v.slab_x1 = 3; v.slab_y1 = EXTSEAT_MAX_DIG_ROWS;
    CHECK(std::string(extseat_submit_verb(1, 2, &v, nullptr)) == "AREA_TOO_LARGE");
    v.slab_x1 = 84; v.slab_y1 = 3;   // 85 x 4 = 340 slabs: more than the engine can keep as dig marks
    CHECK(std::string(extseat_submit_verb(1, 2, &v, nullptr)) == "AREA_TOO_LARGE");
    static_assert(EXTSEAT_MAX_DIG_SLABS < 300, "below the engine's MAPTASKS_COUNT");
    v.kind = ESV_Sell; v.slab_x1 = 30; v.slab_y1 = 30;
    CHECK(std::string(extseat_submit_verb(1, 2, &v, nullptr)) == "AREA_TOO_LARGE");
    v.kind = ESV_BuildRoom; v.slab_x1 = 40; v.slab_y1 = 40;
    CHECK(std::string(extseat_submit_verb(1, 2, &v, nullptr)) == "AREA_TOO_LARGE");
    CHECK(extseat_idle(1));
}

TEST_CASE_METHOD(DriverFixture, "the takeover is off by default and reset clears it", "[kfx_net][extseat][takeover]") {
    CHECK_FALSE(extseat_takeover_armed());
    extseat_set_takeover(true);
    CHECK(extseat_takeover_armed());
    extseat_reset();
    CHECK_FALSE(extseat_takeover_armed());
}

TEST_CASE_METHOD(DriverFixture, "an unarmed watchdog or lost client leaves the seat a seat", "[kfx_net][extseat][takeover]") {
    extseat_pause();
    g_clock += EXTSEAT_DEFAULT_WATCHDOG_MS + 1000;
    extseat_poll();
    CHECK_FALSE(paused());
    extseat_on_client_lost();
    CHECK(get_net_user_player_number(seat_user) == 2);
}

TEST_CASE_METHOD(DriverFixture, "an armed takeover releases the seat when the watchdog fires or the client is lost", "[kfx_net][extseat][takeover]") {
    extseat_set_takeover(true);
    extseat_pause();
    g_clock += EXTSEAT_DEFAULT_WATCHDOG_MS + 1000;
    extseat_poll();
    CHECK_FALSE(paused());
    CHECK(get_net_user_player_number(seat_user) != 2);
    CHECK_FALSE(extseat_takeover_armed()); // one shot

    // And the disconnect path, with a fresh seat.
    const NetUserId again = net_add_external_seat(2);
    REQUIRE(again > 0);
    extseat_set_takeover(true);
    extseat_on_client_lost();
    CHECK(get_net_user_player_number(again) != 2);
}

namespace {
ExtSeatVerb dig_verb(int64_t x, int64_t y) {
    kfx_sim_state.map_tiles_x = 85; kfx_sim_state.map_tiles_y = 85;
    ExtSeatVerb v; std::memset(&v, 0, sizeof(v));
    v.kind = ESV_MarkDig; v.has_rect = true; v.slab_x0 = v.slab_x1 = x; v.slab_y0 = v.slab_y1 = y;
    return v;
}
void run_until_idle(int64_t limit = 2000) {
    for (int64_t i = 0; i < limit && !extseat_idle(1); i++) { g_turn++; extseat_tick(); }
}
}

TEST_CASE_METHOD(DriverFixture, "queue=true lets verbs wait their turn and run in order; without it a busy seat refuses", "[kfx_net][extseat][queue]") {
    ExtSeatSubmitInfo a{}, b{}, c{};
    ExtSeatVerb v = dig_verb(1, 1);
    REQUIRE(extseat_submit_verb_ex(1, 2, &v, false, &a) == nullptr);
    CHECK(a.queued_behind == 0);
    CHECK(std::string(extseat_submit_verb_ex(1, 2, &v, false, &b)) == "ACTION_ALREADY_QUEUED");
    REQUIRE(extseat_submit_verb_ex(1, 2, &v, true, &b) == nullptr);
    REQUIRE(extseat_submit_verb_ex(1, 2, &v, true, &c) == nullptr);
    CHECK(a.id > 0); CHECK(b.id > a.id); CHECK(c.id > b.id);
    CHECK(b.queued_behind == 1);
    CHECK(c.queued_behind == 2);
    CHECK(extseat_queued_verbs(1) == 2);
    CHECK_FALSE(extseat_idle(1));

    run_until_idle();
    CHECK(extseat_idle(1));
    CHECK(extseat_queued_verbs(1) == 0);
    g_turn++; extseat_tick(); // the last verb's result is recorded on the tick after its final step
    ExtSeatResult res[EXTSEAT_RESULT_RING];
    const int64_t n = extseat_results(1, res, EXTSEAT_RESULT_RING);
    REQUIRE(n == 3);
    CHECK(res[0].id == a.id); CHECK(res[1].id == b.id); CHECK(res[2].id == c.id);
    for (int i = 0; i < 3; i++) CHECK_FALSE(res[i].rejected);
}

TEST_CASE_METHOD(DriverFixture, "the verb queue is bounded, and a full one says QUEUE_FULL", "[kfx_net][extseat][queue]") {
    ExtSeatVerb v = dig_verb(1, 1);
    REQUIRE(extseat_submit_verb_ex(1, 2, &v, true, nullptr) == nullptr); // runs
    for (int i = 0; i < EXTSEAT_MAX_QUEUED_VERBS; i++)
        REQUIRE(extseat_submit_verb_ex(1, 2, &v, true, nullptr) == nullptr);
    CHECK(std::string(extseat_submit_verb_ex(1, 2, &v, true, nullptr)) == "QUEUE_FULL");
    CHECK(extseat_queued_verbs(1) == EXTSEAT_MAX_QUEUED_VERBS);
}

TEST_CASE_METHOD(DriverFixture, "a verb whose expiry passed is refused at submit, or dropped EXPIRED when its turn comes", "[kfx_net][extseat][queue]") {
    ExtSeatVerb stale = dig_verb(1, 1);
    stale.expires_turn = g_turn - 1;
    CHECK(std::string(extseat_submit_verb_ex(1, 2, &stale, true, nullptr)) == "STALE_VIEW");

    ExtSeatVerb first = dig_verb(1, 1);
    ExtSeatVerb waits = dig_verb(2, 2);
    waits.expires_turn = g_turn + 2; // the first verb takes more turns than that
    REQUIRE(extseat_submit_verb_ex(1, 2, &first, true, nullptr) == nullptr);
    ExtSeatSubmitInfo w{};
    REQUIRE(extseat_submit_verb_ex(1, 2, &waits, true, &w) == nullptr);
    run_until_idle();
    g_turn++; extseat_tick();
    ExtSeatResult res[EXTSEAT_RESULT_RING];
    const int64_t n = extseat_results(1, res, EXTSEAT_RESULT_RING);
    REQUIRE(n == 2);
    CHECK_FALSE(res[0].rejected);
    CHECK(res[1].id == w.id);
    CHECK(res[1].rejected);
    CHECK(std::string(res[1].error) == "EXPIRED");
}

TEST_CASE_METHOD(DriverFixture, "cancel drops the waiting verbs too and reports them CANCELLED", "[kfx_net][extseat][queue]") {
    ExtSeatVerb v = dig_verb(1, 1);
    REQUIRE(extseat_submit_verb_ex(1, 2, &v, true, nullptr) == nullptr);
    REQUIRE(extseat_submit_verb_ex(1, 2, &v, true, nullptr) == nullptr);
    REQUIRE(extseat_submit_verb_ex(1, 2, &v, true, nullptr) == nullptr);
    ExtSeatVerb c; std::memset(&c, 0, sizeof(c)); c.kind = ESV_Cancel;
    ExtSeatSubmitInfo info{};
    REQUIRE(extseat_submit_verb_ex(1, 2, &c, true, &info) == nullptr);
    CHECK(info.steps >= 3);
    CHECK(extseat_queued_verbs(1) == 0);
    ExtSeatResult res[EXTSEAT_RESULT_RING];
    const int64_t n = extseat_results(1, res, EXTSEAT_RESULT_RING);
    int cancelled = 0;
    for (int64_t i = 0; i < n; i++) if (res[i].rejected && std::string(res[i].error) == "CANCELLED") cancelled++;
    CHECK(cancelled == 3);
}

TEST_CASE_METHOD(DriverFixture, "extseat_check_verb validates like a real submit but never queues, tracks, or is blocked", "[kfx_net][extseat][dry_run]") {
    ExtSeatVerb dig = dig_verb(1, 1);
    int64_t steps = -1;
    CHECK(extseat_check_verb(1, 2, &dig, &steps) == nullptr);
    CHECK(steps > 0);
    CHECK(extseat_idle(1)); // nothing was queued

    ExtSeatVerb bad; std::memset(&bad, 0, sizeof(bad)); bad.kind = ESV_BuildRoom; bad.has_rect = true;
    bad.slab_x1 = 1; bad.slab_y1 = 1; // no name: get_rid("") is not a room
    CHECK(std::string(extseat_check_verb(1, 2, &bad, nullptr)) == "UNKNOWN_KIND");

    CHECK(std::string(extseat_check_verb(0, 2, &dig, nullptr)) == "NOT_A_VALID_SEAT"); // user 0 does not own player 2

    REQUIRE(extseat_submit_verb_ex(1, 2, &dig, false, nullptr) == nullptr); // a real gesture, now running
    CHECK_FALSE(extseat_idle(1));
    ExtSeatVerb other = dig_verb(2, 2);
    steps = -1;
    CHECK(extseat_check_verb(1, 2, &other, &steps) == nullptr); // a dry run is never blocked by a busy seat
    CHECK(steps > 0);
    CHECK_FALSE(extseat_idle(1)); // and does not disturb the one that is running
}

TEST_CASE_METHOD(DriverFixture, "set_alliance validates a target and enabled, honours the lock, and toggles once", "[kfx_net][extseat][alliance]") {
    ExtSeatVerb v; std::memset(&v, 0, sizeof(v)); v.kind = ESV_SetAlliance;
    CHECK(std::string(extseat_submit_verb(1, 2, &v, nullptr)) == "MISSING_TARGET_PLAYER");
    v.has_target_player = true; v.target_player = 2; // self
    CHECK(std::string(extseat_submit_verb(1, 2, &v, nullptr)) == "INVALID_PLAYER");
    v.target_player = PLAYERS_COUNT; // out of range
    CHECK(std::string(extseat_submit_verb(1, 2, &v, nullptr)) == "INVALID_PLAYER");
    v.target_player = 0; // the human, which the fixture makes exist
    CHECK(std::string(extseat_submit_verb(1, 2, &v, nullptr)) == "MISSING_ENABLED");
    v.has_enabled = true; v.enabled = false;
    CHECK(std::string(extseat_submit_verb(1, 2, &v, nullptr)) == "ALREADY_SET"); // withdrawing what was never declared

    set_player_ally_locked(2, 0, true);
    v.enabled = true;
    CHECK(std::string(extseat_submit_verb(1, 2, &v, nullptr)) == "ALLIANCE_LOCKED");
    set_player_ally_locked(2, 0, false);

    // A single step: the toggle packet, aimed at the target player. Whether packets.c's PckA_PlyrToggleAlly handler then
    // actually flips player->allied_players is exercised end to end by the real engine in ftest ai_seat_alliance; this
    // level only owns the verb's own validation and the step it produces.
    ExtSeatSubmitInfo info{};
    REQUIRE(extseat_submit_verb_ex(1, 2, &v, false, &info) == nullptr);
    CHECK(info.steps == 1);
    extseat_tick();
    CHECK(sim_packets[1].action == PckA_PlyrToggleAlly);
    CHECK(sim_packets[1].actn_par1 == 0);
}
