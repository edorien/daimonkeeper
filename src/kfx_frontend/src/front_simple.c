/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file front_simple.c
 *     Simple frontend screens support.
 * @par Purpose:
 *     Displays simple bitmap screens, like loading, no CD or startup screens.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     11 Mar 2009 - 23 Mar 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "front_simple.h"
#include "vidmode.h"

#include <math.h>

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_keybrd.h"
#include "bflib_inputctrl.h"
#include "bflib_datetm.h"
#include "bflib_video.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"
#include "bflib_filelst.h"

#include "config.h"
#include "kjm_input.h"
#include "scrcapt.h"
#include "gui_draw.h"
#include "vidfade.h"

#include <spng.h>
#include <stdlib.h>

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define PNG_LEGAL      "daimonkeeper/legal.png"
#define PNG_LEGAL_WIDE "daimonkeeper/legal-wide.png"
#define PNG_SPLASH      "daimonkeeper/splash.png"
#define PNG_SPLASH_WIDE "daimonkeeper/splash-wide.png"
// The product splash has no raw fallback: data/startfx*.raw in an existing
// KeeperFX install is KeeperFX's own splash, not ours.
#ifdef SPRITE_FORMAT_V2

// Format: <name> <width> <height> <bits per pixel> <file load location> <raw file> <palette file> <png file>
struct RawBitmap bitmaps_1280[] = {
  {"Empty Image",                   1280,  960, 8, FGrp_Main,    NULL,                   NULL,                   NULL},
  {"Loading Image",                 1280,  960, 8, FGrp_StdData, "loading-128.raw",      "loading-128.pal",      NULL},
  {"NoCD Image",                     320,  200, 8, FGrp_StdData, "nocd-32.raw",          "nocd-32.pal",          NULL},
  {"DK Legal Splash",               1280,  960, 8, FGrp_StdData, "legal-128.raw",        "legal-128.pal",        PNG_LEGAL},
  {"Product Splash",                1280,  960, 8, FGrp_StdData, NULL,                   NULL,                   PNG_SPLASH},
  {"DK Legal Splash (Wide Screen)", 1920, 1080, 8, FGrp_StdData, "legal-1080p-wide.raw", "legal-1080p-wide.pal", PNG_LEGAL_WIDE},
  {"Product Splash (Wide Screen)",  1920, 1080, 8, FGrp_StdData, NULL,                   NULL,                   PNG_SPLASH_WIDE},
};

// Format: <name> <width> <height> <bits per pixel> <file load location> <raw file> <palette file> <png file>
struct RawBitmap bitmaps_640[] = {
  {"Empty Image",                    640, 480, 8, FGrp_Main,    NULL,                  NULL,                  NULL},
  {"Loading Image",                  640, 480, 8, FGrp_StdData, "loading-64.raw",      "loading-64.pal",      NULL},
  {"NoCD Image",                     320, 200, 8, FGrp_StdData, "nocd-32.raw",         "nocd-32.pal",         NULL},
  {"DK Legal Splash",                640, 480, 8, FGrp_StdData, "legal-64.raw",        "legal-64.pal",        PNG_LEGAL},
  {"Product Splash",                 640, 480, 8, FGrp_StdData, NULL,                  NULL,                  PNG_SPLASH},
  {"DK Legal Splash (Wide Screen)", 1280, 720, 8, FGrp_StdData, "legal-720p-wide.raw", "legal-720p-wide.pal", PNG_LEGAL_WIDE},
  {"Product Splash (Wide Screen)",  1280, 720, 8, FGrp_StdData, NULL,                  NULL,                  PNG_SPLASH_WIDE},
};

// Format: <name> <width> <height> <bits per pixel> <file load location> <raw file> <palette file> <png file>
struct RawBitmap bitmaps_320[] = {
  {"Empty Image",                    320, 200, 8, FGrp_Main,    NULL,                  NULL,                  NULL},
  {"Loading Image",                  320, 200, 8, FGrp_StdData, "loading-32.raw",      "loading-32.pal",      NULL},
  {"NoCD Image",                     320, 200, 8, FGrp_StdData, "nocd-32.raw",         "nocd-32.pal",         NULL},
  {"DK Legal Splash",                320, 200, 8, FGrp_StdData, "legal-32.raw",        "legal-32.pal",        PNG_LEGAL},
  {"Product Splash",                 320, 200, 8, FGrp_StdData, NULL,                  NULL,                  PNG_SPLASH},
  {"DK Legal Splash (Wide Screen)", 1280, 720, 8, FGrp_StdData, "legal-720p-wide.raw", "legal-720p-wide.pal", PNG_LEGAL_WIDE},
  {"Product Splash (Wide Screen)",  1280, 720, 8, FGrp_StdData, NULL,                  NULL,                  PNG_SPLASH_WIDE},
};
#else

