/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontmenu_select.c
 *     GUI menus for level and campaign select screens.
 * @par Purpose:
 *     Structures to show and maintain menus used for level and campaign list screens.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     07 Dec 2012 - 11 Aug 2014
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "frontmenu_select.h"
#include "globals.h"
#include "bflib_basics.h"

#include "bflib_guibtns.h"
#include "bflib_sprite.h"
#include "bflib_sprfnt.h"
#include "bflib_video.h"
#include "bflib_vidraw.h"
#include "config_strings.h"
#include "game_saves.h"
#include "game_campaign_progress.h" // Skirmish/Free Play completion tracking
#include "gui_draw.h"
#include "gui_frontbtns.h"
#include "gui_soundmsgs.h"
#include "player_data.h"
#include "packets.h"
#include "frontend.h"
#include "frontmenu_selectlist.h"
#include "frontmenu_landpreview.h"
#include "front_landview.h"
#include "sprites.h"
#include "front_network.h"
#include "game_legacy.h"
#include "kfx_sim_state.h"
#include "kjm_input.h"
#include "highscores.h"
#include "bflib_sndlib.h" // StopAllSamples, stop_music, set_music_volume
#include "bflib_sound.h"  // play_sample, get_emitter_id, S3DGetSoundEmitter, Non3DEmitter, NORMAL_PITCH
#include "config_settings.h" // settings.music_volume
#include "config_keeperfx.h" // features_enabled, Ft_AdvAmbSound
#include "skirmish_setup.h"
#include "level_script_override.h"
#include "net_game.h"
#include "post_inc.h"

#include <string.h> // strcmp

/******************************************************************************/
static int64_t frontend_level_select_count(void);
static int64_t frontend_campaign_select_count(void);
static int64_t frontend_mappack_select_count(void);
static int64_t frontend_mp_mappack_select_count(void);

int64_t number_of_freeplay_levels = 0;

// level_select_list uses its own row_base (FE_LEVEL_SELECTLIST_ROW_BASE,
// frontmenu_select.h) rather than the shared FE_SELECTLIST_ROW_BASE -- it's
// the only list ever visible alongside another one (mappack_select_list,
// on the merged Free play screen) and needs non-overlapping content.lval
// values to avoid cross-list hover-highlight bleed; see
// struct FrontendSelectList's row_base comment.
static struct FrontendSelectList level_select_list =
    { .items_visible_max = frontend_select_level_items_max_visible, .item_count = frontend_level_select_count, .row_base = FE_LEVEL_SELECTLIST_ROW_BASE };
static struct FrontendSelectList campaign_select_list =
    { .items_visible_max = frontend_select_campaign_items_max_visible, .item_count = frontend_campaign_select_count, .row_base = FE_SELECTLIST_ROW_BASE };
static struct FrontendSelectList mappack_select_list =
    { .items_visible_max = frontend_select_mappack_items_max_visible, .item_count = frontend_mappack_select_count, .row_base = FE_SELECTLIST_ROW_BASE };
static struct FrontendSelectList mp_mappack_select_list =
    { .items_visible_max = frontend_select_mp_mappack_items_max_visible, .item_count = frontend_mp_mappack_select_count, .row_base = FE_SELECTLIST_ROW_BASE };

// Land selection screen (merged campaign list + interactive land preview):
// which campaign the list is currently highlighting -- NULL until
// frontend_campaign_list_load/frontend_campaign_select first sets it.
// land_preview.highlighted_lvnum (frontmenu_landpreview.h) tracks the
// finer-grained "which level's ensign was clicked" on top of this.
// static dropped (frontmenu_select.h) -- the ImGui screen (frontgui_screens.cpp)
// reads this directly to render the list's selection state and the detail
// panel's name/description, the same single-source-of-truth reasoning
// Phase D used for e.g. sound_volume_ctrl.
struct GameCampaign *land_selection_highlighted_campaign = NULL;

// Merged Free play screen (mappack list + level list + land preview):
// which mappack the list is currently highlighting, and which level
// within it -- both NULL/0 until frontend_mappack_list_load/
// frontend_mappack_select/frontend_level_select first set them. Unlike
// Land selection, land_preview.highlighted_lvnum isn't reused for this:
// that field tracks an ensign clicked *inside* the preview panel, and
// this screen's preview never shows ensigns (see
// LandPreviewPanel.show_ensigns) -- level highlight comes from the level
// list instead. static dropped -- see land_selection_highlighted_campaign's
// comment above.
struct GameCampaign *freeplay_highlighted_mappack = NULL;
LevelNumber freeplay_highlighted_level = 0;

static int64_t frontend_level_select_count(void)
{
    return number_of_freeplay_levels;
}

static int64_t frontend_campaign_select_count(void)
{
    return campaigns_list.items_num;
}

static int64_t frontend_mappack_select_count(void)
{
    return mappacks_list.items_num;
}

static int64_t frontend_mp_mappack_select_count(void)
{
    return mp_mappacks_list.items_num;
}

/** Starts previewing the currently-loaded campaign's theme (SOUNDTRACK
 * music + LAND_AMBIENT good/bad loops from its .cfg) -- found live: since
 * the highlight/commit split landed (change_campaign() on every row
 * click, actually entering Land View only on commit), highlighting a
 * campaign/mappack in the list no longer triggers either, because both
 * were only ever wired into frontmap_load() (front_landview.c), which now
 * only runs once the user commits. Same start sequence frontmap_load()
 * itself uses; same stop sequence frontmap_unload() uses, called first so
 * repeated highlighting (browsing the list) replaces the previous
 * campaign's theme instead of layering on top of it. Shared by both
 * Land selection (frontend_campaign_select_by_index) and Free play
 * (freeplay_highlight_mappack) -- deliberately NOT called from Free
 * play's per-level highlight (frontend_level_select_by_index): the theme
 * is a mappack-level property, restarting it on every level click within
 * the same mappack would be disruptive rather than helpful.
 */
