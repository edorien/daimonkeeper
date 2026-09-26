/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file gui_draw.c
 *     GUI elements drawing functions.
 * @par Purpose:
 *     On-screen drawing of GUI elements, like buttons, menus and panels.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     20 Jan 2009 - 30 Jan 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "gui_draw.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_video.h"
#include "bflib_sprite.h"
#include "bflib_planar.h"
#include "bflib_vidraw.h"
#include "bflib_sprfnt.h"
#include "bflib_guibtns.h"
#include "bflib_datetm.h"
#include "config_strings.h"

#include "player_data.h"
#include "frontend.h"
#include "config_spritecolors.h"
#include "custom_sprites.h"
#include "sprites.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
char gui_textbuf[TEXT_BUFFER_LENGTH];
// gui_slab moved to kfx_render's vidmode.h/vidmode_data.cpp (stage
// 13.3, docs/refactor/stage-13-enforce-and-document.md).
unsigned char *frontend_background;
// frontend_sprite moved to kfx_render's vidmode.h/vidmode_data.cpp
// (stage 13.3, docs/refactor/stage-13-enforce-and-document.md).
// gui_blink_rate/neutral_flash_rate moved to kfx_config's
// kfx_config_state.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md); default value 1 for both set in
// main.cpp's init_keeper().
char gui_room_type_highlighted;
char gui_door_type_highlighted;
const int64_t pixels_needed[] = {
    1,
    1,
    AROUND_2x2_PIXEL,
    AROUND_3x3_PIXEL,
    AROUND_4x4_PIXEL,
    AROUND_5x5_PIXEL,
    AROUND_6x6_PIXEL,
};
// draw_square moved to kfx_sim's power_hand.h/power_hand.c (stage 13.3,
// docs/refactor/stage-13-enforce-and-document.md).
/******************************************************************************/

int64_t scale_pixel(int64_t basic_zoom)
{
    int64_t pixels_per_map_dot = 5;
    if (basic_zoom >= ONE_PIXEL)
    {
        pixels_per_map_dot = 1;
    }
    else if (basic_zoom >= TWO_PIXELS)
    {
        pixels_per_map_dot = 2;
    }
    else if (basic_zoom >= THREE_PIXELS)
    {
        pixels_per_map_dot = 3;
    }
    else if (basic_zoom >= FOUR_PIXELS)
    {
        pixels_per_map_dot = 4;
    } // 128 = 5

    int64_t draw_pixels = scale_fixed_DK_value(pixels_per_map_dot) * 2 / 5;
    if (draw_pixels > 6)
    {
        draw_pixels = 6; // We just support 6 pixels for now
    }
    return draw_pixels;
}

int64_t get_pixels_scaled_and_zoomed(int64_t basic_zoom)
{
    int64_t draw_pixels = scale_pixel(basic_zoom);
    return pixels_needed[draw_pixels];
}

/** Copies the given RAW image at given point of screen buffer.
 *
 * @param dst_buf Destination screen buffer.
 * @param scanline Amount of bytes making up one line in screen buffer.
 * @param nlines Amount of lines in screen buffer.
 * @param dst_width Destination image width.
 * @param dst_height Destination image height.
 * @param spw Starting position in screen buffer.
 * @param sph Starting position in screen buffer.
 * @param src_buf Source image buffer.
 * @param src_width Source image width.
 * @param src_height Source image height.
 *     Factor of 2 would mean every pixel is repeated in both dimensions and drawn 2*2 times.
 * @return Gives true on success.
 */
static void fill_pixel_run(TbPixel *dst, TbPixel colour, int64_t count)
{
    for (int64_t i = 0; i < count; i++)
        dst[i] = colour;
}

TbBool copy_raw8_image_buffer(TbPixel *dst_buf,const int64_t scanline,const int64_t nlines,const int64_t dst_width,const int64_t dst_height,
    const int64_t spw,const int64_t sph,const unsigned char *src_buf,const int64_t src_width,const int64_t src_height)
{
    TbPixel* dst;
    const unsigned char *pal = RendererGetActivePalette();
    const TbPixel black = TbPixel_RGB(0, 0, 0);
    SYNCDBG(18, "Starting; screen buf %" PRId64 ",%" PRId64 " screen size %" PRId64 ",%" PRId64 " dst pos %" PRId64 ",%" PRId64 " src %" PRId64 ",%" PRId64, (int64_t)scanline, (int64_t)nlines, (int64_t)dst_width, (int64_t)dst_height, (int64_t)spw, (int64_t)sph, (int64_t)src_width, (int64_t)src_height);
    // Source pixel coords
    int64_t sw = 0;
    int64_t sh = 0;
    // Clearing top of the canvas
    for (sh = 0; sh < sph; sh++)
    {
        dst = dst_buf + (sh)*scanline;
        fill_pixel_run(dst, black, scanline);
  }
  // Clearing bottom of the canvas
  // (Note: it must be done before drawing, to make sure we won't overwrite last line)
  for (sh=sph+dst_height; sh<nlines; sh++)
  {
      dst = dst_buf + (sh)*scanline;
      fill_pixel_run(dst, black, scanline);
  }
  // Now drawing
  int64_t dhstart = sph;
  for (sh=0; sh<src_height; sh++)
  {
      int64_t dhend = sph + (dst_height * (sh + 1) / src_height);
      const unsigned char* src = src_buf + sh * src_width;
      // make for(k=0;k<dhend-dhstart;k++) but restrict k to draw area
      int64_t mhmin = max(0, -dhstart);
      int64_t mhmax = min(dhend - dhstart, nlines - dhstart);
      for (int64_t k = mhmin; k < mhmax; k++)
      {
          dst = dst_buf + (dhstart+k)*scanline;
          int64_t dwstart = spw;
          if (dwstart > 0) {
              fill_pixel_run(dst, black, dwstart);
          }
          for (sw=0; sw<src_width; sw++)
          {
              int64_t dwend = spw + (dst_width * (sw + 1) / src_width);
              // make for(i=0;i<dwend-dwstart;i++) but restrict i to draw area
              int64_t mwmin = max(0, -dwstart);
              int64_t mwmax = min(dwend - dwstart, scanline - dwstart);
              TbPixel colour = resolve_indexed_pixel(src[sw], pal);
              for (int64_t i = mwmin; i < mwmax; i++)
              {
                  dst[dwstart+i] = colour;
              }
              dwstart = dwend;
          }
          if (dwstart < scanline) {
              fill_pixel_run(dst+dwstart, black, scanline-dwstart);
          }
      }
      dhstart = dhend;
  }
  return true;
}

