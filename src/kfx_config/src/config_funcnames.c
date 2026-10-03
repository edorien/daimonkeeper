/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_funcnames.c
 *     Names of the computer-player and creature function tables. See config_funcnames.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "config_funcnames.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// Indexes into computer_process_func_list[] (kfx_sim/src/player_compprocs.c).
const struct NamedCommand computer_process_func_type[] = {
  {"check_build_all_rooms",   1,},
  {"setup_any_room_continue", 2,},
  {"check_any_room",          3,},
  {"setup_any_room",          4,},
  {"check_dig_to_entrance",   5,},
  {"setup_dig_to_entrance",   6,},
  {"check_dig_to_gold",       7,},
  {"setup_dig_to_gold",       8,},
  {"check_sight_of_evil",     9,},
  {"setup_sight_of_evil",    10,},
  {"process_sight_of_evil",  11,},
  {"check_attack1",          12,},
  {"setup_attack1",          13,},
  {"completed_attack1",      14,},
  {"check_safe_attack",      15,},
  {"process_task",           16,},
  {"completed_build_a_room", 17,},
  {"paused_task",            18,},
  {"completed_task",         19,},
  {"none",                   20,},
  {NULL,                      0,},
};
_Static_assert(sizeof(computer_process_func_type) / sizeof(computer_process_func_type[0]) == COMPUTER_PROCESS_FUNC_TYPE_COUNT + 1, "computer_process_func_type: update COMPUTER_PROCESS_FUNC_TYPE_COUNT");

// Indexes into computer_check_func_list[] (kfx_sim/src/player_compchecks.c).
const struct NamedCommand computer_check_func_type[] = {
  {"checks_hates",            1,},
  {"check_move_to_best_room", 2,},
  {"check_move_to_room",      3,},
  {"check_no_imps",           4,},
  {"check_for_pretty",        5,},
  {"check_for_quick_attack",  6,},
  {"check_for_accelerate",    7,},
  {"check_slap_imps",         8,},
  {"check_enemy_entrances",   9,},
  {"check_for_place_door",   10,},
  {"check_neutral_places",   11,},
  {"check_for_place_trap",   12,},
  {"check_for_expand_room",  13,},
  {"check_for_money",        14,},
  {"check_prison_tendency",  15,},
  {"check_for_flight",       16,},
  {"check_for_vision",       17,},
  {"check_sacrifice_diggers",   18,},
  {"none",                   19,},
  {NULL,                      0,},
};
_Static_assert(sizeof(computer_check_func_type) / sizeof(computer_check_func_type[0]) == COMPUTER_CHECK_FUNC_TYPE_COUNT + 1, "computer_check_func_type: update COMPUTER_CHECK_FUNC_TYPE_COUNT");

// Indexes into computer_event_test_func_list[] (kfx_sim/src/player_compevents.c).
const struct NamedCommand computer_event_test_func_type[] = {
  {"event_battle_test",       1,},
  {"event_check_fighters",    2,},
  {"event_attack_magic_foe",  3,},
  {"event_check_rooms_full",  4,},
  {"event_check_imps_danger", 5,},
  {"event_save_tortured",     6,},
  {"none",                    7,},
  {NULL,                      0,},
};
_Static_assert(sizeof(computer_event_test_func_type) / sizeof(computer_event_test_func_type[0]) == COMPUTER_EVENT_TEST_FUNC_TYPE_COUNT + 1, "computer_event_test_func_type: update COMPUTER_EVENT_TEST_FUNC_TYPE_COUNT");

// Indexes into computer_event_func_list[] (kfx_sim/src/player_compevents.c).
const struct NamedCommand computer_event_func_type[] = {
  {"event_battle",            1,},
  {"event_find_link",         2,},
  {"event_check_payday",      3,},
  {"event_rebuild_room",      4,},
  {"event_handle_prisoner",   5,},
  {"event_attack_door",       6,},
  {"none",                    7,},
  {NULL,                      0,},
};
_Static_assert(sizeof(computer_event_func_type) / sizeof(computer_event_func_type[0]) == COMPUTER_EVENT_FUNC_TYPE_COUNT + 1, "computer_event_func_type: update COMPUTER_EVENT_FUNC_TYPE_COUNT");

