// docs/refactor/editor/phase5/08-script-command-browser.md -- Catch2
// coverage for editor_script_commands.cpp: grouping, signatures, templates
// and line insertion. Pure logic; no engine tables needed.
#include <catch2/catch_test_macros.hpp>

#include "editor_script_commands.h"

#include <string>

namespace {
const char *const kBegin = "REM --- editor-managed setup: do not hand-edit between these markers ---";
const char *const kEnd = "REM --- end editor-managed setup ---";
}

TEST_CASE("commands are grouped after the original manual's chapter 5.2", "[kfx_editor][script_commands]") {
    CHECK(editor_script_command_group("START_MONEY") == ScrGroup_Setup);
    CHECK(editor_script_command_group("ROOM_AVAILABLE") == ScrGroup_Availability);
    CHECK(editor_script_command_group("RESEARCH") == ScrGroup_Research);
    CHECK(editor_script_command_group("SET_CREATURE_HEALTH") == ScrGroup_Creatures);
    CHECK(editor_script_command_group("ADD_PARTY_TO_LEVEL") == ScrGroup_Spawn);
    CHECK(editor_script_command_group("QUICK_OBJECTIVE") == ScrGroup_Objectives);
    CHECK(editor_script_command_group("IF") == ScrGroup_Flow);
    CHECK(editor_script_command_group("SET_FLAG") == ScrGroup_Flags);
    CHECK(editor_script_command_group("USE_POWER") == ScrGroup_MapPowers);
    CHECK(editor_script_command_group("SET_GAME_RULE") == ScrGroup_Setup);
}

TEST_CASE("an unknown command lands in Other, not nowhere", "[kfx_editor][script_commands]") {
    CHECK(editor_script_command_group("SOME_FUTURE_COMMAND") == ScrGroup_Other);
    CHECK_FALSE(editor_script_command_is_classic("SOME_FUTURE_COMMAND"));
    CHECK(std::string(editor_script_command_summary("SOME_FUTURE_COMMAND")).empty());
}

TEST_CASE("classic commands carry a summary, KeeperFX ones don't", "[kfx_editor][script_commands]") {
    CHECK(editor_script_command_is_classic("WIN_GAME"));
    CHECK_FALSE(std::string(editor_script_command_summary("WIN_GAME")).empty());
    CHECK_FALSE(editor_script_command_is_classic("SET_GAME_RULE"));
    CHECK(std::string(editor_script_command_summary("SET_GAME_RULE")).empty());
}

TEST_CASE("every group has a title", "[kfx_editor][script_commands]") {
    for (int64_t g = 0; g < ScrGroup_Count; g++)
        CHECK(std::string(editor_script_group_title(g)).size() > 0);
}

TEST_CASE("signature reads the engine's argument letters", "[kfx_editor][script_commands]") {
    CHECK(editor_script_command_signature("ENDIF", "        ") == "ENDIF");
    CHECK(editor_script_command_signature("START_MONEY", "PN      ") == "START_MONEY(player, number)");
    CHECK(editor_script_command_signature("QUICK_OBJECTIVE", "NAla    ") == "QUICK_OBJECTIVE(number, text, [location], [text])");
    CHECK(editor_script_command_signature("IF", "PAOAa   ") == "IF(player, text, comparison, text, [text])");
    // '!' (extended values) is a modifier, not an argument; '+' means repeatable.
    CHECK(editor_script_command_signature("SET_CREATURE_MAX_LEVEL", "PC!N") == "SET_CREATURE_MAX_LEVEL(player, creature, number)");
    CHECK(editor_script_command_signature("SET_SACRIFICE_RECIPE", "AAA+") == "SET_SACRIFICE_RECIPE(text, text, text, ...)");
}

TEST_CASE("template uses required arguments only", "[kfx_editor][script_commands]") {
    CHECK(editor_script_command_template("WIN_GAME", "        ") == "WIN_GAME");
    CHECK(editor_script_command_template("START_MONEY", "PN      ") == "START_MONEY(PLAYER0,0)");
    CHECK(editor_script_command_template("ADD_CREATURE_TO_LEVEL", "PCANNNa ") == "ADD_CREATURE_TO_LEVEL(PLAYER0,CREATURE,NAME,0,0,0)");
    CHECK(editor_script_command_template("QUICK_OBJECTIVE", "NAla    ") == "QUICK_OBJECTIVE(0,NAME)");
    CHECK(editor_script_command_template("IF", "PAOAa   ") == "IF(PLAYER0,NAME,==,NAME)");
}

