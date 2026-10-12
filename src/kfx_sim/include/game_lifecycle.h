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
/** A level starts from the simulation state as start-up left it (sim_state_take_level_baseline(), at the end of
 *  start-up), with the session's own fields kept: sim_state_reset_for_level(), before the level's clears and set-up
 *  (refactor pass 5, S12). Nothing of a previous level is left in it, so a level's saved state doesn't depend on what
 *  was played before it in the process. */
void sim_state_take_level_baseline(void);
void sim_state_reset_for_level(void);

/** Stores this machine's clock (posix time, time zone, and the game turn) as the game's timestamp, for Lua's
 *  GetGameTimestamp: the host's is the one that counts, sent to the others (PckA_SetTimestamp, the initial sync,
 *  a resync) and recorded in replays (upstream #5383). */
void take_game_timestamp(void);
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
