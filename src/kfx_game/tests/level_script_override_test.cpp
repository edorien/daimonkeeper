// docs/refactor/skirmish/ (S3): the script loader's override seam
// (level_script_override.h + lvl_script.c). Drives the real preload_script()/
// load_script() from an in-memory override -- no level files needed -- and
// checks what the loader actually recorded in kfx_game_state.script.
#include <catch2/catch_test_macros.hpp>

#include "lvl_script.h"
#include "lvl_script_lib.h"
#include "lvl_script_value.h"
#include "lvl_filesdk1.h"
#include "level_script_override.h"
#include "kfx_game_state.h"

#include <cstring>

namespace {

const LevelNumber kLevel = 9876;

struct LoaderFixture {
    int64_t saved_version;
    LoaderFixture()
    {
        std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
        level_script_override_clear();
        saved_version = kfx_game_state.level_file_version;
    }
    ~LoaderFixture()
    {
        level_script_override_clear();
        kfx_game_state.level_file_version = saved_version;
        std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
    }
};

} // namespace

TEST_CASE_METHOD(LoaderFixture, "override storage: set, match, one level only, clear", "[kfx_game][script_override]") {
    CHECK_FALSE(level_script_override_is_set());
    CHECK(level_script_override_prelude() == nullptr);
    level_script_override_set(kLevel, "REM a\n", "REM b\n");
    CHECK(level_script_override_is_set());
    CHECK(level_script_override_matches(kLevel));
    CHECK_FALSE(level_script_override_matches(kLevel + 1));
    CHECK(level_script_override_level() == kLevel);
    CHECK(std::string(level_script_override_prelude()) == "REM a\n");
    CHECK(std::string(level_script_override_masked()) == "REM b\n");
    level_script_override_set(kLevel + 5, "", ""); // replaces
    CHECK(level_script_override_matches(kLevel + 5));
    level_script_override_clear();
    CHECK_FALSE(level_script_override_is_set());
    CHECK(level_script_override_masked() == nullptr);
}

TEST_CASE_METHOD(LoaderFixture, "preload + load run the masked script and the prelude, then consume the override", "[kfx_game][script_override]") {
    level_script_override_set(kLevel,
        "IF(PLAYER0,GAME_TURN > 100)\n\tWIN_GAME\nENDIF\n",            // prelude: a win rule
        "IF(PLAYER1,GAME_TURN > 5)\n\tLOSE_GAME\nENDIF\n");            // "file": a lose rule
    REQUIRE(preload_script(kLevel));
    CHECK(level_script_override_matches(kLevel)); // preload does not consume
    CHECK(load_script(kLevel));
    CHECK_FALSE(level_script_override_is_set());  // load does (one-shot)
    CHECK(kfx_game_state.script.conditions_num == 2);
    CHECK(kfx_game_state.script.win_conditions_num == 1);  // from the prelude
    CHECK(kfx_game_state.script.lose_conditions_num == 1); // from the masked file
}

TEST_CASE_METHOD(LoaderFixture, "the prelude is v1 even when the file is v0", "[kfx_game][script_override]") {
    // TOTAL_DIGGERS exists only in the v1 variable table; the masked file declares no
    // LEVEL_VERSION (v0). If the prelude were parsed at the file's version the condition
    // would be rejected and never recorded.
    level_script_override_set(kLevel, "IF(PLAYER0,TOTAL_DIGGERS == 0)\n\tWIN_GAME\nENDIF\n", "REM v0 file\n");
    REQUIRE(preload_script(kLevel));
    CHECK(kfx_game_state.level_file_version == 0); // preload left the file's own version behind
    CHECK(load_script(kLevel));
    CHECK(kfx_game_state.script.conditions_num == 1);
    CHECK(kfx_game_state.script.win_conditions_num == 1);
}

TEST_CASE_METHOD(LoaderFixture, "control: without forcing v1 the same line is rejected at v0", "[kfx_game][script_override]") {
    // The same v1-only variable, but as an ordinary file line at v0 (no override at all is
    // involved for the parse; the override just supplies the text) -- proves the test above
    // really depends on the forced version.
    level_script_override_set(kLevel, "REM nothing\n", "IF(PLAYER0,TOTAL_DIGGERS == 0)\n\tWIN_GAME\nENDIF\n");
    REQUIRE(preload_script(kLevel));
    CHECK(load_script(kLevel));
    // The refused IF opens a condition that is never true (refactor pass 4, P4-F2), not the real one.
    REQUIRE(kfx_game_state.script.conditions_num == 1);
    CHECK(kfx_game_state.script.conditions[0].variabl_type == SVar_NEVER_TRUE);
}

TEST_CASE_METHOD(LoaderFixture, "the file's own version survives the prelude", "[kfx_game][script_override]") {
    level_script_override_set(kLevel, "REM p\n", "LEVEL_VERSION(1)\nREM f\n");
    REQUIRE(preload_script(kLevel));
    CHECK(kfx_game_state.level_file_version == 1);
    CHECK(load_script(kLevel));
    CHECK(kfx_game_state.level_file_version == 1); // not left at, or reset to, anything else

    // And the other direction: entry version 0 stays 0 across the prelude.
    level_script_override_set(kLevel, "START_MONEY(PLAYER0,1)\n", "REM f\n");
    kfx_game_state.level_file_version = 0;
    CHECK(load_script(kLevel));
    CHECK(kfx_game_state.level_file_version == 0);
}

TEST_CASE_METHOD(LoaderFixture, "the level version is game state: a save carries it to commands run in play", "[kfx_game][script_override]") {
    // Refactor pass 4, P4-F8: the version was a global that a save and a resync didn't carry, so a command
    // run after a load (here BONUS_LEVEL_TIME, whose real-time flag needs version 1) saw whatever the last
    // level read in that session had -- 0 in a new session.
    level_script_override_set(kLevel, "REM p\n", "LEVEL_VERSION(1)\nREM f\n");
    REQUIRE(preload_script(kLevel));
    REQUIRE(load_script(kLevel));
    REQUIRE(kfx_game_state.level_file_version == 1);
    struct KfxGameState saved;
    std::memcpy(&saved, &kfx_game_state, sizeof(saved));       // what the save writes
    std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));   // a new session
    std::memcpy(&kfx_game_state, &saved, sizeof(saved));       // the load
    struct ScriptValue value = {};
    value.longs[0] = 100;
    value.longs[1] = 1;
    script_process_value(Cmd_BONUS_LEVEL_TIME, 0, &value);
    CHECK(kfx_game_state.timer_real);
    CHECK(kfx_game_state.bonus_time > 0);
}

TEST_CASE_METHOD(LoaderFixture, "a stale override (another level) is discarded, not applied", "[kfx_game][script_override]") {
    level_script_override_set(kLevel, "REM p\n", "REM f\n");
    preload_script(kLevel + 1); // no such map file: returns false, but must drop the stale override
    CHECK_FALSE(level_script_override_is_set());
}

TEST_CASE_METHOD(LoaderFixture, "no override: behaviour is unchanged", "[kfx_game][script_override]") {
    CHECK_FALSE(preload_script(kLevel)); // no map file for this number, nothing installed
    CHECK_FALSE(level_script_override_is_set());
}
