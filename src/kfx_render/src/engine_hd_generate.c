/******************************************************************************/
// Free implementation of the Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_hd_generate.c
 *     Generated HD art: the original art enlarged with Lanczos smoothing and an unsharp mask (ASSET_PACK_FILL).
 * @par Purpose:
 *     See engine_hd_generate.h. The resampling follows Pillow's (scripts/hd_resample.py, the draft packs' maker):
 *     Lanczos with a support of 3, each output texel's source centre (x + 0.5) / factor, weights normalised over the
 *     texels inside the image, horizontal then vertical, each pass rounded and clamped to 0..255; the unsharp mask a
 *     Gaussian blur of radius (sigma) 1 subtracted where the difference is at least the threshold. Close to Pillow's,
 *     not bit for bit (its blur is three box passes).
 * @author   KeeperFX Team
 * @date     12 Oct 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "engine_hd_generate.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "globals.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define GEN_LANCZOS_SUPPORT 3.0
#define GEN_SHARPEN_PERCENT 120   ///< UnsharpMask(radius 1, percent 120, threshold 3): batch_upscale.py's 1.2
#define GEN_SHARPEN_THRESHOLD 3
#define GEN_SHARPEN_SIGMA 1.0
#define GEN_SOFTEN 0.5            ///< the alpha's Gaussian, in original texels
#define GEN_BLEED_RINGS 4         ///< how far colours spread into the transparent texels around cut-out art

static double sinc(double x)
{
    if (x == 0.0)
        return 1.0;
    x *= 3.14159265358979323846;
    return sin(x) / x;
}

static double lanczos(double x)
{
    return ((x > -GEN_LANCZOS_SUPPORT) && (x < GEN_LANCZOS_SUPPORT)) ? sinc(x) * sinc(x / GEN_LANCZOS_SUPPORT) : 0.0;
}

/** Each output texel's Lanczos taps over `in_len` source texels at `factor`x: first[i], count[i], and the weights
 *  (normalised) at w[i * taps], taps a texel at most. */
struct Taps {
    int64_t taps;
    int64_t *first;
    int64_t *count;
    double *w;
};

static TbBool taps_make(struct Taps *t, int64_t in_len, int64_t factor)
{
    const int64_t out_len = in_len * factor;
    t->taps = (int64_t)(2.0 * GEN_LANCZOS_SUPPORT) + 2;
    t->first = (int64_t *)malloc((size_t)out_len * sizeof(int64_t));
    t->count = (int64_t *)malloc((size_t)out_len * sizeof(int64_t));
    t->w = (double *)calloc((size_t)(out_len * t->taps), sizeof(double));
    if ((t->first == NULL) || (t->count == NULL) || (t->w == NULL))
        return false;
    // Away from the edges an output texel's weights depend only on its phase (i % factor): computed once per phase.
    int64_t phase_from[16];
    for (int64_t k = 0; k < 16; k++)
        phase_from[k] = -1;
    for (int64_t i = 0; i < out_len; i++)
    {
        const double centre = ((double)i + 0.5) / (double)factor;
        const int64_t lo_free = (int64_t)(centre - GEN_LANCZOS_SUPPORT + 0.5);
        const int64_t hi_free = (int64_t)(centre + GEN_LANCZOS_SUPPORT + 0.5);
        int64_t lo = lo_free, hi = hi_free;
        if (lo < 0)
            lo = 0;
        if (hi > in_len)
            hi = in_len;
        if (hi - lo > t->taps)
            hi = lo + t->taps;
        // (Truncation toward zero: below 0 the first tap isn't the phase's, so those aren't shared either.)
        const TbBool clipped = (lo != lo_free) || (hi != hi_free) || (centre - GEN_LANCZOS_SUPPORT + 0.5 < 0.0);
        const int64_t phase = i % factor;
        if (!clipped && (factor <= 16) && (phase_from[phase] >= 0))
        {
            const int64_t from = phase_from[phase];
            memcpy(&t->w[i * t->taps], &t->w[from * t->taps], (size_t)t->taps * sizeof(double));
            t->first[i] = lo;
            t->count[i] = hi - lo;
            continue;
        }
        if (!clipped && (factor <= 16))
            phase_from[phase] = i;
        double sum = 0.0;
        for (int64_t j = lo; j < hi; j++)
        {
            const double v = lanczos((double)j - centre + 0.5);
            t->w[i * t->taps + (j - lo)] = v;
            sum += v;
        }
        if (sum != 0.0)
            for (int64_t j = 0; j < hi - lo; j++)
                t->w[i * t->taps + j] /= sum;
        t->first[i] = lo;
        t->count[i] = hi - lo;
    }
    return true;
}