// Format: <name> <width> <height> <bits per pixel> <file load location> <raw file> <palette file> <png file>
struct RawBitmap bitmaps_1280[] = {
  {"Empty Image",                    640,  480, 8, FGrp_Main,    NULL,                   NULL,                   NULL},
  {"Loading Image",                  640,  480, 8, FGrp_StdData, "loading64.raw",        "loading64.pal",        NULL},
  {"NoCD Image",                     320,  200, 8, FGrp_StdData, "nocd.raw",             "nocd.pal",             NULL},
  {"DK Legal Splash",                640,  480, 8, FGrp_StdData, "legal64.raw",          "legal64.pal",          PNG_LEGAL},
  {"Product Splash",                 640,  480, 8, FGrp_StdData, NULL,                   NULL,                   PNG_SPLASH},
  {"DK Legal Splash (Wide Screen)", 1920, 1080, 8, FGrp_StdData, "legal-1080p-wide.raw", "legal-1080p-wide.pal", PNG_LEGAL_WIDE},
  {"Product Splash (Wide Screen)",  1920, 1080, 8, FGrp_StdData, NULL,                   NULL,                   PNG_SPLASH_WIDE},
};

// Format: <name> <width> <height> <bits per pixel> <file load location> <raw file> <palette file> <png file>
struct RawBitmap bitmaps_640[] = {
  {"Empty Image",                    640, 480, 8, FGrp_Main,    NULL,                  NULL,                  NULL},
  {"Loading Image",                  640, 480, 8, FGrp_StdData, "loading64.raw",       "loading64.pal",       NULL},
  {"NoCD Image",                     320, 200, 8, FGrp_StdData, "nocd.raw",            "nocd.pal",            NULL},
  {"DK Legal Splash",                640, 480, 8, FGrp_StdData, "legal64.raw",         "legal64.pal",         PNG_LEGAL},
  {"Product Splash",                 640, 480, 8, FGrp_StdData, NULL,                  NULL,                  PNG_SPLASH},
  {"DK Legal Splash (Wide Screen)", 1280, 720, 8, FGrp_StdData, "legal-720p-wide.raw", "legal-720p-wide.pal", PNG_LEGAL_WIDE},
  {"Product Splash (Wide Screen)",  1280, 720, 8, FGrp_StdData, NULL,                  NULL,                  PNG_SPLASH_WIDE},
};

// Format: <name> <width> <height> <bits per pixel> <file load location> <raw file> <palette file> <png file>
struct RawBitmap bitmaps_320[] = {
  {"Empty Image",                    320, 200, 8, FGrp_Main,    NULL,                  NULL,                  NULL},
  {"Loading Image",                  320, 200, 8, FGrp_StdData, "loading32.raw",       "loading32.pal",       NULL},
  {"NoCD Image",                     320, 200, 8, FGrp_StdData, "nocd.raw",            "nocd.pal",            NULL},
  {"DK Legal Splash",                320, 200, 8, FGrp_StdData, "legal32.raw",         "legal32.pal",         PNG_LEGAL},
  {"Product Splash",                 320, 200, 8, FGrp_StdData, NULL,                  NULL,                  PNG_SPLASH},
  {"DK Legal Splash (Wide Screen)", 1280, 720, 8, FGrp_StdData, "legal-720p-wide.raw", "legal-720p-wide.pal", PNG_LEGAL_WIDE},
  {"Product Splash (Wide Screen)",  1280, 720, 8, FGrp_StdData, NULL,                  NULL,                  PNG_SPLASH_WIDE},
};

#endif
struct ActiveBitmap astd_bmp;
struct ActiveBitmap nocd_bmp;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
/******************************************************************************/

// copy_raw8_image_buffer() moved to gui_draw.c (stage 10,
// docs/refactor/stage-10-kfx-frontend.md) -- gui_draw.c/gui_parchment.c
// (kfx_frontend's lower internal sub-layer) needed it without depending
// on front_simple.h.

/**
 * Copies the given RAW image to the center of the screen buffer and swaps video
 * buffers to make the image visible.
 *
 * This function will also scale the image while maintaing its aspect ratio.
 *
 * @param buf Pointer to the RAW image data.
 * @param img_width Width of the RAW image.
 * @param img_height Height of the RAW image.
 *
 * @return Returns true if the operation succeeds.
 */
