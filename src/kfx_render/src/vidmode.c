/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file vidmode.c
 *     Video mode switching/setting function.
 * @par Purpose:
 *     Functions to change video mode in DK way.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     05 Jan 2009 - 12 Jan 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "vidmode.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_render.h"
#include "bflib_fmvids.h"
#include "bflib_video.h"
#include "bflib_mouse.h"
#include "bflib_sprite.h"
#include "bflib_dernc.h"
#include "bflib_sprfnt.h"
#include "bflib_filelst.h"


#include "config_spritecolors.h"
#include "config_strings.h"
#include "vidfade.h"
#include "engine_redraw.h"
#include "engine_textures.h"
#include "config_keeperfx.h"
#include "lens_api.h"
#include "config_settings.h"
#include "creature_graphics.h"
#include "custom_sprites.h"
#include "sprites.h"
#include "kfx_sim_state.h"
#include "ports/ui_port.h"
#include "ports/game_port.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// The single resolution used for the frontend, movie playback, and in-game
// alike (config_keeperfx.c's INGAME_RES). Historically these were four
// separate screen-specific modes (failsafe/movies/frontend/an Alt+R cycle
// list) -- collapsed to one per docs/refactor/renderer/
// 04-imgui-gui-foundation.md sec 6.4, ahead of Phase G's settings screen.

//struct IPOINT_2D units_per_pixel;
int64_t units_per_pixel_min;
int64_t units_per_pixel_width;
int64_t units_per_pixel_height;
int64_t units_per_pixel_menu_height;
int64_t units_per_pixel_best;
int64_t units_per_pixel_menu;
int64_t units_per_pixel_ui;

// units_per_pixel_landview, units_per_pixel_landview_frame,
// aspect_ratio_factor_HOR_PLUS(_AND_VERT_PLUS),
// landview_frame_movement_scale_x/y and first_person_vertical_fov are
// defined in kfx_platform/src/bflib_video.c (declared extern in
// bflib_video.h, included above) instead of here: their only writers,
// calculate_landview_upp() and calculate_aspect_ratio_factor(), already
// live there -- this file only ever calls into them, never reads/writes
// the values directly. See docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md.


static TbBool force_video_mode_reset = true;
struct TbSpriteSheet * pointer_sprites = NULL;

TbBool MinimalResolutionSetup;

struct TbColorTables pixmap;
struct TbAlphaTables alpha_sprite_table;
/******************************************************************************/

// Declarations for font testing screen (reachable at the Debug log level)
extern struct TbLoadFiles testfont_load_files[];

extern struct TbLoadFiles gui_load_files_320[];
extern struct TbLoadFiles gui_load_files_640[];
extern struct TbLoadFiles front_load_files_minimal_640[];
/******************************************************************************/

/**
 * Loads VGA 256 graphics files, for high resolution modes.
 * @return Returns true if all files were loaded, false otherwise.
 */
int64_t LoadVRes256Data(int64_t scrbuf_size)
{
    // Update size of the parchment buffer, as it is also used as screen buffer
    if (scrbuf_size < 640*480)
        scrbuf_size = 640*480;
    gui_load_files_640[1].SLength = scrbuf_size;
    // Load the files
    winfont = load_font("data/font2-64.dat", "data/font2-64.tab");
    font_sprites = load_font("data/font1-64.dat", "data/font1-64.tab");
    button_sprites = load_spritesheet("data/gui1-64.dat", "data/gui1-64.tab");
    gui_panel_sprites = load_spritesheet("data/gui2-64.dat", "data/gui2-64.tab");
    if (!winfont || !font_sprites || !button_sprites || !gui_panel_sprites || LbDataLoadAll(gui_load_files_640)) {
        return 0;
    }
    return 1;
}

void FreeVRes256Data(void)
{
    free_font(&winfont);
    free_font(&font_sprites);
    free_spritesheet(&button_sprites);
    free_spritesheet(&gui_panel_sprites);
    LbDataFreeAll(gui_load_files_640);
}

int64_t LoadVResMinimal(void)
{
    button_sprites = load_spritesheet("data/gui1-32.dat", "data/gui1-32.tab");
#ifdef SPRITE_FORMAT_V2
    frontend_font[0] = load_font("ldata/frontft1-64.dat", "ldata/frontft1-64.tab");
    frontend_font[1] = load_font("ldata/frontft2-64.dat", "ldata/frontft2-64.tab");
    frontend_font[2] = load_font("ldata/frontft3-64.dat", "ldata/frontft3-64.tab");
    frontend_font[3] = load_font("ldata/frontft4-64.dat", "ldata/frontft4-64.tab");
#else
    frontend_font[0] = load_font("ldata/frontft1.dat", "ldata/frontft1.tab");
    frontend_font[1] = load_font("ldata/frontft2.dat", "ldata/frontft2.tab");
    frontend_font[2] = load_font("ldata/frontft3.dat", "ldata/frontft3.tab");
    frontend_font[3] = load_font("ldata/frontft4.dat", "ldata/frontft4.tab");
#endif
    return button_sprites && frontend_font[0] && frontend_font[1] && frontend_font[2] &&
        frontend_font[3] && LbDataLoadAll(front_load_files_minimal_640) == 0;
}

