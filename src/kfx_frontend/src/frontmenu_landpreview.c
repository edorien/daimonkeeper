/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontmenu_landpreview.c
 *     A drag-to-pan, click-to-highlight land-map preview panel.
 * @par Purpose:
 *     Panel-scoped counterpart to front_landview.c's full-screen interactive
 *     landview, for embedding inside a merged frontend screen (Land
 *     selection, Free play). Reuses front_landview.c's data loading
 *     (load_map_and_window/load_map_ensign_sprites), sprite lookup
 *     (get_map_ensign/get_ensign_sprite_for_level) and description speech
 *     (play_description_speech and friends) as-is -- those have no
 *     PhysicalScreenWidth/Height coupling. Drawing and input are new: the
 *     original draw_map_screen/check_mouse_scroll/update_velocity/
 *     is_over_ensign all read the physical screen size and/or a single
 *     global pan state (map_info, units_per_pixel_landview) sized for the
 *     whole screen, neither of which fits a small panel sitting beside
 *     other UI.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "frontmenu_landpreview.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_datetm.h"
#include "bflib_sprite.h"
#include "bflib_video.h"
#include "bflib_vidraw.h"

#include "config.h"
#include "config_campaigns.h"
#include "config_terrain.h"
#include "custom_sprites.h"
#include "front_landview.h"
#include "gui_draw.h"
#include "kjm_input.h"
#include "lvl_filesdk1.h"
#include "map_data.h"
#include "player_data.h"
#include "renderer/RendererManager.h"
#include "sprites.h"
#include "vidfade.h"
#include "vidmode.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// Show roughly half the map's width/height at once -- matches the ~2x
// zoom feel calculate_landview_upp() aims for on the full-screen cutscene,
// just computed from the panel's own rect instead of the physical screen.
#define LANDVIEW_PREVIEW_ZOOM_DIVISOR 2
#define LANDVIEW_PREVIEW_MIN_UPP 4

// The ornate corner frame (land_preview_draw_ornate_frame below) is a
// scaled-down copy of gui_draw.c's draw_ornate_slab_outline64k, sized for
// wrapping a whole GuiMenu -- at full size its dragon-head corners
// overflow a small panel's neighbours (list box, detail box).
// FRAME_SCALE_NUM/DEN shrinks every offset/sprite-size in the copy;
// FRAME_INSET (now land_preview_frame_inset(), see below) shrinks the
// actual land content rect inward from the button's own rect so the
// frame has room to draw its corners without reaching past the button's
// edges.
#define LAND_PREVIEW_FRAME_SCALE_NUM 1
#define LAND_PREVIEW_FRAME_SCALE_DEN 2

struct LandPreviewPanel land_preview;

// Slab-colour minimap scratch data (see land_preview_build_minimap) --
// only one LandPreviewPanel instance exists, so file-scope storage here
// is as safe as the panel struct itself, and keeps the public header from
// having to expose the minimap's own implementation details.
static unsigned short *land_preview_minimap_kind = NULL;
static unsigned char *land_preview_minimap_owner = NULL;
static long land_preview_minimap_map_w = 0;
static long land_preview_minimap_map_h = 0;

/******************************************************************************/
int land_preview_compute_units_per_px(long rect_w, long rect_h)
{
    if ((rect_w <= 0) || (rect_h <= 0))
        return 16;
    long upp_w = 16 * LANDVIEW_PREVIEW_ZOOM_DIVISOR * rect_w / LANDVIEW_MAP_WIDTH;
    long upp_h = 16 * LANDVIEW_PREVIEW_ZOOM_DIVISOR * rect_h / LANDVIEW_MAP_HEIGHT;
    long upp = max(upp_w, upp_h);
    if (upp < LANDVIEW_PREVIEW_MIN_UPP)
        upp = LANDVIEW_PREVIEW_MIN_UPP;
    return (int)upp;
}

void land_preview_clamp_shift(struct LandPreviewPanel *panel, long rect_w, long rect_h)
{
    if (panel->units_per_px <= 0)
        return;
    long visible_w = rect_w * 16 / panel->units_per_px;
    long visible_h = rect_h * 16 / panel->units_per_px;
    long max_x = LANDVIEW_MAP_WIDTH - visible_w;
    long max_y = LANDVIEW_MAP_HEIGHT - visible_h;
    if (max_x < 0)
        max_x = 0;
    if (max_y < 0)
        max_y = 0;
    if (panel->screen_shift_x > max_x)
        panel->screen_shift_x = max_x;
    if (panel->screen_shift_x < 0)
        panel->screen_shift_x = 0;
    if (panel->screen_shift_y > max_y)
        panel->screen_shift_y = max_y;
    if (panel->screen_shift_y < 0)
        panel->screen_shift_y = 0;
}