TbBool copy_raw8_image_to_screen_center(const unsigned char *buf, const int64_t img_width, const int64_t img_height)
{
    // Get screen dimensions
    int64_t screen_width = LbScreenWidth();
    int64_t screen_height = LbScreenHeight();

    // Get the scaling ratios
    double width_ratio = (double)screen_width / (double)img_width;
    double height_ratio = (double)screen_height / (double)img_height;

    // Choose the smaller ratio to maintain the aspect ratio and fit the entire image
    double ratio = width_ratio < height_ratio ? width_ratio : height_ratio;

    // Calculate the scaled dimensions and round up
    int64_t scaled_width = ceil(img_width * ratio);
    int64_t scaled_height = ceil(img_height * ratio);

    // Calculate starting point coordinates to center the image
    int64_t coord_x = (screen_width - scaled_width) >> 1;
    int64_t coord_y = (screen_height - scaled_height) >> 1;

    // Debuglog
    SYNCDBG(18, "Starting; src %" PRId64 ",%" PRId64 " dest %" PRId64 ",%" PRId64 " pos %" PRId64 ",%" PRId64,
        (int64_t)img_width, (int64_t)img_height,
        (int64_t)scaled_width,  (int64_t)scaled_height,
        (int64_t)coord_x,  (int64_t)coord_y);

    // Lock the screen
    if (RendererLockFramebuffer() != Lb_SUCCESS)
        return false;

    // Copy image buffer to screen buffer
    copy_raw8_image_buffer(RendererGetFramebuffer(), LbGraphicsScreenWidth(), LbGraphicsScreenHeight(),
                           scaled_width, scaled_height, coord_x, coord_y, buf, img_width, img_height);

    // Perform any screen capturing
    perform_any_screen_capturing();

    // Unlock the screen
    RendererUnlockFramebuffer();

    // Swap video buffers to make the image visible
    RendererPresentStepFrame();

    return true;
}

/**
 * Draws a 32-bit bitmap screen scaled to fit the screen, centred, and swaps
 * video buffers. The scaled copy is kept in the bitmap until the screen size changes.
 *
 * @return Returns true if the operation succeeds.
 */
static TbBool copy_rgba_bitmap_to_screen_center(struct ActiveBitmap *actv_bmp)
{
    const int64_t screen_width = LbScreenWidth();
    const int64_t screen_height = LbScreenHeight();
    const double width_ratio = (double)screen_width / (double)actv_bmp->width;
    const double height_ratio = (double)screen_height / (double)actv_bmp->height;
    const double ratio = width_ratio < height_ratio ? width_ratio : height_ratio;
    const int64_t scaled_width = (int64_t)ceil(actv_bmp->width * ratio);
    const int64_t scaled_height = (int64_t)ceil(actv_bmp->height * ratio);
    if ((scaled_width <= 0) || (scaled_height <= 0))
        return false;
    if ((actv_bmp->scaled_data == NULL) || (actv_bmp->scaled_width != scaled_width)
     || (actv_bmp->scaled_height != scaled_height))
    {
        free(actv_bmp->scaled_data);
        actv_bmp->scaled_data = malloc(sizeof(TbPixel) * scaled_width * scaled_height);
        if ((actv_bmp->scaled_data == NULL) || !resample_rgba_image(actv_bmp->rgba_data, actv_bmp->width,
            actv_bmp->height, actv_bmp->scaled_data, scaled_width, scaled_height))
        {
            free(actv_bmp->scaled_data);
            actv_bmp->scaled_data = NULL;
            return false;
        }
        actv_bmp->scaled_width = scaled_width;
        actv_bmp->scaled_height = scaled_height;
    }
    if (RendererLockFramebuffer() != Lb_SUCCESS)
        return false;
    copy_rgba_image_buffer(RendererGetFramebuffer(), LbGraphicsScreenWidth(), LbGraphicsScreenHeight(),
        (screen_width - scaled_width) >> 1, (screen_height - scaled_height) >> 1,
        actv_bmp->scaled_data, scaled_width, scaled_height);
    perform_any_screen_capturing();
    RendererUnlockFramebuffer();
    RendererPresentStepFrame();
    return true;
}

/**
 * Decodes a PNG held in memory to 32-bit pixels.
 * @return The pixels (free() them), or NULL if it isn't a usable PNG.
 */
