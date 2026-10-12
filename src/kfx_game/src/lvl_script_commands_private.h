/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lvl_script_commands_private.h
 *     What the level script command files share: lvl_script_commands.c (the command tables) and the
 *     lvl_script_cmd_*.c files with the commands of one family each (refactor pass 5, S09). Not for the rest
 *     of the game: lvl_script_commands.h is the library's.
 * @author   KeeperFX Team
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_LVLSCRIPTCOMMANDS_PRIVATE_H
#define DK_LVLSCRIPTCOMMANDS_PRIVATE_H

#include "lvl_script_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

/* BEGIN generated */
void add_to_party_check(const struct ScriptLine *scline);
void delete_from_party_check(const struct ScriptLine *scline);
void create_party_check(const struct ScriptLine *scline);
void add_party_to_level_check(const struct ScriptLine *scline);
void add_tunneller_to_level_check(const struct ScriptLine *scline);
void add_tunneller_party_to_level_check(const struct ScriptLine *scline);
extern const struct NamedCommand is_free_desc[];
void special_transfer_creature_process(struct ScriptContext* context);
void special_transfer_creature_check(const struct ScriptLine* scline);
void use_spell_on_creature_check(const struct ScriptLine* scline);
void use_spell_on_creature_process(struct ScriptContext* context);
void use_spell_on_players_creatures_check(const struct ScriptLine *scline);
void use_spell_on_players_creatures_process(struct ScriptContext *context);
void use_power_on_players_creatures_check(const struct ScriptLine* scline);
void use_power_on_players_creatures_process(struct ScriptContext* context);
void magic_available_check(const struct ScriptLine *scline);
void magic_available_process(struct ScriptContext *context);
TbBool legacy_value_first_player(const struct ScriptContext *context);
void use_power_on_creature_check(const struct ScriptLine *scline);
void use_power_at_pos_check(const struct ScriptLine *scline);
void use_power_at_location_check(const struct ScriptLine *scline);
void use_power_check(const struct ScriptLine *scline);
void use_special_increase_level_check(const struct ScriptLine *scline);
void use_special_multiply_creatures_check(const struct ScriptLine *scline);
void make_safe_check(const struct ScriptLine *scline);
void locate_hidden_world_check(const struct ScriptLine *scline);
void use_power_on_creature_process(struct ScriptContext *context);
void use_power_at_pos_process(struct ScriptContext *context);
void use_power_at_location_process(struct ScriptContext *context);
void use_power_process(struct ScriptContext *context);
void use_special_increase_level_process(struct ScriptContext *context);
void use_special_multiply_creatures_process(struct ScriptContext *context);
void make_safe_process(struct ScriptContext *context);
void locate_hidden_world_process(struct ScriptContext *context);
void make_unsafe_check(const struct ScriptLine *scline);
void make_unsafe_process(struct ScriptContext *context);
extern const struct NamedCommand script_operator_desc[];
TbBool script_parse_get_varib(const char *varib_name, int64_t *varib_id, int64_t *varib_type, int64_t lvl_file_version);
void script_transfer_creature_check(const struct ScriptLine* scline);
void script_transfer_creature_process(struct ScriptContext* context);
void change_creatures_annoyance_check(const struct ScriptLine* scline);
void change_creatures_annoyance_process(struct ScriptContext* context);
void move_creature_check(const struct ScriptLine* scline);
void count_creatures_at_action_point_check(const struct ScriptLine* scline);
void move_creature_process(struct ScriptContext* context);
void count_creatures_at_action_point_process(struct ScriptContext* context);
void level_up_players_creatures_check(const struct ScriptLine* scline);
void level_up_players_creatures_process(struct ScriptContext* context);
void swap_creature_check(const struct ScriptLine* scline);
void swap_creature_process(struct ScriptContext* context);
void set_generate_speed_check(const struct ScriptLine* scline);
void set_generate_speed_process(struct ScriptContext* context);
void creature_available_check(const struct ScriptLine *scline);
void creature_available_process(struct ScriptContext *context);
TbBool legacy_value_first_player(const struct ScriptContext *context);
void add_creature_to_pool_check(const struct ScriptLine *scline);
void add_creature_to_pool_process(struct ScriptContext *context);
void kill_creature_check(const struct ScriptLine *scline);
void level_up_creature_check(const struct ScriptLine *scline);
void kill_creature_process(struct ScriptContext *context);
void level_up_creature_process(struct ScriptContext *context);
void max_creatures_check(const struct ScriptLine *scline);
void dead_creatures_return_to_pool_check(const struct ScriptLine *scline);
void set_creature_tendencies_check(const struct ScriptLine *scline);
void change_creature_owner_check(const struct ScriptLine *scline);
void creature_entrance_level_check(const struct ScriptLine *scline);
void max_creatures_process(struct ScriptContext *context);
void dead_creatures_return_to_pool_process(struct ScriptContext *context);
void set_creature_tendencies_process(struct ScriptContext *context);
void change_creature_owner_process(struct ScriptContext *context);
void creature_entrance_level_process(struct ScriptContext *context);
void add_creature_to_level_check(const struct ScriptLine *scline);
extern const struct NamedCommand fill_desc[];
extern const struct NamedCommand locked_desc[];
void tag_map_rect_check(const struct ScriptLine* scline);
void tag_map_rect_process(struct ScriptContext* context);
void untag_map_rect_process(struct ScriptContext* context);
void conceal_map_rect_check(const struct ScriptLine *scline);
void conceal_map_rect_process(struct ScriptContext *context);
void create_effect_at_pos_process(struct ScriptContext* context);
void create_effect_process(struct ScriptContext *context);
void set_door_check(const struct ScriptLine* scline);
void set_door_process(struct ScriptContext* context);
void place_door_check(const struct ScriptLine* scline);
void place_door_process(struct ScriptContext* context);
void place_trap_check(const struct ScriptLine* scline);
void place_trap_process(struct ScriptContext* context);
void create_effects_line_check(const struct ScriptLine *scline);
void create_effects_line_process(struct ScriptContext *context);
void create_effect_check(const struct ScriptLine *scline);
void create_effect_at_pos_check(const struct ScriptLine *scline);
void change_slab_owner_check(const struct ScriptLine *scline);
void change_slab_owner_process(struct ScriptContext *context);
void change_slab_type_check(const struct ScriptLine *scline);
void change_slab_type_process(struct ScriptContext *context);
void reveal_map_location_check(const struct ScriptLine *scline);
void reveal_map_location_process(struct ScriptContext *context);
void player_zoom_to_check(const struct ScriptLine *scline);
void player_zoom_to_process(struct ScriptContext *context);
void hide_hero_gate_check(const struct ScriptLine* scline);
void hide_hero_gate_process(struct ScriptContext* context);
void add_effectgen_to_level_check(const struct ScriptLine* scline);
void add_effectgen_to_level_process(struct ScriptContext* context);
void reset_or_trigger_action_point_check(const struct ScriptLine* scline);
void reset_action_point_process(struct ScriptContext* context);
void add_object_to_level_at_pos_check(const struct ScriptLine* scline);
void add_object_to_level_check(const struct ScriptLine* scline);
void add_object_to_level_process(struct ScriptContext* context);
void add_object_to_level_at_pos_process(struct ScriptContext* context);
void trigger_action_point_process(struct ScriptContext* context);
void reveal_map_rect_check(const struct ScriptLine *scline);
void computer_dig_to_location_check(const struct ScriptLine *scline);
void reveal_map_rect_process(struct ScriptContext *context);
void computer_dig_to_location_process(struct ScriptContext *context);
extern const struct NamedCommand timer_desc[];
TbBool script_parse_get_varib(const char *varib_name, int64_t *varib_id, int64_t *varib_type, int64_t lvl_file_version);
void display_objective_check(const struct ScriptLine *scline);
void display_objective_process(struct ScriptContext *context);
void display_player_objective_check(const struct ScriptLine* scline);
void quick_objective_check(const struct ScriptLine* scline);
void quick_objective_process(struct ScriptContext* context);
void quick_player_objective_check(const struct ScriptLine* scline);
void quick_information_check(const struct ScriptLine* scline);
void quick_information_process(struct ScriptContext* context);
void quick_player_information_check(const struct ScriptLine* scline);
void display_information_check(const struct ScriptLine* scline);
void display_information_process(struct ScriptContext* context);
void display_player_information_check(const struct ScriptLine* scline);
void heart_lost_quick_objective_check(const struct ScriptLine *scline);
void heart_lost_quick_objective_process(struct ScriptContext *context);
void heart_lost_objective_check(const struct ScriptLine *scline);
void heart_lost_objective_process(struct ScriptContext *context);
void display_timer_check(const struct ScriptLine *scline);
void display_timer_process(struct ScriptContext *context);
void display_variable_check(const struct ScriptLine *scline);
void display_variable_process(struct ScriptContext *context);
void display_variable_with_label_check(const struct ScriptLine *scline);
void display_variable_with_label_process(struct ScriptContext *context);
void display_countdown_check(const struct ScriptLine *scline);
void cmd_no_param_check(const struct ScriptLine *scline);
void hide_timer_process(struct ScriptContext *context);
void hide_variable_check(const struct ScriptLine *scline);
void hide_variable_process(struct ScriptContext *context);
void set_box_tooltip_check(const struct ScriptLine* scline);
void set_box_tooltip_process(struct ScriptContext* context);
void set_box_tooltip_id_check(const struct ScriptLine *scline);
void set_box_tooltip_id_process(struct ScriptContext* context);
void set_music_check(const struct ScriptLine *scline);
void set_music_process(struct ScriptContext *context);
void play_message_check(const struct ScriptLine *scline);
void play_message_process(struct ScriptContext *context);
void quick_message_check(const struct ScriptLine* scline);
void quick_message_process(struct ScriptContext* context);
void display_message_check(const struct ScriptLine* scline);
void display_message_process(struct ScriptContext* context);
void clear_message_check(const struct ScriptLine* scline);
void clear_message_process(struct ScriptContext* context);
void tutorial_flash_button_check(const struct ScriptLine* scline);
void tutorial_flash_button_process(struct ScriptContext* context);
void print_check(const struct ScriptLine *scline);
void message_check(const struct ScriptLine *scline);
extern const struct NamedCommand timer_desc[];
extern const struct NamedCommand flag_desc[];
extern const struct NamedCommand campaign_flag_desc[];
TbBool parse_creature_bracket_varib(const char *varib_name, int64_t *varib_id, int64_t *varib_type);
TbBool script_parse_get_varib(const char *varib_name, int64_t *varib_id, int64_t *varib_type, int64_t lvl_file_version);
void set_heart_health_check(const struct ScriptLine *scline);
void set_heart_health_process(struct ScriptContext *context);
void add_heart_health_check(const struct ScriptLine *scline);
void add_heart_health_process(struct ScriptContext *context);
void lock_possession_check(const struct ScriptLine* scline);
void lock_possession_process(struct ScriptContext* context);
void add_to_timer_check(const struct ScriptLine *scline);
void add_to_timer_process(struct ScriptContext *context);
void add_bonus_time_check(const struct ScriptLine *scline);
void add_bonus_time_process(struct ScriptContext *context);
void set_player_colour_check(const struct ScriptLine *scline);
void set_player_colour_process(struct ScriptContext *context);
void set_player_modifier_check(const struct ScriptLine* scline);
void set_player_modifier_process(struct ScriptContext* context);
void add_to_player_modifier_check(const struct ScriptLine* scline);
void add_to_player_modifier_process(struct ScriptContext* context);
void computer_player_check(const struct ScriptLine* scline);
void computer_player_process(struct ScriptContext* context);
void set_computer_globals_check(const struct ScriptLine* scline);
void set_computer_globals_process(struct ScriptContext* context);
void set_computer_process_check(const struct ScriptLine* scline);
void set_computer_process_process(struct ScriptContext* context);
void set_computer_checks_process(struct ScriptContext* context);
void set_computer_event_check(const struct ScriptLine* scline);
void set_computer_event_process(struct ScriptContext* context);
void set_next_level_check(const struct ScriptLine* scline);
void set_next_level_process(struct ScriptContext* context);
void set_level_ensign_check(const struct ScriptLine* scline);
void set_level_ensign_process(struct ScriptContext* context);
void show_bonus_level_check(const struct ScriptLine* scline);
void show_bonus_level_process(struct ScriptContext* context);
void hide_bonus_level_process(struct ScriptContext* context);
void room_available_check(const struct ScriptLine *scline);
void room_available_process(struct ScriptContext *context);
void trap_available_check(const struct ScriptLine *scline);
void trap_available_process(struct ScriptContext *context);
void door_available_check(const struct ScriptLine *scline);
void door_available_process(struct ScriptContext *context);
void research_check(const struct ScriptLine *scline);
void research_process(struct ScriptContext *context);
void research_order_check(const struct ScriptLine *scline);
void research_order_process(struct ScriptContext *context);
TbBool legacy_value_first_player(const struct ScriptContext *context);
void set_timer_check(const struct ScriptLine *scline);
void set_timer_process(struct ScriptContext *context);
void set_flag_check(const struct ScriptLine *scline);
void set_flag_process(struct ScriptContext *context);
void add_to_flag_check(const struct ScriptLine *scline);
void add_to_flag_process(struct ScriptContext *context);
void randomise_flag_check(const struct ScriptLine *scline);
void randomise_flag_process(struct ScriptContext *context);
void set_campaign_flag_check(const struct ScriptLine *scline);
void set_campaign_flag_process(struct ScriptContext *context);
void add_to_campaign_flag_check(const struct ScriptLine *scline);
void add_to_campaign_flag_process(struct ScriptContext *context);
void export_variable_check(const struct ScriptLine *scline);
void export_variable_process(struct ScriptContext *context);
void compute_flag_check(const struct ScriptLine *scline);
void compute_flag_process(struct ScriptContext *context);
void bonus_level_time_check(const struct ScriptLine *scline);
void add_gold_to_player_check(const struct ScriptLine *scline);
void ally_players_check(const struct ScriptLine *scline);
void ally_players_process(struct ScriptContext *context);
void bonus_level_time_process(struct ScriptContext *context);
void add_gold_to_player_process(struct ScriptContext *context);
void start_money_check(const struct ScriptLine *scline);
void win_game_check(const struct ScriptLine *scline);
void lose_game_check(const struct ScriptLine *scline);
void run_after_victory_check(const struct ScriptLine *scline);
extern const struct NamedCommand flag_desc[];
extern const struct NamedCommand texture_pack_desc[];
void set_trap_configuration_check(const struct ScriptLine* scline);
void set_room_configuration_check(const struct ScriptLine* scline);
void set_hand_rule_check(const struct ScriptLine* scline);
void copy_creature_type_check(const struct ScriptLine* scline);
void new_creature_type_check(const struct ScriptLine* scline);
void new_room_type_check(const struct ScriptLine* scline);
void new_object_type_check(const struct ScriptLine* scline);
void new_trap_type_check(const struct ScriptLine* scline);
void set_trap_configuration_process(struct ScriptContext *context);
void set_room_configuration_process(struct ScriptContext *context);
void set_hand_rule_process(struct ScriptContext* context);
void set_door_configuration_check(const struct ScriptLine* scline);
void set_door_configuration_process(struct ScriptContext *context);
void set_object_configuration_check(const struct ScriptLine *scline);
void set_creature_configuration_check(const struct ScriptLine* scline);
void set_creature_configuration_process(struct ScriptContext* context);
void set_object_configuration_process(struct ScriptContext *context);
void null_process(struct ScriptContext *context);
void set_sacrifice_recipe_check(const struct ScriptLine *scline);
void remove_sacrifice_recipe_check(const struct ScriptLine *scline);
void set_sacrifice_recipe_process(struct ScriptContext *context);
void set_creature_instance_check(const struct ScriptLine *scline);
void set_creature_instance_process(struct ScriptContext *context);
void set_texture_check(const struct ScriptLine *scline);
void set_texture_process(struct ScriptContext *context);
void set_power_hand_check(const struct ScriptLine *scline);
void set_power_hand_process(struct ScriptContext *context);
void set_effectgen_configuration_check(const struct ScriptLine* scline);
void set_effectgen_configuration_process(struct ScriptContext* context);
void set_power_configuration_check(const struct ScriptLine *scline);
void set_power_configuration_process(struct ScriptContext *context);
void set_game_rule_check(const struct ScriptLine* scline);
void set_game_rule_process(struct ScriptContext* context);
void set_increase_on_experience_check(const struct ScriptLine* scline);
void set_increase_on_experience_process(struct ScriptContext* context);
void set_creature_max_level_check(const struct ScriptLine* scline);
void set_creature_max_level_process(struct ScriptContext* context);
void set_creature_health_check(const struct ScriptLine *scline);
void set_creature_health_process(struct ScriptContext *context);
void set_creature_strength_check(const struct ScriptLine *scline);
void set_creature_strength_process(struct ScriptContext *context);
void set_creature_armour_check(const struct ScriptLine *scline);
void set_creature_armour_process(struct ScriptContext *context);
void set_creature_fear_wounded_check(const struct ScriptLine *scline);
void set_creature_fear_wounded_process(struct ScriptContext *context);
void set_creature_fear_stronger_check(const struct ScriptLine *scline);
void set_creature_fear_stronger_process(struct ScriptContext *context);
void set_creature_fearsome_factor_check(const struct ScriptLine *scline);
void set_creature_fearsome_factor_process(struct ScriptContext *context);
void set_creature_property_check(const struct ScriptLine *scline);
void set_creature_property_process(struct ScriptContext *context);
/* END generated */

#ifdef __cplusplus
}
#endif
#endif
