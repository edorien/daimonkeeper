/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_render.h
 *     Header file for bflib_render.c and bflib_render_*.c.
 * @par Purpose:
 *     Rendering the 3D view functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     20 Mar 2009 - 30 Mar 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef BFLIB_REND_H
#define BFLIB_REND_H

#include "bflib_basics.h"
#include "globals.h"
#include "bflib_video.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

enum VecModes {
    VM_FlatColor = 0,
    VM_Unusedparam1,
    VM_TriangularGouraud,
    VM_TriangularTexture,
    VM_QuadFlatColor,
    VM_QuadTextured,
    VM_TriangularTextured,
    VM_SolidColor,
    VM_Unusedparam8,
    VM_Unusedparam9,
    VM_SpriteTranslucent,
    VM_Unusedparam11,
    VM_Unusedparam12,
    VM_Unusedparam13,
    VM_Unusedparam14,
    VM_Unusedparam15,
    VM_Unusedparam16,
    VM_Unusedparam17,
    VM_Unusedparam18,
    VM_Unusedparam19,
    VM_Unusedparam20,
    VM_Unusedparam21,
    VM_Unusedparam22,
    VM_Unusedparam23,
    VM_Unusedparam24,
    VM_Unusedparam25,
    VM_Unusedparam26,
    VM_Unusedparam27,
};


// These are used "per screen row"
struct PolyPoint {
    int64_t X; // Horizontal coordinate within screen buffer
    int64_t Y; // Vertical coordinate within screen buffer
    int64_t U; // Texture UV mapping, U coordinate
    int64_t V; // Texture UV mapping, V coordinate
    int64_t S; // Shininess / brightness of the point
};

struct GtBlock { // sizeof = 48
  unsigned char *texturedata;
  uint64_t width;
  uint64_t height;
  uint64_t lightness0;
  uint64_t lightness1;
  uint64_t lightness3;
  uint64_t lightness2;
  uint64_t texturestride;
  uint64_t scalingfactor;
  uint64_t colorformat;
  uint64_t renderflags;
  uint64_t textureoffset;
};

/******************************************************************************/

#pragma pack()
/******************************************************************************/
/* vec_colour: for VecModes that resolve to a genuine flat COLOUR
 * (VM_SolidColor, VM_FlatColor, VM_QuadFlatColor) -- the pre-migration code
 * always used this as a raw palette-index byte for these modes (confirmed by
 * tracing every write site: a shade average, a distance value, and a direct
 * `->colour` field access all fed the exact same `*dst = vec_colour`-shaped
 * consumption in bflib_render_trig.c), so every writer now resolves through
 * expand_indexed_pixel() once, here, rather than the consumer re-deriving a
 * colour from a raw int at render time. */
extern TbPixel vec_colour;
/* vec_shade: for VecModes that need a raw shade/intensity value instead of a
 * resolved colour: VM_SpriteTranslucent (creature shadows, shading whatever
 * is already on screen -- trig_render_md10()) and, despite its name,
 * VM_SolidColor too (trig_render_md07() -- confirmed by tracing the
 * original's `vec_colour<<8 | texel` bit-packing: it samples vec_map and
 * shades the sample by this value, it is not a flat single colour). Both
 * combine a base pixel with a translucency/distance-based intensity via
 * render_shade(), the same shading formula draw_gpoly_line() uses. 0-63
 * nominal range, same as fade_tables' shade axis -- see
 * docs/refactor/renderer/02a-pixel-format-design.md §2.1. Kept as a
 * separate global from vec_colour rather than overloading one TbPixel-typed
 * variable for two incompatible purposes. */
extern int64_t vec_shade;
extern unsigned char vec_mode;
extern struct PolyPoint *polyscans;