TbBool land_preview_point_over_ensign_box(long map_x, long map_y, long ensign_x, long ensign_y, long spr_w, long spr_h)
{
    // Same asymmetric box as front_landview.c's is_over_ensign: centered
    // horizontally on the ensign anchor, but vertically only the flag
    // banner above the anchor (the pole's base), not a box centered on it.
    return (map_x >= ensign_x-(spr_w>>1)) && (map_x < ensign_x+(spr_w>>1))
        && (map_y > ensign_y-spr_h) && (map_y < ensign_y-(spr_h/3));
}

// Extra divisor on top of FRAME_SCALE_NUM/DEN, 1 (no change) by default.
// scale_ui_value_lofi() (used throughout land_preview_draw_ornate_frame,
// including this) scales off a value cached at actual window-resolution
// -change time, not off gbtn->width/height -- so the frame's absolute
// pixel size is fixed regardless of how big or small a panel it's drawn
// into. That's correct for the legacy fixed ~368x230 screen this was
// originally sized for, but Phase E's ImGui screens draw this same panel
// into their own, differently-proportioned off-screen texture (found
// live: at a size that otherwise looked right, the frame corners still
// read as oversized) -- land_preview_set_frame_extra_scale_den() lets
// that caller ask for a smaller frame without changing the legacy
// screen's own appearance (which never touches this setter, so it always
// sees the default of 1).
static long land_preview_frame_extra_scale_den = 1;

void land_preview_set_frame_extra_scale_den(long extra_den)
{
    land_preview_frame_extra_scale_den = (extra_den > 0) ? extra_den : 1;
}

static long land_preview_frame_scale(long base_value)
{
    return scale_ui_value_lofi(base_value) * LAND_PREVIEW_FRAME_SCALE_NUM
        / (LAND_PREVIEW_FRAME_SCALE_DEN * land_preview_frame_extra_scale_den);
}

// How far the content rect is inset from gbtn's own rect (see the struct's
// field comment). Was a plain scale_ui_value_lofi(26) #define; also
// divided by land_preview_frame_extra_scale_den now -- found live, after
// halving the frame sprites above: this inset is a fixed real-resolution
// -relative pixel amount too (not the frame_scale-shrunk 26 the sprites
// themselves use, but the same underlying scale_ui_value_lofi), so it was
// left unshrunk while the frame itself got smaller, opening up a large
// empty margin between the outer panel edge and the (now smaller)
// frame+content. Shrinking it by the same factor keeps the frame hugging
// the panel's actual edges as before.
static long land_preview_frame_inset(void)
{
    return scale_ui_value_lofi(26) / land_preview_frame_extra_scale_den;
}

/** Scaled-down copy of gui_draw.c's draw_ornate_slab_outline64k -- see the
 * LAND_PREVIEW_FRAME_* comment above. Not a variant of the original
 * function itself: its own units_per_px parameter is unused (every offset
 * comes from scale_ui_value_lofi() directly), so there was no way to
 * shrink its output by passing a different value -- duplicated here with
 * every scale_ui_value_lofi() result passed through land_preview_frame_scale()
 * instead.
 */
