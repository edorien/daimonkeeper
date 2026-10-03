/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_vidraw_spr_onec.c
 *     Graphics canvas drawing library, scaled sprite drawing with one color.
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
#include "renderer/software/SwDrawTarget.h"
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
#include "bflib_vidraw_spr_scale.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/**
 * Draws a scaled up sprite on given buffer, with transparency mapping and one colour, from right to left.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param colour The colour to be used for drawing.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawOneColourUsingScalingUpDataTrans1RL(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSprite *sprite, TbPixel colour)
{
    const struct SprPixelSource px = {.colour = colour};
    return spr_scaling_up(outbuf, scanline, outheight, xstep, ystep, sprite->Data, sprite->SHeight, SprPx_OneColour, SprBl_Trans1, true, &px);
}

/**
 * Draws a scaled up sprite on given buffer, with transparency mapping and one colour, from left to right.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param colour The colour to be used for drawing.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawOneColourUsingScalingUpDataTrans1LR(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSprite *sprite, TbPixel colour)
{
    const struct SprPixelSource px = {.colour = colour};
    return spr_scaling_up(outbuf, scanline, outheight, xstep, ystep, sprite->Data, sprite->SHeight, SprPx_OneColour, SprBl_Trans1, false, &px);
}

/**
 * Draws a scaled up sprite on given buffer, with reversed transparency mapping and one colour, from right to left.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param colour The colour to be used for drawing.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawOneColourUsingScalingUpDataTrans2RL(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSprite *sprite, TbPixel colour)
{
    const struct SprPixelSource px = {.colour = colour};
    return spr_scaling_up(outbuf, scanline, outheight, xstep, ystep, sprite->Data, sprite->SHeight, SprPx_OneColour, SprBl_Trans2, true, &px);
}

/**
 * Draws a scaled up sprite on given buffer, with reversed transparency mapping and one colour, from left to right.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param colour The colour to be used for drawing.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawOneColourUsingScalingUpDataTrans2LR(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSprite *sprite, TbPixel colour)
{
    const struct SprPixelSource px = {.colour = colour};
    return spr_scaling_up(outbuf, scanline, outheight, xstep, ystep, sprite->Data, sprite->SHeight, SprPx_OneColour, SprBl_Trans2, false, &px);
}

/**
 * Draws a scaled up sprite on given buffer, with one colour, from right to left.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param colour The colour to be used for drawing.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawOneColourUsingScalingUpDataSolidRL(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSprite *sprite, TbPixel colour)
{
    const struct SprPixelSource px = {.colour = colour};
    return spr_scaling_up(outbuf, scanline, outheight, xstep, ystep, sprite->Data, sprite->SHeight, SprPx_OneColour, SprBl_Solid, true, &px);
}

/**
 * Draws a scaled up sprite on given buffer, with one colour, from left to right.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param colour The colour to be used for drawing.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawOneColourUsingScalingUpDataSolidLR(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSprite *sprite, TbPixel colour)
{
    const struct SprPixelSource px = {.colour = colour};
    return spr_scaling_up(outbuf, scanline, outheight, xstep, ystep, sprite->Data, sprite->SHeight, SprPx_OneColour, SprBl_Solid, false, &px);
}

/**
 * Draws a scaled down sprite on given buffer, with transparency mapping and one colour, from right to left.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param colour The colour to be used for drawing.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawOneColourUsingScalingDownDataTrans1RL(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSprite *sprite, TbPixel colour)
{
    const struct SprPixelSource px = {.colour = colour};
    return spr_scaling_down(outbuf, scanline, xstep, ystep, sprite->Data, sprite->SHeight, SprPx_OneColour, SprBl_Trans1, true, &px);
}

/**
 * Draws a scaled down sprite on given buffer, with transparency mapping and one colour, from left to right.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param colour The colour to be used for drawing.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawOneColourUsingScalingDownDataTrans1LR(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSprite *sprite, TbPixel colour)
{
    const struct SprPixelSource px = {.colour = colour};
    return spr_scaling_down(outbuf, scanline, xstep, ystep, sprite->Data, sprite->SHeight, SprPx_OneColour, SprBl_Trans1, false, &px);
}

/**
 * Draws a scaled down sprite on given buffer, with reverse transparency mapping and one colour, from right to left.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param colour The colour to be used for drawing.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawOneColourUsingScalingDownDataTrans2RL(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSprite *sprite, TbPixel colour)
{
    const struct SprPixelSource px = {.colour = colour};
    return spr_scaling_down(outbuf, scanline, xstep, ystep, sprite->Data, sprite->SHeight, SprPx_OneColour, SprBl_Trans2, true, &px);
}

/**
 * Draws a scaled down sprite on given buffer, with reverse transparency mapping and one colour, from left to right.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param colour The colour to be used for drawing.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawOneColourUsingScalingDownDataTrans2LR(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSprite *sprite, TbPixel colour)
{
    const struct SprPixelSource px = {.colour = colour};
    return spr_scaling_down(outbuf, scanline, xstep, ystep, sprite->Data, sprite->SHeight, SprPx_OneColour, SprBl_Trans2, false, &px);
}

/**
 * Draws a scaled down sprite on given buffer, with one colour, from right to left.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param colour The colour to be used for drawing.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawOneColourUsingScalingDownDataSolidRL(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSprite *sprite, TbPixel colour)
{
    const struct SprPixelSource px = {.colour = colour};
    return spr_scaling_down(outbuf, scanline, xstep, ystep, sprite->Data, sprite->SHeight, SprPx_OneColour, SprBl_Solid, true, &px);
}

/**
 * Draws a scaled down sprite on given buffer, with one colour, from left to right.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @param colour The colour to be used for drawing.
 * @return Gives 0 on success.
 */