/** Rect-clipped sibling of copy_raw8_image_buffer.
 *
 * copy_raw8_image_buffer always clears every pixel of every scanline it
 * touches out to the full buffer width/height (scanline/nlines) -- fine
 * when its caller owns the whole screen (the land-view cutscene, the
 * parchment map, the frontend backdrop), actively destructive if the
 * image needs to sit inside a panel alongside other UI, since it would
 * blank whatever's drawn beside it every frame.
 *
 * This variant confines both the drawing and the margin-clearing to
 * [rect_x, rect_x+rect_w) x [rect_y, rect_y+rect_h) of dst_buf -- nothing
 * outside that rect is ever touched. spw/sph are relative to the rect's
 * own top-left corner (0,0 = rect_x,rect_y) rather than the buffer
 * origin, and may be negative to pan a source image larger than the
 * rect, same convention copy_raw8_image_buffer uses relative to the
 * whole screen.
 *
 * @return Gives true on success, false if the rect is degenerate or
 *     entirely outside the buffer.
 */
TbBool copy_raw8_image_buffer_rect(TbPixel *dst_buf,const int64_t scanline,const int64_t nlines,
    const int64_t rect_x,const int64_t rect_y,const int64_t rect_w,const int64_t rect_h,
    const int64_t dst_width,const int64_t dst_height,const int64_t spw,const int64_t sph,
    const unsigned char *src_buf,const int64_t src_width,const int64_t src_height)
{
    TbPixel* dst;
    const unsigned char *pal = RendererGetActivePalette();
    const TbPixel black = TbPixel_RGB(0, 0, 0);
    SYNCDBG(18, "Starting; rect %" PRId64 ",%" PRId64 " %" PRId64 ",%" PRId64 " dst size %" PRId64 ",%" PRId64 " pan %" PRId64 ",%" PRId64 " src %" PRId64 ",%" PRId64,
        (int64_t)(rect_x), (int64_t)(rect_y), (int64_t)(rect_w), (int64_t)(rect_h), (int64_t)(dst_width), (int64_t)(dst_height), (int64_t)(spw), (int64_t)(sph), (int64_t)(src_width), (int64_t)(src_height));

    int64_t clip_x0 = max(0, rect_x);
    int64_t clip_y0 = max(0, rect_y);
    int64_t clip_x1 = min(scanline, rect_x + rect_w);
    int64_t clip_y1 = min(nlines, rect_y + rect_h);
    if ((clip_x1 <= clip_x0) || (clip_y1 <= clip_y0))
        return false;

    // Absolute buffer position of the (possibly panned, possibly
    // off-rect) drawn image's top-left corner.
    int64_t abs_spw = rect_x + spw;
    int64_t abs_sph = rect_y + sph;

    // Clear the rect's own margin above/below the drawn image.
    for (int64_t sh = clip_y0; sh < min(abs_sph, clip_y1); sh++)
    {
        dst = dst_buf + sh*scanline;
        fill_pixel_run(dst + clip_x0, black, clip_x1 - clip_x0);
    }
    for (int64_t sh = max(abs_sph+dst_height, clip_y0); sh < clip_y1; sh++)
    {
        dst = dst_buf + sh*scanline;
        fill_pixel_run(dst + clip_x0, black, clip_x1 - clip_x0);
    }

    // Now drawing, same source-to-destination scan as
    // copy_raw8_image_buffer, clamped against the rect instead of the
    // whole buffer.
    int64_t dhstart = abs_sph;
    for (int64_t sh = 0; sh < src_height; sh++)
    {
        int64_t dhend = abs_sph + (dst_height * (sh + 1) / src_height);
        const unsigned char* src = src_buf + sh * src_width;
        int64_t mhmin = max(clip_y0, dhstart) - dhstart;
        int64_t mhmax = min(dhend, clip_y1) - dhstart;
        for (int64_t k = mhmin; k < mhmax; k++)
        {
            dst = dst_buf + (dhstart+k)*scanline;
            int64_t dwstart = abs_spw;
            if (dwstart > clip_x0) {
                fill_pixel_run(dst + clip_x0, black, min(dwstart, clip_x1) - clip_x0);
            }
            for (int64_t sw=0; sw<src_width; sw++)
            {
                int64_t dwend = abs_spw + (dst_width * (sw + 1) / src_width);
                int64_t mwmin = max(clip_x0, dwstart) - dwstart;
                int64_t mwmax = min(dwend, clip_x1) - dwstart;
                TbPixel colour = resolve_indexed_pixel(src[sw], pal);
                for (int64_t i = mwmin; i < mwmax; i++)
                {
                    dst[dwstart+i] = colour;
                }
                dwstart = dwend;
            }
            if (dwstart < clip_x1) {
                int64_t from = max(dwstart, clip_x0);
                fill_pixel_run(dst + from, black, clip_x1 - from);
            }
        }
        dhstart = dhend;
    }
    return true;
}

void draw_bar64k(int64_t pos_x, int64_t pos_y, int64_t units_per_px, int64_t width)
{
    if (width < 72*units_per_px/16)
    {
        ERRORLOG("Bar is too small");
        return;
    }
    // Button opening sprite
    const struct TbSprite* spr = get_button_sprite(GBS_frontend_button_std_l);
    int64_t x = pos_x;
    LbSpriteDrawResized(x, pos_y, units_per_px, spr);
    x += (spr->SWidth * units_per_px + 8) / 16;
    // Button body
    int64_t body_end = pos_x + width - 2 * ((32 * units_per_px + 8) / 16);
    while (x < body_end)
    {
        spr = get_button_sprite(GBS_frontend_button_std_c);
        LbSpriteDrawResized(x/pixel_size, pos_y/pixel_size, units_per_px, spr);
        x += spr->SWidth * units_per_px / 16;
    }
    x = body_end;
    spr = get_button_sprite(GBS_frontend_button_std_c);
    LbSpriteDrawResized(x/pixel_size, pos_y/pixel_size, units_per_px, spr);
    x += (spr->SWidth * units_per_px + 8) / 16;
    // Button ending sprite
    spr = get_button_sprite(GBS_frontend_button_std_r);
    LbSpriteDrawResized(x/pixel_size, pos_y/pixel_size, units_per_px, spr);
}

void draw_lit_bar64k(int64_t pos_x, int64_t pos_y, int64_t units_per_px, int64_t width)
{
    if (width < 32*units_per_px/16)
    {
        ERRORLOG("Bar is too small");
        return;
    }
    // opening sprite
    int64_t x = pos_x;
    const struct TbSprite* spr = get_button_sprite(GBS_frontend_button_sta_l);
    LbSpriteDrawResized(x, pos_y, units_per_px, spr);
    x += (spr->SWidth * units_per_px + 8) / 16;
    // body
    int64_t body_end = pos_x + width - 2 * ((32 * units_per_px + 8) / 16);
    while (x < body_end)
    {
        spr = get_button_sprite(GBS_frontend_button_sta_c);
        LbSpriteDrawResized(x, pos_y, units_per_px, spr);
        x += (spr->SWidth * units_per_px + 8) / 16;
    }
    x = body_end;
    spr = get_button_sprite(GBS_frontend_button_sta_c);
    LbSpriteDrawResized(x, pos_y, units_per_px, spr);
    x += (spr->SWidth * units_per_px + 8) / 16;
    // ending sprite
    spr = get_button_sprite(GBS_frontend_button_sta_r);
    LbSpriteDrawResized(x, pos_y, units_per_px, spr);
}