static void frontend_play_campaign_preview_audio(void)
{
    StopAllSamples();
    stop_music(false);
    set_music_volume(settings.music_volume);
    frontmap_start_music();
    if ((features_enabled & Ft_AdvAmbSound) != 0)
    {
        SoundEmitterID emit_id = get_emitter_id(S3DGetSoundEmitter(Non3DEmitter));
        play_sample(emit_id, campaign.ambient_good, 0, 0x40, NORMAL_PITCH, -1, 2);
        play_sample(emit_id, campaign.ambient_bad, 0, 0x40, NORMAL_PITCH, -1, 2);
    }
}
/******************************************************************************/
void frontend_level_select_up(struct GuiButton *gbtn)
{
    frontend_selectlist_scroll_up(&level_select_list);
}

void frontend_level_select_down(struct GuiButton *gbtn)
{
    frontend_selectlist_scroll_down(&level_select_list);
}

void frontend_level_select_scroll(struct GuiButton *gbtn)
{
    frontend_selectlist_scroll_to_offset(&level_select_list, gbtn);
}

void frontend_level_select_up_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_up_maintain(&level_select_list, gbtn);
}

void frontend_level_select_down_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_down_maintain(&level_select_list, gbtn);
}

void frontend_level_select_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_row_maintain(&level_select_list, gbtn);
}

void frontend_draw_level_select_button(struct GuiButton *gbtn)
{
    int64_t btn_idx = gbtn->content.lval;
    int64_t i = btn_idx + level_select_list.scroll_offset - level_select_list.row_base;
    uint64_t levels_count;
    LevelNumber *levels = frontend_freeplay_active_levels(&levels_count);
    int64_t lvnum = 0;
    if ((i >= 0) && (i < (int64_t)levels_count))
      lvnum = levels[i];
    struct LevelInformation* lvinfo = get_level_info(lvnum);
    if (lvinfo == NULL)
      return;
    if ((btn_idx > 0) && (frontend_mouse_over_button == btn_idx))
      i = 2;
    else
    if (get_level_highest_score(lvnum))
      i = 3;
    else
      i = 1;
    RendererSetDrawFlags(Lb_TEXT_HALIGN_LEFT);
    LbTextSetFont(frontend_font[i]);
    // This text is a bit condensed - button size is smaller than text height
    int64_t tx_units_per_px = (gbtn->height * 13 / 11) * 16 / LbTextLineHeight();
    i = LbTextLineHeight() * tx_units_per_px / 16;
    LbTextSetWindow(gbtn->scr_pos_x, gbtn->scr_pos_y, gbtn->width, i);
    if (lvinfo->name_stridx > 0)
    {
        LbTextDrawResized(0, 0, tx_units_per_px, get_string(lvinfo->name_stridx));
    }
    else
    {
        LbTextDrawResized(0, 0, tx_units_per_px, lvinfo->name);
    }
}

void frontend_draw_levels_scroll_tab(struct GuiButton *gbtn)
{
    // frontend_draw_scroll_tab (frontend.cpp) only ever draws the movable
    // thumb sprite -- never a groove/track of its own. The ornate
    // GFS_scrollbar_vert_ct_long/short groove sprite (what the classic
    // frontend_draw_scroll_box's own draw_scrollbar=true mode uses) reads
    // as a free-floating fragment sitting on its own in this screen's
    // narrower column, without the rest of that box's border around it --
    // a plain thin line reads better here, with no up/down arrows drawn at
    // all (see this screen's up/down buttons, which no longer have a
    // draw_call).
    frontend_draw_simple_scroll_track(gbtn);
    frontend_selectlist_draw_scroll_tab(&level_select_list, gbtn);
}

// Highlights the level at freeplay_levels index i: loads its own land
// preview (no ensigns -- see LandPreviewPanel.show_ensigns). Committing is
// frontend_freeplay_enter's job -- see the header. Shared the same way
// frontend_campaign_select_by_index is -- see its comment.
void frontend_level_select_by_index(int64_t i)
{
    uint64_t levels_count;
    LevelNumber *levels = frontend_freeplay_active_levels(&levels_count);
    int64_t lvnum = 0;
    if ((i >= 0) && (i < (int64_t)levels_count))
      lvnum = levels[i];
    if (lvnum <= 0)
        return;
    if (lvnum == freeplay_highlighted_level)
        return;
    freeplay_highlighted_level = lvnum;
    land_preview_load(&land_preview, lvnum, false);
}

void frontend_level_select(struct GuiButton *gbtn)
{
    int64_t i = frontend_selectlist_row_to_item_index(&level_select_list, gbtn);
    frontend_level_select_by_index(i);
}

void frontend_level_list_unload(void)
{
  // Nothing needs to be really unloaded; just menu cleanup here
  number_of_freeplay_levels = 0;
}

void frontend_level_list_load(void)
{
    uint64_t levels_count;
    frontend_freeplay_active_levels(&levels_count);
    number_of_freeplay_levels = levels_count;
    frontend_selectlist_set_visible(&level_select_list);
}

void frontend_level_select_update(void)
{
    frontend_selectlist_update(&level_select_list);
}