static void taps_free(struct Taps *t)
{
    free(t->first);
    free(t->count);
    free(t->w);
}

static uint8_t clamp8(float v)
{
    return (v <= 0.0f) ? 0 : ((v >= 255.0f) ? 255 : (uint8_t)(v + 0.5f));
}

/** A rectangle of a factor x enlargement of `src` (`ch` channels of sw x sh, interleaved): output texels [x0, x1) x
 *  [y0, y1) of the whole, into dst (x1 - x0 wide). Horizontal then vertical, each pass rounded and clamped, as
 *  Pillow's 8-bit resampling. */
static TbBool resample(const uint8_t *src, int64_t sw, int64_t sh, int ch, int64_t factor, int64_t x0, int64_t x1,
    int64_t y0, int64_t y1, uint8_t *dst)
{
    struct Taps tx, ty;
    memset(&tx, 0, sizeof(tx));
    memset(&ty, 0, sizeof(ty));
    const int64_t rw = x1 - x0;
    uint8_t *mid = (uint8_t *)malloc((size_t)(rw * sh * ch));
    TbBool ok = (mid != NULL) && taps_make(&tx, sw, factor) && taps_make(&ty, sh, factor);
    if (ok)
    {
        // Only the source rows the vertical taps of rows y0..y1 reach.
        const int64_t sy0 = ty.first[y0], sy1 = ty.first[y1 - 1] + ty.count[y1 - 1];
        for (int64_t y = sy0; y < sy1; y++)
            for (int64_t x = x0; x < x1; x++)
            {
                const double *w = &tx.w[x * tx.taps];
                const uint8_t *row = &src[(y * sw + tx.first[x]) * ch];
                for (int c = 0; c < ch; c++)
                {
                    float v = 0.0f;
                    for (int64_t k = 0; k < tx.count[x]; k++)
                        v += (float)w[k] * row[k * ch + c];
                    mid[(y * rw + x - x0) * ch + c] = clamp8(v);
                }
            }
        const int64_t row_len = rw * ch;
        float *acc = (float *)malloc((size_t)row_len * sizeof(float));
        ok = (acc != NULL);
        for (int64_t y = y0; ok && (y < y1); y++)
        {
            const double *w = &ty.w[y * ty.taps];
            for (int64_t j = 0; j < row_len; j++)
                acc[j] = 0.0f;
            for (int64_t k = 0; k < ty.count[y]; k++)
            {
                const float kv = (float)w[k];
                const uint8_t *m = &mid[(ty.first[y] + k) * row_len];
                for (int64_t j = 0; j < row_len; j++)
                    acc[j] += kv * m[j];
            }
            for (int64_t j = 0; j < row_len; j++)
                dst[(y - y0) * row_len + j] = clamp8(acc[j]);
        }
        free(acc);
    }
    free(mid);
    taps_free(&tx);
    taps_free(&ty);
    return ok;
}

/** A Gaussian blur (sigma, in texels) of `ch` interleaved channels of src (w x h, edges extended), for the texels
 *  inset by `inset` on each side only: dst is (w - 2 inset) x (h - 2 inset). */
