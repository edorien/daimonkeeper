// Pattern B for kfx_platform, per docs/refactor/testing/comprehensive/
// stage-08-comprehensive-library-passes.md §1's checklist ("at least one
// test per library exercises pattern B where the library owns or calls
// through one"): kfx_platform is the library that *hosts* the
// GetGameTurnFunc/EmulateIntegerOverflowFunc callback indirections the
// check-layering-symbol-level-blind-spot.md fix introduced, but stage 1's
// original pilot (bflib_math.c) never exercised either one. Registers a
// fake provider for each and asserts the wrapper actually calls through
// to it and uses the returned value -- not just that the default no-op
// doesn't crash (already implicitly covered by every other *_utest
// linking kfx_platform without wiring either provider).
#include <catch2/catch_test_macros.hpp>

#include "globals.h"
#include "bflib_basics.h"

namespace {
GameTurn g_fake_turn = 0;
GameTurn fake_gameturn() { return g_fake_turn; }

struct ResetGameTurnProvider {
    ResetGameTurnProvider() { g_fake_turn = 0; }
    ~ResetGameTurnProvider() { set_get_gameturn_provider(nullptr); } // restores the default stub
};

TbBool g_fake_overflow_result = false;
TbBool fake_emulate_overflow(int64_t) { return g_fake_overflow_result; }

struct ResetOverflowProvider {
    ~ResetOverflowProvider() { set_emulate_integer_overflow_provider(nullptr); } // restores the default
};
}

TEST_CASE_METHOD(ResetGameTurnProvider, "get_gameturn calls through to a registered provider", "[kfx_platform][bflib_basics]") {
    g_fake_turn = 777;
    set_get_gameturn_provider(fake_gameturn);
    CHECK(get_gameturn() == 777);
}

TEST_CASE_METHOD(ResetGameTurnProvider, "get_gameturn falls back to the default stub once the provider is cleared", "[kfx_platform][bflib_basics]") {
    g_fake_turn = 42;
    set_get_gameturn_provider(fake_gameturn);
    REQUIRE(get_gameturn() == 42);

    set_get_gameturn_provider(nullptr);
    CHECK(get_gameturn() == 0); // default_get_gameturn's documented stub value
}

TEST_CASE_METHOD(ResetOverflowProvider, "saturate_set_unsigned clamps when the provider reports no overflow emulation", "[kfx_platform][bflib_basics]") {
    g_fake_overflow_result = false;
    set_emulate_integer_overflow_provider(fake_emulate_overflow);
    CHECK(saturate_set_unsigned(300, 8) == 255); // clamped to the 8-bit max
}

TEST_CASE_METHOD(ResetOverflowProvider, "saturate_set_unsigned wraps when the provider reports overflow emulation", "[kfx_platform][bflib_basics]") {
    g_fake_overflow_result = true;
    set_emulate_integer_overflow_provider(fake_emulate_overflow);
    CHECK(saturate_set_unsigned(300, 8) == 44); // 300 & 0xFF, not clamped
}
