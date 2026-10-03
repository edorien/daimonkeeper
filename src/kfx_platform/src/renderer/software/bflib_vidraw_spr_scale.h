/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_vidraw_spr_scale.h
 *     The scaled-sprite inner loops shared by bflib_vidraw_spr_norm.c, _onec.c and _remp.c.
 * @par Purpose:
 *     Draws one RLE sprite through the scaling step arrays (LbSpriteSetScalingData()), scaled up or
 *     down. Each of the 36 public LbSprite*UsingScaling{Up,Down}Data* functions is a call to one of
 *     the two bodies here with constant pixel operation, blend and direction, which the compiler
 *     specialises (refactor pass 3, S08). Private to the software renderer: include only from those
 *     three files.
 * @par Comment:
 *     Sprite data is rows of: n > 0 then n pixel bytes, n < 0 to skip -n pixels, 0 to end the row.
 *     xstep/ystep hold (position, length) pairs per source column/row; a length of 0 means the
 *     column/row isn't drawn.
 */
/******************************************************************************/
#ifndef DK_VIDRAW_SPR_SCALE_H
#define DK_VIDRAW_SPR_SCALE_H

#include "compiler_compat.h"
#include "bflib_video.h"
#include "bflib_render.h"
#include "bflib_vidraw.h"

#include <stdint.h>
#include <stdlib.h>

/** Where a drawn pixel's colour comes from. */
enum SprPixelOp {
    SprPx_Copy,      /**< the sprite's own colour (its palette index through the active palette) */
    SprPx_Remap,     /**< the sprite's palette index through a colour map */
    SprPx_OneColour, /**< one fixed colour for every pixel (a silhouette) */
};

/** How a drawn pixel goes onto the destination. */
enum SprBlend {
    SprBl_Solid,  /**< replaces it */
    SprBl_Trans1, /**< ghost blend, 1/3 of the sprite */
    SprBl_Trans2, /**< ghost blend, 2/3 of the sprite */
    SprBl_Alpha,  /**< the sprite palette's alpha (SprPx_Copy only: DrawAlphaSpriteUsingScalingData()) */
};

/** What the pixel operation needs. */
struct SprPixelSource {
    const unsigned char *palette; /**< SprPx_Copy */
    const TbPixel *cmap;          /**< SprPx_Remap */
    TbPixel colour;               /**< SprPx_OneColour */
};

/**
 * Ghost/invisible-creature blend: `output = (colour + 2*dest) / 3` -- a fixed
 * 1/3-weight tint of `colour` onto whatever's already at the destination.
 * Replaces the old `render_ghost[colour<<8 | dest]` table lookup every
 * one-colour "Trans1" variant used to do -- see
 * docs/refactor/renderer/02a-pixel-format-design.md §2.2. `colour` is the
 * fixed silhouette colour; `dest` is the current framebuffer pixel being
 * drawn over. Not render_ghost_blend(), which the copy and remap operations
 * use: the two differ on an opaque destination.
 */
static inline TbPixel ghost_blend_1(TbPixel colour, TbPixel dest)
{
    if (dest.a != 255)
        return render_ghost_blend(colour, dest); // transparent GPU-layer window: alpha-aware (see bflib_render.h)
    return TbPixel_RGB(
        (uint8_t)((colour.r + 2 * dest.r) / 3),
        (uint8_t)((colour.g + 2 * dest.g) / 3),
        (uint8_t)((colour.b + 2 * dest.b) / 3));
}

/**
 * Ghost/invisible-creature blend, the "Trans2" (reverse) weighting used by
 * the one-colour Trans2 variants: `output = (2*colour + dest) / 3` -- mostly
 * the fixed silhouette colour, lightly blended toward the destination.
 * Replaces the old `render_ghost[dest<<8 | colour]` table lookup (note the
 * reversed index order vs. ghost_blend_1 -- deliberately a different blend,
 * confirmed by tracing the original `pxmap` computation, not the same
 * formula applied twice). See
 * docs/refactor/renderer/02a-pixel-format-design.md §2.2/§3.2.
 */
static inline TbPixel ghost_blend_2(TbPixel colour, TbPixel dest)
{
    if (dest.a != 255)
        return render_ghost_blend_2(colour, dest);
    return TbPixel_RGB(
        (uint8_t)((2 * colour.r + dest.r) / 3),
        (uint8_t)((2 * colour.g + dest.g) / 3),
        (uint8_t)((2 * colour.b + dest.b) / 3));
}

