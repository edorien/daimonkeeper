/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_compat_review.h
 *     Header file for game_compat_review.c.
 * @par Purpose:
 *     After a level loads: log the compat report (kfx_config's
 *     compat_report.h -- content features this build doesn't support) and
 *     decide whether the player should see it before play.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_GAME_COMPAT_REVIEW_H
#define DK_GAME_COMPAT_REVIEW_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Whether a non-empty compat report should stop for the player: only a local,
 *  human-attended game -- not multiplayer, a replay, a functional test, an
 *  editor session, or a game the TCP API may be driving (nobody to click). */
TbBool compat_review_wanted(void);
/** Logs the report for the level just loaded, and flags it for the in-game
 *  warning when compat_review_wanted(). Called once the level script has loaded. */
void compat_review_after_level_load(LevelNumber lvnum);

#ifdef __cplusplus
}
#endif
#endif
