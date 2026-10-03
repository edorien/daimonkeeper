// kfx_render: LuaLensEffect.cpp -- the lifecycle/config surface only
// (constructor, Setup/Cleanup/Draw),
// all pure given a NULL lua_State (the constructor's RegisterBufferFunctions
// call is skipped when L is NULL, and Setup()'s three "TODO" asset-load
// branches are all skipped too since a default-constructed LuaLensConfig
// has empty mist_file/overlay_file and displacement_type == 0).
//
// LuaGetPixel/LuaSetPixel/LuaCopyPixel (the actual Lua-exposed pixel
// bounds-checked accessors) are private, only reachable by registering
// them into a real lua_State and running a Lua snippet that calls them
// with a userdata block shaped like the .cpp file's private
// LuaBufferInfo struct -- replicating that unexported layout in a test
// would be a fragile, maintenance-prone shortcut rather than a real
// fixture, so it's not attempted here.
#include <catch2/catch_test_macros.hpp>

#include "LuaLensEffect.h"

TEST_CASE("a LuaLensEffect constructed with a NULL lua_State reports its type/name safely", "[kfx_render][LuaLensEffect]") {
    LuaLensEffect lens("MyCustomLens", nullptr);
    CHECK(lens.GetType() == LensEffectType::Custom);
    CHECK(lens.GetLensName() == "MyCustomLens");
    CHECK(lens.IsEnabled());
}

TEST_CASE("Setup succeeds and records the lens index when no assets are configured", "[kfx_render][LuaLensEffect]") {
    LuaLensEffect lens("MyLens", nullptr);
    CHECK(lens.Setup(7));
}

TEST_CASE("Draw returns false before Setup (no draw callback registered yet)", "[kfx_render][LuaLensEffect]") {
    LuaLensEffect lens("MyLens", nullptr);
    CHECK_FALSE(lens.Draw(nullptr));
}

TEST_CASE("Draw returns false immediately after Cleanup (m_current_lens reset to 0)", "[kfx_render][LuaLensEffect]") {
    LuaLensEffect lens("MyLens", nullptr);
    lens.Setup(1);
    lens.Cleanup();
    CHECK_FALSE(lens.Draw(nullptr));
}

TEST_CASE("SetDrawCallback with a NULL lua_state doesn't crash on repeated calls", "[kfx_render][LuaLensEffect]") {
    LuaLensEffect lens("MyLens", nullptr);
    lens.SetDrawCallback(42);
    lens.SetDrawCallback(43); // releasing the old ref is skipped since m_lua_state == NULL
    SUCCEED();
}
