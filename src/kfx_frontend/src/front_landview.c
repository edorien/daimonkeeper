/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file front_landview.c
 *     Land view, where the user can select map for campaign or multiplayer.
 * @par Purpose:
 *     Functions for displaying and maintaining the land view.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     16 Mar 2009 - 01 Apr 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "front_landview.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_datetm.h"
#include "bflib_planar.h"
#include "bflib_video.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"
#include "bflib_filelst.h"
#include "bflib_sprite.h"
#include "bflib_sprfnt.h"
#include "bflib_mouse.h"
#include "bflib_math.h"
#include "bflib_sndlib.h"
#include "bflib_sound.h"
#include "bflib_vidraw.h"

#include "config_strings.h"
#include "config_campaigns.h"
#include "config_keeperfx.h"
#include "config_settings.h"
#include "game_merge.h"
#include "game_campaign_progress.h" // Phase B: unlocked_levels, new menu only
#include "light_data.h"
#include "lvl_filesdk1.h"
#include "room_list.h"
#include "engine_textures.h"
#include "front_simple.h"
#include "front_network.h"
#include "front_lvlstats.h"
#include "frontend.h"
#include "vidmode.h"
#include "gui_draw.h"
#include "kjm_input.h"
#include "vidmode.h"
#include "vidfade.h"
#include "game_legacy.h"
#include "kfx_frontend_state.h"
#include "front_input.h"
#include "net_game.h"
#include "game_lifecycle.h"
#include "custom_sprites.h"
#include "landview_image.h"
#include "ports/script_port.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define WINDOW_X_SIZE 960
#define WINDOW_Y_SIZE 720
#define LANDVIEW_ZOOM_STEP (2 * 3.3)
#define LANDVIEW_PAN_ACCEL (4 * 3.3)
#define LANDVIEW_PAN_DECEL (1 * 3.3)
#define LANDVIEW_PAN_MAX_SPEED (24 * 3.3)
#define LANDVIEW_SOUND_FADE_PERCENT 90 //90% leaves a silent margin before the hard stop
struct TbSprite dummy_sprite = {0, 0, 0};

LevelNumber mouse_over_lvnum;
LevelNumber playing_speech_lvnum;
struct TbHugeSprite map_window;
int64_t map_window_len = 0;

TbClockMSec play_desc_speech_time;
uint64_t played_bad_descriptive_speech;
uint64_t played_good_descriptive_speech;
struct TbSpriteSheet * map_flag = NULL;
struct TbSpriteSheet * map_font = NULL;
struct TbSpriteSheet * map_hand = NULL;
int64_t map_sound_fade;
unsigned char *map_screen;
char level_name[88];

// Moved from kfx_render's vidmode.c -- vidmode.c only ever defined this
// storage without using its fields; front_landview.c is the real
// reader/writer of every field. See
// docs/refactor/stage-13-enforce-and-document.md.
struct MapLevelInfo map_info;

static TbPixel netfont_palette_remap[PALETTE_COLORS];
static unsigned char netfont_source_palette[PALETTE_SIZE];
/******************************************************************************/
#ifdef __cplusplus
}
#endif
/******************************************************************************/
void draw_map_screen(void)
{
    if (map_screen == NULL)
    {
        ERRORLOG("Map screen buffer is not allocated");
        return;
    }

    copy_raw8_image_buffer(RendererGetFramebuffer(),LbGraphicsScreenWidth(),LbGraphicsScreenHeight(),
        scale_value_landview(LANDVIEW_MAP_WIDTH), scale_value_landview(LANDVIEW_MAP_HEIGHT),
        -scale_value_landview(map_info.screen_shift_x), -scale_value_landview(map_info.screen_shift_y),
        map_screen,LANDVIEW_MAP_WIDTH,LANDVIEW_MAP_HEIGHT);
}

TbBool init_netfont_palette_remap(void)
{
    const char *fname = prepare_file_path(FGrp_LandView, "rgmap00.pal");

    if (LbFileLoadAt(fname, netfont_source_palette) != PALETTE_SIZE)
    {
        ERRORLOG("Unable to load rgmap00.PAL for NETFONT");
        return false;
    }
    return true;
}

/* Upstream (#5253) snaps each source colour to the nearest index of frontend_palette
 * (LbPaletteFindColour) because its remap tables were index->index. Here a remap is a
 * 256-entry TbPixel lookup (see lbSpriteReMapPtr), so the source colour is used as-is. */
static void make_palette_remap(TbPixel *remap, const unsigned char *source_palette)
{
    for (int64_t i = 0; i < PALETTE_COLORS; i++)
    {
        remap[i] = TbPixel_RGB(
            chan6_to_8(source_palette[i * 3 + 0]),
            chan6_to_8(source_palette[i * 3 + 1]),
            chan6_to_8(source_palette[i * 3 + 2]));
    }
}

void pop_palette_remap(void){
    make_palette_remap(netfont_palette_remap, netfont_source_palette);
}

const struct TbSprite * get_map_ensign(int64_t idx)
{
    if (idx >= 0 && idx < num_sprites(map_flag)) {
        return get_sprite(map_flag, idx);
    }
    return &dummy_sprite;
}

/**
 * Determines if given coordinates are screen position which is over an ensign.
 * @param lvinfo Level info struct with ensign definition.
 * @param scr_x The screen point being checked, X coordinate.
 * @param scr_y The screen point being checked, Y coordinate.
 * @return True if the coords are over given ensign, false otherwise.
 */
int64_t is_over_ensign(const struct LevelInformation *lvinfo, int64_t scr_x, int64_t scr_y)
{
    int64_t map_x = map_info.screen_shift_x + scr_x * 16 / units_per_pixel_landview;
    int64_t map_y = map_info.screen_shift_y + scr_y * 16 / units_per_pixel_landview;
    const struct TbSprite* spr = get_map_ensign(EnsFullFlag);
    int64_t spr_w = spr->SWidth;
    int64_t spr_h = spr->SHeight;
    if ((map_x >= lvinfo->ensign_x-(spr_w>>1)) && (map_x < lvinfo->ensign_x+(spr_w>>1))
     && (map_y > lvinfo->ensign_y-spr_h) && (map_y < lvinfo->ensign_y-(spr_h/3)))
        return true;
    return false;
}

/**
 * Determines if given ensign is inside visible part of the map screen.
 * @param lvinfo Level info struct with ensign definition.
 * @return True if the ensign is visible, false otherwise.
 */
int64_t is_ensign_in_screen_rect(const struct LevelInformation *lvinfo)
{
    if ((lvinfo->ensign_zoom_x >= map_info.screen_shift_x) && (lvinfo->ensign_zoom_x < map_info.screen_shift_x+lbDisplay.PhysicalScreenWidth*16/units_per_pixel_landview))
      if ((lvinfo->ensign_zoom_y >= map_info.screen_shift_y) && (lvinfo->ensign_zoom_y < map_info.screen_shift_y+lbDisplay.PhysicalScreenHeight*16/units_per_pixel_landview))
        return true;
    return false;
}

/**
 * Changes state of all land map ensigns.
 */
void set_all_ensigns_state(int64_t nstate)
{
    struct LevelInformation* lvinfo = get_first_level_info();
    while (lvinfo != NULL)
    {
        if (lvinfo->lvnum != 0)
        {
            lvinfo->state = nstate;
        }
        lvinfo = get_next_level_info(lvinfo);
    }
}

// docs/refactor/gui/05-campaign-progress-and-landview.md §3.2 (Phase B):
// every level the player has actually completed stays visible/replayable,
// not just the single "continue" level -- a deliberate KeeperFX-only
// departure from original DK's strictly-linear design, which only ever
// showed the one next level (see this doc's own §2 for the as-shipped
// behaviour this replaces).
static void mark_ensign_visible_if_present(LevelNumber lvnum)
{
    struct LevelInformation *lvinfo = get_level_info(lvnum);
    if (lvinfo != NULL)
        lvinfo->state = LvSt_Visible;
}

