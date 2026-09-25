// kfx_config: config.c's ConfigReloadCallbacks -- a second *Callbacks-
// registration table this library's original exhaustive sweep (and the
// sim_feedback.c follow-up) both missed, likely because it's declared
// and defaulted in config.c itself rather than its own dedicated
// config_reload_callbacks.c file the way net_callbacks.c/game_callbacks.c/
// etc. are. Same shape as every other one: every noop_* stub is
// `static`, only reachable through the default table's function-pointer
// fields, every body a one-liner ignoring its pointer args. 91 fields,
// the largest *Callbacks table in this library.
#include <catch2/catch_test_macros.hpp>

#include "config.h"

#include <string>

TEST_CASE("the default config_reload_callbacks table's every stub is a safe no-op returning its documented default", "[kfx_config][config_reload_callbacks]") {
    REQUIRE(config_reload_callbacks != nullptr);

    config_reload_callbacks->update_room_tab_to_config();
    config_reload_callbacks->update_trap_tab_to_config();
    config_reload_callbacks->update_powers_tab_to_config();
    config_reload_callbacks->update_creatr_model_activities_list(false);
    config_reload_callbacks->update_all_door_stats();
    config_reload_callbacks->update_all_trap_draws_of_model(0);
    CHECK_FALSE(config_reload_callbacks->add_research_to_all_players(0, 0, 0));
    CHECK_FALSE(config_reload_callbacks->clear_research_for_all_players());
    config_reload_callbacks->panel_map_update(0, 0, 0, 0);
    config_reload_callbacks->update_panel_color_player_color(0, 0);
    config_reload_callbacks->setup_panel_colors();
    config_reload_callbacks->reset_panel_map_background_cache();
    config_reload_callbacks->clear_subtiles_lightness();
    CHECK(config_reload_callbacks->get_lua_function_idx(nullptr, nullptr) == -1);
    CHECK(config_reload_callbacks->get_map_subtiles_x() == 0);
    CHECK(config_reload_callbacks->get_map_subtiles_y() == 0);
    CHECK_FALSE(config_reload_callbacks->thing_is_workshop_crate(nullptr));
    CHECK(config_reload_callbacks->get_wealth_size_of_gold_hoard_model(0) == 0);
    config_reload_callbacks->set_call_to_arms_graphics(0, 0, 0, 0);
    config_reload_callbacks->set_screen_vidmode(0);
    CHECK(config_reload_callbacks->get_screen_vidmode() == 0);
    config_reload_callbacks->set_base_mouse_sensitivity(0);
    CHECK(config_reload_callbacks->get_base_mouse_sensitivity() == 0);
    CHECK_FALSE(config_reload_callbacks->thing_is_creature_digger(nullptr));
    CHECK_FALSE(config_reload_callbacks->creature_is_for_dungeon_diggers_list(nullptr));
    CHECK(config_reload_callbacks->get_thing_model(nullptr) == 0);
    CHECK(config_reload_callbacks->get_thing_class_id(nullptr) == 0);
    CHECK(config_reload_callbacks->get_thing_owner(nullptr) == 0);
    CHECK(config_reload_callbacks->get_thing_creation_turn(nullptr) == 0);
    CHECK(config_reload_callbacks->get_thing_index(nullptr) == 0);
    CHECK(config_reload_callbacks->get_creature_blood_type(nullptr) == 0);
    CHECK(config_reload_callbacks->get_creature_name_buffer(nullptr) == nullptr);
    CHECK(config_reload_callbacks->get_slabmap_for_subtile(0, 0) == nullptr);
    CHECK(config_reload_callbacks->slabmap_owner(nullptr) == 0);
    CHECK_FALSE(config_reload_callbacks->thing_create_thing(nullptr));
    CHECK_FALSE(config_reload_callbacks->thing_create_thing_adv(nullptr));
    config_reload_callbacks->set_screenshot_format(0);
    CHECK(config_reload_callbacks->get_screenshot_format() == 0);
    config_reload_callbacks->set_vid_smooth(false);
    config_reload_callbacks->set_hand_scale(0.0);
    CHECK(config_reload_callbacks->get_hand_scale() == 1.0);
    CHECK(config_reload_callbacks->get_room_kind_thing_is_on(nullptr) == 0);
    CHECK(config_reload_callbacks->get_player_color_idx(0) == 0);
    CHECK(config_reload_callbacks->get_slabset_array() == nullptr);
    CHECK(config_reload_callbacks->get_slabset_num_ptr() == nullptr);
    CHECK(config_reload_callbacks->get_slabobjs_array() == nullptr);
    CHECK(config_reload_callbacks->get_slabobjs_idx_array() == nullptr);
    CHECK(config_reload_callbacks->get_slabobjs_num_ptr() == nullptr);
    config_reload_callbacks->set_block_health(0, 0);
    CHECK(config_reload_callbacks->get_player_special_digger(0) == 0);
    config_reload_callbacks->set_player_special_digger(0, 0);
    CHECK(config_reload_callbacks->get_computer_player_f(0, "test") == nullptr);
    CHECK_FALSE(config_reload_callbacks->reactivate_build_process(nullptr, 0));
    CHECK(config_reload_callbacks->reinitialise_rooms_of_kind(0) == 0);
    CHECK(config_reload_callbacks->recalculate_effeciency_for_rooms_of_kind(0) == 0);
    // A real quirk: this one's default is TRUE ("invalid"/blocked),
    // the opposite fail-safe direction of most other TbBool defaults in
    // this table -- makes sense for a "block invalid" predicate, but
    // worth calling out since it breaks the "everything defaults false"
    // pattern the rest of this test relies on.
    CHECK(config_reload_callbacks->slabmap_block_invalid(nullptr));
    CHECK(config_reload_callbacks->slabmap_kind(nullptr) == 0);
    CHECK_FALSE(config_reload_callbacks->find_and_load_lif_files());
    CHECK_FALSE(config_reload_callbacks->find_and_load_lof_files());
    CHECK(config_reload_callbacks->get_computer_process_func_type() == nullptr);
    CHECK(config_reload_callbacks->get_computer_check_func_type() == nullptr);
    CHECK(config_reload_callbacks->get_computer_event_func_type() == nullptr);
    CHECK(config_reload_callbacks->get_computer_event_test_func_type() == nullptr);
    CHECK(config_reload_callbacks->get_my_player_number() == 0);
    CHECK_FALSE(config_reload_callbacks->player_is_roaming(0));
    CHECK_FALSE(config_reload_callbacks->slab_is_area_inner_fill(0, 0));
    CHECK(std::string(config_reload_callbacks->thing_class_and_model_name(0, 0)).empty());
    CHECK(config_reload_callbacks->get_creature_instances_func_type() == nullptr);
    CHECK(config_reload_callbacks->get_creature_instances_validate_func_type() == nullptr);
    CHECK(config_reload_callbacks->get_creature_instances_search_targets_func_type() == nullptr);
    CHECK(config_reload_callbacks->get_creature_job_player_assign_func_type() == nullptr);
    CHECK(config_reload_callbacks->get_creature_job_player_check_func_type() == nullptr);
    CHECK(config_reload_callbacks->get_creature_job_coords_check_func_type() == nullptr);
    CHECK(config_reload_callbacks->get_creature_job_coords_assign_func_type() == nullptr);
    CHECK_FALSE(config_reload_callbacks->remove_creature_lair(nullptr));
    CHECK_FALSE(config_reload_callbacks->update_creature_health_to_max(nullptr));
    CHECK_FALSE(config_reload_callbacks->update_relative_creature_health(nullptr));
    CHECK(config_reload_callbacks->do_to_players_all_creatures_of_model(0, 0, nullptr) == 0);
    CHECK(config_reload_callbacks->do_to_all_things_of_class_and_model(0, 0, nullptr) == 0);
    config_reload_callbacks->recalculate_all_creature_digger_lists();
    CHECK_FALSE(config_reload_callbacks->update_speed_of_player_creatures_of_model(0, 0));
    CHECK_FALSE(config_reload_callbacks->creature_increase_available_instances(nullptr));
    CHECK_FALSE(config_reload_callbacks->process_job_stress_and_going_postal(nullptr));
    CHECK(config_reload_callbacks->get_process_func_commands() == nullptr);
    CHECK(config_reload_callbacks->get_cleanup_func_commands() == nullptr);
    CHECK(config_reload_callbacks->get_move_from_slab_func_commands() == nullptr);
    CHECK(config_reload_callbacks->get_move_check_func_commands() == nullptr);
    CHECK_FALSE(config_reload_callbacks->player_has_heart(0));
    // Another real quirk, same "blocked/invalid by default" shape as
    // slabmap_block_invalid above: thing_is_invalid's default is TRUE.
    CHECK(config_reload_callbacks->thing_is_invalid(nullptr) != 0);
    CHECK(config_reload_callbacks->setup_excess_creatures_to_leave_or_die(0) == 0);
    CHECK(config_reload_callbacks->get_level_strings() == nullptr);
    CHECK_FALSE(config_reload_callbacks->set_door_buildable_and_add_to_amount(0, 0, 0, 0));
    CHECK_FALSE(config_reload_callbacks->set_trap_buildable_and_add_to_amount(0, 0, 0, 0));
    config_reload_callbacks->set_speech_queue_limit(0);
    CHECK(config_reload_callbacks->script_strdup(nullptr) == -1);
    CHECK(config_reload_callbacks->script_strval(0) == nullptr);
    config_reload_callbacks->reset_campaign_progress();
}
