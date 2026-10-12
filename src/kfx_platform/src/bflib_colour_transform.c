/******************************************************************************/
// Free implementation of the Dungeon Keeper strategy game.
/******************************************************************************/
/** @file bflib_colour_transform.c
 *     A palette change as a colour-to-colour function.
 * @par Purpose:
 *     See bflib_colour_transform.h.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     07 Oct 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "bflib_colour_transform.h"

#include <string.h>

#include "post_inc.h"

/******************************************************************************/
#define PAL_ENTRIES 256

void colour_transform_nearest_entries(const unsigned char from[768], uint8_t *out)
{
    // Distinct colours only: the duplicates (the engine palette has ten) can't be nearer than their first.
    int distinct[PAL_ENTRIES];
    int n = 0;
    for (int i = 1; i < PAL_ENTRIES; i++)
    {
        int seen = 0;
        for (int k = 0; k < n; k++)
        {
            if (memcmp(&from[3 * distinct[k]], &from[3 * i], 3) == 0)
            {
                seen = 1;
                break;
            }
        }
        if (!seen)
            distinct[n++] = i;
    }
    // The nearest entry to the centre of each 2x2x2 cell of the 6-bit cube (a 32^3 search, an eighth of the work:
    // tens of milliseconds less on a level's first freeze or lightning), for every colour in the cell. Colours
    // that aren't an entry's are only ever approximated.
    const int half = COLOUR_TRANSFORM_TABLE_SIDE / 2;
    for (int r = 0; r < half; r++)
    {
        for (int g = 0; g < half; g++)
        {
            for (int b = 0; b < half; b++)
            {
                int best = distinct[0];
                int32_t best_d = INT32_MAX;
                for (int k = 0; k < n; k++)
                {
                    const unsigned char *p = &from[3 * distinct[k]];
                    // Cell centre (2r + 0.5, ...), in doubled units to stay in integers.
                    const int32_t dr = (4 * r + 1) - 2 * p[0], dg = (4 * g + 1) - 2 * p[1], db = (4 * b + 1) - 2 * p[2];
                    const int32_t d = dr * dr + dg * dg + db * db;
                    if (d < best_d)
                    {
                        best_d = d;
                        best = distinct[k];
                    }
                }
                for (int cr = 0; cr < 2; cr++)
                    for (int cg = 0; cg < 2; cg++)
                        for (int cb = 0; cb < 2; cb++)
                            out[((uint32_t)(2 * r + cr) << 12) | ((uint32_t)(2 * g + cg) << 6) | (uint32_t)(2 * b + cb)] = (uint8_t)best;
            }
        }
    }
    // An entry's own colour gets that entry (the first of its duplicates): exact where it matters.
    for (int k = n - 1; k >= 0; k--)
    {
        const unsigned char *p = &from[3 * distinct[k]];
        out[((uint32_t)(p[0] & 63) << 12) | ((uint32_t)(p[1] & 63) << 6) | (uint32_t)(p[2] & 63)] = (uint8_t)distinct[k];
    }
}

/** Whether entries 1..255 that share a colour in `from` share one in `to` too, and black stays black: the
 *  condition for any colour function to reproduce the swap. */
static int swap_is_colour_function(const unsigned char from[768], const unsigned char to[768])
{
    for (int i = 1; i < PAL_ENTRIES; i++)
    {
        const unsigned char *fi = &from[3 * i];
        const unsigned char *ti = &to[3 * i];
        // Texel 0 shows black, and transforms keep black: an entry that is black before must stay black.
        if (((fi[0] | fi[1] | fi[2]) == 0) && ((ti[0] | ti[1] | ti[2]) != 0))
            return 0;
        for (int j = 1; j < i; j++)
        {
            if ((memcmp(&from[3 * j], fi, 3) == 0) && (memcmp(&to[3 * j], ti, 3) != 0))
                return 0;
        }
    }
    return 1;
}

/** Per channel, the new 6-bit value of each old one, from entries 1..255; 0 if a channel's mapping isn't a
 *  function of that channel alone. known[c][v] is -1 where no entry has value v in channel c. */
static int channels_separable(const unsigned char from[768], const unsigned char to[768], int known[3][64])
{
    for (int c = 0; c < 3; c++)
        for (int v = 0; v < 64; v++)
            known[c][v] = -1;
    for (int i = 1; i < PAL_ENTRIES; i++)
    {
        for (int c = 0; c < 3; c++)
        {
            const int v = from[3 * i + c] & 63;
            const int w = to[3 * i + c] & 63;
            if ((known[c][v] >= 0) && (known[c][v] != w))
                return 0;
            known[c][v] = w;
        }
    }
    return 1;
}

/** An 8-bit curve through the points (chan6_to_8(v), chan6_to_8(known[v])): exact at those points, linear
 *  between them, flat beyond the first and last. */
static void build_curve(const int known[64], uint8_t curve[256])
{
    int xs[64], ys[64], n = 0;
    for (int v = 0; v < 64; v++)
    {
        if (known[v] < 0)
            continue;
        xs[n] = chan6_to_8((uint8_t)v);
        ys[n] = chan6_to_8((uint8_t)known[v]);
        n++;
    }
    if (n == 0)
    {
        for (int x = 0; x < 256; x++)
            curve[x] = (uint8_t)x;
        return;
    }
    int k = 0;
    for (int x = 0; x < 256; x++)
    {
        while ((k + 1 < n) && (xs[k + 1] <= x))
            k++;
        int y;
        if ((x <= xs[0]) || (k + 1 >= n))
            y = (x <= xs[0]) ? ys[0] : ys[n - 1];
        else if (x == xs[k])
            y = ys[k];
        else
            y = ys[k] + ((ys[k + 1] - ys[k]) * (x - xs[k]) + (xs[k + 1] - xs[k]) / 2) / (xs[k + 1] - xs[k]);
        curve[x] = (uint8_t)y;
    }
}

enum ColourTransformMode colour_transform_build(struct ColourTransform *ct, const unsigned char from[768],
    const unsigned char to[768], const uint8_t *nearest)
{
    int known[3][64];
    if (memcmp(from, to, 768) == 0)
        return ct->mode = CTM_Identity;
    if (!swap_is_colour_function(from, to))
        return ct->mode = CTM_Unsupported;
    if (channels_separable(from, to, known))
    {
        for (int c = 0; c < 3; c++)
            build_curve(known[c], ct->curves[c]);
        return ct->mode = CTM_Curves;
    }
    if ((ct->table == NULL) || (nearest == NULL))
        return ct->mode = CTM_Unsupported;
    for (uint32_t key = 0; key < COLOUR_TRANSFORM_TABLE_SIZE; key++)
    {
        const unsigned char *t = &to[3 * nearest[key]];
        ct->table[4 * key + 0] = chan6_to_8(t[0]);
        ct->table[4 * key + 1] = chan6_to_8(t[1]);
        ct->table[4 * key + 2] = chan6_to_8(t[2]);
        ct->table[4 * key + 3] = 255;
    }
    // An entry's own colour gives its own new colour: its nearest entry has that colour, and entries sharing an
    // old colour share their new one (swap_is_colour_function()).
    return ct->mode = CTM_Table;
}
/******************************************************************************/
