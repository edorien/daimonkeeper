/******************************************************************************/
// Free implementation of the Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_sprites_hd_pack.h
 *     HD sprite packs: the pack index and the frames' decoding (internal to engine_sprites_hd.c).
 * @par Purpose:
 *     docs/refactor/sprites/02-design.md sections 2 and 4. A pack is a folder in FXGraphics' sprites-* layout:
 *     pack.toml (name, scale 2..4, use_original_at_1x), animlist.txt (filelists, any subset), each filelist a header
 *     line then one canvas PNG a line, and keys/animlist.idx (16 bytes a frame line, in animlist order: the 64-bit
 *     content key of the original frame the art was made from, 0 for none; its crop on its canvas, x, y, w, h, u16
 *     each). Packs are opened in priority order (content folders, then the user's); a key the index already has
 *     keeps its first entry. Each entry's art is decoded on worker threads, the drawn ones first; the results are
 *     published on the main thread (sprite_pack_publish()), and the least recently drawn freed over a cap.
 * @author   KeeperFX Team
 * @date     11 Oct 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_ENGINE_SPRITES_HD_PACK_H
#define DK_ENGINE_SPRITES_HD_PACK_H

#include <stddef.h>
#include <stdint.h>
#include "bflib_basics.h"
#include "bflib_video.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define SPRITE_HD_MAX_LEVELS 4
/** The most art past the original's crop a frame keeps, in original texels a side (02-design.md section 3). */
#define SPRITE_HD_MAX_SPILL 8

/** A frame's HD texels: level k is level_scale[k] texels per original texel (finest first), the original crop's
 *  w x h texels widened by `spill` times that. */
struct SpriteHdLevels {
    TbPixel *level[SPRITE_HD_MAX_LEVELS];
    int64_t level_scale[SPRITE_HD_MAX_LEVELS];
    int levels;
    uint64_t bytes;
    int64_t spill[4]; ///< art past the crop, whole original texels: left, top, right, bottom (packs; set after the levels)
    TbPixel *sub[SPRITE_HD_MAX_LEVELS]; ///< additive art (alpha-ramp draws): what each level subtracts (NULL: nothing)
};

enum SpritePackEntryState {
    SpE_Idle = 0,  ///< not decoded (or evicted)
    SpE_Queued,
    SpE_Decoding,
    SpE_Ready,     ///< decoded, not yet published
    SpE_Built,     ///< lv usable on the main thread
    SpE_Failed,    ///< the PNG can't be read, or its size doesn't fit the crop
};

struct SpritePackEntry {
    uint64_t key;
    uint16_t crop_x, crop_y, w, h; ///< the original's crop on its canvas, in original texels
    uint16_t source;               ///< sprite_pack_source(); SPRITE_PACK_GENERATED: generated art
    uint8_t additive;              ///< additive art, for alpha-ramp draws only ("additive" on its filelist's header)
    uint32_t png;                  ///< the PNG's path in the pack, an offset in the path pool (not for generated art)
    TbPixel *gen;                  ///< generated art: the original's colours, w x h (additive: then what it subtracts)
    int32_t state;                 ///< enum SpritePackEntryState; Queued/Decoding/Ready change under the queue lock
    uint64_t uid;                  ///< set when built: never reused (the GPU caches the texels under it)
    uint64_t last_used;            ///< the frame it was last drawn in
    struct SpriteHdLevels lv;      ///< Built: the texels
    struct SpriteHdLevels result;  ///< Ready: the worker's, until published
};

struct SpritePackSource {
    char dir[1024];
    char name[128];
    int64_t scale;
    TbBool original_at_1x;
    TbBool content;     ///< a content folder (sprites-hd/), not the user's pack
    int64_t frames;     ///< frame lines in its animlist
    int64_t indexed;    ///< entries it added to the index
};

/** SpritePackEntry.source of generated art (ASSET_PACK_FILL): not from a pack. */
#define SPRITE_PACK_GENERATED 0xFFFF

/** Adds an entry for generated art (engine_hd_generate.h): `key` (a key of its own, apart from the packs'), the
 *  original frame's w x h texels in `rgba` (alpha below 128 transparent) and, for additive art, what it subtracts in
 *  `sub` (both copied). The workers enlarge it when it is requested; evicted, it is made again. Returns the entry, or
 *  -1 (the key is taken, or out of memory). The index is replaced (and these entries dropped) by
 *  sprite_pack_reopen(). */
int32_t sprite_pack_add_generated(uint64_t key, const TbPixel *rgba, const TbPixel *sub, int64_t w, int64_t h, TbBool additive);

/** Opens a pack folder and adds its entries (keys not indexed yet). Returns the entries added, or -1 (no pack.toml,
 *  a bad scale, keys missing or not matching the frame lines: logged once, the pack ignored). */
int64_t sprite_pack_open(const char *dir, TbBool content);
/** Replaces the index with these packs, opened in this order (content[k]: dirs[k] is a content folder). An entry made
 *  from the same art as one already decoded (the same pack folder, scale, PNG and crop) keeps its texels and uid;
 *  the rest are Idle. Returns the entries indexed. */
int64_t sprite_pack_reopen(const char *const *dirs, const TbBool *content, int64_t count);
/** Stops the decoding and frees every pack, entry and texel. */
void sprite_pack_close_all(void);
int64_t sprite_pack_entry_count(void);
int64_t sprite_pack_source_count(void);
const struct SpritePackSource *sprite_pack_source(int64_t s);
/** The entry for a content key, or -1. */
int32_t sprite_pack_find(uint64_t key);
struct SpritePackEntry *sprite_pack_entry(int32_t e);
/** At most n decoding threads (0: the logical cores but one, up to 8). Stops the running ones. */
void sprite_pack_set_worker_limit(int n);
/** Queues an Idle entry for decoding (urgent: before the background ones). */
void sprite_pack_request(int32_t e, TbBool urgent);
/** Queues every Idle entry (the eager background load). */
void sprite_pack_request_all(void);
/** Main thread: decoded entries become Built. Returns how many. */
int64_t sprite_pack_publish(void);
/** Waits until nothing is queued or decoding (KFX_SPRITE_HD_SYNC), then publishes. Returns how many it published. */
int64_t sprite_pack_wait_idle(void);
/** Built entries' memory. */
uint64_t sprite_pack_bytes(void);
/** Frees the least recently drawn Built entries until their memory is under `target`. Returns how many. */
int64_t sprite_pack_evict(uint64_t target);
/** Entries that failed to decode. */
int64_t sprite_pack_failed(void);
/** Microseconds spent decoding (all workers). */
int64_t sprite_pack_decode_us(void);

/** The levels of an HD frame from its finest texels (`top`, w x h original texels at scale s, the spill included, taken over): halves by
 *  2x2 averaging of the opaque texels (opaque if at least two are) while the scale stays even and above 2... down to 2;
 *  and a 1x level (s x s averages) when to_1x. */
void sprite_hd_build_levels(struct SpriteHdLevels *lv, TbPixel *top, int64_t w, int64_t h, int64_t s, TbBool to_1x);
void sprite_hd_free_levels(struct SpriteHdLevels *lv);
/** Additive art's subtract levels, beside lv's (after sprite_hd_build_levels() on the add texels): `top` (taken over)
 *  the finest, reduced as lv's levels are. */
void sprite_hd_build_sub_levels(struct SpriteHdLevels *lv, TbPixel *top, int64_t w, int64_t h);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
