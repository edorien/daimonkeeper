// kfx_config: net_callbacks.c, a pure callback-registration file (the
// "NetCallbacks" seam architecture.md §5.1 documents: kfx_net implements
// it, main.cpp wires it in setup_game(), and everything below kfx_net
// calls through the net_callbacks pointer instead of #including kfx_net
// directly). Every one of its 51 noop_* stubs is `static`, so the only
// way to actually invoke (and gcov-count) each one is through the
// default_net_callbacks table's function-pointer fields -- exercised
// here as one exhaustive call-every-field test, since every stub's body
// is a one-liner that never dereferences its pointer arguments (verified
// by reading each one, not assumed), making NULL/dummy arguments safe
// throughout.
#include <catch2/catch_test_macros.hpp>

#include "net_callbacks.h"

TEST_CASE("the default net_callbacks table's every stub is a safe no-op returning its documented default", "[kfx_config][net_callbacks]") {
    REQUIRE(net_callbacks != nullptr);

    net_callbacks->enter_net_session_screen();
    net_callbacks->set_lobby_button_labels(true);
    net_callbacks->create_frontend_error_box(1000, "test");
    CHECK(net_callbacks->frontend_save_continue_game(0) == 0);
    CHECK(net_callbacks->toggle_status_menu(1) == 0);
    net_callbacks->set_gui_visible(true);
    CHECK(net_callbacks->get_default_tag_mode() == 0);
    CHECK_FALSE(net_callbacks->is_frontend_starting_mp_level());
    CHECK_FALSE(net_callbacks->is_frontend_at_initial_state());

    net_callbacks->display_attempting_to_join_message(5);
    CHECK_FALSE(net_callbacks->attempting_to_join_cancel_requested());
    net_callbacks->reset_attempting_to_join_cancel();
    net_callbacks->process_network_error(1);
    CHECK_FALSE(net_callbacks->frontnet_service_selected(0));

    net_callbacks->turn_off_all_menus();
    net_callbacks->turn_off_query_menus();
    net_callbacks->turn_on_main_panel_menu();
    net_callbacks->turn_off_all_panel_menus();
    net_callbacks->turn_on_menu(0);

    net_callbacks->panel_map_update(0, 0, 1, 1);

    net_callbacks->update_trap_tab_to_config();
    net_callbacks->instant_instance_selected(0);

    CHECK(net_callbacks->is_key_pressed(0, 0) == 0);
    net_callbacks->clear_key_pressed(0);

    HitPoints hp = 0;
    CHECK_FALSE(net_callbacks->process_cheat_heart_health_inputs(&hp, 100));

    net_callbacks->clear_player_lightning_palette(nullptr);

    CHECK_FALSE(net_callbacks->cmd_exec(0, nullptr));

    net_callbacks->lua_on_chatmsg(0, nullptr);

    CHECK_FALSE(net_callbacks->lua_script_active());
    size_t len = 999;
    const char *exported = net_callbacks->lua_resync_export(&len);
    REQUIRE(exported != nullptr);
    CHECK(exported[0] == '\0');
    CHECK(len == 0);
    CHECK(net_callbacks->lua_resync_import("", 0));
    net_callbacks->lua_set_random_seed(0);
    net_callbacks->lua_cleanup_serialized_data();

    net_callbacks->network_yield_poll_gameplay();
    net_callbacks->network_yield_waiting_gameplay_packets();
    net_callbacks->network_yield_draw_frontend();
    CHECK_FALSE(net_callbacks->output_message(0, 0));
    CHECK(net_callbacks->report_error_stat(0) == 0);
    CHECK_FALSE(net_callbacks->show_onscreen_msg(0, nullptr));
    CHECK_FALSE(net_callbacks->is_onscreen_msg_visible());
    int32_t plyr_count = 0;
    CHECK(net_callbacks->winning_player_quitting(nullptr, &plyr_count) == 0);
    net_callbacks->reinit_level_after_load();
    CHECK(net_callbacks->complete_level(nullptr) == 0);
    CHECK(net_callbacks->lose_level(nullptr) == 0);
    CHECK(net_callbacks->resign_level(nullptr) == 0);
    CHECK(net_callbacks->load_game_chunks(nullptr, nullptr) == 0);
    CHECK_FALSE(net_callbacks->fill_game_catalogue_entry(nullptr, "name"));
    CHECK_FALSE(net_callbacks->save_packet_chunks(nullptr, nullptr));
    net_callbacks->draw_out_of_sync_box(0, 0, 0);
    net_callbacks->process_frontend_chat_message(0, "hi");
    net_callbacks->set_host_packet_received(0);
}

TEST_CASE("set_net_callbacks installs a custom table and falls back to the default once cleared", "[kfx_config][net_callbacks]") {
    struct NetCallbacks fake = *net_callbacks; // start from a valid table, override one field
    static bool called = false;
    called = false;
    fake.enter_net_session_screen = []() { called = true; };

    set_net_callbacks(&fake);
    net_callbacks->enter_net_session_screen();
    CHECK(called);

    set_net_callbacks(nullptr); // restores the default table
    called = false;
    net_callbacks->enter_net_session_screen();
    CHECK_FALSE(called); // back to the no-op default
}
