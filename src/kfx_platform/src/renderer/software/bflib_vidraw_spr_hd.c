/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_vidraw_spr_hd.c
 *     HD sprites: a scaled sprite drawn from true-colour texels instead of palette indices.
 * @par Purpose:
 *     docs/refactor/sprites/02-design.md sections 3 and 7. The sprite keeps its original geometry: the scaling
 *     step arrays LbSpriteSetScalingData() set for the original frame place it, flip it and clip it, exactly as
 *     LbSpriteDrawUsingScalingData() would draw the original. Only the colours come from the HD texels, through
 *     the palette effect's colour transform (RendererHdColourTransform()) and the remap as curves
 *     (SpriteRemapCurves()).
 *     Parity mode walks the original frame's own run-length data with the shared body's rules (the same runs,
 *     skips, clipping and row repeats) and takes each drawn pixel's colour from the HD texel in the middle of its
 *     original texel: an HD frame made by nearest-neighbour upscaling of the original draws exactly the original.
 *     Precision mode samples the HD texel under each destination pixel, its position within the original texel
 *     included, and every destination row on its own: HD detail shows, and no edge the original had moves.
 * @par Comment:
 *     Includes bflib_vidraw_spr_scale.h for its row helpers, like the three files that file names.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/software/SwDrawTarget.h"
#include "renderer/RendererManager.h"
#include "renderer/WorldFrame.h"
#include "bflib_vidraw.h"

#include <string.h>
#include <stdlib.h>
#include "globals.h"

#include "bflib_video.h"
#include "bflib_sprite.h"
#include "bflib_render.h"
#include "bflib_colour_transform.h"
#include "bflib_vidraw_spr_scale.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct HdDraw {
    const struct TbHdSpriteSource *src;
    enum SprBlend blend;  ///< Solid, Trans1, Trans2, or Alpha for additive art (src->additive)
    TbBool remap;         ///< through the remap op's curves
    const struct ColourTransform *transform; ///< NULL: the active palette is the reference
    uint8_t curves[3][256];
};

static KFX_FORCE_INLINE TbPixel hd_colour(const struct HdDraw *d, TbPixel t)
{
    if (d->transform != NULL)
        t = colour_transform_apply(d->transform, t);
    if (d->remap)
        return TbPixel_RGBA(d->curves[0][t.r], d->curves[1][t.g], d->curves[2][t.b], 255);
    return TbPixel_RGBA(t.r, t.g, t.b, 255);
}

static KFX_FORCE_INLINE TbPixel hd_blend(enum SprBlend blend, TbPixel ref, TbPixel dest)
{
    switch (blend)
    {
    case SprBl_Trans1: return render_ghost_blend(ref, dest);
    case SprBl_Trans2: return render_ghost_blend_2(ref, dest);
    default:           return ref;
    }
}

/** Additive art (an alpha-ramp draw): the destination plus the texel's add, minus its sub, clamped (the original's
 *  render_alpha_blend() with HD deltas). */
static KFX_FORCE_INLINE TbPixel hd_additive(const struct TbHdSpriteSource *src, int64_t at, TbPixel dest)
{
    const TbPixel a = src->texels[at];
    int64_t r = dest.r + a.r, g = dest.g + a.g, b = dest.b + a.b;
    if (src->sub_texels != NULL)
    {
        const TbPixel m = src->sub_texels[at];
        r -= m.r;
        g -= m.g;
        b -= m.b;
    }
    return TbPixel_RGBA((uint8_t)clamp(r, 0, 255), (uint8_t)clamp(g, 0, 255), (uint8_t)clamp(b, 0, 255), dest.a);
}

/** Parity mode, additive art: what original texel (row, col) leaves on `dest` -- the middle HD texel's add and sub; a
 *  transparent one writes black, as the original's index 0 or an index above 64 does (render_alpha_blend()). */