void draw_slab64k_background(int64_t pos_x, int64_t pos_y, int64_t width, int64_t height)
{
    RendererDrawSlabBackground(pos_x, pos_y, width, height);
}

void draw_slab64k_background_immediate(int64_t pos_x, int64_t pos_y, int64_t width, int64_t height)
{
    int64_t i;
    int64_t scr_x = pos_x / pixel_size;
    int64_t scr_y = pos_y / pixel_size;
    int64_t scr_h = height / pixel_size;
    int64_t scr_w = width / pixel_size;
    if (scr_x < 0)
    {
        i = scr_x + width / pixel_size;
        scr_x = 0;
        scr_w = i;
    }
    if (scr_y < 0)
    {
        i = scr_y + scr_h;
        scr_y = 0;
        scr_h = i;
    }
    i = lbDisplay.PhysicalScreenWidth * pixel_size;
    if (scr_x + scr_w > i)
        scr_w = i - scr_x;
    i = MyScreenHeight;
    if (scr_y + scr_h > i)
        scr_h = i - scr_y;
    TbPixel* out = &RendererGetFramebuffer()[scr_x + lbDisplay.GraphicsScreenWidth * scr_y];
    const unsigned char *pal = RendererGetActivePalette();
    TbPixel tile[GUI_SLAB_DIMENSION];
    for (i=0; scr_h > i; i++)
    {
        const unsigned char* inp = &gui_slab[GUI_SLAB_DIMENSION * (i % GUI_SLAB_DIMENSION)];
        for (int64_t t = 0; t < GUI_SLAB_DIMENSION; t++)
            tile[t] = resolve_indexed_pixel(inp[t], pal);
        if (scr_w >= GUI_SLAB_DIMENSION)
        {
            memcpy(out, tile, GUI_SLAB_DIMENSION * sizeof(TbPixel));
            int64_t k;
            for (k = GUI_SLAB_DIMENSION; k < scr_w - GUI_SLAB_DIMENSION; k += GUI_SLAB_DIMENSION)
            {
                memcpy(out + k, tile, GUI_SLAB_DIMENSION * sizeof(TbPixel));
            }
            if (width - k > 0) {
                memcpy(out + k, tile, (scr_w - k) * sizeof(TbPixel));
            }
        } else
        {
            memcpy(out, tile, scr_w * sizeof(TbPixel));
        }
        out += lbDisplay.GraphicsScreenWidth;
    }
}

void draw_slab64k(int64_t pos_x, int64_t pos_y, int64_t units_per_px, int64_t width, int64_t height)
{
    // Draw one pixel more, to make sure we won't get empty area after scaling
    draw_slab64k_background(pos_x, pos_y, width+scale_value_for_resolution_with_upp(1,units_per_px), height+scale_value_for_resolution_with_upp(1,units_per_px));
    const struct TbSprite* spr = get_button_sprite(GBS_borders_frame_thck_tl);
    int64_t bs_units_per_spr = calculate_relative_upp(16, units_per_px, spr->SWidth);
    int64_t border_shift = scale_value_for_resolution_with_upp(6,units_per_px);
    int64_t i;
    int64_t i_increment = units_per_px;
    for (i = i_increment - border_shift; i < width-2*border_shift; i += i_increment)
    {
        spr = get_button_sprite(GBS_borders_frame_thck_tc);
        LbSpriteDrawResized(pos_x + i, pos_y - border_shift, bs_units_per_spr, spr);
        spr = get_button_sprite(GBS_borders_frame_thck_bc);
        LbSpriteDrawResized(pos_x + i, pos_y + height, bs_units_per_spr, spr);
    }
    for (i = i_increment - border_shift; i < height-2*border_shift; i += i_increment)
    {
        spr = get_button_sprite(GBS_borders_frame_thck_ml);
        LbSpriteDrawResized(pos_x - border_shift, pos_y + i, bs_units_per_spr, spr);
        spr = get_button_sprite(GBS_borders_frame_thck_mr);
        LbSpriteDrawResized(pos_x + width, pos_y + i, bs_units_per_spr, spr);
    }
    spr = get_button_sprite(GBS_borders_frame_thck_tl);
    LbSpriteDrawResized(pos_x - border_shift, pos_y - border_shift, bs_units_per_spr, spr);
    spr = get_button_sprite(GBS_borders_frame_thck_tr);
    LbSpriteDrawResized(pos_x + width - 2*border_shift, pos_y - border_shift, bs_units_per_spr, spr);
    spr = get_button_sprite(GBS_borders_frame_thck_bl);
    LbSpriteDrawResized(pos_x - border_shift, pos_y + height - 2*border_shift, bs_units_per_spr, spr);
    spr = get_button_sprite(GBS_borders_frame_thck_br);
    LbSpriteDrawResized(pos_x + width - 2*border_shift, pos_y + height - 2*border_shift, bs_units_per_spr, spr);
}

