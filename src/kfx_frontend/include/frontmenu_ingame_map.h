/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontmenu_ingame_map.h
 *     Header file for frontmenu_ingame_map.c.
 * @par Purpose:
 *     Map on in-game GUI panel drawing and support functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     20 Jan 2009 - 23 Nov 2012
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/

#ifndef DK_FRONTMENU_INGAMEMAP_H
#define DK_FRONTMENU_INGAMEMAP_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define PANEL_MAP_RADIUS       58
/******************************************************************************/
extern int64_t MapDiagonalLength;
extern unsigned char grabbed_small_map;
extern int64_t clicked_on_small_map;
/******************************************************************************/
void panel_map_update(int64_t x, int64_t y, int64_t w, int64_t h);
void panel_map_draw_slabs(int64_t x, int64_t y, int64_t units_per_px, int64_t zoom);
void panel_map_draw_overlay_things(int64_t units_per_px, int64_t zoom, int64_t basic_zoom);

int64_t do_left_map_drag(int64_t begin_x, int64_t begin_y, int64_t curr_x, int64_t curr_y, int64_t zoom);
int64_t do_left_map_click(int64_t begin_x, int64_t begin_y, int64_t curr_x, int64_t curr_y, int64_t zoom);
int64_t do_right_map_click(int64_t start_x, int64_t start_y, int64_t curr_x, int64_t curr_y, int64_t zoom);

void update_panel_colors(void);
void update_panel_color_player_color(PlayerNumber plyr_idx, unsigned char color_idx);
void setup_panel_colors();
void reset_panel_map_background_cache(void);

/******************************************************************************/

#ifdef __cplusplus
}
#endif
#endif
