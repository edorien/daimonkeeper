/******************************************************************************/
// Free implementation of the Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_textures_hd.c
 *     True-colour HD terrain textures: a render-only store beside block_mem.
 * @par Purpose:
 *     See engine_textures_hd.h.
 * @par Comment:
 *     HD blocks live in pages 256 texels wide (the rasteriser's atlas stride), as many
 *     blocks side by side as fit, one block per chosen pack entry.
 *     A load plans first (which pack entry each slot gets: content keys, sources, tie-break)
 *     and decodes after (only the sheets a chosen entry is on, once each). A plan over the
 *     memory cap waits for the first drawn frame, when the level's slab textures are known,
 *     to keep the variations in use.
 * @author   KeeperFX Team
 * @date     09 Oct 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "engine_textures_hd.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>
#include <spng.h>

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_dernc.h"
#include "bflib_fileio.h"
#include "config.h"
#include "config_keeperfx.h"
#include "engine_sprites_hd.h" // asset_pack_memory_budget
#include "engine_hd_generate.h"
#include "value_util.h"
#include "engine_textures.h"
#include "dungeon_data.h"
#include "kfx_sim_state.h"
#include "renderer/RendererManager.h"
#include "bflib_colour_transform.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define HD_ATLAS_STRIDE 256
#define HD_KEY_BITS 17
#define HD_KEY_SLOTS ((size_t)1 << HD_KEY_BITS)
#define FNV_OFFSET 14695981039346656037ULL
#define FNV_PRIME 1099511628211ULL
#define HD_STEM_NUMBERS 64            ///< tmapa000..tmapa063 and tmapb000..tmapb063 are looked for in a pack
#define HD_FILELIST_MAX_BLOCKS 1024
#define HD_SHEET_NAME_LEN 128
#define HD_SLOT_COUNT (TEXTURE_VARIATIONS_COUNT * TEXTURE_BLOCKS_COUNT)

struct TextureHd block_hd[HD_SLOT_COUNT];

struct HdPage {
    TbPixel *pixels;
    int8_t shift;   ///< blocks of 32 << shift texels (negative: mip levels below 32)
    int used;     ///< blocks placed
    int capacity; ///< blocks that fit side by side
};

/** One filelist block of a pack that has a key: an HD version of the 32x32 block with that content. */
struct HdEntry {
    uint64_t key;
    int32_t next;   ///< the next entry with the same key in this source, in pack order; -1
    int16_t stem;   ///< letter * HD_STEM_NUMBERS + number: the filelist it is in
    int16_t index;  ///< its block index in that filelist
    int32_t sheet;  ///< into HdSource.sheets
    int32_t x, y;   ///< its crop rectangle's corner in the sheet (size x size)
};

/** A pack folder: a content folder beside a tmap file, or the user pack. */
struct HdSource {
    char dir[512];
    uint8_t shift;                    ///< the pack's blocks are 32 << shift texels a side
    TbBool content;                   ///< a content folder (else the user pack)
    TbBool original_at_32;            ///< pack.toml use_original_at_32 (default true)
    struct HdEntry *entries;
    size_t entry_count, entry_capacity;
    char (*sheets)[HD_SHEET_NAME_LEN];
    size_t sheet_count, sheet_capacity;
    uint64_t *table_keys;             ///< open addressing: key -> first entry
    int32_t *table_heads;
    size_t table_mask;
    int32_t *entry_art;               ///< an entry's decoded block, once decoded: hd_arts index + 1; 0 none
    uint8_t *entry_needed;
};

static struct HdPage *hd_pages;
static size_t hd_page_count;
static size_t hd_page_capacity;
static struct TextureHdStats hd_stats;
static unsigned char hd_reference[768];
static TbBool hd_parity;
static TbBool hd_draw; // this frame
static uint64_t hd_memory_cap_override; ///< texture_hd_set_memory_cap() (tests); 0: ASSET_PACK_MEMORY's share
static uint64_t hd_memory_cap;          ///< the cap the last load had (hd_cap() then)

// The last load: what it was for (a reload is due when ASSET_PACK changes), and its plan while it waits for
// the first frame (over the cap).
static TbBool hd_tmap_loaded;
static char hd_loaded_pack[64];
static struct HdSource *hd_sources;
static size_t hd_source_count;
// While decoding: the HD blocks stored so far, by their pixels -- packs repeat art (the same block in several
// filelists, a slot's own entry chosen over an identical one), and identical blocks share a page.
#define HD_ART_BITS 16
struct HdArtSlot {
    uint64_t hash;
    int32_t art;   ///< hd_arts index + 1; 0: empty
};
static struct HdArtSlot *hd_art;
/** A stored HD block and its mip levels (struct TextureHd's). */
struct HdArt {
    const TbPixel *level[4];
    const TbPixel *low[TEXTURE_HD_LOW_LEVELS];
    uint8_t shift;
    TbBool original_at_32;
};
static struct HdArt *hd_arts;
static int hd_finest_shift; ///< the largest shift among hd_arts
static size_t hd_art_count, hd_art_capacity;
// TEXTURE_MIPMAPS when the store was loaded: the levels below 32x32 exist. The indexed ones are every block's in
// block_mem, by its place there (hd_static_block()); one buffer per level, 256 / side blocks to a 256-wide page.
#define HD_STATIC_BLOCKS (TEXTURE_VARIATIONS_COUNT * TEXTURE_BLOCKS_STAT_COUNT)
static TbBool hd_low;
static unsigned char *hd_index_low[TEXTURE_HD_LOW_LEVELS];
static uint8_t *hd_plan_source;  ///< per slot: 0 none, else source index + 1
static int32_t *hd_plan_entry;
static TbBool hd_plan_pending;
static TbBool hd_loaded_fill;    ///< ASSET_PACK_FILL the last load was for
#define HD_PLAN_GENERATED 0xFF   ///< hd_plan_source: generated art (ASSET_PACK_FILL)
#define HD_GEN_SHIFT 2           ///< generated blocks: 32 << 2 = 32 x HD_GENERATE_FACTOR texels
/** One distinct block to generate (ASSET_PACK_FILL): a slot with it (its context), and the art made. */
struct HdGenTask {
    int64_t slot;
    uint64_t key;
    int32_t art;   ///< hd_arts index + 1; 0 none (over the cap, or out of memory)
};
static struct HdGenTask *hd_gen_tasks;
static size_t hd_gen_count, hd_gen_capacity;
static uint64_t *hd_gen_keys;     ///< open addressing over HD_KEY_SLOTS: the tasks' keys
static int32_t *hd_gen_key_task;  ///< and their task + 1 (0: empty)
static SDL_AtomicInt hd_next_gen;
static SDL_Mutex *hd_decode_lock; ///< while decoding on several threads: storing a block
/******************************************************************************/

uint64_t texture_hd_block_key(const unsigned char *block)
{
    uint64_t h = FNV_OFFSET;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            h = (h ^ block[y * HD_ATLAS_STRIDE + x]) * FNV_PRIME;
    return h;
}

static TbBool block_is_uniform(const unsigned char *block)
{
    const unsigned char first = block[0];
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            if (block[y * HD_ATLAS_STRIDE + x] != first)
                return false;
    return true;
}

/** Whether a slot id holds a block of its own (not an animated id, which shows another's). */
static TbBool hd_static_id(int64_t id)
{
    return (id < TEXTURE_BLOCKS_STAT_COUNT_A) || ((id >= TEX_B_START_POINT) && (id < TEX_B_START_POINT + TEXTURE_BLOCKS_STAT_COUNT_B));
}

static int hd_side(int shift)
{
    return (shift >= 0) ? (32 << shift) : (32 >> -shift);
}

/** A new block's top-left texel in a page with room, or NULL. */
static TbPixel *hd_alloc_block(int shift)
{
    const int side = hd_side(shift);
    struct HdPage *page = NULL;
    for (size_t i = hd_page_count; i > 0; i--)
    {
        if ((hd_pages[i - 1].shift == shift) && (hd_pages[i - 1].used < hd_pages[i - 1].capacity))
        {
            page = &hd_pages[i - 1];
            break;
        }
    }
    if (page == NULL)
    {
        if (hd_page_count == hd_page_capacity)
        {
            const size_t cap = (hd_page_capacity == 0) ? 256 : hd_page_capacity * 2;
            struct HdPage *grown = (struct HdPage *)realloc(hd_pages, cap * sizeof(struct HdPage));
            if (grown == NULL)
                return NULL;
            hd_pages = grown;
            hd_page_capacity = cap;
        }
        page = &hd_pages[hd_page_count];
        page->pixels = (TbPixel *)calloc((size_t)HD_ATLAS_STRIDE * side, sizeof(TbPixel));
        if (page->pixels == NULL)
            return NULL;
        page->shift = (int8_t)shift;
        page->used = 0;
        page->capacity = HD_ATLAS_STRIDE / side;
        hd_page_count++;
        hd_stats.bytes += (uint64_t)HD_ATLAS_STRIDE * side * sizeof(TbPixel);
    }
    return page->pixels + (size_t)side * (size_t)(page->used++);
}

/** Bytes an HD block takes in pages, with its levels down to 64 (about a third more). */
static uint64_t hd_block_bytes(uint8_t shift)
{
    const uint64_t side = (uint64_t)32 << shift;
    return side * side * sizeof(TbPixel) * 4 / 3;
}

/** A level box-filtered from the one above (2 x 2 texels each; side: the new level's): colours averaged, black
 *  (alpha 0, texture index 0) as black; alpha 0 only where all four were. */
static void hd_box_filter(const TbPixel *src, TbPixel *dst, int side)
{
    for (int y = 0; y < side; y++)
    {
        const TbPixel *r0 = src + (size_t)(2 * y) * HD_ATLAS_STRIDE;
        const TbPixel *r1 = r0 + HD_ATLAS_STRIDE;
        TbPixel *out = dst + (size_t)y * HD_ATLAS_STRIDE;
        for (int x = 0; x < side; x++)
        {
            const TbPixel a = r0[2 * x], b = r0[2 * x + 1], c = r1[2 * x], d = r1[2 * x + 1];
            out[x].r = (uint8_t)((a.r + b.r + c.r + d.r + 2) >> 2);
            out[x].g = (uint8_t)((a.g + b.g + c.g + d.g + 2) >> 2);
            out[x].b = (uint8_t)((a.b + b.b + c.b + d.b + 2) >> 2);
            out[x].a = ((a.a | b.a | c.a | d.a) == 0) ? 0 : 255;
        }
    }
}

/** A new art record: the block at `top` and its levels below it, down to 64, then 32 and below for art that
 *  replaces the original at 32 (the levels below 32 with TEXTURE_MIPMAPS). Its index + 1, or 0 when out of memory. */
static int32_t hd_add_art(const TbPixel *top, uint8_t shift, TbBool original_at_32)
{
    if (hd_art_count == hd_art_capacity)
    {
        const size_t cap = (hd_art_capacity == 0) ? 1024 : hd_art_capacity * 2;
        struct HdArt *grown = (struct HdArt *)realloc(hd_arts, cap * sizeof(struct HdArt));
        if (grown == NULL)
            return 0;
        hd_arts = grown;
        hd_art_capacity = cap;
    }
    struct HdArt *art = &hd_arts[hd_art_count];
    memset(art, 0, sizeof(*art));
    art->shift = shift;
    art->original_at_32 = original_at_32;
    art->level[shift] = top;
    if (shift > hd_finest_shift)
        hd_finest_shift = shift;
    const int lowest = original_at_32 ? 1 : (hd_low ? -TEXTURE_HD_LOW_LEVELS : 0);
    const TbPixel *above = top;
    for (int s = (int)shift - 1; s >= lowest; s--)
    {
        TbPixel *dst = hd_alloc_block(s);
        if (dst == NULL)
            return 0;
        hd_box_filter(above, dst, hd_side(s));
        if (s >= 0)
            art->level[s] = dst;
        else
            art->low[-s - 1] = dst;
        above = dst;
    }
    return (int32_t)(++hd_art_count);
}

/** Fills a slot from an art record. */
static void hd_set_slot(int64_t idx, const struct HdArt *art, enum TextureHdSource source)
{
    struct TextureHd *e = &block_hd[idx];
    e->page = art->level[art->shift];
    e->shift = art->shift;
    e->source = (uint8_t)source;
    memcpy(e->level, art->level, sizeof(e->level));
    memcpy(e->low, art->low, sizeof(e->low));
}

/******************************************************************************/
// Synthetic store (KFX_TEXTURE_HD_SYNTH)

/** The synthetic HD version of a 32x32 block: each texel 2^shift times each way, in the reference palette's
 *  colours; index 0 as (0,0,0,0), as the indexed path draws it. Its own levels are the same upscales (a box of
 *  identical texels), so every level draws the 32x32 block's texels in parity mode. */
static void hd_build_synthetic(TbPixel *dst, const unsigned char *block, uint8_t shift)
{
    const int side = 32 << shift;
    for (int y = 0; y < side; y++)
        for (int x = 0; x < side; x++)
            dst[(size_t)y * HD_ATLAS_STRIDE + x] = expand_indexed_pixel(block[(y >> shift) * HD_ATLAS_STRIDE + (x >> shift)], hd_reference);
}

static void hd_load_synthetic(uint8_t shift)
{
    struct SynthSlot { uint64_t key; int32_t art; };
    struct SynthSlot *keys = (struct SynthSlot *)calloc(HD_KEY_SLOTS, sizeof(struct SynthSlot));
    if (keys == NULL)
        return;
    for (int64_t idx = 0; idx < HD_SLOT_COUNT; idx++)
    {
        if (!hd_static_id(idx % TEXTURE_BLOCKS_COUNT))
            continue; // animated ids follow their frame (texture_hd_copy())
        const unsigned char *block = block_ptrs[idx];
        if ((block == NULL) || block_is_uniform(block))
            continue;
        hd_stats.slots_hashed++;
        const uint64_t key = texture_hd_block_key(block);
        size_t slot = (size_t)(key ^ (key >> 29)) & (HD_KEY_SLOTS - 1);
        while ((keys[slot].art != 0) && (keys[slot].key != key))
            slot = (slot + 1) & (HD_KEY_SLOTS - 1);
        if (keys[slot].art == 0)
        {
            TbPixel *dst = hd_alloc_block(shift);
            if (dst != NULL)
                hd_build_synthetic(dst, block, shift);
            const int32_t art = (dst != NULL) ? hd_add_art(dst, shift, true) : 0;
            if (art == 0)
            {
                WARNMSG("HD textures: out of memory after %" PRIu64 " blocks", hd_stats.blocks);
                break;
            }
            keys[slot].key = key;
            keys[slot].art = art;
            hd_stats.blocks++;
        }
        hd_set_slot(idx, &hd_arts[keys[slot].art - 1], TexHdSrc_Synthetic);
        hd_stats.slots_with_hd++;
    }
    free(keys);
}

/******************************************************************************/
// Packs: pack.toml, filelists, keys, sheets

static void hd_source_free(struct HdSource *src)
{
    free(src->entries);
    free(src->sheets);
    free(src->table_keys);
    free(src->table_heads);
    free(src->entry_art);
    free(src->entry_needed);
    memset(src, 0, sizeof(*src));
}

static void hd_plan_free(void)
{
    for (size_t i = 0; i < hd_source_count; i++)
        hd_source_free(&hd_sources[i]);
    free(hd_sources);
    hd_sources = NULL;
    hd_source_count = 0;
    free(hd_plan_source);
    hd_plan_source = NULL;
    free(hd_plan_entry);
    hd_plan_entry = NULL;
    hd_plan_pending = false;
}

/** A whole file into a new buffer (with a terminating 0 after it), or NULL. Not RNC-unpacked. */
static unsigned char *hd_read_file(const char *fname, size_t *len)
{
    *len = 0;
    if (!LbFileExists(fname))
        return NULL;
    const int64_t length = LbFileLength(fname);
    if (length <= 0)
        return NULL;
    TbFileHandle handle = LbFileOpen(fname, Lb_FILE_MODE_READ_ONLY);
    if (!handle)
        return NULL;
    unsigned char *data = (unsigned char *)malloc((size_t)length + 1);
    if ((data != NULL) && (LbFileRead(handle, data, (uint64_t)length) != length))
    {
        free(data);
        data = NULL;
    }
    LbFileClose(handle);
    if (data != NULL)
    {
        data[length] = 0;
        *len = (size_t)length;
    }
    return data;
}

static int32_t hd_sheet_id(struct HdSource *src, const char *name)
{
    for (size_t i = 0; i < src->sheet_count; i++)
        if (strcmp(src->sheets[i], name) == 0)
            return (int32_t)i;
    if (src->sheet_count == src->sheet_capacity)
    {
        const size_t cap = (src->sheet_capacity == 0) ? 64 : src->sheet_capacity * 2;
        char (*grown)[HD_SHEET_NAME_LEN] = (char (*)[HD_SHEET_NAME_LEN])realloc(src->sheets, cap * HD_SHEET_NAME_LEN);
        if (grown == NULL)
            return -1;
        src->sheets = grown;
        src->sheet_capacity = cap;
    }
    snprintf(src->sheets[src->sheet_count], HD_SHEET_NAME_LEN, "%s", name);
    return (int32_t)src->sheet_count++;
}

static TbBool hd_add_entry(struct HdSource *src, const struct HdEntry *entry)
{
    if (src->entry_count == src->entry_capacity)
    {
        const size_t cap = (src->entry_capacity == 0) ? 1024 : src->entry_capacity * 2;
        struct HdEntry *grown = (struct HdEntry *)realloc(src->entries, cap * sizeof(struct HdEntry));
        if (grown == NULL)
            return false;
        src->entries = grown;
        src->entry_capacity = cap;
    }
    src->entries[src->entry_count++] = *entry;
    return true;
}

int64_t texture_hd_parse_filelist(const char *text, size_t len, struct TextureHdFilelistLine *lines, int64_t max_lines,
    int64_t *header_size)
{
    // UTF-8 BOM, then "name cols rows w h", then one "sheet x y w h" line per block; tabs or spaces; CRLF or LF.
    size_t pos = 0;
    if ((len >= 3) && ((unsigned char)text[0] == 0xEF) && ((unsigned char)text[1] == 0xBB) && ((unsigned char)text[2] == 0xBF))
        pos = 3;
    TbBool header = true;
    int64_t count = 0;
    *header_size = 0;
    while (pos < len)
    {
        size_t end = pos;
        while ((end < len) && (text[end] != '\n'))
            end++;
        char line[512];
        size_t n = end - pos;
        if (n >= sizeof(line))
            n = sizeof(line) - 1;
        memcpy(line, text + pos, n);
        line[n] = '\0';
        pos = end + 1;
        char name[HD_SHEET_NAME_LEN];
        long long a = 0, b = 0, c = 0, d = 0;
        if (sscanf(line, "%127s %lld %lld %lld %lld", name, &a, &b, &c, &d) != 5)
            continue; // blank or not a block line
        if (header)
        {
            header = false;
            *header_size = (c == d) ? (int64_t)c : 0;
            continue;
        }
        if (count < max_lines)
        {
            snprintf(lines[count].sheet, sizeof(lines[count].sheet), "%s", name);
            lines[count].x = (int32_t)a;
            lines[count].y = (int32_t)b;
            lines[count].w = (int32_t)c;
            lines[count].h = (int32_t)d;
        }
        count++;
    }
    return (count < max_lines) ? count : max_lines;
}

/** Reads one filelist of a pack and its keys into the source's entries. */
static void hd_source_add_filelist(struct HdSource *src, char letter, int number)
{
    char stem[16];
    snprintf(stem, sizeof(stem), "tmap%c%03d", letter, number);
    char fname[1024];
    snprintf(fname, sizeof(fname), "%s/filelist_%s.txt", src->dir, stem);
    size_t len;
    char *text = (char *)hd_read_file(fname, &len);
    if (text == NULL)
        return;
    snprintf(fname, sizeof(fname), "%s/keys/%s.idx", src->dir, stem);
    size_t keys_len;
    unsigned char *keys = hd_read_file(fname, &keys_len);
    if (keys == NULL)
    {
        WARNMSG("HD textures: \"%s/filelist_%s.txt\" has no keys/%s.idx (scripts/texpack.py keys); ignored", src->dir, stem, stem);
        free(text);
        return;
    }
    struct TextureHdFilelistLine *lines = (struct TextureHdFilelistLine *)calloc(HD_FILELIST_MAX_BLOCKS, sizeof(*lines));
    if (lines == NULL)
    {
        free(text);
        free(keys);
        return;
    }
    int64_t header_size;
    const int64_t count = texture_hd_parse_filelist(text, len, lines, HD_FILELIST_MAX_BLOCKS, &header_size);
    const int32_t side = 32 << src->shift;
    int32_t warned_sheet = -1;
    for (int64_t i = 0; i < count; i++)
    {
        if ((size_t)(i + 1) * 8 > keys_len)
            break;
        uint64_t key = 0;
        for (int k = 7; k >= 0; k--)
            key = (key << 8) | keys[i * 8 + k];
        if (key == 0)
            continue; // no 32x32 block to upgrade (uniform, or not in the .dat the keys were made from)
        const int32_t sheet = hd_sheet_id(src, lines[i].sheet);
        if (sheet < 0)
            break;
        if ((lines[i].w != side) || (lines[i].h != side) || (lines[i].x < 0) || (lines[i].y < 0))
        {
            if (warned_sheet != sheet)
                WARNMSG("HD textures: \"%s/filelist_%s.txt\": %s crops %dx%d blocks, the pack's size is %d; skipped",
                    src->dir, stem, lines[i].sheet, (int)lines[i].w, (int)lines[i].h, (int)side);
            warned_sheet = sheet;
            continue;
        }
        struct HdEntry entry;
        entry.key = key;
        entry.next = -1;
        entry.stem = (int16_t)(((letter == 'b') ? 1 : 0) * HD_STEM_NUMBERS + number);
        entry.index = (int16_t)i;
        entry.sheet = sheet;
        entry.x = lines[i].x;
        entry.y = lines[i].y;
        if (!hd_add_entry(src, &entry))
            break;
    }
    free(lines);
    free(text);
    free(keys);
}

/** The first entry with a key in a source, or -1. */
static int32_t hd_source_find(const struct HdSource *src, uint64_t key)
{
    if (src->table_keys == NULL)
        return -1;
    size_t slot = (size_t)(key ^ (key >> 29)) & src->table_mask;
    while (src->table_heads[slot] >= 0)
    {
        if (src->table_keys[slot] == key)
            return src->table_heads[slot];
        slot = (slot + 1) & src->table_mask;
    }
    return -1;
}

/** The key table: each key's entries chained in pack order. */
static TbBool hd_source_index(struct HdSource *src)
{
    size_t size = 1024;
    while (size < src->entry_count * 2)
        size *= 2;
    src->table_keys = (uint64_t *)calloc(size, sizeof(uint64_t));
    src->table_heads = (int32_t *)malloc(size * sizeof(int32_t));
    src->entry_art = (int32_t *)calloc(src->entry_count + 1, sizeof(int32_t));
    src->entry_needed = (uint8_t *)calloc(src->entry_count + 1, 1);
    if ((src->table_keys == NULL) || (src->table_heads == NULL) || (src->entry_art == NULL) || (src->entry_needed == NULL))
        return false;
    for (size_t i = 0; i < size; i++)
        src->table_heads[i] = -1;
    src->table_mask = size - 1;
    // Backwards, so each chain ends up in pack order.
    for (size_t i = src->entry_count; i > 0; i--)
    {
        struct HdEntry *e = &src->entries[i - 1];
        size_t slot = (size_t)(e->key ^ (e->key >> 29)) & src->table_mask;
        while ((src->table_heads[slot] >= 0) && (src->table_keys[slot] != e->key))
            slot = (slot + 1) & src->table_mask;
        e->next = src->table_heads[slot];
        src->table_keys[slot] = e->key;
        src->table_heads[slot] = (int32_t)(i - 1);
    }
    return true;
}

/** Opens a pack folder (pack.toml, then every filelist with keys), or returns false: not there, or not a pack. */
static TbBool hd_source_open(struct HdSource *src, const char *dir, TbBool content)
{
    memset(src, 0, sizeof(*src));
    snprintf(src->dir, sizeof(src->dir), "%s", dir);
    src->content = content;
    char fname[1024];
    snprintf(fname, sizeof(fname), "%s/pack.toml", dir);
    if (!LbFileExists(fname))
    {
        if (!content)
            WARNMSG("HD textures: no pack \"%s\" (no pack.toml)", fname);
        return false;
    }
    VALUE root;
    if (!load_toml_file(fname, &root, 0))
        return false;
    int64_t size = 0;
    VALUE *size_value = value_dict_get(&root, "size");
    if (value_type(size_value) == VALUE_INT32)
        size = value_int32(size_value);
    VALUE *original_value = value_dict_get(&root, "use_original_at_32");
    src->original_at_32 = (value_type(original_value) == VALUE_BOOL) ? (value_bool(original_value) ? true : false) : true;
    value_fini(&root);
    if ((size != 64) && (size != 128) && (size != 256))
    {
        WARNMSG("HD textures: \"%s\": size must be 64, 128 or 256, not %" PRId64 "; pack ignored", fname, size);
        return false;
    }
    src->shift = (uint8_t)((size == 64) ? 1 : ((size == 128) ? 2 : 3));
    for (int letter = 0; letter < 2; letter++)
        for (int number = 0; number < HD_STEM_NUMBERS; number++)
            hd_source_add_filelist(src, (letter == 0) ? 'a' : 'b', number);
    if (!hd_source_index(src))
    {
        hd_source_free(src);
        return false;
    }
    return true;
}

/** The source index (+1) of a pack folder, opened the first time; 0 if it isn't a pack. */
static uint8_t hd_source_for(const char *dir, TbBool content, size_t *opened_capacity)
{
    for (size_t i = 0; i < hd_source_count; i++)
        if (strcmp(hd_sources[i].dir, dir) == 0)
            return (hd_sources[i].entry_count > 0) ? (uint8_t)(i + 1) : 0;
    if (hd_source_count >= 254)
        return 0;
    if (hd_source_count == *opened_capacity)
    {
        const size_t cap = (*opened_capacity == 0) ? 8 : *opened_capacity * 2;
        struct HdSource *grown = (struct HdSource *)realloc(hd_sources, cap * sizeof(struct HdSource));
        if (grown == NULL)
            return 0;
        hd_sources = grown;
        *opened_capacity = cap;
    }
    struct HdSource *src = &hd_sources[hd_source_count++];
    if (!hd_source_open(src, dir, content))
    {
        // remembered (dir kept), so it isn't opened again
        char kept[512];
        snprintf(kept, sizeof(kept), "%s", dir);
        hd_source_free(src);
        snprintf(src->dir, sizeof(src->dir), "%s", kept);
        return 0;
    }
    return (src->entry_count > 0) ? (uint8_t)(hd_source_count) : 0;
}

/** The content folder beside a tmap file: <its folder>/textures-hd. */
static void hd_content_dir(const char *tmap_path, char *dir, size_t size)
{
    snprintf(dir, size, "%s", tmap_path);
    char *slash = strrchr(dir, '/');
    char *backslash = strrchr(dir, '\\');
    if ((backslash != NULL) && ((slash == NULL) || (backslash > slash)))
        slash = backslash;
    if (slash != NULL)
        *slash = '\0';
    else
        snprintf(dir, size, ".");
    const size_t used = strlen(dir);
    snprintf(dir + used, size - used, "/textures-hd");
}

/** The entry a slot gets in a source: the one made for the slot's own (stem, index), else the first in pack order. */
static int32_t hd_choose_entry(const struct HdSource *src, int32_t first, int16_t stem, int16_t index)
{
    for (int32_t e = first; e >= 0; e = src->entries[e].next)
        if ((src->entries[e].stem == stem) && (src->entries[e].index == index))
            return e;
    return first;
}

/** Decodes a PNG to RGBA8; NULL if it can't. */
static unsigned char *hd_decode_png(const char *fname, uint32_t *width, uint32_t *height)
{
    size_t len;
    unsigned char *data = hd_read_file(fname, &len);
    if (data == NULL)
        return NULL;
    unsigned char *pixels = NULL;
    spng_ctx *ctx = spng_ctx_new(0);
    if (ctx != NULL)
    {
        struct spng_ihdr ihdr;
        size_t size = 0;
        spng_set_image_limits(ctx, 16384, 16384);
        if ((spng_set_png_buffer(ctx, data, len) == 0) && (spng_get_ihdr(ctx, &ihdr) == 0) &&
            (spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &size) == 0))
        {
            pixels = (unsigned char *)malloc(size);
            if ((pixels != NULL) && (spng_decode_image(ctx, pixels, size, SPNG_FMT_RGBA8, SPNG_DECODE_TRNS) != 0))
            {
                free(pixels);
                pixels = NULL;
            }
            *width = ihdr.width;
            *height = ihdr.height;
        }
        spng_ctx_free(ctx);
    }
    free(data);
    return pixels;
}

