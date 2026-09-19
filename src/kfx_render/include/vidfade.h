/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file vidfade.h
 *     Header file for vidfade.c.
 * @par Purpose:
 *     Video fading routines.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     16 Jul 2010 - 05 Nov 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_VIDFADE_H
#define DK_VIDFADE_H

#include "bflib_basics.h"
#include "globals.h"
#include "bflib_video.h"
#include "bflib_netsp.h" // NetUserId

#ifdef __cplusplus
extern "C" {
#endif

#define COLOUR_TABLE_BITS_PER_VALUE 4
#define COLOUR_TABLE_DIMENSION (1<<COLOUR_TABLE_BITS_PER_VALUE)

/******************************************************************************/
#pragma pack(1)

struct Thing;
struct TbColorTables;
struct TbAlphaTables;
struct PlayerInfo;
typedef unsigned char TbRGBColorTable[COLOUR_TABLE_DIMENSION][COLOUR_TABLE_DIMENSION][COLOUR_TABLE_DIMENSION];

/** Reverse RGB -> palette-index lookup via the precomputed quantized table
 * (16x16x16 buckets over the palette's own 6-bit VGA space) -- O(1) instead
 * of LbPaletteFindColour()'s O(256) linear scan (with a second full pass for
 * tie-breaking). Use for per-pixel reverse lookups against the game's own
 * tracked palette; ctab must already be populated for that palette (see
 * compute_rgb2idx_table()/init_colours()) and colour is assumed already
 * expanded to true colour (e.g. read back off the true-colour framebuffer),
 * not a raw palette index. */
static inline unsigned char TbRGBColorTable_Lookup(const TbRGBColorTable ctab, TbPixel colour)
{
    const int scaler = (1 << 6) / COLOUR_TABLE_DIMENSION;
    return ctab[chan8_to_6(colour.r) / scaler][chan8_to_6(colour.g) / scaler][chan8_to_6(colour.b) / scaler];
}

/******************************************************************************/
extern unsigned char frontend_palette[768];
extern unsigned char palette_buf[PALETTE_SIZE];
/* colours (TbRGBColorTable) moved to kfx_sim_state.h (stage 13.3,
   docs/refactor/stage-13-enforce-and-document.md) -- see kfx_sim_state's
   comment for consumer-set rationale. */

#pragma pack()
/******************************************************************************/
// fade_in()/fade_out()/ProperFadePalette() removed per
// docs/refactor/renderer/05-imgui-owned-menu-backdrop.md Phase 0 -- the
// between-screens fade they drove is no longer needed.
void compute_fade_tables(struct TbColorTables *coltbl,unsigned char *spal,unsigned char *dpal);
void ProperForcedFadePalette(unsigned char *pal, long n, enum TbPaletteFadeFlag flg);

void compute_alpha_tables(struct TbAlphaTables *alphtbls,unsigned char *spal,unsigned char *dpal);
void compute_rgb2idx_table(TbRGBColorTable ctab,unsigned char *spal);


long PaletteFadePlayer(struct PlayerInfo *player);
void PaletteApplyPainToPlayer(struct PlayerInfo *player, long intense);

void PaletteSetUserPalette(NetUserId user, unsigned char *pal);
TbBool set_gamma(char corrlvl, TbBool do_set);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