/** The colour a sprite pixel draws with, before blending. */
static KFX_FORCE_INLINE TbPixel spr_source_pixel(enum SprPixelOp op, uint8_t texel, const struct SprPixelSource *px)
{
    switch (op)
    {
    case SprPx_Remap:     return px->cmap[texel];
    case SprPx_OneColour: return px->colour;
    case SprPx_Copy:
    default:              return expand_indexed_pixel(texel, px->palette);
    }
}

/** What a sprite pixel (its index `texel`, its colour `ref`) leaves on the destination pixel `dest`. */
static KFX_FORCE_INLINE TbPixel spr_blend_pixel(enum SprPixelOp op, enum SprBlend blend, uint8_t texel, TbPixel ref,
    TbPixel dest, const struct SprPixelSource *px)
{
    switch (blend)
    {
    case SprBl_Trans1:
        if (op == SprPx_OneColour)
            return ghost_blend_1(ref, dest);
        return render_ghost_blend(ref, dest);
    case SprBl_Trans2:
        if (op == SprPx_OneColour)
            return ghost_blend_2(ref, dest);
        return render_ghost_blend_2(ref, dest);
    case SprBl_Alpha:
        return render_alpha_blend(texel, dest);
    case SprBl_Solid:
    default:
        return ref;
    }
}

/**
 * The step over a skipped run of `pxlen` source pixels: moves out_end and xcurstep past them,
 * leftwards for a right-to-left draw.
 */
static KFX_FORCE_INLINE void spr_skip_run(TbPixel **out_end, int64_t **xcurstep, int64_t pxlen, TbBool right_to_left)
{
    if (right_to_left)
    {
        *out_end -= (*xcurstep)[0] + (*xcurstep)[1];
        *xcurstep -= 2 * pxlen;
        *out_end += (*xcurstep)[0] + (*xcurstep)[1];
    }
    else
    {
        *out_end -= (*xcurstep)[0];
        *xcurstep += 2 * pxlen;
        *out_end += (*xcurstep)[0];
    }
}

/** Skips one source row of RLE data. */
static KFX_FORCE_INLINE const unsigned char *spr_skip_row(const unsigned char *sprdata)
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
    return sprdata;
}

/**
 * Draws one source row of a sprite, returning the source data after it.
 * Scaled down, a source pixel with a step length above 0 is one destination pixel; scaled up, it is
 * its step length of pixels, clipped at the end of the scanline.
 * @param out_end Where the row's first drawn pixel goes.
 * @param copy_rows Rows below this one each run is copied to once drawn (a solid draw repeating the
 *     row); 0 for none. A right-to-left copy takes one pixel more than was drawn, the one just left of
 *     the run (kept as it was).
 */
static KFX_FORCE_INLINE const unsigned char *spr_draw_row(TbPixel *out_end, int64_t *xcurstep,
    const unsigned char *sprdata, TbBool upscale, int64_t scanline, int64_t copy_rows, enum SprPixelOp op,
    enum SprBlend blend, TbBool right_to_left, const struct SprPixelSource *px)
{
    const int64_t xdir = right_to_left ? -1 : 1;
    while ( 1 )
    {
        int64_t pxlen;
        pxlen = (signed char)*sprdata;
        sprdata++;
        if (pxlen == 0)
            break;
        if (pxlen < 0)
        {
            spr_skip_run(&out_end, &xcurstep, -pxlen, right_to_left);
            continue;
        }
        TbPixel *out_start;
        out_start = out_end;
        for (;pxlen > 0; pxlen--)
        {
            uint8_t texel = *sprdata;
            if (!upscale)
            {
                if (xcurstep[1] > 0)
                {
                    *out_end = spr_blend_pixel(op, blend, texel, spr_source_pixel(op, texel, px), *out_end, px);
                    out_end += xdir;
                }
            }
            else
            {
                int64_t xdup;
                xdup = xcurstep[1];
                if (xcurstep[0]+xdup > llabs(scanline))
                    xdup = llabs(scanline)-xcurstep[0];
                if (xdup > 0)
                {
                    TbPixel ref = spr_source_pixel(op, texel, px);
                    for (;xdup > 0; xdup--)
                    {
                        *out_end = spr_blend_pixel(op, blend, texel, ref, *out_end, px);
                        out_end += xdir;
                    }
                }
            }
            sprdata++;
            xcurstep += 2 * xdir;
        }
        if (copy_rows > 0)
        {
            int64_t solid_len;
            TbPixel * out_line;
            if (right_to_left)
            {
                solid_len = out_start - out_end;
                out_start = out_end;
                solid_len++;
            }
            else
            {
                solid_len = out_end - out_start;
            }
            out_line = out_start + scanline;
            for (int64_t ycur = copy_rows; ycur > 0; ycur--)
            {
                if (solid_len > 0) {
                    LbPixelBlockCopyForward(out_line, out_start, solid_len);
                }
                out_line += scanline;
            }
        }
    }
    return sprdata;
}