void draw_ornate_slab64k(int64_t pos_x, int64_t pos_y, int64_t units_per_px, int64_t width, int64_t height)
{
    draw_slab64k_background(pos_x, pos_y, width, height);
    const struct TbSprite* spr = get_button_sprite(GBS_parchment_map_frame_deco_a_tl);
    int64_t bs_units_per_spr = scale_ui_value(2048/spr->SWidth);
    int64_t i;
    for (i= scale_ui_value(10); i < width- scale_ui_value(12); i+= scale_ui_value(32))
    {
        spr = get_button_sprite(GBS_borders_frame_thin_tc);
        LbSpriteDrawResized(pos_x + i, pos_y - scale_ui_value(4), bs_units_per_spr, spr);
        spr = get_button_sprite(GBS_borders_frame_thin_bc);
        LbSpriteDrawResized(pos_x + i, pos_y + height, bs_units_per_spr, spr);
    }
    for (i= scale_ui_value(10); i < height- scale_ui_value(16); i+= scale_ui_value(32))
    {
        spr = get_button_sprite(GBS_borders_frame_thin_ml);
        LbSpriteDrawResized(pos_x - scale_ui_value(4), pos_y + i, bs_units_per_spr, spr);
        spr = get_button_sprite(GBS_borders_frame_thin_mr);
        LbSpriteDrawResized(pos_x + width, pos_y + i, bs_units_per_spr, spr);
    }
    spr = get_button_sprite(GBS_borders_frame_thin_tl);
    LbSpriteDrawResized(pos_x - scale_ui_value(4), pos_y - scale_ui_value(4), bs_units_per_spr, spr);
    spr = get_button_sprite(GBS_borders_frame_thin_tr);
    LbSpriteDrawResized(pos_x + width - scale_ui_value(28), pos_y - scale_ui_value(4), bs_units_per_spr, spr);
    spr = get_button_sprite(GBS_borders_frame_thin_bl);
    LbSpriteDrawResized(pos_x - scale_ui_value(4), pos_y + height - scale_ui_value(28), bs_units_per_spr, spr);
    spr = get_button_sprite(GBS_borders_frame_thin_br);
    LbSpriteDrawResized(pos_x + width - scale_ui_value(28), pos_y + height - scale_ui_value(28), bs_units_per_spr, spr);
    spr = get_button_sprite(GBS_parchment_map_frame_deco_a_tl);
    LbSpriteDrawResized(pos_x - scale_ui_value(32), pos_y - scale_ui_value(14), bs_units_per_spr, spr);
    spr = get_button_sprite(GBS_parchment_map_frame_deco_a_bl);
    LbSpriteDrawResized(pos_x - scale_ui_value(34), pos_y + height - scale_ui_value(78), bs_units_per_spr, spr);
    RendererAddDrawFlags(Lb_SPRITE_FLIP_HORIZ);
    spr = get_button_sprite(GBS_parchment_map_frame_deco_a_tl);
    LbSpriteDrawResized(pos_x + width - scale_ui_value(96), pos_y - scale_ui_value(14), bs_units_per_spr, spr);
    spr = get_button_sprite(GBS_parchment_map_frame_deco_a_bl);
    LbSpriteDrawResized(pos_x + width - scale_ui_value(92), pos_y + height - scale_ui_value(78), bs_units_per_spr, spr);
    RendererClearDrawFlags(Lb_SPRITE_FLIP_HORIZ);
}

void draw_ornate_slab_outline64k(int64_t pos_x, int64_t pos_y, int64_t units_per_px, int64_t width, int64_t height)
{
    const struct TbSprite* spr = get_button_sprite(GBS_parchment_map_frame_deco_a_tl);
    int64_t bs_units_per_spr = scale_ui_value_lofi(2048)/spr->SWidth;
    int64_t x = pos_x;
    int64_t y = pos_y;
    int64_t i;
    for (i = scale_ui_value_lofi(10); i < width - scale_ui_value_lofi(12); i += scale_ui_value_lofi(32))
    {
        spr = get_button_sprite(GBS_borders_frame_thin_tc);
        LbSpriteDrawResized(pos_x + i, pos_y - scale_ui_value_lofi(4), bs_units_per_spr, spr);
        spr = get_button_sprite(GBS_borders_frame_thin_bc);
        LbSpriteDrawResized(pos_x + i, pos_y + height, bs_units_per_spr, spr);
    }
    for (i= scale_ui_value_lofi(10); i < height - scale_ui_value_lofi(16); i+= scale_ui_value_lofi(32))
    {
        spr = get_button_sprite(GBS_borders_frame_thin_ml);
        LbSpriteDrawResized(x - scale_ui_value_lofi(4), y + i, bs_units_per_spr, spr);
        spr = get_button_sprite(GBS_borders_frame_thin_mr);
        LbSpriteDrawResized(x + width, y + i, bs_units_per_spr, spr);
    }
    spr = get_button_sprite(GBS_borders_frame_thin_tl);
    LbSpriteDrawResized(x - scale_ui_value_lofi(4),          y - scale_ui_value_lofi(4),           bs_units_per_spr, spr);
    spr = get_button_sprite(GBS_borders_frame_thin_tr);
    LbSpriteDrawResized(x + width - scale_ui_value_lofi(28), y - scale_ui_value_lofi(4),           bs_units_per_spr, spr);
    spr = get_button_sprite(GBS_borders_frame_thin_bl);
    LbSpriteDrawResized(x - scale_ui_value_lofi(4),          y + height - scale_ui_value_lofi(28), bs_units_per_spr, spr);
    spr = get_button_sprite(GBS_borders_frame_thin_br);
    LbSpriteDrawResized(x + width - scale_ui_value_lofi(28), y + height - scale_ui_value_lofi(28), bs_units_per_spr, spr);
    spr = get_button_sprite(GBS_parchment_map_frame_deco_a_tl);
    LbSpriteDrawResized(x - scale_ui_value_lofi(32), y - scale_ui_value_lofi(14),          bs_units_per_spr, spr);
    spr = get_button_sprite(GBS_parchment_map_frame_deco_a_bl);
    LbSpriteDrawResized(x - scale_ui_value_lofi(34), y + height - scale_ui_value_lofi(78), bs_units_per_spr, spr);
    RendererAddDrawFlags(Lb_SPRITE_FLIP_HORIZ);
    spr = get_button_sprite(GBS_parchment_map_frame_deco_a_tl);
    LbSpriteDrawResized(x + width - scale_ui_value_lofi(96), y - scale_ui_value_lofi(14),          bs_units_per_spr, spr);
    spr = get_button_sprite(GBS_parchment_map_frame_deco_a_bl);
    LbSpriteDrawResized(x + width - scale_ui_value_lofi(92), y + height - scale_ui_value_lofi(78), bs_units_per_spr, spr);
    RendererClearDrawFlags(Lb_SPRITE_FLIP_HORIZ);
}

