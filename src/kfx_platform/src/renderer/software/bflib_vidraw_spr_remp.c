/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_vidraw_spr_remp.c
 *     Graphics canvas drawing library, scaled sprite drawing with remaped colors.
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
/**
 * Draws a scaled up sprite on given buffer, with transparency mapping and source colours remapped, from right to left.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param cmap The colour remap table to be used.
 * @param transmap The transparency mapping table to be used.
 * @return Gives 0 on success.
 */
/* Only ever called with a ghost blend (see LbSpriteDrawRemapUsingScalingData()
 * below -- both Trans1 and Trans2 here are ghost-only, unlike
 * bflib_vidraw_spr_norm.c's Trans1 which also serves an alpha caller). cmap
 * is already a resolved TbPixel per source byte (palette expansion + colour
 * remap folded into one lookup by the caller), so no expand_indexed_pixel()
 * here. */
TbResult LbSpriteDrawRemapUsingScalingUpDataTrans1RL(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf, const TbPixel *cmap)
{
    const struct SprPixelSource px = {.cmap = cmap};
    return spr_scaling_up(outbuf, scanline, outheight, xstep, ystep, src_buf->data, src_buf->height, SprPx_Remap, SprBl_Trans1, true, &px);
}

/**
 * Draws a scaled up sprite on given buffer, with transparency mapping and source colours remapped, from left to right.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param cmap The colour remap table to be used.
 * @param transmap The transparency mapping table to be used. Should have a size of 256x256 to avoid invalid memory reads.
 * @return Gives 0 on success.
 */
/* Ghost-only, same reasoning as LbSpriteDrawRemapUsingScalingUpDataTrans1RL(). */
TbResult LbSpriteDrawRemapUsingScalingUpDataTrans1LR(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf, const TbPixel *cmap)
{
    const struct SprPixelSource px = {.cmap = cmap};
    return spr_scaling_up(outbuf, scanline, outheight, xstep, ystep, src_buf->data, src_buf->height, SprPx_Remap, SprBl_Trans1, false, &px);
}

/**
 * Draws a scaled up sprite on given buffer, with reversed transparency mapping and source colours remapped, from right to left.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param cmap The colour remap table to be used.
 * @param transmap The transparency mapping table to be used.
 * @return Gives 0 on success.
 */
/* Ghost-only (reversed ghost_blend_2 weighting), same reasoning as
 * LbSpriteDrawRemapUsingScalingUpDataTrans1RL(). */
TbResult LbSpriteDrawRemapUsingScalingUpDataTrans2RL(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf, const TbPixel *cmap)
{
    const struct SprPixelSource px = {.cmap = cmap};
    return spr_scaling_up(outbuf, scanline, outheight, xstep, ystep, src_buf->data, src_buf->height, SprPx_Remap, SprBl_Trans2, true, &px);
}

/**
 * Draws a scaled up sprite on given buffer, with reversed transparency mapping and source colours remapped, from left to right.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param cmap The colour remap table to be used.
 * @param transmap The transparency mapping table to be used.
 * @return Gives 0 on success.
 */
/* Ghost-only, same reasoning as LbSpriteDrawRemapUsingScalingUpDataTrans2RL(). */
TbResult LbSpriteDrawRemapUsingScalingUpDataTrans2LR(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf, const TbPixel *cmap)
{
    const struct SprPixelSource px = {.cmap = cmap};
    return spr_scaling_up(outbuf, scanline, outheight, xstep, ystep, src_buf->data, src_buf->height, SprPx_Remap, SprBl_Trans2, false, &px);
}

/**
 * Draws a scaled up sprite on given buffer, with source colours remapped, from right to left.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param cmap The colour remap table to be used.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawRemapUsingScalingUpDataSolidRL(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf, const TbPixel *cmap)
{
    const struct SprPixelSource px = {.cmap = cmap};
    return spr_scaling_up(outbuf, scanline, outheight, xstep, ystep, src_buf->data, src_buf->height, SprPx_Remap, SprBl_Solid, true, &px);
}

/**
 * Draws a scaled up sprite on given buffer, with source colours remapped, from left to right.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param cmap The colour remap table to be used.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawRemapUsingScalingUpDataSolidLR(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf, const TbPixel *cmap)
{
    const struct SprPixelSource px = {.cmap = cmap};
    return spr_scaling_up(outbuf, scanline, outheight, xstep, ystep, src_buf->data, src_buf->height, SprPx_Remap, SprBl_Solid, false, &px);
}

/**
 * Draws a scaled down sprite on given buffer, with transparency mapping and source colours remapped, from right to left.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param cmap The colour remap table to be used.
 * @param transmap The transparency mapping table to be used.
 * @return Gives 0 on success.
 */