void FreeVResMinimal(void)
{
    for (int64_t i = 0; i < FRONTEND_FONTS_COUNT; ++i) {
        free_font(&frontend_font[i]);
    }
    free_spritesheet(&button_sprites);
    LbDataFreeAll(front_load_files_minimal_640);
}

/**
 * Loads MCGA graphics files, for low resolution mode.
 * It is modified version of LbDataLoadAll, optimized for maximum
 * game speed on very slow machines.
 * @return Returns true if all files were loaded, false otherwise.
 */
int64_t LoadMcgaData(void)
{
    int64_t ferror = 0;
    int64_t i = 0;
    struct TbLoadFiles* t_lfile = &gui_load_files_320[i];
    while (t_lfile->Start != NULL)
    {
        // Don't allow loading flags
        t_lfile->Flags = 0;
        int64_t ret_val = LbDataLoad(t_lfile, NULL, NULL);
        if (ret_val == -100)
        {
            ERRORLOG("Can't allocate memory for MCGA files element \"%s\".", t_lfile->FName);
            ferror++;
        }
        else if (ret_val == -101)
        {
            ERRORLOG("Can't load MCGA file \"%s\".", t_lfile->FName);
            ferror++;
        }
        i++;
        t_lfile = &gui_load_files_320[i];
  }
  button_sprites = load_spritesheet("data/gui1-32.dat", "data/gui1-32.tab");
  winfont = load_font("data/font2-32.dat", "data/font2-32.tab");
  font_sprites = load_font("data/font1-32.dat", "data/font1-32.tab");
  gui_panel_sprites = load_spritesheet("data/gui2-32.dat", "data/gui2-32.tab");
  return button_sprites && winfont && font_sprites && gui_panel_sprites && (ferror == 0);
}

void FreeMcgaData(void)
{
    LbDataFreeAll(gui_load_files_320);
    free_font(&winfont);
    free_font(&font_sprites);
    free_spritesheet(&button_sprites);
    free_spritesheet(&gui_panel_sprites);
}


/**
 * Hardcoded emergency fallback used by setup_screen_mode()'s failsafe path
 * when the configured screen_vidmode can't be set up. Always one of the
 * standard VESA modes registered unconditionally by
 * LbRegisterStandardVideoModes(), so it never depends on user config.
 */
static TbScreenMode try_failsafe_vidmode(void)
{
  TbScreenMode failsafe_vidmode = Lb_SCREEN_MODE_640_480_8;
  if (!LbScreenIsModeAvailable(failsafe_vidmode, display_id))
  {
      ERRORLOG("Failsafe video mode (mode %" PRId64 ") is invalid.",(int64_t)failsafe_vidmode);
      return Lb_SCREEN_MODE_INVALID;
  }
  return failsafe_vidmode;
}

void load_pointer_file(int64_t hi_res)
{
#ifdef SPRITE_FORMAT_V2
    pointer_sprites = load_spritesheet("data/pointer-64.dat", "data/pointer-64.tab");
#else
    pointer_sprites = load_spritesheet("data/pointer64.dat", "data/pointer64.tab");
#endif
    if (!pointer_sprites) ERRORLOG("Unable to load pointer sprites");
}

TbBool set_pointer_graphic_none(void)
{
  LbMouseChangeSpriteAndHotspot(NULL, 0, 0);
  return true;
}

TbBool set_pointer_graphic_menu(void)
{
  if (frontend_sprite == NULL)
  {
    WARNLOG("Frontend sprites not loaded, setting pointer to none");
    LbMouseChangeSpriteAndHotspot(NULL, 0, 0);
    return false;
  }
  LbMouseChangeSpriteAndHotspot(get_frontend_sprite(GFS_cursor_horny), 0, 0);
  return true;
}

TbBool set_pointer_graphic_spell(int64_t spridx, int64_t frame)
{
    int64_t i;
    int64_t x;
    int64_t y;
    SYNCDBG(8, "Setting to sprite %" PRId64, (int64_t)spridx);
    if (pointer_sprites == NULL)
    {
        WARNLOG("Pointer sprites not loaded, setting to none");
        LbMouseChangeSpriteAndHotspot(NULL, 0, 0);
        return false;
  }
  if (is_feature_on(Ft_BigPointer))
  {
    y = 32;
    x = 32;
    i = spridx + (frame%8);
  } else
  {
    y = 78;
    x = 26;
    i = spridx;
  }
  const struct TbSprite* spr = NULL;

  if (is_custom_icon(i))
  {
      spr = get_new_icon_sprite(i);
      SYNCDBG(8,"Activating pointer %" PRId64, (int64_t)(i));
      LbMouseChangeSpriteAndHotspot(spr, x/2, y/2);
  }
  else
  {
      SYNCDBG(8,"Activating pointer %" PRId64, (int64_t)(40+i));
      if (i >= 0 && i < num_sprites(pointer_sprites))
      {
          spr = get_sprite(pointer_sprites, i);
          LbMouseChangeSpriteAndHotspot(spr, x/2, y/2);
      } else
      {
          WARNLOG("Sprite %" PRId64 " exceeds buffer, setting pointer to none",(int64_t)i);
          LbMouseChangeSpriteAndHotspot(NULL, 0, 0);
      }
  }
  return true;
}