/** The hash a decoded block (side x side, contiguous) is found by in hd_art: FNV-1a over 64-bit words (a texel pair
 *  at a time) -- only ever compared with itself, and 8x fewer steps than bytes. */
static uint64_t hd_art_hash(const TbPixel *block, uint8_t shift)
{
    const size_t side = (size_t)32 << shift;
    const size_t bytes = side * side * sizeof(TbPixel);
    uint64_t h = FNV_OFFSET ^ shift;
    const unsigned char *b = (const unsigned char *)block;
    for (size_t i = 0; i + 8 <= bytes; i += 8)
    {
        uint64_t word;
        memcpy(&word, b + i, sizeof(word));
        h = (h ^ word) * FNV_PRIME;
        h ^= h >> 29;
    }
    return h;
}

/** A decoded block (side x side, contiguous, hashed by hd_art_hash()) in the store, with its levels: the art of an
 *  identical one stored before, or a new one; its hd_arts index + 1, 0 when it would pass the memory cap. Under
 *  hd_decode_lock. */
static int32_t hd_store_block(const TbPixel *block, uint8_t shift, uint64_t h, TbBool original_at_32)
{
    h ^= original_at_32 ? 0x9E3779B97F4A7C15ULL : 0;
    const size_t side = (size_t)32 << shift;
    const size_t mask = ((size_t)1 << HD_ART_BITS) - 1;
    size_t slot = (size_t)(h ^ (h >> 31)) & mask;
    for (size_t probe = 0; (hd_art != NULL) && (probe <= mask); probe++)
    {
        struct HdArtSlot *a = &hd_art[slot];
        if (a->art == 0)
            break;
        const struct HdArt *art = &hd_arts[a->art - 1];
        if ((a->hash == h) && (art->shift == shift) && (art->original_at_32 == original_at_32))
        {
            const TbPixel *page = art->level[shift];
            TbBool equal = true;
            for (size_t y = 0; (y < side) && equal; y++)
                equal = (memcmp(page + y * HD_ATLAS_STRIDE, block + y * side, side * sizeof(TbPixel)) == 0);
            if (equal)
                return a->art;
        }
        slot = (slot + 1) & mask;
    }
    if (hd_stats.bytes + hd_block_bytes(shift) > hd_memory_cap)
        return 0;
    TbPixel *dst = hd_alloc_block(shift);
    if (dst == NULL)
        return 0;
    for (size_t y = 0; y < side; y++)
        memcpy(dst + y * HD_ATLAS_STRIDE, block + y * side, side * sizeof(TbPixel));
    const int32_t art = hd_add_art(dst, shift, original_at_32);
    if (art == 0)
        return 0;
    hd_stats.blocks++;
    if ((hd_art != NULL) && (hd_art[slot].art == 0))
    {
        hd_art[slot].hash = h;
        hd_art[slot].art = art;
    }
    return art;
}

