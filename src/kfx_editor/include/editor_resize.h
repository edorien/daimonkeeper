/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_resize.h
 *     Resizing a map that already has content (fx-plans/00-audit-and-index.md
 *     A14). Done on a MapContent snapshot, never on the live map: the resized
 *     copy is written to a scratch level and the editor reopens it.
 */
#ifndef DK_EDITOR_RESIZE_H
#define DK_EDITOR_RESIZE_H

#include "map_content.h"

struct EditorResizeReport
{
    int64_t things_dropped = 0;
    int64_t lights_dropped = 0;
    int64_t action_points_dropped = 0;
};

enum { EDITOR_RESIZE_MIN = 8, EDITOR_RESIZE_MAX = 170 };

/** Changes `content` to new_w x new_h slabs. New ground is neutral rock. With `centered` the old map is
 *  placed in the middle (cropped or padded equally), otherwise its top-left corner stays put. Things,
 *  lights and action points move with the ground; those left outside the new bounds are removed and
 *  counted. The classic derived files are dropped (they belong to the old size). False if the size is out
 *  of range. */
bool editor_resize_content(MapContent &content, int64_t new_w, int64_t new_h, bool centered, PlayerNumber neutral_owner,
    EditorResizeReport *report);

#endif
