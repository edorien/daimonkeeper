/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_overlay.h
 *     Header file for editor_overlay.cpp.
 * @par Purpose:
 *     docs/refactor/editor/04-views-camera-overlays.md phase 4 slices 4-5 --
 *     the "core" world-space overlays (slab grid, coordinate readout,
 *     ownership tint) plus slice 5's marker overlays (things, lights,
 *     action points/hero gates), all built on
 *     project_world_position_to_screen() (kfx_render, slice 3). Internal to
 *     kfx_editor (not part of kfx_editor.h's public surface); split into its
 *     own file for the same reason editor_toolbox.cpp/editor_journal.cpp are
 *     their own files rather than growing editor_session.cpp into a
 *     catch-all.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_EDITOR_OVERLAY_H
#define DK_EDITOR_OVERLAY_H

#include "bflib_basics.h" // TbBool

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

// Draws whichever of the toggles below are on. Called every frame from
// editor_frame() while editor_is_active(); does not check that itself.
void editor_overlay_frame(void);

// Toggled from the View menu (editor_menubar.cpp), each independent of the
// others -- mirrors editor_preview_motion()/editor_set_preview_motion()'s
// own get/set accessor shape (kfx_editor.h) so the menu code looks the
// same for every toggle it owns.
TbBool editor_overlay_slab_grid_enabled(void);
void editor_overlay_set_slab_grid_enabled(TbBool enabled);
TbBool editor_overlay_coordinates_enabled(void);
void editor_overlay_set_coordinates_enabled(TbBool enabled);
TbBool editor_overlay_ownership_tint_enabled(void);
void editor_overlay_set_ownership_tint_enabled(TbBool enabled);

// docs/refactor/editor/04-views-camera-overlays.md phase 4 slice 5. Action
// points and hero gates are one toggle, not two -- the design doc's own
// overlay list already grouped them as a single bullet ("Action point /
// hero gate markers"), and a hero gate is itself a kind of thing (an
// object with object_is_hero_gate() true), so this pairing tracks how the
// data is actually shaped rather than an arbitrary split.
TbBool editor_overlay_thing_markers_enabled(void);
void editor_overlay_set_thing_markers_enabled(TbBool enabled);
TbBool editor_overlay_light_markers_enabled(void);
void editor_overlay_set_light_markers_enabled(TbBool enabled);
TbBool editor_overlay_ap_herogate_markers_enabled(void);
void editor_overlay_set_ap_herogate_markers_enabled(TbBool enabled);

// The marker dot + label and radius ring the overlays above use, exported
// for editor_points.cpp (the Points tool draws its selection highlight and
// its drag-radius preview with the same look). `color` is an ImU32.
void editor_overlay_draw_marker(int64_t wx, int64_t wy, int64_t wz, uint64_t color, const char *label);
void editor_overlay_draw_radius_ring(int64_t wx, int64_t wy, int64_t wz, int64_t radius, uint64_t color);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