void draw_round_slab64k(int64_t pos_x, int64_t pos_y, int64_t units_per_px, int64_t width, int64_t height, int64_t style_type)
{
    int64_t drwflags_mem = RendererGetDrawFlags();
    RendererClearDrawFlags(Lb_SPRITE_OUTLINE);
    int32_t fill_inset = scale_ui_value_lofi(4);
    /* Keep the fill out of the rounded outer edge, but cover the transparent
       interior of the larger corner and button sprites. */
    int32_t corner_width = scale_ui_value_lofi(12);
    int32_t corner_height = scale_ui_value_lofi(12);
    RendererClearDrawFlags(Lb_SPRITE_OUTLINE);
    if (style_type == ROUNDSLAB64K_LIGHT) {
        RendererAddDrawFlags(Lb_SPRITE_TRANSPAR4);
        LbDrawBox(pos_x + scale_ui_value_lofi(4), pos_y + scale_ui_value_lofi(4), width - scale_ui_value_lofi(8), height - scale_ui_value_lofi(8), resolve_indexed_pixel(1, RendererGetActivePalette()));
        LbDrawBox(pos_x + fill_inset, pos_y + fill_inset, width - 2 * fill_inset, height - 2 * fill_inset, resolve_indexed_pixel(1, RendererGetActivePalette()));
        LbDrawBox(pos_x + width - fill_inset, pos_y + corner_height, fill_inset, height - 2 * corner_height, resolve_indexed_pixel(1, RendererGetActivePalette()));
        LbDrawBox(pos_x + corner_width, pos_y + height - fill_inset, width - 2 * corner_width, fill_inset, resolve_indexed_pixel(1, RendererGetActivePalette()));
        RendererClearDrawFlags(Lb_SPRITE_TRANSPAR4);
    } else {
        RendererAddDrawFlags(Lb_SPRITE_TRANSPAR8);
        LbDrawBox(pos_x + scale_ui_value_lofi(4), pos_y + scale_ui_value_lofi(4), width - scale_ui_value_lofi(8), height - scale_ui_value_lofi(8), resolve_indexed_pixel(1, RendererGetActivePalette()));
        LbDrawBox(pos_x + fill_inset, pos_y + fill_inset, width - 2 * fill_inset, height - 2 * fill_inset, resolve_indexed_pixel(1, RendererGetActivePalette()));
        LbDrawBox(pos_x + width - fill_inset, pos_y + corner_height, fill_inset, height - 2 * corner_height, resolve_indexed_pixel(1, RendererGetActivePalette()));
        LbDrawBox(pos_x + corner_width, pos_y + height - fill_inset, width - 2 * corner_width, fill_inset, resolve_indexed_pixel(1, RendererGetActivePalette()));
        RendererClearDrawFlags(Lb_SPRITE_TRANSPAR8);
    }
    int64_t x;
    int64_t y;
    const struct TbSprite* spr = get_panel_sprite(GPS_message_frame_thin_hex_ct);
    int64_t ps_units_per_spr = scale_ui_value_lofi(416)/spr->SWidth;
    int64_t i;
    for (i = 0; i < width - scale_ui_value_lofi(68); i += scale_ui_value_lofi(26))
    {
        x = pos_x + i + scale_ui_value_lofi(34);
        y = pos_y;
        spr = get_panel_sprite(GPS_message_frame_thin_hex_ct);
        LbSpriteDrawResized(x, y, ps_units_per_spr, spr);
        y += height - scale_ui_value_lofi(4);
        spr = get_panel_sprite(GPS_message_frame_thin_hex_cb);
        LbSpriteDrawResized(x, y, ps_units_per_spr, spr);
    }
    for (i = 0; i < height - scale_ui_value_lofi(56); i += scale_ui_value_lofi(20))
    {
        x = pos_x;
        y = pos_y + i + scale_ui_value_lofi(28);
        spr = get_panel_sprite(GPS_message_frame_thin_hex_cr);
        LbSpriteDrawResized(x, y, ps_units_per_spr, spr);
        x += width - scale_ui_value_lofi(4);
        spr = get_panel_sprite(GPS_message_frame_thin_hex_cl);
        LbSpriteDrawResized(x, y, ps_units_per_spr, spr);
    }
    x = pos_x + width - scale_ui_value_lofi(34);
    y = pos_y + height - scale_ui_value_lofi(28);
    spr = get_panel_sprite(GPS_message_frame_thin_hex_tl);
    LbSpriteDrawResized(pos_x, pos_y, ps_units_per_spr, spr);
    spr = get_panel_sprite(GPS_message_frame_thin_hex_tr);
    LbSpriteDrawResized(x,     pos_y, ps_units_per_spr, spr);
    spr = get_panel_sprite(GPS_message_frame_thin_hex_bl);
    LbSpriteDrawResized(pos_x, y,     ps_units_per_spr, spr);
    spr = get_panel_sprite(GPS_message_frame_thin_hex_br);
    LbSpriteDrawResized(x,     y,     ps_units_per_spr, spr);
    RendererSetDrawFlags(drwflags_mem);
}

/**
 * Returns units-per-pixel to be used for drawing given GUI button, assuming it consists of one panel sprite.
 * Uses sprite height as constant factor.
 * @param gbtn
 * @param spridx
 * @return
 */
int64_t simple_gui_panel_sprite_height_units_per_px(const struct GuiButton *gbtn, int64_t spridx, int64_t fraction)
{
    const struct TbSprite* spr = get_panel_sprite(spridx);
    if (spr->SHeight < 1)
        return 16;
    int64_t units_per_px = ((gbtn->height * fraction / 100) * 16 + spr->SHeight / 2) / spr->SHeight;
    if (units_per_px < 1)
        units_per_px = 1;
    return units_per_px;
}

/**
 * Returns units-per-pixel to be used for drawing given GUI button, assuming it consists of one panel sprite.
 * Uses sprite width as constant factor.
 * @param gbtn
 * @param spridx
 * @return
 */
int64_t simple_gui_panel_sprite_width_units_per_px(const struct GuiButton *gbtn, int64_t spridx, int64_t fraction)
{
    const struct TbSprite* spr = get_panel_sprite(spridx);
    if (spr->SWidth < 1)
        return 16;
    int64_t units_per_px = ((gbtn->width * fraction / 100) * 16 + spr->SWidth / 2) / spr->SWidth;
    if (units_per_px < 1)
        units_per_px = 1;
    return units_per_px;
}

/**
 * Returns units-per-pixel to be used for drawing given GUI button, assuming it consists of one button sprite.
 * Uses sprite height as constant factor.
 * @param gbtn
 * @param spridx
 * @return
 */
int64_t simple_button_sprite_height_units_per_px(const struct GuiButton *gbtn, int64_t spridx, int64_t fraction)
{
    const struct TbSprite* spr = get_button_sprite_for_player(spridx, my_player_number);
    if (spr->SHeight < 1)
        return 16;
    int64_t units_per_px = ((gbtn->height * fraction / 100) * 16 + spr->SHeight / 2) / spr->SHeight;
    if (units_per_px < 1)
        units_per_px = 1;
    return units_per_px;
}

/**
 * Returns units-per-pixel to be used for drawing given GUI button, assuming it consists of one button sprite.
 * Uses sprite width as constant factor.
 * @param gbtn
 * @param spridx
 * @return
 */
int64_t simple_button_sprite_width_units_per_px(const struct GuiButton *gbtn, int64_t spridx, int64_t fraction)
{
    const struct TbSprite* spr = get_button_sprite_for_player(spridx, my_player_number);
    if (spr->SWidth < 1)
        return 16;
    int64_t units_per_px = ((gbtn->width * fraction / 100) * 16 + spr->SWidth / 2) / spr->SWidth;
    if (units_per_px < 1)
        units_per_px = 1;
    return units_per_px;
}

/**
 * Returns units-per-pixel to be used for drawing given GUI button, assuming it consists of one sprite.
 * Uses sprite height as constant factor.
 * @param gbtn
 * @param spridx
 * @return
 */
