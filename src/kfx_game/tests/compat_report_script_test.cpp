// kfx_game: lvl_script.c's hooks into kfx_config's compat report -- a level
// script written for a newer KeeperFX (a command, or a creature/room/slab
// name, this build doesn't know) is recorded, not just logged and skipped.
// Scripts are fed through level_script_override (same as
// script_message_value_layout_test.cpp).
#include <catch2/catch_test_macros.hpp>

#include "lvl_script.h"
#include "level_script_override.h"
#include "compat_report.h"
#include "kfx_game_state.h"

#include <cstring>
#include <string>

namespace {
const LevelNumber kLevel = 9878;

struct ResetScript {
    ResetScript() {
        std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
        level_script_override_clear();
        compat_report_clear();
    }
    ~ResetScript() { level_script_override_clear(); }
};

const struct CompatIssue *find_issue(enum CompatIssueKind kind, const char *what)
{
    for (int64_t i = 0; i < compat_report_count(); i++) {
        const struct CompatIssue *issue = compat_report_get(i);
        if ((issue->kind == kind) && (std::string(issue->what) == what))
            return issue;
    }
    return nullptr;
}
}

TEST_CASE_METHOD(ResetScript, "an unknown script command is reported with its line", "[kfx_game][compat_report]") {
    level_script_override_set(kLevel, "REM nothing\n",
        "LEVEL_VERSION(1)\nREM a comment\nSET_FUTURE_FEATURE(1,2)\n");
    REQUIRE(load_script(kLevel));
    const struct CompatIssue *issue = find_issue(CompatIssue_ScriptCommand, "SET_FUTURE_FEATURE");
    REQUIRE(issue != nullptr);
    CHECK(issue->line == 3);
    CHECK(compat_report_count() == 1); // LEVEL_VERSION and REM are known
}

TEST_CASE_METHOD(ResetScript, "an unknown creature name in a script argument is reported", "[kfx_game][compat_report]") {
    level_script_override_set(kLevel, "REM nothing\n",
        "LEVEL_VERSION(1)\nADD_CREATURE_TO_POOL(FUTURE_BEAST,1)\n");
    REQUIRE(load_script(kLevel));
    CHECK(find_issue(CompatIssue_ScriptName, "FUTURE_BEAST") != nullptr);
    CHECK(find_issue(CompatIssue_ScriptCommand, "ADD_CREATURE_TO_POOL") == nullptr);
}