void frontend_draw_level_select_mappack(struct GuiButton *gbtn)
{
    const char *text;
    if (campaign.display_name[0] != '\0')
        text = campaign.display_name;
    else
        text = frontend_button_caption_text(gbtn);
    RendererSetDrawFlags(Lb_TEXT_HALIGN_LEFT);
    LbTextSetFont(frontend_font[2]);
    int64_t tx_units_per_px;
    tx_units_per_px = gbtn->height * 16 / LbTextLineHeight();
    LbTextSetWindow(gbtn->scr_pos_x, gbtn->scr_pos_y, gbtn->width, gbtn->height);
    LbTextDrawResized(0, 0, tx_units_per_px, text);
}

void frontend_campaign_select_up(struct GuiButton *gbtn)
{
    frontend_selectlist_scroll_up(&campaign_select_list);
}

void frontend_campaign_select_down(struct GuiButton *gbtn)
{
    frontend_selectlist_scroll_down(&campaign_select_list);
}

void frontend_campaign_select_scroll(struct GuiButton *gbtn)
{
    frontend_selectlist_scroll_to_offset(&campaign_select_list, gbtn);
}

void frontend_campaign_select_up_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_up_maintain(&campaign_select_list, gbtn);
}

void frontend_campaign_select_down_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_down_maintain(&campaign_select_list, gbtn);
}

void frontend_campaign_select_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_row_maintain(&campaign_select_list, gbtn);
}

void frontend_draw_campaign_select_button(struct GuiButton *gbtn)
{
    if (gbtn == NULL)
      return;
    int64_t btn_idx = gbtn->content.lval;
    int64_t i = frontend_selectlist_row_to_item_index(&campaign_select_list, gbtn);
    struct GameCampaign* campgn = NULL;
    if ((i >= 0) && (i < campaigns_list.items_num))
      campgn = &campaigns_list.items[i];
    if (campgn == NULL)
      return;
    if ((btn_idx > 0) && (frontend_mouse_over_button == btn_idx))
      i = 2;
    else
/*    if (campaign has been passed)
      i = 3;
    else*/
      i = 1;
    RendererSetDrawFlags(Lb_TEXT_HALIGN_LEFT);
    LbTextSetFont(frontend_font[i]);
    // This text is a bit condensed - button size is smaller than text height
    int64_t tx_units_per_px = (gbtn->height * 13 / 11) * 16 / LbTextLineHeight();
    i = LbTextLineHeight() * tx_units_per_px / 16;
    LbTextSetWindow(gbtn->scr_pos_x, gbtn->scr_pos_y, gbtn->width, i);
    LbTextDrawResized(0, 0, tx_units_per_px, campgn->display_name);
}

/** Highlights the campaign at list index i: loads its land preview and
 * resets the detail panel to the campaign's own description. Committing
 * is frontend_land_selection_enter's job -- see the header. Shared by the
 * legacy row click_event (frontend_campaign_select, below -- decodes i
 * from a row button's content.lval) and the ImGui screen
 * (frontgui_screens.cpp), which iterates campaigns_list directly and
 * already has a real index, with no row button to decode one from.
 */
void frontend_campaign_select_by_index(int64_t i)
{
    struct GameCampaign* campgn = NULL;
    if ((i >= 0) && (i < campaigns_list.items_num))
        campgn = &campaigns_list.items[i];
    if (campgn == NULL)
        return;
    if (campgn == land_selection_highlighted_campaign)
        return; // already highlighted, nothing to reload
    if (!change_campaign(CampgnT_Campaign, campgn->fname))
    {
        ERRORLOG("Unable to load campaign for land preview");
        return;
    }
    land_selection_highlighted_campaign = campgn;
    // land_preview_load() releases the previous ensign sheet itself and
    // doesn't need land_preview_unload() first -- that also tears down
    // gameplay state meant for actually leaving this screen (see its
    // comment), which running on every campaign click would be wasteful
    // and risk the crash-after-several-switches class of bug.
    land_preview_load(&land_preview, SINGLEPLAYER_NOTSTARTED, true);
    frontend_play_campaign_preview_audio();
}

void frontend_campaign_select(struct GuiButton *gbtn)
{
    if (gbtn == NULL)
        return;
    int64_t i = frontend_selectlist_row_to_item_index(&campaign_select_list, gbtn);
    frontend_campaign_select_by_index(i);
}

/** "Enter this land": commits whichever the detail panel is currently
 * showing -- the highlighted level within the highlighted campaign if an
 * ensign was clicked in the preview panel, otherwise the campaign's own
 * first level, same as the old immediate-commit-on-row-click behavior.
 */
// Neither of these unloads the land preview themselves --
// frontend_set_state always calls frontend_shutdown_state(<state being
// left>) first, which does it centrally for every way of leaving
// FeSt_CAMPAIGN_SELECT (these two buttons, but also e.g. an ESC handler
// added later), the same way FeSt_LAND_VIEW's frontmap_unload() already
// works.
/** frontend_land_selection_enter's actual work, minus the state-transition
 * call itself: returns the FrontendMenuState to transition to, or -1 if
 * there's nothing highlighted / starting the campaign failed. Split out
 * so the ImGui "Enter this land" button can request the transition itself
 * (frontend_set_state() is unsafe to call synchronously from inside an
 * active ImGui window -- see frontgui_screens.cpp's request_frontend_state
 * comment) while the legacy click_event below keeps calling
 * frontend_set_state() directly, unchanged.
 */
