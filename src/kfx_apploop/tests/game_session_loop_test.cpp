// First kfx_apploop coverage, per docs/refactor/testing/comprehensive/
// stage-07-kfx-apploop-game-process.md: display_should_be_updated_this_turn()
// was picked as the pilot for being a state read with branching, not
// update()'s dispatcher -- but reading its body (not just its name) found
// it's richer than that: the "not fast-forwarding" branch unconditionally
// calls find_frame_rate(), which reads kfx_platform's LbTimerClock. That
// turned out to already be a registered function-pointer seam
// (bflib_datetm.h: `extern TbClockMSec (*LbTimerClock)(void);`, default-
// NULL until LbTimerInit() runs) rather than a raw platform call, so a
// fake clock is enough to make this safely callable without crashing --
// exactly the kind of thing worth confirming by reading the real
// declaration rather than assuming, per this whole stage's own "verify,
// don't guess" note in stage-04b/04c.
//
// Also exercises pattern B for the first time in this library: kfx_platform's
// get_gameturn() reads through a source pointer (globals.h) that reads 0
// until wired -- this fixture points it at its own fake turn so the gameturn-modulo branches below are actually
// reachable and controllable, not stuck at the default's degenerate "0 %
// anything == 0" case.
#include <catch2/catch_test_macros.hpp>

#include "game_session_loop.h"
#include "kfx_sim_state.h"
#include "kfx_net_state.h"
#include "kfx_frontend_state.h"
#include "globals.h"
#include "bflib_datetm.h"

#include <cstring>

namespace {
GameTurn g_fake_gameturn = 0;

// Settable, defaulting to 0 so the pre-existing display_should_be_updated_
// this_turn tests below (which never touch it) keep seeing the same
// always-0 clock they were written against.
TbClockMSec g_fake_clock = 0;
TbClockMSec fake_clock_provider() { return g_fake_clock; }

struct AppLoopFixture {
    AppLoopFixture() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_net_state, 0, sizeof(kfx_net_state));
        // Avoid a null-pointer call inside find_frame_rate()/
        // packet_load_find_frame_rate() -- LbTimerClock defaults to NULL
        // until LbTimerInit() (never called in this test binary) sets it.
        LbTimerClock = fake_clock_provider;
        g_fake_gameturn = 0;
        g_fake_clock = 0;
        set_gameturn_source(&g_fake_gameturn);
    }
    ~AppLoopFixture() {
        set_gameturn_source(nullptr); // restores kfx_platform's unwired 0
    }
};
}

TEST_CASE_METHOD(AppLoopFixture, "display_should_be_updated_this_turn returns true when paused", "[kfx_apploop][game_session_loop]") {
    kfx_sim_state.operation_flags |= GOF_Paused;
    CHECK(display_should_be_updated_this_turn());
}

TEST_CASE_METHOD(AppLoopFixture, "display_should_be_updated_this_turn returns true when frame_skip is disabled", "[kfx_apploop][game_session_loop]") {
    replay.turns_fastforward = 0;
    replay.loading_in_progress = 0;
    kfx_net_state.frame_skip = 0;
    CHECK(display_should_be_updated_this_turn());
}

TEST_CASE_METHOD(AppLoopFixture, "display_should_be_updated_this_turn honors frame_skip against the current turn", "[kfx_apploop][game_session_loop]") {
    replay.turns_fastforward = 0;
    replay.loading_in_progress = 0;
    kfx_net_state.frame_skip = 5;

    g_fake_gameturn = 10; // 10 % 5 == 0
    CHECK(display_should_be_updated_this_turn());

    g_fake_gameturn = 7; // 7 % 5 != 0
    CHECK_FALSE(display_should_be_updated_this_turn());
}

TEST_CASE_METHOD(AppLoopFixture, "display_should_be_updated_this_turn while fast-forwarding checks the turn's low bits", "[kfx_apploop][game_session_loop]") {
    replay.turns_fastforward = 1; // nonzero -- takes the fast-forward branch
    replay.loading_in_progress = 0;

    g_fake_gameturn = 0x40; // & 0x3F == 0
    CHECK(display_should_be_updated_this_turn());

    g_fake_gameturn = 0x01; // & 0x3F != 0, and not packet-loading
    CHECK_FALSE(display_should_be_updated_this_turn());
}