// Indexes into creature_instances_func_list[] (kfx_sim/src/creature_instances.c).
const struct NamedCommand creature_instances_func_type[] = {
  {"attack_room_slab",         1},
  {"creature_cast_spell",      2},
  {"creature_fire_shot",       3},
  {"creature_damage_wall",     4},
  {"creature_destroy",         5},
  {"creature_dig",             6},
  {"creature_eat",             7},
  {"creature_fart",            8},
  {"first_person_do_imp_task", 9},
  {"creature_pretty_path",     10},
  {"creature_reinforce",       11},
  {"creature_tortured",        12},
  {"creature_tunnel",          13},
  {"none",                     14},
  {NULL,                       0},
};
_Static_assert(sizeof(creature_instances_func_type) / sizeof(creature_instances_func_type[0]) == CREATURE_INSTANCES_FUNC_TYPE_COUNT + 1, "creature_instances_func_type: update CREATURE_INSTANCES_FUNC_TYPE_COUNT");

// Indexes into creature_instances_validate_func_list[] (kfx_sim/src/creature_instances.c).
const struct NamedCommand creature_instances_validate_func_type[] = {
    {"validate_source_generic",                                 1},
    {"validate_source_even_in_prison",                          2},
    {"validate_target_generic",                                 3},
    {"validate_target_even_in_prison",                          4},
    {"validate_target_benefits_from_missile_defense",           5},
    {"validate_target_benefits_from_defensive",                 6},
    {"validate_target_benefits_from_healing",                   7},
    {"validate_target_benefits_from_higher_altitude",           8},
    {"validate_target_benefits_from_offensive",                 9},
    {"validate_target_benefits_from_wind",                      10},
    {"validate_target_non_idle",                                11},
    {"validate_target_takes_gas_damage",                        12},
    {"validate_target_requires_cleansing",                      13},
    {NULL, 0},
};
_Static_assert(sizeof(creature_instances_validate_func_type) / sizeof(creature_instances_validate_func_type[0]) == CREATURE_INSTANCES_VALIDATE_FUNC_TYPE_COUNT + 1, "creature_instances_validate_func_type: update CREATURE_INSTANCES_VALIDATE_FUNC_TYPE_COUNT");

// Indexes into creature_instances_search_targets_func_list[] (kfx_sim/src/creature_instances.c).
const struct NamedCommand creature_instances_search_targets_func_type[] = {
    {"search_target_generic",        1},
    {"search_target_ranged_heal",    2},
    {NULL,                           0},
};
_Static_assert(sizeof(creature_instances_search_targets_func_type) / sizeof(creature_instances_search_targets_func_type[0]) == CREATURE_INSTANCES_SEARCH_TARGETS_FUNC_TYPE_COUNT + 1, "creature_instances_search_targets_func_type: update CREATURE_INSTANCES_SEARCH_TARGETS_FUNC_TYPE_COUNT");

// Indexes into creature_job_player_check_func_list[] (kfx_sim/src/creature_jobs.c).
const struct NamedCommand creature_job_player_check_func_type[] = {
  {"can_do_job_always",        1},
  {"can_do_training",          2},
  {"can_do_research",          3},
  {"can_do_manufacturing",     4},
  {"can_do_scavenging",        5},
  {"can_freeze_prisoners",     6},
  {"can_join_fight",           7},
  {"can_do_barracking",        8},
  {"none",                     9},
  {NULL,                       0},
};
_Static_assert(sizeof(creature_job_player_check_func_type) / sizeof(creature_job_player_check_func_type[0]) == CREATURE_JOB_PLAYER_CHECK_FUNC_TYPE_COUNT + 1, "creature_job_player_check_func_type: update CREATURE_JOB_PLAYER_CHECK_FUNC_TYPE_COUNT");