static KFX_FORCE_INLINE TbPixel hd_parity_additive(const struct HdDraw *d, int64_t row, int64_t col, TbPixel dest)
{
    const struct TbHdSpriteSource *src = d->src;
    if (col >= src->orig_width)
        col = src->orig_width - 1;
    if (row >= src->orig_height)
        row = src->orig_height - 1;
    const int64_t off = (src->scale - 1) / 2;
    const int64_t at = (row * src->scale + off) * src->stride + col * src->scale + off;
    if (src->texels[at].a < 128)
        return TbPixel_RGBA(0, 0, 0, dest.a);
    return hd_additive(src, at, dest);
}

/** Parity mode: the colour of original texel (row, col) -- the HD texel in the middle of its HD block. A transparent
 *  one draws as the original's index 0 does (a literal 0 inside a run: transparent black, or the remap's entry 0). */
static KFX_FORCE_INLINE TbPixel hd_parity_texel(const struct HdDraw *d, int64_t row, int64_t col)
{
    const struct TbHdSpriteSource *src = d->src;
    if (col >= src->orig_width)
        col = src->orig_width - 1;
    if (row >= src->orig_height)
        row = src->orig_height - 1;
    const int64_t off = (src->scale - 1) / 2;
    const TbPixel t = src->texels[(row * src->scale + off) * src->stride + col * src->scale + off];
    if (t.a < 128)
        return d->remap ? lbSpriteRemapTable[0] : TbPixel_Transparent;
    return hd_colour(d, t);
}

