// kfx_script: Lua's DisplayVariable / DisplayVariableWithLabel / HideVariable, which share the script commands'
// list (lvl_script_lib.c's script_display_variable() / script_hide_variable()).
//  * P5-F5: HideVariable(player, variable) read the variable from the player argument (an error).
//  * P5-F6: DisplayVariableWithLabel stored the icon's message id (a creature model, a player number, 0 for none)
//    as a panel sprite; the icon is now the sprite the script command's would be, none meaning the kind's own.
#include "lua_script_fixture.h"

#include "game_merge.h"
#include "kfx_game_state.h"
#include "lvl_script.h"
#include "lvl_script_lib.h"
#include "gui_msgs.h"

#include <memory>

namespace {

struct ScopedVariables {
    std::unique_ptr<KfxGameState> saved{new KfxGameState(kfx_game_state)};
    ScopedVariables() {
        std::memset(kfx_game_state.script_variables, 0, sizeof(kfx_game_state.script_variables));
        kfx_game_state.active_script_var_count = 0;
    }
    ~ScopedVariables() { std::memcpy(&kfx_game_state, saved.get(), sizeof(kfx_game_state)); }
};

bool run(const char *code) {
    if (luaL_dostring(Lvl_script, code) == 0)
        return true;
    UNSCOPED_INFO(lua_tostring(Lvl_script, -1));
    lua_pop(Lvl_script, 1);
    return false;
}

} // namespace

TEST_CASE_METHOD(LuaScriptFixture, "P5-F5: HideVariable hides the named variable", "[kfx_script][lua_display_variable]") {
    ScopedVariables scope;
    REQUIRE(run("DisplayVariable(PLAYER0, 'FLAG0') DisplayVariable(PLAYER0, 'FLAG1', 10)"));
    REQUIRE(kfx_game_state.active_script_var_count == 2);
    REQUIRE(run("HideVariable(PLAYER0, 'FLAG1')"));
    REQUIRE(kfx_game_state.active_script_var_count == 1);
    CHECK(kfx_game_state.script_variables[0].value_id == 0);
    REQUIRE(run("HideVariable(PLAYER0)"));
    CHECK(kfx_game_state.active_script_var_count == 0);
    CHECK((kfx_game_state.flags_gui & GGUI_Variable) == 0);
}

TEST_CASE_METHOD(LuaScriptFixture, "P5-F6: DisplayVariableWithLabel's icon is a panel sprite", "[kfx_script][lua_display_variable]") {
    ScopedVariables scope;
    REQUIRE(run("DisplayVariableWithLabel(PLAYER0, 'FLAG0')"));
    CHECK(kfx_game_state.script_variables[0].include_icon);
    CHECK(kfx_game_state.script_variables[0].icon_idx == -1); // none: the variable kind's own
    CHECK(kfx_game_state.script_variables[0].variable_target == 0);
    REQUIRE(run("DisplayVariableWithLabel(PLAYER0, 'FLAG1', PLAYER0)"));
    CHECK(kfx_game_state.script_variables[0].icon_idx == get_chat_icon_sprite_idx_from_id(0, MsgType_Player));
    CHECK(kfx_game_state.script_variables[0].icon_idx > 0);
}
