/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_state_validate.h
 *     Whether a game state (the level script and what runs it) that came from outside can be used without leaving its
 *     arrays. The counterpart of sim_state_validate() for kfx_game_state (security pass S06, docs/security-pass).
 * @par Purpose:
 *     The state holds the level's script as tables with counts (values, conditions, triggers, parties, win and lose
 *     conditions): the game loops to a count and indexes by what the entries name. A saved game or a resync supplies
 *     all of it.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_GAME_STATE_VALIDATE_H
#define DK_GAME_STATE_VALIDATE_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

struct KfxGameState;

/** @return true if the state is safe to use; otherwise `why` (if given) says which field is wrong. */
TbBool game_state_validate(const struct KfxGameState *state, char *why, size_t why_size);

/** Every kind of violation at once, for calibrating against real states (see state_check.h). @return how many kinds. */
int64_t game_state_survey(const struct KfxGameState *state, char *report, size_t report_size);

#ifdef __cplusplus
}
#endif

#endif
