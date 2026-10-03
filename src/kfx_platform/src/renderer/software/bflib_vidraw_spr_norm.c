/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_vidraw_spr_norm.c
 *     Graphics canvas drawing library, normal scaled sprite drawing.
 * @par Purpose:
 *    Screen drawing routines; draws rescaled sprite.
 * @par Comment:
 *     Medium level library, draws on screen buffer used in bflib_video.
 *     Used for drawing screen components.
 * @author   Tomasz Lis
 * @date     12 Feb 2008 - 01 Aug 2014
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "renderer/WorldFrame.h"
#include "bflib_vidraw.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "globals.h"

#include "bflib_video.h"
#include "bflib_sprite.h"
#include "bflib_mouse.h"
#include "bflib_render.h"
#include "bflib_vidraw_spr_scale.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// The functions below are from colour remap version of the routine - rhey're used for shadows
TbResult LbSpriteDrawRemapUsingScalingUpDataSolidRL(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf, const TbPixel *cmap);
TbResult LbSpriteDrawRemapUsingScalingUpDataSolidLR(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf, const TbPixel *cmap);
TbResult LbSpriteDrawRemapUsingScalingDownDataSolidRL(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf, const TbPixel *cmap);
TbResult LbSpriteDrawRemapUsingScalingDownDataSolidLR(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf, const TbPixel *cmap);
/******************************************************************************/
/**
 * Draws a scaled up sprite on given buffer, with transparency mapping, from right to left.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param transmap The transparency mapping table to be used.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawUsingScalingUpDataTrans1RL(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf, TbBool use_alpha_blend)
{
    const struct SprPixelSource px = {.palette = RendererGetActivePalette()};
    if (use_alpha_blend)
        return spr_scaling_up(outbuf, scanline, outheight, xstep, ystep, src_buf->data, src_buf->height, SprPx_Copy, SprBl_Alpha, true, &px);
    return spr_scaling_up(outbuf, scanline, outheight, xstep, ystep, src_buf->data, src_buf->height, SprPx_Copy, SprBl_Trans1, true, &px);
}

/**
 * Draws a scaled up sprite on given buffer, with transparency mapping, from left to right.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param transmap The transparency mapping table to be used. Should have a size of 256x256 to avoid invalid memory reads.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawUsingScalingUpDataTrans1LR(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf, TbBool use_alpha_blend)
{
    const struct SprPixelSource px = {.palette = RendererGetActivePalette()};
    if (use_alpha_blend)
        return spr_scaling_up(outbuf, scanline, outheight, xstep, ystep, src_buf->data, src_buf->height, SprPx_Copy, SprBl_Alpha, false, &px);
    return spr_scaling_up(outbuf, scanline, outheight, xstep, ystep, src_buf->data, src_buf->height, SprPx_Copy, SprBl_Trans1, false, &px);
}

/**
 * Draws a scaled up sprite on given buffer, with reversed transparency mapping, from right to left.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param transmap The transparency mapping table to be used.
 * @return Gives 0 on success.
 */
/* Trans2 is only ever called with a ghost blend (never alpha -- see
 * DrawAlphaSpriteUsingScalingData(), which only uses the Trans1 family), so
 * unlike Trans1 above this needs no runtime mode switch. */
TbResult LbSpriteDrawUsingScalingUpDataTrans2RL(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf)
{
    const struct SprPixelSource px = {.palette = RendererGetActivePalette()};
    return spr_scaling_up(outbuf, scanline, outheight, xstep, ystep, src_buf->data, src_buf->height, SprPx_Copy, SprBl_Trans2, true, &px);
}

/**
 * Draws a scaled up sprite on given buffer, with reversed transparency mapping, from left to right.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param transmap The transparency mapping table to be used.
 * @return Gives 0 on success.
 */