/**
 * Shading/lighting blend -- replaces the old `render_fade_tables[(shade<<8)|
 * palette_index]` lookup. `shade` is 0-63 (both segments -- 0-31 linear
 * darken, 32-63 non-linear brighten-beyond-source -- are reachable at
 * runtime; see docs/refactor/renderer/02a-pixel-format-design.md §2.1 for
 * the full derivation from compute_fade_tables()). `sample` is the true-
 * colour pixel already expanded from its source palette index via
 * expand_indexed_pixel() (bflib_video.h) -- this function does the shade
 * math only, it does not itself touch a palette.
 */
static inline TbPixel render_shade(TbPixel sample, int64_t shade)
{
    /* factor(shade), scaled by 32 to stay in integer arithmetic:
     *   shade in 0..31:  factor*32 = shade          (linear darken)
     *   shade in 32..63: factor*32 = 3*shade - 64    (non-linear brighten) */
    const int64_t factor_x32 = (shade <= 31) ? shade : (3 * shade - 64);
    return TbPixel_RGBA(
        (uint8_t)clamp((sample.r * factor_x32) / 32, 0, 255),
        (uint8_t)clamp((sample.g * factor_x32) / 32, 0, 255),
        (uint8_t)clamp((sample.b * factor_x32) / 32, 0, 255),
        sample.a);
}

/**
 * Ghost/invisible-creature blend -- replaces the old `render_ghost[ref<<8 |
 * dest]` lookup (`compute_fade_tables`'s second table): `output = (ref +
 * 2*dest) / 3`, `ref` weighted 1/3, `dest` (the pixel already on screen)
 * weighted 2/3. See 02a-pixel-format-design.md §2.2.
 */
static inline TbPixel render_ghost_blend(TbPixel ref, TbPixel dest)
{
    return TbPixel_RGBA(
        (uint8_t)((ref.r + 2 * dest.r) / 3),
        (uint8_t)((ref.g + 2 * dest.g) / 3),
        (uint8_t)((ref.b + 2 * dest.b) / 3),
        255);
}

/**
 * Ghost blend, reversed weighting -- `output = (2*ref + dest) / 3`, `ref`
 * weighted 2/3 instead of render_ghost_blend()'s 1/3. Replaces the old
 * `render_ghost[dest<<8 | ref]` lookup (same table as render_ghost_blend(),
 * indexed in the opposite byte order -- see 02a-pixel-format-design.md §2.2
 * and bflib_vidraw_spr_onec.c's ghost_blend_1/ghost_blend_2 for the
 * bit-packing trace that distinguishes the two).
 */
static inline TbPixel render_ghost_blend_2(TbPixel ref, TbPixel dest)
{
    return TbPixel_RGBA(
        (uint8_t)((2 * ref.r + dest.r) / 3),
        (uint8_t)((2 * ref.g + dest.g) / 3),
        (uint8_t)((2 * ref.b + dest.b) / 3),
        255);
}

/**
 * Alpha-sprite blend -- replaces the old `render_alpha[texel<<8 | dest]`
 * lookup (`compute_alpha_table(s)`, vidfade.c). CORRECTION to the original
 * pixel-format design pass: this is not a simple (ref, dest) blend the way
 * ghost is. `struct TbAlphaTables` concatenates 9 named ramps as one flat
 * 256-row table (`vidmode.h:123-135`): row 0 = `void_black` (1 row), rows
 * 1-8 = `white`, 9-16 = `yellow`, 17-24 = `red`, 25-32 = `blue`, 33-40 =
 * `green`, 41-48 = `purple`, 49-56 = `black`, 57-64 = `orange` (8 rows each,
 * one per intensity step), rows 65-255 = `unused` padding. `texel` (the
 * *sprite's own* palette-index byte -- not a fixed colour parameter, unlike
 * ghost) selects which (ramp, intensity) row; `dest` (the pixel already on
 * screen) is the colour that gets additively shifted by that ramp's step.
 *
 * Also corrects an earlier "confirmed dead" finding: `void_black` (row 0)
 * is never written by compute_alpha_tables() at all -- its 144-flat-fill
 * loop (vidfade.c:156-159) targets `black`, not `void_black` (a probable
 * copy-paste slip in the original, left as-is per this migration's
 * preserve-legacy-behaviour default) -- so `void_black`/`unused` are BSS-
 * zero in every generated table, not "dead" in the sense of unreachable;
 * they're reachable and always resolve to palette index 0. Preserved here
 * as a flat near-black fallback for texel 0 and 65-255, matching that
 * observed behaviour, since no sprite asset data is available to confirm
 * whether real EngineSpriteDrawUsingAlpha sprites ever emit those texel
 * values in practice. See docs/refactor/renderer/02b-legacy-bugs-found.md.
 */