TbBool set_pointer_graphic(int64_t ptr_idx)
{
    int64_t x;
    int64_t y;
    const struct TbSprite* spr;
    SYNCDBG(8, "Setting to %" PRId64, (int64_t)ptr_idx);
    if (pointer_sprites == NULL)
    {
        WARNLOG("Pointer sprites not loaded, setting to none");
        LbMouseChangeSpriteAndHotspot(NULL, 0, 0);
        return false;
    }


  switch (ptr_idx)
  {
  case MousePG_Invisible:
  case MousePG_Arrow:
  case MousePG_Pickaxe:
  case MousePG_Query:
  case MousePG_DenyMark:
  case MousePG_Pickaxe2:
    ptr_idx = get_player_colored_pointer_icon_idx(ptr_idx,my_player_number);
      x = 12; y = 15;
      break;
  case MousePG_Sell:
      ptr_idx = get_player_colored_pointer_icon_idx(ptr_idx,my_player_number);
      x = 17; y = 29;
      break;
  case MousePG_PlaceTrap01:
  case MousePG_PlaceTrap02:
  case MousePG_PlaceTrap03:
  case MousePG_PlaceTrap04:
  case MousePG_PlaceTrap05:
  case MousePG_PlaceTrap06:
  case MousePG_PlaceTrap07:
  case MousePG_PlaceTrap08:
  case MousePG_PlaceTrap09:
  case MousePG_PlaceTrap10:
  case MousePG_PlaceTrap11:
  case MousePG_PlaceTrap12:
  case MousePG_PlaceTrap13:
  case MousePG_PlaceTrap14:
  case MousePG_PlaceDoor01:
  case MousePG_PlaceDoor02:
  case MousePG_PlaceDoor03:
  case MousePG_PlaceDoor04:
  case MousePG_Mystery:
      // 166..181 are place trap pointers with spell icons
  case 166:
  case 167:
  case 168:
  case 169:
  case 170:
  case 171:
  case 172:
  case 173:
  case 174:
  case 175:
  case 176:
  case 177:
  case 178:
  case 179:
  case 180:
  case 181:
      ptr_idx = get_player_colored_pointer_icon_idx(ptr_idx,my_player_number);
      x = 12; y = 38;
      break;
  case  MousePG_SpellCharge0:
  case  MousePG_SpellCharge1:
  case  MousePG_SpellCharge2:
  case  MousePG_SpellCharge3:
  case  MousePG_SpellCharge4:
  case  MousePG_SpellCharge5:
  case  MousePG_SpellCharge6:
  case  MousePG_SpellCharge7:
  case  MousePG_SpellCharge8:
      ptr_idx = get_player_colored_pointer_icon_idx(ptr_idx,my_player_number);
      x = 20; y = 20;
      break;
  case  MousePG_PlaceRoom01:
  case  MousePG_PlaceRoom02:
  case  MousePG_PlaceRoom03:
  case  MousePG_PlaceRoom04:
  case  MousePG_PlaceRoom05:
  case  MousePG_PlaceRoom06:
  case  MousePG_PlaceRoom07:
  case  MousePG_PlaceRoom08:
  case  MousePG_PlaceRoom09:
  case  MousePG_PlaceRoom10:
  case  MousePG_PlaceRoom11:
  case  MousePG_PlaceRoom12:
  case  MousePG_PlaceRoom13:
  case  MousePG_PlaceRoom14:
  case  MousePG_PlaceRoom15:
      ptr_idx = get_player_colored_pointer_icon_idx(ptr_idx,my_player_number);
      x = 12; y = 38;
      break;
  case  MousePG_LockMark:
  // 40..144 are spell pointers
  case  MousePG_Unkn47:
      ptr_idx = get_player_colored_pointer_icon_idx(ptr_idx,my_player_number);
      x = 12; y = 15;
      break;
  case  96:
  case  97:
  case  98:
  case  99:
  case 100:
  case 101:
  case 102:
  case 103:
      ptr_idx = get_player_colored_pointer_icon_idx(ptr_idx,my_player_number);
      x = 12; y = 15;
      break;
  case MousePG_PlaceImpRock:
  case MousePG_PlaceGold:
  case MousePG_PlaceEarth:
  case MousePG_PlaceWall:
  case MousePG_PlacePath:
  case MousePG_PlaceClaimed:
  case MousePG_PlaceLava:
  case MousePG_PlaceWater:
  case MousePG_PlaceGems:
  case MousePG_MkDigger:
  case MousePG_MkCreature:
  case MousePG_MvCreature:
      ptr_idx = get_player_colored_pointer_icon_idx(ptr_idx,my_player_number);
      x = 12; y = 38;
      break;
  default:
      ptr_idx = get_player_colored_pointer_icon_idx(ptr_idx,my_player_number);
      spr = get_new_icon_sprite(ptr_idx);
      if (spr != NULL)
      {
          LbMouseChangeSpriteAndHotspot(spr, spr->SWidth/2, spr->SHeight);
          return true;
      }
    WARNLOG("Unrecognized Mouse Pointer index, %" PRId64,(int64_t)(ptr_idx));
    LbMouseChangeSpriteAndHotspot(NULL, 0, 0);
    return false;
  }
  if (ptr_idx >= 0 && ptr_idx < num_sprites(pointer_sprites)) {
    spr = get_sprite(pointer_sprites, ptr_idx);
    LbMouseChangeSpriteAndHotspot(spr, x, y);
  } else {
    WARNLOG("Sprite %" PRId64 " exceeds buffer, setting pointer to none",(int64_t)ptr_idx);
    LbMouseChangeSpriteAndHotspot(NULL, 0, 0);
  }
  return true;
}

