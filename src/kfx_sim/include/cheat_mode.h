/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cheat_mode.h
 *     Header file for cheat_mode.c.
 * @par Purpose:
 *     Whether cheats are allowed: off in a multiplayer game, whatever each machine was started with.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_CHEAT_MODE_H
#define DK_CHEAT_MODE_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/** Whether this game refuses cheats and the editor's actions: a multiplayer game. The same on every machine. */
TbBool game_refuses_cheats(void);
/** Whether cheat mode is on: started with it (-alex, kfx_sim_state.easter_eggs_enabled, each machine's own command
 *  line) in a game that doesn't refuse cheats. */
TbBool cheat_mode_enabled(void);
/** Whether a given player's cheats (console commands, cheat packets) take effect. */
TbBool player_cheats_allowed(PlayerNumber plyr_idx);
/** Whether a player work state is a cheat's or the editor's cursor mode. */
TbBool player_state_is_cheat(int64_t work_state);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