/** spr_draw_row() with the HD colours (see there): the same runs, skips, clipping and row copies. */
static const unsigned char *hd_parity_row(const struct HdDraw *d, int64_t data_row, TbPixel *out_end, int64_t *xcurstep,
    const unsigned char *sprdata, TbBool upscale, int64_t scanline, int64_t copy_rows, TbBool right_to_left)
{
    const int64_t xdir = right_to_left ? -1 : 1;
    int64_t col = 0;
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
            col -= pxlen;
            continue;
        }
        TbPixel *out_start;
        out_start = out_end;
        for (;pxlen > 0; pxlen--)
        {
            const TbBool additive = (d->blend == SprBl_Alpha);
            const TbPixel ref = additive ? TbPixel_Transparent : hd_parity_texel(d, data_row, col);
            if (!upscale)
            {
                if (xcurstep[1] > 0)
                {
                    *out_end = additive ? hd_parity_additive(d, data_row, col, *out_end) : hd_blend(d->blend, ref, *out_end);
                    out_end += xdir;
                }
            }
            else
            {
                int64_t xdup;
                xdup = xcurstep[1];
                if (xcurstep[0]+xdup > llabs(scanline))
                    xdup = llabs(scanline)-xcurstep[0];
                for (;xdup > 0; xdup--)
                {
                    *out_end = additive ? hd_parity_additive(d, data_row, col, *out_end) : hd_blend(d->blend, ref, *out_end);
                    out_end += xdir;
                }
            }
            sprdata++;
            col++;
            xcurstep += 2 * xdir;
        }
        if (copy_rows > 0)
        {
            int64_t solid_len;
            TbPixel * out_line;
            if (right_to_left)
            {
                solid_len = out_start - out_end;
                out_start = out_end + 1;
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

/** spr_scaling() with the HD colours (see there). data_row follows the run-length data: a row whose data the
 *  original reads again (a blend row clipped away) is the same HD row again. */
static void hd_parity_scaling(const struct HdDraw *d, TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep,
    int64_t *ystep, TbBool upscale, TbBool right_to_left)
{
    const unsigned char *sprdata = d->src->rle;
    const int64_t ystep_delta = (scanline < 0) ? -2 : 2;
    int64_t *ycurstep = ystep;
    int64_t data_row = 0;
    for (int64_t h = d->src->orig_height; h > 0; h--)
    {
        if (ycurstep[1] == 0)
        {
            sprdata = spr_skip_row(sprdata);
            data_row++;
        }
        else if (!upscale)
        {
            sprdata = hd_parity_row(d, data_row, outbuf, xstep, sprdata, false, scanline, 0, right_to_left);
            data_row++;
            outbuf += scanline;
        }
        else
        {
            int64_t ydup;
            ydup = ycurstep[1];
            if (ycurstep[0]+ydup > outheight)
                ydup = outheight-ycurstep[0];
            if (d->blend == SprBl_Solid)
            {
                sprdata = hd_parity_row(d, data_row, outbuf, xstep, sprdata, true, scanline, ydup - 1, right_to_left);
                data_row++;
                outbuf += scanline;
                for (int64_t ycur = ydup - 1; ycur > 0; ycur--)
                {
                    outbuf += scanline;
                }
            }
            else
            {
                const unsigned char *prevdata;
                prevdata = sprdata;
                const TbBool drawn = (ydup > 0);
                while (ydup > 0)
                {
                    sprdata = hd_parity_row(d, data_row, outbuf, xstep, prevdata, true, scanline, 0, right_to_left);
                    outbuf += scanline;
                    ydup--;
                }
                if (drawn)
                    data_row++;
            }
        }
        ycurstep += ystep_delta;
    }
}

/** Precision mode: every destination pixel the step arrays give an original texel, the HD texel under it. */
static void hd_precision(const struct HdDraw *d, int64_t posx, int64_t posy, TbBool flip_h, TbBool flip_v)
{
    const struct TbHdSpriteSource *src = d->src;
    TbPixel *const window = SwTargetGraphicsWindowPtr();
    const int64_t scan = SwTargetScanline();
    const int64_t win_w = SwTargetWindowWidth();
    const int64_t win_h = SwTargetWindowHeight();
    const int64_t s = src->scale;
    for (int64_t r = 0; r < src->orig_height; r++)
    {
        const int64_t ye = flip_v ? (posy + src->orig_height - 1 - r) : (posy + r);
        if ((ye < 0) || (ye >= SPRITE_SCALING_YSTEPS))
            continue;
        const int64_t ypos = ysteps_array[2 * ye], ylen = ysteps_array[2 * ye + 1];
        for (int64_t k = 0; k < ylen; k++)
        {
            const int64_t dy = flip_v ? (ypos + ylen - 1 - k) : (ypos + k);
            if ((dy < 0) || (dy >= win_h))
                continue;
            const TbPixel *hd_row = src->texels + (r * s + (2 * k + 1) * s / (2 * ylen)) * src->stride;
            TbPixel *out = window + dy * scan;
            for (int64_t c = 0; c < src->orig_width; c++)
            {
                const int64_t xe = flip_h ? (posx + src->orig_width - 1 - c) : (posx + c);
                if ((xe < 0) || (xe >= SPRITE_SCALING_XSTEPS))
                    continue;
                const int64_t xpos = xsteps_array[2 * xe], xlen = xsteps_array[2 * xe + 1];
                for (int64_t j = 0; j < xlen; j++)
                {
                    const int64_t dx = flip_h ? (xpos + xlen - 1 - j) : (xpos + j);
                    if ((dx < 0) || (dx >= win_w))
                        continue;
                    const int64_t at = (hd_row - src->texels) + c * s + (2 * j + 1) * s / (2 * xlen);
                    const TbPixel t = src->texels[at];
                    if (t.a < 128)
                        continue;
                    out[dx] = (d->blend == SprBl_Alpha) ? hd_additive(src, at, out[dx]) : hd_blend(d->blend, hd_colour(d, t), out[dx]);
                }
            }
        }
    }
}

static TbBool hd_draw(int64_t posx, int64_t posy, const struct TbHdSpriteSource *src)
{
    struct HdDraw d;
    memset(&d, 0, sizeof(d));
    d.src = src;
    const int64_t flags = RendererGetDrawFlags();
    if (src->additive)
    {
        d.blend = SprBl_Alpha; // what DrawAlphaSpriteUsingScalingData() draws, whatever the flags' blends
    }
    else if ((flags & Lb_SPRITE_REMAP) != 0)
    {
        // Only the sprite remaps have curves (a table of a caller's own has no op behind it).
        if ((lbSpriteReMapPtr != lbSpriteRemapTable) || (lbSpriteRemapOp.kind == 0))
            return false;
        d.remap = true;
        SpriteRemapCurves(&lbSpriteRemapOp, d.curves);
        d.blend = SprBl_Solid;
    }
    else if ((flags & Lb_SPRITE_TRANSPAR4) != 0)
        d.blend = SprBl_Trans1;
    else if ((flags & Lb_SPRITE_TRANSPAR8) != 0)
        d.blend = SprBl_Trans2;
    else
        d.blend = SprBl_Solid;
    // A GPU world frame records the sprite (Vulkan draws it, renderer/GpuSpriteRenderer.cpp).
    if (RendererWorldFrameCapturing() && (SwTargetGraphicsWindowPtr() == RendererWorldFrameWindow()))
    {
        const uint32_t mode = (d.blend == SprBl_Alpha) ? WFS_ALPHA :
            ((d.blend == SprBl_Trans1) ? WFS_GHOST1 : ((d.blend == SprBl_Trans2) ? WFS_GHOST2 : WFS_SOLID));
        return SwCaptureSpriteHd(posx, posy, src, mode, d.remap);
    }
    d.transform = RendererHdColourTransform();
    const TbBool flip_h = (flags & Lb_SPRITE_FLIP_HORIZ) != 0;
    const TbBool flip_v = (flags & Lb_SPRITE_FLIP_VERTIC) != 0;
    if (src->parity)
    {
        if (src->rle == NULL)
            return false;
        const struct TbSourceBuffer buffer = { src->rle, (uint64_t)src->orig_width, (uint64_t)src->orig_height, (uint64_t)src->orig_width };
        int64_t *xstep;
        int64_t *ystep;
        int64_t scanline;
        TbPixel *outbuf;
        int64_t outheight;
        setup_steps(posx, posy, &buffer, &xstep, &ystep, &scanline);
        setup_outbuf(xstep, ystep, &outbuf, &outheight);
        hd_parity_scaling(&d, outbuf, scanline, outheight, xstep, ystep, scale_up != 0, flip_h);
    }
    else
    {
        hd_precision(&d, posx, posy, flip_h, flip_v);
    }
    return true;
}
/** Bound i of an axis LbSpriteSetScalingData() set for x, swidth -> dwidth (factor f), by the arrays' own rule: cell i
 *  spans bound i to bound i+1, (f/2 + x*65536 + i*f) >> 16, clipped to the window's [0, g]. */
static int64_t spill_bound(int64_t x, int64_t f, int64_t i, int64_t g)
{
    const int64_t b = ((f >> 1) + x * 65536 + i * f) >> 16; // an arithmetic shift: floor, for negative i too
    return (b < 0) ? 0 : ((b > g) ? g : b);
}

/** Fills cells 0..count-1 of an axis's step array with its cells first..first+count-1: those it has (0..n-1, in
 *  `old`) unchanged, the ones past either end by the arrays' rule, meeting the end cells exactly. */
static void spill_axis(int64_t *steps, const int64_t *old, int64_t n, int64_t first, int64_t count, int64_t x, int64_t f, int64_t g)
{
    for (int64_t k = 0; k < count; k++)
    {
        const int64_t i = first + k;
        int64_t st, en;
        if (i < 0)
        {
            en = (i == -1) ? old[0] : spill_bound(x, f, i + 1, g);
            st = min(spill_bound(x, f, i, g), en);
        }
        else if (i >= n)
        {
            st = (i == n) ? (old[2 * (n - 1)] + old[2 * (n - 1) + 1]) : spill_bound(x, f, i, g);
            en = max(spill_bound(x, f, i + 1, g), st);
        }
        else
        {
            st = old[2 * i];
            en = st + old[2 * i + 1];
        }
        steps[2 * k] = st;
        steps[2 * k + 1] = en - st;
    }
}

TbBool LbSpriteDrawHdUsingScalingData(int64_t posx, int64_t posy, const struct TbHdSpriteSource *src)
{
    if ((src == NULL) || (src->texels == NULL) || (src->scale < 1) || (src->orig_width <= 0) || (src->orig_height <= 0))
        return false;
    const int64_t *spill = src->spill;
    if (src->parity || ((spill[0] | spill[1] | spill[2] | spill[3]) == 0))
        return hd_draw(posx, posy, src);
    // Art past the original's crop (precision mode): the arrays extended over the widened rectangle, in their index
    // order (a flip swaps the sides), the original's cells unchanged; drawn from there, and the arrays put back. Rows
    // the water cutoff hides hide the art below them too.
    const int64_t flags = RendererGetDrawFlags();
    const TbBool flip_h = (flags & Lb_SPRITE_FLIP_HORIZ) != 0;
    const TbBool flip_v = (flags & Lb_SPRITE_FLIP_VERTIC) != 0;
    const int64_t below = (src->orig_height == src->full_height) ? spill[3] : 0;
    const int64_t lead_x = flip_h ? spill[2] : spill[0];
    const int64_t lead_y = flip_v ? below : spill[1];
    const int64_t width = src->orig_width + spill[0] + spill[2], height = src->orig_height + spill[1] + below;
    const struct TbSpriteScaling *sc = &lbSpriteScaling;
    const int64_t nx = min(sc->swidth, (int64_t)SPRITE_SCALING_XSTEPS), ny = min(sc->sheight, (int64_t)SPRITE_SCALING_YSTEPS);
    if ((nx <= 0) || (ny <= 0) || (sc->dwidth <= 0) || (sc->dheight <= 0) ||
        (width > SPRITE_SCALING_XSTEPS) || (height > SPRITE_SCALING_YSTEPS))
    {
        struct TbHdSpriteSource core = *src;
        memset(core.spill, 0, sizeof(core.spill));
        return hd_draw(posx, posy, &core);
    }
    static int64_t saved_x[2 * SPRITE_SCALING_XSTEPS];
    static int64_t saved_y[2 * SPRITE_SCALING_YSTEPS];
    const int64_t keep_x = max(nx, width), keep_y = max(ny, height);
    memcpy(saved_x, xsteps_array, (size_t)(2 * keep_x) * sizeof(int64_t));
    memcpy(saved_y, ysteps_array, (size_t)(2 * keep_y) * sizeof(int64_t));
    spill_axis(xsteps_array, saved_x, nx, posx - lead_x, width, sc->x, (sc->dwidth << 16) / sc->swidth, SwTargetWindowWidth());
    spill_axis(ysteps_array, saved_y, ny, posy - lead_y, height, sc->y, (sc->dheight << 16) / sc->sheight, SwTargetWindowHeight());
    struct TbHdSpriteSource wide = *src;
    wide.texels = src->texels - (spill[1] * src->scale) * src->stride - spill[0] * src->scale;
    if (src->sub_texels != NULL)
        wide.sub_texels = src->sub_texels - (spill[1] * src->scale) * src->stride - spill[0] * src->scale;
    wide.orig_width = width;
    wide.orig_height = height;
    wide.full_height = src->full_height + spill[1] + spill[3];
    wide.rle = NULL;                  // its original doesn't fit this rectangle: no fallback to it on the GPU
    wide.id = src->id | (1ULL << 63); // other texels than the unwidened draw's
    memset(wide.spill, 0, sizeof(wide.spill));
    const TbBool drawn = hd_draw(0, 0, &wide);
    memcpy(xsteps_array, saved_x, (size_t)(2 * keep_x) * sizeof(int64_t));
    memcpy(ysteps_array, saved_y, (size_t)(2 * keep_y) * sizeof(int64_t));
    return drawn;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