int64_t frontend_land_selection_enter_resolve(void)
{
    // Single-player-only entry point (Campaign Select's "Enter this land",
    // and Continue Game via frontend_load_continue_game_resolve() routing
    // here) -- must not inherit a stale fe_computer_players=1 left behind by
    // an earlier Skirmish match (frontend_freeplay_enter_resolve() below) or
    // Campaign-Editor test play (run_pending_content_tool_play(),
    // frontgui_screens.cpp): unlike those, this path reaches FeSt_START_KPRLEVEL
    // directly, bypassing FeSt_LAND_VIEW (frontmap_load()'s own reset,
    // front_landview.c) entirely. Set unconditionally, even on the early-return
    // below, so this stays correct regardless of what else changes here.
    fe_computer_players = 0;
    if (land_selection_highlighted_campaign == NULL)
        return -1;
    if (!frontend_start_new_campaign(land_selection_highlighted_campaign->fname))
    {
        ERRORLOG("Unable to start new campaign");
        return -1;
    }
    // FeSt_CAMPAIGN_INTRO immediately redirects to FeSt_LAND_VIEW
    // (frontend_setup_state's redirect table) -- the old full-screen
    // cutscene this screen replaces. What actually launches a level
    // without it is FeSt_START_KPRLEVEL, driven by
    // set_selected_level_number (not set_continue_level_number, which
    // only sets the *resume point* for "Continue Game") -- same call
    // clicked_map_level_ensign() used to make before triggering it via
    // the old zoom-in animation.
    LevelNumber lvnum = (land_preview.highlighted_lvnum != SINGLEPLAYER_NOTSTARTED)
        ? land_preview.highlighted_lvnum : first_singleplayer_level();
    set_selected_level_number(lvnum);
    return FeSt_START_KPRLEVEL;
}

void frontend_land_selection_enter(struct GuiButton *gbtn)
{
    int64_t next_state = frontend_land_selection_enter_resolve();
    if (next_state >= 0)
        frontend_set_state((FrontendMenuState)next_state);
}

void frontend_land_selection_return_to_main(struct GuiButton *gbtn)
{
    frontend_set_state(FeSt_MAIN_MENU);
}

/** A resizable panel background, genuinely independent of width/height
 * (unlike frontend_draw_scroll_box/_tab, which couples row height to
 * width -- see gui_draw_scroll_box's own comment). Reuses the same ornate
 * hugearea border art as the classic scroll box, at native scale, cropped
 * to this panel's own (narrower) rect -- see gui_draw_scroll_box_cropped's
 * comment -- instead of a flat opaque fill.
 */
void frontend_draw_land_selection_panel_bg(struct GuiButton *gbtn)
{
    gui_draw_scroll_box_cropped(gbtn, false);
}

/** Detail panel: the highlighted level's name+description if an ensign is
 * highlighted, otherwise the highlighted campaign's own.
 */
void frontend_draw_land_selection_detail(struct GuiButton *gbtn)
{
    const char *name = NULL;
    const char *description = NULL;
    if (land_preview.highlighted_lvnum != SINGLEPLAYER_NOTSTARTED)
    {
        struct LevelInformation *lvinfo = get_level_info(land_preview.highlighted_lvnum);
        if (lvinfo != NULL)
        {
            name = (lvinfo->name_stridx > 0) ? get_string(lvinfo->name_stridx) : lvinfo->name;
            description = lvinfo->description;
        }
    }
    if ((name == NULL) && (land_selection_highlighted_campaign != NULL))
    {
        name = land_selection_highlighted_campaign->display_name;
        description = land_selection_highlighted_campaign->description;
    }
    if (name == NULL)
        return;
    frontend_draw_land_selection_panel_bg(gbtn);
    RendererSetDrawFlags(Lb_TEXT_HALIGN_LEFT);
    // Fixed ~20px-tall reference line for the name, independent of
    // gbtn->height -- deriving scale from the *panel's* full height (as
    // frontend_draw_product_version-style code does for a short, single-
    // line button) stretches the text to fill this whole multi-line
    // panel, rendering it far too large. Description follows at half
    // that scale; LbTextDrawResized already word-wraps within
    // LbTextSetWindow's width, no extra wrapping needed.
    LbTextSetFont(frontend_font[1]);
    int64_t name_upp = (20 * 13 / 11) * 16 / LbTextLineHeight();
    int64_t name_line_h = LbTextLineHeight() * name_upp / 16;
    int64_t pad = 10 * name_upp / 16;
    int64_t inner_x = gbtn->scr_pos_x + pad;
    int64_t inner_w = gbtn->width - 2*pad;
    LbTextSetWindow(inner_x, gbtn->scr_pos_y + pad, inner_w, name_line_h);
    LbTextDrawResized(0, 0, name_upp, name);
    if ((description != NULL) && (description[0] != '\0'))
    {
        int64_t desc_upp = max(4, name_upp / 2);
        int64_t desc_y = gbtn->scr_pos_y + pad + name_line_h + (4 * name_upp / 16);
        int64_t desc_h = gbtn->scr_pos_y + gbtn->height - pad - desc_y;
        if (desc_h > 0)
        {
            LbTextSetWindow(inner_x, desc_y, inner_w, desc_h);
            LbTextDrawResized(0, 0, desc_upp, description);
        }
    }
}

// "Return to Main"/"Enter this land" are frontend_draw_button_icon
// buttons, same as the main menu's -- auto-sized to their own caption via
// frontend_menu_button_natural_width (frontend.h, the same helper
// frontend.cpp's main-menu buttons use), chained left-to-right from
// FE_LANDSEL_COL_X (not from FE_LANDSEL_PANEL_X, the preview column's own
// start -- anchoring the two from opposite ends of only ~384px risks
// overlap depending on their actual rendered width; starting both at the
// screen's left margin gives the full ~590px budget the main menu's own
// bottom row uses). Return sits on the left, Enter/Play on the right.
#define FE_LANDSEL_BOTTOMROW_GAP 24

