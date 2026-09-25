/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file front_lvlstats.h
 *     Header file for front_lvlstats.c.
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
#ifndef DK_FRONT_LVLSTATS_H
#define DK_FRONT_LVLSTATS_H

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_guibtns.h"
#include "dungeon_stats.h" // struct LevelStats

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
#pragma pack(1)

typedef int64_t (*StatGetValueCallback)(void *ptr);

struct StatsData { // sizeof = 12
  uint64_t name_stridx;
  StatGetValueCallback get_value;
  void *get_arg;
};

#pragma pack()
/******************************************************************************/
// Backing data for the two stat blocks (front_lvlstats_data.cpp) -- already
// external linkage there, just never declared here; the ImGui screen
// (frontgui_screens.cpp) iterates these directly the same way the legacy
// draw_calls do, rather than duplicating the stat list.
extern struct LevelStats frontstats_data;
extern struct StatsData main_stats_data[];
extern struct StatsData scrolling_stats_data[];
void frontstats_draw_main_stats(struct GuiButton *gbtn);
void frontstats_draw_scrolling_stats(struct GuiButton *gbtn);
void frontstats_leave(struct GuiButton *gbtn);
void frontstats_set_timer(void);
void frontstats_update(void);
void frontstats_initialise(void);
void init_menu_state_on_net_stats_exit(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
