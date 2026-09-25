// kfx_script: lua_utils.c -- pure Lua-stack helper functions used by the
// per-class __index metamethods (try_get_c_method scans a plain
// luaL_Reg[] table; try_get_from_methods walks an object's metatable
// looking for a "__methods" sub-table). Same bare luaL_newstate()
// fixture as lua_params_test.cpp -- deliberately not the full
// open_lua_script() init chain, since these two functions only need a
// live Lua stack, not the production Lvl_script/reg_host_functions()
// setup.
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <catch2/catch_test_macros.hpp>

#include "lua_utils.h"

namespace {
struct LuaState {
    lua_State *L;
    LuaState() { L = luaL_newstate(); }
    ~LuaState() { lua_close(L); }
};

int dummy_cfunc(lua_State *) { return 0; }
}

TEST_CASE_METHOD(LuaState, "try_get_c_method finds a matching entry by name and pushes its function", "[kfx_script][lua_utils]") {
    luaL_Reg methods[] = {
        {"foo", dummy_cfunc},
        {NULL, NULL},
    };
    int64_t top_before = lua_gettop(L);
    CHECK(try_get_c_method(L, "foo", methods));
    CHECK(lua_gettop(L) == top_before + 1);
    CHECK(lua_iscfunction(L, -1));
}

TEST_CASE_METHOD(LuaState, "try_get_c_method returns false and pushes nothing when the name isn't found", "[kfx_script][lua_utils]") {
    luaL_Reg methods[] = {
        {"foo", dummy_cfunc},
        {NULL, NULL},
    };
    int64_t top_before = lua_gettop(L);
    CHECK_FALSE(try_get_c_method(L, "bar", methods));
    CHECK(lua_gettop(L) == top_before); // stack unchanged
}

TEST_CASE_METHOD(LuaState, "try_get_c_method returns false immediately for an empty methods table", "[kfx_script][lua_utils]") {
    luaL_Reg methods[] = {
        {NULL, NULL},
    };
    CHECK_FALSE(try_get_c_method(L, "anything", methods));
}

TEST_CASE_METHOD(LuaState, "try_get_from_methods is false when the object has no metatable at all", "[kfx_script][lua_utils]") {
    lua_newtable(L); // a plain table, no metatable set
    CHECK_FALSE(try_get_from_methods(L, -1, "foo"));
    lua_pop(L, 1);
}

TEST_CASE_METHOD(LuaState, "try_get_from_methods is false when the metatable has no __methods field", "[kfx_script][lua_utils]") {
    lua_newtable(L);          // object
    lua_newtable(L);          // metatable, no __methods
    lua_setmetatable(L, -2);
    CHECK_FALSE(try_get_from_methods(L, -1, "foo"));
    lua_pop(L, 1);
}

TEST_CASE_METHOD(LuaState, "try_get_from_methods is false when __methods exists but the key isn't in it", "[kfx_script][lua_utils]") {
    lua_newtable(L);          // object
    lua_newtable(L);          // metatable
    lua_newtable(L);          // __methods (empty)
    lua_setfield(L, -2, "__methods");
    lua_setmetatable(L, -2);
    CHECK_FALSE(try_get_from_methods(L, -1, "foo"));
    lua_pop(L, 1);
}

TEST_CASE_METHOD(LuaState, "try_get_from_methods finds and pushes a function registered under __methods", "[kfx_script][lua_utils]") {
    lua_newtable(L);          // object
    lua_newtable(L);          // metatable
    lua_newtable(L);          // __methods
    lua_pushcfunction(L, dummy_cfunc);
    lua_setfield(L, -2, "foo"); // __methods.foo = dummy_cfunc
    lua_setfield(L, -2, "__methods");
    lua_setmetatable(L, -2);

    int64_t obj_index = lua_gettop(L);
    CHECK(try_get_from_methods(L, obj_index, "foo"));
    CHECK(lua_iscfunction(L, -1));
    lua_pop(L, 1); // the pushed function
    lua_pop(L, 1); // the object
}
