// kfx_config: game_callbacks.c -- same shape as net_callbacks_test.cpp:
// every noop_* stub is `static`, only reachable through the default
// table's function-pointer fields, and every one's body is a one-liner
// that never dereferences its pointer args (noop_get_high_score_entry
// is the sole exception -- it writes a single NUL byte to dest, guarded
// by dest_size, so it needs a real buffer, not nullptr).
#include <catch2/catch_test_macros.hpp>

#include "game_callbacks.h"

TEST_CASE("the default game_callbacks table's every stub is a safe no-op returning its documented default", "[kfx_config][game_callbacks]") {
    REQUIRE(game_callbacks != nullptr);

    CHECK(game_callbacks->toggle_main_cheat_menu() == 0);
    CHECK(game_callbacks->toggle_instance_cheat_menu() == 0);
    CHECK_FALSE(game_callbacks->toggle_secondary_cheat_menu());
    CHECK_FALSE(game_callbacks->toggle_creature_cheat_menu());
    CHECK_FALSE(game_callbacks->close_main_cheat_menu());
    CHECK_FALSE(game_callbacks->close_instance_cheat_menu());
    CHECK_FALSE(game_callbacks->close_secondary_cheat_menu());
    CHECK_FALSE(game_callbacks->close_creature_cheat_menu());
    game_callbacks->create_error_box(0);
    CHECK_FALSE(game_callbacks->is_fe_computer_players_active());
    CHECK_FALSE(game_callbacks->is_fe_spectate_campaign_active());
    game_callbacks->set_gui_visible(true);
    CHECK(game_callbacks->is_menu_active(0) == 0);

    game_callbacks->set_timer_turns(0);
    CHECK_FALSE(game_callbacks->is_timer_enabled());
    game_callbacks->toggle_debug_network_stats();
    CHECK_FALSE(game_callbacks->is_bonus_timer_enabled());

    game_callbacks->go_to_my_next_room_of_type(0);
    CHECK(game_callbacks->get_button_designation(0, 0) == -1);
    game_callbacks->gui_set_button_flashing(0, 0);

    CHECK(game_callbacks->create_gui_box(0, 0, nullptr) == nullptr);

    game_callbacks->zero_all_messages();
    game_callbacks->show_game_time_taken(0, 0);

    CHECK_FALSE(game_callbacks->toggle_tooltip_land_coord());

    char dest[8] = {'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X'};
    game_callbacks->get_high_score_entry(dest, sizeof(dest));
    CHECK(dest[0] == '\0');
    game_callbacks->set_high_score_entry("name");

    game_callbacks->frontstats_initialise();

    game_callbacks->setup_alliances();

    game_callbacks->clear_all_messages();
    game_callbacks->process_all_messages();
    game_callbacks->script_play_message(false, 0, 0, nullptr);

    game_callbacks->turn_on_ingame_menu(0);
    game_callbacks->turn_off_ingame_menu(0);

    game_callbacks->clear_top_message_stats();

    game_callbacks->init_gui();

    game_callbacks->set_level_objective(0, nullptr);
    game_callbacks->display_objectives(0, 0, 0);
    game_callbacks->display_objectives_with_icon(0, 0, 0, 0);

    game_callbacks->reset_gui_based_on_player_mode();

    game_callbacks->update_panel_colors();
    CHECK_FALSE(game_callbacks->save_frontend_state(nullptr));
    CHECK_FALSE(game_callbacks->load_frontend_state(nullptr));
    game_callbacks->reset_frontend_state();
    CHECK(game_callbacks->get_frontend_state_size() == 0);
    CHECK(game_callbacks->get_intralvl_next_level() == 0);
    game_callbacks->clear_intralvl_next_level();
}

TEST_CASE("set_game_callbacks installs a custom table and falls back to the default once cleared", "[kfx_config][game_callbacks]") {
    struct GameCallbacks fake = *game_callbacks;
    static bool called = false;
    called = false;
    fake.frontstats_initialise = []() { called = true; };

    set_game_callbacks(&fake);
    game_callbacks->frontstats_initialise();
    CHECK(called);

    set_game_callbacks(nullptr);
    called = false;
    game_callbacks->frontstats_initialise();
    CHECK_FALSE(called);
}

TEST_CASE("get_high_score_entry's default stub writes nothing when dest_size is 0", "[kfx_config][game_callbacks]") {
    char dest[1] = {'Y'};
    game_callbacks->get_high_score_entry(dest, 0);
    CHECK(dest[0] == 'Y'); // untouched: the dest_size>0 guard skips the write
}