// Indexes into creature_job_player_assign_func_list[] (kfx_sim/src/creature_jobs.c).
const struct NamedCommand creature_job_player_assign_func_type[] = {
  {"work_in_room",             1},
  {"in_state_on_room_content", 2},
  {"move_to_event",            3},
  {"in_state_internal",        4},
  {"none",                     5},
  {NULL,                       0},
};
_Static_assert(sizeof(creature_job_player_assign_func_type) / sizeof(creature_job_player_assign_func_type[0]) == CREATURE_JOB_PLAYER_ASSIGN_FUNC_TYPE_COUNT + 1, "creature_job_player_assign_func_type: update CREATURE_JOB_PLAYER_ASSIGN_FUNC_TYPE_COUNT");

// Indexes into creature_job_coords_check_func_list[] (kfx_sim/src/creature_jobs.c).
const struct NamedCommand creature_job_coords_check_func_type[] = {
  {"can_do_job_always",        1},
  {"can_do_research",          2},
  {"can_do_training",          3},
  {"can_do_manufacturing",     4},
  {"can_do_scavenging",        5},
  {"can_place_in_vault",       6},
  {"can_take_salary",          7},
  {"can_take_sleep",           8},
  {"none",                     9},
  {NULL,                       0},
};
_Static_assert(sizeof(creature_job_coords_check_func_type) / sizeof(creature_job_coords_check_func_type[0]) == CREATURE_JOB_COORDS_CHECK_FUNC_TYPE_COUNT + 1, "creature_job_coords_check_func_type: update CREATURE_JOB_COORDS_CHECK_FUNC_TYPE_COUNT");

// Indexes into creature_job_coords_assign_func_list[] (kfx_sim/src/creature_jobs.c).
const struct NamedCommand creature_job_coords_assign_func_type[] = {
  {"work_in_room",             1},
  {"work_in_room_and_cure",    2},
  {"sleep_in_lair",            3},
  {"in_state_internal",        4},
  {"none",                     5},
  {NULL,                       0},
};
_Static_assert(sizeof(creature_job_coords_assign_func_type) / sizeof(creature_job_coords_assign_func_type[0]) == CREATURE_JOB_COORDS_ASSIGN_FUNC_TYPE_COUNT + 1, "creature_job_coords_assign_func_type: update CREATURE_JOB_COORDS_ASSIGN_FUNC_TYPE_COUNT");