static void land_preview_draw_ornate_frame(long pos_x, long pos_y, long width, long height)
{
    const struct TbSprite* spr = get_button_sprite(GBS_parchment_map_frame_deco_a_tl);
    int bs_units_per_spr = (int)(land_preview_frame_scale(2048) / spr->SWidth);
    long x = pos_x;
    long y = pos_y;
    long i;
    for (i = land_preview_frame_scale(10); i < width - land_preview_frame_scale(12); i += land_preview_frame_scale(32))
    {
        spr = get_button_sprite(GBS_borders_frame_thin_tc);
        LbSpriteDrawResized(pos_x + i, pos_y - land_preview_frame_scale(4), bs_units_per_spr, spr);
        spr = get_button_sprite(GBS_borders_frame_thin_bc);
        LbSpriteDrawResized(pos_x + i, pos_y + height, bs_units_per_spr, spr);
    }
    for (i = land_preview_frame_scale(10); i < height - land_preview_frame_scale(16); i += land_preview_frame_scale(32))
    {
        spr = get_button_sprite(GBS_borders_frame_thin_ml);
        LbSpriteDrawResized(x - land_preview_frame_scale(4), y + i, bs_units_per_spr, spr);
        spr = get_button_sprite(GBS_borders_frame_thin_mr);
        LbSpriteDrawResized(x + width, y + i, bs_units_per_spr, spr);
    }
    spr = get_button_sprite(GBS_borders_frame_thin_tl);
    LbSpriteDrawResized(x - land_preview_frame_scale(4),          y - land_preview_frame_scale(4),           bs_units_per_spr, spr);
    spr = get_button_sprite(GBS_borders_frame_thin_tr);
    LbSpriteDrawResized(x + width - land_preview_frame_scale(28), y - land_preview_frame_scale(4),           bs_units_per_spr, spr);
    spr = get_button_sprite(GBS_borders_frame_thin_bl);
    LbSpriteDrawResized(x - land_preview_frame_scale(4),          y + height - land_preview_frame_scale(28), bs_units_per_spr, spr);
    spr = get_button_sprite(GBS_borders_frame_thin_br);
    LbSpriteDrawResized(x + width - land_preview_frame_scale(28), y + height - land_preview_frame_scale(28), bs_units_per_spr, spr);
    spr = get_button_sprite(GBS_parchment_map_frame_deco_a_tl);
    LbSpriteDrawResized(x - land_preview_frame_scale(32), y - land_preview_frame_scale(14),          bs_units_per_spr, spr);
    spr = get_button_sprite(GBS_parchment_map_frame_deco_a_bl);
    LbSpriteDrawResized(x - land_preview_frame_scale(34), y + height - land_preview_frame_scale(78), bs_units_per_spr, spr);
    RendererAddDrawFlags(Lb_SPRITE_FLIP_HORIZ);
    spr = get_button_sprite(GBS_parchment_map_frame_deco_a_tl);
    LbSpriteDrawResized(x + width - land_preview_frame_scale(96), y - land_preview_frame_scale(14),          bs_units_per_spr, spr);
    spr = get_button_sprite(GBS_parchment_map_frame_deco_a_bl);
    LbSpriteDrawResized(x + width - land_preview_frame_scale(92), y + height - land_preview_frame_scale(78), bs_units_per_spr, spr);
    RendererClearDrawFlags(Lb_SPRITE_FLIP_HORIZ);
}

/** Semantic colour buckets for the slab-data minimap fallback below,
 * indexed directly into land_preview_minimap_colours -- real TbPixel
 * literals now, no palette-index lookup needed (see
 * docs/refactor/renderer/00-overview.md's cross-reference note).
 */
enum LandPreviewMinimapColor {
    LPMC_Rock = 0,       /**< Unrevealed rock / unclassified. */
    LPMC_Dirt,           /**< Diggable earth (SlbAtCtg_FriableDirt). */
    LPMC_Path,           /**< Claimed/open floor, unowned (SlbAtCtg_FortifiedGround). */
    LPMC_Wall,           /**< Fortified wall (SlbAtCtg_FortifiedWall). */
    LPMC_RoomNeutral,    /**< Room interior with no real owner. */
    LPMC_Obstacle,       /**< SlbAtCtg_Obstacle. */
    LPMC_Gold,
    LPMC_Gems,
    LPMC_Lava,
    LPMC_Water,
    LPMC_Player0, LPMC_Player1, LPMC_Player2, LPMC_Player3,
    LPMC_PlayerGood, LPMC_Player4, LPMC_Player5, LPMC_Player6,
    LPMC_COUNT,
};

// Approximate classic DK player colours (PlayerNames enum comments,
// globals.h).
static const TbPixel land_preview_minimap_colours[LPMC_COUNT] = {
    [LPMC_Rock]        = { 18, 14, 10, 255},
    [LPMC_Dirt]        = { 86, 58, 30, 255},
    [LPMC_Path]        = {150,128, 64, 255},
    [LPMC_Wall]        = {100, 92, 84, 255},
    [LPMC_RoomNeutral] = { 90, 86,110, 255},
    [LPMC_Obstacle]    = { 55, 55, 55, 255},
    [LPMC_Gold]        = {218,178, 42, 255},
    [LPMC_Gems]        = {214, 84,178, 255},
    [LPMC_Lava]        = {196, 74, 24, 255},
    [LPMC_Water]       = { 42, 92,158, 255},
    [LPMC_Player0]     = {196, 40, 40, 255}, // red
    [LPMC_Player1]     = { 40, 90,196, 255}, // blue
    [LPMC_Player2]     = { 60,170, 70, 255}, // green
    [LPMC_Player3]     = {206,190, 40, 255}, // yellow
    [LPMC_PlayerGood]  = {220,220,220, 255}, // white
    [LPMC_Player4]     = {150, 60,190, 255}, // purple
    [LPMC_Player5]     = { 30, 30, 30, 255}, // black
    [LPMC_Player6]     = {220,130, 30, 255}, // orange
};