/******************************************************************************/
// Generated art (ASSET_PACK_FILL)

/** The slot of the block beside `slot` (dx, dy) in its tmap file's grid (8 blocks a row), or -1. */
static int64_t hd_neighbour_slot(int64_t slot, int dx, int dy)
{
    const int64_t f = slot / TEXTURE_BLOCKS_COUNT, id = slot % TEXTURE_BLOCKS_COUNT;
    int64_t base, count;
    if (id < TEXTURE_BLOCKS_STAT_COUNT_A)
    {
        base = 0;
        count = TEXTURE_BLOCKS_STAT_COUNT_A;
    }
    else if ((id >= TEX_B_START_POINT) && (id < TEX_B_START_POINT + TEXTURE_BLOCKS_STAT_COUNT_B))
    {
        base = TEX_B_START_POINT;
        count = TEXTURE_BLOCKS_STAT_COUNT_B;
    }
    else
    {
        return -1;
    }
    const int64_t j = id - base, col = j % 8 + dx, row = j / 8 + dy;
    if ((col < 0) || (col >= 8) || (row < 0) || (row * 8 + col >= count))
        return -1;
    const int64_t n = f * TEXTURE_BLOCKS_COUNT + base + row * 8 + col;
    return (block_ptrs[n] != NULL) ? n : -1;
}

