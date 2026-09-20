// Catch2 coverage for editor_script_validate.cpp (Script Editor > Validate).
#include <catch2/catch_test_macros.hpp>

#include "editor_script_validate.h"

namespace {
const char *lookup(const std::string &name)
{
    if (name == "IF") return "PAOAa   ";
    if (name == "IF_ACTION_POINT") return "NP      ";
    if (name == "ENDIF") return "        ";
    if (name == "START_MONEY") return "PN      ";
    if (name == "QUICK_OBJECTIVE") return "NAll    ";
    if (name == "ADD_TO_LIST") return "A+      ";
    if (name == "WIN_GAME") return "        ";
    return nullptr;
}
std::vector<ScriptIssue> run(const std::string &t) { return editor_script_validate(t, lookup); }
}

TEST_CASE("a clean script has no issues", "[kfx_editor][script_validate]") {
    CHECK(run("REM hello\nSTART_MONEY(PLAYER0,1000)\nIF(PLAYER0,FLAG0,==,1)\n  WIN_GAME\nENDIF\n").empty());
    CHECK(run("").empty());
    CHECK(run("start_money(player0, 5)\r\nrem lower case\r\n").empty());
}

TEST_CASE("unknown commands and bad argument counts are reported", "[kfx_editor][script_validate]") {
    auto issues = run("FROB(1)\nSTART_MONEY(PLAYER0)\nSTART_MONEY(PLAYER0,1,2)\nWIN_GAME()\n");
    REQUIRE(issues.size() == 3);
    CHECK(issues[0].line == 0);
    CHECK(issues[0].severity == ScrIssue_Error);
    CHECK(issues[1].line == 1);
    CHECK(issues[1].severity == ScrIssue_Warning); // too few: engine defaults, so only a warning
    CHECK(issues[2].line == 2);
    CHECK(issues[2].severity == ScrIssue_Warning); // extra argument: engine ignores, still suspicious
}

TEST_CASE("optional and repeatable arguments and quoted commas", "[kfx_editor][script_validate]") {
    CHECK(run("QUICK_OBJECTIVE(1,\"Build, then dig\")\n").empty());
    CHECK(run("QUICK_OBJECTIVE(1,\"x\",PLAYER0)\n").empty());
    CHECK(run("ADD_TO_LIST(A,B,C,D,E)\n").empty());
    CHECK(run("QUICK_OBJECTIVE(1)\n").size() == 1);
}

TEST_CASE("IF blocks must balance", "[kfx_editor][script_validate]") {
    auto unclosed = run("IF(PLAYER0,FLAG0,==,1)\nIF_ACTION_POINT(1,PLAYER0)\nENDIF\n");
    REQUIRE(unclosed.size() == 1);
    CHECK(unclosed[0].line == 0);
    auto stray = run("ENDIF\n");
    REQUIRE(stray.size() == 1);
    CHECK(stray[0].line == 0);
}

TEST_CASE("missing parenthesis is an error", "[kfx_editor][script_validate]") {
    auto issues = run("START_MONEY(PLAYER0,5\n");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].severity == ScrIssue_Error);
}

TEST_CASE("a byte order mark and lower-case REM are accepted", "[kfx_editor][script_validate]") {
    CHECK(run("\xEF\xBB\xBF" "REM x\nrem\ty\n").empty());
}
