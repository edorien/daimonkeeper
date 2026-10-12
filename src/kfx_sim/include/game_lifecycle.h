/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_lifecycle.h
 *     Header file for game_lifecycle.c.
 * @par Purpose:
 *     Clearing/resetting the simulated game world (map, things, players,
 *     computer state) between levels or before a save/load. Moved out of
 *     src/main.cpp in stage 12 (docs/refactor/stage-12-slim-app-target.md)
 *     -- these are kfx_sim-internal reset routines that simply never got
 *     migrated in stages 6-11.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_GAME_LIFECYCLE_H
#define DK_GAME_LIFECYCLE_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
void clear_creature_pool(void);
void clear_map(void);
void clear_things_and_persons_data(void);
void clear_computer(void);
/** Forgets every keeper's combat assistant rules and counters (a new level). */
void clear_keeper_assists(void);
void init_keepers_map_exploration(void);
void clear_players_for_save(void);
void delete_all_thing_structures(void);
void delete_all_structures(void);
void clear_game_for_save(void);
void reset_creature_max_levels(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