/** How far apart (summed RGB differences) the texel lines a and b are: 32 texels from (ax, ay) and (bx, by), along x
 *  or along y. */
static int64_t hd_line_diff(const unsigned char *a, int ax, int ay, const unsigned char *b, int bx, int by, TbBool along_x)
{
    int64_t d = 0;
    for (int i = 0; i < 32; i++)
    {
        const int dx = along_x ? i : 0, dy = along_x ? 0 : i;
        const TbPixel p = resolve_indexed_pixel(a[(ay + dy) * HD_ATLAS_STRIDE + ax + dx], hd_reference);
        const TbPixel q = resolve_indexed_pixel(b[(by + dy) * HD_ATLAS_STRIDE + bx + dx], hd_reference);
        d += abs(p.r - q.r) + abs(p.g - q.g) + abs(p.b - q.b);
    }
    return d;
}

/** A block and what surrounds it for generating its art: `out`, (32 + 2 pad) texels a side in the reference palette's
 *  colours (index 0 transparent). On each side the block next to it in its tmap file where its edge runs on into that
 *  one more smoothly than into its own opposite edge (a picture over several blocks), else its own opposite edge (a
 *  block that tiles); a corner as its two sides say (the diagonal block, one side's neighbour, or this block). */
static void hd_generate_context(int64_t slot, TbPixel *out, int pad)
{
    const unsigned char *me = block_ptrs[slot];
    const unsigned char *grid[3][3];
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 3; x++)
            grid[y][x] = me;
    static const int sides[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
    TbBool real[3][3];
    memset(real, 0, sizeof(real));
    for (int k = 0; k < 4; k++)
    {
        const int dx = sides[k][0], dy = sides[k][1];
        const int64_t n = hd_neighbour_slot(slot, dx, dy);
        if (n < 0)
            continue;
        const unsigned char *nb = block_ptrs[n];
        int64_t there, wrap;
        if (dx != 0)
        {
            const int edge = (dx > 0) ? 31 : 0, near = (dx > 0) ? 0 : 31;
            there = hd_line_diff(me, edge, 0, nb, near, 0, false);
            wrap = hd_line_diff(me, edge, 0, me, near, 0, false);
        }
        else
        {
            const int edge = (dy > 0) ? 31 : 0, near = (dy > 0) ? 0 : 31;
            there = hd_line_diff(me, 0, edge, nb, 0, near, true);
            wrap = hd_line_diff(me, 0, edge, me, 0, near, true);
        }
        if (there < wrap)
        {
            grid[dy + 1][dx + 1] = nb;
            real[dy + 1][dx + 1] = true;
        }
    }
    // A corner: the diagonal block where both its sides run on into their neighbours; where one side does, that
    // neighbour (tiling across the other side, as this block does); else this block.
    for (int dy = -1; dy <= 1; dy += 2)
        for (int dx = -1; dx <= 1; dx += 2)
        {
            const int64_t n = hd_neighbour_slot(slot, dx, dy);
            if (real[1][dx + 1] && real[dy + 1][1])
                grid[dy + 1][dx + 1] = (n >= 0) ? block_ptrs[n] : grid[1][dx + 1];
            else if (real[1][dx + 1])
                grid[dy + 1][dx + 1] = grid[1][dx + 1];
            else if (real[dy + 1][1])
                grid[dy + 1][dx + 1] = grid[dy + 1][1];
        }
    const int side = 32 + 2 * pad;
    for (int py = 0; py < side; py++)
        for (int px = 0; px < side; px++)
        {
            const int y = py - pad, x = px - pad;
            const unsigned char *b = grid[(y < 0) ? 0 : ((y >= 32) ? 2 : 1)][(x < 0) ? 0 : ((x >= 32) ? 2 : 1)];
            out[py * side + px] = expand_indexed_pixel(b[((y + 32) % 32) * HD_ATLAS_STRIDE + (x + 32) % 32], hd_reference);
        }
}