/* Ghost-only, same reasoning as LbSpriteDrawRemapUsingScalingUpDataTrans1RL(). */
TbResult LbSpriteDrawRemapUsingScalingDownDataTrans1RL(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf, const TbPixel *cmap)
{
    const struct SprPixelSource px = {.cmap = cmap};
    return spr_scaling_down(outbuf, scanline, xstep, ystep, src_buf->data, src_buf->height, SprPx_Remap, SprBl_Trans1, true, &px);
}

/**
 * Draws a scaled down sprite on given buffer, with transparency mapping and source colours remapped, from left to right.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param cmap The colour remap table to be used.
 * @param transmap The transparency mapping table to be used.
 * @return Gives 0 on success.
 */
/* Ghost-only, same reasoning as LbSpriteDrawRemapUsingScalingUpDataTrans1RL(). */
TbResult LbSpriteDrawRemapUsingScalingDownDataTrans1LR(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf, const TbPixel *cmap)
{
    const struct SprPixelSource px = {.cmap = cmap};
    return spr_scaling_down(outbuf, scanline, xstep, ystep, src_buf->data, src_buf->height, SprPx_Remap, SprBl_Trans1, false, &px);
}

/**
 * Draws a scaled down sprite on given buffer, with reverse transparency mapping and source colours remapped, from right to left.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param cmap The colour remap table to be used.
 * @param transmap The transparency mapping table to be used.
 * @return Gives 0 on success.
 */
/* Ghost-only (reversed ghost_blend_2 weighting), same reasoning as
 * LbSpriteDrawRemapUsingScalingUpDataTrans1RL().
 * Legacy bug fixed here, per docs/refactor/renderer/02b-legacy-bugs-found.md
 * #3: the original bit-packing (`pxmap = cmap[*sprdata] << 8;` immediately
 * overwritten by `pxmap = (pxmap & ~0xff00) | (dest << 8);`) discarded the
 * sprite's remapped colour entirely -- every sibling variant (Up Trans2RL/
 * LR, Down Trans2LR) omits that initial `<< 8`, this one alone had it. */
TbResult LbSpriteDrawRemapUsingScalingDownDataTrans2RL(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf, const TbPixel *cmap)
{
    const struct SprPixelSource px = {.cmap = cmap};
    return spr_scaling_down(outbuf, scanline, xstep, ystep, src_buf->data, src_buf->height, SprPx_Remap, SprBl_Trans2, true, &px);
}

/**
 * Draws a scaled down sprite on given buffer, with reverse transparency mapping and source colours remapped, from left to right.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param cmap The colour remap table to be used.
 * @param transmap The transparency mapping table to be used.
 * @return Gives 0 on success.
 */
/* Ghost-only, same reasoning as LbSpriteDrawRemapUsingScalingUpDataTrans2RL(). */
TbResult LbSpriteDrawRemapUsingScalingDownDataTrans2LR(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf, const TbPixel *cmap)
{
    const struct SprPixelSource px = {.cmap = cmap};
    return spr_scaling_down(outbuf, scanline, xstep, ystep, src_buf->data, src_buf->height, SprPx_Remap, SprBl_Trans2, false, &px);
}