void update_ensigns_visibility(void)
{
  struct LevelInformation *lvinfo;
  SYNCDBG(18,"Starting");
  set_all_ensigns_state(LvSt_Hidden);
  struct PlayerInfo* player = get_my_player();
  int64_t show_all_sp = false;
  int64_t lvnum = get_continue_level_number();
  struct CampaignProgressEntry *progress = get_campaign_progress(campaign.fname, false);
  if (progress != NULL)
  {
      for (uint64_t i = 0; i < progress->unlocked_levels_count; i++)
          mark_ensign_visible_if_present(progress->unlocked_levels[i]);
      if (progress->intralvl.next_level == SINGLEPLAYER_FINISHED)
          show_all_sp = true;
      else if (progress->intralvl.next_level > 0)
          mark_ensign_visible_if_present((LevelNumber)progress->intralvl.next_level);
  }
  else if (lvnum == SINGLEPLAYER_FINISHED)
  {
      // No progress.cfg entry yet for this campaign (e.g. reconciliation
      // hasn't run this session) but fx1contn.sav/get_continue_level_number()
      // already says finished -- fall back rather than showing nothing.
      show_all_sp = true;
  }
  lvnum = first_singleplayer_level();
  while (lvnum > 0)
  {
    if (show_all_sp)
    {
      lvinfo = get_level_info(lvnum);
      if (lvinfo != NULL)
        lvinfo->state = LvSt_Visible;
    }
    int64_t bn_lvnum = bonus_level_for_singleplayer_level(lvnum);
    if (is_bonus_level_visible(player, bn_lvnum))
    {
        lvinfo = get_level_info(bn_lvnum);
        if (lvinfo != NULL)
            lvinfo->state = LvSt_Visible;
    }
    lvnum = next_singleplayer_level(lvnum, true);
  }
  // Extra level - full moon
  lvnum = get_extra_level(ExLv_FullMoon);
  lvinfo = get_level_info(lvnum);
  if (lvinfo != NULL)
      lvinfo->state = get_extra_level_kind_visibility(ExLv_FullMoon);
  // Extra level - new moon
  lvnum = get_extra_level(ExLv_NewMoon);
  lvinfo = get_level_info(lvnum);
  if (lvinfo != NULL)
      lvinfo->state = get_extra_level_kind_visibility(ExLv_NewMoon);
}

// Same "which level is next" source of truth update_ensigns_visibility()
// above already uses for which ensign(s) to reveal, exposed as a single
// accessor so the small land-preview panel (frontmenu_landpreview.c) can
// centre its initial view on it too (live-tested: "campaign view, when a
// campaign is first selected, can the landview preview pane center on the
// next_level ensign"). SINGLEPLAYER_NOTSTARTED means "no single next
// level to centre on" (finished campaign / no progress yet) -- the panel
// keeps its own default view in that case.
LevelNumber get_next_singleplayer_level_for_landview(void)
{
    // No progress yet (fresh campaign, never played) used to fall back to
    // SINGLEPLAYER_NOTSTARTED, i.e. "don't centre at all" -- land_preview_
    // maintain() then left screen_shift_x/y at their zeroed default, which
    // reads as the view starting in the map's top corner instead of on any
    // level at all (live-tested request: "when no levels have been played
    // in a campaign it should centre on 1st available level"). Centring on
    // first_singleplayer_level() instead gives a sensible starting view
    // either way.
    struct CampaignProgressEntry *progress = get_campaign_progress(campaign.fname, false);
    if ((progress != NULL) && (progress->intralvl.next_level > 0)
     && (progress->intralvl.next_level != SINGLEPLAYER_FINISHED))
        return (LevelNumber)progress->intralvl.next_level;
    return first_singleplayer_level();
}

int64_t compute_sound_good_to_bad_factor(void)
{
    SYNCDBG(18, "Starting");
    uint64_t onscr_bad = 0;
    uint64_t onscr_good = 0;
    LevelNumber continue_lvnum = get_continue_level_number();
    int64_t lv_beaten = (continue_lvnum != SINGLEPLAYER_NOTSTARTED);
    LevelNumber sp_lvnum = first_singleplayer_level();
    while (sp_lvnum > 0)
    {
        if (sp_lvnum == continue_lvnum)
            lv_beaten = false;
        struct LevelInformation* lvinfo = get_level_info(sp_lvnum);
        if (lvinfo != NULL)
        {
            if (is_ensign_in_screen_rect(lvinfo))
            {
                if (lv_beaten)
                    onscr_bad++;
                else
                    onscr_good++;
            }
        }
        sp_lvnum = next_singleplayer_level(sp_lvnum, true);
    }
    if ((onscr_bad + onscr_good) == 0)
        onscr_good++;
    // return a value between 0 (all bad) and 256 (all good)
    return (FULL_LOUDNESS * onscr_good) / (onscr_bad + onscr_good);
}

void update_frontmap_ambient_sound(void)
{
    // NOTE: the good / bad samples start at a volume of zero.
    SoundEmitterID emit_id = get_emitter_id(S3DGetSoundEmitter(Non3DEmitter));
    if (map_sound_fade > 0)
    {
        int64_t lvidx = array_index_for_singleplayer_level(get_continue_level_number());
        if ((features_enabled & Ft_AdvAmbSound) != 0)
        {
            int64_t factor = compute_sound_good_to_bad_factor();
            SetSampleVolume(emit_id, campaign.ambient_good, (map_sound_fade * (((int64_t)volume_setting_curve(settings.sound_volume) * factor) / FULL_LOUDNESS)) / FULL_LOUDNESS);
            SetSampleVolume(emit_id, campaign.ambient_bad, (map_sound_fade * (((int64_t)volume_setting_curve(settings.sound_volume) * (FULL_LOUDNESS - factor)) / FULL_LOUDNESS)) / FULL_LOUDNESS);
        } else
        if (lvidx > 13)
        {
            SetSampleVolume(emit_id, campaign.ambient_bad, ((int64_t)volume_setting_curve(settings.sound_volume) * map_sound_fade) / FULL_LOUDNESS);
        } else
        {
        SetSampleVolume(emit_id, campaign.ambient_good, ((int64_t)volume_setting_curve(settings.sound_volume) * map_sound_fade) / FULL_LOUDNESS);
        }
        set_streamed_sample_volume(((int64_t)settings.sound_volume * map_sound_fade) / FULL_LOUDNESS);
        set_music_volume((map_sound_fade * settings.music_volume) / FULL_LOUDNESS);
    } else
    {
        if ((features_enabled & Ft_AdvAmbSound) != 0)
        {
            SetSampleVolume(emit_id, campaign.ambient_good, 0);
            SetSampleVolume(emit_id, campaign.ambient_bad, 0);
        }
        set_music_volume(0);
        set_streamed_sample_volume(0);
    }
}

int64_t get_disabled_flag_option(int64_t ensign_type, int64_t default_ensign)
{
    int64_t base_ensign;
    if (ensign_type == 0)
    {
        base_ensign = default_ensign;
    }
    else
    {
        base_ensign = ensign_type;
    }
    if (base_ensign == EnsTutorial)
        return EnsDisTutorial;
    else if (base_ensign == EnsFullFlag)
        return EnsDisFull;
    else if (base_ensign == EnsBonus)
        return EnsDisFull;
    else if (base_ensign == EnsFullMoon)
        return EnsDisMoonF;
    else if (base_ensign == EnsNewMoon)
        return EnsDisMoonN;

    return EnsDisFull;
}

