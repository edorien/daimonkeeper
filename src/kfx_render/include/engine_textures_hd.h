/******************************************************************************/
// Free implementation of the Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_textures_hd.h
 *     True-colour HD terrain textures: a render-only store beside block_mem.
 * @par Purpose:
 *     docs/newFeatures/texture-resolution/02-design.md sections 3 and 5.3. Each loaded
 *     32x32 texture block may have an HD version (64 or 128 texels a side, RGBA, in
 *     the colours of the reference palette, data/palette.dat): found by the block's
 *     own content (a 64-bit hash of its 1024 indices), so it never attaches to art it
 *     wasn't made from. The 32x32 atlas (block_mem, block_ptrs) stays as it is and is
 *     the fallback for every block without HD, and for every frame the HD colours
 *     can't follow the active palette in (RendererHdUsable()).
 *     Nothing here is saved, resynced or simulated.
 *     HD comes from packs in FXGraphics' layout (02 section 3.2: pack.toml, filelist_tmapXNNN.txt,
 *     the sheets, keys/tmapXNNN.idx): first a content folder beside the slot's tmap file
 *     (<its folder>/textures-hd), then the user pack, fxdata/asset_packs/<ASSET_PACK>/textures.
 *     Within a pack, a key with several HD versions gives the slot the one made for its own
 *     (stem, index), else the first in pack order.
 * @par Comment:
 *     KFX_TEXTURE_HD_SYNTH=1|2 (a debug switch) builds the store from the loaded 32x32
 *     blocks themselves, as x2 / x4 nearest-neighbour upscales drawn in parity mode:
 *     the game must then draw exactly what it draws without HD (03 section B). With
 *     KFX_TEXTURE_HD_PRECISION=1 too it draws them in precision mode instead: the
 *     negative control, which must differ (by a texel at texel boundaries).
 * @author   KeeperFX Team
 * @date     09 Oct 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_ENGINE_TEXTURES_HD_H
#define DK_ENGINE_TEXTURES_HD_H

#include <stddef.h>
#include <stdint.h>
#include "bflib_basics.h"
#include "bflib_video.h"
#include "kfx_config_state.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
enum TextureHdSource {
    TexHdSrc_None = 0,      ///< drawn from the 32x32 block
    TexHdSrc_Synthetic,     ///< KFX_TEXTURE_HD_SYNTH
    TexHdSrc_ContentFolder, ///< textures-hd/ beside the slot's tmap file
    TexHdSrc_UserPack,      ///< fxdata/asset_packs/<ASSET_PACK>/textures/
    TexHdSrc_Generated,     ///< ASSET_PACK_FILL: made from the 32x32 block (engine_hd_generate.h)
};

#define TEXTURE_HD_LOW_LEVELS 5 ///< mip levels below 32x32: 16, 8, 4, 2, 1

/** One block's HD version and its mip levels, each the top-left texel of a block in a page 256 texels wide;
 *  page NULL: none. level[s] is 32 << s texels a side: level[shift] == page, the levels between are box-filtered
 *  from it; level[0] is NULL when the 32x32 level is the original indexed block (use_original_at_32, the
 *  default). low[k] (16 >> k a side) exist only with TEXTURE_MIPMAPS and level[0]. */
struct TextureHd {
    const TbPixel *page;
    uint8_t shift;  ///< 32 << shift texels a side
    uint8_t source; ///< enum TextureHdSource
    const TbPixel *level[4];
    const TbPixel *low[TEXTURE_HD_LOW_LEVELS];
};

/** What to draw a block with: an RGBA block (rgba) or an indexed one (indexed), 32 << shift texels a side
 *  (shift -5..3; negative: a mip level below 32x32), in parity mode or not (vec_map_* in bflib_vidraw.h). */
struct TextureHdDraw {
    const unsigned char *indexed;
    const TbPixel *rgba;
    int64_t shift;
    TbBool parity;
    TbBool original_at_32; ///< with rgba: the block's 32x32 level is the original indexed one
};

/** One block line of a filelist: its sheet (relative to the pack) and crop rectangle. */
struct TextureHdFilelistLine {
    char sheet[128];
    int32_t x, y, w, h;
};