void unload_pointer_file(int64_t hi_res)
{
    set_pointer_graphic_none();
    free_spritesheet(&pointer_sprites);
}

TbBool init_fades_table(void)
{
    /* Raw palette-index bytes (not TbPixel colours) -- pixmap.map_abyss[]
     * they feed is still an unsigned char[] table read directly as palette
     * indices by gui_parchment.c/frontmenu_ingame_map.c, which haven't been
     * migrated yet (see docs/refactor/renderer/02a-pixel-format-design.md's
     * third scope-correction note on the lens/frontend consumers of
     * pixmap.ghost/fade_tables/map_abyss). Converting this to real RGB
     * belongs to that same follow-up pass, not here. */
    static const unsigned char abyss_colours[] = {160, 1, 253};
    char* fname = prepare_file_path(FGrp_StdData, "tables.dat");
    SYNCDBG(0,"Reading fade table file \"%s\".",fname);
    if (LbFileLoadAt(fname, &pixmap) != sizeof(struct TbColorTables))
    {
        compute_fade_tables(&pixmap,engine_palette,engine_palette);
        LbFileSaveAt(fname, &pixmap, sizeof(struct TbColorTables));
    }
    lbDisplay.FadeTable = pixmap.fade_tables;
    /* render_fade_tables/render_ghost/render_alpha wiring retired: every
     * former reader (bflib_render_trig.c, bflib_vidraw*.c, engine_render.c)
     * now computes shade/ghost/alpha blends directly via
     * render_shade()/render_ghost_blend()/render_alpha_blend() instead of
     * indexing a precomputed table -- see
     * docs/refactor/renderer/02a-pixel-format-design.md §2. pixmap itself
     * (and lbDisplay.FadeTable/GlassMap below) stays for now: gui_parchment.c/
     * frontmenu_ingame_map.c/gui_draw.c/MistEffect.cpp still read
     * pixmap.ghost/fade_tables/map_abyss directly as palette-index bytes,
     * and retiring the struct needs that whole cluster migrated first. */
    unsigned char cblack = 144;
    // Update black color
    for (int64_t i = 0; i < 8192; i++)
    {
        if (pixmap.fade_tables[i] == 0) {
            pixmap.fade_tables[i] = cblack;
        }
    }
    for (int64_t i = 0; i < 256; i++) {
        pixmap.map_abyss[i] = abyss_colours[pixmap.ghost[i] * 3 >> 8];
    }
    return true;
}

TbBool init_alpha_table(void)
{
    char* fname = prepare_file_path(FGrp_StdData, "alpha.col");
    SYNCDBG(0,"Reading alpha color table file \"%s\".",fname);
    // Loading file data
    if (LbFileLoadAt(fname, &alpha_sprite_table) != sizeof(struct TbAlphaTables))
    {
        compute_alpha_tables(&alpha_sprite_table,engine_palette,engine_palette);
        LbFileSaveAt(fname, &alpha_sprite_table, sizeof(struct TbAlphaTables));
    }
    return true;
}

TbBool init_rgb2idx_table(void)
{
    char* fname = prepare_file_path(FGrp_StdData, "colours.col");
    SYNCDBG(0,"Reading rgb-to-index color table file \"%s\".",fname);
    // Loading file data
    if (LbFileLoadAt(fname, &kfx_sim_state.colours) != sizeof(TbRGBColorTable))
    {
        compute_rgb2idx_table(kfx_sim_state.colours,engine_palette);
        LbFileSaveAt(fname, &kfx_sim_state.colours, sizeof(TbRGBColorTable));
    }
    return true;
}