const struct TbSprite *get_ensign_sprite_for_level(struct LevelInformation *lvinfo, int64_t anim_frame)
{
    const struct TbSprite *spr;
    if (lvinfo == NULL)
        return NULL;
    if (lvinfo->state == LvSt_Hidden)
        return NULL;
    struct LevelEnsignOverride *override = get_level_ensign_override(lvinfo->lvnum);   
    int64_t ensign_type = override != NULL && override->active ? override->ensign_type : lvinfo->ensign_type;
    if (ensign_type >= CUSTOM_ENSIGN_BASE)
    {
        int64_t frame = 0;
        if (lvinfo->level_type & LvKind_IsMulti){
            if ((fe_net_level_selected == lvinfo->lvnum) || (net_level_highlighted == lvinfo->lvnum))
                frame = 1;
        } else {
            frame = anim_frame & 3;
            if (lvinfo->lvnum == mouse_over_lvnum)
                frame += 4;
        }
        spr = get_custom_ensign_sprite(
            map_flag,
            ensign_type - CUSTOM_ENSIGN_BASE,
            frame);
    } else {
        int64_t ensign_sprite_index = ensign_type;
        if (lvinfo->level_type & LvKind_IsSingle)
        {
            switch (lvinfo->state)
            {
            case LvSt_Visible:
                if (ensign_sprite_index == 0)
                    ensign_sprite_index = EnsFullFlag;
                if (lvinfo->lvnum == mouse_over_lvnum)
                    ensign_sprite_index += 4;
                spr = get_map_ensign(ensign_sprite_index + (anim_frame & 3));
                break;
            default:
                ensign_sprite_index = get_disabled_flag_option(ensign_type, EnsFullFlag);
                spr = get_map_ensign(ensign_sprite_index);
                break;
            }
        } else
        if (lvinfo->level_type & LvKind_IsBonus)
        {
            if (ensign_sprite_index == 0)
                ensign_sprite_index = EnsBonus;
            switch (lvinfo->state)
            {
            case LvSt_Visible:
                if (lvinfo->lvnum == mouse_over_lvnum)
                    ensign_sprite_index += 4;
                spr = get_map_ensign(ensign_sprite_index + (anim_frame & 3));
                break;
            default:
                ensign_sprite_index = get_disabled_flag_option(ensign_type, EnsTutorial);
                spr = get_map_ensign(ensign_sprite_index);
                break;
            }
        } else
        if (lvinfo->level_type & LvKind_IsExtra)
        {
            if (ensign_sprite_index == 0)
            {
                if (lvinfo->lvnum == get_extra_level(ExLv_NewMoon))
                {
                    ensign_sprite_index = EnsNewMoon;
                }
                else
                {
                    ensign_sprite_index = EnsFullMoon;
                }
            }
            switch (lvinfo->state)
            {
                case LvSt_Visible:
                    if (lvinfo->lvnum == mouse_over_lvnum)
                        ensign_sprite_index += 4;
                    spr = get_map_ensign(ensign_sprite_index + (anim_frame & 3));
                    break;
                default:
                    if (lvinfo->lvnum == get_extra_level(ExLv_NewMoon))
                    {
                        ensign_sprite_index = get_disabled_flag_option(ensign_type, EnsNewMoon);
                    }
                    else
                    {
                        ensign_sprite_index = get_disabled_flag_option(ensign_type, EnsFullMoon);
                    }
                    spr = get_map_ensign(ensign_sprite_index);
                    break;
            }
        } else
        if (lvinfo->level_type & LvKind_IsMulti) //Note that multiplayer flags have different file
        {
            if (frontend_menu_state == FeSt_NETLAND_VIEW)
            {
                switch (lvinfo->players)
                {
                case 2:
                    ensign_sprite_index = 5;
                    break;
                case 3:
                    ensign_sprite_index = 7;
                    break;
                case 4:
                    ensign_sprite_index = 9;
                    break;
                default:
                    ensign_sprite_index = 5;
                    break;
                }
                if ((fe_net_level_selected == lvinfo->lvnum) || (net_level_highlighted == lvinfo->lvnum))
                    ensign_sprite_index++;
                if (ensign_type == EnsCoop)
                {
                    ensign_sprite_index = ensign_sprite_index + 6;
                }
            }
            else
            {
                switch (lvinfo->players)
                {
                case 2:
                    ensign_sprite_index = EnsDisMulti2;
                    break;
                case 3:
                    ensign_sprite_index = EnsDisMulti3;
                    break;
                case 4:
                    ensign_sprite_index = EnsDisMulti4;
                    break;
                default:
                    ensign_sprite_index = EnsDisMulti2;
                    break;
                }
            }
            spr = get_map_ensign(ensign_sprite_index);
        }
        else
        {
            spr = get_map_ensign(EnsDisFull);
        }
    }
    if (spr == &dummy_sprite)
        ERRORLOG("Can't get Land view Ensign sprite");
    return spr;
}

/**
 * Draws the visible level ensigns on screen.
 * Note that the drawing is in reverse order than the one of reading inputs.
 */
void draw_map_level_ensigns(void)
{
    SYNCDBG(18,"Starting");
    int64_t k = LbTimerClock() / 200;
    struct LevelInformation* lvinfo = get_last_level_info();
    while (lvinfo != NULL)
    {
      // the flag sprite
      const struct TbSprite* spr = get_ensign_sprite_for_level(lvinfo, k);
      if (spr != NULL)
      {
          int64_t x = lvinfo->ensign_x - (int64_t)map_info.screen_shift_x - (int64_t)(spr->SWidth >> 1);
          int64_t y = lvinfo->ensign_y - (int64_t)map_info.screen_shift_y - (int64_t)(spr->SHeight);
          LbSpriteDrawResized(scale_value_landview(x), scale_value_landview(y), units_per_pixel_landview, spr);
      }
      lvinfo = get_prev_level_info(lvinfo);
    }
}

/** Sets visible land picture area to have top left corner on given coordinates.
 *
 * @param map_x Shift X coordinate for top left of the visible land picture area.
 * @param map_y Shift Y coordinate for top left of the visible land picture area.
 * @note To be used only in low-level screen shift processing; use set_map_info_screen_shift() instead.
 */
void set_map_info_screen_shift_raw(int64_t map_x, int64_t map_y)
{
    map_info.screen_shift_x = map_x;
    map_info.screen_shift_y = map_y;
    // Make sure the hotspot will not be too close to border to not be drawn correctly at full zoom
    int64_t delta_x;
    int64_t delta_y;
    if ((map_info.fadeflags & MLInfoFlg_Zooming) != 0) {
        delta_x = (lbDisplay.PhysicalScreenWidth*(256 - map_info.fade_pos)*16/units_per_pixel_landview) / 256;
        delta_y = (lbDisplay.PhysicalScreenHeight*(256 - map_info.fade_pos)*16/units_per_pixel_landview) / 256;
    } else {
        delta_x = (lbDisplay.PhysicalScreenWidth*16/units_per_pixel_landview);
        delta_y = (lbDisplay.PhysicalScreenHeight*16/units_per_pixel_landview);
    }
    if (map_info.screen_shift_x > LANDVIEW_MAP_WIDTH - delta_x)
        map_info.screen_shift_x = LANDVIEW_MAP_WIDTH - delta_x;
    if (map_info.screen_shift_x < 0)
        map_info.screen_shift_x = 0;
    if (map_info.screen_shift_y > LANDVIEW_MAP_HEIGHT - delta_y)
        map_info.screen_shift_y = LANDVIEW_MAP_HEIGHT - delta_y;
    if (map_info.screen_shift_y < 0)
        map_info.screen_shift_y = 0;
}

/** Sets visible land picture area to be centered over given coordinates.
 *
 * @param map_x Shift X coordinate for center of the visible land picture area.
 * @param map_y Shift Y coordinate for center of the visible land picture area.
 */
void set_map_info_screen_shift(int64_t map_x, int64_t map_y)
{
    int64_t delta_x = (lbDisplay.PhysicalScreenWidth * 16 / units_per_pixel_landview) / 2;
    int64_t delta_y = (lbDisplay.PhysicalScreenHeight * 16 / units_per_pixel_landview) / 2;
    set_map_info_screen_shift_raw(map_x - delta_x, map_y - delta_y);
    // Reset precise shifts, which are often used for screen shift update
    // The reset is here so that new values have correct clipping applied
    map_info.precise_scrshift_x = map_info.screen_shift_x * 256;
    map_info.precise_scrshift_y = map_info.screen_shift_y * 256;
}

void step_frontmap_info_screen_shift_zoom(void)
{
    // Count the remaining shift
    int64_t scr_x = map_info.hotspot_shift_x - map_info.screen_shift_aimed_x;
    int64_t scr_y = map_info.hotspot_shift_y - map_info.screen_shift_aimed_y;
    if ((scr_x != 0) || (scr_y != 0))
    {
        int64_t step = LbSinL(DEGREES_90 * (int64_t)map_info.fade_pos / FRONTMAP_ZOOM_LENGTH);
        map_info.precise_scrshift_x = (map_info.screen_shift_aimed_x * 256) + (scr_x * step) / 256;
        map_info.precise_scrshift_y = (map_info.screen_shift_aimed_y * 256) + (scr_y * step) / 256;
        set_map_info_screen_shift_raw(map_info.precise_scrshift_x / 256, map_info.precise_scrshift_y / 256);
    }
}

