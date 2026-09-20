/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_brush.h
 *     The editor's Stamp tool without its UI: capture an area (terrain slabs,
 *     things, lights / action points / effect generators) and stamp it
 *     elsewhere as one undoable step. Split out of editor_toolbox.cpp so it can
 *     be tested and reused (fx-plans/00-audit-and-index.md B9).
 */
#ifndef DK_EDITOR_BRUSH_H
#define DK_EDITOR_BRUSH_H

#include <cstddef>
#include "globals.h"

// Captures the slabs, things and points in the inclusive slab box. Engine-derived
// slabs (bridge, gems, guard post) and the heart / portal are left out: they cannot
// be copied meaningfully.
void editor_brush_capture(MapSlabCoord box_beg_x, MapSlabCoord box_beg_y, MapSlabCoord box_end_x, MapSlabCoord box_end_y);

// Stamps the captured area with its top-left at this slab. One journal entry.
void editor_brush_stamp(MapSlabCoord cur_slb_x, MapSlabCoord cur_slb_y);

bool editor_brush_is_empty(void);
size_t editor_brush_slab_count(void);
size_t editor_brush_thing_count(void);
size_t editor_brush_point_count(void);
void editor_brush_clear(void);

#endif
