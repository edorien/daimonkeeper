/******************************************************************************/
// Free implementation of the Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_sprites_hd.h
 *     HD sprite packs: the content key of a sprite frame, and the sprite draw audit.
 * @par Purpose:
 *     docs/refactor/sprites/ (git-ignored plan), Phase 0. An HD frame will attach to an original sprite
 *     frame by the frame's content: sprite_hd_frame_key(), a 64-bit hash of the decoded frame, the same
 *     function scripts/spritepack.py computes. The audit measures how big sprites are drawn (destination
 *     pixels per source texel), for choosing the pack scales (ftest sprite_density).
 *     Nothing here is saved, resynced or simulated.
 * @author   KeeperFX Team
 * @date     10 Oct 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_ENGINE_SPRITES_HD_H
#define DK_ENGINE_SPRITES_HD_H

#include <stddef.h>
#include <stdint.h>
#include "bflib_basics.h"
#include "terrain_audit.h"
#include "engine_sprites_hd_pack.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/** Widest frame sprite_hd_frame_key() takes (originals are at most 174, custom sprites under 255). */
#define SPRITE_HD_KEY_MAX_WIDTH 1024

/** The content key of a sprite frame: FNV-1a 64 over its width and height (two bytes each, low byte first),
 *  then its width x height pixels row by row, each the palette index the frame's run-length data gives it
 *  and 0 where the frame is transparent (a literal index 0 draws transparent too, so it hashes alike).
 *  `rle` is the frame's rows (n > 0: n index bytes follow; n < 0: skip -n; 0: end of row); at most rle_len
 *  bytes of it are read. 0 for an empty, too wide or truncated frame (0 means "none" in pack key files). */
uint64_t sprite_hd_frame_key(const unsigned char *rle, size_t rle_len, int64_t width, int64_t height);

/** Writes the key of every original frame (creature.tab/.jty) and every loaded custom frame to `path`, one
 *  line each: "orig <table index> <width> <height> <key>" and "add <slot> <width> <height> <key>" (key in
 *  16 hex digits). For checking scripts/spritepack.py against the engine. Returns the frames written, or -1
 *  when the file can't be written or creature.jty isn't open. */
int64_t sprite_hd_dump_keys(const char *path);

/** HD sprite frames (docs/refactor/sprites/02-design.md section 4). Packs: the sprites/ folder of the asset pack
 *  ASSET_PACK names, fxdata/asset_packs/<pack>/sprites/ (engine_sprites_hd_pack.h's layout); its frames attach to the originals by content key, are decoded
 *  in the background (the drawn ones first; the original draws until then), and the least recently drawn dropped over
 *  the memory cap. KFX_SPRITE_HD_SYNC=1 decodes a pack whole before the next frame (frame hashes); KFX_SPRITE_HD_PARITY=1
 *  draws a pack in parity mode (spritepack.py upscale's pack must then draw exactly what the game draws without HD).
 *  The synthetic store, for tests --
 *  KFX_SPRITE_HD_SYNTH=1|2 makes every original and custom frame, as it is first drawn, an x2 / x4 nearest-neighbour
 *  upscale of itself in the reference palette's colours, drawn in parity mode (the game must then draw exactly what
 *  it draws without HD), or with KFX_SPRITE_HD_PRECISION=1 in precision mode. With KFX_SPRITE_HD_SYNTH_PATTERN=1
 *  every texel but the one parity mode samples is inverted: parity must still draw the originals, precision mustn't
 *  (the negative control; without the pattern, precision draws the originals too: the art is the originals). */
struct SpriteHdStats {
    uint64_t frames;   ///< HD frames built (synthetic) or decoded (packs)
    uint64_t bytes;    ///< their texels' memory, all levels
    uint64_t draws;    ///< sprite draws that used an HD frame
    uint64_t zip_draws;  ///< sprite draws in a zip frame's true colours (counted in draws too)
    uint64_t alpha_draws; ///< alpha-ramp draws with additive art (counted in draws too)
    uint64_t zip_frames; ///< custom slots with their zip PNG's true colours (SPRITE_TRUE_COLOUR)
    uint64_t zip_bytes;  ///< their memory
    uint64_t entries;  ///< pack entries indexed
    uint64_t failed;   ///< pack entries whose art couldn't be used
    uint64_t evicted;  ///< decoded frames dropped over the memory cap
    uint64_t generated; ///< frames given generated art (ASSET_PACK_FILL), colour and additive
    int64_t load_us;   ///< KFX_SPRITE_HD_SYNC: the whole pack's load
    int64_t decode_us; ///< decoding, summed over the workers
};