/** Generates one block's art (hd_store_block(), locked). */
static void hd_run_gen_task(struct HdGenTask *task, TbPixel *scratch)
{
    TbPixel context[(32 + 2 * HD_GENERATE_PAD) * (32 + 2 * HD_GENERATE_PAD)];
    hd_generate_context(task->slot, context, HD_GENERATE_PAD);
    const int side = 32 << HD_GEN_SHIFT;
    if (!hd_generate_upscale(context, 32 + 2 * HD_GENERATE_PAD, 32, 32, HD_GENERATE_PAD, HD_GENERATE_FACTOR, false, scratch, side))
        return;
    // Terrain is opaque: transparent is texture index 0's black, as the indexed path draws it (it is (0,0,0,0)).
    const uint64_t h = hd_art_hash(scratch, HD_GEN_SHIFT);
    if (hd_decode_lock != NULL)
        SDL_LockMutex(hd_decode_lock);
    task->art = hd_store_block(scratch, HD_GEN_SHIFT, h, true);
    if (hd_decode_lock != NULL)
        SDL_UnlockMutex(hd_decode_lock);
}

static const struct HdSource *hd_sort_source;
static int hd_compare_by_sheet(const void *a, const void *b)
{
    const struct HdEntry *ea = &hd_sort_source->entries[*(const int32_t *)a];
    const struct HdEntry *eb = &hd_sort_source->entries[*(const int32_t *)b];
    if (ea->sheet != eb->sheet)
        return (ea->sheet < eb->sheet) ? -1 : 1;
    return (*(const int32_t *)a < *(const int32_t *)b) ? -1 : ((*(const int32_t *)a > *(const int32_t *)b) ? 1 : 0);
}

/** One sheet to decode: a source's needed entries on it (order[first..last)). */
struct HdSheetTask {
    struct HdSource *src;
    const int32_t *order;
    size_t first, last;
    // results, logged by the main thread
    uint8_t status; ///< 0 decoded, 1 missing (a partial pack: quiet), 2 can't decode, 3 a crop off the sheet
    uint32_t w, h;
};

static struct HdSheetTask *hd_tasks;
static size_t hd_task_count;
static SDL_AtomicInt hd_next_task;

/** Decodes one sheet and stores its needed blocks (hd_store_block(), locked). */
static void hd_run_task(struct HdSheetTask *task, TbPixel *scratch)
{
    struct HdSource *src = task->src;
    const int32_t sheet = src->entries[task->order[task->first]].sheet;
    const int32_t side = 32 << src->shift;
    char fname[1024];
    snprintf(fname, sizeof(fname), "%s/%s", src->dir, src->sheets[sheet]);
    if (!LbFileExists(fname))
    {
        task->status = 1;
        return;
    }
    unsigned char *rgba = hd_decode_png(fname, &task->w, &task->h);
    if (rgba == NULL)
    {
        task->status = 2;
        return;
    }
    for (size_t k = task->first; k < task->last; k++)
    {
        const int32_t index = task->order[k];
        const struct HdEntry *e = &src->entries[index];
        if (((uint32_t)(e->x + side) > task->w) || ((uint32_t)(e->y + side) > task->h))
        {
            task->status = 3;
            continue;
        }
        for (int32_t y = 0; y < side; y++)
        {
            const unsigned char *row = rgba + ((size_t)(e->y + y) * task->w + (size_t)e->x) * 4;
            TbPixel *out = scratch + (size_t)y * side;
            for (int32_t x = 0; x < side; x++)
            {
                // Terrain is opaque: alpha below 128 is texture index 0's black, as the indexed path draws it.
                if (row[x * 4 + 3] < 128)
                {
                    out[x].r = out[x].g = out[x].b = out[x].a = 0;
                }
                else
                {
                    out[x].r = row[x * 4 + 0];
                    out[x].g = row[x * 4 + 1];
                    out[x].b = row[x * 4 + 2];
                    out[x].a = 255;
                }
            }
        }
        const uint64_t h = hd_art_hash(scratch, src->shift);
        if (hd_decode_lock != NULL)
            SDL_LockMutex(hd_decode_lock);
        const int32_t art = hd_store_block(scratch, src->shift, h, src->original_at_32);
        if (hd_decode_lock != NULL)
            SDL_UnlockMutex(hd_decode_lock);
        // Each entry is in one task only: no other thread writes these.
        if (art == 0)
            src->entry_needed[index] = 3; // over the cap (or out of memory)
        src->entry_art[index] = art;
    }
    free(rgba);
}

static int SDLCALL hd_decode_worker(void *data)
{
    (void)data;
    TbPixel *scratch = (TbPixel *)malloc((size_t)256 * 256 * sizeof(TbPixel)); // the largest block
    if (scratch == NULL)
        return 0;
    for (;;)
    {
        const int t = SDL_AddAtomicInt(&hd_next_task, 1);
        if ((t < 0) || ((size_t)t >= hd_task_count))
            break;
        hd_run_task(&hd_tasks[t], scratch);
    }
    // Then the generated blocks: the packs' art is stored first, and wins the memory cap.
    for (;;)
    {
        const int t = SDL_AddAtomicInt(&hd_next_gen, 1);
        if ((t < 0) || ((size_t)t >= hd_gen_count))
            break;
        hd_run_gen_task(&hd_gen_tasks[t], scratch);
    }
    free(scratch);
    return 0;
}

/** Decodes every source's needed entries: one task per sheet, on worker threads (sheets are independent; storing a
 *  block is locked). Entries on a sheet that can't be read stay without. */
static void hd_decode_all(void)
{
    // The tasks: each source's needed entries sorted by sheet, a run per sheet.
    size_t capacity = 0;
    int32_t **orders = (int32_t **)calloc(hd_source_count + 1, sizeof(int32_t *));
    if (orders == NULL)
        return;
    for (size_t i = 0; i < hd_source_count; i++)
    {
        struct HdSource *src = &hd_sources[i];
        if (src->entry_count == 0)
            continue;
        int32_t *order = orders[i] = (int32_t *)malloc(src->entry_count * sizeof(int32_t));
        if (order == NULL)
            continue;
        size_t n = 0;
        for (size_t e = 0; e < src->entry_count; e++)
            if (src->entry_needed[e])
                order[n++] = (int32_t)e;
        hd_sort_source = src;
        qsort(order, n, sizeof(int32_t), hd_compare_by_sheet);
        for (size_t k = 0; k < n;)
        {
            size_t j = k;
            while ((j < n) && (src->entries[order[j]].sheet == src->entries[order[k]].sheet))
                j++;
            if (hd_task_count == capacity)
            {
                capacity = (capacity == 0) ? 256 : capacity * 2;
                struct HdSheetTask *grown = (struct HdSheetTask *)realloc(hd_tasks, capacity * sizeof(struct HdSheetTask));
                if (grown == NULL)
                    break;
                hd_tasks = grown;
            }
            struct HdSheetTask *task = &hd_tasks[hd_task_count++];
            memset(task, 0, sizeof(*task));
            task->src = src;
            task->order = order;
            task->first = k;
            task->last = j;
            k = j;
        }
    }
    hd_art = (struct HdArtSlot *)calloc((size_t)1 << HD_ART_BITS, sizeof(struct HdArtSlot));
    SDL_SetAtomicInt(&hd_next_task, 0);
    SDL_SetAtomicInt(&hd_next_gen, 0);
    int workers = SDL_GetNumLogicalCPUCores();
    if (workers > 8)
        workers = 8;
    if ((size_t)workers > hd_task_count + hd_gen_count)
        workers = (int)(hd_task_count + hd_gen_count);
    SDL_Thread *threads[8];
    int started = 0;
    if (workers > 1)
    {
        hd_decode_lock = SDL_CreateMutex();
        for (int i = 0; (hd_decode_lock != NULL) && (i < workers); i++)
        {
            threads[started] = SDL_CreateThread(hd_decode_worker, "texture_hd", NULL);
            if (threads[started] != NULL)
                started++;
        }
    }
    hd_decode_worker(NULL); // this thread helps, and does everything when no worker started
    for (int i = 0; i < started; i++)
        SDL_WaitThread(threads[i], NULL);
    if (hd_decode_lock != NULL)
        SDL_DestroyMutex(hd_decode_lock);
    hd_decode_lock = NULL;
    free(hd_art);
    hd_art = NULL;
    for (size_t t = 0; t < hd_task_count; t++)
    {
        const struct HdSheetTask *task = &hd_tasks[t];
        const struct HdSource *src = task->src;
        const char *sheet = src->sheets[src->entries[task->order[task->first]].sheet];
        if (task->status == 0 || task->status == 3)
            hd_stats.sheets++;
        if (task->status == 2)
            WARNMSG("HD textures: can't decode \"%s/%s\"; its blocks stay 32x32", src->dir, sheet);
        else if (task->status == 3)
            WARNMSG("HD textures: \"%s/%s\" is %ux%u, smaller than some of its blocks' crops; those stay 32x32", src->dir, sheet,
                task->w, task->h);
    }
    free(hd_tasks);
    hd_tasks = NULL;
    hd_task_count = 0;
    for (size_t i = 0; i < hd_source_count; i++)
        free(orders[i]);
    free(orders);
}