TEST_CASE("insert fills a blank cursor line in place", "[kfx_editor][script_commands]") {
    auto r = editor_script_insert_command("a\n\nb\n", 1, "WIN_GAME");
    CHECK(r.text == "a\nWIN_GAME\nb\n");
    CHECK(r.line == 1);
}

TEST_CASE("insert goes below a non-blank cursor line", "[kfx_editor][script_commands]") {
    auto r = editor_script_insert_command("a\nb\n", 0, "WIN_GAME");
    CHECK(r.text == "a\nWIN_GAME\nb\n");
    CHECK(r.line == 1);
    auto end = editor_script_insert_command("a\nb", 1, "WIN_GAME");
    CHECK(end.text == "a\nb\nWIN_GAME");
    CHECK(end.line == 2);
}

TEST_CASE("insert follows IF/ENDIF indentation", "[kfx_editor][script_commands]") {
    // Under an IF: one level deeper.
    CHECK(editor_script_insert_command("IF(PLAYER0,MONEY>1)\nENDIF\n", 0, "WIN_GAME").text ==
        "IF(PLAYER0,MONEY>1)\n\tWIN_GAME\nENDIF\n");
    // Inside an IF body: same level.
    CHECK(editor_script_insert_command("IF(PLAYER0,MONEY>1)\n\tWIN_GAME\nENDIF\n", 1, "LOSE_GAME").text ==
        "IF(PLAYER0,MONEY>1)\n\tWIN_GAME\n\tLOSE_GAME\nENDIF\n");
    // ENDIF after the body: back out one level.
    CHECK(editor_script_insert_command("IF(PLAYER0,MONEY>1)\n\tWIN_GAME\n", 1, "ENDIF").text ==
        "IF(PLAYER0,MONEY>1)\n\tWIN_GAME\nENDIF\n");
    // ENDIF directly under its IF stays at the IF's level.
    CHECK(editor_script_insert_command("IF(PLAYER0,MONEY>1)\n", 0, "ENDIF").text == "IF(PLAYER0,MONEY>1)\nENDIF\n");
    // IF_ACTION_POINT is an IF too.
    CHECK(editor_script_insert_command("IF_ACTION_POINT(1,PLAYER0)\n", 0, "WIN_GAME").text ==
        "IF_ACTION_POINT(1,PLAYER0)\n\tWIN_GAME\n");
}

TEST_CASE("insert keeps CRLF scripts consistent", "[kfx_editor][script_commands]") {
    auto r = editor_script_insert_command("a\r\nb\r\n", 0, "WIN_GAME");
    CHECK(r.text == "a\r\nWIN_GAME\r\nb\r\n");
}

TEST_CASE("insert never lands inside the managed setup region", "[kfx_editor][script_commands]") {
    const std::string script = std::string("REM top\n") + kBegin + "\nSET_GENERATE_SPEED(300)\n" + kEnd + "\nIF(x)\n";
    auto r = editor_script_insert_command(script, 2, "WIN_GAME");
    CHECK(r.text == std::string("REM top\n") + kBegin + "\nSET_GENERATE_SPEED(300)\n" + kEnd + "\nWIN_GAME\nIF(x)\n");
    CHECK(r.line == 4);
}

TEST_CASE("insert into an empty script", "[kfx_editor][script_commands]") {
    auto r = editor_script_insert_command("", 0, "WIN_GAME");
    CHECK(r.text == "WIN_GAME");
    CHECK(r.line == 0);
}

TEST_CASE("flow commands are the ones the editor colours as keywords", "[kfx_editor][script_commands]") {
    for (const char *n : {"IF", "IF_ACTION_POINT", "IF_AVAILABLE", "IF_CONTROLS", "ENDIF", "WIN_GAME", "LOSE_GAME", "REM",
                          "NEXT_COMMAND_REUSABLE", "NEXT_COMMAND_REUSEABLE"})
        CHECK(editor_script_command_is_flow(n));
    // Not flow control, even though a name starts with IF-ish letters or is an action.
    for (const char *n : {"IFX", "SET_FLAG", "START_MONEY", "TRIGGER_ACTION_POINT", "QUICK_OBJECTIVE"})
        CHECK_FALSE(editor_script_command_is_flow(n));
}