static enum LandPreviewMinimapColor land_preview_minimap_owner_color(unsigned char owner)
{
    switch (owner)
    {
        case PLAYER0: return LPMC_Player0;
        case PLAYER1: return LPMC_Player1;
        case PLAYER2: return LPMC_Player2;
        case PLAYER3: return LPMC_Player3;
        case PLAYER_GOOD: return LPMC_PlayerGood;
        case PLAYER4: return LPMC_Player4;
        case PLAYER5: return LPMC_Player5;
        case PLAYER6: return LPMC_Player6;
        default: return LPMC_RoomNeutral;
    }
}

void land_preview_free_minimap(void)
{
    if (land_preview_minimap_kind != NULL)
    {
        free(land_preview_minimap_kind);
        land_preview_minimap_kind = NULL;
    }
    if (land_preview_minimap_owner != NULL)
    {
        free(land_preview_minimap_owner);
        land_preview_minimap_owner = NULL;
    }
    land_preview_minimap_map_w = 0;
    land_preview_minimap_map_h = 0;
}

/** Reads a level's own map%05u.slb (terrain kind per slab) and
 * map%05u.own (owner per subtile) files directly off disk into scratch
 * buffers, for levels with no land_view/land_window art of their own --
 * the common case for individual freeplay levels, since that art is
 * authored per campaign, not per level (front_landview.c's
 * load_map_and_window already falls back campaign-start-art-wards when a
 * level doesn't have its own, but freeplay levels commonly belong to no
 * land-art-bearing campaign at all).
 *
 * Deliberately does NOT call load_map_slab_file()/load_map_ownership_file()
 * (kfx_sim/lvl_filesdk1.c): those write into kfx_sim_state's single live
 * slabmap/map arrays, which is unsafe to do just for browsing a preview.
 * This function never touches kfx_sim_state -- only get_slab_kind_stats()
 * (static per-kind category/fill_style/block_flags classification).
 */
TbBool land_preview_build_minimap(LevelNumber lvnum)
{
    land_preview_free_minimap();

    struct LevelInformation *lvinfo = get_level_info(lvnum);
    long map_w = ((lvinfo != NULL) && (lvinfo->mapsize_x > 0)) ? lvinfo->mapsize_x : DEFAULT_MAP_SIZE;
    long map_h = ((lvinfo != NULL) && (lvinfo->mapsize_y > 0)) ? lvinfo->mapsize_y : DEFAULT_MAP_SIZE;

    int32_t slb_fsize = 2 * (int32_t)map_w * (int32_t)map_h;
    unsigned char *slb_buf = load_single_map_file_to_buffer(lvnum, "slb", &slb_fsize, LMFF_None);
    if (slb_buf == NULL)
        return false;

    long subtiles_x = map_w * STL_PER_SLB;
    long subtiles_y = map_h * STL_PER_SLB;
    int32_t own_fsize = (int32_t)((subtiles_x + 1) * (subtiles_y + 1));
    unsigned char *own_buf = load_single_map_file_to_buffer(lvnum, "own", &own_fsize, LMFF_Optional);

    land_preview_minimap_kind = malloc((size_t)map_w * (size_t)map_h * sizeof(unsigned short));
    land_preview_minimap_owner = malloc((size_t)map_w * (size_t)map_h);
    if ((land_preview_minimap_kind == NULL) || (land_preview_minimap_owner == NULL))
    {
        land_preview_free_minimap();
        free(slb_buf);
        if (own_buf != NULL)
            free(own_buf);
        return false;
    }
    land_preview_minimap_map_w = map_w;
    land_preview_minimap_map_h = map_h;

    for (long y = 0; y < map_h; y++)
    {
        for (long x = 0; x < map_w; x++)
        {
            long slb_i = y * map_w + x;
            unsigned short kind = (unsigned short)(slb_buf[slb_i*2] | (slb_buf[slb_i*2+1] << 8));
            land_preview_minimap_kind[slb_i] = kind;
            unsigned char owner = PLAYER_NEUTRAL;
            if (own_buf != NULL)
            {
                long stl_x = x * STL_PER_SLB + 1;
                long stl_y = y * STL_PER_SLB + 1;
                owner = own_buf[stl_y * (subtiles_x + 1) + stl_x];
            }
            land_preview_minimap_owner[slb_i] = owner;
        }
    }

    free(slb_buf);
    if (own_buf != NULL)
        free(own_buf);
    return true;
}

