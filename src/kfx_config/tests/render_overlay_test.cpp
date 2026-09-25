// kfx_config: render_overlay.c -- same callback-registration shape as
// net_callbacks_test.cpp. Every struct-pointer parameter (Camera/Thing/
// Packet/PlayerInfo/InitLight) is only forward-declared in this header
// and every noop_* body ignores its arguments (verified by reading each
// one), so nullptr is safe throughout.
#include <catch2/catch_test_macros.hpp>

#include "render_overlay.h"

TEST_CASE("the default render_overlay table's every stub is a safe no-op returning its documented default", "[kfx_config][render_overlay]") {
    REQUIRE(render_overlay != nullptr);

    render_overlay->redraw_parchment_view();
    render_overlay->load_and_redraw_minimal_overhead_view();
    render_overlay->set_parchment_loaded(1);
    CHECK_FALSE(render_overlay->is_parchment_loaded());
    render_overlay->reload_parchment_file(true);
    int64_t map_x, map_y;
    CHECK_FALSE(render_overlay->point_to_overhead_map(nullptr, 0, 0, &map_x, &map_y));

    CHECK(render_overlay->get_main_menu_width() == 0);

    render_overlay->draw_gui_panel_sprite_left(0, 0, 1, 0);
    render_overlay->draw_slab64k(0, 0, 1, 0, 0);
    render_overlay->draw_gui_panel_sprite_centered(0, 0, 1, 0);
    render_overlay->draw_button_sprite_left(0, 0, 1, 0);

    render_overlay->message_draw();
    render_overlay->gui_draw_all_boxes();
    render_overlay->draw_tooltip();
    render_overlay->sync_cheat_box_3_active_option(0);
    CHECK_FALSE(render_overlay->cheat_or_menu_window_active());

    render_overlay->draw_eastegg();

    render_overlay->set_winfont();
    CHECK(render_overlay->get_status_panel_width() == 0);
    render_overlay->draw_gui();
    CHECK_FALSE(render_overlay->game_is_busy_doing_gui());

    CHECK_FALSE(render_overlay->game_is_busy_doing_gui_string_input());

    render_overlay->draw_whole_status_panel();

    render_overlay->draw_debug_overlays();
    CHECK_FALSE(render_overlay->bonus_script_or_variable_overlay_active());
    CHECK(render_overlay->get_battle_creature_over() == 0);

    CHECK(render_overlay->get_map_diagonal_length() == 0);

    CHECK_FALSE(render_overlay->get_unpausing_in_progress());
    CHECK_FALSE(render_overlay->can_process_creature_input(nullptr));
    int64_t out_h = 0, out_v = 0, out_r = 0;
    render_overlay->process_first_person_look(nullptr, nullptr, 0, 0, &out_h, &out_v, &out_r);
    render_overlay->process_camera_controls(nullptr, nullptr, nullptr, false);
    render_overlay->process_camera_action(nullptr, nullptr);
    CHECK(render_overlay->get_history_packet(0, 0) == nullptr);
    render_overlay->set_packet_control(nullptr, 0);

    render_overlay->frontend_load_data_from_cd();
    render_overlay->frontend_load_data_reset();
    CHECK(render_overlay->menu_is_active(0) == 0);
    render_overlay->reinit_all_menus();

    render_overlay->turn_on_menu(0);

    render_overlay->sync_render_globals();

    CHECK_FALSE(render_overlay->setup_heap_manager());
    render_overlay->reset_heap_manager();
    CHECK(render_overlay->he_alloc(16) == nullptr);
    CHECK(render_overlay->light_create_light(nullptr) == 0);
    render_overlay->light_set_attached_slab(0, 0);
    render_overlay->delete_lights_attached_to_slab_in_area(0, 0, 0, 0, 0);
    CHECK_FALSE(render_overlay->get_lights_enabled());
    CHECK(render_overlay->get_interpolate_time() == 0.0);
}

TEST_CASE("set_render_overlay_callbacks installs a custom table and falls back to the default once cleared", "[kfx_config][render_overlay]") {
    struct RenderOverlayCallbacks fake = *render_overlay;
    static bool called = false;
    called = false;
    fake.redraw_parchment_view = []() { called = true; };

    set_render_overlay_callbacks(&fake);
    render_overlay->redraw_parchment_view();
    CHECK(called);

    set_render_overlay_callbacks(nullptr);
    called = false;
    render_overlay->redraw_parchment_view();
    CHECK_FALSE(called);
}