void frontend_land_selection_enter_maintain(struct GuiButton *gbtn)
{
    int64_t units_per_px = simple_frontend_sprite_height_units_per_px(gbtn, GFS_hugebutton_a05l, 100);
    int64_t x = FE_LANDSEL_COL_X + frontend_menu_button_natural_width(FEBtn_MnuReturnToMain, units_per_px)
        + FE_LANDSEL_BOTTOMROW_GAP * units_per_px / 16;
    gbtn->width = frontend_menu_button_natural_width(FEBtn_MnuEnterLand, units_per_px);
    gbtn->pos_x = x;
    gbtn->scr_pos_x = x;
}

void frontend_land_selection_return_to_main_maintain(struct GuiButton *gbtn)
{
    int64_t units_per_px = simple_frontend_sprite_height_units_per_px(gbtn, GFS_hugebutton_a05l, 100);
    gbtn->width = frontend_menu_button_natural_width(FEBtn_MnuReturnToMain, units_per_px);
    gbtn->pos_x = FE_LANDSEL_COL_X;
    gbtn->scr_pos_x = FE_LANDSEL_COL_X;
}

// "Play this level": commits the highlighted level directly, same as the
// original (pre-merge) frontend_level_select did on click -- no
// intermediate cutscene for free play, unlike campaign levels.
// frontend_shutdown_state's FeSt_MAPPACK_SELECT case unloads the preview
// centrally (mirrors FeSt_CAMPAIGN_SELECT's), not this function.
void frontend_freeplay_enter(struct GuiButton *gbtn)
{
    int64_t next_state = frontend_freeplay_enter_resolve();
    if (next_state >= 0)
        frontend_set_state((FrontendMenuState)next_state);
}

void frontend_freeplay_return_to_main(struct GuiButton *gbtn)
{
    frontend_set_state(FeSt_MAIN_MENU);
}

/** frontend_freeplay_enter's actual work, minus the state-transition call
 * itself -- see frontend_land_selection_enter_resolve's comment for why.
 */
int64_t frontend_freeplay_enter_resolve(void)
{
    if (freeplay_highlighted_level <= 0)
        return -1;
    // docs/refactor/skirmish/: the Setup tab's edits become a one-shot script override
    // for exactly this level (installed if anything changed, cleared otherwise -- also
    // clears a stale one when this is ordinary Free play). Blocked outright while the
    // tab reports errors (e.g. a Replace with no win rule).
    if (frontend_freeplay_is_skirmish())
    {
        if (skirmish_setup_play_blocked(freeplay_highlighted_level))
            return -1;
        skirmish_setup_install_for_play(freeplay_highlighted_level);
    }
    else
    {
        level_script_override_clear();
        net_pending_external_seats_clear();
    }
    kfx_sim_state.selected_level_number = freeplay_highlighted_level;
    // Mirrors front_landview_multiplayer.c's own frontnetmap_update()
    // (`if (!fe_network_active) fe_computer_players = 1;`), the level-pick
    // commit Skirmish used to go through before it was routed directly
    // into this screen -- without it a skirmish level would start with no
    // computer opponents at all. Set explicitly either way (not just the
    // Skirmish branch) -- a plain, non-Skirmish Free play level is single-
    // dungeon and must not inherit a stale 1 left by an earlier Skirmish
    // visit this session.
    fe_computer_players = frontend_freeplay_is_skirmish() ? 1 : 0;
    return FeSt_START_KPRLEVEL;
}

// Auto-fit to caption + left-anchored pair, same pattern as Land
// selection's Enter/Return buttons -- Return on the left, Play on the right.
void frontend_freeplay_enter_maintain(struct GuiButton *gbtn)
{
    int64_t units_per_px = simple_frontend_sprite_height_units_per_px(gbtn, GFS_hugebutton_a05l, 100);
    int64_t x = FE_LANDSEL_COL_X + frontend_menu_button_natural_width(FEBtn_MnuReturnToMain, units_per_px)
        + FE_LANDSEL_BOTTOMROW_GAP * units_per_px / 16;
    gbtn->width = frontend_menu_button_natural_width(FEBtn_MnuPlayLevel, units_per_px);
    gbtn->pos_x = x;
    gbtn->scr_pos_x = x;
}

void frontend_freeplay_return_to_main_maintain(struct GuiButton *gbtn)
{
    int64_t units_per_px = simple_frontend_sprite_height_units_per_px(gbtn, GFS_hugebutton_a05l, 100);
    gbtn->width = frontend_menu_button_natural_width(FEBtn_MnuReturnToMain, units_per_px);
    gbtn->pos_x = FE_LANDSEL_COL_X;
    gbtn->scr_pos_x = FE_LANDSEL_COL_X;
}

/** Detail panel: the highlighted level's name + description. Unlike Land
 * selection's equivalent there's no campaign-level fallback -- Free play
 * always has a specific level highlighted (or none at all, if the
 * mappack has no levels).
 */