void set_map_info_visible_hotspot_raw(int64_t map_x,int64_t map_y)
{
    map_info.hotspot_shift_x = map_x;
    map_info.hotspot_shift_y = map_y;
    if (map_info.hotspot_shift_x > LANDVIEW_MAP_WIDTH - lbDisplay.PhysicalScreenWidth*16/units_per_pixel_landview)
        map_info.hotspot_shift_x = LANDVIEW_MAP_WIDTH - lbDisplay.PhysicalScreenWidth*16/units_per_pixel_landview;
    if (map_info.hotspot_shift_x < 0)
        map_info.hotspot_shift_x = 0;
    if (map_info.hotspot_shift_y > LANDVIEW_MAP_HEIGHT - lbDisplay.PhysicalScreenHeight*16/units_per_pixel_landview)
        map_info.hotspot_shift_y = LANDVIEW_MAP_HEIGHT - lbDisplay.PhysicalScreenHeight*16/units_per_pixel_landview;
    if (map_info.hotspot_shift_y < 0)
        map_info.hotspot_shift_y = 0;
}

void set_map_info_visible_hotspot(int64_t map_x,int64_t map_y)
{
    int64_t delta_x = (lbDisplay.PhysicalScreenWidth * 16 / units_per_pixel_landview) / 2;
    int64_t delta_y = (lbDisplay.PhysicalScreenHeight * 16 / units_per_pixel_landview) / 2;
    set_map_info_visible_hotspot_raw(map_x - delta_x, map_y - delta_y);
}

void frontmap_zoom_skip_init(LevelNumber lvnum)
{
    struct LevelInformation* lvinfo = get_level_info(lvnum);
    // Update hostspot to ensign position
    if (lvinfo != NULL)
    {
        map_info.hotspot_imgpos_x = lvinfo->ensign_zoom_x;
        map_info.hotspot_imgpos_y = lvinfo->ensign_zoom_y;
    } else
    {
        map_info.hotspot_imgpos_x = (LANDVIEW_MAP_WIDTH>>1);
        map_info.hotspot_imgpos_y = (LANDVIEW_MAP_HEIGHT>>1);
    }
    set_map_info_visible_hotspot(map_info.hotspot_imgpos_x, map_info.hotspot_imgpos_y);
    // Disable fade so that screen shift function fixes coords correctly
    map_info.fadeflags &= ~MLInfoFlg_Zooming;
    set_map_info_screen_shift(map_info.hotspot_imgpos_x,map_info.hotspot_imgpos_y);
    // Set aimed screen shift, shouldn't be used anyway
    map_info.screen_shift_aimed_x = map_info.hotspot_shift_x;
    map_info.screen_shift_aimed_y = map_info.hotspot_shift_y;
    // Set working parameters for zooming
    map_info.fade_pos = 1;
    map_info.fade_step = 0;
    map_info.fadeflags &= ~MLInfoFlg_SpeechAfterZoom;
    map_info.fadeflags &= ~MLInfoFlg_Zooming;
}

void frontmap_zoom_out_init(LevelNumber prev_lvnum, LevelNumber next_lvnum)
{
    struct LevelInformation* prev_lvinfo = get_level_info(prev_lvnum);
    struct LevelInformation* next_lvinfo = get_level_info(next_lvnum);
    // Update hostspot to ensign position
    if (prev_lvinfo != NULL)
    {
        map_info.hotspot_imgpos_x = prev_lvinfo->ensign_zoom_x;
        map_info.hotspot_imgpos_y = prev_lvinfo->ensign_zoom_y;
    } else
    {
        map_info.hotspot_imgpos_x = (LANDVIEW_MAP_WIDTH>>1);
        map_info.hotspot_imgpos_y = (LANDVIEW_MAP_HEIGHT>>1);
    }
    set_map_info_visible_hotspot(map_info.hotspot_imgpos_x, map_info.hotspot_imgpos_y);
    // Disable fade so that screen shift function fixes coords correctly
    map_info.fadeflags &= ~MLInfoFlg_Zooming;
    if (next_lvinfo != NULL)
    {
        // Shift towards next flag, but not too much - old flag pos must be on screen all the time
        // otherwise draw function will clip its coordinates
        int64_t maxdelta_x = (lbDisplay.PhysicalScreenWidth * 16 / units_per_pixel_landview) / 2;
        int64_t maxdelta_y = (lbDisplay.PhysicalScreenHeight * 16 / units_per_pixel_landview) / 2;
        int64_t dt_x = (next_lvinfo->ensign_zoom_x - map_info.hotspot_imgpos_x) / 2;
        if (dt_x > maxdelta_x)
            dt_x = maxdelta_x;
        if (dt_x < -maxdelta_x)
            dt_x = -maxdelta_x;
        int64_t dt_y = (next_lvinfo->ensign_zoom_y - map_info.hotspot_imgpos_y) / 2;
        if (dt_y > maxdelta_y)
            dt_y = maxdelta_y;
        if (dt_y < -maxdelta_y)
            dt_y = -maxdelta_y;
        set_map_info_screen_shift(map_info.hotspot_imgpos_x+dt_x, map_info.hotspot_imgpos_y+dt_y);
    } else
    {
        set_map_info_screen_shift(map_info.hotspot_imgpos_x,map_info.hotspot_imgpos_y);
    }
    // Set aimed screen shift, used as reference position we want to achieve
    map_info.screen_shift_aimed_x = map_info.screen_shift_x;
    map_info.screen_shift_aimed_y = map_info.screen_shift_y;
    // Set working parameters for zooming
    map_info.fade_pos = FRONTMAP_ZOOM_LENGTH - 1;
    map_info.fade_step = -LANDVIEW_ZOOM_STEP;
    map_info.fadeflags |= MLInfoFlg_SpeechAfterZoom;
    map_info.fadeflags |= MLInfoFlg_Zooming;
}

void frontmap_zoom_in_init(LevelNumber lvnum)
{
    struct LevelInformation* lvinfo = get_level_info(lvnum);
    // Disable fade so that hostspot function fixes coords correctly
    map_info.fadeflags &= ~MLInfoFlg_Zooming;
    if (lvinfo != NULL)
    {
        map_info.hotspot_imgpos_x = lvinfo->ensign_zoom_x;
        map_info.hotspot_imgpos_y = lvinfo->ensign_zoom_y;
    } else
    {
        map_info.hotspot_imgpos_x = (LANDVIEW_MAP_WIDTH>>1);
        map_info.hotspot_imgpos_y = (LANDVIEW_MAP_HEIGHT>>1);
    }
    set_map_info_visible_hotspot(map_info.hotspot_imgpos_x, map_info.hotspot_imgpos_y);
    // We don't need to set screen_shift as it is user-made value when zooming in
    // Set aimed screen shift, used as reference position we started with
    map_info.screen_shift_aimed_x = map_info.screen_shift_x;
    map_info.screen_shift_aimed_y = map_info.screen_shift_y;
    SYNCDBG(8,"Level %" PRId64 " hotspot (%" PRId64 ",%" PRId64 ") zoom (%" PRId64 ",%" PRId64 ")",(int64_t)lvnum,(int64_t)map_info.hotspot_shift_x,(int64_t)map_info.hotspot_shift_y,(int64_t)map_info.hotspot_imgpos_x,(int64_t)map_info.hotspot_imgpos_y);
    // Set working parameters for zooming
    map_info.fade_step = LANDVIEW_ZOOM_STEP;
    map_info.fade_pos = 0;
    map_info.fadeflags |= MLInfoFlg_SpeechAfterZoom;
    map_info.fadeflags |= MLInfoFlg_Zooming;
}

TbBool frontmap_input_active_ensign(int64_t curr_mx, int64_t curr_my)
{
    struct LevelInformation* lvinfo = get_first_level_info();
    while (lvinfo != NULL && lvinfo->lvnum != 0)
    {
      if (lvinfo->state == LvSt_Visible)
        if (is_over_ensign(lvinfo, curr_mx, curr_my))
        {
            mouse_over_lvnum = lvinfo->lvnum;
            return true;
        }
      lvinfo = get_next_level_info(lvinfo);
    }
    mouse_over_lvnum = SINGLEPLAYER_NOTSTARTED;
    return false;
}

