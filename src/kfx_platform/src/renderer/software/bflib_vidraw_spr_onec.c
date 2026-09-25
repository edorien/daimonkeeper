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
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

/**
 * Ghost/invisible-creature blend: `output = (colour + 2*dest) / 3` -- a fixed
 * 1/3-weight tint of `colour` onto whatever's already at the destination.
 * Replaces the old `render_ghost[colour<<8 | dest]` table lookup every
 * "Trans1" variant in this file used to do -- see
 * docs/refactor/renderer/02a-pixel-format-design.md §2.2. `colour` is the
 * fixed silhouette colour; `dest` is the current framebuffer pixel being
 * drawn over.
 */
static inline TbPixel ghost_blend_1(TbPixel colour, TbPixel dest)
{
    return TbPixel_RGB(
        (uint8_t)((colour.r + 2 * dest.r) / 3),
        (uint8_t)((colour.g + 2 * dest.g) / 3),
        (uint8_t)((colour.b + 2 * dest.b) / 3));
}

/**
 * Ghost/invisible-creature blend, the "Trans2" (reverse) weighting used by
 * this file's Trans2 variants: `output = (2*colour + dest) / 3` -- mostly
 * the fixed silhouette colour, lightly blended toward the destination.
 * Replaces the old `render_ghost[dest<<8 | colour]` table lookup (note the
 * reversed index order vs. ghost_blend_1 -- deliberately a different blend,
 * confirmed by tracing the original `pxmap` computation, not the same
 * formula applied twice). See
 * docs/refactor/renderer/02a-pixel-format-design.md §2.2/§3.2.
 */