/* Kept as a no-op rather than removed: still called (render_power_hand.c)
 * after state changes
 * that used to require re-pointing render_fade_tables/render_ghost/
 * render_alpha at pixmap/alpha_sprite_table. Nothing to sync anymore -- see
 * init_fades_table()'s comment -- but the callback contract stays so those
 * callers don't need touching as part of this pass. */
void sync_render_globals(void)
{
}

/* init_redpal_table()/init_whitepal_table()/white_pal/red_pal/
 * compute_shifted_palette_table() removed: white_pal/red_pal had exactly one
 * remaining consumer (engine_render.c's lbSpriteReMapPtr flash-effect sites),
 * already migrated to SetupSpriteRemapWhiteFlash()/SetupSpriteRemapRedFlash()
 * (bflib_vidraw.c), which compute the same render_flash_blend() formula
 * on demand instead of indexing a precomputed table -- see
 * docs/refactor/renderer/02a-pixel-format-design.md §2.4. Confirmed no other
 * caller exists before deleting. */
void init_colours(void)
{
    init_rgb2idx_table();
}

char *get_vidmode_name(TbScreenMode mode)
{
    TbScreenModeInfo* curr_mdinfo = LbScreenGetModeInfo(mode);
    return curr_mdinfo->Desc;
}

/**
 * Set up a new screen mode suitable for playing the game.
 *
 * @param nmode The mode (index number) that we want to change to.
 * @param falisafe If TRUE the the failsafe resolution will be used if nmode is not available
 * @return Returns the mode that the screen was setup successfully with (or Lb_SCREEN_MODE_INVALID/false when the screen was not setup successfully).
 */
TbScreenMode setup_screen_mode(TbScreenMode nmode, TbBool failsafe)
{
  SYNCDBG(4,"Setting up mode %" PRId64,(int64_t)nmode);
  TbScreenModeInfo* new_mdinfo = LbScreenGetModeInfo(nmode);
  TbScreenMode old_mode = LbScreenActiveMode();
  TbScreenModeInfo* old_mdinfo = LbScreenGetModeInfo(old_mode);
  // we don't want to get the current display when using the "fill all" mode, we want to keep the old version
  if (!(old_mdinfo->VideoFlags & Lb_VF_FILLALL))
  {
    display_id = LbGetCurrentDisplayIndex(); // get current display
  }
  // Check that the desired mode is available for the current display
  if (!LbScreenIsModeAvailable(nmode, display_id))
  {
    if (failsafe)
    {
      ERRORLOG("Unable to setup screen resolution %s (mode %" PRId64 "), trying failsafe mode", new_mdinfo->Desc,(int64_t)nmode);
      nmode = try_failsafe_vidmode();
      if (nmode == Lb_SCREEN_MODE_INVALID)
      {
        force_video_mode_reset = true;
        return Lb_SCREEN_MODE_INVALID;
      }
    }
    else
    {
      ERRORLOG("Unable to setup screen resolution %s (mode %" PRId64 ")", new_mdinfo->Desc,(int64_t)nmode);
      return Lb_SCREEN_MODE_INVALID;
    }
    new_mdinfo = LbScreenGetModeInfo(nmode);
  }
  if (!force_video_mode_reset)
  {
    if ((nmode == old_mode) && (!MinimalResolutionSetup))
    {
      SYNCDBG(6,"Mode %" PRId64 " already active, no changes.",(int64_t)nmode);
      return nmode;
    }
  }
  TbBool hi_res = ((LbGraphicsScreenHeight() < 400) ? false : true);
  int64_t lens_mem = kfx_sim_state.applied_lens_type;
  uint64_t flg_mem = RendererGetDrawFlags();
  TbBool was_minimal_res = (MinimalResolutionSetup || force_video_mode_reset);
  set_pointer_graphic_none();
  if (LbGraphicsScreenHeight() < 200)
  {
      WARNLOG("Unhandled previous Screen Mode %" PRId64 ", Reset skipped",(int64_t)old_mode);
  } else
  {
    if (!MinimalResolutionSetup)
    {
      reset_eye_lenses();
      game_reset_heap_manager();
      unload_pointer_file(hi_res);
    }
    if (nmode != old_mode)
        RendererResetScreen(false);
    if (MinimalResolutionSetup) {
      if (hi_res) {
        FreeVResMinimal();
      }
    } else {
      if (hi_res) {
        FreeVRes256Data();
      } else {
        FreeMcgaData();
      }
    }
    if (!hi_res) ERRORLOG("MCGA Minimal not allowed (Reset)");
    MinimalResolutionSetup = false;
  }
  hi_res = ((new_mdinfo->Height < 400) ? false : true);
  if (new_mdinfo->Height < 200)
  {
      ERRORLOG("Unhandled Screen Mode %" PRId64 ", setup failed",(int64_t)nmode);
      force_video_mode_reset = true;
      return Lb_SCREEN_MODE_INVALID;
  } else
  {
    SYNCDBG(6,"Entering %s mode %" PRId64 ", resolution %" PRId64 "x%" PRId64 ".",hi_res?"hi-res":"low-res",(int64_t)nmode,(int64_t)new_mdinfo->Width,(int64_t)new_mdinfo->Height);
    if (hi_res)
    {
      if (!LoadVRes256Data((int64_t)new_mdinfo->Width*(int64_t)new_mdinfo->Height))
      {
        ERRORLOG("Unable to load VRes256 data files");
        force_video_mode_reset = true;
        return Lb_SCREEN_MODE_INVALID;
      }
    }
    else
    {
      if (!LoadMcgaData())
      {
        ERRORLOG("Loading Mcga files failed");
        force_video_mode_reset = true;
        return Lb_SCREEN_MODE_INVALID;
      }
    }
    if ((nmode != old_mode) || (was_minimal_res))
    {
        if (RendererSetupScreen(nmode, new_mdinfo->Width, new_mdinfo->Height, engine_palette, (hi_res ? 1 : 2), 0) < Lb_SUCCESS)
        {
          ERRORLOG("Unable to setup screen resolution %s (mode %" PRId64 ")", new_mdinfo->Desc,(int64_t)nmode);
          force_video_mode_reset = true;
          return Lb_SCREEN_MODE_INVALID;
        }
    }
    load_pointer_file(hi_res);
  }
  RendererClearScreen(0);
  RendererPresentStepFrame();
  update_screen_mode_data(new_mdinfo->Width, new_mdinfo->Height);
  if (ui_is_parchment_loaded())
    ui_reload_parchment_file(hi_res);
  reinitialise_eye_lens(lens_mem);
  RendererSetDrawFlags(flg_mem);
  game_setup_heap_manager();
  force_video_mode_reset = false;
  SYNCDBG(8,"Finished");
  return nmode;
}

