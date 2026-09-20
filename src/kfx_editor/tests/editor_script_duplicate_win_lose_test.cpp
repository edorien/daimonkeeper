// The editor wraps kfx_config's script_setup_find_duplicate_win_lose() as
// ScriptIssue warnings for the script editor's Validate button.
#include <catch2/catch_test_macros.hpp>

#include "editor_script_validate.h"
#include "script_setup.h"

TEST_CASE("hand-written WIN_GAME beside a managed win rule is flagged", "[kfx_editor][script_setup]") {
    ManagedSetupValues v;
    v.generate_speed = 0;
    WinLoseRule win;
    win.clauses.push_back(WinLoseClause());
    v.rules.push_back(win);
    const std::string body = script_setup_generate(v, 1);
    const std::string script = script_setup_replace_region(
        "REM top\nIF(PLAYER1,DUNGEON_DESTROYED >= 1)\n\tWIN_GAME\nENDIF\nIF(PLAYER0,DUNGEON_DESTROYED >= 1)\n\tLOSE_GAME\nENDIF\n", body);
    const auto issues = editor_script_check_duplicate_win_lose(script);
    REQUIRE(issues.size() == 1); // only WIN_GAME: the managed block has no lose rule
    CHECK(issues[0].message.find("WIN_GAME") == 0);
    CHECK(script.find("WIN_GAME", script.find("REM top")) != std::string::npos);
    // The block's own WIN_GAME is not counted, nor is a script with no block.
    CHECK(editor_script_check_duplicate_win_lose(script_setup_replace_region("", body)).empty());
    CHECK(editor_script_check_duplicate_win_lose("IF(PLAYER1,X>1)\n WIN_GAME\nENDIF\n").empty());
}

