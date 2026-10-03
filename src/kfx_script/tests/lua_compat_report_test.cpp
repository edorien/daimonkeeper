// kfx_script: lua_base.c's lua_report_missing_function() -- a Lua error that
// is a call to a function this build doesn't have (a newer KeeperFX API
// function, most likely) is recorded in the compat report, with where.
#include <catch2/catch_test_macros.hpp>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "lua_base.h"
#include "compat_report.h"

#include <string>

namespace {
struct ResetReport {
    ResetReport() { compat_report_clear(); }
    ~ResetReport() { compat_report_clear(); }
};
}

TEST_CASE_METHOD(ResetReport, "LuaJIT's wording of a call to a missing global is recorded", "[kfx_script][compat_report]") {
    CHECK(lua_report_missing_function("/game/levels/standard/map00301.lua:42: attempt to call global 'SetFutureThing' (a nil value)"));
    REQUIRE(compat_report_count() == 1);
    const struct CompatIssue *issue = compat_report_get(0);
    CHECK(issue->kind == CompatIssue_LuaFunction);
    CHECK(std::string(issue->what) == "SetFutureThing");
    CHECK(std::string(issue->where) == "map00301.lua");
    CHECK(issue->line == 42);
}

TEST_CASE_METHOD(ResetReport, "Lua 5.4's wording, and methods and fields, are recorded too", "[kfx_script][compat_report]") {
    CHECK(lua_report_missing_function("map.lua:3: attempt to call a nil value (global 'A')"));
    CHECK(lua_report_missing_function("map.lua:4: attempt to call method 'B' (a nil value)"));
    CHECK(lua_report_missing_function("map.lua:5: attempt to call field 'C' (a nil value)"));
    REQUIRE(compat_report_count() == 3);
    CHECK(std::string(compat_report_get(1)->what) == "B");
    CHECK(compat_report_get(2)->line == 5);
}

TEST_CASE_METHOD(ResetReport, "other Lua errors aren't compatibility issues", "[kfx_script][compat_report]") {
    CHECK_FALSE(lua_report_missing_function("map.lua:9: attempt to index a nil value (global 'Game')"));
    CHECK_FALSE(lua_report_missing_function("map.lua:9: attempt to perform arithmetic on a nil value"));
    CHECK_FALSE(lua_report_missing_function("map.lua:9: attempt to call a number value (global 'X')"));
    CHECK_FALSE(lua_report_missing_function(nullptr));
    CHECK(compat_report_count() == 0);
}


TEST_CASE_METHOD(ResetReport, "a real call to a missing function, through CheckLua, is recorded", "[kfx_script][compat_report]") {
    lua_State *L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    const char code[] = "local x = 1\nSetFutureThing(x)\n";
    REQUIRE(luaL_loadbuffer(L, code, sizeof(code) - 1, "map00001.lua") == 0);
    CHECK_FALSE(CheckLua(L, lua_pcall(L, 0, 0, 0), "test"));
    lua_close(L);
    REQUIRE(compat_report_count() == 1);
    CHECK(std::string(compat_report_get(0)->what) == "SetFutureThing");
    CHECK(compat_report_get(0)->line == 2);
}
