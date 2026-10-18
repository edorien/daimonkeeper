// Tier 2 for kfx_script, per docs/refactor/testing/comprehensive/
// stage-06-kfx-script-luajit.md §2/§3: calls the real open_lua_script()
// production entry point directly, rather than a hand-rolled narrower
// init -- confirmed viable by tracing its file-loading calls (the
// mandatory init.lua load only needs this repo's own config/fxdata/lua/
// tree, not original game data; campaign/level/mod loads gracefully
// no-op via LbFileExists() guards when absent).
#include "lua_script_fixture.h"
#include "lens_api.h"

TEST_CASE_METHOD(LuaScriptFixture, "open_lua_script loads the real production init.lua chain", "[kfx_script][lua_base]") {
    // init.lua's own top-level code defines Game = {} once every
    // `require`d module (core.serialisation, triggers.*, classes.*,
    // managers.*, gamelogic.ShotFunctions, utils.Debug) has loaded
    // without error -- the simplest real signal that the whole chain
    // (reg_host_functions()'s C bindings, setLuaPath(), every require)
    // actually ran, not just that open_lua_script() returned true.
    lua_getglobal(Lvl_script, "Game");
    CHECK(lua_istable(Lvl_script, -1));
    lua_pop(Lvl_script, 1);
}

namespace {
// Runs `code` in Lvl_script under a plain lua_pcall (no traceback handler, so the error object reaches CheckLua
// exactly as the script raised it) and hands the result to CheckLua.
bool check_lua_on(const char *code) {
    lua_State *L = Lvl_script;
    REQUIRE(luaL_loadstring(L, code) == LUA_OK);
    return CheckLua(L, lua_pcall(L, 0, 0, 0), "lua_base_test");
}
}

TEST_CASE_METHOD(LuaScriptFixture, "CheckLua reports a non-string error without calling into Lua", "[kfx_script][lua_base]") {
    lua_State *L = Lvl_script;
    const int top = lua_gettop(L);
    SECTION("an error object whose __tostring raises an error") {
        // Before the fix, CheckLua called tostring() on this unprotected: the __tostring error escaped every pcall
        // and LuaJIT's panic handler ended the process.
        CHECK_FALSE(check_lua_on("error(setmetatable({}, {__tostring = function() error('x') end}))"));
    }
    SECTION("tostring removed by the script") {
        CHECK_FALSE(check_lua_on("tostring = nil; error({})"));
    }
    SECTION("an error that is not a table or string") {
        CHECK_FALSE(check_lua_on("error(true)"));
        CHECK_FALSE(check_lua_on("error(nil)"));
    }
    SECTION("a string error is still a string, and still reaches the compat report") {
        CHECK_FALSE(check_lua_on("error('plain message')"));
        CHECK_FALSE(check_lua_on("NoSuchFunctionInThisBuild()"));
    }
    CHECK(lua_gettop(L) == top); // the error object is popped either way
    CHECK(check_lua_on("return 1"));
}

namespace {
bool g_old_state_closed = false;
int mark_state_closed(lua_State *) {
    g_old_state_closed = true;
    return 0;
}
// A userdata whose __gc only runs when its state is closed (it is anchored in the registry).
void plant_close_sentinel(lua_State *L) {
    g_old_state_closed = false;
    lua_newuserdata(L, 1);
    lua_newtable(L);
    lua_pushcfunction(L, mark_state_closed);
    lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -2);
    lua_setfield(L, LUA_REGISTRYINDEX, "lua_base_test.close_sentinel");
}
}

TEST_CASE_METHOD(LuaScriptFixture, "open_lua_script closes the state it replaces", "[kfx_script][lua_base]") {
    // A level restart or a save load opens the scripts again; the state they replace must not be leaked.
    plant_close_sentinel(Lvl_script);
    REQUIRE(open_lua_script(0));
    CHECK(g_old_state_closed);
    REQUIRE(Lvl_script != nullptr);
    lua_getglobal(Lvl_script, "Game");
    CHECK(lua_istable(Lvl_script, -1));
    lua_pop(Lvl_script, 1);
}

TEST_CASE_METHOD(LuaScriptFixture, "open_lua_script leaves no state behind when init.lua is missing", "[kfx_script][lua_base]") {
    plant_close_sentinel(Lvl_script);
    std::strncpy(keeper_runtime_directory, KFX_REPO_CONFIG_DIR "/no-such-directory", sizeof(keeper_runtime_directory) - 1);
    CHECK_FALSE(open_lua_script(0));
    CHECK(g_old_state_closed);
    CHECK(Lvl_script == nullptr);
    close_lua_script(); // closing twice is harmless
    close_lua_script();
    CHECK(Lvl_script == nullptr);
}

TEST_CASE_METHOD(LuaScriptFixture, "closing the scripts' state takes the custom lenses made in it along", "[kfx_script][lua_base]") {
    // A custom lens keeps the lua_State its draw callback lives in; once open_lua_script() closes the state it
    // replaces, a lens left behind would draw through (and, when deleted, unref in) freed memory.
    void *mgr = LensManager_GetInstance();
    REQUIRE(mgr != nullptr);
    REQUIRE(execute_lua_code_from_script(
        "assert(CreateLens('lua_base_test_lens'))\n"
        "assert(SetLensDrawCallback('lua_base_test_lens', function(ctx) return false end))\n"));
    REQUIRE(LensManager_GetCustomLens(mgr, "lua_base_test_lens") != nullptr);

    REQUIRE(open_lua_script(0));
    CHECK(LensManager_GetCustomLens(mgr, "lua_base_test_lens") == nullptr);
    // the new scripts make it again, in their own state
    REQUIRE(execute_lua_code_from_script("assert(CreateLens('lua_base_test_lens'))"));
    CHECK(LensManager_GetCustomLens(mgr, "lua_base_test_lens") != nullptr);
    close_lua_script();
    CHECK(LensManager_GetCustomLens(mgr, "lua_base_test_lens") == nullptr);
}
