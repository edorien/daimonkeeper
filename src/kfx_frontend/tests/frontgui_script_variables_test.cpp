// kfx_frontend: the top-right readout's rows for the script's displayed variables (frontgui_ingame_debug.cpp,
// script_variable_rows()): every displayed variable, newest first, each with its value against the target and,
// for DISPLAY_VARIABLE_WITH_LABEL, its icon -- a custom one, or the variable kind's own over the message tile
// (upstream's draw_script_variable_list(); the overlay showed only the newest, as a bare number, before).
#include <catch2/catch_test_macros.hpp>

#include "frontgui_ingame_debug.h"
#include "bflib_video.h"
#include "dungeon_data.h"
#include "game_merge.h"
#include "kfx_game_state.h"
#include "lvl_script.h"
#include "lvl_script_lib.h"
#include "sprites.h"

#include <cstring>
#include <memory>

namespace {

struct ScopedScriptVariables {
    std::unique_ptr<KfxGameState> saved{new KfxGameState(kfx_game_state)};
    struct Dungeon *dungeon;
    int64_t saved_flag0, saved_flag1, saved_money, saved_upp;
    ScopedScriptVariables() {
        dungeon = get_dungeon(0);
        saved_flag0 = dungeon->script_flags[0];
        saved_flag1 = dungeon->script_flags[1];
        saved_money = dungeon->total_money_owned;
        saved_upp = units_per_pixel;
        units_per_pixel = 16;
    }
    ~ScopedScriptVariables() {
        std::memcpy(&kfx_game_state, saved.get(), sizeof(kfx_game_state));
        dungeon->script_flags[0] = saved_flag0;
        dungeon->script_flags[1] = saved_flag1;
        dungeon->total_money_owned = saved_money;
        units_per_pixel = saved_upp;
    }
};

struct ScriptVariable shown(unsigned char type, int64_t id, bool icon, int64_t icon_idx, int64_t target = 0,
    unsigned char target_type = 0) {
    struct ScriptVariable v;
    std::memset(&v, 0, sizeof(v));
    v.variable_player = 0;
    v.value_type = type;
    v.value_id = id;
    v.variable_target = target;
    v.variable_target_type = target_type;
    v.include_icon = icon;
    v.icon_idx = icon_idx;
    v.is_active = true;
    return v;
}

} // namespace

TEST_CASE("the readout lists every displayed script variable, newest first, with its icon", "[kfx_frontend][script_variables]") {
    ScopedScriptVariables scope;
    scope.dungeon->script_flags[0] = 5;
    scope.dungeon->script_flags[1] = 3;
    scope.dungeon->total_money_owned = 1200;
    kfx_game_state.script_variables[0] = shown(SVar_MONEY, 0, true, -1);           // labelled, kind's own icon
    kfx_game_state.script_variables[1] = shown(SVar_FLAG, 1, true, 77);             // labelled, custom icon
    kfx_game_state.script_variables[2] = shown(SVar_FLAG, 1, false, -1, 10, 0);     // counting down to 10
    kfx_game_state.script_variables[3] = shown(SVar_FLAG, 0, false, -1);            // plain
    kfx_game_state.active_script_var_count = 4;

    struct ScriptVariableRow rows[DISPLAY_VARIABLES_LIMIT];
    kfx_game_state.flags_gui &= ~GGUI_Variable;
    CHECK(script_variable_rows(rows, DISPLAY_VARIABLES_LIMIT) == 0); // not shown

    kfx_game_state.flags_gui |= GGUI_Variable;
    REQUIRE(script_variable_rows(rows, DISPLAY_VARIABLES_LIMIT) == 4);
    CHECK(std::strcmp(rows[0].text, "1200") == 0);
    CHECK(rows[0].icon == GPS_symbols_goldpot_sml);
    CHECK(rows[0].on_tile);
    CHECK(rows[0].dx == 0.0);
    CHECK(rows[0].dy == 0.0); // the gold pot sits where the tile does

    CHECK(std::strcmp(rows[1].text, "3") == 0);
    CHECK(rows[1].icon == 77);
    CHECK_FALSE(rows[1].on_tile);

    CHECK(std::strcmp(rows[2].text, "7") == 0); // 10 - 3
    CHECK(rows[2].icon == -1);

    CHECK(std::strcmp(rows[3].text, "5") == 0);
    CHECK(rows[3].icon == -1);

    CHECK(script_variable_rows(rows, 2) == 2); // at most max
}

TEST_CASE("a hidden slot past the count, or one marked inactive, isn't listed", "[kfx_frontend][script_variables]") {
    ScopedScriptVariables scope;
    kfx_game_state.flags_gui |= GGUI_Variable;
    kfx_game_state.script_variables[0] = shown(SVar_FLAG, 0, false, -1);
    kfx_game_state.script_variables[1] = shown(SVar_FLAG, 1, false, -1);
    kfx_game_state.script_variables[1].is_active = false;
    kfx_game_state.script_variables[2] = shown(SVar_FLAG, 0, false, -1); // a stale copy beyond the count
    kfx_game_state.active_script_var_count = 2;
    struct ScriptVariableRow rows[DISPLAY_VARIABLES_LIMIT];
    CHECK(script_variable_rows(rows, DISPLAY_VARIABLES_LIMIT) == 1);
}
