/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file front_simple.h
 *     Header file for front_simple.c.
 * @par Purpose:
 *     Simple frontend screens support.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     11 Mar 2009 - 23 Mar 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/

#ifndef DK_FRONT_SIMPL_H
#define DK_FRONT_SIMPL_H

#include "bflib_basics.h"
#include "globals.h"
#include "bflib_video.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

enum RawBitmaps {
    RBmp_None               =  0x00,
    RBmp_WaitLoading        =  0x01,
    RBmp_WaitNoCD           =  0x02,
    RBmp_SplashLegal        =  0x03,
    RBmp_SplashFx           =  0x04,
    RBmp_SplashLegalWide    =  0x05,
    RBmp_SplashFxWide       =  0x06,
};

struct RawBitmap {
  const char *name;
  int64_t width;
  int64_t height;
  int64_t bpp;
  int64_t fgroup;
  const char *raw_fname;
  const char *pal_fname;
  // 32-bit PNG under fxdata/, used in preference to the raw+pal pair when
  // present (its own size replaces width/height); raw_fname NULL = PNG only.
  const char *png_fname;
};

struct ActiveBitmap {
  const char *name;
  int64_t width;
  int64_t height;
  int64_t bpp;
  TbClockMSec start_tm;
  // Raw 8bpp indexed bitmap bytes loaded straight from a .raw file (1 byte
  // per pixel on disk) -- genuinely a byte buffer, not TbPixel storage.
  unsigned char *raw_data;
  unsigned char *pal_data;
  // Set instead of raw_data/pal_data when the screen came from a PNG, plus
  // the copy scaled to the current screen (rebuilt when the size changes).
  TbPixel *rgba_data;
  TbPixel *scaled_data;
  int64_t scaled_width;
  int64_t scaled_height;
};

/******************************************************************************/
// palette_buf moved to vidfade.h, scratch moved to vidmode.h, big_scratch
// moved to sim_scratch.h (stage 13.2, docs/refactor/stage-13-enforce-and-document.md)
// -- all three were misclassified kfx_frontend globals whose real lowest-rank
// consumers live in kfx_render/kfx_sim.
/******************************************************************************/
// copy_raw8_image_buffer() moved to gui_draw.h (stage 10,
// docs/refactor/stage-10-kfx-frontend.md).
TbBool copy_raw8_image_to_screen_center(const unsigned char *buf,const int64_t img_width,const int64_t img_height);
TbBool show_rawimage_screen(unsigned char *raw,unsigned char *pal,int64_t width,int64_t height,TbClockMSec tmdelay);
TbPixel *decode_png_screen_rgba(const unsigned char *data, size_t len, int64_t *out_width, int64_t *out_height);
/******************************************************************************/
TbBool draw_clear_screen(void);
TbBool init_actv_bitmap_screen(int64_t stype);
TbBool free_actv_bitmap_screen(void);
TbBool show_actv_bitmap_screen(TbClockMSec tmdelay);
/******************************************************************************/

TbBool display_loading_screen(void);
TbBool wait_for_installation_files(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