/** The generated-art task for a slot's block (one per distinct block: a key table over the tasks), added if new; -1
 *  when out of memory. */
static int32_t hd_gen_task_for(int64_t slot)
{
    if (hd_gen_keys == NULL)
    {
        hd_gen_keys = (uint64_t *)calloc(HD_KEY_SLOTS, sizeof(uint64_t));
        hd_gen_key_task = (int32_t *)calloc(HD_KEY_SLOTS, sizeof(int32_t));
    }
    uint64_t *keys = hd_gen_keys;
    int32_t *tasks_of = hd_gen_key_task;
    if ((keys == NULL) || (tasks_of == NULL) || (hd_gen_count >= HD_KEY_SLOTS / 2))
        return -1;
    const uint64_t key = texture_hd_block_key(block_ptrs[slot]);
    size_t i = (size_t)(key ^ (key >> 29)) & (HD_KEY_SLOTS - 1);
    while ((tasks_of[i] != 0) && (keys[i] != key))
        i = (i + 1) & (HD_KEY_SLOTS - 1);
    if (tasks_of[i] != 0)
        return tasks_of[i] - 1;
    if (hd_gen_count == hd_gen_capacity)
    {
        const size_t cap = (hd_gen_capacity == 0) ? 1024 : hd_gen_capacity * 2;
        struct HdGenTask *grown = (struct HdGenTask *)realloc(hd_gen_tasks, cap * sizeof(struct HdGenTask));
        if (grown == NULL)
            return -1;
        hd_gen_tasks = grown;
        hd_gen_capacity = cap;
    }
    struct HdGenTask *task = &hd_gen_tasks[hd_gen_count];
    task->slot = slot;
    task->key = key;
    task->art = 0;
    keys[i] = key;
    tasks_of[i] = (int32_t)(++hd_gen_count);
    return (int32_t)(hd_gen_count - 1);
}

/** Decodes the plan's blocks for the variations kept (within the cap) and fills block_hd. */
static void hd_plan_apply(const TbBool *keep_variation)
{
    const int64_t t0 = (int64_t)(SDL_GetTicksNS() / 1000);
    // The entries the kept variations need; decoding stops storing new blocks at the cap (hd_store_block()).
    for (int64_t idx = 0; idx < HD_SLOT_COUNT; idx++)
    {
        const uint8_t s = hd_plan_source[idx];
        if (s == 0)
            continue;
        if (!keep_variation[idx / TEXTURE_BLOCKS_COUNT])
        {
            hd_stats.slots_capped++;
            continue;
        }
        if (s == HD_PLAN_GENERATED)
            hd_plan_entry[idx] = hd_gen_task_for(idx);
        else
            hd_sources[s - 1].entry_needed[hd_plan_entry[idx]] = 1;
    }
    const int64_t t_gen = (int64_t)(SDL_GetTicksNS() / 1000);
    hd_decode_all();
    uint64_t generated_slots = 0;
    // Per tmap file, for the log.
    uint32_t file_hd[TEXTURE_VARIATIONS_COUNT][2];
    uint32_t file_blocks[TEXTURE_VARIATIONS_COUNT][2];
    memset(file_hd, 0, sizeof(file_hd));
    memset(file_blocks, 0, sizeof(file_blocks));
    for (int64_t idx = 0; idx < HD_SLOT_COUNT; idx++)
    {
        const int64_t f = idx / TEXTURE_BLOCKS_COUNT;
        const int64_t id = idx % TEXTURE_BLOCKS_COUNT;
        if (!hd_static_id(id))
            continue;
        const int letter = (id < TEXTURE_BLOCKS_STAT_COUNT_A) ? 0 : 1;
        if ((block_ptrs[idx] != NULL) && !block_is_uniform(block_ptrs[idx]))
            file_blocks[f][letter]++;
        const uint8_t s = hd_plan_source[idx];
        if ((s == 0) || !keep_variation[f])
            continue;
        if (s == HD_PLAN_GENERATED)
        {
            const int32_t task = hd_plan_entry[idx];
            const int32_t art = (task >= 0) ? hd_gen_tasks[task].art : 0;
            if (art == 0)
            {
                hd_stats.slots_capped++;
                continue;
            }
            hd_set_slot(idx, &hd_arts[art - 1], TexHdSrc_Generated);
            hd_stats.slots_with_hd++;
            generated_slots++;
            continue;
        }
        const struct HdSource *src = &hd_sources[s - 1];
        const int32_t art = src->entry_art[hd_plan_entry[idx]];
        if (art == 0)
        {
            if (src->entry_needed[hd_plan_entry[idx]] == 3)
                hd_stats.slots_capped++;
            continue;
        }
        hd_set_slot(idx, &hd_arts[art - 1], src->content ? TexHdSrc_ContentFolder : TexHdSrc_UserPack);
        hd_stats.slots_with_hd++;
        file_hd[f][letter]++;
    }
    for (int64_t f = 0; f < TEXTURE_VARIATIONS_COUNT; f++)
        for (int letter = 0; letter < 2; letter++)
            if (file_hd[f][letter] > 0)
                JUSTLOG("HD textures: %s (%s): %u/%u blocks", texture_map_files[f][letter].stem, texture_map_files[f][letter].path,
                    file_hd[f][letter], file_blocks[f][letter]);
    if (hd_gen_count > 0)
        JUSTLOG("HD textures: generated art (ASSET_PACK_FILL) for %" PRIu64 " slots, %" PRIu64 " distinct blocks, with the packs' decoding in %" PRId64 " ms",
            generated_slots, (uint64_t)hd_gen_count, ((int64_t)(SDL_GetTicksNS() / 1000) - t_gen) / 1000);
    free(hd_gen_tasks);
    hd_gen_tasks = NULL;
    hd_gen_count = hd_gen_capacity = 0;
    free(hd_gen_keys);
    free(hd_gen_key_task);
    hd_gen_keys = NULL;
    hd_gen_key_task = NULL;
    if (hd_stats.slots_capped > 0)
        WARNMSG("HD textures: over the %" PRIu64 " MiB cap; %" PRIu64 " slots stay 32x32 (the variations the level doesn't use, then the "
            "last decoded)", hd_memory_cap >> 20, hd_stats.slots_capped);
    hd_stats.load_us += (int64_t)(SDL_GetTicksNS() / 1000) - t0;
    JUSTLOG("HD textures: %" PRIu64 " blocks for %" PRIu64 " of %" PRIu64 " slots from %" PRIu64 " sheets, %" PRIu64 " KiB, %" PRId64 " ms",
        hd_stats.blocks, hd_stats.slots_with_hd, hd_stats.slots_hashed, hd_stats.sheets, hd_stats.bytes / 1024, hd_stats.load_us / 1000);
    hd_plan_free();
    if (hd_stats.blocks > 0)
        RendererSetHdReferencePalette(hd_reference);
    // New pages: nothing may still be cached under a page address a previous load freed.
    RendererWorldFrameTexturesChanged();
}

/** The variations the level draws: its slabs' and each keeper's claim texture, and the default (0). */
static void hd_variations_in_use(TbBool *keep)
{
    memset(keep, 0, sizeof(TbBool) * TEXTURE_VARIATIONS_COUNT);
    keep[0] = true;
    const int64_t slabs = (int64_t)kfx_sim_state.map_tiles_x * (int64_t)kfx_sim_state.map_tiles_y;
    for (int64_t i = 0; (i < slabs) && (i < (int64_t)sizeof(kfx_config_state.slab_ext_data)); i++)
        keep[kfx_config_state.slab_ext_data[i] & 0x1F] = true;
    for (int i = 0; i < DUNGEONS_COUNT; i++)
        keep[kfx_sim_state.dungeon[i].texture_pack & 0x1F] = true;
}