static enum LandPreviewMinimapColor land_preview_minimap_slab_color(long x, long y)
{
    long slb_i = y * land_preview_minimap_map_w + x;
    unsigned short kind = land_preview_minimap_kind[slb_i];
    unsigned char owner = land_preview_minimap_owner[slb_i];
    struct SlabConfigStats *slabst = get_slab_kind_stats(kind);
    if ((kind == SlbT_GOLD) || (kind == SlbT_DENSEGOLD))
        return LPMC_Gold;
    if (kind == SlbT_GEMS)
        return LPMC_Gems;
    if (slabst->fill_style == SlbFillStl_Lava)
        return LPMC_Lava;
    if (slabst->fill_style == SlbFillStl_Water)
        return LPMC_Water;
    if (slabst->category == SlbAtCtg_RoomInterior)
        return land_preview_minimap_owner_color(owner);
    if (slabst->category == SlbAtCtg_FortifiedWall)
        return LPMC_Wall;
    if (slabst->category == SlbAtCtg_FortifiedGround)
        return (owner == PLAYER_NEUTRAL) ? LPMC_Path : land_preview_minimap_owner_color(owner);
    if (slabst->category == SlbAtCtg_FriableDirt)
        return LPMC_Dirt;
    if (slabst->category == SlbAtCtg_Obstacle)
        return LPMC_Obstacle;
    return LPMC_Rock;
}

// docs/refactor/editor/phase3/02-slice3-dialogs-menubar.md -- the Open Map
// dialog (kfx_editor/editor_dialogs.cpp) draws its own lightweight thumbnail
// per row via ImGui's draw list (a handful of filled rects, not the full
// ornate-frame/pan/zoom LandPreviewPanel machinery this file's own
// land_preview_load() drives) -- these three thin exports are all it needs:
// the minimap's dimensions and an RGB lookup per slab, reusing this file's
// own colour tables so a thumbnail matches the real in-game minimap
// palette. Width/height are 0 if land_preview_build_minimap() hasn't been
// called yet (or failed) -- callers should check before indexing.
long land_preview_minimap_width(void)
{
    return land_preview_minimap_map_w;
}

long land_preview_minimap_height(void)
{
    return land_preview_minimap_map_h;
}

void land_preview_minimap_pixel_rgb(long x, long y, unsigned char *r, unsigned char *g, unsigned char *b)
{
    enum LandPreviewMinimapColor color = LPMC_Rock;
    if ((land_preview_minimap_kind != NULL) && (land_preview_minimap_owner != NULL)
        && (x >= 0) && (x < land_preview_minimap_map_w) && (y >= 0) && (y < land_preview_minimap_map_h))
    {
        color = land_preview_minimap_slab_color(x, y);
    }
    const TbPixel *px = &land_preview_minimap_colours[color];
    *r = px->r;
    *g = px->g;
    *b = px->b;
}

/******************************************************************************/
TbBool land_preview_load(struct LandPreviewPanel *panel, LevelNumber target_lvnum, TbBool show_ensigns)
{
    // Release any previously-loaded ensign sheet before loading a new one
    // -- load_map_ensign_sprites() unconditionally reassigns map_flag via
    // load_spritesheet(), which would otherwise orphan it. Deliberately
    // NOT calling unload_map_and_window() here: that also clears gameplay
    // state (clear_light_system/clear_things_and_persons_data/clear_mapmap/
    // clear_computer/clear_slabs/clear_rooms/clear_dungeons) meant for a
    // genuine "leaving gameplay, entering land-view" transition -- calling
    // it on every list click (browsing, not leaving the screen) would run
    // that gameplay-state teardown many times in a row for no reason.
    // That full teardown only happens in land_preview_unload, called once
    // from frontend_shutdown_state when actually leaving the screen.
    free_spritesheet(&map_flag);
    TbBool minimap_mode = false;
    if (load_map_and_window(target_lvnum))
    {
        // Keep the land's own just-loaded palette independent of
        // frontend_palette -- see struct LandPreviewPanel.land_palette's
        // comment. Restore frontend_palette's buffer to the standard
        // frontend palette immediately so it reads correctly everywhere
        // else (including the *next* load_map_and_window call's own
        // backup step).
        memcpy(panel->land_palette, frontend_palette, PALETTE_SIZE);
        memcpy(frontend_palette, frontend_backup_palette, PALETTE_SIZE);
    }
    else
    {
        // No land_view/land_window art for this level (or its campaign)
        // -- the common case for an individual freeplay level, see
        // land_preview_build_minimap's comment.
        if (!land_preview_build_minimap(target_lvnum))
            return false;
        minimap_mode = true;
    }
    if (!load_map_ensign_sprites())
        return false; // map_screen/map_window stay loaded (harmless -- overwritten by the next load, or released by land_preview_unload on screen exit)
    update_ensigns_visibility();
    // update_ensigns_visibility only marks a level visible via continue-
    // progress (or shows every level once SINGLEPLAYER_FINISHED) -- a
    // freshly-browsed campaign with no progress yet leaves every ensign
    // hidden, so browsing it here would show a blank map. Always show at
    // least the campaign's own first level.
    {
        LevelNumber first_lvnum = first_singleplayer_level();
        struct LevelInformation *first_lvinfo = get_level_info(first_lvnum);
        if (first_lvinfo != NULL)
            first_lvinfo->state = LvSt_Visible;
    }
    initialize_description_speech();
    mouse_over_lvnum = SINGLEPLAYER_NOTSTARTED;
    panel->screen_shift_x = 0;
    panel->screen_shift_y = 0;
    panel->units_per_px = 16;
    panel->dragging = false;
    panel->highlighted_lvnum = SINGLEPLAYER_NOTSTARTED;
    // Only the campaign-overview picture (show_ensigns) has ensign
    // positions worth centring on -- a specific level's own land_view
    // image (show_ensigns == false) has none (struct LandPreviewPanel's
    // own show_ensigns comment).
    panel->pending_center_lvnum = show_ensigns
        ? get_next_singleplayer_level_for_landview() : SINGLEPLAYER_NOTSTARTED;
    panel->show_ensigns = show_ensigns;
    panel->minimap_mode = minimap_mode;
    panel->loaded = true;
    return true;
}