struct TextureHdStats {
    uint64_t blocks;          ///< distinct HD blocks in the store
    uint64_t slots_with_hd;   ///< (variation, static block) slots that got one
    uint64_t slots_hashed;    ///< non-uniform 32x32 blocks looked up
    uint64_t bytes;           ///< memory of the pages
    int64_t load_us;          ///< time the last load took
    uint64_t draws;           ///< block draws that used an HD version, since the load
    uint64_t sheets;          ///< pack sheets decoded
    uint64_t slots_capped;    ///< slots a pack had HD for, left 32x32 by the memory cap
    uint64_t low_bytes;       ///< memory of the mip levels below 32x32 (indexed and RGBA)
};

/** Parallel to block_ptrs: the HD version of each (variation, block id), animated ids included. */
extern struct TextureHd block_hd[TEXTURE_VARIATIONS_COUNT * TEXTURE_BLOCKS_COUNT];

/** Builds the store for the 32x32 blocks just loaded into block_mem (load_texture_map_file()). */
void texture_hd_load(void);
/** Empties the store and frees its memory. */
void texture_hd_clear(void);
/** Loads the store again for the blocks in block_mem (ASSET_PACK or the memory cap changed), and drops the GPU's copies.
 *  In engine_textures.c, with texture_hd_sync_animation(): they need its animation counter. */
void texture_hd_reload(void);
/** The animated ids' HD versions, for the frame each shows now. */
void texture_hd_sync_animation(void);
/** Animation: block id `dst` shows the frame `src` shows (update_animating_texture_maps()'s lockstep). */
void texture_hd_copy(int64_t dst, int64_t src);

/** Once per drawn frame, before its terrain: whether HD draws in this frame (a store, and colours that
 *  follow the active palette). Reloads when ASSET_PACK has changed, and finishes a load the memory cap
 *  held back. */
void texture_hd_frame_begin(void);
/** The HD version to draw a block id with this frame, or NULL; *shift its size. */
const TbPixel *texture_hd_block(int64_t block, int64_t *shift);
/** What to draw a block id with at a level of detail: lod is the wanted size, 32 << lod texels a side (the
 *  one whose texel is nearest a pixel, texture_hd_lod_for_texel()). It gets the nearest level there is: HD
 *  levels above 32 (HD drawing this frame), the original block or the pack's own 32x32 level at 0, mips below 32
 *  with TEXTURE_MIPMAPS. */
void texture_hd_select(int64_t block, int lod, struct TextureHdDraw *out);
/** The level of detail for a block texel (32-res) covering texel_px pixels on screen: round(log2), in -5..3,
 *  never below 0 without TEXTURE_MIPMAPS. */
int texture_hd_lod_for_texel(double texel_px);
/** Whether the mip levels below 32x32 were built (TEXTURE_MIPMAPS when the store was loaded). */
TbBool texture_hd_low_levels(void);
/** The UV precision HD asks of the terrain this frame, as a shift: the finest HD block's (1..3) while HD draws in
 *  precision mode, else 0 (no HD, or parity mode: UVs as for the 32x32 blocks). The rough-terrain inset is half a
 *  texel at this size, and the liquids scroll in fractions of a texel (02-design.md sections 4.2, 4.3). */
int texture_hd_uv_shift(void);
/** Whether a level of detail can change what is drawn this frame (HD drawing, or levels below 32): else
 *  texture_hd_select() gives the 32x32 block whatever the level. */
TbBool texture_hd_lod_matters(void);
/** Whether HD blocks are drawn in parity mode (the synthetic store). */
TbBool texture_hd_parity(void);

/** The 64-bit content key of a 32x32 block in the 256-wide atlas (FNV-1a over its 1024 indices, row by row). */
uint64_t texture_hd_block_key(const unsigned char *block);

const struct TextureHdStats *texture_hd_stats(void);
/** Where a slot's HD version came from (a debug query: tests, ftests). */
enum TextureHdSource texture_hd_slot_source(int64_t block);
/** The memory the packs may take (tests); 0: a quarter of ASSET_PACK_MEMORY (asset_pack_memory_budget()), followed
 *  live. Over it, only the variations the level uses get HD. */
void texture_hd_set_memory_cap(uint64_t bytes);
/** Parses a filelist (BOM, header line, then "sheet x y w h" lines; tabs or spaces): its block lines, at most
 *  max_lines, returning how many; *header_size the header's block size (0 if not square). */
int64_t texture_hd_parse_filelist(const char *text, size_t len, struct TextureHdFilelistLine *lines, int64_t max_lines,
    int64_t *header_size);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
