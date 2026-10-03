/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_funcnames.h
 *     Names of the computer-player and creature function tables, for the .cfg parsers.
 * @par Purpose:
 *     Each table maps a name used in a .cfg file (keepcompp.cfg, creature and
 *     crstates configs) to an index into a function-pointer table that stays in
 *     kfx_sim (the table named in each comment below). The names moved here in
 *     refactor pass 2 (S03) so kfx_config can parse without calling up into
 *     kfx_sim. _COUNT is the number of named entries; _SLOTS is the highest
 *     index + 1, which the kfx_sim function table must cover (a _Static_assert
 *     next to it checks that). When adding a function, add its name here and its
 *     pointer there, at the same index.
 */
/******************************************************************************/
#ifndef DK_CONFIG_FUNCNAMES_H
#define DK_CONFIG_FUNCNAMES_H

#include "bflib_basics.h"
#include "globals.h"
#include "config.h" // struct NamedCommand

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/* computer_process_func_list[], kfx_sim/src/player_compprocs.c */
#define COMPUTER_PROCESS_FUNC_TYPE_COUNT 20
#define COMPUTER_PROCESS_FUNC_TYPE_SLOTS 21
extern const struct NamedCommand computer_process_func_type[];

/* computer_check_func_list[], kfx_sim/src/player_compchecks.c */
#define COMPUTER_CHECK_FUNC_TYPE_COUNT 19
#define COMPUTER_CHECK_FUNC_TYPE_SLOTS 20
extern const struct NamedCommand computer_check_func_type[];

/* computer_event_test_func_list[], kfx_sim/src/player_compevents.c */
#define COMPUTER_EVENT_TEST_FUNC_TYPE_COUNT 7
#define COMPUTER_EVENT_TEST_FUNC_TYPE_SLOTS 8
extern const struct NamedCommand computer_event_test_func_type[];

/* computer_event_func_list[], kfx_sim/src/player_compevents.c */
#define COMPUTER_EVENT_FUNC_TYPE_COUNT 7
#define COMPUTER_EVENT_FUNC_TYPE_SLOTS 8
extern const struct NamedCommand computer_event_func_type[];

/* creature_instances_func_list[], kfx_sim/src/creature_instances.c */
#define CREATURE_INSTANCES_FUNC_TYPE_COUNT 14
#define CREATURE_INSTANCES_FUNC_TYPE_SLOTS 15
extern const struct NamedCommand creature_instances_func_type[];

/* creature_instances_validate_func_list[], kfx_sim/src/creature_instances.c */
#define CREATURE_INSTANCES_VALIDATE_FUNC_TYPE_COUNT 13
#define CREATURE_INSTANCES_VALIDATE_FUNC_TYPE_SLOTS 14
extern const struct NamedCommand creature_instances_validate_func_type[];

/* creature_instances_search_targets_func_list[], kfx_sim/src/creature_instances.c */
#define CREATURE_INSTANCES_SEARCH_TARGETS_FUNC_TYPE_COUNT 2
#define CREATURE_INSTANCES_SEARCH_TARGETS_FUNC_TYPE_SLOTS 3
extern const struct NamedCommand creature_instances_search_targets_func_type[];

/* creature_job_player_check_func_list[], kfx_sim/src/creature_jobs.c */
#define CREATURE_JOB_PLAYER_CHECK_FUNC_TYPE_COUNT 9
#define CREATURE_JOB_PLAYER_CHECK_FUNC_TYPE_SLOTS 10
extern const struct NamedCommand creature_job_player_check_func_type[];

/* creature_job_player_assign_func_list[], kfx_sim/src/creature_jobs.c */
#define CREATURE_JOB_PLAYER_ASSIGN_FUNC_TYPE_COUNT 5
#define CREATURE_JOB_PLAYER_ASSIGN_FUNC_TYPE_SLOTS 6
extern const struct NamedCommand creature_job_player_assign_func_type[];

/* creature_job_coords_check_func_list[], kfx_sim/src/creature_jobs.c */
#define CREATURE_JOB_COORDS_CHECK_FUNC_TYPE_COUNT 9
#define CREATURE_JOB_COORDS_CHECK_FUNC_TYPE_SLOTS 10
extern const struct NamedCommand creature_job_coords_check_func_type[];

/* creature_job_coords_assign_func_list[], kfx_sim/src/creature_jobs.c */
#define CREATURE_JOB_COORDS_ASSIGN_FUNC_TYPE_COUNT 5
#define CREATURE_JOB_COORDS_ASSIGN_FUNC_TYPE_SLOTS 6
extern const struct NamedCommand creature_job_coords_assign_func_type[];

/* process_func_list[], kfx_sim/src/creature_states.c */
#define PROCESS_FUNC_COMMANDS_COUNT 137
#define PROCESS_FUNC_COMMANDS_SLOTS 137
extern const struct NamedCommand process_func_commands[];

/* cleanup_func_list[], kfx_sim/src/creature_states.c */
#define CLEANUP_FUNC_COMMANDS_COUNT 19
#define CLEANUP_FUNC_COMMANDS_SLOTS 19
extern const struct NamedCommand cleanup_func_commands[];

/* move_from_slab_func_list[], kfx_sim/src/creature_states.c */
#define MOVE_FROM_SLAB_FUNC_COMMANDS_COUNT 2
#define MOVE_FROM_SLAB_FUNC_COMMANDS_SLOTS 2
extern const struct NamedCommand move_from_slab_func_commands[];

/* move_check_func_list[], kfx_sim/src/creature_states.c */
#define MOVE_CHECK_FUNC_COMMANDS_COUNT 15
#define MOVE_CHECK_FUNC_COMMANDS_SLOTS 15
extern const struct NamedCommand move_check_func_commands[];

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