/** Once per drawn frame, before its things: whether HD sprites draw in this frame (a store, and colours that follow
 *  the active palette, RendererHdUsable()). */
void sprite_hd_frame_begin(void);
/** Whether HD sprites draw in this frame (sprite_hd_frame_begin()'s finding). */
TbBool sprite_hd_drawing_frame(void);
/** The HD version to draw the original frame kspr_idx (a creature_table index, or KEEPERSPRITE_ADD_OFFSET + a custom
 *  slot) with, at `scale` (the sprite's x32 scale, 32 = one pixel a texel): fills `out` and returns true, or false
 *  to draw the original (no HD frame; in precision mode, a level of detail of one texel a pixel or less, which the
 *  original is). `rle` and `width` x `full_height` are the original's; `height` is how much of it is drawn (the
 *  water cutoff). */
struct TbHdSpriteSource;
/** As sprite_hd_source(), for an alpha-ramp draw (EngineSpriteDrawUsingAlpha): the frame's additive art (a pack
 *  entry whose filelist is "additive", or the synthetic store's ramp deltas), never colour art. */
TbBool sprite_hd_alpha_source(int64_t kspr_idx, const unsigned char *rle, int64_t width, int64_t full_height, int64_t height,
    int64_t scale, struct TbHdSpriteSource *out);
TbBool sprite_hd_source(int64_t kspr_idx, const unsigned char *rle, int64_t width, int64_t full_height, int64_t height,
    int64_t scale, struct TbHdSpriteSource *out);
/** The level's HD content folders (sprites-hd/ beside its sprite zips: init_custom_sprites()), in the order their
 *  zips load: a later folder's art wins a key, and any folder's wins over the user pack (ASSET_PACK). The packs
 *  are opened again at the next frame if the list changed; frames decoded from unchanged art are kept. */
void sprite_hd_set_content_folders(const char *const *dirs, int64_t count);
/** The content folders as last set, in load order (tests). */
int64_t sprite_hd_content_folder_count(void);
const char *sprite_hd_content_folder(int64_t i);
/** A custom slot was filled from a zip PNG (custom_sprites.c read_png_data(), after compress_raw()): its key from the
 *  quantised frame, where it came from (to read it again), and with SPRITE_TRUE_COLOUR on its RGBA (w x h, alpha below
 *  0x40 transparent as compress_raw() reads it), drawn instead of the quantised frame unless a pack has the key. */
void sprite_hd_zip_frame_loaded(int64_t slot, const unsigned char *rgba, int64_t w, int64_t h, const char *zip, const char *png);
/** The custom slots are emptied (init_custom_sprites()): their zip records and true colours go. */
void sprite_hd_zip_frames_clear(void);
/** A custom slot's zip frame record (tests, tools): its key, zip and PNG, or false if it has none. */
TbBool sprite_hd_zip_frame(int64_t slot, uint64_t *key, const char **zip, const char **png);
/** A custom slot's true colours, w x h of its frame (tests), or NULL. */
const TbPixel *sprite_hd_zip_true_colour(int64_t slot);
/** A frame drawn with alpha ramps (EngineSpriteDrawUsingAlpha): with KFX_SPRITE_ALPHA_LOG=<file>, each frame's index
 *  is appended to that file the first time (which frames additive HD art is for, docs/refactor/sprites/ Phase 9). */
void sprite_hd_note_alpha_frame(int64_t kspr_idx);
/** Whether creature shadows are soft (Phase 10): SHADOW_STYLE is SOFT, not in parity mode (which draws exactly the
 *  originals) nor with the synthetic store. Their shape is the HD art's alpha where a pack has the frame, else the
 *  original's silhouette. */
