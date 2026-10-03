// Pattern B for kfx_platform, per docs/refactor/testing/comprehensive/
// stage-08-comprehensive-library-passes.md §1's checklist ("at least one
// test per library exercises pattern B where the library owns or calls
// through one"): kfx_platform is the library that *hosts* the
// get_gameturn()'s read-only source pointer (refactor pass 2, S10; it was a
// GetGameTurnFunc provider) and the EmulateIntegerOverflowFunc indirection,
// but stage 1's original pilot (bflib_math.c) never exercised either one.
// Installs a fake for each and asserts the value actually comes through -- not just that the default no-op
// doesn't crash (already implicitly covered by every other *_utest
// linking kfx_platform without wiring either provider).
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "globals.h"
#include "bflib_basics.h"

namespace {
GameTurn g_fake_turn = 0;

struct ResetGameTurnProvider {
    ResetGameTurnProvider() { g_fake_turn = 0; }
    ~ResetGameTurnProvider() { set_gameturn_source(nullptr); } // restores the unwired 0
};

TbBool g_fake_overflow_result = false;
TbBool fake_emulate_overflow(int64_t) { return g_fake_overflow_result; }

struct ResetOverflowProvider {
    ~ResetOverflowProvider() { set_emulate_integer_overflow_provider(nullptr); } // restores the default
};
}

TEST_CASE_METHOD(ResetGameTurnProvider, "get_gameturn reads the installed source, live", "[kfx_platform][bflib_basics]") {
    g_fake_turn = 777;
    set_gameturn_source(&g_fake_turn);
    CHECK(get_gameturn() == 777);
    g_fake_turn = 778; // a later write is seen without re-installing
    CHECK(get_gameturn() == 778);
}

TEST_CASE_METHOD(ResetGameTurnProvider, "get_gameturn reads 0 once the source is cleared", "[kfx_platform][bflib_basics]") {
    g_fake_turn = 42;
    set_gameturn_source(&g_fake_turn);
    REQUIRE(get_gameturn() == 42);

    set_gameturn_source(nullptr);
    CHECK(get_gameturn() == 0); // the unwired source's value
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

// docs/refactor-pass2/stage-02-logging-option.md: the LOG_LEVEL option's runtime level.
TEST_CASE("set_log_level maps each level to its *DBG threshold", "[kfx_platform][bflib_basics][log_level]") {
    const int64_t saved = get_log_level();
    set_log_level(LogLvl_Off);
    CHECK(get_log_level() == LogLvl_Off);
    CHECK(kfx_debug_threshold == 0);
    set_log_level(LogLvl_Normal);
    CHECK(kfx_debug_threshold == 0);
    set_log_level(LogLvl_Debug);
    CHECK(kfx_debug_threshold == 10);
    set_log_level(LogLvl_DebugMax);
    CHECK(kfx_debug_threshold == 20);
    set_log_level(99); // out of range falls back to Normal
    CHECK(get_log_level() == LogLvl_Normal);
    CHECK(kfx_debug_threshold == 0);
    set_log_level(saved);
}

TEST_CASE("KFX_DEBUG_ON needs both the compile-time ceiling and the runtime threshold", "[kfx_platform][bflib_basics][log_level]") {
    const int64_t saved = get_log_level();
    set_log_level(LogLvl_DebugMax);
    CHECK(KFX_DEBUG_ON(0) == (0 < KFX_DEBUG_CEILING));
    CHECK(KFX_DEBUG_ON(19) == (19 < KFX_DEBUG_CEILING));
    set_log_level(LogLvl_Debug);
    CHECK_FALSE(KFX_DEBUG_ON(10)); // Debug stops below level 10
    set_log_level(LogLvl_Normal);
    CHECK_FALSE(KFX_DEBUG_ON(0));
    set_log_level(saved);
}

TEST_CASE("the Off level writes nothing; startup lines are buffered until the level is known", "[kfx_platform][bflib_basics][log_level]") {
    const int64_t saved = get_log_level();
    // LbErrorLogSetup's directory is relative to the current directory and
    // must end with a separator (LbFileMakeFullPath drops its last character).
    const char *dir = "kfx_log_level_test/";
    const std::filesystem::path full_dir = std::filesystem::current_path() / "kfx_log_level_test";
    std::filesystem::remove_all(full_dir);
    std::filesystem::create_directories(full_dir);
    const std::string path = (full_dir / "test.log").string();
    REQUIRE(LbErrorLogSetup(dir, "test.log", 1) == 1);

    // Buffered, then dropped (the level turned out to be Off).
    LbLogStartStartupBuffering();
    LbSyncLog("dropped startup line\n");
    LbLogEndStartupBuffering(false);
    CHECK_FALSE(std::filesystem::exists(path));

    set_log_level(LogLvl_Off);
    LbSyncLog("written at Off\n");
    LbErrorLog("error written at Off\n");
    CHECK_FALSE(std::filesystem::exists(path));

    set_log_level(LogLvl_Normal);
    // Buffered, then written out once the level is known.
    LbLogStartStartupBuffering();
    LbSyncLog("kept startup line\n");
    CHECK_FALSE(std::filesystem::exists(path));
    LbLogEndStartupBuffering(true);
    LbSyncLog("written at Normal\n");
    REQUIRE(std::filesystem::exists(path));
    std::ifstream in(path);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find("written at Normal") != std::string::npos);
    CHECK(text.find("written at Off") == std::string::npos);
    CHECK(text.find("dropped startup line") == std::string::npos);
    const size_t kept = text.find("kept startup line");
    REQUIRE(kept != std::string::npos);
    CHECK(text.find("kept startup line", kept + 1) == std::string::npos); // exactly once
    CHECK(kept < text.find("written at Normal")); // in order

    LbErrorLogClose();
    set_log_level(saved);
    std::filesystem::remove_all(full_dir);
}

TEST_CASE("a pinned session level ignores keeperfx.cfg but not an explicit change", "[kfx_platform][bflib_basics][log_level]") {
    const int64_t saved = get_log_level();
    set_log_level_from_config(LogLvl_Debug); // not pinned: the config applies
    CHECK(get_log_level() == LogLvl_Debug);
    set_log_level_pinned(LogLvl_Normal);
    CHECK(log_level_is_pinned());
    set_log_level_from_config(LogLvl_Off);   // pinned: the config is ignored
    CHECK(get_log_level() == LogLvl_Normal);
    set_log_level(LogLvl_DebugMax);          // an options-screen change still applies
    CHECK(get_log_level() == LogLvl_DebugMax);
    set_log_level(saved);
}