int64_t clicked_map_level_ensign(void)
{
    struct LevelInformation* lvinfo = get_level_info(mouse_over_lvnum);
    if (lvinfo != NULL)
    {
      set_selected_level_number(lvinfo->lvnum);
      frontmap_zoom_in_init(lvinfo->lvnum);
      map_info.state_trigger = FeSt_START_KPRLEVEL;
      return true;
    }
    return false;
}

TbBool initialize_description_speech(void)
{
    play_desc_speech_time = 0;
    playing_speech_lvnum = SINGLEPLAYER_NOTSTARTED;
    playing_good_descriptive_speech = 0;
    playing_bad_descriptive_speech = 0;
    played_good_descriptive_speech = 0;
    played_bad_descriptive_speech = 0;
    return true;
}

TbBool stop_description_speech(void)
{
    if ((playing_good_descriptive_speech) || (playing_bad_descriptive_speech))
    {
        playing_good_descriptive_speech = 0;
        playing_bad_descriptive_speech = 0;
        playing_speech_lvnum = SINGLEPLAYER_NOTSTARTED;
        stop_streamed_samples();
        return true;
    }
    return false;
}

TbBool play_current_description_speech(int64_t play_good)
{
    LevelNumber lvnum = get_continue_level_number();
    if (!play_good)
        lvnum = prev_singleplayer_level(lvnum);
    return play_description_speech(lvnum,play_good);
}

TbBool play_description_speech(LevelNumber lvnum, int64_t play_good)
{
    char *fname;
    if (playing_speech_lvnum == lvnum)
      return true;
    struct LevelInformation* lvinfo = get_level_info(lvnum);
    if (lvinfo == NULL)
      return false;
    if (play_good)
    {
      if (lvinfo->speech_before[0] == '\0')
        return false;
      stop_description_speech();
      if (strchr(lvinfo->speech_before, '.') == NULL)
      {
          WARNLOG("No extension specified for good speech file; defaulting to '.wav'.");
          char path[DISKPATH_SIZE];
          snprintf(path, sizeof(path), "%s.wav", lvinfo->speech_before);
          fname = prepare_file_fmtpath(FGrp_AtlSound, "%s", path);
      }
      else
      {
          fname = prepare_file_fmtpath(FGrp_AtlSound,"%s",lvinfo->speech_before);
      }
      playing_good_descriptive_speech = 1;
    } else
    {
      if (lvinfo->speech_after[0] == '\0')
        return false;
      stop_description_speech();
      if (strchr(lvinfo->speech_after, '.') == NULL)
      {
          WARNLOG("No extension specified for evil speech file; defaulting to '.wav'.");
          char path[DISKPATH_SIZE];
          snprintf(path, sizeof(path), "%s.wav", lvinfo->speech_after);
          fname = prepare_file_fmtpath(FGrp_AtlSound, "%s", path);
      }
      else
      {
          fname = prepare_file_fmtpath(FGrp_AtlSound,"%s",lvinfo->speech_after);
      }
      playing_bad_descriptive_speech = 1;
    }
    playing_speech_lvnum = lvnum;
    SYNCMSG("Playing %s", fname);
    //volume is overwritten in update_frontmap_ambient_sound
    return play_streamed_sample(fname, settings.sound_volume);
}

TbBool set_pointer_graphic_spland(int64_t frame)
{
    const struct TbSprite* spr = get_map_ensign(1);
    if (spr == &dummy_sprite)
      ERRORLOG("Can't get Land view Mouse sprite");
    LbMouseChangeSprite(spr);
    return (spr != &dummy_sprite);
}

void frontzoom_to_point(int64_t map_x, int64_t map_y, int64_t zoom)
{
    unsigned char *src;
    int64_t bpos_x;
    int64_t x;
    int64_t y;
    int64_t src_delta = (256 - zoom) * 16 / units_per_pixel_landview;
    int64_t smap_x = scale_value_landview(map_x);
    int64_t smap_y = scale_value_landview(map_y);
    // Initializing variables used for all quadres of screen
    // First find a quadres division place - coords bounding the quadres
    // Make sure each quadre is at least one pixel wide and high
    int64_t scr_x = smap_x - scale_value_landview(map_info.screen_shift_x);
    if (scr_x > lbDisplay.PhysicalScreenWidth-1) scr_x = lbDisplay.PhysicalScreenWidth-1;
    if (scr_x < 1) scr_x = 1;
    int64_t scr_y = smap_y - scale_value_landview(map_info.screen_shift_y);
    if (scr_y > lbDisplay.PhysicalScreenHeight-1) scr_y = lbDisplay.PhysicalScreenHeight-1;
    if (scr_y < 1) scr_y = 1;
    const unsigned char *pal = RendererGetActivePalette();
    unsigned char* src_buf = &map_screen[LANDVIEW_MAP_WIDTH * map_y + map_x];
    int64_t dst_scanln = lbDisplay.GraphicsScreenWidth;
    TbPixel* dst_buf = &RendererGetFramebuffer()[dst_scanln * scr_y + scr_x];
    // Drawing first quadre
    int64_t bpos_y = 0;
    TbPixel* dst = dst_buf;
    int64_t dst_width = scr_x;
    int64_t dst_height = scr_y;
    // FIXME: I'm sure there's a less convoluted way of doing this, code below is setting off lots of cppcheck alarms
    for (y=0; y <= dst_height; y++)
    {
        bpos_x = 0;
        src = &src_buf[-LANDVIEW_MAP_WIDTH*(bpos_y >> 8)];
        for (x=0; x <= dst_width; x++)
        {
          bpos_x += src_delta;
          dst[-x] = resolve_indexed_pixel(src[-(bpos_x >> 8)], pal);
        }
        dst -= dst_scanln;
        bpos_y += src_delta;
    }
    // Drawing 2nd quadre
    bpos_y = 0;
    dst = dst_buf + 1;
    dst_width = -scr_x + lbDisplay.PhysicalScreenWidth - 1; // one pixel less in destination
    dst_height = scr_y;
    for (y=0; y <= dst_height; y++)
    {
        bpos_x = (1 << 8); // one pixel less in source
        src = &src_buf[-LANDVIEW_MAP_WIDTH*(bpos_y >> 8)];
        for (x=0; x < dst_width; x++)
        {
          bpos_x += src_delta;
          dst[x] = resolve_indexed_pixel(src[(bpos_x >> 8)], pal);
        }
        dst -= dst_scanln;
        bpos_y += src_delta;
    }
    // Drawing 3rd quadre
    bpos_y = (1 << 8); // one pixel less in source
    dst = dst_buf + dst_scanln;
    dst_width = scr_x;
    dst_height = -scr_y + lbDisplay.PhysicalScreenHeight - 1; // one pixel less in destination
    for (y=0; y < dst_height; y++)
    {
        bpos_x = 0;
        src = &src_buf[LANDVIEW_MAP_WIDTH*(bpos_y >> 8)];
        for (x=0; x <= dst_width; x++)
        {
            bpos_x += src_delta;
            dst[-x] = resolve_indexed_pixel(src[-(bpos_x >> 8)], pal);
        }
        dst += dst_scanln;
        bpos_y += src_delta;
    }
    // Drawing 4th quadre
    bpos_y = (1 << 8);
    dst = dst_buf + dst_scanln + 1;
    dst_width = -scr_x + lbDisplay.PhysicalScreenWidth - 1;
    dst_height = -scr_y + lbDisplay.PhysicalScreenHeight - 1;
    for (y=0; y < dst_height; y++)
    {
        bpos_x = (1 << 8);
        src = &src_buf[LANDVIEW_MAP_WIDTH*(bpos_y >> 8)];
        for (x=0; x < dst_width; x++)
        {
            dst[x] = resolve_indexed_pixel(src[(bpos_x >> 8)], pal);
            bpos_x += src_delta;
        }
        dst += dst_scanln;
        bpos_y += src_delta;
    }
}
/** Draw the window frame on the campaign map (land view). */
void compressed_window_draw(void)
{
    SYNCDBG(18,"Starting");
    int64_t default_movement_scale = 1024;
    int64_t xshift = map_info.screen_shift_x * landview_frame_movement_scale_x / default_movement_scale / 2; // X speed is slower on aspect ratios wider than 4:3
    int64_t yshift = map_info.screen_shift_y *landview_frame_movement_scale_y / default_movement_scale / 2; // Y speed is slower on aspect ratios taller than 4:3
    LbHugeSpriteDraw(&map_window, map_window_len,
        RendererGetFramebuffer(), lbDisplay.GraphicsScreenWidth, lbDisplay.PhysicalScreenHeight,
        xshift, yshift, units_per_pixel_landview_frame);
}

