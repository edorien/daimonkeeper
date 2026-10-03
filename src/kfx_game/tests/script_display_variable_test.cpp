// kfx_game: the list of displayed script variables (lvl_script_lib.c's script_display_variable() and
// script_hide_variable(), which DISPLAY_VARIABLE, DISPLAY_VARIABLE_WITH_LABEL, HIDE_VARIABLE and their Lua
// twins share).
//  * P5-F3: a labelled variable has no target; it took the previous newest entry's.
//  * P5-F4: hiding a variable looked at every slot, not only the shown ones, so hiding one already hidden
//    could match its stale copy and take a shown variable off the list.
#include <catch2/catch_test_macros.hpp>

#include "game_merge.h"
#include "kfx_game_state.h"
#include "lvl_script.h"
#include "lvl_script_lib.h"

#include <cstring>
#include <memory>

namespace {

struct ScopedGameState {
    std::unique_ptr<KfxGameState> saved{new KfxGameState(kfx_game_state)};
    ScopedGameState() {
        std::memset(kfx_game_state.script_variables, 0, sizeof(kfx_game_state.script_variables));
        kfx_game_state.active_script_var_count = 0;
        kfx_game_state.flags_gui &= ~GGUI_Variable;
    }
    ~ScopedGameState() { std::memcpy(&kfx_game_state, saved.get(), sizeof(kfx_game_state)); }
};

const struct ScriptVariable &shown(int64_t i) { return kfx_game_state.script_variables[i]; }

} // namespace

TEST_CASE("displayed variables are listed newest first, the oldest leaving a full list", "[kfx_game][display_variable]") {
    ScopedGameState scope;
    for (int64_t i = 0; i < DISPLAY_VARIABLES_LIMIT + 2; i++)
        script_display_variable(0, SVar_FLAG, i, 0, 0, false, -1);
    CHECK(kfx_game_state.active_script_var_count == DISPLAY_VARIABLES_LIMIT);
    CHECK(shown(0).value_id == DISPLAY_VARIABLES_LIMIT + 1);
    CHECK(shown(DISPLAY_VARIABLES_LIMIT - 1).value_id == 2);
    CHECK((kfx_game_state.flags_gui & GGUI_Variable) != 0);
}

TEST_CASE("P5-F3: a labelled variable has no target of its own", "[kfx_game][display_variable]") {
    ScopedGameState scope;
    script_display_variable(0, SVar_FLAG, 0, 10, 1, false, -1); // counting down to 10
    script_display_variable(0, SVar_FLAG, 1, 0, 0, true, -1);   // labelled: no target
    CHECK(shown(0).value_id == 1);
    CHECK(shown(0).variable_target == 0);
    CHECK(shown(0).variable_target_type == 0);
    CHECK(shown(0).include_icon);
    CHECK(shown(0).icon_idx == -1);
    CHECK(shown(1).variable_target == 10);
}

TEST_CASE("P5-F4: hiding a variable that isn't shown leaves the shown ones", "[kfx_game][display_variable]") {
    ScopedGameState scope;
    script_display_variable(0, SVar_FLAG, 0, 0, 0, false, -1); // A
    script_hide_variable(0, SVar_FLAG, 0);                      // A hidden
    CHECK(kfx_game_state.active_script_var_count == 0);
    CHECK((kfx_game_state.flags_gui & GGUI_Variable) == 0);
    script_display_variable(0, SVar_FLAG, 1, 0, 0, false, -1); // B
    script_hide_variable(0, SVar_FLAG, 0);                      // A again: not shown
    REQUIRE(kfx_game_state.active_script_var_count == 1);
    CHECK(shown(0).value_id == 1);
    CHECK((kfx_game_state.flags_gui & GGUI_Variable) != 0);
}

TEST_CASE("hiding by player, and everyone's", "[kfx_game][display_variable]") {
    ScopedGameState scope;
    script_display_variable(0, SVar_FLAG, 0, 0, 0, false, -1);
    script_display_variable(1, SVar_FLAG, 0, 0, 0, false, -1);
    script_display_variable(0, SVar_FLAG, 1, 0, 0, false, -1);
    script_display_variable(0, SVar_FLAG, 1, 0, 0, false, -1); // shown twice
    script_hide_variable(0, SVar_FLAG, 1);                      // its newest entry
    CHECK(kfx_game_state.active_script_var_count == 3);
    script_hide_variable(0, -1, -1);                            // the rest of player 0's
    REQUIRE(kfx_game_state.active_script_var_count == 1);
    CHECK(shown(0).variable_player == 1);
    script_hide_variable(-1, -1, -1);                           // everyone's
    CHECK(kfx_game_state.active_script_var_count == 0);
    CHECK((kfx_game_state.flags_gui & GGUI_Variable) == 0);
}