int64_t simple_frontend_sprite_height_units_per_px(const struct GuiButton *gbtn, int64_t spridx, int64_t fraction)
{
    const struct TbSprite* spr = get_frontend_sprite(spridx);
    if (spr->SHeight < 1)
        return 16;
    int64_t units_per_px = ((gbtn->height * fraction / 100) * 16 + spr->SHeight / 2) / spr->SHeight;
    if (units_per_px < 1)
        units_per_px = 1;
    return units_per_px;
}

/**
 * Returns units-per-pixel to be used for drawing given GUI button, assuming it consists of one sprite.
 * Uses sprite width as constant factor.
 * @param gbtn
 * @param spridx
 * @return
 */
int64_t simple_frontend_sprite_width_units_per_px(const struct GuiButton *gbtn, int64_t spridx, int64_t fraction)
{
    const struct TbSprite* spr = get_frontend_sprite(spridx);
    if (spr->SWidth < 1)
        return 16;
    int64_t units_per_px = ((gbtn->width * fraction / 100) * 16 + spr->SWidth / 2) / spr->SWidth;
    if (units_per_px < 1)
        units_per_px = 1;
    return units_per_px;
}

/** Draws a string on GUI button.
 *  Note that the source text buffer may be damaged by this function.
 * @param gbtn Button to draw text on.
 * @param base_width Width of the button before scaling.
 * @param text Text to be displayed.
 */
void draw_button_string(struct GuiButton *gbtn, int64_t base_width, const char *text)
{
    uint64_t flgmem = RendererGetDrawFlags();
    int64_t cursor_pos = -1;
    static char dtext[TEXT_BUFFER_LENGTH];
    snprintf(dtext, TEXT_BUFFER_LENGTH, "%s", text);
    if ((gbtn->gbtype == LbBtnT_EditBox) && (gbtn == input_button))
    {
        // Time-based blink; original DK toggled every 2 frames at 20 fps, so 100ms on/off
        if ((LbTimerClock() / 100 & 1) == 0)
          cursor_pos = input_field_pos;
        LbLocTextStringConcat(dtext, " ", TEXT_BUFFER_LENGTH);
        RendererSetDrawColour(LbTextGetFontFaceColor());
        lbDisplayEx.ShadowColour = LbTextGetFontBackColor();
    }
    TbBool low_res = ( (MyScreenHeight < 400) && (dbc_initialized && dbc_enabled) );
    int64_t width = gbtn->width;
    int64_t x = gbtn->scr_pos_x;
    if (low_res)
    {
        // TODO: Is there a better way of adjusting for East Asian text? This is ridiculous.
        width += 32;
        switch (gbtn->tooltip_stridx)
        {
            case GUIStr_PickCreatrIdleDesc:
            case GUIStr_PickCreatrWorkingDesc:
            case GUIStr_PickCreatrFightingDesc:
            {
                x -= gbtn->width;
                break;
            }
            case GUIStr_MnuCancel:
            {
                x -= 12;
                break;
            }
            case GUIStr_ExperienceDesc:
            {
                x -= 16;
                break;
            }
            default:
            {
                x -= 8;
                break;
            }
        }
    }
    LbTextSetJustifyWindow(x, gbtn->scr_pos_y, width);
    LbTextSetClipWindow(x, gbtn->scr_pos_y, width, gbtn->height);
    RendererSetDrawFlags(Lb_TEXT_HALIGN_CENTER | Lb_TEXT_UNDERLNSHADOW);
    if (cursor_pos >= 0) {
        // Mind the order, 'cause inserting makes positions shift
        LbLocTextStringInsert(dtext, "\x0B", cursor_pos+1, TEXT_BUFFER_LENGTH);
        LbLocTextStringInsert(dtext, "\x0B", cursor_pos, TEXT_BUFFER_LENGTH);
    }
    int64_t units_per_px = (gbtn->width * 16 + base_width / 2) / base_width;
    int64_t tx_units_per_px = (units_per_px * 22 / LbTextLineHeight());
    uint64_t w = 4 * units_per_px / 16;
    if (low_res)
    {
        if ( (gbtn->tooltip_stridx != GUIStr_PickCreatrIdleDesc) && (gbtn->tooltip_stridx != GUIStr_PickCreatrWorkingDesc) && (gbtn->tooltip_stridx != GUIStr_PickCreatrFightingDesc) )
        {
            tx_units_per_px += (units_per_px / 2);
        }
    }
    uint64_t h = (gbtn->height - text_string_height(tx_units_per_px, dtext)) / 2 - 3 * units_per_px / 16;
    if (dbc_initialized && dbc_enabled)
    {
        if (gbtn->id_num == BID_QUERY_INFO)
        {
            if (MyScreenWidth > 640)
            {
                h += (13 + (MyScreenWidth / 640));
                w += 8;
                tx_units_per_px = scale_value_by_horizontal_resolution(10);
            }
        }
        else if (gbtn->id_num == BID_DUNGEON_INFO)
        {
            if (MyScreenWidth > 640)
            {
                h += (12 + (MyScreenWidth / 640));
                w += 8;
                tx_units_per_px = scale_value_by_horizontal_resolution(12);
            }
        }
        else if (gbtn->tooltip_stridx == GUIStr_ExperienceDesc)
        {
            if (MyScreenWidth > 640)
            {
                h += (8 + (MyScreenWidth / 640));
            }
        }
    }
    LbTextDrawResized(w, h, tx_units_per_px, dtext);
    LbTextSetJustifyWindow(0, 0, LbGraphicsScreenWidth());
    LbTextSetClipWindow(0/pixel_size, 0/pixel_size, MyScreenWidth/pixel_size, MyScreenHeight/pixel_size);
    LbTextSetWindow(0/pixel_size, 0/pixel_size, MyScreenWidth/pixel_size, MyScreenHeight/pixel_size);
    RendererSetDrawFlags(flgmem);
}