TbPixel *decode_png_screen_rgba(const unsigned char *data, size_t len, int64_t *out_width, int64_t *out_height)
{
    _Static_assert(sizeof(TbPixel) == 4, "TbPixel must be packed RGBA8 to take spng output directly");
    spng_ctx *ctx = spng_ctx_new(0);
    if (ctx == NULL)
        return NULL;
    TbPixel *pixels = NULL;
    struct spng_ihdr ihdr;
    size_t size = 0;
    if ((spng_set_png_buffer(ctx, data, len) == 0) && (spng_get_ihdr(ctx, &ihdr) == 0)
     && (ihdr.width > 0) && (ihdr.height > 0) && (ihdr.width <= 8192) && (ihdr.height <= 8192)
     && (spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &size) == 0)
     && (size == sizeof(TbPixel) * ihdr.width * ihdr.height))
    {
        pixels = malloc(size);
        if ((pixels != NULL) && (spng_decode_image(ctx, pixels, size, SPNG_FMT_RGBA8, SPNG_DECODE_TRNS) != 0))
        {
            free(pixels);
            pixels = NULL;
        }
    }
    spng_ctx_free(ctx);
    if (pixels != NULL)
    {
        *out_width = ihdr.width;
        *out_height = ihdr.height;
    }
    return pixels;
}

/**
 * Loads a PNG as 32-bit pixels.
 * @return The pixels (free() them), or NULL if the file is missing or unreadable.
 */
static TbPixel *load_png_rgba(int64_t fgroup, const char *fname, int64_t *out_width, int64_t *out_height)
{
    char fpath[2048];
    prepare_file_path_buf(fpath, sizeof(fpath), fgroup, fname);
    const int64_t fsize = LbFileLength(fpath);
    if (fsize <= 0)
    {
        WARNLOG("No image \"%s\"", fpath);
        return NULL;
    }
    unsigned char *fbuf = malloc(fsize);
    TbFileHandle fh = (fbuf != NULL) ? LbFileOpen(fpath, Lb_FILE_MODE_READ_ONLY) : NULL;
    int64_t rlen = -1;
    if (fh != NULL)
    {
        rlen = LbFileRead(fh, fbuf, (uint64_t)fsize);
        LbFileClose(fh);
    }
    TbPixel *pixels = (rlen == fsize) ? decode_png_screen_rgba(fbuf, (size_t)fsize, out_width, out_height) : NULL;
    free(fbuf);
    if (pixels == NULL)
        WARNLOG("Couldn't read image \"%s\"", fpath);
    return pixels;
}

/** Polls input; true (and the key/click consumed) when the player skips the screen. */
static TbBool splash_skip_requested(void)
{
    poll_inputs();
    if (is_key_pressed(KC_SPACE, KMod_DONTCARE)
     || is_key_pressed(KC_ESCAPE, KMod_DONTCARE)
     || is_key_pressed(KC_RETURN, KMod_DONTCARE)
     || is_mouse_pressed_lrbutton())
    {
        clear_key_pressed(KC_SPACE);
        clear_key_pressed(KC_ESCAPE);
        clear_key_pressed(KC_RETURN);
        clear_mouse_pressed_lrbutton();
        return true;
    }
    return false;
}

static TbClockMSec splash_redraw_interval(TbClockMSec tmdelay)
{
    TbClockMSec tmdelta = tmdelay / 100;
    if (tmdelta > 100)
        tmdelta = 100;
    if (tmdelta < 10)
        tmdelta = 10;
    return tmdelta;
}

TbBool show_rawimage_screen(unsigned char *raw,unsigned char *pal,int64_t width,int64_t height,TbClockMSec tmdelay)
{
    RendererPaletteSet(pal);
    TbClockMSec end_time = LbTimerClock() + tmdelay;
    TbClockMSec tmdelta = splash_redraw_interval(tmdelay);
    while (LbTimerClock() < end_time)
    {
        copy_raw8_image_to_screen_center(raw, width, height);
        if (splash_skip_requested())
            break;
        LbSleepFor(tmdelta);
    }
    return true;
}

/**
 * Resets bitmap screen structure to zero without freeing.
 * @return Returns true on success.
 */
int64_t clear_bitmap_screen(struct ActiveBitmap *actv_bmp)
{
  memset(actv_bmp, 0, sizeof(struct ActiveBitmap));
  return true;
}

/**
 * Frees memory used by bitmap screen and zeroes the data.
 * @return Returns true on success.
 */
