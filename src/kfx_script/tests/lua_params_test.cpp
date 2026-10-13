// First kfx_script coverage, per docs/refactor/testing/comprehensive/
// stage-06-kfx-script-luajit.md's Tier 1 survey: lua_params.c's
// argument-checking helpers are pure Lua-stack operations. They need a
// live lua_State (LuaJIT's C API, already available via kfx_common_opts'
// kfx_luajit target -- no new dependency, per the fetch-not-system
// preference this stage was scoped under: LuaJIT itself is already
// fetched/built by Dependencies.cmake for the main program, reused here
// as-is), but deliberately NOT the full open_lua_script() init chain
// (Lvl_script global, reg_host_functions(), config/fxdata/lua/init.lua
// loading, ...) -- a bare luaL_newstate() plus a value pushed directly
// onto the stack is enough, the same "start with what needs the least
// setup" discipline every prior stage-04* pilot followed.
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <catch2/catch_test_macros.hpp>

#include "lua_params.h"
#include "config.h"
#include "kfx_sim_state.h"

#include <cstring>

namespace {
struct LuaState {
    lua_State *L;
    LuaState() { L = luaL_newstate(); }
    ~LuaState() { lua_close(L); }
};
}

TEST_CASE_METHOD(LuaState, "luaL_checkIntMinMax accepts a value inside the range", "[kfx_script][lua_params]") {
    lua_pushinteger(L, 5);
    CHECK(luaL_checkIntMinMax(L, 1, 0, 10) == 5);
}

TEST_CASE_METHOD(LuaState, "luaL_optCheckinteger returns 0 when the argument is absent", "[kfx_script][lua_params]") {
    // Nothing pushed -- index 1 is "none" on an empty stack.
    CHECK(luaL_optCheckinteger(L, 1) == 0);
}

TEST_CASE_METHOD(LuaState, "luaL_optCheckinteger returns the argument when present", "[kfx_script][lua_params]") {
    lua_pushinteger(L, 42);
    CHECK(luaL_optCheckinteger(L, 1) == 42);
}

TEST_CASE_METHOD(LuaState, "luaL_checkstl_x accepts an in-bounds subtile coordinate", "[kfx_script][lua_params]") {
    // Pattern A (docs/refactor/testing/stage-02-testability-and-fakes.md
    // §2): luaL_checkstl_x range-checks against kfx_sim_state.map_subtiles_x.
    std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
    kfx_sim_state.map_subtiles_x = 100;

    lua_pushinteger(L, 50);
    CHECK(luaL_checkstl_x(L, 1) == 50);
}

// Security pass S08 (docs/security-pass/README.md): the Lua API takes numbers from a level's script and used them as
// indexes into the tables of creatures, rooms, slabs, players... A number now has to be one the table has, a player
// one that exists, a position one on the map. (Found by the Lua API fuzzer.)
namespace {
/** Runs `fn` (which reads its arguments from the stack) under a protected call with everything on the stack as its
 *  arguments; true if it raised an error. The stack is empty afterwards. */
bool raises(lua_State *L, int (*fn)(lua_State *))
{
    const int nargs = lua_gettop(L);
    lua_pushcfunction(L, fn);
    lua_insert(L, 1);
    const int status = lua_pcall(L, nargs, LUA_MULTRET, 0);
    lua_settop(L, 0);
    return status != 0;
}
const struct NamedCommand kTable[] = { {"ALPHA", 1}, {"BETA", 5}, {"NONE", -1}, {NULL, 0} };
int check_named(lua_State *L) { lua_pushinteger(L, luaL_checkNamedCommand(L, 1, kTable)); return 1; }
int check_range(lua_State *L) { luaL_checkPlayerRange(L, 1); return 0; }
int check_effect(lua_State *L) { luaL_checkEffectOrEffElModel(L, 1); return 0; }
}

TEST_CASE_METHOD(LuaState, "luaL_checkNamedCommand takes a number only if the table has it", "[kfx_script][lua_params][security]") {
    lua_pushinteger(L, 5);
    CHECK_FALSE(raises(L, check_named));
    lua_pushinteger(L, -1);
    CHECK_FALSE(raises(L, check_named)); // a table's own "none"
    for (int64_t bad : {int64_t(0), int64_t(2), int64_t(6), int64_t(-6), int64_t(1) << 40, INT64_MIN})
    {
        INFO(bad);
        lua_pushnumber(L, (lua_Number)bad);
        CHECK(raises(L, check_named));
    }
    lua_pushstring(L, "BETA");
    CHECK_FALSE(raises(L, check_named));
    lua_pushstring(L, "GAMMA");
    CHECK(raises(L, check_named));
    lua_newtable(L);
    CHECK(raises(L, check_named));
}

TEST_CASE_METHOD(LuaState, "a player range is a player of the game or all of them", "[kfx_script][lua_params][security]") {
    for (int64_t ok : {0, 4, 8, 9})
    {
        INFO(ok);
        lua_newtable(L);
        lua_pushinteger(L, ok);
        lua_setfield(L, -2, "playerId");
        CHECK_FALSE(raises(L, check_range));
    }
    for (int64_t bad : {-1, 10, 255, 1 << 20})
    {
        INFO(bad);
        lua_newtable(L);
        lua_pushinteger(L, bad);
        lua_setfield(L, -2, "playerId");
        CHECK(raises(L, check_range));
    }
    lua_pushnil(L);
    CHECK(raises(L, check_range)); // not a string either (it was handed to a string function as NULL)
}

TEST_CASE_METHOD(LuaState, "an effect number stays inside the effect tables", "[kfx_script][lua_params][security]") {
    lua_pushinteger(L, 2047);
    CHECK_FALSE(raises(L, check_effect));
    lua_pushinteger(L, -2047);
    CHECK_FALSE(raises(L, check_effect));
    lua_pushinteger(L, 2048);
    CHECK(raises(L, check_effect));
    lua_pushnumber(L, -1e15);
    CHECK(raises(L, check_effect));
}