void draw_message_box_at(int64_t startx, int64_t starty, int64_t box_width, int64_t box_height, int64_t spritesx, int64_t spritesy)
{
    const struct TbSprite *spr;
    int64_t n;

    // Draw top line of sprites
    int64_t x = startx;
    int64_t y = starty;
    {
        spr = get_frontend_sprite(GFS_hugearea_thn_cor_tl);
        LbSpriteDrawResized(x, y, units_per_pixel, spr);
        x += spr->SWidth * units_per_pixel / 16;
    }
    for (n=0; n < spritesx; n++)
    {
        spr = get_frontend_sprite((n % 4) + GFS_hugearea_thn_tx1_tc);
        LbSpriteDrawResized(x, y, units_per_pixel, spr);
        x += spr->SWidth * units_per_pixel / 16;
    }
    x = startx;
    {
        spr = get_frontend_sprite(GFS_hugearea_thn_cor_tl);
        x += spr->SWidth * units_per_pixel / 16;
    }
    for (n=0; n < spritesx; n++)
    {
        spr = get_frontend_sprite((n % 4) + GFS_hugearea_thn_tx1_tc);
        LbSpriteDrawResized(x, y, units_per_pixel, spr);
        x += spr->SWidth * units_per_pixel / 16;
    }
    {
        spr = get_frontend_sprite(GFS_hugearea_thn_cor_tr);
        LbSpriteDrawResized(x, y, units_per_pixel, spr);
    }
    // Draw centered line of sprites
    spr = get_frontend_sprite(GFS_hugearea_thn_cor_tl);
    x = startx;
    y += spr->SHeight * units_per_pixel / 16;
    {
        spr = get_frontend_sprite(GFS_hugearea_thc_cor_ml);
        LbSpriteDrawResized(x, y, units_per_pixel, spr);
        x += spr->SWidth * units_per_pixel / 16;
    }
    for (n=0; n < spritesx; n++)
    {
        spr = get_frontend_sprite((n % 4) + GFS_hugearea_thc_tx1_mc);
        LbSpriteDrawResized(x, y, units_per_pixel, spr);
        x += spr->SWidth * units_per_pixel / 16;
    }
    {
        spr = get_frontend_sprite(GFS_hugearea_thc_cor_mr);
        LbSpriteDrawResized(x, y, units_per_pixel, spr);
    }
    // Draw bottom line of sprites
    spr = get_frontend_sprite(GFS_hugearea_thc_cor_ml);
    x = startx;
    y += spr->SHeight * units_per_pixel / 16;
    {
        spr = get_frontend_sprite(GFS_hugearea_thn_cor_bl);
        LbSpriteDrawResized(x, y, units_per_pixel, spr);
        x += spr->SWidth * units_per_pixel / 16;
    }
    for (n=0; n < spritesx; n++)
    {
        spr = get_frontend_sprite((n % 4) + GFS_hugearea_thn_tx1_bc);
        LbSpriteDrawResized(x, y, units_per_pixel, spr);
        x += spr->SWidth * units_per_pixel / 16;
    }
    {
        spr = get_frontend_sprite(GFS_hugearea_thn_cor_br);
        LbSpriteDrawResized(x, y, units_per_pixel, spr);
    }
}

TbBool draw_text_box(const char *text)
{
    int64_t spritesy;
    int64_t spritesx;
    LbTextSetFont(frontend_font[1]);
    int64_t n = LbTextStringWidth(text);
    if (n < (4*108)) {
        spritesy = 1;
        spritesx = n / 108;
    } else {
        spritesx = 4;
        spritesy = n / (3*108);
    }
    if (spritesy > 4) {
      ERRORLOG("Text too long for error box");
    }
    if (spritesx < 2) {
        spritesx = 2;
    } else
    if (spritesx > 4) {
        spritesx = 4;
    }
    int64_t box_width = (108 * spritesx + 18) * units_per_pixel / 16;
    int64_t box_height = 92 * units_per_pixel / 16;
    int64_t startx = (lbDisplay.PhysicalScreenWidth - box_width) / 2;
    int64_t starty = (lbDisplay.PhysicalScreenHeight - box_height) / 2;
    draw_message_box_at(startx, starty, box_width, box_height, spritesx, spritesy);
    // Draw the text inside box
    RendererSetDrawFlags(Lb_TEXT_HALIGN_CENTER);
    int64_t tx_units_per_px = ((box_height / 4) * 13 / 11) * 16 / LbTextLineHeight();
    LbTextSetWindow(startx, starty, box_width, box_height);
    n = LbTextLineHeight() * tx_units_per_px / 16;
    int64_t line_count = 1;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') {
            line_count++;
        }
    }
    return LbTextDrawResized(0, (box_height - line_count * n) / 2, tx_units_per_px, text);
}

TbBool draw_text_box_top(const char* text, uint64_t drawflags)
{
    int64_t spritesy;
    int64_t spritesx;
    LbTextSetFont(frontend_font[1]);
    int64_t n = LbTextStringWidth(text);
    if (n < (4 * 108)) {
        spritesy = 1;
        spritesx = n / 108;
    }
    else {
        spritesx = 4;
        spritesy = n / (3 * 108);
    }
    if (spritesy > 4) {
        ERRORLOG("Text too long for error box");
    }
    if (spritesx < 2) {
        spritesx = 2;
    }
    else
        if (spritesx > 4) {
            spritesx = 4;
        }
    int64_t box_width = (108 * spritesx + 18) * units_per_pixel / 16;
    int64_t box_height = 92 * units_per_pixel / 16;
    int64_t startx = (lbDisplay.PhysicalScreenWidth - box_width) / 2;
    int64_t starty = (lbDisplay.PhysicalScreenHeight - box_height) / 2;
    draw_message_box_at(startx, starty, box_width, box_height, spritesx, spritesy);
    // Draw the text inside box
    RendererSetDrawFlags(drawflags);
    int64_t tx_units_per_px = ((box_height / 4) * 13 / 11) * 16 / LbTextLineHeight();
    LbTextSetWindow(startx, starty, box_width, box_height);
    n = LbTextLineHeight() * tx_units_per_px / 16;
    return LbTextDrawResized(tx_units_per_px/2, 0, tx_units_per_px, text);
}

int64_t scroll_box_get_units_per_px(struct GuiButton *gbtn)
{
    int64_t width = 0;
    int64_t spridx = GFS_hugearea_thc_cor_ml;
    const struct TbSprite* spr = get_frontend_sprite(spridx);
    for (int64_t i = 6; i > 0; i--)
    {
        width += spr->SWidth;
        spr++;
    }
    return (gbtn->width * 16 + 8) / width;
}

void draw_scroll_box(struct GuiButton *gbtn, int64_t units_per_px, int64_t num_rows)
{
    const struct TbSprite *spr;
    int64_t pos_x;
    int64_t i;
    RendererSetDrawFlags(0);
    int64_t pos_y = gbtn->scr_pos_y;
    { // First row
        pos_x = gbtn->scr_pos_x;
        spr = get_frontend_sprite(GFS_hugearea_thn_cor_tl);
        for (i = 6; i > 0; i--)
        {
            LbSpriteDrawResized(pos_x, pos_y, units_per_px, spr);
            pos_x += spr->SWidth * units_per_px / 16;
            spr++;
        }
        spr = get_frontend_sprite(GFS_hugearea_thn_cor_tl);
        pos_y += spr->SHeight * units_per_px / 16;
    }
    // Further rows
    while (num_rows > 0)
    {
        int64_t spridx = GFS_hugearea_thc_cor_ml;
        if (num_rows < 3)
          spridx = GFS_hugearea_thn_cor_ml;
        spr = get_frontend_sprite(spridx);
        pos_x = gbtn->scr_pos_x;
        for (i = 6; i > 0; i--)
        {
            LbSpriteDrawResized(pos_x, pos_y, units_per_px, spr);
            pos_x += spr->SWidth * units_per_px / 16;
            spr++;
        }
        spr = get_frontend_sprite(spridx);
        pos_y += spr->SHeight * units_per_px / 16;
        int64_t delta = 3;
        if (num_rows < 3)
            delta = 1;
        num_rows -= delta;
    }
    // Last row
    spr = get_frontend_sprite(GFS_hugearea_thn_cor_bl);
    pos_x = gbtn->scr_pos_x;
    for (i = 6; i > 0; i--)
    {
        LbSpriteDrawResized(pos_x, pos_y, units_per_px, spr);
        pos_x += spr->SWidth * units_per_px / 16;
        spr++;
    }
}