void frontend_draw_freeplay_detail(struct GuiButton *gbtn)
{
    if (freeplay_highlighted_level <= 0)
        return;
    struct LevelInformation *lvinfo = get_level_info(freeplay_highlighted_level);
    if (lvinfo == NULL)
        return;
    const char *name = (lvinfo->name_stridx > 0) ? get_string(lvinfo->name_stridx) : lvinfo->name;
    frontend_draw_land_selection_panel_bg(gbtn);
    RendererSetDrawFlags(Lb_TEXT_HALIGN_LEFT);
    LbTextSetFont(frontend_font[1]);
    int64_t name_upp = (20 * 13 / 11) * 16 / LbTextLineHeight();
    int64_t name_line_h = LbTextLineHeight() * name_upp / 16;
    int64_t pad = 10 * name_upp / 16;
    int64_t inner_x = gbtn->scr_pos_x + pad;
    int64_t inner_w = gbtn->width - 2*pad;
    LbTextSetWindow(inner_x, gbtn->scr_pos_y + pad, inner_w, name_line_h);
    LbTextDrawResized(0, 0, name_upp, name);
    if (lvinfo->description[0] != '\0')
    {
        int64_t desc_upp = max(4, name_upp / 2);
        int64_t desc_y = gbtn->scr_pos_y + pad + name_line_h + (4 * name_upp / 16);
        int64_t desc_h = gbtn->scr_pos_y + gbtn->height - pad - desc_y;
        if (desc_h > 0)
        {
            LbTextSetWindow(inner_x, desc_y, inner_w, desc_h);
            LbTextDrawResized(0, 0, desc_upp, lvinfo->description);
        }
    }
}

void frontend_campaign_select_update(void)
{
    frontend_selectlist_update(&campaign_select_list);
}

/** A plain thin vertical line behind the scroll thumb, for this screen's
 * narrower scroll column -- see frontend_draw_levels_scroll_tab's comment
 * for why the ornate groove sprite doesn't fit here.
 */
void frontend_draw_simple_scroll_track(struct GuiButton *gbtn)
{
    int64_t track_w = max(4, gbtn->width / 3);
    int64_t track_x = gbtn->scr_pos_x + (gbtn->width - track_w) / 2;
    LbDrawBox(track_x, gbtn->scr_pos_y, track_w, gbtn->height, TbPixel_RGB(0, 0, 0));
}

void frontend_draw_campaign_scroll_tab(struct GuiButton *gbtn)
{
    // See frontend_draw_levels_scroll_tab's comment.
    frontend_draw_simple_scroll_track(gbtn);
    frontend_selectlist_draw_scroll_tab(&campaign_select_list, gbtn);
}

/** Switches which mappack is highlighted: loads it (change_campaign),
 * refreshes the level list to its levels (restarting scroll at the top --
 * it's about to show a different set of levels than whatever the
 * previous mappack's list was scrolled to), and auto-highlights/previews
 * that mappack's first level so the panel isn't empty. Shared by the
 * mappack row's click_event and the screen's own entry point.
 */
static void freeplay_highlight_mappack(struct GameCampaign *campgn)
{
    enum CampaignTypes cmpgn_type = frontend_freeplay_is_skirmish() ? CampgnT_MultiplayerMappack : CampgnT_Mappack;
    if (!change_campaign(cmpgn_type, campgn->fname))
        return;
    freeplay_highlighted_mappack = campgn;
    level_select_list.scroll_offset = 0;
    frontend_level_list_load();
    freeplay_highlighted_level = 0;
    uint64_t levels_count;
    LevelNumber *levels = frontend_freeplay_active_levels(&levels_count);
    if (levels_count > 0)
    {
        LevelNumber lvnum = levels[0];
        freeplay_highlighted_level = lvnum;
        land_preview_load(&land_preview, lvnum, false);
    } else
    {
        land_preview_unload(&land_preview);
    }
    frontend_play_campaign_preview_audio();
}

// FeSt_MAPPACK_SELECT is shared by two entry points: the normal Free play
// button (Main Menu) and Skirmish (frontend_start_skirmish_resolve(),
// frontend.cpp) -- Skirmish sets net_service_index_selected before
// transitioning here the same way it already flags itself for the rest
// of the (now largely dead, see frontend_mp_mappack_select_resolve's own
// comment) old multiplayer-mappack-select codepath, so this screen reuses
// that same check to pick which campaign list/type to source from,
// rather than inventing a second flag for the same distinction.
TbBool frontend_freeplay_is_skirmish(void)
{
    return net_service_index_selected == FrontendNetSvc_Skirmish;
}

struct CampaignsList *frontend_freeplay_active_mappacks_list(void)
{
    return frontend_freeplay_is_skirmish() ? &mp_mappacks_list : &mappacks_list;
}

// The level list's own equivalent of frontend_freeplay_active_mappacks_list():
// Skirmish mappacks register their levels as LvKind_IsMulti (.lof KIND=MULTI),
// landing in campaign.multi_levels, not campaign.freeplay_levels (.lif files,
// and .lof KIND=FREE, only) -- found live, the merged screen's level list was
// hardcoded to freeplay_levels and came up empty for every Skirmish mappack.
// The old NETLAND_VIEW screen (front_landview_multiplayer.c's
// update_net_ensigns_visibility, via first_multiplayer_level()/
// next_multiplayer_level()) always read multi_levels for this reason.
LevelNumber *frontend_freeplay_active_levels(uint64_t *out_count)
{
    if (frontend_freeplay_is_skirmish())
    {
        *out_count = campaign.multi_levels_count;
        return campaign.multi_levels;
    }
    *out_count = campaign.freeplay_levels_count;
    return campaign.freeplay_levels;
}