// Indexes into process_func_list[] (kfx_sim/src/creature_states.c).
const struct NamedCommand process_func_commands[] = {
    {"NULL",                                         0},
    {"imp_doing_nothing",                            1},
    {"imp_arrives_at_dig_or_mine",                   2},
    {"imp_digs_mines",                               3},
    {"creature_casting_preparation",                 4},
    {"imp_drops_gold",                               5},
    {"imp_last_did_job",                             6},
    {"imp_arrives_at_improve_dungeon",               7},
    {"imp_improves_dungeon",                         8},
    {"creature_picks_up_trap_object",                9},
    {"creature_arms_trap",                          10},
    {"creature_picks_up_crate_for_workshop",        11},
    {"move_to_position",                            12},
    {"creature_drops_crate_in_workshop",            13},
    {"creature_doing_nothing",                      14},
    {"creature_to_garden",                          15},
    {"creature_arrived_at_garden",                  16},
    {"creature_wants_a_home",                       17},
    {"creature_choose_room_for_lair_site",          18},
    {"creature_at_new_lair",                        19},
    {"person_sulk_head_for_lair",                   20},
    {"person_sulk_at_lair",                         21},
    {"creature_going_home_to_sleep",                22},
    {"creature_sleep",                              23},
    {"tunnelling",                                  24},
    {"at_research_room",                            25},
    {"researching",                                 26},
    {"at_training_room",                            27},
    {"training",                                    28},
    {"good_doing_nothing",                          29},
    {"good_returns_to_start",                       30},
    {"good_back_at_start",                          31},
    {"good_drops_gold",                             32},
    {"arrive_at_call_to_arms",                      33},
    {"creature_arrived_at_prison",                  34},
    {"creature_in_prison",                          35},
    {"at_torture_room",                             36},
    {"torturing",                                   37},
    {"at_workshop_room",                            38},
    {"manufacturing",                               39},
    {"at_scavenger_room",                           40},
    {"scavengering",                                41},
    {"creature_dormant",                            42},
    {"creature_in_combat",                          43},
    {"creature_leaving_dungeon",                    44},
    {"creature_leaves",                             45},
    {"creature_in_hold_audience",                   46},
    {"patrol_here",                                 47},
    {"patrolling",                                  48},
    {"creature_kill_creatures",                     49},
    {"creature_kill_diggers",                       50},
    {"person_sulking",                              51},
    {"at_barrack_room",                             52},
    {"barracking",                                  53},
    {"creature_slap_cowers",                        54},
    {"creature_unconscious",                        55},
    {"creature_pick_up_unconscious_body",           56},
    {"imp_toking",                                  57},
    {"imp_picks_up_gold_pile",                      58},
    {"move_backwards_to_position",                  59},
    {"creature_drop_body_in_prison",                60},
    {"imp_arrives_at_convert_dungeon",              61},
    {"imp_converts_dungeon",                        62},
    {"creature_wants_salary",                       63},
    {"creature_take_salary",                        64},
    {"tunneller_doing_nothing",                     65},
    {"creature_object_combat",                      66},
    {"creature_change_lair",                        67},
    {"imp_birth",                                   68},
    {"at_temple",                                   69},
    {"praying_in_temple",                           70},
    {"creature_follow_leader",                      71},
    {"creature_door_combat",                        72},
    {"creature_combat_flee",                        73},
    {"creature_sacrifice",                          74},
    {"at_lair_to_sleep",                            75},
    {"creature_exempt",                             76},
    {"creature_being_dropped",                      77},
    {"creature_being_sacrificed",                   78},
    {"creature_scavenged_disappear",                79},
    {"creature_scavenged_reappear",                 80},
    {"creature_being_summoned",                     81},
    {"creature_hero_entering",                      82},
    {"imp_arrives_at_reinforce",                    83},
    {"imp_reinforces",                              84},
    {"arrive_at_alarm",                             85},
    {"creature_picks_up_spell_object",              86},
    {"creature_drops_spell_object_in_library",      87},
    {"creature_picks_up_corpse",                    88},
    {"creature_drops_corpse_in_graveyard",          89},
    {"at_guard_post_room",                          90},
    {"guarding",                                    91},
    {"creature_eat",                                92},
    {"creature_evacuate_room",                      93},
    {"creature_wait_at_treasure_room_door",         94},
    {"at_kinky_torture_room",                       95},
    {"kinky_torturing",                             96},
    {"mad_killing_psycho",                          97},
    {"creature_search_for_gold_to_steal_in_room",   98},
    {"creature_vandalise_rooms",                    99},
    {"creature_steal_gold",                        100},
    {"seek_the_enemy",                             101},
    {"already_at_call_to_arms",                    102},
    {"creature_damage_walls",                      103},
    {"creature_attempt_to_damage_walls",           104},
    {"creature_persuade",                          105},
    {"creature_change_to_chicken",                 106},
    {"creature_change_from_chicken",               107},
    {"creature_cannot_find_anything_to_do",        108},
    {"creature_piss",                              109},
    {"creature_roar",                              110},
    {"creature_at_changed_lair",                   111},
    {"creature_be_happy",                          112},
    {"good_leave_through_exit_door",               113},
    {"good_wait_in_exit_door",                     114},
    {"good_attack_room",                           115},
    {"good_arrived_at_attack_room",                116},
    {"creature_pretend_chicken_setup_move",        117},
    {"creature_pretend_chicken_move",              118},
    {"creature_attack_rooms",                      119},
    {"creature_freeze_prisoners",                  120},
    {"creature_explore_dungeon",                   121},
    {"creature_eating_at_garden",                  122},
    {"creature_leaves_or_dies",                    123},
    {"creature_moan",                              124},
    {"creature_set_work_room_based_on_position",   125},
    {"creature_being_scavenged",                   126},
    {"creature_escaping_death",                    127},
    {"creature_present_to_dungeon_heart",          128},
    {"creature_search_for_spell_to_steal_in_room", 129},
    {"creature_pick_up_spell_to_steal",            130},
    {"creature_going_to_safety_for_toking",        131},
    {"creature_timebomb",                          132},
    {"good_arrived_at_combat",                     133},
    {"good_arrived_at_attack_dungeon_heart",       134},
    {"creature_drop_unconscious_in_lair",          135},
    {"creature_save_unconscious_creature",         136},
    {NULL,                                           0},
};
_Static_assert(sizeof(process_func_commands) / sizeof(process_func_commands[0]) == PROCESS_FUNC_COMMANDS_COUNT + 1, "process_func_commands: update PROCESS_FUNC_COMMANDS_COUNT");

