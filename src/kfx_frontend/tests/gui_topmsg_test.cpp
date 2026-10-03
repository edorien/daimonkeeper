// First kfx_frontend coverage, per docs/refactor/testing/
// stage-02-testability-and-fakes.md's rollout order (lowest priority of
// the "normal" libraries, most pattern-C-heavy by file count -- most of
// gui_topmsg.c's neighbors in this library are GUI-state/rendering-
// coupled the way stage-02 §4 predicted). erstat_inc()'s error-statistics
// counter is the exception: a small, self-contained module-static array
// with its own reset function (erstats_clear()) -- pattern A, same idiom
// as ariadne_points.c/light_data.c in earlier stages.
//
// erstat_check()/erstat[]/last_checked_stat_num/render_onscreen_msg_time
// had no header declaration anywhere (only used within gui_topmsg.c
// itself) -- added to gui_topmsg.h, the usual "add the missing
// declaration" fix. erstat_check() needs get_gameturn() to land on a
// multiple of 8 (its own "don't check more than every 7 turns" gate),
// so this points get_gameturn()'s source (set_gameturn_source(), globals.h)
// at a fake turn rather than depending on real wall-clock/game
// state.
#include <catch2/catch_test_macros.hpp>

#include "gui_topmsg.h"
#include "globals.h" // GameTurn/set_gameturn_source

namespace {
struct ResetErrorStats {
    ResetErrorStats() { erstats_clear(); }
};

GameTurn g_fake_turn = 0;

struct ResetErrorStatsWithGameTurn : ResetErrorStats {
    ResetErrorStatsWithGameTurn() {
        g_fake_turn = 0;
        set_gameturn_source(&g_fake_turn);
    }
    ~ResetErrorStatsWithGameTurn() { set_gameturn_source(nullptr); }
};

// erstat[] is declared `extern ...[]` (incomplete array type), so
// sizeof() isn't available at the test call site -- this must match the
// real 10-entry initializer in gui_topmsg.c.
constexpr int64_t kNumStats = 10;
}

TEST_CASE_METHOD(ResetErrorStats, "erstat_inc returns the count of new occurrences since the last flush", "[kfx_frontend][gui_topmsg]") {
    CHECK(erstat_inc(0) == 1);
    CHECK(erstat_inc(0) == 2);
    CHECK(erstat_inc(0) == 3);
}

TEST_CASE_METHOD(ResetErrorStats, "erstat_inc tracks stats independently", "[kfx_frontend][gui_topmsg]") {
    CHECK(erstat_inc(0) == 1);
    CHECK(erstat_inc(1) == 1);
    CHECK(erstat_inc(0) == 2);
}

TEST_CASE_METHOD(ResetErrorStats, "erstat_inc rejects an out-of-range stat_num without side effects", "[kfx_frontend][gui_topmsg]") {
    CHECK(erstat_inc(-1) == 1);
    CHECK(erstat_inc(99999) == 1);
    // Neither out-of-range call should have touched a real slot.
    CHECK(erstat_inc(0) == 1);
}

TEST_CASE_METHOD(ResetErrorStats, "is_onscreen_msg_visible reflects whether a message timer is still counting down", "[kfx_frontend][gui_topmsg]") {
    render_onscreen_msg_time = 0.0;
    CHECK_FALSE(is_onscreen_msg_visible());
    render_onscreen_msg_time = 1.0;
    CHECK(is_onscreen_msg_visible());
}

TEST_CASE_METHOD(ResetErrorStats, "show_onscreen_msg formats the message and starts the timer at nturns", "[kfx_frontend][gui_topmsg]") {
    render_onscreen_msg_time = 0.0;
    CHECK(show_onscreen_msg(5, "hello %" PRId64, (int64_t)(42)));
    CHECK(render_onscreen_msg_time == 5.0);
    CHECK(is_onscreen_msg_visible());
}

TEST_CASE_METHOD(ResetErrorStatsWithGameTurn, "erstat_check skips checking except on a multiple-of-8 game turn", "[kfx_frontend][gui_topmsg]") {
    g_fake_turn = 3; // 3 & 0x07 != 0
    CHECK_FALSE(erstat_check());
    CHECK(last_checked_stat_num == 0); // untouched -- the turn gate returned before anything else ran
}

TEST_CASE_METHOD(ResetErrorStatsWithGameTurn, "erstat_check is false and advances the cursor when the current stat has no new occurrences", "[kfx_frontend][gui_topmsg]") {
    g_fake_turn = 0;
    CHECK_FALSE(erstat_check());
    CHECK(last_checked_stat_num == 1);
}

TEST_CASE_METHOD(ResetErrorStatsWithGameTurn, "erstat_check is true and flushes nprv when the current stat has new occurrences", "[kfx_frontend][gui_topmsg]") {
    g_fake_turn = 0;
    erstat_inc(0); // erstat[0].n becomes 1, nprv stays 0
    CHECK(erstat_check());
    CHECK(erstat[0].nprv == erstat[0].n); // flushed
    CHECK(last_checked_stat_num == 1);
}

TEST_CASE_METHOD(ResetErrorStatsWithGameTurn, "erstat_check resets an out-of-range cursor back to 0 before checking", "[kfx_frontend][gui_topmsg]") {
    g_fake_turn = 0;
    last_checked_stat_num = kNumStats + 5; // corrupted/out-of-range
    CHECK_FALSE(erstat_check()); // erstat[0] has no diff after erstats_clear()
    CHECK(last_checked_stat_num == 1); // reset to 0, then advanced past it
}