/* Ghost-only, same reasoning as LbSpriteDrawUsingScalingUpDataTrans2RL(). */
TbResult LbSpriteDrawUsingScalingUpDataTrans2LR(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf)
{
    const struct SprPixelSource px = {.palette = RendererGetActivePalette()};
    return spr_scaling_up(outbuf, scanline, outheight, xstep, ystep, src_buf->data, src_buf->height, SprPx_Copy, SprBl_Trans2, false, &px);
}

/**
 * Draws a scaled up sprite on given buffer, with original colours, from right to left.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawUsingScalingUpDataSolidRL(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf)
{
    const struct SprPixelSource px = {.palette = RendererGetActivePalette()};
    return spr_scaling_up(outbuf, scanline, outheight, xstep, ystep, src_buf->data, src_buf->height, SprPx_Copy, SprBl_Solid, true, &px);
}

/**
 * Draws a scaled up sprite on given buffer, with original colours, from left to right.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param outheight
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawUsingScalingUpDataSolidLR(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf)
{
    const struct SprPixelSource px = {.palette = RendererGetActivePalette()};
    return spr_scaling_up(outbuf, scanline, outheight, xstep, ystep, src_buf->data, src_buf->height, SprPx_Copy, SprBl_Solid, false, &px);
}

/**
 * Draws a scaled down sprite on given buffer, with transparency mapping, from right to left.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param transmap The transparency mapping table to be used.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawUsingScalingDownDataTrans1RL(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf, TbBool use_alpha_blend)
{
    const struct SprPixelSource px = {.palette = RendererGetActivePalette()};
    if (use_alpha_blend)
        return spr_scaling_down(outbuf, scanline, xstep, ystep, src_buf->data, src_buf->height, SprPx_Copy, SprBl_Alpha, true, &px);
    return spr_scaling_down(outbuf, scanline, xstep, ystep, src_buf->data, src_buf->height, SprPx_Copy, SprBl_Trans1, true, &px);
}

/**
 * Draws a scaled down sprite on given buffer, with transparency mapping, from left to right.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param transmap The transparency mapping table to be used.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawUsingScalingDownDataTrans1LR(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf, TbBool use_alpha_blend)
{
    const struct SprPixelSource px = {.palette = RendererGetActivePalette()};
    if (use_alpha_blend)
        return spr_scaling_down(outbuf, scanline, xstep, ystep, src_buf->data, src_buf->height, SprPx_Copy, SprBl_Alpha, false, &px);
    return spr_scaling_down(outbuf, scanline, xstep, ystep, src_buf->data, src_buf->height, SprPx_Copy, SprBl_Trans1, false, &px);
}

/**
 * Draws a scaled down sprite on given buffer, with reverse transparency mapping, from right to left.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param transmap The transparency mapping table to be used.
 * @return Gives 0 on success.
 */
/* Ghost-only, same reasoning as LbSpriteDrawUsingScalingUpDataTrans2RL(). */
TbResult LbSpriteDrawUsingScalingDownDataTrans2RL(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf)
{
    const struct SprPixelSource px = {.palette = RendererGetActivePalette()};
    return spr_scaling_down(outbuf, scanline, xstep, ystep, src_buf->data, src_buf->height, SprPx_Copy, SprBl_Trans2, true, &px);
}

/**
 * Draws a scaled down sprite on given buffer, with reverse transparency mapping, from left to right.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param transmap The transparency mapping table to be used.
 * @return Gives 0 on success.
 */
/* Ghost-only, same reasoning as LbSpriteDrawUsingScalingUpDataTrans2RL(). */
TbResult LbSpriteDrawUsingScalingDownDataTrans2LR(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf)
{
    const struct SprPixelSource px = {.palette = RendererGetActivePalette()};
    return spr_scaling_down(outbuf, scanline, xstep, ystep, src_buf->data, src_buf->height, SprPx_Copy, SprBl_Trans2, false, &px);
}