void unload_map_and_window(void)
{
    clear_light_system(&lish);
    light_registry_clear();
    clear_things_and_persons_data();
    clear_mapmap();
    clear_computer();
    clear_slabs();
    clear_rooms();
    clear_dungeons();
    memcpy(frontend_palette, frontend_backup_palette, PALETTE_SIZE);
    map_window_len = 0;
}

TbBool load_map_and_window(LevelNumber lvnum)
{
    SYNCDBG(8,"Starting");
    // Select proper land view image for the level
    char* land_view = NULL;
    char* land_window = NULL;
    if (lvnum == SINGLEPLAYER_NOTSTARTED)
    {
        land_view = campaign.land_view_start;
        land_window = campaign.land_window_start;
    } else
    if (lvnum == SINGLEPLAYER_FINISHED)
    {
        land_view = campaign.land_view_end;
        land_window = campaign.land_window_end;
    } else
    {
        struct LevelInformation* lvinfo = get_level_info(lvnum);
        if (lvinfo != NULL)
        {
            land_view = lvinfo->land_view;
            land_window = lvinfo->land_window;
        } else
        {
            land_view = campaign.land_view_start;
            land_window = campaign.land_window_start;
        }
    }
    if ((land_view == NULL) || (land_window == NULL))
    {
        ERRORLOG("No Land View file names for level %" PRId64,(int64_t)(lvnum));
        return false;
    }
    // A PNG of the same name takes the place of the .raw + .pal pair (an indexed PNG as it is, other PNGs quantised to 256
    // colours; must be 1280 x 960), so authors need no converter.
    uint8_t png_palette[PALETTE_SIZE];
    char png_base[DISKPATH_SIZE];
    snprintf(png_base, sizeof(png_base), "%s", prepare_file_fmtpath(FGrp_LandView, "%s", land_view));
    const int png_result = landview_load_png_indexed(png_base, (uint8_t *)kfx_frontend_state.land_map_start, png_palette);
    if (png_result < 0)
        return false;
    char* fname;
    if (png_result == 0)
    {
        // Prepare full file name and load the image
        fname = prepare_file_fmtpath(FGrp_LandView, "%s.raw", land_view);
        int64_t flen = LbFileLengthRnc(fname);
        if (flen < 1024)
        {
            ERRORLOG("Land Map background \"%s.raw\" doesn't exist or is too small",land_view);
            return false;
        }
        if (flen > 1228997)
        {
            ERRORLOG("Not enough memory in game structure for Land Map background \"%s.raw\"",land_view);
            return false;
        }
        if (LbFileLoadAt(fname, kfx_frontend_state.land_map_start) != flen)
        {
            ERRORLOG("Unable to load Land Map background \"%s.raw\"",land_view);
            return false;
        }
    }
    map_screen = kfx_frontend_state.land_map_start;
    // Texture blocks memory isn't used here, so reuse it instead of allocating
    unsigned char* ptr = block_mem;
    memcpy(frontend_backup_palette, &frontend_palette, PALETTE_SIZE);
    // Now prepare window sprite file name and load the file
    fname = prepare_file_fmtpath(FGrp_LandView,"%s.dat",land_window);
    map_window_len = LbFileLoadAt(fname, ptr);
    if (map_window_len < (int64_t)(WINDOW_Y_SIZE*sizeof(int32_t)))
    {
        ERRORLOG("Unable to load Land Map Window \"%s.dat\"",land_window);
        unload_map_and_window();
        return false;
    }
    // Prepare pointer to offsets array; WINDOW_Y_SIZE entries
    map_window.Lines = (int32_t *)&ptr[0]; // offsets stored as 32-bit in the data file
    // Prepare pointer to window data
    map_window.Data = &ptr[WINDOW_Y_SIZE*sizeof(int32_t)];
    // Fill the rest of huge sprite
    map_window.SWidth = WINDOW_X_SIZE;
    map_window.SHeight = WINDOW_Y_SIZE;
    // Update length, so that it corresponds to map_window pointer
    map_window_len -= WINDOW_Y_SIZE*sizeof(int32_t);
    // Load palette
    if (png_result == 1)
    {
        memcpy(frontend_palette, png_palette, PALETTE_SIZE);
    } else
    {
        fname = prepare_file_fmtpath(FGrp_LandView,"%s.pal",land_view);
        if (LbFileLoadAt(fname, frontend_palette) != PALETTE_SIZE)
        {
            ERRORLOG("Unable to load Land Map palette \"%s.pal\"",land_view);
            unload_map_and_window();
            return false;
        }
    }
    SYNCDBG(9,"Finished");
    return true;
}

void frontmap_start_music(void)
{
    if (strlen(campaign.soundtrack_fname) > 0) {
        const int64_t track = atoi(campaign.soundtrack_fname);
        if (track >= 1) {
            play_music_track(track);
        } else {
            const char* fname = prepare_file_fmtpath(FGrp_CmpgMedia, "%s", campaign.soundtrack_fname);
            play_music(fname);
        }
    } else {
        play_music_track(2);
    }
}

void process_map_zoom_in(void)
{
    step_frontmap_info_screen_shift_zoom();
    int64_t fade_len = (FRONTMAP_ZOOM_LENGTH * LANDVIEW_SOUND_FADE_PERCENT) / 100;
    int64_t remaining = fade_len - (int64_t)map_info.fade_pos;
    map_sound_fade = max(0, remaining * FULL_LOUDNESS / fade_len);
}

void process_map_zoom_out(void)
{
    step_frontmap_info_screen_shift_zoom();
}

void process_zoom_palette(void)
{
    if (map_info.fade_step == 0 || map_info.fade_pos <= FRONTMAP_ZOOM_LENGTH/2)
        return;
    unsigned char palette[PALETTE_SIZE];
    int64_t remaining = FRONTMAP_ZOOM_LENGTH - map_info.fade_pos;
    int64_t half_length = FRONTMAP_ZOOM_LENGTH / 2;
    for (int64_t i = 0; i < PALETTE_SIZE; i++) {
        palette[i] = frontend_palette[i] * remaining / half_length;
    }
    RendererPaletteSet(palette);
}

TbBool frontmap_update_zoom(void)
{
    SYNCDBG(8,"Starting");
    if (map_info.fade_step == LANDVIEW_ZOOM_STEP)
    {
        process_map_zoom_in();
    } else
    if (map_info.fade_step == -LANDVIEW_ZOOM_STEP)
    {
        process_map_zoom_out();
    }
    map_info.fade_pos += map_info.fade_step * kfx_render_state.delta_time;
    if (map_info.fade_pos < 0 || map_info.fade_pos >= FRONTMAP_ZOOM_LENGTH)
    {
        SYNCDBG(8,"Stopping fade");
        LbPaletteStopOpenFade();
        map_info.fadeflags &= ~MLInfoFlg_Zooming;
        if (map_info.state_trigger != FeSt_INITIAL)
        {
            frontend_set_state(map_info.state_trigger);
            RendererClearScreen(0);
            RendererPresentStepFrame();
            map_info.state_trigger = FeSt_INITIAL;
            return true;
        }
    }
    process_zoom_palette();
    return false;
}

/**
 * Loads map_flag, the land-view ensign sprite sheet, per the active
 * campaign's land_markers kind, plus any custom ensigns. Split out of
 * frontmap_load so frontmenu_landpreview.c's panel-scoped preview can
 * load the same ensign sheet without frontmap_load's continue-level/zoom/
 * music/ambient-sound orchestration, which is specific to the full-screen
 * cutscene entry sequence.
 */