int64_t free_bitmap_screen(struct ActiveBitmap *actv_bmp)
{
  free(actv_bmp->raw_data);
  free(actv_bmp->pal_data);
  free(actv_bmp->rgba_data);
  free(actv_bmp->scaled_data);
  return clear_bitmap_screen(actv_bmp);
}

/**
 * Initializes bitmap screen. Loads all files and sets variables.
 * @return Returns true on success.
 */
TbBool init_bitmap_screen(struct ActiveBitmap *actv_bmp,int64_t stype)
{
  struct RawBitmap *rbmp;

  // Decide best image to show based on the width of the screen
  if (LbGraphicsScreenWidth() >= 1280)
    rbmp = &bitmaps_1280[stype];
  else if (LbGraphicsScreenWidth() >= 640)
    rbmp = &bitmaps_640[stype];
  else
    rbmp = &bitmaps_320[stype];

  clear_bitmap_screen(actv_bmp);
  actv_bmp->name = rbmp->name;
  actv_bmp->width = rbmp->width;
  actv_bmp->height = rbmp->height;
  actv_bmp->bpp = rbmp->bpp;
  actv_bmp->start_tm = LbTimerClock();
  SYNCDBG(18,"Starting; src %" PRId64 ",%" PRId64 " bpp %" PRId64,(int64_t)actv_bmp->width,(int64_t)actv_bmp->height,(int64_t)actv_bmp->bpp);
  // 32-bit PNG first
  if (rbmp->png_fname != NULL)
  {
    int64_t png_width;
    int64_t png_height;
    actv_bmp->rgba_data = load_png_rgba(FGrp_FxData, rbmp->png_fname, &png_width, &png_height);
    if (actv_bmp->rgba_data != NULL)
    {
      actv_bmp->width = png_width;
      actv_bmp->height = png_height;
      actv_bmp->bpp = 32;
      return true;
    }
  }
  if (rbmp->raw_fname == NULL)
  {
    clear_bitmap_screen(actv_bmp);
    return false;
  }
  // Load PAL
  int64_t ldsize = PALETTE_SIZE;
  unsigned char* buf = load_data_file_to_buffer(&ldsize, rbmp->fgroup, "%s", rbmp->pal_fname);
  if (buf == NULL)
  {
    ERRORLOG("Couldn't load palette file for %s screen",rbmp->name);
    clear_bitmap_screen(actv_bmp);
    return false;
  }
  actv_bmp->pal_data = (unsigned char *)buf;
  // Load RAW
  ldsize = actv_bmp->width*actv_bmp->height*((actv_bmp->bpp >> 3) + ((actv_bmp->bpp%8)>0));
  buf = load_data_file_to_buffer(&ldsize, rbmp->fgroup, "%s", rbmp->raw_fname);
  if (buf == NULL)
  {
    ERRORLOG("Couldn't load raw bitmap file for %s screen",rbmp->name);
    free(actv_bmp->pal_data);
    clear_bitmap_screen(actv_bmp);
    return false;
  }
  actv_bmp->raw_data = buf;
  return true;
}

/** Draws active bitmap on screen.
 *
 * @param actv_bmp The active bitmap structure to be drawn.
 * @return Returns true on success.
 */
TbBool draw_bitmap_screen(struct ActiveBitmap *actv_bmp)
{
    if (actv_bmp->rgba_data != NULL)
      return copy_rgba_bitmap_to_screen_center(actv_bmp);
    if (actv_bmp->pal_data == NULL)
      return false;
    RendererPaletteSet(actv_bmp->pal_data);
    if (actv_bmp->raw_data == NULL)
      return false;
    copy_raw8_image_to_screen_center(actv_bmp->raw_data,actv_bmp->width,actv_bmp->height);
    return true;
}

/** Draws active bitmap on screen, without setting palette.
 *
 * @param actv_bmp The active bitmap structure to be re-drawn.
 * @return Returns true on success.
 */
int64_t redraw_bitmap_screen(struct ActiveBitmap *actv_bmp)
{
    if (actv_bmp->rgba_data != NULL)
      return copy_rgba_bitmap_to_screen_center(actv_bmp);
    if (actv_bmp->raw_data == NULL)
      return false;
    copy_raw8_image_to_screen_center(actv_bmp->raw_data,actv_bmp->width,actv_bmp->height);
    return true;
}

/**
 * Shows active bitmap screen for specific time.
 * @return Returns true on success.
 */