/**
 * Draws a scaled down sprite on given buffer, with original colours, from right to left.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawUsingScalingDownDataSolidRL(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf)
{
    const struct SprPixelSource px = {.palette = RendererGetActivePalette()};
    return spr_scaling_down(outbuf, scanline, xstep, ystep, src_buf->data, src_buf->height, SprPx_Copy, SprBl_Solid, true, &px);
}

/**
 * Draws a scaled down sprite on given buffer, with original colours, from left to right.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawUsingScalingDownDataSolidLR(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf)
{
    const struct SprPixelSource px = {.palette = RendererGetActivePalette()};
    return spr_scaling_down(outbuf, scanline, xstep, ystep, src_buf->data, src_buf->height, SprPx_Copy, SprBl_Solid, false, &px);
}

/**
 * Draws a scaled sprite on current graphics window at given position.
 * Requires LbSpriteSetScalingData() to be called before.
 *
 * @param posx The X coord within current graphics window.
 * @param posy The Y coord within current graphics window.
 * @param sprite The source sprite.
 * @return Gives 0 on success.
 * @see LbSpriteSetScalingData()
 */
TbResult LbSpriteDrawUsingScalingData(int64_t posx, int64_t posy, const struct TbSourceBuffer * src_buf)
{
    SYNCDBG(17,"Drawing at (%" PRId64 ",%" PRId64 ")",(int64_t)(posx),(int64_t)(posy));
    {
        // gpu-v2 Phase C.2: record for the GPU world frame instead, when one is being built.
        const int64_t draw_flags = RendererGetDrawFlags();
        uint32_t mode = WFS_SOLID;
        const TbPixel *cmap = NULL;
        if ((draw_flags & Lb_SPRITE_REMAP) != 0) cmap = lbSpriteReMapPtr;
        else if ((draw_flags & Lb_SPRITE_TRANSPAR4) != 0) mode = WFS_GHOST1;
        else if ((draw_flags & Lb_SPRITE_TRANSPAR8) != 0) mode = WFS_GHOST2;
        const TbPixel no_colour = { 0, 0, 0, 0 };
        if (SwCaptureSprite(posx, posy, src_buf->data, src_buf->width, src_buf->height, mode, cmap, no_colour))
            return 0;
    }
    int64_t *xstep;
    int64_t *ystep;
    int64_t scanline;
    TbPixel *outbuf;
    int64_t outheight;
    setup_steps(posx, posy, src_buf, &xstep, &ystep, &scanline);
    setup_outbuf(xstep, ystep, &outbuf, &outheight);

    if ( scale_up )
    {
        if ((RendererGetDrawFlags() & Lb_SPRITE_REMAP) != 0)
        {
          if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
          {
              return LbSpriteDrawRemapUsingScalingUpDataSolidRL(outbuf, scanline, outheight, xstep, ystep, src_buf, lbSpriteReMapPtr);
          }
          else
          {
              return LbSpriteDrawRemapUsingScalingUpDataSolidLR(outbuf, scanline, outheight, xstep, ystep, src_buf, lbSpriteReMapPtr);
          }
        }
        else
        if ((RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR4) != 0)
        {
          if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
          {
              return LbSpriteDrawUsingScalingUpDataTrans1RL(outbuf, scanline, outheight, xstep, ystep, src_buf, false);
          }
          else
          {
              return LbSpriteDrawUsingScalingUpDataTrans1LR(outbuf, scanline, outheight, xstep, ystep, src_buf, false);
          }
        }
        else
        if ((RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR8) != 0)
        {
          if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
          {
              return LbSpriteDrawUsingScalingUpDataTrans2RL(outbuf, scanline, outheight, xstep, ystep, src_buf);
          }
          else
          {
              return LbSpriteDrawUsingScalingUpDataTrans2LR(outbuf, scanline, outheight, xstep, ystep, src_buf);
          }
        }
        else
        {
          if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
          {
              return LbSpriteDrawUsingScalingUpDataSolidRL(outbuf, scanline, outheight, xstep, ystep, src_buf);
          }
          else
          {
              return LbSpriteDrawUsingScalingUpDataSolidLR(outbuf, scanline, outheight, xstep, ystep, src_buf);
          }
        }
    }
    else
    {
        if ((RendererGetDrawFlags() & Lb_SPRITE_REMAP) != 0)
        {
          if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
          {
              return LbSpriteDrawRemapUsingScalingDownDataSolidRL(outbuf, scanline, outheight, xstep, ystep, src_buf, lbSpriteReMapPtr);
          }
          else
          {
              return LbSpriteDrawRemapUsingScalingDownDataSolidLR(outbuf, scanline, outheight, xstep, ystep, src_buf, lbSpriteReMapPtr);
          }
        }
        else
        if ((RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR4) != 0)
        {
          if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
          {
              return LbSpriteDrawUsingScalingDownDataTrans1RL(outbuf, scanline, outheight, xstep, ystep, src_buf, false);
          }
          else
          {
              return LbSpriteDrawUsingScalingDownDataTrans1LR(outbuf, scanline, outheight, xstep, ystep, src_buf, false);
          }
        }
        else
        if ((RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR8) != 0)
        {
          if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
          {
              return LbSpriteDrawUsingScalingDownDataTrans2RL(outbuf, scanline, outheight, xstep, ystep, src_buf);
          }
          else
          {
              return LbSpriteDrawUsingScalingDownDataTrans2LR(outbuf, scanline, outheight, xstep, ystep, src_buf);
          }
        }
        else
        {
          if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
          {
              return LbSpriteDrawUsingScalingDownDataSolidRL(outbuf, scanline, outheight, xstep, ystep, src_buf);
          }
          else
          {
              return LbSpriteDrawUsingScalingDownDataSolidLR(outbuf, scanline, outheight, xstep, ystep, src_buf);
          }
        }
    }
}