// Registered with ports/display_host_port.h's DisplayHostPort (see
// docs/refactor/todo/check-layering-symbol-level-blind-spot.md);
// bflib_video.c's scaling math can't read units_per_pixel_width/height/
// ui/best/menu (assigned below) directly.
const struct VideoScaleValues *get_video_scale_values(void)
{
    static struct VideoScaleValues values;
    values.units_per_pixel_width = units_per_pixel_width;
    values.units_per_pixel_height = units_per_pixel_height;
    values.units_per_pixel_ui = units_per_pixel_ui;
    values.units_per_pixel_best = units_per_pixel_best;
    values.units_per_pixel_menu = units_per_pixel_menu;
    return &values;
}

TbBool update_screen_mode_data(int64_t width, int64_t height)
{
  // if ((width >= 640) && (height >= 400))
  // {
    pixel_size = 1;
    /*
  } else
  {
    pixel_size = 2;
  }
  */
  int64_t psize = pixel_size;

  MyScreenWidth = width * psize;
  MyScreenHeight = height * psize;
  pixels_per_block = 16 * psize;


  // Adjust scaling factor (units per pixel) based on window resolution compared to the original DK resolutions
  // low-res - units per pixel = 8, low-res - units per pixel = 16 (or upp min is low-res = 4, high-res = 10)

  // In-game scaling (DK original: low-res - 320x200, high-res - 640x400)
  units_per_pixel = (width>height?width:height)/40;// originally was 16 for hires, 8 for lores
  units_per_pixel_min = (width>height?height:width)/40;// originally 10 for hires
  units_per_pixel_width = width/40; // 8 for low res, 16 is "kfx default"
  units_per_pixel_height = height/25; // 8 for low res, 16 is "kfx default"
  units_per_pixel_best = ((is_ar_wider_than_original(width, height)) ? units_per_pixel_height : units_per_pixel_width); // If the screen is wider than 16:10 the height is used; if the screen is narrower than 16:10 the width is used.

  // In-game scaling: UI (for the side bar menu and escape menu)
  int64_t ui_scale = UI_NORMAL_SIZE; // UI_NORMAL_SIZE, UI_HALF_SIZE, or UI_DOUBLE_SIZE (not fully implemented yet)
  units_per_pixel_ui = resize_ui(units_per_pixel_best, ui_scale);

  // In-game scaling: Posession Mode (a 3D 1st person perspective camera)
  calculate_aspect_ratio_factor(width, height);
  first_person_vertical_fov = DEFAULT_FIRST_PERSON_VERTICAL_FOV;
  first_person_horizontal_fov = FOV_based_on_aspect_ratio();

  // Main menu scaling (DK original: 640x480)
  units_per_pixel_menu_height = height/30; // 16 is "kfx default" (640x480)
  units_per_pixel_menu = ((is_menu_ar_wider_than_original(width, height)) ? units_per_pixel_menu_height : units_per_pixel_width); // If the screen is wider than 4:3 the height is used; if the screen is narrower than 4:3 the width is used.

  // Main menu scaling: Campaign map "land view" screen (including the window frame)
  calculate_landview_upp(width, height, LANDVIEW_MAP_WIDTH, LANDVIEW_MAP_HEIGHT); // 16 is "kfx default" for 640x480 game window (1x), a 960x720 frame (1.5x), and a 1280x960 landview (2x)

  LbMouseChangeMoveRatio(kfx_runtime_settings.base_mouse_sensitivity, kfx_runtime_settings.base_mouse_sensitivity);
  LbMouseSetPointerHotspot(0, 0);
  LbScreenSetGraphicsWindow(0, 0, MyScreenWidth/pixel_size, MyScreenHeight/pixel_size);
  LbTextSetWindow(0, 0, MyScreenWidth/pixel_size, MyScreenHeight/pixel_size);
  LbMouseSetup(NULL);
  return true;
}