int64_t show_bitmap_screen(struct ActiveBitmap *actv_bmp,TbClockMSec tmdelay)
{
    if (actv_bmp->rgba_data != NULL)
    {
        TbClockMSec end_time = LbTimerClock() + tmdelay;
        TbClockMSec tmdelta = splash_redraw_interval(tmdelay);
        while (LbTimerClock() < end_time)
        {
            copy_rgba_bitmap_to_screen_center(actv_bmp);
            if (splash_skip_requested())
                break;
            LbSleepFor(tmdelta);
        }
        return true;
    }
    if (actv_bmp->pal_data == NULL)
      return false;
    if (actv_bmp->raw_data == NULL)
      return false;
    show_rawimage_screen(actv_bmp->raw_data,actv_bmp->pal_data,actv_bmp->width,actv_bmp->height,tmdelay);
    return true;
}

/**
 * Clears the screen and its palette.
 * @return Returns true on success.
 */
TbBool draw_clear_screen(void)
{
    LbPaletteDataFillBlack(palette_buf);
    RendererPaletteSet(palette_buf);
    RendererClearScreen(0);
    RendererPresentStepFrame();
    return true;
}

/** Initializes bitmap screen on static struct.
 *  Loads all files and sets variables.
 *
 * @param stype Bitmap screen type selector.
 * @return Returns true on success.
 */
TbBool init_actv_bitmap_screen(int64_t stype)
{
    return init_bitmap_screen(&astd_bmp,stype);
}

/**
 * Frees static active bitmap struct.
 */
TbBool free_actv_bitmap_screen(void)
{
  return free_bitmap_screen(&astd_bmp);
}

/**
 * Shows active bitmap screen from static struct for specific time.
 * @return Returns true on success.
 */
TbBool show_actv_bitmap_screen(TbClockMSec tmdelay)
{
  return show_bitmap_screen(&astd_bmp,tmdelay);
}

/**
 * Displays the loading screen.
 * Will work properly only on any resolutions.
 * @return Returns true on success.
 */
TbBool display_loading_screen(void)
{
    draw_clear_screen();
    TbBool done = init_bitmap_screen(&astd_bmp, RBmp_WaitLoading);
    if (done)
    {
      redraw_bitmap_screen(&astd_bmp);
      LbPaletteStopOpenFade();
      ProperForcedFadePalette(astd_bmp.pal_data, 8, Lb_PALETTE_FADE_CLOSED);
    }
    if (done)
      free_bitmap_screen(&astd_bmp);
    return done;
}

TbBool wait_for_installation_files(void)
{
  char ffullpath[2048];
  int64_t was_locked = LbScreenIsLocked();
  prepare_file_path_buf(ffullpath, sizeof(ffullpath), FGrp_StdData, "bluepal.dat");
  if ( LbFileExists(ffullpath) )
    return true;
  if ( was_locked )
    RendererUnlockFramebuffer();
  SYNCMSG("Installation file not found, waiting");
  if (!init_bitmap_screen(&nocd_bmp,RBmp_WaitNoCD))
  {
      ERRORLOG("Unable to display CD wait splash");
      return false;
  }
  draw_bitmap_screen(&nocd_bmp);
  uint64_t counter = 0;
  while ( !exit_keeper )
  {
      if ( LbFileExists(ffullpath) )
        break;
      for (uint64_t i = 0; i < 10; i++)
      {
        redraw_bitmap_screen(&nocd_bmp);
        do
        {
            if (!poll_inputs())
                exit_keeper = 1;
            if ((exit_keeper) || (quit_game))
              break;
        } while (!LbIsActive());
        if (is_key_pressed(KC_Q,KMod_DONTCARE) || is_key_pressed(KC_X,KMod_DONTCARE) || is_key_pressed(KC_ESCAPE, KMod_DONTCARE))
        {
          ERRORLOG("User requested quit, giving up");
          clear_key_pressed(KC_Q);
          clear_key_pressed(KC_X);
          clear_key_pressed(KC_ESCAPE);
          exit_keeper = 1;
          break;
        }
        LbSleepFor(100);
      }
      // One 'counter' cycle lasts approx. 1 second.
      counter++;
      if (counter > 5)
      {
          ERRORLOG("Wait time too long, giving up");
          exit_keeper = 1;
      }
  }
  SYNCMSG("Finished waiting for installation after %" PRIu64 " seconds",(uint64_t)(counter));
  free_bitmap_screen(&nocd_bmp);
  if ( was_locked )
    RendererLockFramebuffer();
  return (!exit_keeper);
}

/******************************************************************************/
