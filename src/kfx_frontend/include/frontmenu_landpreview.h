/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontmenu_landpreview.h
 *     Header file for frontmenu_landpreview.c.
 * @par Purpose:
 *     A drag-to-pan, click-to-highlight land-map preview panel, sized to fit
 *     inside a merged frontend screen (e.g. the Land selection screen) --
 *     as opposed to front_landview.c's full-screen interactive cutscene.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_FRONTMENU_LANDPREVIEW_H
#define DK_FRONTMENU_LANDPREVIEW_H

#include "bflib_basics.h"
#include "bflib_guibtns.h"
#include "bflib_video.h"
#include "config_campaigns.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct LandPreviewPanel {
    long screen_shift_x; /**< Pan position, top-left corner of the visible area, in content_w/content_h (map-bitmap) space. */
    long screen_shift_y;
    int units_per_px; /**< Panel-local landview scale; 16 = 1:1. Independent of front_landview.c's units_per_pixel_landview, which is sized for the whole physical screen. */
    TbBool dragging;
    long drag_last_x; /**< Screen-space mouse position as of the last drag step, for computing the per-frame delta. */
    long drag_last_y;
    LevelNumber highlighted_lvnum; /**< SINGLEPLAYER_NOTSTARTED if none -- the detail panel should show the campaign's own description in that case. */
    // Set by land_preview_load() when a campaign overview just loaded, to
    // the campaign's "next" playable level (get_next_singleplayer_level_for_landview(),
    // front_landview.c); consumed once by land_preview_maintain() (which
    // knows the real on-screen rect + zoom, load() doesn't) to centre the
    // initial view on that level's ensign, then reset to
    // SINGLEPLAYER_NOTSTARTED. Doesn't apply on later browsing (dragging,
    // clicking another level) -- only the just-loaded state.
    LevelNumber pending_center_lvnum;
    TbBool loaded;
    // Whether land_preview_draw overlays level ensigns on top of the
    // backdrop. Land selection loads a *campaign* overview image
    // (SINGLEPLAYER_NOTSTARTED) where every level's ensign_x/y is a
    // meaningful position on that shared picture. Free play loads a
    // specific *level's own* land_view image instead (see
    // land_preview_load's lvnum parameter) -- ensign coordinates authored
    // for the campaign overview don't correspond to anything sensible on
    // that different picture, so ensigns are skipped there.
    TbBool show_ensigns;
    // True when this load used land_preview_build_minimap's slab-colour
    // synthesis instead of a real load_map_and_window() (the common case
    // for an individual freeplay level -- see land_preview_load). Changes
    // two things: (1) land_preview_unload must NOT call
    // unload_map_and_window() -- that assumes a real load happened
    // (frontend_backup_palette is stale/never-set otherwise); (2) the
    // whole synthesized image already fits content_w x content_h, so the
    // panel should zoom to show all of it and skip drag-to-pan, rather
    // than the campaign-art half-visible/zoomable/pannable behaviour.
    TbBool minimap_mode;
    // The land's own just-loaded palette (from load_map_and_window, via
    // frontend_palette), kept independent of frontend_palette itself --
    // frontend_palette is restored to the standard frontend palette
    // immediately after load_map_and_window runs (see land_preview_load),
    // so it's always safe to read as "the standard palette" everywhere
    // else. land_preview_draw swaps the renderer's active palette to this
    // buffer for just the one backdrop blit, then back to frontend_palette
    // -- see docs/refactor/renderer/00-overview.md's cross-reference note
    // on why this swap-and-restore, unlike the old whole-buffer remap it
    // replaces, can't corrupt anything already drawn.
    unsigned char land_palette[PALETTE_SIZE];
};

extern struct LandPreviewPanel land_preview;

/******************************************************************************/
// Pure math -- unit tested directly, no sprites/campaign data needed.
int land_preview_compute_units_per_px(long rect_w, long rect_h);
void land_preview_clamp_shift(struct LandPreviewPanel *panel, long rect_w, long rect_h);
TbBool land_preview_point_over_ensign_box(long map_x, long map_y, long ensign_x, long ensign_y, long spr_w, long spr_h);

// Lifecycle and per-frame use -- real sprites/campaign data, not covered
// by unit tests. target_lvnum is passed straight to load_map_and_window:
// SINGLEPLAYER_NOTSTARTED loads the active campaign's own overview image
// (campaign.land_view_start), any other LevelNumber loads that specific
// level's own land_view/land_window (falling back to the campaign's if
// the level doesn't have its own -- see load_map_and_window). If that
// fails (the common case for an individual freeplay level, which usually
// has no land_view art of its own), falls back to a slab-colour minimap
// synthesized directly from the level's own map data (see
// land_preview_build_minimap in the .c file). Pass show_ensigns = true
// only when target_lvnum's image is the shared campaign overview the
// ensign coordinates were authored against.
TbBool land_preview_load(struct LandPreviewPanel *panel, LevelNumber target_lvnum, TbBool show_ensigns);
void land_preview_unload(struct LandPreviewPanel *panel);
void land_preview_maintain(struct GuiButton *gbtn);
void land_preview_draw(struct GuiButton *gbtn);
// Shrinks the ornate corner frame's absolute pixel size by an extra
// factor of extra_den (2 = half size), on top of its own fixed
// scale_ui_value_lofi()-based sizing -- see the .c file's own comment on
// why that sizing doesn't respond to the panel's own dimensions. Applies
// to every land_preview_draw() call until changed again; callers embedding
// this panel at a size very different from the legacy screen it was
// originally tuned for should set this before drawing and reset it to 1
// afterward, so the legacy screen (which never touches this) is
// unaffected. extra_den <= 0 is treated as 1 (no change).
void land_preview_set_frame_extra_scale_den(long extra_den);

// docs/refactor/editor/phase3/02-slice3-dialogs-menubar.md -- exported for
// the editor's Open Map dialog (kfx_editor, ranks above kfx_frontend) to
// draw its own lightweight per-row thumbnail, without pulling in the full
// LandPreviewPanel pan/zoom/ornate-frame machinery land_preview_load()
// drives. Reads a level's map%05u.slb/.own files directly off disk (see
// the .c file's own comment) -- never touches kfx_sim_state, safe to call
// just for browsing. Returns false (and leaves the minimap empty) if the
// level has no .slb file at all.
TbBool land_preview_build_minimap(LevelNumber lvnum);
void land_preview_free_minimap(void);
// 0 if land_preview_build_minimap() hasn't been called yet, or failed.
long land_preview_minimap_width(void);
long land_preview_minimap_height(void);
// Out-of-range x/y (or an empty minimap) returns SlbT_ROCK's own colour
// rather than asserting -- callers should still check width()/height()
// first to size their loop, this is just a safety net.
void land_preview_minimap_pixel_rgb(long x, long y, unsigned char *r, unsigned char *g, unsigned char *b);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
