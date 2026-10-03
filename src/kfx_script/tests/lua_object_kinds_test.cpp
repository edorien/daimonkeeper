// kfx_script: lua_params.c's object kind checks.
//  * P5-F7: luaL_isThing() and luaL_isPlayer() read the object's ThingIndex/playerId through its metatable,
//    whose __index raises an error for a field its kind doesn't have: asking a Player if it is a thing (a
//    message icon given as a player: luaL_checkMessageIcon() asks luaL_isCreature() first), or a Thing if it
//    is a player (a location given as a creature), failed with an error instead of answering no.
#include "lua_script_fixture.h"

#include "lua_params.h"
#include "thing_data.h"

namespace {
int is_player_of_arg(lua_State *L) { lua_pushboolean(L, luaL_isPlayer(L, 1)); return 1; }
int is_thing_of_arg(lua_State *L) { lua_pushboolean(L, luaL_isThing(L, 1)); return 1; }

/** Calls fn on the value at the top of the stack, protected: 1 true, 0 false, -1 an error. */
int64_t ask(lua_CFunction fn) {
    lua_pushcfunction(Lvl_script, fn);
    lua_insert(Lvl_script, -2);
    if (lua_pcall(Lvl_script, 1, 1, 0) != 0) {
        lua_pop(Lvl_script, 1);
        return -1;
    }
    const int64_t r = lua_toboolean(Lvl_script, -1) ? 1 : 0;
    lua_pop(Lvl_script, 1);
    return r;
}
} // namespace

TEST_CASE_METHOD(LuaScriptFixture, "P5-F7: asking a Player if it is a thing, or a Thing if it is a player, answers no", "[kfx_script][lua_display_variable]") {
    lua_pushPlayer(Lvl_script, 0);
    CHECK(ask(is_thing_of_arg) == 0); // was an error: the Player's __index has no ThingIndex
    lua_pushPlayer(Lvl_script, 0);
    CHECK(ask(is_player_of_arg) == 1);
    lua_pushThing(Lvl_script, thing_get(1));
    REQUIRE(lua_istable(Lvl_script, -1)); // a Thing object, not nil
    CHECK(ask(is_player_of_arg) == 0); // was an error: the Thing's __index has no playerId
}
