/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_points.h
 *     Header file for editor_points.cpp.
 * @par Purpose:
 *     docs/refactor/editor/05-script-and-level-settings.md §2/§3 -- the
 *     "Points" tool: static lights, action points and effect generators are
 *     all a position plus an effect radius, so one tool places, selects,
 *     edits and deletes all three. Internal to kfx_editor, same role as
 *     editor_toolbox.h.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_EDITOR_POINTS_H
#define DK_EDITOR_POINTS_H

#include "bflib_basics.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

enum EditorPointKind {
    EPK_Light = 0,
    EPK_ActionPoint,
    EPK_EffectGen,
    EPK_Count
};

// Everything needed to (re)create a point, and enough to recognise it again
// later: `id` is a light index / action point number / thing index
// depending on `kind`. Light and thing slots get reused, so the undo journal
// never trusts `id` alone -- delete_point() re-checks position (and model,
// for effect generators) before touching anything.
struct EditorPointSnapshot {
    int kind;
    long id;
    long x, y, z;     // raw map units (256 per subtile)
    long radius;      // light radius / action point range / effect range, raw units
    int intensity;    // lights only
    int model;        // effect generators only
    int owner;        // effect generators only
    long parent;      // light attached_slb / effect generator parent_idx
};

// Toolbox panel: kind selector, default parameters for new points, counts
// with soft-cap warnings, and the inspector for the selected point. Called
// from editor_toolbox_frame() while the Points tool is active.
void editor_points_draw_panel(void);

// Click/drag/delete handling plus the tool's own overlay. Called every frame
// from editor_toolbox_frame() while the Points tool is active.
void editor_points_frame(void);

// How many effect generator kinds the picker offers (test seam: the picker
// once came up empty because it read a count the loader never sets).
int editor_points_effectgen_kind_count(void);

// Drops the selection (a new/opened level invalidates every id).
void editor_points_reset(void);

// Undo journal hooks (editor_journal.cpp). create_point() fills in
// snap->id; delete_point() returns false (and does nothing) if the target no
// longer matches the snapshot.
TbBool editor_points_create(struct EditorPointSnapshot *snap);
TbBool editor_points_delete(const struct EditorPointSnapshot *snap);
const char *editor_points_describe(const struct EditorPointSnapshot *snap, TbBool placed);

// Lights the session owns on behalf of a thing (its own light) or a player
// (the cursor light) rather than the level. Marks owned[light_index] = 1;
// `owned` must have LIGHTS_COUNT entries. Shared with the map snapshot so a
// save never persists a torch's own light as a second, level-owned copy.
void editor_points_mark_thing_owned_lights(unsigned char *owned);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