// Runs on every entry into FeSt_MAPPACK_SELECT (frontend_setup_state calls
// it unconditionally, same as frontend_campaign_list_load for Land
// selection), not just this menu's first-ever creation.
void frontend_mappack_list_load(void)
{
    // Defensive reload: guarantees Skirmish/Free Play completion tracking
    // (game_campaign_progress.h's completed_levels[]) reflects prior
    // sessions regardless of whether progress.cfg's own startup load ran
    // before or after mappacks_list/mp_mappacks_list were populated --
    // cheap (small file), same "reload defensively" convention
    // campaign_progress_record_level_completed() already documents.
    load_campaign_progress_file();
    frontend_selectlist_set_visible(&mappack_select_list);
    freeplay_highlighted_mappack = NULL;
    freeplay_highlighted_level = 0;
    land_preview.loaded = false;
    struct CampaignsList *list = frontend_freeplay_active_mappacks_list();
    if (list->items_num > 0)
        freeplay_highlight_mappack(&list->items[0]);
}

void frontend_mappack_select_up(struct GuiButton *gbtn)
{
    frontend_selectlist_scroll_up(&mappack_select_list);
}

void frontend_mappack_select_down(struct GuiButton *gbtn)
{
    frontend_selectlist_scroll_down(&mappack_select_list);
}

void frontend_mappack_select_scroll(struct GuiButton *gbtn)
{
    frontend_selectlist_scroll_to_offset(&mappack_select_list, gbtn);
}

void frontend_mappack_select_up_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_up_maintain(&mappack_select_list, gbtn);
}

void frontend_mappack_select_down_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_down_maintain(&mappack_select_list, gbtn);
}

void frontend_mappack_select_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_row_maintain(&mappack_select_list, gbtn);
}

// Highlights the mappack at list index i (see freeplay_highlight_mappack)
// rather than committing/transitioning screens -- the merged Free play
// screen shows both lists together. Shared the same way
// frontend_campaign_select_by_index is -- see its comment.
void frontend_mappack_select_by_index(int64_t i)
{
    struct CampaignsList *list = frontend_freeplay_active_mappacks_list();
    struct GameCampaign *campgn = NULL;
    if ((i >= 0) && (i < list->items_num))
        campgn = &list->items[i];
    if (campgn == NULL)
        return;
    if (campgn == freeplay_highlighted_mappack)
        return; // already highlighted, nothing to reload
    freeplay_highlight_mappack(campgn);
}

void frontend_mappack_select(struct GuiButton *gbtn)
{
    if (gbtn == NULL)
        return;
    int64_t i = frontend_selectlist_row_to_item_index(&mappack_select_list, gbtn);
    frontend_mappack_select_by_index(i);
}


void frontend_mp_mappack_list_load(void)
{
    // See frontend_mappack_list_load(): entering multiplayer map-pack
    // select likewise swaps in a new freeplay level list, which must
    // restart at the top.
    level_select_list.scroll_offset = 0;
    frontend_selectlist_set_visible(&mp_mappack_select_list);
}

void frontend_mp_mappack_select_up(struct GuiButton *gbtn)
{
    frontend_selectlist_scroll_up(&mp_mappack_select_list);
}

void frontend_mp_mappack_select_down(struct GuiButton *gbtn)
{
    frontend_selectlist_scroll_down(&mp_mappack_select_list);
}

void frontend_mp_mappack_select_scroll(struct GuiButton *gbtn)
{
    frontend_selectlist_scroll_to_offset(&mp_mappack_select_list, gbtn);
}

void frontend_mp_mappack_select_up_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_up_maintain(&mp_mappack_select_list, gbtn);
}

void frontend_mp_mappack_select_down_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_down_maintain(&mp_mappack_select_list, gbtn);
}

void frontend_mp_mappack_select_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_row_maintain(&mp_mappack_select_list, gbtn);
}

// frontend_mp_mappack_select's actual work, minus the state-transition
// call itself -- see frontend_land_selection_enter_resolve's comment for
// why. Unlike Land selection/Free play, MP mappack select has no
// highlight/commit split (never did -- see frontmenu_select.h's own note
// on frontend_mp_mappack_select), so this is keyed by list index directly
// like the others but still commits immediately on call, same as the
// legacy click_event. Used only for real multiplayer sessions now --
// Skirmish (still identified the same way, net_service_index_selected ==
// FrontendNetSvc_Skirmish) no longer reaches FeSt_MP_MAPPACK_SELECT at
// all; it routes straight into the merged Free play screen instead (see
// frontend_start_skirmish_resolve(), frontend.cpp, and
// frontend_mappack_list_load()'s own comment below).
int64_t frontend_mp_mappack_select_resolve(int64_t i)
{
    struct GameCampaign *campgn = NULL;
    if ((i >= 0) && (i < mp_mappacks_list.items_num))
        campgn = &mp_mappacks_list.items[i];
    if (campgn == NULL)
        return -1;

    frontnet_send_campaign_change_message(campgn->fname);

    if (!change_campaign(CampgnT_MultiplayerMappack, campgn->fname))
        return -1;
    return FeSt_NET_START;
}

void frontend_mp_mappack_select(struct GuiButton *gbtn)
{
    if (gbtn == NULL)
        return;
    int64_t i = frontend_selectlist_row_to_item_index(&mp_mappack_select_list, gbtn);
    int64_t next_state = frontend_mp_mappack_select_resolve(i);
    if (next_state >= 0)
        frontend_set_state((FrontendMenuState)next_state);
}

int64_t frontend_back_from_mp_mappack_list_target(void)
{
    return FeSt_NET_START;
}

void frontend_back_from_mp_mappack_list(struct GuiButton *gbtn)
{
    frontend_set_state(frontend_back_from_mp_mappack_list_target());
}