/**
 * Draws a scaled down sprite on given buffer, with source colours remapped, from right to left.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param cmap The colour remap table to be used.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawRemapUsingScalingDownDataSolidRL(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf, const TbPixel *cmap)
{
    const struct SprPixelSource px = {.cmap = cmap};
    return spr_scaling_down(outbuf, scanline, xstep, ystep, src_buf->data, src_buf->height, SprPx_Remap, SprBl_Solid, true, &px);
}

/**
 * Draws a scaled down sprite on given buffer, with source colours remapped, from left to right.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param cmap The colour remap table to be used.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawRemapUsingScalingDownDataSolidLR(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf, const TbPixel *cmap)
{
    // Only this variant checks its arguments (kept as it was, pass 3 S08).
    if (!outbuf || !xstep || !ystep || !src_buf || !src_buf->data || !cmap)
        return -1;
    const struct SprPixelSource px = {.cmap = cmap};
    return spr_scaling_down(outbuf, scanline, xstep, ystep, src_buf->data, src_buf->height, SprPx_Remap, SprBl_Solid, false, &px);
}

/**
 * Draws a scaled sprite with remapped colours on current graphics window at given position.
 * Requires LbSpriteSetScalingData() to be called before.
 *
 * @param posx The X coord within current graphics window.
 * @param posy The Y coord within current graphics window.
 * @param sprite The source sprite.
 * @param cmap Colour mapping array.
 * @return Gives 0 on success.
 * @see LbSpriteSetScalingData()
 */
TbResult LbSpriteDrawRemapUsingScalingData(int64_t posx, int64_t posy, const struct TbSourceBuffer * src_buf, const TbPixel *cmap)
{
    SYNCDBG(17,"Drawing at (%" PRId64 ",%" PRId64 ")",(int64_t)(posx),(int64_t)(posy));
    {
        // gpu-v2 Phase C.2: record for the GPU world frame instead, when one is being built.
        const int64_t draw_flags = RendererGetDrawFlags();
        uint32_t mode = WFS_SOLID;
        if ((draw_flags & Lb_SPRITE_TRANSPAR4) != 0) mode = WFS_GHOST1;
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
        if ((RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR4) != 0)
        {
          if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
          {
              return LbSpriteDrawRemapUsingScalingUpDataTrans1RL(outbuf, scanline, outheight, xstep, ystep, src_buf, cmap);
          }
          else
          {
              return LbSpriteDrawRemapUsingScalingUpDataTrans1LR(outbuf, scanline, outheight, xstep, ystep, src_buf, cmap);
          }
        }
        else
        if ((RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR8) != 0)
        {
          if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
          {
              return LbSpriteDrawRemapUsingScalingUpDataTrans2RL(outbuf, scanline, outheight, xstep, ystep, src_buf, cmap);
          }
          else
          {
              return LbSpriteDrawRemapUsingScalingUpDataTrans2LR(outbuf, scanline, outheight, xstep, ystep, src_buf, cmap);
          }
        }
        else
        {
          if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
          {
              return LbSpriteDrawRemapUsingScalingUpDataSolidRL(outbuf, scanline, outheight, xstep, ystep, src_buf, cmap);
          }
          else
          {
              return LbSpriteDrawRemapUsingScalingUpDataSolidLR(outbuf, scanline, outheight, xstep, ystep, src_buf, cmap);
          }
        }
    }
    else
    {
        if ((RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR4) != 0)
        {
          if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
          {
              return LbSpriteDrawRemapUsingScalingDownDataTrans1RL(outbuf, scanline, outheight, xstep, ystep, src_buf, cmap);
          }
          else
          {
              return LbSpriteDrawRemapUsingScalingDownDataTrans1LR(outbuf, scanline, outheight, xstep, ystep, src_buf, cmap);
          }
        }
        else
        if ((RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR8) != 0)
        {
          if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
          {
              return LbSpriteDrawRemapUsingScalingDownDataTrans2RL(outbuf, scanline, outheight, xstep, ystep, src_buf, cmap);
          }
          else
          {
              return LbSpriteDrawRemapUsingScalingDownDataTrans2LR(outbuf, scanline, outheight, xstep, ystep, src_buf, cmap);
          }
        }
        else
        {
          if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
          {
              return LbSpriteDrawRemapUsingScalingDownDataSolidRL(outbuf, scanline, outheight, xstep, ystep, src_buf, cmap);
          }
          else
          {
              return LbSpriteDrawRemapUsingScalingDownDataSolidLR(outbuf, scanline, outheight, xstep, ystep, src_buf, cmap);
          }
        }
    }
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