/**
 * Draws a sprite through the scaling step arrays.
 * @param outbuf Where the sprite's first drawn pixel goes (see setup_outbuf()).
 * @param scanline The output scanline; negative for a vertically flipped draw.
 * @param outheight The output buffer's height; a row's repeats stop there (scaled up only).
 * @param xstep, ystep The sprite's first column's and row's scaling steps (see setup_steps()).
 * @param sprdata, height The RLE sprite.
 * @param upscale Scaled down, each source row is at most one destination row; scaled up, a row is
 *     repeated its step length of rows, clipped at outheight. Solid draws a repeated row once and
 *     copies it; the blends draw every repeat, since each depends on the destination.
 */
static KFX_FORCE_INLINE TbResult spr_scaling(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep,
    int64_t *ystep, const unsigned char *sprdata, int64_t height, TbBool upscale, enum SprPixelOp op,
    enum SprBlend blend, TbBool right_to_left, const struct SprPixelSource *px)
{
    SYNCDBG(17,"Drawing");
    int64_t ystep_delta;
    int64_t *ycurstep;

    ystep_delta = 2;
    if (scanline < 0) {
        ystep_delta = -2;
    }
    ycurstep = ystep;

    for (int64_t h = height; h > 0; h--)
    {
        if (ycurstep[1] == 0)
        {
            sprdata = spr_skip_row(sprdata);
        }
        else if (!upscale)
        {
            sprdata = spr_draw_row(outbuf, xstep, sprdata, false, scanline, 0, op, blend, right_to_left, px);
            outbuf += scanline;
        }
        else
        {
            int64_t ydup;
            ydup = ycurstep[1];
            if (ycurstep[0]+ydup > outheight)
                ydup = outheight-ycurstep[0];
            if (blend == SprBl_Solid)
            {
                // Drawn once even when clipped away (ydup <= 0), as it was.
                sprdata = spr_draw_row(outbuf, xstep, sprdata, true, scanline, ydup - 1, op, blend, right_to_left, px);
                outbuf += scanline;
                for (int64_t ycur = ydup - 1; ycur > 0; ycur--)
                {
                    outbuf += scanline;
                }
            }
            else
            {
                // Not drawn when clipped away, and then the row's data isn't skipped: the next row
                // reads it again (kept as it was).
                const unsigned char *prevdata;
                prevdata = sprdata;
                while (ydup > 0)
                {
                    sprdata = spr_draw_row(outbuf, xstep, prevdata, true, scanline, 0, op, blend, right_to_left, px);
                    outbuf += scanline;
                    ydup--;
                }
            }
        }
        ycurstep += ystep_delta;
    }
    return 0;
}

/** A sprite scaled down; see spr_scaling(). */
static KFX_FORCE_INLINE TbResult spr_scaling_down(TbPixel *outbuf, int64_t scanline, int64_t *xstep, int64_t *ystep,
    const unsigned char *sprdata, int64_t height, enum SprPixelOp op, enum SprBlend blend, TbBool right_to_left,
    const struct SprPixelSource *px)
{
    return spr_scaling(outbuf, scanline, 0, xstep, ystep, sprdata, height, false, op, blend, right_to_left, px);
}

/** A sprite scaled up; see spr_scaling(). */
static KFX_FORCE_INLINE TbResult spr_scaling_up(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep,
    int64_t *ystep, const unsigned char *sprdata, int64_t height, enum SprPixelOp op, enum SprBlend blend,
    TbBool right_to_left, const struct SprPixelSource *px)
{
    return spr_scaling(outbuf, scanline, outheight, xstep, ystep, sprdata, height, true, op, blend, right_to_left, px);
}

#endif
