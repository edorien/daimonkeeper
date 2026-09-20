#include <catch2/catch_test_macros.hpp>
#include "editor_lua_validate.h"
#include "editor_lua_support.h"
#include "lvl_script_lib.h"
#include "lvl_script_commands.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

const std::set<std::string> &api()
{
    static const std::set<std::string> a = editor_lua_scan_api(std::string(KFX_SOURCE_DIR) + "/config/fxdata/lua");
    return a;
}

const char *lookup_args(const std::string &name)
{
    if (name == "SET_GAME_RULE")
        return "AA";
    if (name == "LEVEL_VERSION")
        return "N";
    return nullptr;
}

std::vector<ScriptIssue> check(const std::string &text, const LuaModuleText &mods = LuaModuleText())
{
    return editor_lua_validate(text, api(), lookup_args, mods);
}

std::string slurp(const std::filesystem::path &p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("lua validate: clean script has no issues", "[kfx_editor][lua]") {
    CHECK(check("function OnGameStart()\n  local x = 5\n  print(x)\n  RegisterTimerEvent(OnTick, 10, true)\nend\nfunction OnTick() end\n").empty());
}

TEST_CASE("lua validate: a missing end is a syntax error with the right line", "[kfx_editor][lua]") {
    const auto issues = check("function OnGameStart()\n  print(1)\n");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].severity == ScrIssue_Error);
    CHECK(issues[0].message.find("Lua syntax") == 0);
    CHECK(issues[0].line >= 1);
}

TEST_CASE("lua validate: unknown function is a warning, defined and library ones are not", "[kfx_editor][lua]") {
    const auto issues = check("function OnGameStart()\n  WinGaem(PLAYER0)\n  WinGame(PLAYER0)\n  local f = function(a) return a end\n  f(1)\n  string.format('x')\n  obj:method()\nend\n");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].severity == ScrIssue_Warning);
    CHECK(issues[0].line == 1);
    CHECK(issues[0].message.find("WinGaem") != std::string::npos);
}

TEST_CASE("lua validate: comments and strings are not code", "[kfx_editor][lua]") {
    CHECK(check("-- Nope(1)\n--[[ Nope(2)\n]] local s = 'Nope(3)' local t = [[ Nope(4) ]]\n").empty());
}

TEST_CASE("lua validate: embedded classic commands use the classic validator", "[kfx_editor][lua]") {
    const auto issues = check("function OnGameStart()\n  RunDKScriptCommand(\"NO_SUCH_COMMAND(1)\")\n  RunDKScriptCommand(\"LEVEL_VERSION(1)\")\nend\n");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].line == 1);
    CHECK(issues[0].message.find("In RunDKScriptCommand") == 0);
}

TEST_CASE("lua validate: names from a required module are known", "[kfx_editor][lua]") {
    const std::string src = "local m = require \"pack\"\nPackFunc()\nOther()\n";
    const auto issues = check(src, [](const std::string &n) {
        return n == "pack" ? std::string("function PackFunc() end\n") : std::string();
    });
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].message.find("Other") != std::string::npos);
}

TEST_CASE("lua validate: shipped level scripts have no errors and no warnings", "[kfx_editor][lua]") {
    namespace fs = std::filesystem;
    const fs::path root = fs::path(KFX_SOURCE_DIR) / "core_files";
    const LuaModuleText mods = [&](const std::string &n) {
        return slurp(root / "levels" / "dungeon_architect" / "cfg" / "lua" / (n + ".lua"));
    };
    int files = 0;
    std::string report;
    for (const fs::path &dir : {root / "levels", root / "multiplayer"})
        for (fs::recursive_directory_iterator it(dir), end; it != end; ++it)
        {
            if (!it->is_regular_file() || it->path().extension() != ".lua")
                continue;
            const std::string fname = it->path().filename().string();
            if (fname.compare(0, 3, "map") != 0)
                continue;
            files++;
            const ScriptCommandLookup engine = [](const std::string &name) -> const char * {
                for (int i = 0; command_desc[i].textptr != NULL; i++)
                    if (name == command_desc[i].textptr)
                        return command_desc[i].args;
                return nullptr;
            };
            for (const ScriptIssue &si : editor_lua_validate(slurp(it->path()), api(), engine, mods))
                report += fname + ":" + std::to_string(si.line + 1) + " " + si.message + "\n";
        }
    CHECK(files >= 8);
    INFO(report);
    CHECK(report.empty());
}