TbBool sprite_hd_soft_shadows(void);
/** A frame's HD art for its soft shadow: the finest level's texels of the original's crop (stride texels a row, scale
 *  texels per original texel, `id` naming them -- never reused for other texels), or false (draw the shadow from the
 *  original's silhouette). */
TbBool sprite_hd_shadow_art(int64_t kspr_idx, const unsigned char *rle, int64_t width, int64_t full_height, const TbPixel **texels,
    int64_t *stride, int64_t *scale, uint64_t *id);
/** A soft shadow's HD texels for the GPU (docs/plans/sprite-atlas-thrash.md, Phase 2): the same texels and id a sprite
 *  draw of frame kspr_idx at `scale` (global_scaler: 32 = one texel a pixel) takes, so the shadow shares its atlas
 *  entry. False when there are none (no pack frame, hard shadows, or the original is nearer than every level). */
struct TbHdSpriteSource;
TbBool sprite_hd_shadow_source(int64_t kspr_idx, const unsigned char *rle, int64_t width, int64_t full_height, int64_t scale,
    struct TbHdSpriteSource *out);
/** The custom sprites changed (keeper_sprites_publish_custom_frames()): their HD frames are dropped. */
void sprite_hd_forget_custom(void);
const struct SpriteHdStats *sprite_hd_stats(void);
/** The memory decoded pack frames may take (tests); 0: ASSET_PACK_MEMORY less what the terrain textures took, at
 *  least half of it, followed live (the Vulkan HD sprite atlas follows it too). */
void sprite_hd_set_memory_cap(uint64_t bytes);
/** ASSET_PACK_MEMORY in bytes on this computer (asset_pack_memory_bytes() with SDL's figure for its memory). */
uint64_t asset_pack_memory_budget(void);
/** Whether HD sprites draw in parity mode. */
TbBool sprite_hd_parity(void);

/** The sprite draw audit: per drawn frame, how many destination pixels each source texel of a thing sprite
 *  covers (along its more magnified axis), weighted by the sprite's area inside the graphics window, in the
 *  terrain audit's half-octave bins (TERRAIN_TEXEL_BINS; terrain_texel_percentile() reads them). */
enum SpriteAuditKind {
    SprAud_Solid = 0, ///< opaque, the palette's colours
    SprAud_Remap,     ///< opaque through a remap table: shade, tint or flash
    SprAud_Ghost,     ///< TRANSPAR4 / TRANSPAR8
    SprAud_Alpha,     ///< alpha ramps (EngineSpriteDrawUsingAlpha)
    SprAud_KindCount,
};

struct SpriteAuditStats {
    uint64_t frame;            ///< audited frames so far, this one included
    uint64_t gameturn;         ///< game turn the frame began on
    uint64_t draws;            ///< thing sprite draws
    uint64_t flipped;          ///< ... of them drawn mirrored
    uint64_t kind[SprAud_KindCount]; ///< ... by how they were drawn
    double area;               ///< their area inside the graphics window, pixels
    double mag_max;            ///< the largest magnification drawn
    double mag_hist[TERRAIN_TEXEL_BINS]; ///< area by destination pixels per source texel
};

void sprite_audit_set_enabled(int enabled);
int sprite_audit_is_enabled(void);
/** A frame starts: what was drawn since the last start becomes sprite_audit_last() (the things in the power
 *  hand, drawn after the world, count with the frame before). */
void sprite_audit_frame_begin(uint64_t gameturn);
/** One thing sprite drawn: src_w x src_h source texels onto dst_w x dst_h pixels at (x, y), graphics-window
 *  coordinates. Reads the draw flags for its kind. */
void sprite_audit_draw(int64_t x, int64_t y, int64_t src_w, int64_t src_h, int64_t dst_w, int64_t dst_h);
/** The bin of a magnification (destination pixels per source texel). */
int sprite_audit_bin(double magnification);
/** Stats of the last finished frame (all zero before the first). */
const struct SpriteAuditStats *sprite_audit_last(void);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