static inline TbPixel render_alpha_blend(uint8_t texel, TbPixel dest)
{
    static const int8_t ramp_deltas[8][3] = {
        {  4,  4,  4 }, /* white  */
        {  6,  4,  0 }, /* yellow */
        {  6,  1,  1 }, /* red    */
        {  2,  2,  6 }, /* blue   */
        {  2,  6,  2 }, /* green  */
        {  3,  0,  3 }, /* purple */
        { -2, -2, -2 }, /* black  */
        {  6,  3,  1 }, /* orange */
    };
    if (texel < 1 || texel > 64)
        return TbPixel_RGBA(0, 0, 0, dest.a); /* void_black/unused: always index 0 */
    const int64_t ramp = (texel - 1) / 8;
    const int64_t step = (texel - 1) % 8;
    /* Deltas are in the original's 6-bit VGA scale; scale to 0-255 to match
     * a true dest.r/g/b, same conversion chan6_to_8() uses elsewhere. */
    const int64_t dr = (ramp_deltas[ramp][0] * step * 255) / 63;
    const int64_t dg = (ramp_deltas[ramp][1] * step * 255) / 63;
    const int64_t db = (ramp_deltas[ramp][2] * step * 255) / 63;
    return TbPixel_RGBA(
        (uint8_t)clamp(dest.r + dr, 0, 255),
        (uint8_t)clamp(dest.g + dg, 0, 255),
        (uint8_t)clamp(dest.b + db, 0, 255),
        dest.a);
}
/**
 * Flash-blend remap -- replaces the old `white_pal[i]`/`red_pal[i]` lookups
 * (`compute_shifted_palette_table`, vidfade.c:206-222): a single additive
 * channel shift, no per-destination-pixel dependency (unlike ghost/alpha).
 * `shiftR/G/B` are in the original's VGA-6 scale (e.g. white_pal's
 * (+48,+48,+48), red_pal's (+20,-10,-10) -- see
 * docs/refactor/renderer/02a-pixel-format-design.md §2.4), scaled to 0-255
 * the same way §2.3's alpha-ramp deltas are.
 */
static inline TbPixel render_flash_blend(TbPixel sample, int64_t shiftR, int64_t shiftG, int64_t shiftB)
{
    const int64_t dr = (shiftR * 255) / 63;
    const int64_t dg = (shiftG * 255) / 63;
    const int64_t db = (shiftB * 255) / 63;
    return TbPixel_RGBA(
        (uint8_t)clamp(sample.r + dr, 0, 255),
        (uint8_t)clamp(sample.g + dg, 0, 255),
        (uint8_t)clamp(sample.b + db, 0, 255),
        sample.a);
}
/******************************************************************************/
void draw_gpoly(struct PolyPoint *point_a, struct PolyPoint *point_b, struct PolyPoint *point_c);
/******************************************************************************/
void gtblock_set_clipping_window(unsigned char *screen_addr, int64_t clip_width, int64_t clip_height, int64_t screen_width);
/******************************************************************************/
void trig(struct PolyPoint *point_a, struct PolyPoint *point_b, struct PolyPoint *point_c);
/******************************************************************************/
void setup_bflib_render();
void reset_bflib_render();
void finish_bflib_render();

#ifdef __cplusplus
}
#endif
#endif
