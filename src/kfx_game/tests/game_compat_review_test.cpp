// kfx_game: game_compat_review.c -- when a level whose content this build
// doesn't fully support stops for the player (the in-game warning), and when
// it's only logged: nobody should be asked to click in multiplayer, a replay,
// or a game the TCP API may be driving.
#include <catch2/catch_test_macros.hpp>

#include "packet_data.h"
#include "game_compat_review.h"
#include "compat_report.h"
#include "config_keeperfx.h"
#include "kfx_sim_state.h"

#include <cstring>

namespace {
struct LocalGameWithIssue {
    int64_t saved_api_enabled = api_enabled;
    LocalGameWithIssue() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        kfx_sim_state.game_kind = GKind_LocalGame;
        api_enabled = 0;
        compat_report_clear();
        compat_report_add(CompatIssue_ScriptCommand, "SET_FUTURE_FEATURE", NULL, 3);
    }
    ~LocalGameWithIssue() {
        api_enabled = saved_api_enabled;
        compat_report_clear();
    }
};
}

TEST_CASE_METHOD(LocalGameWithIssue, "a local game with unsupported content asks the player", "[kfx_game][compat_review]") {
    CHECK(compat_review_wanted());
    compat_review_after_level_load(1);
    CHECK(compat_report_review_pending());
    compat_report_clear(); // the next level load starts clean
    CHECK_FALSE(compat_report_review_pending());
}

TEST_CASE_METHOD(LocalGameWithIssue, "nothing unsupported: no warning", "[kfx_game][compat_review]") {
    compat_report_clear();
    CHECK_FALSE(compat_review_wanted());
    compat_review_after_level_load(1);
    CHECK_FALSE(compat_report_review_pending());
}

TEST_CASE_METHOD(LocalGameWithIssue, "multiplayer, replays and API-driven games only log it", "[kfx_game][compat_review]") {
    SECTION("multiplayer") { kfx_sim_state.game_kind = GKind_MultiGame; }
    SECTION("replay") { replay.load_enable = true; }
    SECTION("TCP API enabled") { api_enabled = 1; }
    CHECK_FALSE(compat_review_wanted());
    compat_review_after_level_load(1);
    CHECK_FALSE(compat_report_review_pending());
}