static inline TbPixel ghost_blend_2(TbPixel colour, TbPixel dest)
{
    return TbPixel_RGB(
        (uint8_t)((2 * colour.r + dest.r) / 3),
        (uint8_t)((2 * colour.g + dest.g) / 3),
        (uint8_t)((2 * colour.b + dest.b) / 3));
}

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
    SYNCDBG(17,"Drawing");
    int64_t ystep_delta;
    unsigned char *sprdata;
    int64_t *ycurstep;

    ystep_delta = 2;
    if (scanline < 0) {
        ystep_delta = -2;
    }
    sprdata = sprite->Data;
    ycurstep = ystep;

    int64_t h;
    for (h=sprite->SHeight; h > 0; h--)
    {
        if (ycurstep[1] != 0)
        {
            unsigned char *prevdata;
            int64_t xdup;
            int64_t ydup;
            int64_t *xcurstep;
            ydup = ycurstep[1];
            if (ycurstep[0]+ydup > outheight)
                ydup = outheight-ycurstep[0];
            prevdata = sprdata;
            while (ydup > 0)
            {
                sprdata = prevdata;
                xcurstep = xstep;
                TbPixel *out_end;
                out_end = outbuf;
                while ( 1 )
                {
                    int64_t pxlen;
                    pxlen = (signed char)*sprdata;
                    sprdata++;
                    if (pxlen == 0)
                        break;
                    if (pxlen < 0)
                    {
                        pxlen = -pxlen;
                        out_end -= xcurstep[0] + xcurstep[1];
                        xcurstep -= 2 * pxlen;
                        out_end += xcurstep[0] + xcurstep[1];
                    }
                    else
                    {
                        for (;pxlen > 0; pxlen--)
                        {
                            xdup = xcurstep[1];
                            if (xcurstep[0]+xdup > llabs(scanline))
                                xdup = llabs(scanline)-xcurstep[0];
                            if (xdup > 0)
                            {
                                for (;xdup > 0; xdup--)
                                {
                                    *out_end = ghost_blend_1(colour, *out_end);
                                    out_end--;
                                }
                            }
                            sprdata++;
                            xcurstep -= 2;
                        }
                    }
                }
                outbuf += scanline;
                ydup--;
            }
        }
        else
        {
            while ( 1 )
            {
                int64_t pxlen;
                pxlen = (signed char)*sprdata;
                sprdata++;
                if (pxlen == 0)
                  break;
                if (pxlen > 0)
                {
                    sprdata += pxlen;
                }
            }
        }
        ycurstep += ystep_delta;
    }
    return 0;
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
    SYNCDBG(17,"Drawing");
    int64_t ystep_delta;
    unsigned char *sprdata;
    int64_t *ycurstep;

    ystep_delta = 2;
    if (scanline < 0) {
        ystep_delta = -2;
    }
    sprdata = sprite->Data;
    ycurstep = ystep;

    int64_t h;
    for (h=sprite->SHeight; h > 0; h--)
    {
        if (ycurstep[1] != 0)
        {
            unsigned char *prevdata;
            int64_t xdup;
            int64_t ydup;
            int64_t *xcurstep;
            ydup = ycurstep[1];
            if (ycurstep[0]+ydup > outheight)
                ydup = outheight-ycurstep[0];
            prevdata = sprdata;
            while (ydup > 0)
            {
                sprdata = prevdata;
                xcurstep = xstep;
                TbPixel *out_end;
                out_end = outbuf;
                while ( 1 )
                {
                    int64_t pxlen;
                    pxlen = (signed char)*sprdata;
                    sprdata++;
                    if (pxlen == 0)
                        break;
                    if (pxlen < 0)
                    {
                        pxlen = -pxlen;
                        out_end -= xcurstep[0];
                        xcurstep += 2 * pxlen;
                        out_end += xcurstep[0];
                    }
                    else
                    {
                        for (;pxlen > 0; pxlen--)
                        {
                            xdup = xcurstep[1];
                            if (xcurstep[0]+xdup > llabs(scanline))
                                xdup = llabs(scanline)-xcurstep[0];
                            if (xdup > 0)
                            {
                                for (;xdup > 0; xdup--)
                                {
                                    *out_end = ghost_blend_1(colour, *out_end);
                                    out_end++;
                                }
                            }
                            sprdata++;
                            xcurstep += 2;
                        }
                    }
                }
                outbuf += scanline;
                ydup--;
            }
        }
        else
        {
            while ( 1 )
            {
                int64_t pxlen;
                pxlen = (signed char)*sprdata;
                sprdata++;
                if (pxlen == 0)
                  break;
                if (pxlen > 0)
                {
                    sprdata += pxlen;
                }
            }
        }
        ycurstep += ystep_delta;
    }
    return 0;
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
    SYNCDBG(17,"Drawing");
    int64_t ystep_delta;
    unsigned char *sprdata;
    int64_t *ycurstep;

    ystep_delta = 2;
    if (scanline < 0) {
        ystep_delta = -2;
    }
    sprdata = sprite->Data;
    ycurstep = ystep;

    int64_t h;
    for (h=sprite->SHeight; h > 0; h--)
    {
        if (ycurstep[1] != 0)
        {
            unsigned char *prevdata;
            int64_t xdup;
            int64_t ydup;
            int64_t *xcurstep;
            ydup = ycurstep[1];
            if (ycurstep[0]+ydup > outheight)
                ydup = outheight-ycurstep[0];
            prevdata = sprdata;
            while (ydup > 0)
            {
                sprdata = prevdata;
                xcurstep = xstep;
                TbPixel *out_end;
                out_end = outbuf;
                while ( 1 )
                {
                    int64_t pxlen;
                    pxlen = (signed char)*sprdata;
                    sprdata++;
                    if (pxlen == 0)
                        break;
                    if (pxlen < 0)
                    {
                        pxlen = -pxlen;
                        out_end -= xcurstep[0] + xcurstep[1];
                        xcurstep -= 2 * pxlen;
                        out_end += xcurstep[0] + xcurstep[1];
                    }
                    else
                    {
                        for (;pxlen > 0; pxlen--)
                        {
                            xdup = xcurstep[1];
                            if (xcurstep[0]+xdup > llabs(scanline))
                                xdup = llabs(scanline)-xcurstep[0];
                            if (xdup > 0)
                            {
                                for (;xdup > 0; xdup--)
                                {
                                    *out_end = ghost_blend_2(colour, *out_end);
                                    out_end--;
                                }
                            }
                            sprdata++;
                            xcurstep -= 2;
                        }
                    }
                }
                outbuf += scanline;
                ydup--;
            }
        }
        else
        {
            while ( 1 )
            {
                int64_t pxlen;
                pxlen = (signed char)*sprdata;
                sprdata++;
                if (pxlen == 0)
                  break;
                if (pxlen > 0)
                {
                    sprdata += pxlen;
                }
            }
        }
        ycurstep += ystep_delta;
    }
    return 0;
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
    SYNCDBG(17,"Drawing");
    int64_t ystep_delta;
    unsigned char *sprdata;
    int64_t *ycurstep;

    ystep_delta = 2;
    if (scanline < 0) {
        ystep_delta = -2;
    }
    sprdata = sprite->Data;
    ycurstep = ystep;

    int64_t h;
    for (h=sprite->SHeight; h > 0; h--)
    {
        if (ycurstep[1] != 0)
        {
            unsigned char *prevdata;
            int64_t xdup;
            int64_t ydup;
            int64_t *xcurstep;
            ydup = ycurstep[1];
            if (ycurstep[0]+ydup > outheight)
                ydup = outheight-ycurstep[0];
            prevdata = sprdata;
            while (ydup > 0)
            {
                sprdata = prevdata;
                xcurstep = xstep;
                TbPixel *out_end;
                out_end = outbuf;
                while ( 1 )
                {
                    int64_t pxlen;
                    pxlen = (signed char)*sprdata;
                    sprdata++;
                    if (pxlen == 0)
                        break;
                    if (pxlen < 0)
                    {
                        pxlen = -pxlen;
                        out_end -= xcurstep[0];
                        xcurstep += 2 * pxlen;
                        out_end += xcurstep[0];
                    }
                    else
                    {
                        for (;pxlen > 0; pxlen--)
                        {
                            xdup = xcurstep[1];
                            if (xcurstep[0]+xdup > llabs(scanline))
                                xdup = llabs(scanline)-xcurstep[0];
                            if (xdup > 0)
                            {
                                for (;xdup > 0; xdup--)
                                {
                                    *out_end = ghost_blend_2(colour, *out_end);
                                    out_end++;
                                }
                            }
                            sprdata++;
                            xcurstep += 2;
                        }
                    }
                }
                outbuf += scanline;
                ydup--;
            }
        }
        else
        {
            while ( 1 )
            {
                int64_t pxlen;
                pxlen = (signed char)*sprdata;
                sprdata++;
                if (pxlen == 0)
                  break;
                if (pxlen > 0)
                {
                    sprdata += pxlen;
                }
            }
        }
        ycurstep += ystep_delta;
    }
    return 0;
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
    SYNCDBG(17,"Drawing");
    int64_t ystep_delta;
    unsigned char *sprdata;
    int64_t *ycurstep;

    ystep_delta = 2;
    if (scanline < 0) {
        ystep_delta = -2;
    }
    sprdata = sprite->Data;
    ycurstep = ystep;

    int64_t h;
    for (h=sprite->SHeight; h > 0; h--)
    {
        if (ycurstep[1] != 0)
        {
            int64_t ycur;
            int64_t solid_len;
            TbPixel * out_line;
            int64_t xdup;
            int64_t ydup;
            int64_t *xcurstep;
            ydup = ycurstep[1];
            if (ycurstep[0]+ydup > outheight)
                ydup = outheight-ycurstep[0];
            xcurstep = xstep;
            TbPixel *out_end;
            out_end = outbuf;
            while ( 1 )
            {
                int64_t pxlen;
                pxlen = (signed char)*sprdata;
                sprdata++;
                if (pxlen == 0)
                    break;
                if (pxlen < 0)
                {
                    pxlen = -pxlen;
                    out_end -= xcurstep[0] + xcurstep[1];
                    xcurstep -= 2 * pxlen;
                    out_end += xcurstep[0] + xcurstep[1];
                }
                else
                {
                    TbPixel *out_start;
                    out_start = out_end;
                    for(;pxlen > 0; pxlen--)
                    {
                        xdup = xcurstep[1];
                        if (xcurstep[0]+xdup > llabs(scanline))
                            xdup = llabs(scanline)-xcurstep[0];
                        if (xdup > 0)
                        {
                            TbPixel pxval;
                            pxval = colour;
                            for (;xdup > 0; xdup--)
                            {
                                *out_end = pxval;
                                out_end--;
                            }
                        }
                        sprdata++;
                        xcurstep -= 2;
                    }
                    ycur = ydup - 1;
                    if (ycur > 0)
                    {
                        solid_len = out_start - out_end;
                        out_start = out_end;
                        solid_len++;
                        out_line = out_start + scanline;
                        for (;ycur > 0; ycur--)
                        {
                            if (solid_len > 0) {
                                LbPixelBlockCopyForward(out_line, out_start, solid_len);
                            }
                            out_line += scanline;
                        }
                    }
                }
            }
            outbuf += scanline;
            ycur = ydup - 1;
            for (;ycur > 0; ycur--)
            {
                outbuf += scanline;
            }
        }
        else
        {
            while ( 1 )
            {
                int64_t pxlen;
                pxlen = (signed char)*sprdata;
                sprdata++;
                if (pxlen == 0)
                  break;
                if (pxlen > 0)
                {
                    sprdata += pxlen;
                }
            }
        }
        ycurstep += ystep_delta;
    }
    return 0;
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
    SYNCDBG(17,"Drawing");
    int64_t ystep_delta;
    unsigned char *sprdata;
    int64_t *ycurstep;

    ystep_delta = 2;
    if (scanline < 0) {
        ystep_delta = -2;
    }
    sprdata = sprite->Data;
    ycurstep = ystep;

    int64_t h;
    for (h=sprite->SHeight; h > 0; h--)
    {
        if (ycurstep[1] != 0)
        {
            int64_t ycur;
            int64_t solid_len;
            TbPixel * out_line;
            int64_t xdup;
            int64_t ydup;
            int64_t *xcurstep;
            ydup = ycurstep[1];
            if (ycurstep[0]+ydup > outheight)
                ydup = outheight-ycurstep[0];
            xcurstep = xstep;
            TbPixel *out_end;
            out_end = outbuf;
            while ( 1 )
            {
                int64_t pxlen;
                pxlen = (signed char)*sprdata;
                sprdata++;
                if (pxlen == 0)
                    break;
                if (pxlen < 0)
                {
                    pxlen = -pxlen;
                    out_end -= xcurstep[0];
                    xcurstep += 2 * pxlen;
                    out_end += xcurstep[0];
                }
                else
                {
                    TbPixel *out_start;
                    out_start = out_end;
                    for(;pxlen > 0; pxlen--)
                    {
                        xdup = xcurstep[1];
                        if (xcurstep[0]+xdup > llabs(scanline))
                            xdup = llabs(scanline)-xcurstep[0];
                        if (xdup > 0)
                        {
                            TbPixel pxval;
                            pxval = colour;
                            for (;xdup > 0; xdup--)
                            {
                                *out_end = pxval;
                                out_end++;
                            }
                        }
                        sprdata++;
                        xcurstep += 2;
                    }
                    ycur = ydup - 1;
                    if (ycur > 0)
                    {
                        solid_len = out_end - out_start;
                        out_line = out_start + scanline;
                        for (;ycur > 0; ycur--)
                        {
                            if (solid_len > 0) {
                                LbPixelBlockCopyForward(out_line, out_start, solid_len);
                            }
                            out_line += scanline;
                        }
                    }
                }
            }
            outbuf += scanline;
            ycur = ydup - 1;
            for (;ycur > 0; ycur--)
            {
                outbuf += scanline;
            }
        }
        else
        {
            while ( 1 )
            {
                int64_t pxlen;
                pxlen = (signed char)*sprdata;
                sprdata++;
                if (pxlen == 0)
                  break;
                if (pxlen > 0)
                {
                    sprdata += pxlen;
                }
            }
        }
        ycurstep += ystep_delta;
    }
    return 0;
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
    SYNCDBG(17,"Drawing");
    int64_t ystep_delta;
    unsigned char *sprdata;
    int64_t *ycurstep;

    ystep_delta = 2;
    if (scanline < 0) {
        ystep_delta = -2;
    }
    sprdata = sprite->Data;
    ycurstep = ystep;

    int64_t h;
    for (h=sprite->SHeight; h > 0; h--)
    {
        if (ycurstep[1] != 0)
        {
            int64_t *xcurstep;
            xcurstep = xstep;
            TbPixel *out_end;
            out_end = outbuf;
            while ( 1 )
            {
                int64_t pxlen;
                pxlen = (signed char)*sprdata;
                sprdata++;
                if (pxlen == 0)
                    break;
                if (pxlen < 0)
                {
                    pxlen = -pxlen;
                    out_end -= xcurstep[0] + xcurstep[1];
                    xcurstep -= 2 * pxlen;
                    out_end += xcurstep[0] + xcurstep[1];
                }
                else
                {
                    for (;pxlen > 0; pxlen--)
                    {
                        if (xcurstep[1] > 0)
                        {
                            *out_end = ghost_blend_1(colour, *out_end);
                            out_end--;
                        }
                        sprdata++;
                        xcurstep -= 2;
                    }
                }
            }
            outbuf += scanline;
        }
        else
        {
            while ( 1 )
            {
                int64_t pxlen;
                pxlen = (signed char)*sprdata;
                sprdata++;
                if (pxlen == 0)
                  break;
                if (pxlen > 0)
                {
                    sprdata += pxlen;
                }
            }
        }
        ycurstep += ystep_delta;
    }
    return 0;
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
    SYNCDBG(17,"Drawing");
    int64_t ystep_delta;
    unsigned char *sprdata;
    int64_t *ycurstep;

    ystep_delta = 2;
    if (scanline < 0) {
        ystep_delta = -2;
    }
    sprdata = sprite->Data;
    ycurstep = ystep;

    int64_t h;
    for (h=sprite->SHeight; h > 0; h--)
    {
        if (ycurstep[1] != 0)
        {
            int64_t *xcurstep;
            xcurstep = xstep;
            TbPixel *out_end;
            out_end = outbuf;
            while ( 1 )
            {
                int64_t pxlen;
                pxlen = (signed char)*sprdata;
                sprdata++;
                if (pxlen == 0)
                    break;
                if (pxlen < 0)
                {
                    pxlen = -pxlen;
                    out_end -= xcurstep[0];
                    xcurstep += 2 * pxlen;
                    out_end += xcurstep[0];
                }
                else
                {
                    for (;pxlen > 0; pxlen--)
                    {
                        if (xcurstep[1] > 0)
                        {
                            *out_end = ghost_blend_1(colour, *out_end);
                            out_end++;
                        }
                        sprdata++;
                        xcurstep += 2;
                    }
                }
            }
            outbuf += scanline;
        }
        else
        {
            while ( 1 )
            {
                int64_t pxlen;
                pxlen = (signed char)*sprdata;
                sprdata++;
                if (pxlen == 0)
                  break;
                if (pxlen > 0)
                {
                    sprdata += pxlen;
                }
            }
        }
        ycurstep += ystep_delta;
    }
    return 0;
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
    SYNCDBG(17,"Drawing");
    int64_t ystep_delta;
    unsigned char *sprdata;
    int64_t *ycurstep;

    ystep_delta = 2;
    if (scanline < 0) {
        ystep_delta = -2;
    }
    sprdata = sprite->Data;
    ycurstep = ystep;

    int64_t h;
    for (h=sprite->SHeight; h > 0; h--)
    {
        if (ycurstep[1] != 0)
        {
            int64_t *xcurstep;
            xcurstep = xstep;
            TbPixel *out_end;
            out_end = outbuf;
            while ( 1 )
            {
                int64_t pxlen;
                pxlen = (signed char)*sprdata;
                sprdata++;
                if (pxlen == 0)
                    break;
                if (pxlen < 0)
                {
                    pxlen = -pxlen;
                    out_end -= xcurstep[0] + xcurstep[1];
                    xcurstep -= 2 * pxlen;
                    out_end += xcurstep[0] + xcurstep[1];
                }
                else
                {
                    for (;pxlen > 0; pxlen--)
                    {
                        if (xcurstep[1] > 0)
                        {
                            /* Legacy bug fixed here, per docs/refactor/renderer/
                             * 02b-legacy-bugs-found.md #1: the pre-migration code
                             * computed `pxmap = (colour << 8)` then immediately
                             * overwrote that byte with the destination pixel,
                             * silently discarding `colour` (always read
                             * ghost[dest<<8|0] instead of ghost[dest<<8|colour]).
                             * Its LR counterpart never had this bug. Fixed to
                             * match: use the real ghost_blend_2(colour, dest). */
                            *out_end = ghost_blend_2(colour, *out_end);
                            out_end--;
                        }
                        sprdata++;
                        xcurstep -= 2;
                    }
                }
            }
            outbuf += scanline;
        }
        else
        {
            while ( 1 )
            {
                int64_t pxlen;
                pxlen = (signed char)*sprdata;
                sprdata++;
                if (pxlen == 0)
                  break;
                if (pxlen > 0)
                {
                    sprdata += pxlen;
                }
            }
        }
        ycurstep += ystep_delta;
    }
    return 0;
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
    SYNCDBG(17,"Drawing");
    int64_t ystep_delta;
    unsigned char *sprdata;
    int64_t *ycurstep;

    ystep_delta = 2;
    if (scanline < 0) {
        ystep_delta = -2;
    }
    sprdata = sprite->Data;
    ycurstep = ystep;

    int64_t h;
    for (h=sprite->SHeight; h > 0; h--)
    {
        if (ycurstep[1] != 0)
        {
            int64_t *xcurstep;
            xcurstep = xstep;
            TbPixel *out_end;
            out_end = outbuf;
            while ( 1 )
            {
                int64_t pxlen;
                pxlen = (signed char)*sprdata;
                sprdata++;
                if (pxlen == 0)
                    break;
                if (pxlen < 0)
                {
                    pxlen = -pxlen;
                    out_end -= xcurstep[0];
                    xcurstep += 2 * pxlen;
                    out_end += xcurstep[0];
                }
                else
                {
                    for (;pxlen > 0; pxlen--)
                    {
                        if (xcurstep[1] > 0)
                        {
                            *out_end = ghost_blend_2(colour, *out_end);
                            out_end++;
                        }
                        sprdata++;
                        xcurstep += 2;
                    }
                }
            }
            outbuf += scanline;
        }
        else
        {
            while ( 1 )
            {
                int64_t pxlen;
                pxlen = (signed char)*sprdata;
                sprdata++;
                if (pxlen == 0)
                  break;
                if (pxlen > 0)
                {
                    sprdata += pxlen;
                }
            }
        }
        ycurstep += ystep_delta;
    }
    return 0;
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
    SYNCDBG(17,"Drawing");
    int64_t ystep_delta;
    unsigned char *sprdata;
    int64_t *ycurstep;

    ystep_delta = 2;
    if (scanline < 0) {
        ystep_delta = -2;
    }
    sprdata = sprite->Data;
    ycurstep = ystep;

    int64_t h;
    for (h=sprite->SHeight; h > 0; h--)
    {
        if (ycurstep[1] != 0)
        {
            int64_t *xcurstep;
            xcurstep = xstep;
            TbPixel *out_end;
            out_end = outbuf;
            while ( 1 )
            {
                int64_t pxlen;
                pxlen = (signed char)*sprdata;
                sprdata++;
                if (pxlen == 0)
                    break;
                if (pxlen < 0)
                {
                    pxlen = -pxlen;
                    out_end -= xcurstep[0] + xcurstep[1];
                    xcurstep -= 2 * pxlen;
                    out_end += xcurstep[0] + xcurstep[1];
                }
                else
                {
                    for (;pxlen > 0; pxlen--)
                    {
                        if (xcurstep[1] > 0)
                        {
                            *out_end = colour;
                            out_end--;
                        }
                        sprdata++;
                        xcurstep -= 2;
                    }
                }
            }
            outbuf += scanline;
        }
        else
        {
            while ( 1 )
            {
                int64_t pxlen;
                pxlen = (signed char)*sprdata;
                sprdata++;
                if (pxlen == 0)
                  break;
                if (pxlen > 0)
                {
                    sprdata += pxlen;
                }
            }
        }
        ycurstep += ystep_delta;
    }
    return 0;
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
    SYNCDBG(17,"Drawing");
    int64_t ystep_delta;
    unsigned char *sprdata;
    int64_t *ycurstep;

    ystep_delta = 2;
    if (scanline < 0) {
        ystep_delta = -2;
    }
    sprdata = sprite->Data;
    ycurstep = ystep;

    int64_t h;
    for (h=sprite->SHeight; h > 0; h--)
    {
        if (ycurstep[1] != 0)
        {
            int64_t *xcurstep;
            xcurstep = xstep;
            TbPixel *out_end;
            out_end = outbuf;
            while ( 1 )
            {
                int64_t pxlen;
                pxlen = (signed char)*sprdata;
                sprdata++;
                if (pxlen == 0)
                    break;
                if (pxlen < 0)
                {
                    pxlen = -pxlen;
                    out_end -= xcurstep[0];
                    xcurstep += 2 * pxlen;
                    out_end += xcurstep[0];
                }
                else
                {
                    for (;pxlen > 0; pxlen--)
                    {
                        if (xcurstep[1] > 0)
                        {
                            *out_end = colour;
                            out_end++;
                        }
                        sprdata++;
                        xcurstep += 2;
                    }
                }
            }
            outbuf += scanline;
        }
        else
        {
            while ( 1 )
            {
                int64_t pxlen;
                pxlen = (signed char)*sprdata;
                sprdata++;
                if (pxlen == 0)
                  break;
                if (pxlen > 0)
                {
                    sprdata += pxlen;
                }
            }
        }
        ycurstep += ystep_delta;
    }
    return 0;
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