TbBool load_map_ensign_sprites(void)
{
    switch (campaign.land_markers) {
        case LndMk_PINPOINTS:
            map_flag = load_spritesheet("ldata/lndflag_pin.dat", "ldata/lndflag_pin.tab");
            break;
        default:
            ERRORLOG("Unsupported land markers type %" PRId64,(int64_t)campaign.land_markers);
            // Fall through
        case LndMk_ENSIGNS:
            map_flag = load_spritesheet("ldata/lndflag_ens.dat", "ldata/lndflag_ens.tab");
            break;
    }
    // append any custom ensigns to the sheet
    map_flag = load_custom_ensigns_into_sheet(map_flag, frontend_palette);
    return map_flag != NULL;
}

TbBool frontmap_load(void)
{
    SYNCDBG(4,"Starting");
    memset(scratch, 0, PALETTE_SIZE);
    RendererPaletteSet(scratch);
    initialize_description_speech();
    mouse_over_lvnum = SINGLEPLAYER_NOTSTARTED;
    frontend_load_data_from_cd();
    LevelNumber lvnum = get_continue_level_number();
    if (!load_map_and_window(lvnum))
    {
        frontend_load_data_reset();
        return false;
    }
    TbBool ensigns_loaded = load_map_ensign_sprites();
    init_netfont_palette_remap();
    pop_palette_remap();
    map_font = load_spritesheet("ldata/netfont.dat", "ldata/netfont.tab");
    if (!ensigns_loaded)
    {
        ERRORLOG("Unable to load Land View Screen sprites");
        frontend_load_data_reset();
        return false;
    }
    frontend_load_data_reset();
    struct PlayerInfo* player = get_my_player();
    lvnum = get_continue_level_number();
    if ((player->display_flags & PlaF6_PlyrHasQuit) != 0)
    {
        lvnum = get_loaded_level_number();
        frontmap_zoom_out_init(lvnum, lvnum);
    } else
    if ((lvnum == first_singleplayer_level()) || (player->victory_state == VicS_LostLevel) || (player->victory_state == VicS_State3))
    {
        frontmap_zoom_skip_init(lvnum);
        // fade_palette_in's "fading controlled by main frontend loop" cue
        // removed per docs/refactor/renderer/05-imgui-owned-menu-backdrop.md
        // Phase 0 -- that loop no longer acts on it.
        play_desc_speech_time = LbTimerClock() + 1000;
    } else
    {
        frontmap_zoom_out_init(prev_singleplayer_level(lvnum), lvnum);
    }
    SYNCDBG(9,"Zoom hotspot set to (%" PRId64 ",%" PRId64 ") %s fade",(int64_t)map_info.hotspot_imgpos_x,(int64_t)map_info.hotspot_imgpos_y,(map_info.fadeflags & MLInfoFlg_Zooming)?"with":"without");
    map_sound_fade = FULL_LOUDNESS;
    map_info.velocity_x = 0;
    map_info.velocity_y = 0;
    set_pointer_graphic_spland(0);
    LbMouseSetPosition(lbDisplay.PhysicalScreenWidth/2, lbDisplay.PhysicalScreenHeight/2);
    if ((features_enabled & Ft_AdvAmbSound) != 0)
    {
        // don't use play_non_3d_sample; we want looping, fading, and volume control
        SoundEmitterID emit_id = get_emitter_id(S3DGetSoundEmitter(Non3DEmitter));
        play_sample(emit_id, campaign.ambient_good, 0, 0x40, NORMAL_PITCH, -1, 2);
        play_sample(emit_id, campaign.ambient_bad, 0, 0x40, NORMAL_PITCH, -1, 2);
    }
    set_music_volume(settings.music_volume);
    frontmap_start_music();
    fe_computer_players = 0;
    update_ensigns_visibility();
    SYNCDBG(7,"Finished");
    script_api_event("CAMPAIGN_LOADED");
    return true;
}

void frontmap_draw(void)
{
    SYNCDBG(8,"Starting");
    LbTextSetFont(map_font);
    LbTextSetWindow(0, 0, lbDisplay.PhysicalScreenWidth, lbDisplay.PhysicalScreenHeight);
    if ((map_info.fadeflags & MLInfoFlg_Zooming) != 0)
    {
        frontzoom_to_point(map_info.hotspot_imgpos_x, map_info.hotspot_imgpos_y, map_info.fade_pos);
        compressed_window_draw();
    } else
    {
        draw_map_screen();
        draw_map_level_ensigns();        
        draw_map_level_descriptions();
        set_pointer_graphic_spland(0);
        compressed_window_draw();
    }
}

void check_mouse_scroll(void)
{
    int64_t mx = GetMouseX();
    if ( (mx < 8) || ( (is_game_key_pressed(Gkey_MoveLeft, false, false)) || (is_key_pressed(KC_LEFT,KMod_DONTCARE)) ) )
    {
        map_info.velocity_x -= LANDVIEW_PAN_ACCEL * kfx_render_state.delta_time;
        if (map_info.velocity_x < -LANDVIEW_PAN_MAX_SPEED)
            map_info.velocity_x = -LANDVIEW_PAN_MAX_SPEED;
        if (map_info.velocity_x > LANDVIEW_PAN_MAX_SPEED)
            map_info.velocity_x = LANDVIEW_PAN_MAX_SPEED;
  } else
  if ( (mx >= lbDisplay.PhysicalScreenWidth-8) || ( (is_game_key_pressed(Gkey_MoveRight, false, false)) || (is_key_pressed(KC_RIGHT,KMod_DONTCARE)) ) )
  {
    map_info.velocity_x += LANDVIEW_PAN_ACCEL * kfx_render_state.delta_time;
    if (map_info.velocity_x < -LANDVIEW_PAN_MAX_SPEED)
      map_info.velocity_x = -LANDVIEW_PAN_MAX_SPEED;
    if (map_info.velocity_x > LANDVIEW_PAN_MAX_SPEED)
      map_info.velocity_x = LANDVIEW_PAN_MAX_SPEED;
  }
  int64_t my = GetMouseY();
  if ( (my < 8) || ( (is_game_key_pressed(Gkey_MoveUp, false, false)) || (is_key_pressed(KC_UP,KMod_DONTCARE)) ) )
  {
    map_info.velocity_y -= LANDVIEW_PAN_ACCEL * kfx_render_state.delta_time;
    if (map_info.velocity_y < -LANDVIEW_PAN_MAX_SPEED)
      map_info.velocity_y = -LANDVIEW_PAN_MAX_SPEED;
    if (map_info.velocity_y > LANDVIEW_PAN_MAX_SPEED)
      map_info.velocity_y = LANDVIEW_PAN_MAX_SPEED;
  } else
  if ( (my >= lbDisplay.PhysicalScreenHeight-8) || ( (is_game_key_pressed(Gkey_MoveDown, false, false)) || (is_key_pressed(KC_DOWN,KMod_DONTCARE)) ) )
  {
    map_info.velocity_y += LANDVIEW_PAN_ACCEL * kfx_render_state.delta_time;
    if (map_info.velocity_y < -LANDVIEW_PAN_MAX_SPEED)
      map_info.velocity_y = -LANDVIEW_PAN_MAX_SPEED;
    if (map_info.velocity_y > LANDVIEW_PAN_MAX_SPEED)
      map_info.velocity_y = LANDVIEW_PAN_MAX_SPEED;
  }
}