static TbBool gaussian(const uint8_t *src, int64_t w, int64_t h, int ch, double sigma, int64_t inset, uint8_t *dst)
{
    const int64_t r = (int64_t)ceil(3.0 * sigma);
    const int64_t dw = w - 2 * inset, dh = h - 2 * inset;
    float *k = (float *)malloc((size_t)(2 * r + 1) * sizeof(float));
    float *mid = (float *)malloc((size_t)(dw * h * ch) * sizeof(float));
    if ((k == NULL) || (mid == NULL) || (dw <= 0) || (dh <= 0))
    {
        free(k);
        free(mid);
        return false;
    }
    double sum = 0.0;
    for (int64_t i = -r; i <= r; i++)
        sum += exp(-(double)(i * i) / (2.0 * sigma * sigma));
    for (int64_t i = -r; i <= r; i++)
        k[i + r] = (float)(exp(-(double)(i * i) / (2.0 * sigma * sigma)) / sum);
    // The source as floats, edges extended by r (none needed when the inset covers the blur's reach).
    const int64_t ew = w + 2 * r;
    float *ext = (float *)malloc((size_t)(ew * h * ch) * sizeof(float));
    if (ext == NULL)
    {
        free(k);
        free(mid);
        return false;
    }
    for (int64_t y = 0; y < h; y++)
        for (int64_t x = 0; x < ew; x++)
        {
            const int64_t xx = (x - r < 0) ? 0 : ((x - r >= w) ? w - 1 : x - r);
            for (int c = 0; c < ch; c++)
                ext[(y * ew + x) * ch + c] = src[(y * w + xx) * ch + c];
        }
    // Horizontal: output column x reads ext columns x + inset .. x + inset + 2r, all channels together.
    const int64_t row_len = dw * ch;
    for (int64_t y = 0; y < h; y++)
    {
        float *o = &mid[y * row_len];
        for (int64_t j = 0; j < row_len; j++)
            o[j] = 0.0f;
        for (int64_t i = 0; i <= 2 * r; i++)
        {
            const float kv = k[i];
            const float *e = &ext[(y * ew + inset + i) * ch];
            for (int64_t j = 0; j < row_len; j++)
                o[j] += kv * e[j];
        }
    }
    // Vertical: output row y reads mid rows y + inset - r .. y + inset + r, clamped.
    float *acc = (float *)malloc((size_t)row_len * sizeof(float));
    if (acc == NULL)
    {
        free(k);
        free(mid);
        free(ext);
        return false;
    }
    for (int64_t y = 0; y < dh; y++)
    {
        for (int64_t j = 0; j < row_len; j++)
            acc[j] = 0.0f;
        for (int64_t i = -r; i <= r; i++)
        {
            int64_t yy = y + inset + i;
            yy = (yy < 0) ? 0 : ((yy >= h) ? h - 1 : yy);
            const float kv = k[i + r];
            const float *m = &mid[yy * row_len];
            for (int64_t j = 0; j < row_len; j++)
                acc[j] += kv * m[j];
        }
        for (int64_t j = 0; j < row_len; j++)
            dst[y * row_len + j] = clamp8(acc[j]);
    }
    free(ext);
    free(acc);
    free(k);
    free(mid);
    return true;
}

/** Cut-out art's colours spread into the transparent texels around it (rgb, w x h, opaque[] marks the art): ring by
 *  ring, each new texel the mean of its filled 8-neighbours; the rest the art's mean colour. */
static void bleed(uint8_t *rgb, const uint8_t *opaque, int64_t w, int64_t h)
{
    uint8_t *filled = (uint8_t *)malloc((size_t)(w * h));
    uint8_t *grow = (uint8_t *)malloc((size_t)(w * h));
    if ((filled == NULL) || (grow == NULL))
    {
        free(filled);
        free(grow);
        return;
    }
    memcpy(filled, opaque, (size_t)(w * h));
    double mean[3] = { 0.0, 0.0, 0.0 };
    int64_t n = 0;
    for (int64_t i = 0; i < w * h; i++)
        if (opaque[i])
        {
            for (int c = 0; c < 3; c++)
                mean[c] += rgb[i * 3 + c];
            n++;
        }
    for (int ring = 0; (ring < GEN_BLEED_RINGS) && (n > 0); ring++)
    {
        memset(grow, 0, (size_t)(w * h));
        TbBool any = false;
        for (int64_t y = 0; y < h; y++)
            for (int64_t x = 0; x < w; x++)
            {
                if (filled[y * w + x])
                    continue;
                int sum[3] = { 0, 0, 0 }, cnt = 0;
                for (int64_t dy = -1; dy <= 1; dy++)
                    for (int64_t dx = -1; dx <= 1; dx++)
                    {
                        const int64_t xx = x + dx, yy = y + dy;
                        if (((dx == 0) && (dy == 0)) || (xx < 0) || (yy < 0) || (xx >= w) || (yy >= h) || !filled[yy * w + xx])
                            continue;
                        for (int c = 0; c < 3; c++)
                            sum[c] += rgb[(yy * w + xx) * 3 + c];
                        cnt++;
                    }
                if (cnt == 0)
                    continue;
                for (int c = 0; c < 3; c++)
                    rgb[(y * w + x) * 3 + c] = (uint8_t)((sum[c] + cnt / 2) / cnt);
                grow[y * w + x] = 1;
                any = true;
            }
        if (!any)
            break;
        for (int64_t i = 0; i < w * h; i++)
            filled[i] |= grow[i];
    }
    for (int64_t i = 0; (i < w * h) && (n > 0); i++)
        if (!filled[i])
            for (int c = 0; c < 3; c++)
                rgb[i * 3 + c] = clamp8(mean[c] / (double)n);
    free(filled);
    free(grow);
}