void land_preview_unload(struct LandPreviewPanel *panel)
{
    if (!panel->loaded)
        return;
    stop_description_speech();
    free_spritesheet(&map_flag);
    // unload_map_and_window() assumes a real load_map_and_window() call
    // happened (it restores frontend_palette from frontend_backup_palette,
    // which the minimap path never touches/sets, and runs gameplay-state
    // teardown -- clear_light_system/clear_things_and_persons_data/etc --
    // meant for a genuine "leaving loaded land-view" transition).
    if (panel->minimap_mode)
        land_preview_free_minimap();
    else
        unload_map_and_window();
    panel->loaded = false;
}

/** Finds the visible level whose ensign is at panel-relative point (rel_x,rel_y), or NULL.
 * Always NULL when !panel->show_ensigns -- ensign_x/y is only meaningful
 * against the campaign overview image, not a single level's own art.
 */
static struct LevelInformation *land_preview_ensign_at(struct LandPreviewPanel *panel, long rel_x, long rel_y)
{
    if (!panel->show_ensigns)
        return NULL;
    long map_x = panel->screen_shift_x + rel_x * 16 / panel->units_per_px;
    long map_y = panel->screen_shift_y + rel_y * 16 / panel->units_per_px;
    const struct TbSprite *spr = get_map_ensign(EnsFullFlag);
    struct LevelInformation *lvinfo = get_first_level_info();
    while (lvinfo != NULL)
    {
        if ((lvinfo->lvnum != 0) && (lvinfo->state == LvSt_Visible))
        {
            if (land_preview_point_over_ensign_box(map_x, map_y, lvinfo->ensign_x, lvinfo->ensign_y, spr->SWidth, spr->SHeight))
                return lvinfo;
        }
        lvinfo = get_next_level_info(lvinfo);
    }
    return NULL;
}

