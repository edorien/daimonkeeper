/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file level_start_options.h
 *     Header file for level_start_options.c.
 * @par Purpose:
 *     One-shot options the Skirmish setup tab asks for at Play and the level start carries out once its players
 *     exist -- things that are not part of the level script (so not in the script override).
 *     Keyed on the level number, like level_script_override.h: one asked for another level is discarded, so it
 *     cannot leak into a later campaign / free-play / network level.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_LEVEL_START_OPTIONS_H
#define DK_LEVEL_START_OPTIONS_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/** Asks for the whole map revealed to every active keeper when `lvnum` starts (the reveal map special / cheat). */
void level_start_reveal_map_request(LevelNumber lvnum);
/** Forgets any request. */
void level_start_options_clear(void);
/** Whether a reveal is asked for `lvnum`. */
TbBool level_start_reveal_map_requested(LevelNumber lvnum);
/** Carries the request out for the level just started (every keeper that exists reveals the whole map), and
 *  forgets it whatever it was for. Returns how many keepers revealed. */
int64_t level_start_options_apply(LevelNumber lvnum);
/******************************************************************************/
#ifdef __cplusplus
}
#endif

#endif