TbBool hd_generate_upscale(const TbPixel *in, int64_t in_stride, int64_t w, int64_t h, int64_t pad, int64_t factor,
    TbBool additive, TbPixel *out, int64_t out_stride)
{
    if ((w <= 0) || (h <= 0) || (pad < 0) || (factor < 1))
        return false;
    // The enlargement is computed for the output (the art without its context) and the blurs' reach around it: m
    // texels a side, as far as the context goes.
    const int64_t pw = w + 2 * pad, ph = h + 2 * pad, ow = w * factor, oh = h * factor;
    const int64_t reach = (int64_t)ceil(3.0 * GEN_SOFTEN * (double)factor);
    const int64_t m = (reach < pad * factor) ? reach : pad * factor;
    const int64_t x0 = pad * factor - m, y0 = pad * factor - m, rw = ow + 2 * m, rh = oh + 2 * m;
    uint8_t *rgb = (uint8_t *)malloc((size_t)(pw * ph * 3));
    uint8_t *opaque = (uint8_t *)malloc((size_t)(pw * ph));
    uint8_t *mask = (uint8_t *)malloc((size_t)(pw * ph));
    uint8_t *big = (uint8_t *)malloc((size_t)(rw * rh * 3));
    uint8_t *big_mask = (uint8_t *)malloc((size_t)(rw * rh));
    uint8_t *blur = (uint8_t *)malloc((size_t)(ow * oh * 3));
    uint8_t *soft = (uint8_t *)malloc((size_t)(ow * oh));
    TbBool ok = (rgb != NULL) && (opaque != NULL) && (mask != NULL) && (big != NULL) && (big_mask != NULL) && (blur != NULL) &&
        (soft != NULL);
    if (ok)
    {
        for (int64_t y = 0; y < ph; y++)
            for (int64_t x = 0; x < pw; x++)
            {
                const TbPixel t = in[y * in_stride + x];
                const int64_t i = y * pw + x;
                opaque[i] = (t.a >= 128) ? 1 : 0;
                mask[i] = opaque[i] ? 255 : 0;
                rgb[i * 3 + 0] = (opaque[i] || !additive) ? t.r : 0;
                rgb[i * 3 + 1] = (opaque[i] || !additive) ? t.g : 0;
                rgb[i * 3 + 2] = (opaque[i] || !additive) ? t.b : 0;
            }
        if (!additive)
            bleed(rgb, opaque, pw, ph);
        ok = resample(rgb, pw, ph, 3, factor, x0, x0 + rw, y0, y0 + rh, big) &&
            resample(mask, pw, ph, 1, factor, x0, x0 + rw, y0, y0 + rh, big_mask) &&
            gaussian(big, rw, rh, 3, GEN_SHARPEN_SIGMA, m, blur) &&
            gaussian(big_mask, rw, rh, 1, GEN_SOFTEN * (double)factor, m, soft);
    }
    if (ok)
    {
        // The middle (factor - 1) texels square of each opaque original texel stay opaque.
        const int64_t core = (factor > 1) ? factor - 1 : 1, lo = (factor - core) / 2;
        for (int64_t y = 0; y < oh; y++)
            for (int64_t x = 0; x < ow; x++)
            {
                const int64_t ox = x / factor + pad, oy = y / factor + pad;
                const TbBool in_core = opaque[oy * pw + ox] && ((x % factor) >= lo) && ((x % factor) < lo + core) &&
                    ((y % factor) >= lo) && ((y % factor) < lo + core);
                TbPixel *o = &out[y * out_stride + x];
                if (!in_core && (soft[y * ow + x] < 128))
                {
                    *o = TbPixel_Transparent;
                    continue;
                }
                const uint8_t *v3 = &big[((y + m) * rw + x + m) * 3];
                const uint8_t *b3 = &blur[(y * ow + x) * 3];
                uint8_t c3[3];
                for (int c = 0; c < 3; c++)
                {
                    const int diff = (int)v3[c] - (int)b3[c];
                    const int sharp = (abs(diff) >= GEN_SHARPEN_THRESHOLD) ? v3[c] + diff * GEN_SHARPEN_PERCENT / 100 : v3[c];
                    c3[c] = (uint8_t)((sharp < 0) ? 0 : ((sharp > 255) ? 255 : sharp));
                }
                *o = TbPixel_RGBA(c3[0], c3[1], c3[2], 255);
            }
    }
    free(rgb);
    free(opaque);
    free(mask);
    free(big);
    free(big_mask);
    free(blur);
    free(soft);
    return ok;
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