void land_preview_maintain(struct GuiButton *gbtn)
{
    struct LandPreviewPanel *panel = &land_preview;
    if (!panel->loaded)
        return;
    // Inset from gbtn's own rect: land_preview_draw_ornate_frame's corners
    // are drawn around this smaller rect, in the margin the inset frees up
    // between it and gbtn's actual edges (see land_preview_frame_inset()).
    long frame_inset = land_preview_frame_inset();
    long rect_x = gbtn->scr_pos_x + frame_inset;
    long rect_y = gbtn->scr_pos_y + frame_inset;
    long rect_w = gbtn->width - 2*frame_inset;
    long rect_h = gbtn->height - 2*frame_inset;

    if (panel->minimap_mode)
    {
        // The synthesized minimap is stretched independently on each axis
        // to exactly fill the panel rect (land_preview_draw) -- no
        // scrolling, panning, or letterboxing needed, unlike the
        // campaign-art half-visible/pannable behaviour below (no ensigns
        // either: minimap_mode only ever loads with show_ensigns = false,
        // land_preview_load).
        panel->units_per_px = 16;
        panel->screen_shift_x = 0;
        panel->screen_shift_y = 0;
        panel->dragging = false;
        return;
    }

    panel->units_per_px = land_preview_compute_units_per_px(rect_w, rect_h);

    // One-shot: land_preview_load() flagged a level to centre on but
    // didn't know the real rect/zoom yet (this function runs every frame
    // with both) -- consume it once, here, before the general clamp below
    // (which would otherwise just re-clamp the stale (0,0) default).
    if (panel->pending_center_lvnum != SINGLEPLAYER_NOTSTARTED)
    {
        struct LevelInformation *centre_lvinfo = get_level_info(panel->pending_center_lvnum);
        if (centre_lvinfo != NULL)
        {
            long visible_w = rect_w * 16 / panel->units_per_px;
            long visible_h = rect_h * 16 / panel->units_per_px;
            panel->screen_shift_x = centre_lvinfo->ensign_x - visible_w / 2;
            panel->screen_shift_y = centre_lvinfo->ensign_y - visible_h / 2;
        }
        panel->pending_center_lvnum = SINGLEPLAYER_NOTSTARTED;
    }
    land_preview_clamp_shift(panel, rect_w, rect_h);

    long mouse_x = GetMouseX();
    long mouse_y = GetMouseY();
    TbBool mouse_in_rect = (mouse_x >= rect_x) && (mouse_x < rect_x + rect_w)
        && (mouse_y >= rect_y) && (mouse_y < rect_y + rect_h);

    if (right_button_clicked && mouse_in_rect)
    {
        right_button_clicked = 0;
        panel->highlighted_lvnum = SINGLEPLAYER_NOTSTARTED;
    }

    if (left_button_clicked && mouse_in_rect)
    {
        left_button_clicked = 0;
        struct LevelInformation *lvinfo = land_preview_ensign_at(panel, mouse_x - rect_x, mouse_y - rect_y);
        if (lvinfo != NULL)
        {
            panel->highlighted_lvnum = lvinfo->lvnum;
            mouse_over_lvnum = lvinfo->lvnum;
            play_description_speech(lvinfo->lvnum, 1);
        } else
        {
            panel->dragging = true;
            panel->drag_last_x = mouse_x;
            panel->drag_last_y = mouse_y;
        }
    }

    if (panel->dragging)
    {
        // lbDisplay.LeftButton is a one-shot "pressed this frame" flag --
        // update_mouse() (kjm_input.c) zeroes it right after capturing it
        // into left_button_clicked each frame, so it reads false on every
        // frame after the initial press and would cancel the drag
        // immediately. lbDisplay.MLeftButton is the continuous "currently
        // held" state gui_vscroll.c's own drag-the-thumb code already
        // relies on for the same reason.
        if (!lbDisplay.MLeftButton)
        {
            panel->dragging = false;
        } else
        {
            long dx = mouse_x - panel->drag_last_x;
            long dy = mouse_y - panel->drag_last_y;
            panel->screen_shift_x -= dx * 16 / panel->units_per_px;
            panel->screen_shift_y -= dy * 16 / panel->units_per_px;
            land_preview_clamp_shift(panel, rect_w, rect_h);
            panel->drag_last_x = mouse_x;
            panel->drag_last_y = mouse_y;
        }
    }

    // Hover tracking for get_ensign_sprite_for_level()'s highlight-frame
    // animation, mirroring frontmap_input_active_ensign's continuous scan.
    if (!panel->dragging)
    {
        struct LevelInformation *hover = mouse_in_rect ? land_preview_ensign_at(panel, mouse_x - rect_x, mouse_y - rect_y) : NULL;
        mouse_over_lvnum = (hover != NULL) ? hover->lvnum : SINGLEPLAYER_NOTSTARTED;
    }
}