// find_frame_rate()/packet_load_find_frame_rate() each keep their own
// function-local static accumulator/timestamp pair with no reset
// accessor. Each function gets a single TEST_CASE below covering both
// the "still accumulating" and "threshold crossed, time_delta computed"
// behavior in one linear sequence -- two separate TEST_CASEs sharing
// these statics was tried first and found to be order-dependent within
// a single (non-ctest, raw-binary --order rand) process: whichever test
// ran second could find the static already sitting at the same
// "primed" value the first test left it at, silently skipping its own
// prime and inheriting the first test's leftover accumulator instead of
// starting clean. A single TEST_CASE sidesteps that: the one prime call
// at the top is guaranteed to fire because nothing else in this binary
// ever pushes these particular statics forward (the
// display_should_be_updated_this_turn tests above only ever call these
// two functions with clock=0, so prev_time2/start_time can only be
// nonzero here if this same test already set it).
TEST_CASE_METHOD(AppLoopFixture, "find_frame_rate accumulates until 1000ms elapse, then computes a frames-per-second-derived time_delta", "[kfx_apploop][game_session_loop]") {
    g_fake_clock = 2000; // definitely >=1000 past this static's initial 0
    find_frame_rate(); // prime: forces the fire branch, resetting prev_time2=2000, cntr_time2=0

    kfx_frontend_state.time_delta = 0; // observe from a known baseline
    g_fake_clock = 2000; // no elapsed time since the prime -- accumulate only
    find_frame_rate();
    find_frame_rate();
    find_frame_rate(); // cntr_time2: 0 -> 1 -> 2 -> 3, still under the 1000ms threshold
    CHECK(kfx_frontend_state.time_delta == 0);

    g_fake_clock = 3000; // 2000 -> 3000 is exactly the 1000ms threshold
    find_frame_rate(); // cntr_time2 becomes 4 before the check fires
    // time_fdelta = 1000.0 * 4 / (3000 - 2000) = 4.0; time_delta = (unsigned long)(4.0 * 256.0)
    CHECK(kfx_frontend_state.time_delta == 1024);
}

TEST_CASE_METHOD(AppLoopFixture, "packet_load_find_frame_rate accumulates incr until 5000ms elapse, then computes a time_delta", "[kfx_apploop][game_session_loop]") {
    g_fake_clock = 6000; // definitely >=5000 past this static's initial 0
    packet_load_find_frame_rate(0); // prime: forces the fire branch, resetting start_time=6000, extra_frames=0

    kfx_frontend_state.time_delta = 0;
    g_fake_clock = 6000; // no elapsed time since the prime -- accumulate only
    packet_load_find_frame_rate(10);
    packet_load_find_frame_rate(20); // extra_frames: 0 -> 10 -> 30, still under the 5000ms threshold
    CHECK(kfx_frontend_state.time_delta == 0);

    g_fake_clock = 11000; // 6000 -> 11000 is exactly the 5000ms threshold
    packet_load_find_frame_rate(70);
    // time_fdelta = 1000.0 * (30 + 70) / (11000 - 6000) = 20.0; time_delta = (unsigned long)(20.0 * 256.0)
    CHECK(kfx_frontend_state.time_delta == 5120);
}

TEST_CASE_METHOD(AppLoopFixture, "keeper_screen_swap presents the frame and returns true", "[kfx_apploop][game_session_loop]") {
    // RendererPresentGameFrame() (kfx_platform's RendererManager.cpp,
    // wrapping the underlying present) guards on its own static
    // s_active_renderer, which defaults to nullptr and is never set in
    // this test binary (no RendererSetActive() call) -- so this is a safe
    // no-op here, not a real rendering-surface call.
    CHECK(keeper_screen_swap());
}

TEST_CASE_METHOD(AppLoopFixture, "keeper_wait_for_next_turn returns false without sleeping when neither wait-sleep mode nor frame_skip pacing applies", "[kfx_apploop][game_session_loop]") {
    // frame_skip < 0 and GNFldD_WaitSleepMode unset leaves this
    // function's own tick_ns_one_frame at its initial -1, so it returns
    // false before reaching get_time_tick_ns()/LbSleepUntilExt() -- the
    // one branch reachable without a real wall-clock read, which this
    // library doesn't have a fake-provider seam for (get_time_tick_ns()
    // reads std::chrono::high_resolution_clock directly, unlike
    // LbTimerClock's registered function pointer).
    kfx_sim_state.view_mode_flags = 0; // GNFldD_WaitSleepMode not set
    kfx_net_state.frame_skip = -1;
    CHECK_FALSE(keeper_wait_for_next_turn());
}