/** Plans which pack entry each slot gets: a content folder beside its tmap file first, then the user pack. */
static void hd_load_packs(const char *user_pack)
{
    hd_plan_source = (uint8_t *)calloc(HD_SLOT_COUNT, 1);
    hd_plan_entry = (int32_t *)calloc(HD_SLOT_COUNT, sizeof(int32_t));
    if ((hd_plan_source == NULL) || (hd_plan_entry == NULL))
    {
        hd_plan_free();
        return;
    }
    size_t capacity = 0;
    uint8_t user = 0;
    char user_dir[512];
    if (asset_pack_dir(user_pack, "textures", user_dir, sizeof(user_dir)) && LbFileExists(user_dir))
        user = hd_source_for(user_dir, false, &capacity);
    uint8_t content[TEXTURE_VARIATIONS_COUNT][2];
    memset(content, 0, sizeof(content));
    for (int64_t f = 0; f < TEXTURE_VARIATIONS_COUNT; f++)
    {
        for (int letter = 0; letter < 2; letter++)
        {
            if (!texture_map_files[f][letter].loaded)
                continue;
            char dir[512];
            hd_content_dir(texture_map_files[f][letter].path, dir, sizeof(dir));
            if (LbFileExists(dir))
                content[f][letter] = hd_source_for(dir, true, &capacity);
        }
    }
    TbBool any_source = (user != 0);
    for (int64_t f = 0; f < TEXTURE_VARIATIONS_COUNT; f++)
        any_source = any_source || (content[f][0] != 0) || (content[f][1] != 0);
    // ASSET_PACK_FILL: the slots no pack covers get generated art (not in parity mode, a test of packs' exactness).
    const TbBool fill = (keeperfx_ui_config.asset_pack_fill != 0) && !hd_parity;
    if (!any_source && !fill)
    {
        hd_plan_free();
        return;
    }
    uint64_t *gen_keys = fill ? (uint64_t *)calloc(HD_KEY_SLOTS, sizeof(uint64_t)) : NULL; // distinct blocks, for the estimate
    uint64_t planned_bytes = 0;
    for (int64_t idx = 0; idx < HD_SLOT_COUNT; idx++)
    {
        const int64_t f = idx / TEXTURE_BLOCKS_COUNT;
        const int64_t id = idx % TEXTURE_BLOCKS_COUNT;
        if (!hd_static_id(id))
            continue;
        const unsigned char *block = block_ptrs[idx];
        if ((block == NULL) || block_is_uniform(block))
            continue;
        hd_stats.slots_hashed++;
        const int letter = (id < TEXTURE_BLOCKS_STAT_COUNT_A) ? 0 : 1;
        const uint8_t order[2] = { content[f][letter], user };
        if ((order[0] == 0) && (order[1] == 0) && !fill)
            continue;
        const uint64_t key = texture_hd_block_key(block);
        const char *stem_name = texture_map_files[f][letter].stem;
        const int16_t stem = (int16_t)(letter * HD_STEM_NUMBERS + atoi(stem_name + 5));
        const int16_t index = (int16_t)((letter == 0) ? id : (id - TEX_B_START_POINT));
        for (int k = 0; k < 2; k++)
        {
            if (order[k] == 0)
                continue;
            struct HdSource *src = &hd_sources[order[k] - 1];
            const int32_t first = hd_source_find(src, key);
            if (first < 0)
                continue;
            const int32_t e = hd_choose_entry(src, first, stem, index);
            hd_plan_source[idx] = order[k];
            hd_plan_entry[idx] = e;
            // The estimate counts a key once per source: its entries are usually the same art, which shares a page.
            if (!src->entry_needed[first])
            {
                src->entry_needed[first] = 2;
                planned_bytes += hd_block_bytes(src->shift);
            }
            break;
        }
        if ((hd_plan_source[idx] == 0) && fill)
        {
            hd_plan_source[idx] = HD_PLAN_GENERATED;
            hd_plan_entry[idx] = -1;
            if (gen_keys != NULL)
            {
                size_t i = (size_t)(key ^ (key >> 29)) & (HD_KEY_SLOTS - 1);
                while ((gen_keys[i] != 0) && (gen_keys[i] != key))
                    i = (i + 1) & (HD_KEY_SLOTS - 1);
                if (gen_keys[i] == 0)
                {
                    gen_keys[i] = key;
                    planned_bytes += hd_block_bytes(HD_GEN_SHIFT);
                }
            }
        }
    }
    free(gen_keys);
    for (size_t i = 0; i < hd_source_count; i++)
        if (hd_sources[i].entry_needed != NULL)
            memset(hd_sources[i].entry_needed, 0, hd_sources[i].entry_count + 1);
    if (planned_bytes <= hd_memory_cap)
    {
        TbBool all[TEXTURE_VARIATIONS_COUNT];
        for (int64_t f = 0; f < TEXTURE_VARIATIONS_COUNT; f++)
            all[f] = true;
        hd_plan_apply(all);
        return;
    }
    // Over the cap: the level's slab textures aren't loaded yet (load_level_file() reads them after the tmaps), so
    // the first drawn frame chooses the variations to keep.
    JUSTLOG("HD textures: the packs need %" PRIu64 " MiB, over the %" PRIu64 " MiB cap; keeping the variations in use at the first frame",
        planned_bytes >> 20, hd_memory_cap >> 20);
    hd_plan_pending = true;
}

/******************************************************************************/

static TbBool hd_load_reference_palette(void)
{
    const char *fname = prepare_file_fmtpath(FGrp_StdData, "palette.dat");
    if ((fname == NULL) || !LbFileExists(fname) || (LbFileLoadAtMax(fname, hd_reference, sizeof(hd_reference)) != (int64_t)sizeof(hd_reference)))
    {
        WARNMSG("HD textures: can't read the reference palette \"%s\"; drawing the 32x32 textures", (fname != NULL) ? fname : "palette.dat");
        return false;
    }
    return true;
}

/** Where a block of block_mem is: its variation's A blocks, then B, each 8 to a 256-wide row of 32x32 blocks.
 *  Its number among all of them, or -1 if `block` isn't in block_mem. */
static int64_t hd_static_block(const unsigned char *block)
{
    if ((block < block_mem) || (block >= block_mem + sizeof(block_mem)))
        return -1;
    const size_t off = (size_t)(block - block_mem);
    const size_t per_variation = (size_t)TEXTURE_BLOCKS_STAT_COUNT * 32 * 32;
    const size_t f = off / per_variation;
    size_t in = off % per_variation;
    size_t base = 0;
    if (in >= (size_t)TEXTURE_BLOCKS_STAT_COUNT_A * 32 * 32)
    {
        in -= (size_t)TEXTURE_BLOCKS_STAT_COUNT_A * 32 * 32;
        base = TEXTURE_BLOCKS_STAT_COUNT_A;
    }
    const size_t row = in / (HD_ATLAS_STRIDE * 32);
    const size_t col = (in % HD_ATLAS_STRIDE) / 32;
    return (int64_t)(f * TEXTURE_BLOCKS_STAT_COUNT + base + row * 8 + col);
}

/** The indexed mip level k (16 >> k texels a side) of block number b (hd_static_block()). */
static unsigned char *hd_index_low_block(int k, int64_t b)
{
    const int side = 16 >> k;
    const int64_t per_page = HD_ATLAS_STRIDE / side;
    return hd_index_low[k] + (size_t)(b / per_page) * HD_ATLAS_STRIDE * side + (size_t)(b % per_page) * side;
}

/** Every block's indexed mip levels below 32x32 (TEXTURE_MIPMAPS): colours averaged through the reference
 *  palette, black for index 0, back to the nearest entry; index 0 only where all four were. */
static void hd_build_index_low(void)
{
    uint8_t *nearest = (uint8_t *)malloc(COLOUR_TRANSFORM_TABLE_SIZE);
    if (nearest == NULL)
        return;
    colour_transform_nearest_entries(hd_reference, nearest);
    for (int k = 0; k < TEXTURE_HD_LOW_LEVELS; k++)
    {
        const int side = 16 >> k;
        const size_t pages = (HD_STATIC_BLOCKS + (HD_ATLAS_STRIDE / side) - 1) / (HD_ATLAS_STRIDE / side);
        hd_index_low[k] = (unsigned char *)calloc(pages * HD_ATLAS_STRIDE * side, 1);
        if (hd_index_low[k] == NULL)
        {
            free(nearest);
            for (int j = 0; j <= k; j++)
            {
                free(hd_index_low[j]);
                hd_index_low[j] = NULL;
            }
            return;
        }
        hd_stats.low_bytes += pages * HD_ATLAS_STRIDE * side;
    }
    for (int64_t b = 0; b < HD_STATIC_BLOCKS; b++)
    {
        const int64_t f = b / TEXTURE_BLOCKS_STAT_COUNT;
        const int64_t local = b % TEXTURE_BLOCKS_STAT_COUNT;
        const int64_t id = (local < TEXTURE_BLOCKS_STAT_COUNT_A) ? local : (TEX_B_START_POINT + local - TEXTURE_BLOCKS_STAT_COUNT_A);
        const unsigned char *above = block_ptrs[f * TEXTURE_BLOCKS_COUNT + id];
        for (int k = 0; k < TEXTURE_HD_LOW_LEVELS; k++)
        {
            const int side = 16 >> k;
            unsigned char *dst = hd_index_low_block(k, b);
            for (int y = 0; y < side; y++)
            {
                for (int x = 0; x < side; x++)
                {
                    const unsigned char i[4] = { above[(2 * y) * HD_ATLAS_STRIDE + 2 * x], above[(2 * y) * HD_ATLAS_STRIDE + 2 * x + 1],
                        above[(2 * y + 1) * HD_ATLAS_STRIDE + 2 * x], above[(2 * y + 1) * HD_ATLAS_STRIDE + 2 * x + 1] };
                    if ((i[0] == i[1]) && (i[1] == i[2]) && (i[2] == i[3]))
                    {
                        dst[y * HD_ATLAS_STRIDE + x] = i[0];
                        continue;
                    }
                    int c[3] = { 0, 0, 0 };
                    for (int n = 0; n < 4; n++)
                        for (int ch = 0; ch < 3; ch++)
                            c[ch] += (i[n] == 0) ? 0 : hd_reference[3 * i[n] + ch];
                    // 6-bit palette channels: the average, rounded, is the table's own 6-bit cell
                    const int r6 = (c[0] + 2) >> 2, g6 = (c[1] + 2) >> 2, b6 = (c[2] + 2) >> 2;
                    dst[y * HD_ATLAS_STRIDE + x] = nearest[(r6 << 12) | (g6 << 6) | b6];
                }
            }
            above = dst;
        }
    }
    free(nearest);
}

