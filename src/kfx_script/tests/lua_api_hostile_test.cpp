// Security pass S08 (docs/security-pass/README.md): what a level's Lua script can pass to the API. These calls came from
// the Lua API fuzzer (kfx_fuzz lua_api), which found an out-of-bounds write or read in each: a number used as an index
// into a table of creatures, box tooltips or players, a map rectangle of any size, a class with no list. Each now fails
// with a Lua error (or does nothing); the sanitizer build (scripts/build-fuzz.sh asan) fails them if they go out of
// bounds again.
#include "lua_script_fixture.h"

#include <string>

namespace {
/** Runs Lua code; true if it ran without raising an error. */
bool runs(const std::string &code)
{
    if (luaL_loadstring(Lvl_script, code.c_str()) != 0) {
        lua_pop(Lvl_script, 1);
        return false;
    }
    const bool ok = (lua_pcall(Lvl_script, 0, 0, 0) == 0);
    if (!ok)
        lua_pop(Lvl_script, 1);
    return ok;
}
}

TEST_CASE("a creature given as a number that is no creature is refused", "[kfx_script][lua_api][security]") {
    LuaScriptFixture fx;
    for (const char *call : {"AddCreatureToPool(-6, 1)", "AddCreatureToPool(1e15, 1)", "AddCreatureToPool(-9223372036854775807, 1)",
                             "CopyCreatureType(-1, 'COPY')", "CopyCreatureType(1e18, 'COPY')"})
    {
        INFO(call);
        CHECK_FALSE(runs(call));
    }
}

TEST_CASE("a box tooltip number is one of the boxes", "[kfx_script][lua_api][security]") {
    LuaScriptFixture fx;
    CHECK(runs("SetBoxTooltip(3, 'text')"));
    CHECK(runs("SetBoxTooltip(255, 'text')"));
    for (const char *call : {"SetBoxTooltip(-1, 'x')", "SetBoxTooltip(256, 'x')", "SetBoxTooltip(2^62, 'x')", "SetBoxTooltip(-2^63, 'x')",
                             "SetBoxTooltipId(-19, 1)", "SetBoxTooltipId(2^62, 1)"})
    {
        INFO(call);
        CHECK_FALSE(runs(call));
    }
}

TEST_CASE("a class with no list of things gives no things", "[kfx_script][lua_api][security]") {
    LuaScriptFixture fx;
    // used to walk the list of a class that has none
    CHECK(runs("local t = GetThingsOfClass('Creature') assert(type(t) == 'table')"));
    CHECK_FALSE(runs("GetThingsOfClass(-3)"));
    CHECK_FALSE(runs("GetThingsOfClass(1e9)"));
}

TEST_CASE("a map rectangle that is not on the map is refused", "[kfx_script][lua_api][security]") {
    LuaScriptFixture fx;
    for (const char *call : {"ConcealMapRect('PLAYER0', 0, 0, 2^60, 2^60)", "RevealMapRect('PLAYER0', 0, 0, -5, 3)",
                             "ConcealMapRect({playerId = 50}, 0, 0, 3, 3)", "RevealMapRect({playerId = -1}, 0, 0, 3, 3)"})
    {
        INFO(call);
        CHECK_FALSE(runs(call));
    }
}

TEST_CASE("a player that does not exist, a party that is not a name and a position off the map are refused", "[kfx_script][lua_api][security]") {
    LuaScriptFixture fx;
    for (const char *call : {"DeleteFromParty(5, 'IMP', 1)", "AddToParty({}, 'IMP', 1, 0, 'ATTACK_ENEMIES', 0)", "DeleteFromParty(nil, 'IMP', 1)",
                             "SetGenerateSpeed(100, {playerId = 77})", "AddCreatureToPool({}, 1)"})
    {
        INFO(call);
        CHECK_FALSE(runs(call));
    }
}