void land_preview_draw(struct GuiButton *gbtn)
{
    struct LandPreviewPanel *panel = &land_preview;
    if (!panel->loaded)
        return;
    long frame_inset = land_preview_frame_inset();
    long rect_x = gbtn->scr_pos_x + frame_inset;
    long rect_y = gbtn->scr_pos_y + frame_inset;
    long rect_w = gbtn->width - 2*frame_inset;
    long rect_h = gbtn->height - 2*frame_inset;
    int upp = panel->units_per_px;
    long pan_x = -((panel->screen_shift_x * upp + 8) / 16);
    long pan_y = -((panel->screen_shift_y * upp + 8) / 16);

    if (panel->minimap_mode)
    {
        // Direct TbPixel fills, one LbDrawBox per visible slab -- no
        // intermediate indexed buffer, no palette lookup, unlike the
        // real-art path below (see docs/refactor/renderer/00-overview.md's
        // cross-reference note: this is the concrete payoff of the
        // true-colour renderer for this screen). Each axis is stretched
        // independently to exactly fill rect_w x rect_h (cumulative
        // fractional tiling, same idiom copy_raw8_image_buffer uses for
        // its own dst-size scaling) rather than preserving the map's
        // aspect ratio, so the minimap always fills its bounding box.
        long map_w = land_preview_minimap_map_w;
        long map_h = land_preview_minimap_map_h;
        if ((map_w > 0) && (map_h > 0))
        {
            for (long y = 0; y < map_h; y++)
            {
                long y0 = rect_y + rect_h * y / map_h;
                long y1 = rect_y + rect_h * (y + 1) / map_h;
                if (y1 <= y0)
                    y1 = y0 + 1;
                for (long x = 0; x < map_w; x++)
                {
                    long x0 = rect_x + rect_w * x / map_w;
                    long x1 = rect_x + rect_w * (x + 1) / map_w;
                    if (x1 <= x0)
                        x1 = x0 + 1;
                    enum LandPreviewMinimapColor color = land_preview_minimap_slab_color(x, y);
                    LbDrawBox(x0, y0, x1 - x0, y1 - y0, land_preview_minimap_colours[color]);
                }
            }
        }
    }
    else
    {
        // Backdrop: same panned-oversized-image-at-a-negative-offset
        // technique draw_map_screen() uses, but clipped to the panel's
        // own rect instead of the whole screen buffer. Active palette is
        // swapped to the land's own for this blit AND the ensign sprites
        // drawn below -- the original single-screen cutscene draws both
        // under one shared active palette (the land's own, made active via
        // fade_in()), and the ensign flag art was authored against that
        // same palette (its "red"/"blue" indices aren't necessarily red/
        // blue when resolved against the standard frontend palette).
        // copy_raw8_image_buffer_rect/LbSpriteDrawResized both resolve
        // their source bytes to a real TbPixel at call time, so restoring
        // to frontend_palette right after this whole block (not right
        // after just the blit) can't corrupt anything drawn before or
        // after, on this frame or any other.
        long dst_w = (LANDVIEW_MAP_WIDTH * upp + 8) / 16;
        long dst_h = (LANDVIEW_MAP_HEIGHT * upp + 8) / 16;
        RendererPaletteSet(panel->land_palette);
        copy_raw8_image_buffer_rect(RendererGetFramebuffer(), LbGraphicsScreenWidth(), LbGraphicsScreenHeight(),
            rect_x, rect_y, rect_w, rect_h,
            dst_w, dst_h, pan_x, pan_y,
            map_screen, LANDVIEW_MAP_WIDTH, LANDVIEW_MAP_HEIGHT);

        // Ensigns, same draw order (last-to-first) as draw_map_level_ensigns(),
        // positioned relative to the panel's own rect instead of the screen.
        // Skipped entirely when !show_ensigns (a single level's own land_view
        // image, not the campaign overview ensign_x/y was authored against).
        if (panel->show_ensigns)
        {
            int anim_frame = LbTimerClock() / 200;
            struct LevelInformation *lvinfo = get_last_level_info();
            while (lvinfo != NULL)
            {
                const struct TbSprite *spr = get_ensign_sprite_for_level(lvinfo, anim_frame);
                if (spr != NULL)
                {
                    long map_x = lvinfo->ensign_x - panel->screen_shift_x - (spr->SWidth >> 1);
                    long map_y = lvinfo->ensign_y - panel->screen_shift_y - spr->SHeight;
                    long scr_x = rect_x + (map_x * upp + 8) / 16;
                    long scr_y = rect_y + (map_y * upp + 8) / 16;
                    // Full containment on every edge, not partial overlap:
                    // an ensign panned so only part of it crosses the
                    // border must vanish outright, not draw in full and
                    // float outside the panel on the ambient backdrop.
                    if ((scr_x >= rect_x) && (scr_x + spr->SWidth*upp/16 <= rect_x + rect_w)
                     && (scr_y >= rect_y) && (scr_y + spr->SHeight*upp/16 <= rect_y + rect_h))
                        LbSpriteDrawResized(scr_x, scr_y, upp, spr);
                }
                lvinfo = get_prev_level_info(lvinfo);
            }
        }
        RendererPaletteSet(frontend_palette);
    }

    // Ornate dragon-head corner frame -- built from get_button_sprite()'s
    // frontend-loaded GBS_ sheet, drawn last so it sits on top of the
    // backdrop/ensigns.
    land_preview_draw_ornate_frame(rect_x, rect_y, rect_w, rect_h);
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