TbResult LbSpriteDrawOneColourUsingScalingDownDataSolidLR(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSprite *sprite, TbPixel colour)
{
    const struct SprPixelSource px = {.colour = colour};
    return spr_scaling_down(outbuf, scanline, xstep, ystep, sprite->Data, sprite->SHeight, SprPx_OneColour, SprBl_Solid, false, &px);
}

/**
 * Draws a scaled sprite with one colour on current graphics window at given position.
 * Requires LbSpriteSetScalingData() to be called before.
 *
 * @param posx The X coord within current graphics window.
 * @param posy The Y coord within current graphics window.
 * @param sprite The source sprite.
 * @param colour The colour to be used for drawing.
 * @return Gives 0 on success.
 * @see LbSpriteSetScalingData()
 */
TbResult LbSpriteDrawOneColourUsingScalingData(int64_t posx, int64_t posy, const struct TbSprite *sprite, TbPixel colour)
{
    SYNCDBG(17,"Drawing at (%" PRId64 ",%" PRId64 ")",(int64_t)(posx),(int64_t)(posy));
    if ((RendererGetDrawFlags() & (Lb_SPRITE_TRANSPAR4 | Lb_SPRITE_TRANSPAR8)) == 0)
    {
        // gpu-v2 Phase C.2: record for the GPU world frame instead, when one is being built.
        if (SwCaptureSprite(posx, posy, sprite->Data, sprite->SWidth, sprite->SHeight, WFS_ONECOLOUR, NULL, colour))
            return 0;
    }
    int64_t *xstep;
    int64_t *ystep;
    int64_t scanline;
    {
        int64_t sposx;
        int64_t sposy;
        sposx = posx;
        sposy = posy;
        scanline = SwTargetScanline();
        if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0) {
            sposx = sprite->SWidth + posx - 1;
        }
        if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_VERTIC) != 0) {
            sposy = sprite->SHeight + posy - 1;
            scanline = -SwTargetScanline();
        }
        xstep = &xsteps_array[2 * sposx];
        ystep = &ysteps_array[2 * sposy];
    }
    TbPixel *outbuf;
    int64_t outheight;
    {
        int64_t gspos_x;
        int64_t gspos_y;
        gspos_y = ystep[0];
        if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_VERTIC) != 0)
            gspos_y += ystep[1] - 1;
        gspos_x = xstep[0];
        if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
            gspos_x += xstep[1] - 1;
        outbuf = &SwTargetGraphicsWindowPtr()[gspos_x + SwTargetScanline() * gspos_y];
        outheight = SwTargetScreenHeight();
    }
    if ( scale_up )
    {
        if ((RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR4) != 0)
        {
          if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
          {
              return LbSpriteDrawOneColourUsingScalingUpDataTrans1RL(outbuf, scanline, outheight, xstep, ystep, sprite, colour);
          }
          else
          {
              return LbSpriteDrawOneColourUsingScalingUpDataTrans1LR(outbuf, scanline, outheight, xstep, ystep, sprite, colour);
          }
        }
        else
        if ((RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR8) != 0)
        {
          if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
          {
              return LbSpriteDrawOneColourUsingScalingUpDataTrans2RL(outbuf, scanline, outheight, xstep, ystep, sprite, colour);
          }
          else
          {
              return LbSpriteDrawOneColourUsingScalingUpDataTrans2LR(outbuf, scanline, outheight, xstep, ystep, sprite, colour);
          }
        }
        else
        {
          if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
          {
              return LbSpriteDrawOneColourUsingScalingUpDataSolidRL(outbuf, scanline, outheight, xstep, ystep, sprite, colour);
          }
          else
          {
              return LbSpriteDrawOneColourUsingScalingUpDataSolidLR(outbuf, scanline, outheight, xstep, ystep, sprite, colour);
          }
        }
    }
    else
    {
        if ((RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR4) != 0)
        {
          if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
          {
              return LbSpriteDrawOneColourUsingScalingDownDataTrans1RL(outbuf, scanline, outheight, xstep, ystep, sprite, colour);
          }
          else
          {
              return LbSpriteDrawOneColourUsingScalingDownDataTrans1LR(outbuf, scanline, outheight, xstep, ystep, sprite, colour);
          }
        }
        else
        if ((RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR8) != 0)
        {
          if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
          {
              return LbSpriteDrawOneColourUsingScalingDownDataTrans2RL(outbuf, scanline, outheight, xstep, ystep, sprite, colour);
          }
          else
          {
              return LbSpriteDrawOneColourUsingScalingDownDataTrans2LR(outbuf, scanline, outheight, xstep, ystep, sprite, colour);
          }
        }
        else
        {
          if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
          {
              return LbSpriteDrawOneColourUsingScalingDownDataSolidRL(outbuf, scanline, outheight, xstep, ystep, sprite, colour);
          }
          else
          {
              return LbSpriteDrawOneColourUsingScalingDownDataSolidLR(outbuf, scanline, outheight, xstep, ystep, sprite, colour);
          }
        }
    }
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
/******************************************************************************/