void update_velocity(void)
{
    if (map_info.velocity_x != 0)
    {
      map_info.screen_shift_x += (map_info.velocity_x / 4) * kfx_render_state.delta_time;
      if (map_info.screen_shift_x > LANDVIEW_MAP_WIDTH - lbDisplay.PhysicalScreenWidth*16/units_per_pixel_landview)
        map_info.screen_shift_x = LANDVIEW_MAP_WIDTH - lbDisplay.PhysicalScreenWidth*16/units_per_pixel_landview;
      if (map_info.screen_shift_x < 0)
        map_info.screen_shift_x = 0;
      if (map_info.velocity_x < 0) {
        map_info.velocity_x += LANDVIEW_PAN_DECEL * kfx_render_state.delta_time;
        if (map_info.velocity_x > 0)
          map_info.velocity_x = 0;
      } else {
        map_info.velocity_x -= LANDVIEW_PAN_DECEL * kfx_render_state.delta_time;
        if (map_info.velocity_x < 0)
          map_info.velocity_x = 0;
      }
    }
    if (map_info.velocity_y != 0)
    {
      map_info.screen_shift_y += (map_info.velocity_y / 4) * kfx_render_state.delta_time;
      if (map_info.screen_shift_y > LANDVIEW_MAP_HEIGHT - lbDisplay.PhysicalScreenHeight*16/units_per_pixel_landview)
        map_info.screen_shift_y = LANDVIEW_MAP_HEIGHT - lbDisplay.PhysicalScreenHeight*16/units_per_pixel_landview;
      if (map_info.screen_shift_y < 0)
        map_info.screen_shift_y = 0;
      if (map_info.velocity_y < 0) {
        map_info.velocity_y += LANDVIEW_PAN_DECEL * kfx_render_state.delta_time;
        if (map_info.velocity_y > 0)
          map_info.velocity_y = 0;
      } else {
        map_info.velocity_y -= LANDVIEW_PAN_DECEL * kfx_render_state.delta_time;
        if (map_info.velocity_y < 0)
          map_info.velocity_y = 0;
      }
    }
    map_info.precise_scrshift_x = map_info.screen_shift_x * 256;
    map_info.precise_scrshift_y = map_info.screen_shift_y * 256;
}

/**
 * Sets the level name text for active level display.
 */
void set_level_name_text(LevelNumber lvnum, const char *lv_name)
{
    struct LevelInformation *lvinfo = get_level_info(lvnum);
    if (lvinfo == NULL) {
        snprintf(level_name, sizeof(level_name), "%s", get_string(GUIStr_MnuLevel));
        return;
    }
    if ((lv_name != NULL) && (strlen(lv_name) > 0)) {
        if (lvinfo->level_type & LvKind_IsMulti)
            snprintf(level_name, sizeof(level_name), "%s %" PRId64 ": %s", get_string(GUIStr_MnuLevel), (int64_t)lvinfo->lvnum, lv_name);
        else             
            snprintf(level_name, sizeof(level_name), "%s", lv_name);
        return;
    }
    snprintf(level_name, sizeof(level_name), "%s %" PRId64, get_string(GUIStr_MnuLevel), (int64_t)lvinfo->lvnum);
}

int64_t order_number_for_bonus_level(LevelNumber bn_lvnum)
{
  int64_t orderNum = 1;
  if (bn_lvnum < 1) return -1;
  for (int64_t i = 0; i < CAMPAIGN_LEVELS_COUNT; i++)
  {
    if (campaign.bonus_levels[i] == bn_lvnum)
    {
      return orderNum;
    }
    else if (campaign.bonus_levels[i] != 0)
    {
      orderNum++;
    }
  }
  return -1;
}

const char* get_level_description(struct LevelInformation *lvinfo)
{
    if (lvinfo == NULL)
        return NULL;

    if (lvinfo->name_stridx > 0)
        return get_string(lvinfo->name_stridx);
    else
        return lvinfo->name;
 
    return "";
}

/**
 * Draws text description of active level.
 */
void draw_map_level_descriptions(void)
{
  if ((fe_net_level_selected > 0) || (net_level_highlighted > 0) || (mouse_over_lvnum > 0))
  {
    RendererSetDrawFlags(0);
    LevelNumber lvnum = (mouse_over_lvnum > 0) ? mouse_over_lvnum : (fe_net_level_selected > 0) ? fe_net_level_selected : net_level_highlighted;
    if (lvnum <= 0)
      lvnum = net_level_highlighted;
    struct LevelInformation* lvinfo = get_level_info(lvnum);
    if (lvinfo == NULL)
      return;
    const char* lv_name = get_level_description(lvinfo); 
    set_level_name_text(lvnum, lv_name);
    int64_t w = LbTextStringWidth(level_name);
    int64_t x = lvinfo->ensign_x - (int64_t)map_info.screen_shift_x;
    int64_t y = lvinfo->ensign_y - (int64_t)map_info.screen_shift_y - 8;
    int64_t h = LbTextHeight(level_name);
    LbDrawBox(scale_value_landview(x-4), scale_value_landview(y), scale_value_landview(w+8), scale_value_landview(h), resolve_indexed_pixel(0, RendererGetActivePalette()));

    lbSpriteReMapPtr = netfont_palette_remap;
    RendererSetDrawFlags(Lb_TEXT_REMAP);

    LbTextDrawResized(scale_value_landview(x), scale_value_landview(y), units_per_pixel_landview, level_name);
    RendererSetDrawFlags(0);
  }
}

void frontmap_input(void)
{
    SYNCDBG(8,"Starting");
    int64_t zoom_done;
    if ((map_info.fadeflags & MLInfoFlg_SpeechAfterZoom) != 0)
    {
        if ((map_info.fadeflags & MLInfoFlg_Zooming) == 0)
        {
            map_info.fadeflags &= ~MLInfoFlg_SpeechAfterZoom;
            play_desc_speech_time = LbTimerClock() + 1000;
        }
        zoom_done = false;
    } else
    {
        zoom_done = true;
    }
    if (is_key_pressed(KC_ESCAPE, KMod_DONTCARE))
    {
        clear_key_pressed(KC_ESCAPE);
        FrontendMenuState nstate = get_menu_state_when_back_from_substate(frontend_menu_state);
        frontend_set_state(nstate);
        LbPaletteStopOpenFade();
        return;
    }
    if (zoom_done)
    {
      check_mouse_scroll();
      if (is_game_key_pressed(Gkey_LVShowAllEnsigns, true, false))
      {
        if (kfx_sim_state.easter_eggs_enabled == true)
        {
          set_all_ensigns_state(LvSt_Visible);
          return;
        }
      }
      if (is_game_key_pressed(Gkey_LVNextLevel, true, false))
      {
        if (kfx_sim_state.easter_eggs_enabled == true)
        {
          move_campaign_to_next_level();
          frontmap_unload();
          frontmap_load();
          //update_ensigns_visibility();
          return;
        }
      }
      if (is_game_key_pressed(Gkey_LVPrevLevel, true, false))
      {
        if (kfx_sim_state.easter_eggs_enabled == true)
        {
          move_campaign_to_prev_level();
          frontmap_unload();
          frontmap_load();
          //update_ensigns_visibility();
          return;
        }
      }
      if (left_button_clicked)
      {
        left_button_clicked = 0;
        frontmap_input_active_ensign(left_button_clicked_x, left_button_clicked_y);
        if (clicked_map_level_ensign())
          return;
      }
      int64_t mouse_x = GetMouseX();
      int64_t mouse_y = GetMouseY();
      frontmap_input_active_ensign(mouse_x, mouse_y);
    }
    update_velocity();
}

void frontmap_unload(void)
{
    SYNCDBG(8,"Starting");
    set_pointer_graphic_none();
    unload_map_and_window();
    free_spritesheet(&map_flag);
    free_spritesheet(&map_font);
    StopAllSamples();
    stop_description_speech();
    stop_music(false);
    set_music_volume(settings.music_volume);
}

int64_t frontmap_update(void)
{
  SYNCDBG(8,"Starting");
  if ((mouse_over_lvnum > 0) && (playing_speech_lvnum != mouse_over_lvnum))
  {
      play_desc_speech_time = 0;
      play_description_speech(mouse_over_lvnum,1);
  }
  if ((play_desc_speech_time != 0) && (LbTimerClock() > play_desc_speech_time))
  {
      play_desc_speech_time = 0;
      play_current_description_speech(1);
  }
  update_frontmap_ambient_sound();

  if ((map_info.fadeflags & MLInfoFlg_Zooming) != 0)
  {
      if (frontmap_update_zoom())
      {
          SYNCDBG(8,"Zoom end");
          return true;
      }
  }
  if (playing_good_descriptive_speech)
  {
    if (!speech_sample_playing())
    {
      playing_good_descriptive_speech = 0;
//      playing_speech_lvnum = SINGLEPLAYER_NOTSTARTED;
    }
  }
  SYNCDBG(8,"Finished");
  return 0;
}

/******************************************************************************/
