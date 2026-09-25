/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file ariadne_update.h
 *     Header file for ariadne_update.c.
 * @par Purpose:
 *     map updates for Ariadne system support.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_ARIADNE_UPDATE_H
#define DK_ARIADNE_UPDATE_H

#include "bflib_basics.h"
#include "globals.h"


#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/

/******************************************************************************/
int64_t update_navigation_triangulation(int64_t start_x, int64_t start_y, int64_t end_x, int64_t end_y);
int64_t init_navigation(void);

// Ariadne-owned navigation-map bookkeeping (see docs/refactor/
// stage-06a-ariadne-pathfinding-interface.md, Track 1). navigation_map/
// navigation_map_size_x/y/map_changed_for_navigation are ariadne's own
// cache, not general sim state, but currently still live inside
// kfx_sim_state -- callers outside ariadne go through these accessors
// instead of poking the fields directly, so the fields can move to their
// own state struct later without touching call sites again.
void ariadne_reset_navigation_map(void);
void ariadne_set_navigation_map_size(MapSubtlCoord size_x, MapSubtlCoord size_y);
TbBool ariadne_is_map_dirty_for_navigation(void);
void ariadne_clear_map_dirty_for_navigation(void);
void ariadne_mark_map_dirty_for_navigation(void);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