static void hd_clear_store(void)
{
    hd_plan_free();
    for (size_t i = 0; i < hd_page_count; i++)
        free(hd_pages[i].pixels);
    free(hd_pages);
    hd_pages = NULL;
    hd_page_count = 0;
    hd_page_capacity = 0;
    free(hd_arts);
    hd_arts = NULL;
    hd_art_count = 0;
    hd_art_capacity = 0;
    hd_finest_shift = 0;
    for (int k = 0; k < TEXTURE_HD_LOW_LEVELS; k++)
    {
        free(hd_index_low[k]);
        hd_index_low[k] = NULL;
    }
    hd_low = false;
    memset(block_hd, 0, sizeof(block_hd));
    memset(&hd_stats, 0, sizeof(hd_stats));
    hd_parity = false;
    hd_draw = false;
    RendererSetHdReferencePalette(NULL);
}

void texture_hd_clear(void)
{
    hd_clear_store();
    hd_tmap_loaded = false;
}

/** The memory the terrain packs may take: a quarter of ASSET_PACK_MEMORY (asset_pack_memory_budget()), or the
 *  tests' override. */
static uint64_t hd_cap(void)
{
    return (hd_memory_cap_override > 0) ? hd_memory_cap_override : asset_pack_memory_budget() / 4;
}

void texture_hd_load(void)
{
    hd_clear_store();
    hd_tmap_loaded = true;
    hd_memory_cap = hd_cap();
    hd_loaded_fill = (keeperfx_ui_config.asset_pack_fill != 0);
    snprintf(hd_loaded_pack, sizeof(hd_loaded_pack), "%s", keeperfx_ui_config.asset_pack);
    // TEXTURE_MIPMAPS: every block's indexed levels below 32, and an HD pack's own below its 32 level.
    if (RendererGetTextureMipmaps() && hd_load_reference_palette())
    {
        const int64_t t0 = (int64_t)(SDL_GetTicksNS() / 1000);
        hd_low = true;
        hd_build_index_low();
        hd_stats.load_us += (int64_t)(SDL_GetTicksNS() / 1000) - t0;
    }
    const char *synth = getenv("KFX_TEXTURE_HD_SYNTH");
    const int synth_shift = (synth != NULL) ? atoi(synth) : 0;
    const TbBool synthetic = (synth_shift >= 1) && (synth_shift <= 3);
    if (!synthetic && (strcasecmp(hd_loaded_pack, "NONE") == 0) && !hd_loaded_fill)
    {
        // No user pack and nothing generated: content folders only, and only if there is one beside a loaded tmap file.
        TbBool any = false;
        for (int64_t f = 0; (f < TEXTURE_VARIATIONS_COUNT) && !any; f++)
        {
            for (int letter = 0; (letter < 2) && !any; letter++)
            {
                if (!texture_map_files[f][letter].loaded)
                    continue;
                char dir[512];
                hd_content_dir(texture_map_files[f][letter].path, dir, sizeof(dir));
                any = LbFileExists(dir);
            }
        }
        if (!any)
            return;
    }
    const int64_t t0 = (int64_t)(SDL_GetTicksNS() / 1000);
    if (!hd_load_reference_palette())
        return;
    // Parity (drawn as the 32x32 block would be) is the synthetic store's; a pack draws in precision mode. Either
    // can be forced, for the equivalence gates (03 section B).
    hd_parity = synthetic;
    if (getenv("KFX_TEXTURE_HD_PRECISION") != NULL)
        hd_parity = false;
    if (getenv("KFX_TEXTURE_HD_PARITY") != NULL)
        hd_parity = true;
    if (synthetic)
    {
        hd_load_synthetic((uint8_t)synth_shift);
        if (hd_stats.blocks > 0)
            RendererSetHdReferencePalette(hd_reference);
        hd_stats.load_us += (int64_t)(SDL_GetTicksNS() / 1000) - t0;
        JUSTLOG("HD textures: synthetic x%d (%s mode): %" PRIu64 " blocks for %" PRIu64 " of %" PRIu64 " slots, %" PRIu64 " KiB, %" PRId64 " ms",
            1 << synth_shift, hd_parity ? "parity" : "precision", hd_stats.blocks, hd_stats.slots_with_hd, hd_stats.slots_hashed,
            hd_stats.bytes / 1024, hd_stats.load_us / 1000);
        return;
    }
    hd_load_packs(hd_loaded_pack);
    hd_stats.load_us += (int64_t)(SDL_GetTicksNS() / 1000) - t0;
}

void texture_hd_copy(int64_t dst, int64_t src)
{
    block_hd[dst] = block_hd[src];
}

void texture_hd_frame_begin(void)
{
    if (hd_tmap_loaded && ((strcasecmp(keeperfx_ui_config.asset_pack, hd_loaded_pack) != 0) || (hd_cap() != hd_memory_cap) ||
        ((keeperfx_ui_config.asset_pack_fill != 0) != hd_loaded_fill) ||
        ((RendererGetTextureMipmaps() ? 1 : 0) != (hd_low ? 1 : 0))))
        texture_hd_reload();
    if (hd_plan_pending)
    {
        TbBool keep[TEXTURE_VARIATIONS_COUNT];
        hd_variations_in_use(keep);
        hd_plan_apply(keep);
        texture_hd_sync_animation();
    }
    hd_draw = (hd_stats.blocks > 0) && RendererHdUsable();
}

const TbPixel *texture_hd_block(int64_t block, int64_t *shift)
{
    if (!hd_draw || (block < 0) || (block >= HD_SLOT_COUNT))
        return NULL;
    const struct TextureHd *e = &block_hd[block];
    if (e->page == NULL)
        return NULL;
    *shift = e->shift;
    hd_stats.draws++;
    return e->page;
}

TbBool texture_hd_parity(void)
{
    return hd_parity;
}

void texture_hd_select(int64_t block, int lod, struct TextureHdDraw *out)
{
    out->indexed = NULL;
    out->rgba = NULL;
    out->shift = 0;
    out->parity = false;
    out->original_at_32 = false;
    if ((block < 0) || (block >= HD_SLOT_COUNT))
        return;
    const struct TextureHd *e = (hd_draw && (block_hd[block].page != NULL)) ? &block_hd[block] : NULL;
    if (lod > 3)
        lod = 3;
    if (lod < (hd_low ? -TEXTURE_HD_LOW_LEVELS : 0))
        lod = hd_low ? -TEXTURE_HD_LOW_LEVELS : 0;
    if ((lod > 0) && (e != NULL))
    {
        const int s = (lod < e->shift) ? lod : e->shift;
        out->rgba = e->level[s];
        out->shift = s;
        out->parity = hd_parity;
        out->original_at_32 = (e->level[0] == NULL);
        hd_stats.draws++;
        return;
    }
    if (lod >= 0)
    {
        if ((e != NULL) && (e->level[0] != NULL))
        {
            out->rgba = e->level[0];
            hd_stats.draws++;
            return;
        }
        out->indexed = block_ptrs[block];
        return;
    }
    if ((e != NULL) && (e->low[-lod - 1] != NULL))
    {
        out->rgba = e->low[-lod - 1];
        out->shift = lod;
        hd_stats.draws++;
        return;
    }
    const int64_t b = hd_static_block(block_ptrs[block]);
    if ((b < 0) || (hd_index_low[-lod - 1] == NULL))
    {
        out->indexed = block_ptrs[block];
        return;
    }
    out->indexed = hd_index_low_block(-lod - 1, b);
    out->shift = lod;
}

int texture_hd_lod_for_texel(double texel_px)
{
    const int lowest = hd_low ? -TEXTURE_HD_LOW_LEVELS : 0;
    if (!(texel_px > 0.0))
        return 0;
    // round(log2(texel_px)): the level whose texel is nearest a pixel (0.71 to 1.41 px), as a GPU's nearest mip
    // picks it
    int lod = (int)floor(log2(texel_px) + 0.5);
    if (lod > 3)
        lod = 3;
    if (lod < lowest)
        lod = lowest;
    return lod;
}

TbBool texture_hd_low_levels(void)
{
    return hd_low;
}

int texture_hd_uv_shift(void)
{
    return (hd_draw && !hd_parity) ? hd_finest_shift : 0;
}

TbBool texture_hd_lod_matters(void)
{
    return hd_draw || hd_low;
}

enum TextureHdSource texture_hd_slot_source(int64_t block)
{
    if ((block < 0) || (block >= HD_SLOT_COUNT) || (block_hd[block].page == NULL))
        return TexHdSrc_None;
    return (enum TextureHdSource)block_hd[block].source;
}

void texture_hd_set_memory_cap(uint64_t bytes)
{
    hd_memory_cap_override = bytes;
}

const struct TextureHdStats *texture_hd_stats(void)
{
    return &hd_stats;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