/**
 * Set up a new screen mode suitable for the frontend or movie playback.
 *
 * @param nmode The mode (index number) that we want to change to.
 * @return Returns the mode that the screen was setup successfully with (or Lb_SCREEN_MODE_INVALID/false when the screen was not setup successfully).
 */
TbScreenMode setup_screen_mode_minimal(TbScreenMode nmode)
{
  SYNCDBG(4,"Setting up mode %" PRId64,(int64_t)nmode);
  TbScreenModeInfo* new_mdinfo = LbScreenGetModeInfo(nmode);
  // we don't want to get the current display when using the "fill all" mode, we want to keep the old version
  TbScreenMode old_mode = LbScreenActiveMode();
  TbScreenModeInfo* old_mdinfo = LbScreenGetModeInfo(old_mode);
  if (!(old_mdinfo->VideoFlags & Lb_VF_FILLALL))
  {
    display_id = LbGetCurrentDisplayIndex(); // get current display
  }
  // Check that the desired mode is available for the current display
  if (!LbScreenIsModeAvailable(nmode, display_id))
  {
      ERRORLOG("Unable to setup screen resolution %s (mode %" PRId64 "), trying failsafe mode", new_mdinfo->Desc,(int64_t)nmode);
      nmode = try_failsafe_vidmode();
      if (nmode == Lb_SCREEN_MODE_INVALID)
      {
        force_video_mode_reset = true;
        return Lb_SCREEN_MODE_INVALID;
      }
      new_mdinfo = LbScreenGetModeInfo(nmode);
  }
  if (!force_video_mode_reset)
  {
    if ((nmode == old_mode) && (MinimalResolutionSetup))
    {
      SYNCDBG(6,"Mode %" PRId64 " already active, no changes.",(int64_t)nmode);
      return nmode;
    }
  }
  TbBool hi_res = ((LbGraphicsScreenHeight() < 400) ? false : true);
  uint64_t flg_mem = RendererGetDrawFlags();
  if (LbGraphicsScreenHeight() < 200)
  {
    WARNLOG("Unhandled previous Screen Mode %" PRId64 ", Reset skipped",(int64_t)old_mode);
  } else
  {
    if (!MinimalResolutionSetup)
    {
      reset_eye_lenses();
      game_reset_heap_manager();
    }
    if ((!MinimalResolutionSetup && !hi_res) || (MinimalResolutionSetup && hi_res))
      unload_pointer_file(hi_res);
    if ((nmode != old_mode) || (force_video_mode_reset))
      RendererResetScreen(false);
    if (hi_res)
    {
      if (MinimalResolutionSetup) {
        FreeVResMinimal();
      } else {
        FreeVRes256Data();
      }
    }
    else
    {
      if (!MinimalResolutionSetup) FreeMcgaData();
    }
    MinimalResolutionSetup = false;
  }
  hi_res = ((new_mdinfo->Height < 400) ? false : true);
  if (new_mdinfo->Height < 200)
  {
      ERRORLOG("Unhandled Screen Mode %" PRId64 ", setup failed",(int64_t)nmode);
      force_video_mode_reset = true;
      return Lb_SCREEN_MODE_INVALID;
  } else
  {
    SYNCDBG(17,"Preparing minimal %s resolution mode",(hi_res ? "high" : "low"));
    MinimalResolutionSetup = true;
    if (hi_res)
    {
      ui_frontend_load_data_from_cd();
      if (!LoadVResMinimal())
      {
        ERRORLOG("Unable to load VRes256 front_load minimal files");
        force_video_mode_reset = true;
        return Lb_SCREEN_MODE_INVALID;
      }
      ui_frontend_load_data_reset();
    }

    if ((nmode != old_mode) || (force_video_mode_reset))
    {
        if (RendererSetupScreen(nmode, new_mdinfo->Width, new_mdinfo->Height, engine_palette, (hi_res ? 1 : 2), 0) < Lb_SUCCESS)
        {
          ERRORLOG("Unable to setup screen resolution %s (mode %" PRId64 ")", new_mdinfo->Desc,(int64_t)nmode);
          force_video_mode_reset = true;
          return Lb_SCREEN_MODE_INVALID;
        }
    }
  }
  RendererClearScreen(0);
  RendererPresentStepFrame();
  update_screen_mode_data(new_mdinfo->Width, new_mdinfo->Height);
  RendererSetDrawFlags(flg_mem);
  force_video_mode_reset = false;
  return nmode;
}

