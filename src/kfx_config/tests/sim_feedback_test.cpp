// kfx_config: sim_feedback.c -- same shape as net_callbacks_test.cpp/
// game_callbacks_test.cpp/etc.: every noop_* stub is `static`, only
// reachable through the default table's function-pointer fields, and
// every one's body is a one-liner that never dereferences its pointer
// args -- missed by the original kfx_config *Callbacks sweep despite
// being the exact same pattern.
#include <catch2/catch_test_macros.hpp>

#include "sim_feedback.h"

TEST_CASE("the default sim_feedback table's every stub is a safe no-op returning its documented default", "[kfx_config][sim_feedback]") {
    REQUIRE(sim_feedback != nullptr);

    CHECK(sim_feedback->report_error_stat(0) == 0);
    CHECK_FALSE(sim_feedback->show_onscreen_msg(0, nullptr));

    CHECK_FALSE(sim_feedback->play_sound_message(0, 0));
    CHECK_FALSE(sim_feedback->output_room_message(0, 0, 0));
    CHECK_FALSE(sim_feedback->play_sound_message_far_from_thing(nullptr, 0, 0));
    CHECK_FALSE(sim_feedback->play_speech_ref(nullptr, 0));
    sim_feedback->clear_sound_messages();
    sim_feedback->process_sound_messages();

    sim_feedback->clear_messages_from_player(0, 0);
    sim_feedback->targeted_message_add(0, 0, 0, 0, nullptr);
    sim_feedback->message_add(0, 0, nullptr);
    sim_feedback->message_add_fmt(0, 0, "%" PRId64, (int64_t)(1));
    sim_feedback->zero_messages();
    sim_feedback->show_real_time_taken();

    sim_feedback->thing_play_sample(nullptr, 0, 0, 0, 0, 0, 0, 0);
    sim_feedback->stop_thing_playing_sample(nullptr, 0);
    CHECK(sim_feedback->create_ambient_sound(nullptr, 0, 0) == nullptr);
    sim_feedback->play_sound_if_close_to_receiver(nullptr, 0);
    sim_feedback->play_thing_walking(nullptr);

    CHECK_FALSE(sim_feedback->is_best_roomspace_key_pressed());
    CHECK_FALSE(sim_feedback->is_square_roomspace_key_pressed());
    CHECK_FALSE(sim_feedback->is_roomspace_incsize_key_pressed());
    CHECK_FALSE(sim_feedback->is_roomspace_decsize_key_pressed());
    CHECK_FALSE(sim_feedback->is_sell_trap_on_subtile_key_pressed());

    sim_feedback->set_room_type_highlighted(0);

    sim_feedback->set_visible_event_idx(0);
    sim_feedback->clear_all_event_button_states();
    sim_feedback->clear_event_button_state(0);
    sim_feedback->mark_event_button_read(0);

    CHECK_FALSE(sim_feedback->is_battle_creature_over_active());

    sim_feedback->hide_map_volume_box();
    sim_feedback->reset_box_lag_compensation();

    CHECK(sim_feedback->tag_cursor_blocks_dig(nullptr, 0, nullptr, nullptr, 0, 0, 0) == 0);
    CHECK_FALSE(sim_feedback->tag_cursor_blocks_place_door(0, 0, 0));
    CHECK_FALSE(sim_feedback->tag_cursor_blocks_place_room(0, 0, 0, 0));
    CHECK_FALSE(sim_feedback->tag_cursor_blocks_sell_area(0, 0, 0, 0));

    sim_feedback->set_engine_view(nullptr, 0);
    sim_feedback->setup_engine_window(0, 0, 0, 0);

    CHECK(sim_feedback->light_create_light(nullptr) == 0);
    sim_feedback->light_init_dungeon_heart(0, 0, 0);
    sim_feedback->light_delete_light(0);
    sim_feedback->light_turn_light_off(0);
    sim_feedback->light_turn_light_on(0);
    CHECK(sim_feedback->light_get_light_intensity(0) == 0);
    sim_feedback->light_set_light_intensity(0, 0);
    sim_feedback->light_signal_update_in_area(0, 0, 0, 0);
    sim_feedback->light_set_light_never_cache(0);
    CHECK(sim_feedback->light_is_light_allocated(0) == 0);
    sim_feedback->light_set_light_position(0, nullptr);
    CHECK(sim_feedback->light_get_light_radius(0) == 0);
    sim_feedback->light_set_light_radius(0, 0);
    sim_feedback->light_initialise();
    CHECK(sim_feedback->light_count_lights() == 0);
    CHECK_FALSE(sim_feedback->light_create_light_adv(nullptr));

    sim_feedback->process_dungeon_destroy(nullptr);
    sim_feedback->initialise_devastate_dungeon_from_heart(0);

    CHECK_FALSE(sim_feedback->load_texture_map_file(0, 0, 0));

    CHECK(sim_feedback->get_event_button_info(0) == nullptr);

    sim_feedback->frontstats_initialise();

    CHECK(sim_feedback->GetMouseX() == 0);
    CHECK(sim_feedback->GetMouseY() == 0);
    CHECK(sim_feedback->is_mouse_pressed_lrbutton() == 0);
    CHECK(sim_feedback->is_key_pressed(0, 0) == 0);
    CHECK_FALSE(sim_feedback->mouse_is_over_panel_map(0, 0));
    CHECK_FALSE(sim_feedback->is_left_button_held());
    sim_feedback->PaletteSetUserPalette(0, nullptr);
    sim_feedback->PaletteApplyPainToPlayer(nullptr, 0);
    CHECK(sim_feedback->toggle_status_menu(0) == 0);
    sim_feedback->turn_off_roaming_menus();
    sim_feedback->initialise_tab_tags_and_menu(0);
    sim_feedback->init_gui();
    sim_feedback->set_gui_visible(true);
    sim_feedback->update_player_objectives(0);
    sim_feedback->create_message_box(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    sim_feedback->turn_on_menu(0);
    sim_feedback->turn_off_menu(0);
    sim_feedback->turn_off_query_menus();
    sim_feedback->turn_off_all_menus();
    CHECK(sim_feedback->turn_off_all_window_menus() == 0);
    sim_feedback->turn_on_main_panel_menu();
    sim_feedback->turn_off_all_panel_menus();
    sim_feedback->turn_off_event_box_if_necessary(0, 0);
    sim_feedback->refresh_active_button_sprites_for_player(0);
    CHECK(sim_feedback->find_next_room_of_type(0, 0) == 0);

    sim_feedback->sync_local_camera(nullptr);
    sim_feedback->set_local_camera_destination(nullptr);
    // get_local_camera echoes its own cam argument back, not a fresh nullptr.
    struct Camera *cam = reinterpret_cast<struct Camera *>(0x1234);
    CHECK(sim_feedback->get_local_camera(cam) == cam);
    CHECK(sim_feedback->get_camera_zoom(nullptr) == 0);
    sim_feedback->set_camera_zoom(nullptr, 0);
    sim_feedback->view_zoom_camera_in(nullptr, 0, 0);
    sim_feedback->view_zoom_camera_out(nullptr, 0, 0);
    sim_feedback->view_set_camera_move_to_position(nullptr, 0, 0, nullptr, nullptr);
    CHECK_FALSE(sim_feedback->view_move_camera_to_position(nullptr, 0, 0, 0, 0));
    sim_feedback->init_player_cameras(nullptr);
    CHECK_FALSE(sim_feedback->any_player_close_enough_to_see(nullptr));
    CHECK(sim_feedback->lightning_is_close_to_player(nullptr, nullptr) == 0);

    CHECK_FALSE(sim_feedback->packet_crtr_control_pressed(nullptr));
    CHECK_FALSE(sim_feedback->output_message_far_from_thing(nullptr, 0, 0));
    CHECK_FALSE(sim_feedback->get_packet_load_enable());
    CHECK(sim_feedback->get_local_plyr_idx() == 0);
    CHECK(sim_feedback->get_input_lag_turns() == 0);
    sim_feedback->set_human_players_count(0);
    CHECK(sim_feedback->get_isometric_view_zoom_level() == 0);
    CHECK(sim_feedback->get_frontview_zoom_level() == 0);
    CHECK_FALSE(sim_feedback->get_player_exists_flag(0));
    CHECK_FALSE(sim_feedback->get_player_comp_flag(0));
    sim_feedback->increment_human_players_count();
    CHECK(sim_feedback->get_loaded_level_number() == 0);
    CHECK(sim_feedback->get_selected_level_number() == 0);
    CHECK(sim_feedback->get_level_number() == 0);
    CHECK(sim_feedback->get_play_gameturn() == 0);
    sim_feedback->update_time();
    struct GameTime gt;
    sim_feedback->get_game_time(&gt, 0, 0);
    CHECK(gt.Hours == 0);
    CHECK(sim_feedback->get_zoom_key_room_order(0) == 0);
    CHECK(sim_feedback->get_history_packet(0, 0) == nullptr);

    sim_feedback->setup_eye_lens(0);
    CHECK_FALSE(sim_feedback->lens_is_ready());
    CHECK(sim_feedback->lens_get_render_target() == nullptr);
    CHECK(sim_feedback->lens_get_render_target_width() == 0);
    CHECK(sim_feedback->lens_get_render_target_height() == 0);
    sim_feedback->draw_lens_effect(nullptr, 0, nullptr, 0, 0, 0, 0, 0);
    CHECK(sim_feedback->get_td_animation_sprite(0) == 0);
    sim_feedback->process_keeper_sprite(0, 0, 0, 0, 0, 0);
    sim_feedback->engine(nullptr, nullptr);
    CHECK_FALSE(sim_feedback->add_transfered_creature(0, 0, 0, nullptr));
    sim_feedback->clear_transfered_creatures();
    sim_feedback->reset_ambient_sound_thing_idx();
    CHECK(sim_feedback->get_lens_mode() == 0);
    sim_feedback->hide_tooltip();
    CHECK_FALSE(sim_feedback->timer_enabled());
    sim_feedback->set_timer_turns(0);
    CHECK_FALSE(sim_feedback->get_transferred_creature(0, 0, nullptr, nullptr, nullptr, 0));
    CHECK_FALSE(sim_feedback->activate_bonus_level_for_singleplayer(nullptr, 0));
}
