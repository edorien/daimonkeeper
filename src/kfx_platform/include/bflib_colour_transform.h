/******************************************************************************/
// Free implementation of the Dungeon Keeper strategy game.
/******************************************************************************/
/** @file bflib_colour_transform.h
 *     A palette change as a colour-to-colour function.
 * @par Purpose:
 *     The palette effects (freeze, lightning, white flash, pain and level fades,
 *     gamma) swap the palette that 8-bit texels expand through. Texels already
 *     expanded to colours -- the GPU renderer's cached texture layers, true-colour
 *     HD textures -- can follow such a swap only through a function from the old
 *     palette's colours to the new one's. This builds that function, exactly where
 *     it exists (docs/newFeatures/texture-resolution/02-design.md section 3.3):
 *     - per-channel curves when each new channel depends only on the same old
 *       channel (the fades, white);
 *     - a 64^3 table otherwise (freeze, lightning), indexed by the colour's 6-bit
 *       channels, each point taking the new colour of the nearest old entry;
 *     - "unsupported" when two entries share an old colour but get different new
 *       ones: then no function reproduces the swap and the caller re-expands.
 *     Palette index 0 is left out: texture texel 0 always shows black (it is art,
 *     docs/newFeatures/texture-resolution/01-current-pipeline.md section 7), so black
 *     maps to black whatever the palettes say.
 * @par Comment:
 *     Palettes are 768 bytes of 6-bit VGA values, as everywhere in the engine.
 * @author   KeeperFX Team
 * @date     07 Oct 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef BFLIB_COLOUR_TRANSFORM_H
#define BFLIB_COLOUR_TRANSFORM_H

#include <stdint.h>
#include "bflib_video.h" // TbPixel

#ifdef __cplusplus
extern "C" {
#endif

#define COLOUR_TRANSFORM_TABLE_SIDE 64
#define COLOUR_TRANSFORM_TABLE_SIZE (COLOUR_TRANSFORM_TABLE_SIDE * COLOUR_TRANSFORM_TABLE_SIDE * COLOUR_TRANSFORM_TABLE_SIDE)

enum ColourTransformMode {
    CTM_Identity = 0,    ///< the palettes are the same: colours stay
    CTM_Curves = 1,      ///< curves[channel][8-bit value]
    CTM_Table = 2,       ///< table[(r6 << 12) | (g6 << 6) | b6], RGBA
    CTM_Unsupported = 3, ///< no colour function reproduces the swap
};

struct ColourTransform {
    enum ColourTransformMode mode;
    uint8_t curves[3][256];
    /** COLOUR_TRANSFORM_TABLE_SIZE RGBA texels (alpha 255); the caller owns it, NULL when only curves are wanted. */
    uint8_t *table;
};

/** For each 6-bit colour (r6 << 12 | g6 << 6 | b6), a palette entry near it (entries 1..255 only): an entry's
 *  own colour gets an entry of that colour, exactly; any other colour the entry nearest to the centre of its
 *  2x2x2 cell (squared RGB distance). out holds COLOUR_TRANSFORM_TABLE_SIZE bytes. */
void colour_transform_nearest_entries(const unsigned char from[768], uint8_t *out);

/** Builds the function from `from`'s colours to `to`'s into ct (ct->table must be allocated when a table may be
 *  needed; with ct->table NULL a non-separable swap is reported unsupported). `nearest` is
 *  colour_transform_nearest_entries(from), needed only for a table (may be NULL otherwise: then a table is
 *  unsupported too). Returns ct->mode. */
enum ColourTransformMode colour_transform_build(struct ColourTransform *ct, const unsigned char from[768],
    const unsigned char to[768], const uint8_t *nearest);

/** The colour a texel expanded through `from` has after the swap to `to`. Exact (equal to expanding through
 *  `to`) for every colour of `from`'s entries 1..255, and black stays black. */
static inline TbPixel colour_transform_apply(const struct ColourTransform *ct, TbPixel c)
{
    if ((ct->mode != CTM_Curves) && (ct->mode != CTM_Table))
        return c;
    if ((c.r | c.g | c.b) == 0)
        return c;
    if (ct->mode == CTM_Curves)
        return TbPixel_RGBA(ct->curves[0][c.r], ct->curves[1][c.g], ct->curves[2][c.b], c.a);
    const uint8_t *t = ct->table + 4 * (((uint32_t)(c.r >> 2) << 12) | ((uint32_t)(c.g >> 2) << 6) | (uint32_t)(c.b >> 2));
    return TbPixel_RGBA(t[0], t[1], t[2], c.a);
}

#ifdef __cplusplus
}
#endif
#endif