/**
 * Set up a new screen mode with a blank black screen.
 *
 * @param nmode The mode (index number) that we want to change to.
 * @return Returns the mode that the screen was setup successfully with (or Lb_SCREEN_MODE_INVALID/false when the screen was not setup successfully).
 */
TbScreenMode setup_screen_mode_zero(TbScreenMode nmode)
{
  SYNCDBG(4,"Setting up mode %" PRId64,(int64_t)nmode);
  TbScreenModeInfo* new_mdinfo = LbScreenGetModeInfo(nmode);
  // we don't want to get the current display when using the "fill all" mode, we want to keep the old version
  TbScreenMode old_mode = LbScreenActiveMode();
  TbScreenModeInfo* old_mdinfo = LbScreenGetModeInfo(old_mode);
  if (!(old_mdinfo->VideoFlags & Lb_VF_FILLALL))
  {
    display_id = LbGetCurrentDisplayIndex(); // get current display
  }
  // Check that the desired mode is available for the current display
  if (!LbScreenIsModeAvailable(nmode, display_id))
  {
      ERRORLOG("Unable to setup screen resolution %s (mode %" PRId64 "), trying failsafe mode", new_mdinfo->Desc,(int64_t)nmode);
      nmode = try_failsafe_vidmode();
      if (nmode == Lb_SCREEN_MODE_INVALID)
      {
        return Lb_SCREEN_MODE_INVALID;
      }
      new_mdinfo = LbScreenGetModeInfo(nmode);
  }
  LbPaletteDataFillBlack(engine_palette);
  if (RendererSetupScreen(nmode, new_mdinfo->Width, new_mdinfo->Height, engine_palette, 2, 0) < Lb_SUCCESS)
  {
      ERRORLOG("Unable to setup screen resolution %s (mode %" PRId64 ")", new_mdinfo->Desc,(int64_t)nmode);
      return Lb_SCREEN_MODE_INVALID;
  }
  update_screen_mode_data(new_mdinfo->Width, new_mdinfo->Height);
  force_video_mode_reset = true;
  return nmode;
}

/**
 * Set up the screen using the single resolution set by the INGAME_RES config
 * setting (screen_vidmode), falling back to a hardcoded safe mode if it
 * can't be set up.
 *
 * @return Returns the mode that the screen was setup successfully with (or Lb_SCREEN_MODE_INVALID/false when the screen was not setup successfully).
 */
TbScreenMode reenter_video_mode(void)
{
  TbScreenMode scrmode = setup_screen_mode(get_screen_vidmode(), true);
  if (scrmode == Lb_SCREEN_MODE_INVALID)
  {
    FatalError = 1;
    exit_keeper = 1;
    return Lb_SCREEN_MODE_INVALID;
  }
  SYNCLOG("set in-game video as %s (mode %" PRId64 ")", get_vidmode_name(scrmode),(int64_t)scrmode);
  return scrmode;
}

TbBool load_testfont_fonts(void)
{
    testfont[0] = load_font("ldata/frontft1.dat", "ldata/frontft1.tab");
    testfont[1] = load_font("ldata/frontft2.dat", "ldata/frontft2.tab");
    testfont[2] = load_font("ldata/frontft3.dat", "ldata/frontft3.tab");
    testfont[3] = load_font("ldata/frontft4.dat", "ldata/frontft4.tab");
    testfont[4] = load_font("data/font0-0.dat", "data/font-0-0.tab");
    testfont[5] = load_font("data/font0-1.dat", "data/font-0-1.tab");
    testfont[6] = load_font("data/font2-32.dat", "data/font2-32.tab");
    testfont[7] = load_font("data/font2-64.dat", "data/font2-64.tab");
    testfont[8] = load_font("data/font1-64.dat", "data/font1-64.tab");
    testfont[9] = load_font("data/font1-32.dat", "data/font1-32.tab");
    testfont[10] = load_font("ldata/netfont.dat", "ldata/netfont.tab");
    for (int64_t i = 0; i < TESTFONTS_COUNT; ++i) {
        if (!testfont[i]) {
            ERRORLOG("Unable to load test font %" PRId64, (int64_t)(i));
            return false;
        }
    }
    if (!LbDataLoadAll(testfont_load_files) )
    {
      ERRORLOG("Unable to load testfont_load_files files");
      return false;
    }
    return true;
}

void free_testfont_fonts(void)
{
    for (int64_t i = 0; i < TESTFONTS_COUNT; ++i) {
        free_font(&testfont[i]);
    }
    LbDataFreeAll(testfont_load_files);
}

void setup_stuff(void)
{
    setup_texture_block_mem();
    init_fades_table();
    init_alpha_table();
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
