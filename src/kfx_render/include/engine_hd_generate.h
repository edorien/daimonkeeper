/******************************************************************************/
// Free implementation of the Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_hd_generate.h
 *     Generated HD art: the original art enlarged with Lanczos smoothing and an unsharp mask (ASSET_PACK_FILL).
 * @par Purpose:
 *     The in-game version of scripts/hd_resample.py (spritepack.py / texpack.py --filter lanczos): Pillow's Lanczos
 *     resampling at an integer factor, then UnsharpMask(radius 1, 120%, threshold 3), with what game art needs on
 *     top -- cut-out art's colours spread into the transparent texels around it first (no dark fringes), its alpha
 *     filtered alone, softened by half an original texel and cut at 128, every opaque texel's middle kept (no thin
 *     detail lost); additive art's transparent texels adding nothing. The HD sprite and texture stores use it for the
 *     frames and blocks no pack covers.
 * @author   KeeperFX Team
 * @date     12 Oct 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_ENGINE_HD_GENERATE_H
#define DK_ENGINE_HD_GENERATE_H

#include <stdint.h>
#include "bflib_basics.h"
#include "bflib_video.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/** Generated art's scale: texels per original texel (sprites x4, terrain 128). */
#define HD_GENERATE_FACTOR 4
/** Original texels of context the caller puts around the art: the filters reach about 3 across an edge. */
#define HD_GENERATE_PAD 4

/** Enlarges `in` (w + 2 * pad by h + 2 * pad RGBA texels, row stride in_stride; alpha below 128 transparent; the
 *  pad texels around the art are context, not output) `factor` times into `out` (w * factor by h * factor, row stride
 *  out_stride; alpha 0 or 255, transparent texels (0,0,0,0)). Cut-out art (sprites, terrain) unless `additive` (each
 *  texel's RGB is what it adds to the screen). False when out of memory (out untouched). Thread-safe. */
TbBool hd_generate_upscale(const TbPixel *in, int64_t in_stride, int64_t w, int64_t h, int64_t pad, int64_t factor,
    TbBool additive, TbPixel *out, int64_t out_stride);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
