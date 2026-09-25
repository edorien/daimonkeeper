/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file front_landview.h
 *     Header file for front_landview.c.
 * @par Purpose:
 *     Land view, where the user can select map for campaign or multiplayer.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     16 Mar 2009 - 01 Apr 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/

#ifndef DK_FRONT_LAND_H
#define DK_FRONT_LAND_H

#include "bflib_basics.h"
#include "globals.h"
#include "bflib_sprite.h"
#include "config_campaigns.h"
#include "net_game.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
#define FRONTMAP_ZOOM_LENGTH 240
/******************************************************************************/
#pragma pack(1)

struct TbSprite;

enum MapLevelInfoFlags {
    MLInfoFlg_None            =  0x00,
    MLInfoFlg_Zooming         =  0x01,
    MLInfoFlg_SpeechAfterZoom =  0x02,
};

struct MapLevelInfo { // sizeof = 56
  unsigned char fadeflags;
  double fade_step;
  double fade_pos;
  double hotspot_imgpos_x; /**< Position of the chosen level ensign zoom area, which is either being zoomed in to or zoomed out from. Stored as land view background bitmap coordinate. */
  double hotspot_imgpos_y;
  int64_t state_trigger;
  double screen_shift_x; /**< Shift X coordinate for top left corner of the visible land picture area. Acts as the final shift in both zoom and non-zoom modes. */
  double screen_shift_y; /**< Shift Y coordinate for top left corner of the visible land picture area. */
  double precise_scrshift_x; /**< Precise shift X for top left corner of the visible land picture area. Extended precision version, used as source for scrshift_x while zooming. */
  double precise_scrshift_y; /**< Precise shift Y for top left corner of the visible land picture area. */
  double velocity_x; /**< Velocity at which screen_shift_x is being changed. */
  double velocity_y; /**< Velocity at which screen_shift_y is being changed. */
  double hotspot_shift_x; /**< Position of the chosen level ensign zoom area, which is either being zoomed in to or zoomed out from. Set to top left corner of an area which would have the ensign in center. */
  double hotspot_shift_y;
  double screen_shift_aimed_x; /**< Shift X coordinate at which the screen_shift is aiming towards zooming. */
  double screen_shift_aimed_y;
};

// enum NetStatusLayout/NetAction, struct ScreenPacket, and
// screen_packet_action()/screen_packet_set_action() moved to net_main.h
// (kfx_net) -- see there. This header already includes net_game.h, which
// pulls in net_main.h.

/******************************************************************************/
extern TbClockMSec play_desc_speech_time;
extern uint64_t played_bad_descriptive_speech;
extern uint64_t played_good_descriptive_speech;
extern struct TbSpriteSheet *map_flag;
extern struct TbSpriteSheet *map_font;
extern struct TbSpriteSheet *map_hand;
extern int64_t map_sound_fade;
extern unsigned char *map_screen;
extern int64_t fe_net_level_selected;

#pragma pack()
/******************************************************************************/
extern struct MapLevelInfo map_info;

extern int64_t map_window_len;
/******************************************************************************/
void frontnetmap_unload(void);
TbBool frontnetmap_load(void);
void frontnetmap_input(void);
void frontnetmap_draw(void);
TbBool frontnetmap_update(void);
void frontmap_input(void);
void frontmap_draw(void);
TbBool frontmap_load(void);
void frontmap_unload(void);
// Plays campaign.soundtrack_fname (a track number, or a filename under
// FGrp_CmpgMedia) -- previously only ever forward-declared ad-hoc at each
// call site (front_landview.c/front_landview_multiplayer.c); declared
// here now that frontmenu_select.c also needs it, to preview the
// highlighted campaign/mappack's theme on the merged select screens
// (Land selection, Free play) rather than only once Land View is
// actually entered.
void frontmap_start_music(void);
int64_t frontmap_update(void);
void frontzoom_to_point(int64_t a1, int64_t a2, int64_t a3);
void compressed_window_draw(void);
const struct TbSprite *get_ensign_sprite_for_level(struct LevelInformation *lvinfo, int64_t anim_frame);
void set_level_name_text(LevelNumber lvnum, const char *lv_name);
void draw_map_level_descriptions(void);
TbBool init_netfont_palette_remap(void);
void pop_palette_remap(void);

// Reused as-is (no PhysicalScreenWidth/Height coupling) by
// frontmenu_landpreview.c's panel-scoped embedding.
// is_over_ensign is NOT reused, despite being close: it reads the global
// map_info.screen_shift_x/y and units_per_pixel_landview directly rather
// than taking them as parameters, both of which are this cutscene's own
// full-screen state, not the panel's independent pan/scale -- the panel
// has its own equivalent hit-test instead (land_preview_point_over_ensign
// in frontmenu_landpreview.c).
const struct TbSprite *get_map_ensign(int64_t idx);
TbBool load_map_and_window(LevelNumber lvnum);
void unload_map_and_window(void);
TbBool load_map_ensign_sprites(void);
extern LevelNumber mouse_over_lvnum;

TbBool initialize_description_speech(void);
TbBool stop_description_speech(void);
TbBool play_current_description_speech(int64_t play_good);
TbBool play_description_speech(LevelNumber lvnum, int64_t play_good);
void check_mouse_scroll(void);
void update_velocity(void);
void update_ensigns_visibility(void);
LevelNumber get_next_singleplayer_level_for_landview(void);

extern char level_name[88];
// frontend_backup_palette moved to kfx_render's vidmode.h (stage 13.3) --
// vidmode_data.cpp's file-loading table needs its address directly.
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