/**
 * Draws an alpha-blended scaled sprite on current graphics window at given position.
 * Requires LbSpriteSetScalingData() to be called before.
 *
 * @param posx The X coord within current graphics window.
 * @param posy The Y coord within current graphics window.
 * @param sprite The source sprite.
 * @return Gives 0 on success.
 * @see LbSpriteSetScalingData()
 */
TbResult DrawAlphaSpriteUsingScalingData(int64_t posx, int64_t posy, const struct TbSourceBuffer * src_buf)
{
    SYNCDBG(17,"Drawing at (%" PRId64 ",%" PRId64 ")",(int64_t)(posx),(int64_t)(posy));
    {
        const TbPixel no_colour = { 0, 0, 0, 0 };
        if (SwCaptureSprite(posx, posy, src_buf->data, src_buf->width, src_buf->height, WFS_ALPHA, NULL, no_colour))
            return 0;
    }
    int64_t *xstep;
    int64_t *ystep;
    int64_t scanline;
    TbPixel *outbuf;
    int64_t outheight;
    setup_steps(posx, posy, src_buf, &xstep, &ystep, &scanline);
    setup_outbuf(xstep, ystep, &outbuf, &outheight);
    if ( scale_up )
    {
        if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
        {
            return LbSpriteDrawUsingScalingUpDataTrans1RL(outbuf, scanline, outheight, xstep, ystep, src_buf, true);
        }
        else
        {
            return LbSpriteDrawUsingScalingUpDataTrans1LR(outbuf, scanline, outheight, xstep, ystep, src_buf, true);
        }
    }
    else
    {
        if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
        {
            return LbSpriteDrawUsingScalingDownDataTrans1RL(outbuf, scanline, outheight, xstep, ystep, src_buf, true);
        }
        else
        {
            return LbSpriteDrawUsingScalingDownDataTrans1LR(outbuf, scanline, outheight, xstep, ystep, src_buf, true);
        }
    }
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
