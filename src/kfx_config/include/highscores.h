/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file highscores.h
 *     Header file for highscores.c.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/

#ifndef HIGHSCORES_H
#define HIGHSCORES_H

#include "bflib_basics.h"
#include "globals.h"

struct GameCampaign;

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
void load_or_create_high_score_table(void);
TbBool save_high_score_table(void);
int64_t add_high_score_entry(uint64_t score, LevelNumber lvnum, const char *name);
uint64_t get_level_highest_score(LevelNumber lvnum);
// Read-only counterpart of load_or_create_high_score_table() for browsing
// another pack's high scores (High Scores screen's pack picker) without
// making it the active campaign: loads campgn->hiscore_table from disk if
// not already loaded, but -- unlike load_or_create_high_score_table() --
// never synthesizes/saves a placeholder table for a pack that's merely
// being viewed, never played. Safe to call every frame; a no-op once the
// table is loaded (campgn->hiscore_table != NULL).
void ensure_high_score_table_loaded(struct GameCampaign *campgn);

#ifdef __cplusplus
}
#endif
#endif
