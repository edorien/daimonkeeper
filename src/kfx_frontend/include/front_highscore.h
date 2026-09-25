/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file front_highscore.h
 *     Header file for front_highscore.c.
 * @par Purpose:
 *     High Score screen displaying routines.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     01 Jan 2012 - 23 Jun 2012
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_FRONT_HIGHSCORE_H
#define DK_FRONT_HIGHSCORE_H

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_guibtns.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
extern int64_t high_score_entry_input_active;
extern char high_score_entry[64];
extern int64_t fe_high_score_table_from_main_menu;
/******************************************************************************/
void frontend_draw_high_score_table(struct GuiButton *gbtn);
void frontend_quit_high_score_table(struct GuiButton *gbtn);
// Writes high_score_entry (or the localized default name) into the pending
// entry and saves the table -- split out (no longer `static`) so
// kfx_frontend's ImGui FeSt_HIGH_SCORES screen (docs/refactor/renderer/
// 04-imgui-gui-foundation.md Phase D) can finalize an ImGui InputText the
// same way the legacy Enter/Escape handling in
// frontend_high_score_table_input() does.
void finalize_high_score_entry(TbBool restore_default_name);
void frontend_maintain_high_score_ok_button(struct GuiButton *gbtn);
TbBool frontend_high_score_table_input(void);
void frontend_maintain_high_score_ok_button(struct GuiButton *gbtn);
void frontstats_save_high_score(void);
void highscore_scroll_up(struct GuiButton *gbtn);
void highscore_scroll_down(struct GuiButton *gbtn);
void highscore_scroll(struct GuiButton *gbtn);
void frontend_highscore_scroll_up_maintain(struct GuiButton *gbtn);
void frontend_highscore_scroll_down_maintain(struct GuiButton *gbtn);
void frontend_highscore_scroll_tab_maintain(struct GuiButton *gbtn);
void frontend_draw_highscores_scroll_tab(struct GuiButton *gbtn);
void frontend_high_scores_update();
void frontend_draw_highscores_scroll_box_tab(struct GuiButton *gbtn);
void frontend_draw_high_scores_mappack(struct GuiButton *gbtn);
uint64_t count_high_scores();
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