void frontend_draw_mp_mappack_select_button(struct GuiButton *gbtn)
{
    struct GameCampaign *campgn;
    int64_t btn_idx;
    int64_t i;
    if (gbtn == NULL)
      return;
    btn_idx = gbtn->content.lval;
    i = frontend_selectlist_row_to_item_index(&mp_mappack_select_list, gbtn);
    campgn = NULL;
    if ((i >= 0) && (i < mp_mappacks_list.items_num))
      campgn = &mp_mappacks_list.items[i];
    if (campgn == NULL)
      return;
    if ((btn_idx > 0) && (frontend_mouse_over_button == btn_idx))
      i = 2;
    else
      i = 1;

    RendererSetDrawFlags(Lb_TEXT_HALIGN_LEFT);
    LbTextSetFont(frontend_font[i]);
    int64_t tx_units_per_px;
    // This text is a bit condensed - button size is smaller than text height
    tx_units_per_px = (gbtn->height*13/11) * 16 / LbTextLineHeight();
    i = LbTextLineHeight() * tx_units_per_px / 16;
    LbTextSetWindow(gbtn->scr_pos_x, gbtn->scr_pos_y, gbtn->width, i);
    LbTextDrawResized(0, 0, tx_units_per_px, campgn->display_name);
}

void frontend_draw_mappack_select_button(struct GuiButton *gbtn)
{
    struct GameCampaign *campgn;
    int64_t btn_idx;
    int64_t i;
    if (gbtn == NULL)
      return;
    btn_idx = gbtn->content.lval;
    i = frontend_selectlist_row_to_item_index(&mappack_select_list, gbtn);
    campgn = NULL;
    if ((i >= 0) && (i < mappacks_list.items_num))
      campgn = &mappacks_list.items[i];
    if (campgn == NULL)
      return;
    if ((btn_idx > 0) && (frontend_mouse_over_button == btn_idx))
      i = 2;
    else
      i = 1;

    RendererSetDrawFlags(Lb_TEXT_HALIGN_LEFT);
    LbTextSetFont(frontend_font[i]);
    int64_t tx_units_per_px;
    // This text is a bit condensed - button size is smaller than text height
    tx_units_per_px = (gbtn->height*13/11) * 16 / LbTextLineHeight();
    i = LbTextLineHeight() * tx_units_per_px / 16;
    LbTextSetWindow(gbtn->scr_pos_x, gbtn->scr_pos_y, gbtn->width, i);
    LbTextDrawResized(0, 0, tx_units_per_px, campgn->display_name);
}

void frontend_mappack_select_update(void)
{
    frontend_selectlist_update(&mappack_select_list);
}

void frontend_mp_mappack_select_update(void)
{
    frontend_selectlist_update(&mp_mappack_select_list);
}

void frontend_draw_mappack_scroll_tab(struct GuiButton *gbtn)
{
    // Same groove-behind-the-thumb story as frontend_draw_campaign_scroll_tab
    // -- see its comment.
    frontend_draw_simple_scroll_track(gbtn);
    frontend_selectlist_draw_scroll_tab(&mappack_select_list, gbtn);
}

void frontend_draw_mp_mappack_scroll_tab(struct GuiButton *gbtn)
{
    frontend_selectlist_draw_scroll_tab(&mp_mappack_select_list, gbtn);
}

void frontend_campaign_list_load(void)
{
    frontend_selectlist_set_visible(&campaign_select_list);
    land_selection_highlighted_campaign = NULL;
    land_preview.loaded = false;
    // Default to the first campaign so the land preview/detail panel isn't
    // empty on entry, same as an implicit first row click -- delegating to
    // frontend_campaign_select_by_index() itself (rather than duplicating
    // its body here) also means the first campaign's theme preview starts
    // automatically on entry, the same as clicking it would. This runs on
    // every entry into FeSt_CAMPAIGN_SELECT (frontend_setup_state calls it
    // unconditionally), not just the menu's first-ever creation, so it's
    // the right place for this rather than the GuiMenu's create_cb.
    //
    // BUT: get_menu_state_based_on_last_level() now sends a just-won/lost
    // campaign level straight back here (this screen replaces the old
    // FeSt_LAND_VIEW cutscene) -- always defaulting to index 0 would
    // silently swap the player to whichever campaign happens to load
    // first any time they're not actually playing that one (found while
    // wiring that reroute up). Re-highlight the campaign already active
    // (is_campaign_loaded()/campaign.fname) if it's still in this list,
    // so the screen picks up exactly where gameplay left off; only fall
    // back to index 0 when there's genuinely nothing loaded yet.
    int64_t idx = 0;
    if (is_campaign_loaded() && campaign.fname[0] != '\0')
    {
        for (int64_t i = 0; i < campaigns_list.items_num; i++)
        {
            if (strcmp(campaigns_list.items[i].fname, campaign.fname) == 0)
            {
                idx = i;
                break;
            }
        }
    }
    if (campaigns_list.items_num > 0)
        frontend_campaign_select_by_index(idx);
}

void frontend_draw_variable_mappack_exit_button(struct GuiButton *gbtn)
{
    int64_t str_idx = FEBtn_MnuReturnToFreePlay;
    int64_t mnu_idx = 34; //map pack selection screen
    if (mappacks_list.items_num == 1)
    {
        str_idx = FEBtn_MnuReturnToMain;
        mnu_idx = 1; //main menu
    }
    gbtn->btype_value = mnu_idx;
    gbtn->content.lval = str_idx;
    const char *text;
    text = frontend_button_caption_text(gbtn);
    frontend_draw_button(gbtn, 1, text, Lb_TEXT_HALIGN_CENTER);
}
/******************************************************************************/