void draw_gui_panel_sprite_left_player(int64_t x, int64_t y, int64_t units_per_px, int64_t spridx, PlayerNumber plyr_idx)
{
    spridx = get_player_colored_icon_idx(spridx,plyr_idx);
    const struct TbSprite* spr = get_panel_sprite(spridx);
    LbSpriteDrawResized(x, y, units_per_px, spr);
}

void draw_gui_panel_sprite_rmleft_player(int64_t x, int64_t y, int64_t units_per_px, int64_t spridx, uint64_t remap, PlayerNumber plyr_idx)
{
    spridx = get_player_colored_icon_idx(spridx, plyr_idx);
    const struct TbSprite* spr = get_panel_sprite(spridx);
    SetupSpriteRemapShade((int64_t)remap);
    LbSpriteDrawResizedRemap(x, y, units_per_px, spr, lbSpriteRemapTable);
}

void draw_gui_panel_sprite_centered(int64_t x, int64_t y, int64_t units_per_px, int64_t spridx)
{
    spridx = get_player_colored_icon_idx(spridx,my_player_number);
    const struct TbSprite* spr = get_panel_sprite(spridx);
    x -= ((spr->SWidth*units_per_px/16) >> 1);
    y -= ((spr->SHeight*units_per_px/16) >> 1);
    LbSpriteDrawResized(x, y, units_per_px, spr);
}

void draw_gui_panel_sprite_occentered(int64_t x, int64_t y, int64_t units_per_px, int64_t spridx, TbPixel color)
{
    spridx = get_player_colored_icon_idx(spridx,my_player_number);
    const struct TbSprite* spr = get_panel_sprite(spridx);
    x -= ((spr->SWidth*units_per_px/16) >> 1);
    y -= ((spr->SHeight*units_per_px/16) >> 1);
    LbSpriteDrawResizedOneColour(x, y, units_per_px, spr, color);
}

void draw_button_sprite_left(int64_t x, int64_t y, int64_t units_per_px, int64_t spridx)
{
    const struct TbSprite* spr = get_button_sprite_for_player(spridx, my_player_number);
    LbSpriteDrawResized(x, y, units_per_px, spr);
}

void draw_button_sprite_rmleft(int64_t x, int64_t y, int64_t units_per_px, int64_t spridx, uint64_t remap)
{
    const struct TbSprite* spr = get_button_sprite_for_player(spridx, my_player_number);
    SetupSpriteRemapShade((int64_t)remap);
    LbSpriteDrawResizedRemap(x, y, units_per_px, spr, lbSpriteRemapTable);
}

void draw_frontend_sprite_left(int64_t x, int64_t y, int64_t units_per_px, int64_t spridx)
{
    const struct TbSprite* spr = get_frontend_sprite(spridx);
    LbSpriteDrawResized(x, y, units_per_px, spr);
}

void draw_string64k(int64_t x, int64_t y, int64_t units_per_px, const char * text)
{
    int64_t drwflags_mem = RendererGetDrawFlags();
    RendererClearDrawFlags(Lb_TEXT_ONE_COLOR);
    LbTextDrawResized(x, y, units_per_px, text);
    RendererSetDrawFlags(drwflags_mem);
}

TbBool frontmenu_copy_background_at(const struct TbRect *bkgnd_area, int64_t units_per_px)
{
    int64_t img_width = 640;
    int64_t img_height = 480;
    const unsigned char *srcbuf = frontend_background;
    // Do the drawing
    copy_raw8_image_buffer(RendererGetFramebuffer(),LbGraphicsScreenWidth(),LbGraphicsScreenHeight(),
        img_width*units_per_px/16,img_height*units_per_px/16,bkgnd_area->left,bkgnd_area->top,srcbuf,img_width,img_height);
    // Burning candle flames
    return true;
}

int64_t get_frontmenu_background_area_rect(int64_t rect_x, int64_t rect_y, int64_t rect_w, int64_t rect_h, struct TbRect *bkgnd_area)
{
    int64_t img_width = 640;
    int64_t img_height = 480;
    // Parchment bitmap scaling
    int64_t units_per_px = max(16 * rect_w / img_width, 16 * rect_h / img_height);
    int64_t units_per_px_max = min(16 * 7 * rect_w / (6 * img_width), 16 * 4 * rect_h / (3 * img_height));
    if (units_per_px > units_per_px_max)
        units_per_px = units_per_px_max;
    // The image width can't be larger than video resolution
    if (units_per_px < 1) {
        units_per_px = 1;
    }
    // Set rectangle coords
    bkgnd_area->left = rect_x + (rect_w-units_per_px*img_width/16)/2;
    bkgnd_area->top = rect_y + (rect_h-units_per_px*img_height/16)/2;
    if (bkgnd_area->top < 0) bkgnd_area->top = 0;
    bkgnd_area->right = bkgnd_area->left + units_per_px*img_width/16;
    bkgnd_area->bottom = bkgnd_area->top + units_per_px*img_height/16;
    if (bkgnd_area->bottom > rect_y+rect_h) bkgnd_area->bottom = rect_y+rect_h;
    return units_per_px;
}

/**
 * Draws menu background.
 */
void draw_frontmenu_background(int64_t rect_x,int64_t rect_y,int64_t rect_w,int64_t rect_h)
{
    // Validate parameters with video mode
    TbScreenModeInfo *mdinfo = LbScreenGetModeInfo(LbScreenActiveMode());
    if (rect_w == POS_AUTO)
      rect_w = mdinfo->Width-rect_x;
    if (rect_h == POS_AUTO)
      rect_h = mdinfo->Height-rect_y;
    if (rect_w<0) rect_w=0;
    if (rect_h<0) rect_h=0;
    // Get background area rectangle
    struct TbRect bkgnd_area;
    int64_t units_per_px = get_frontmenu_background_area_rect(rect_x, rect_y, rect_w, rect_h, &bkgnd_area);
    // Draw it
    frontmenu_copy_background_at(&bkgnd_area, units_per_px);
    SYNCDBG(9,"Done");
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