// Indexes into cleanup_func_list[] (kfx_sim/src/creature_states.c).
const struct NamedCommand cleanup_func_commands[] = {
    {"none",                                0},
    {"state_cleanup_dragging_object",       1},
    {"state_cleanup_in_room",               2},
    {"cleanup_sleep",                       3},
    {"state_cleanup_unable_to_fight",       4},
    {"cleanup_prison",                      5},
    {"cleanup_torturing",                   6},
    {"cleanup_combat",                      7},
    {"cleanup_hold_audience",               8},
    {"state_cleanup_unconscious",           9},
    {"state_cleanup_dragging_body",        10},
    {"cleanup_object_combat",              11},
    {"state_cleanup_in_temple",            12},
    {"cleanup_door_combat",                13},
    {"cleanup_sacrifice",                  14},
    {"state_cleanup_wait_at_door",         15},
    {"cleanup_seek_the_enemy",             16},
    {"cleanup_creature_leaves_or_dies",    17},
    {"cleanup_timebomb",                   18},
    {NULL,                                  0},
};
_Static_assert(sizeof(cleanup_func_commands) / sizeof(cleanup_func_commands[0]) == CLEANUP_FUNC_COMMANDS_COUNT + 1, "cleanup_func_commands: update CLEANUP_FUNC_COMMANDS_COUNT");

// Indexes into move_from_slab_func_list[] (kfx_sim/src/creature_states.c).
const struct NamedCommand move_from_slab_func_commands[] = {
    {"none",                                         0},
    {"new_slab_tunneller_check_for_breaches",        1},
    {NULL,                                           0},
};
_Static_assert(sizeof(move_from_slab_func_commands) / sizeof(move_from_slab_func_commands[0]) == MOVE_FROM_SLAB_FUNC_COMMANDS_COUNT + 1, "move_from_slab_func_commands: update MOVE_FROM_SLAB_FUNC_COMMANDS_COUNT");

// Indexes into move_check_func_list[] (kfx_sim/src/creature_states.c).
const struct NamedCommand move_check_func_commands[] = {
    {"none",                               0},
    {"move_check_on_head_for_room",        1},
    {"process_research_function",          2},
    {"process_prison_function",            3},
    {"process_torture_function",           4},
    {"process_scavenge_function",          5},
    {"move_check_near_dungeon_heart",      6},
    {"move_check_kill_creatures",          7},
    {"move_check_kill_diggers",            8},
    {"move_check_wait_at_door_for_wage",   9},
    {"process_temple_function",           10},
    {"process_kinky_function",            11},
    {"move_check_attack_any_door",        12},
    {"move_check_can_damage_wall",        13},
    {"move_check_persuade",               14},
    {NULL,                                 0},
};
_Static_assert(sizeof(move_check_func_commands) / sizeof(move_check_func_commands[0]) == MOVE_CHECK_FUNC_COMMANDS_COUNT + 1, "move_check_func_commands: update MOVE_CHECK_FUNC_COMMANDS_COUNT");

/******************************************************************************/
#ifdef __cplusplus
}
#endif
