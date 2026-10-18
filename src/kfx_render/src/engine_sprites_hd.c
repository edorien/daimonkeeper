/******************************************************************************/
// Free implementation of the Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_sprites_hd.c
 *     HD sprite packs: the content key of a sprite frame, and the sprite draw audit.
 * @par Purpose:
 *     See engine_sprites_hd.h.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     10 Oct 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "engine_sprites_hd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_fileio.h"
#include "bflib_video.h"
#include "keeper_sprites.h"
#include "engine_render.h"
#include "vidmode.h"
#include "renderer/RendererManager.h"
#include "bflib_vidraw.h"
#include "engine_sprites_hd_pack.h"
#include "custom_sprites.h"
#include "engine_textures_hd.h"
#include "config.h"
#include "config_keeperfx.h"
#include <strings.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_cpuinfo.h>
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define FNV_OFFSET 14695981039346656037ULL
#define FNV_PRIME 1099511628211ULL

/** An original frame's HD version: its own levels (the synthetic store), or a pack entry (engine_sprites_hd_pack.c). */
struct SpriteHdFrame {
    struct SpriteHdLevels syn;    ///< HdF_Synthetic
    uint64_t uid;                 ///< HdF_Synthetic: never reused (the GPU caches the texels under it)
    int32_t entry;                ///< HdF_Pack: the pack entry
    uint8_t state;                ///< HdF_*
};
enum SpriteHdFrameState {
    HdF_Untried = 0,
    HdF_Synthetic,
    HdF_Pack,
    HdF_None,
};

static struct SpriteHdFrame *hd_orig;   ///< KEEPSPRITE_LENGTH entries, by creature_table index
static struct SpriteHdFrame *hd_custom; ///< KEEPERSPRITE_ADD_NUM entries, by custom slot
static struct SpriteHdFrame *hd_orig_alpha;   ///< as hd_orig, for alpha-ramp draws (additive art)
static struct SpriteHdFrame *hd_custom_alpha; ///< as hd_custom, for alpha-ramp draws
static int hd_synth_shift;              ///< KFX_SPRITE_HD_SYNTH: 0 none, 1 x2, 2 x4
static TbBool hd_parity_mode;           ///< synthetic: unless KFX_SPRITE_HD_PRECISION; packs: KFX_SPRITE_HD_PARITY
static TbBool hd_synth_pattern;         ///< KFX_SPRITE_HD_SYNTH_PATTERN: invert every texel but the one parity samples
static TbBool hd_sync;                  ///< KFX_SPRITE_HD_SYNC: a pack is decoded whole before the next frame draws
static TbBool hd_env_read;
static TbBool hd_draw_frame;            ///< HD sprites draw in this frame
static char hd_loaded_pack[64];         ///< ASSET_PACK as loaded ("" before the first frame)
static char **hd_content;               ///< sprite_hd_set_content_folders(), in load order
static int64_t hd_content_count;
static TbBool hd_content_changed;
static uint64_t hd_frame_counter;
static uint64_t hd_memory_cap_override; ///< sprite_hd_set_memory_cap() (tests); 0: ASSET_PACK_MEMORY's share
static struct SpriteHdStats hd_stats;
static uint64_t hd_next_uid = 1;
static TbBool hd_loaded_fill;            ///< ASSET_PACK_FILL as the packs were last opened with
// Generated art's keys: the original frame's, apart from the packs' (and from each other: its colours from the
// palette or a zip PNG's true colours, or additive art).
#define HD_GEN_SALT_COLOUR 0x5BD1E9955BD1E995ULL
#define HD_GEN_SALT_TRUE_COLOUR 0xC2B2AE3D27D4EB4FULL
#define HD_GEN_SALT_ADDITIVE 0x165667B19E3779F9ULL

/** A custom slot filled from a zip PNG (Phase 6). */
struct SpriteHdZipFrame {
    char *zip;
    char *png;
    uint64_t key;               ///< of the quantised frame (0: none)
    int64_t w, h;
    struct SpriteHdLevels tc;   ///< SPRITE_TRUE_COLOUR: one level, scale 1, the PNG's colours (alpha 0 or 255)
    uint64_t tc_uid;
};
static struct SpriteHdZipFrame *hd_zip;   ///< KEEPERSPRITE_ADD_NUM entries, by custom slot
static int64_t hd_zip_tc_count;           ///< slots with true colours
static int64_t hd_tc_loaded = -1;         ///< SPRITE_TRUE_COLOUR the true colours were made for (-1: not yet)
static TbBool hd_tc_quantised;            ///< KFX_SPRITE_TC_QUANTISED: the control, true colours = the quantised frame's
static uint64_t hd_tc_next_uid = 1;
static TbBool hd_zip_pending;             ///< slots loaded without the true colours SPRITE_TRUE_COLOUR wants

static TbBool hd_decode(const unsigned char *rle, int64_t w, int64_t h, unsigned char *idx);

static int audit_enabled;
static struct SpriteAuditStats audit_cur;
static struct SpriteAuditStats audit_last;
/******************************************************************************/

uint64_t sprite_hd_frame_key(const unsigned char *rle, size_t rle_len, int64_t width, int64_t height)
{
    unsigned char row[SPRITE_HD_KEY_MAX_WIDTH];
    if ((rle == NULL) || (width <= 0) || (height <= 0) || (width > SPRITE_HD_KEY_MAX_WIDTH) || (width > 0xFFFF) || (height > 0xFFFF))
        return 0;
    uint64_t h = FNV_OFFSET;
    const unsigned char dims[4] = { (unsigned char)(width & 0xFF), (unsigned char)(width >> 8),
        (unsigned char)(height & 0xFF), (unsigned char)(height >> 8) };
    for (int i = 0; i < 4; i++)
        h = (h ^ dims[i]) * FNV_PRIME;
    size_t p = 0;
    for (int64_t y = 0; y < height; y++)
    {
        memset(row, 0, (size_t)width);
        int64_t x = 0;
        for (;;)
        {
            if (p >= rle_len)
                return 0;
            const int64_t n = (signed char)rle[p++];
            if (n == 0)
                break;
            if (n < 0)
            {
                x -= n;
                continue;
            }
            if (p + (size_t)n > rle_len)
                return 0;
            for (int64_t k = 0; k < n; k++, x++)
                if ((x >= 0) && (x < width))
                    row[x] = rle[p + k];
            p += (size_t)n;
        }
        for (int64_t i = 0; i < width; i++)
            h = (h ^ row[i]) * FNV_PRIME;
    }
    return h;
}

int64_t sprite_hd_dump_keys(const char *path)
{
    if ((jty_file_handle == NULL) || (creature_table == NULL))
        return -1;
    FILE *f = fopen(path, "w");
    if (f == NULL)
        return -1;
    int64_t written = 0;
    unsigned char *buf = NULL;
    size_t buf_len = 0;
    // The last table entry only ends the one before it (load_single_frame() reads entry i+1's offset).
    for (size_t i = 0; i + 1 < creature_table_length; i++)
    {
        const struct KeeperSprite *kspr = &creature_table[i];
        uint64_t key = 0;
        const uint64_t len = creature_table[i + 1].DataOffset - kspr->DataOffset;
        if ((kspr->SWidth > 0) && (kspr->SHeight > 0) && (creature_table[i + 1].DataOffset > kspr->DataOffset))
        {
            if (len > buf_len)
            {
                unsigned char *nbuf = realloc(buf, len);
                if (nbuf == NULL)
                    break;
                buf = nbuf;
                buf_len = len;
            }
            if ((LbFileSeek(jty_file_handle, (int64_t)kspr->DataOffset, Lb_FILE_SEEK_BEGINNING) >= 0)
                && (LbFileRead(jty_file_handle, buf, len) == (int64_t)len))
                key = sprite_hd_frame_key(buf, len, kspr->SWidth, kspr->SHeight);
        }
        fprintf(f, "orig %" PRIu64 " %" PRId64 " %" PRId64 " %016" PRIx64 "\n", (uint64_t)i, kspr->SWidth, kspr->SHeight, key);
        written++;
    }
    free(buf);
    for (int64_t i = 0; i < KEEPERSPRITE_ADD_NUM; i++)
    {
        if (keepersprite_add[i] == NULL)
            continue;
        const struct KeeperSprite *kspr = &creature_table_add[i];
        // Custom frames are whole RLE buffers of their own; the rows end the read.
        const uint64_t key = sprite_hd_frame_key(keepersprite_add[i], SIZE_MAX, kspr->SWidth, kspr->SHeight);
        fprintf(f, "add %" PRId64 " %" PRId64 " %" PRId64 " %016" PRIx64 "\n", i, kspr->SWidth, kspr->SHeight, key);
        written++;
    }
    fclose(f);
    return written;
}

/******************************************************************************/

static void hd_reset_frames(struct SpriteHdFrame *frames, int64_t count)
{
    if (frames == NULL)
        return;
    for (int64_t i = 0; i < count; i++)
    {
        sprite_hd_free_levels(&frames[i].syn);
        memset(&frames[i], 0, sizeof(frames[i]));
    }
}

void sprite_hd_forget_custom(void)
{
    hd_reset_frames(hd_custom, KEEPERSPRITE_ADD_NUM);
    hd_reset_frames(hd_custom_alpha, KEEPERSPRITE_ADD_NUM);
}

/******************************************************************************/
// Mods' zip frames (docs/refactor/sprites/02-design.md section 9)

static void hd_zip_free_tc(struct SpriteHdZipFrame *z)
{
    if (z->tc.levels > 0)
    {
        hd_stats.zip_bytes -= z->tc.bytes;
        hd_stats.zip_frames--;
        hd_zip_tc_count--;
    }
    sprite_hd_free_levels(&z->tc);
}

/** True colours from the PNG's RGBA (w x h): compress_raw()'s mask (alpha below 0x40 transparent), opaque elsewhere. */
static void hd_zip_set_tc(struct SpriteHdZipFrame *z, const unsigned char *rgba)
{
    hd_zip_free_tc(z);
    TbPixel *top = malloc((size_t)(z->w * z->h) * sizeof(TbPixel));
    if (top == NULL)
        return;
    for (int64_t i = 0; i < z->w * z->h; i++)
    {
        const unsigned char *px = rgba + i * 4;
        top[i] = (px[3] >= 0x40) ? TbPixel_RGBA(px[0], px[1], px[2], 255) : TbPixel_Transparent;
    }
    sprite_hd_build_levels(&z->tc, top, z->w, z->h, 1, false);
    z->tc_uid = hd_tc_next_uid++;
    hd_stats.zip_bytes += z->tc.bytes;
    hd_stats.zip_frames++;
    hd_zip_tc_count++;
}

/** The control's true colours: the quantised frame's own, through the reference palette (parity mode must then draw
 *  exactly the quantised frame). */
static void hd_zip_set_tc_quantised(struct SpriteHdZipFrame *z, const unsigned char *rle)
{
    hd_zip_free_tc(z);
    const unsigned char *palette = RendererHdReferencePalette();
    if ((palette == NULL) || (rle == NULL))
        return;
    unsigned char *idx = malloc((size_t)(z->w * z->h));
    TbPixel *top = malloc((size_t)(z->w * z->h) * sizeof(TbPixel));
    if ((idx == NULL) || (top == NULL))
    {
        free(idx);
        free(top);
        return;
    }
    hd_decode(rle, z->w, z->h, idx);
    for (int64_t i = 0; i < z->w * z->h; i++)
        top[i] = (idx[i] != 0) ? resolve_indexed_pixel(idx[i], palette) : TbPixel_Transparent;
    free(idx);
    sprite_hd_build_levels(&z->tc, top, z->w, z->h, 1, false);
    z->tc_uid = hd_tc_next_uid++;
    hd_stats.zip_bytes += z->tc.bytes;
    hd_stats.zip_frames++;
    hd_zip_tc_count++;
}

/** The KFX_SPRITE_HD_* and KFX_SPRITE_TC_QUANTISED switches, read once (the zips load before the first frame). */
static void hd_read_env(void)
{
    if (hd_env_read)
        return;
    hd_env_read = true;
    const char *synth = getenv("KFX_SPRITE_HD_SYNTH");
    hd_sync = (getenv("KFX_SPRITE_HD_SYNC") != NULL);
    if ((synth != NULL) && ((atoi(synth) == 1) || (atoi(synth) == 2)))
    {
        hd_synth_shift = atoi(synth);
        hd_parity_mode = (getenv("KFX_SPRITE_HD_PRECISION") == NULL);
        hd_synth_pattern = (getenv("KFX_SPRITE_HD_SYNTH_PATTERN") != NULL);
        JUSTLOG("HD sprites: synthetic x%d%s, %s mode", 1 << hd_synth_shift, hd_synth_pattern ? " with the sub-texel pattern" : "",
            hd_parity_mode ? "parity" : "precision");
    }
    else
    {
        hd_parity_mode = (getenv("KFX_SPRITE_HD_PARITY") != NULL);
    }
    hd_tc_quantised = (getenv("KFX_SPRITE_TC_QUANTISED") != NULL);
}

void sprite_hd_zip_frame_loaded(int64_t slot, const unsigned char *rgba, int64_t w, int64_t h, const char *zip, const char *png)
{
    if ((slot < 0) || (slot >= KEEPERSPRITE_ADD_NUM) || (w <= 0) || (h <= 0))
        return;
    if (hd_zip == NULL)
        hd_zip = calloc(KEEPERSPRITE_ADD_NUM, sizeof(struct SpriteHdZipFrame));
    if (hd_zip == NULL)
        return;
    hd_read_env();
    struct SpriteHdZipFrame *z = &hd_zip[slot];
    hd_zip_free_tc(z);
    free(z->zip);
    free(z->png);
    z->zip = (zip != NULL) ? strdup(zip) : NULL;
    z->png = (png != NULL) ? strdup(png) : NULL;
    z->w = w;
    z->h = h;
    z->key = sprite_hd_frame_key(keepersprite_add[slot], SIZE_MAX, w, h);
    // The control's colours need the reference palette: they are made at the next frame's begin.
    if (keeperfx_ui_config.sprite_true_colour != 0)
    {
        if ((rgba != NULL) && !hd_tc_quantised)
            hd_zip_set_tc(z, rgba);
        else
            hd_zip_pending = true;
    }
}

void sprite_hd_zip_frames_clear(void)
{
    if (hd_zip == NULL)
        return;
    for (int64_t i = 0; i < KEEPERSPRITE_ADD_NUM; i++)
    {
        hd_zip_free_tc(&hd_zip[i]);
        free(hd_zip[i].zip);
        free(hd_zip[i].png);
        memset(&hd_zip[i], 0, sizeof(hd_zip[i]));
    }
}

TbBool sprite_hd_zip_frame(int64_t slot, uint64_t *key, const char **zip, const char **png)
{
    if ((hd_zip == NULL) || (slot < 0) || (slot >= KEEPERSPRITE_ADD_NUM) || (hd_zip[slot].zip == NULL))
        return false;
    if (key != NULL)
        *key = hd_zip[slot].key;
    if (zip != NULL)
        *zip = hd_zip[slot].zip;
    if (png != NULL)
        *png = hd_zip[slot].png;
    return true;
}

const TbPixel *sprite_hd_zip_true_colour(int64_t slot)
{
    if ((hd_zip == NULL) || (slot < 0) || (slot >= KEEPERSPRITE_ADD_NUM) || (hd_zip[slot].tc.levels == 0))
        return NULL;
    return hd_zip[slot].tc.level[0];
}

/** SPRITE_TRUE_COLOUR changed, or slots came without their colours (the control, or loaded while it was off): every
 *  recorded slot gets its true colours (the zip PNGs read again), or loses them. */
static void hd_zip_sync_true_colour(void)
{
    const int64_t want = (keeperfx_ui_config.sprite_true_colour != 0) ? 1 : 0;
    if (hd_zip == NULL)
    {
        hd_tc_loaded = want;
        return;
    }
    const uint64_t t0 = SDL_GetTicksNS();
    int64_t made = 0, failed = 0;
    for (int64_t i = 0; i < KEEPERSPRITE_ADD_NUM; i++)
    {
        struct SpriteHdZipFrame *z = &hd_zip[i];
        if (z->zip == NULL)
            continue;
        if (!want)
        {
            hd_zip_free_tc(z);
            continue;
        }
        if (z->tc.levels > 0)
            continue;
        if (hd_tc_quantised)
        {
            hd_zip_set_tc_quantised(z, keepersprite_add[i]);
        }
        else
        {
            int64_t w = 0, h = 0;
            unsigned char *rgba = custom_sprite_png_rgba(z->zip, z->png, &w, &h);
            if ((rgba != NULL) && (w == z->w) && (h == z->h))
                hd_zip_set_tc(z, rgba);
            else
                failed++;
            free(rgba);
        }
        made += (z->tc.levels > 0);
    }
    custom_sprite_png_rgba(NULL, NULL, NULL, NULL); // closes the last zip
    if (want && (made + failed > 0))
        JUSTLOG("HD sprites: %" PRId64 " zip frames in true colour%s (%" PRId64 " ms, %" PRIu64 " KiB, %" PRId64 " unreadable)", made,
            hd_tc_quantised ? " (the quantised control)" : "", (int64_t)((SDL_GetTicksNS() - t0) / 1000000), hd_stats.zip_bytes >> 10, failed);
    hd_tc_loaded = want;
}

void sprite_hd_note_alpha_frame(int64_t kspr_idx)
{
    static const char *path;
    static TbBool path_read;
    static unsigned char *seen;
    if (!path_read)
    {
        path_read = true;
        path = getenv("KFX_SPRITE_ALPHA_LOG");
    }
    if ((path == NULL) || (kspr_idx < 0) || (kspr_idx >= KEEPERSPRITE_ADD_OFFSET + KEEPERSPRITE_ADD_NUM))
        return;
    if (seen == NULL)
        seen = calloc((size_t)(KEEPERSPRITE_ADD_OFFSET + KEEPERSPRITE_ADD_NUM) / 8 + 1, 1);
    if ((seen == NULL) || (seen[kspr_idx / 8] & (1 << (kspr_idx % 8))))
        return;
    seen[kspr_idx / 8] |= (unsigned char)(1 << (kspr_idx % 8));
    FILE *f = fopen(path, "a");
    if (f == NULL)
        return;
    fprintf(f, "%" PRId64 "\n", kspr_idx);
    fclose(f);
}

const struct SpriteHdStats *sprite_hd_stats(void)
{
    hd_stats.entries = (uint64_t)sprite_pack_entry_count();
    hd_stats.failed = (uint64_t)sprite_pack_failed();
    hd_stats.decode_us = sprite_pack_decode_us();
    if (hd_synth_shift == 0)
        hd_stats.bytes = sprite_pack_bytes();
    return &hd_stats;
}

TbBool sprite_hd_parity(void)
{
    return hd_parity_mode;
}

void sprite_hd_set_memory_cap(uint64_t bytes)
{
    hd_memory_cap_override = bytes;
}

uint64_t asset_pack_memory_budget(void)
{
    const int ram_mib = SDL_GetSystemRAM();
    return asset_pack_memory_bytes((ram_mib > 0) ? ((uint64_t)ram_mib << 20) : 0);
}

/** The memory decoded pack frames may take: ASSET_PACK_MEMORY less what the terrain textures took (at least half of
 *  it), or the tests' override. */
static uint64_t hd_cap(void)
{
    if (hd_memory_cap_override > 0)
        return hd_memory_cap_override;
    const uint64_t budget = asset_pack_memory_budget();
    const uint64_t terrain = texture_hd_stats()->bytes + texture_hd_stats()->low_bytes;
    return (terrain < budget / 2) ? budget - terrain : budget / 2;
}

void sprite_hd_set_content_folders(const char *const *dirs, int64_t count)
{
    TbBool same = (count == hd_content_count);
    for (int64_t i = 0; same && (i < count); i++)
        same = (strcmp(dirs[i], hd_content[i]) == 0);
    if (same)
        return;
    for (int64_t i = 0; i < hd_content_count; i++)
        free(hd_content[i]);
    free(hd_content);
    hd_content = NULL;
    hd_content_count = 0;
    if (count > 0)
    {
        hd_content = (char **)calloc((size_t)count, sizeof(char *));
        for (int64_t i = 0; (hd_content != NULL) && (i < count); i++)
            if ((hd_content[hd_content_count] = strdup(dirs[i])) != NULL)
                hd_content_count++;
    }
    hd_content_changed = true;
}

int64_t sprite_hd_content_folder_count(void)
{
    return hd_content_count;
}

const char *sprite_hd_content_folder(int64_t i)
{
    return ((i >= 0) && (i < hd_content_count)) ? hd_content[i] : NULL;
}

/** Whether frames no pack covers get generated art (ASSET_PACK_FILL; not in the test modes, parity or the synthetic
 *  store). */
static TbBool hd_fill_active(void)
{
    return (keeperfx_ui_config.asset_pack_fill != 0) && (hd_synth_shift == 0) && !hd_parity_mode;
}

/** ASSET_PACK or the content folders changed (or the first frame): the packs opened again, most specific first,
 *  and every frame looked up again. */
static void hd_reload_packs(void)
{
    snprintf(hd_loaded_pack, sizeof(hd_loaded_pack), "%s", keeperfx_ui_config.asset_pack);
    hd_loaded_fill = hd_fill_active();
    hd_content_changed = false;
    hd_reset_frames(hd_orig, KEEPSPRITE_LENGTH);
    hd_reset_frames(hd_custom, KEEPERSPRITE_ADD_NUM);
    hd_reset_frames(hd_orig_alpha, KEEPSPRITE_LENGTH);
    hd_reset_frames(hd_custom_alpha, KEEPERSPRITE_ADD_NUM);
    const uint64_t zip_frames = hd_stats.zip_frames, zip_bytes = hd_stats.zip_bytes; // the true colours stay
    memset(&hd_stats, 0, sizeof(hd_stats));
    hd_stats.zip_frames = zip_frames;
    hd_stats.zip_bytes = zip_bytes;
    const char **dirs = (const char **)calloc((size_t)hd_content_count + 1, sizeof(char *));
    TbBool *content = (TbBool *)calloc((size_t)hd_content_count + 1, sizeof(TbBool));
    if ((dirs == NULL) || (content == NULL))
    {
        free(dirs);
        free(content);
        sprite_pack_reopen(NULL, NULL, 0);
        return;
    }
    int64_t n = 0;
    for (int64_t k = hd_content_count - 1; k >= 0; k--)
    {
        dirs[n] = hd_content[k];
        content[n++] = true;
    }
    char user[1024];
    if (asset_pack_dir(hd_loaded_pack, "sprites", user, sizeof(user)) && LbFileExists(user))
    {
        dirs[n] = user;
        content[n++] = false;
    }
    const uint64_t t0 = SDL_GetTicksNS();
    const int64_t indexed = sprite_pack_reopen(dirs, content, n);
    free(dirs);
    free(content);
    if (indexed <= 0)
        return;
    // Everything is loaded in the background, the frames being drawn first (sprite_hd_source()).
    sprite_pack_request_all();
    if (hd_sync)
    {
        hd_stats.frames += (uint64_t)sprite_pack_wait_idle();
        hd_stats.load_us = (int64_t)((SDL_GetTicksNS() - t0) / 1000);
        JUSTLOG("HD sprites: %" PRId64 " entries decoded in %" PRId64 " ms (%" PRId64 " ms on the workers), %" PRIu64 " MiB, %" PRId64 " failed",
            sprite_pack_entry_count(), hd_stats.load_us / 1000, sprite_pack_decode_us() / 1000, sprite_pack_bytes() >> 20, sprite_pack_failed());
    }
}

void sprite_hd_frame_begin(void)
{
    hd_read_env();
    hd_frame_counter++;
    hd_draw_frame = false;
    if (hd_synth_shift == 0)
    {
        const TbBool fill = hd_fill_active();
        if (hd_content_changed || (strcasecmp(keeperfx_ui_config.asset_pack, hd_loaded_pack) != 0) || (fill != hd_loaded_fill))
            hd_reload_packs();
        hd_stats.frames += (uint64_t)sprite_pack_publish();
        const uint64_t hd_memory_cap = hd_cap();
        RendererSetHdSpriteAtlasCap(hd_memory_cap);
        if (sprite_pack_bytes() > hd_memory_cap)
        {
            const int64_t evicted = sprite_pack_evict(hd_memory_cap - hd_memory_cap / 10);
            hd_stats.evicted += (uint64_t)evicted;
            if (hd_stats.evicted == (uint64_t)evicted)
                WARNMSG("HD sprites: over the %" PRIu64 " MiB cap; the least recently drawn frames are dropped (and decoded again when drawn)",
                    hd_memory_cap >> 20);
        }
        // True colours missing for slots loaded since (the control, or SPRITE_TRUE_COLOUR switched), or to free.
        if (hd_zip_pending || (hd_tc_loaded != ((keeperfx_ui_config.sprite_true_colour != 0) ? 1 : 0)))
        {
            // Generated art made from the other colours: made again (at the next frame, the packs reopened).
            if (fill && (hd_tc_loaded >= 0) && (hd_tc_loaded != ((keeperfx_ui_config.sprite_true_colour != 0) ? 1 : 0)))
                hd_content_changed = true;
            hd_zip_pending = false;
            if (hd_tc_quantised && (RendererHdReferencePalette() == NULL))
                RendererSetHdReferencePalette(engine_palette);
            hd_zip_sync_true_colour();
        }
        if ((sprite_pack_entry_count() == 0) && (hd_zip_tc_count == 0) && !fill)
            return;
    }
    // The HD art is in the reference palette's colours (the game's own, as for HD terrain): a palette effect reaches
    // it as a colour transform. The terrain store clears the reference when it holds nothing; set it again.
    if (RendererHdReferencePalette() == NULL)
        RendererSetHdReferencePalette(engine_palette);
    hd_draw_frame = RendererHdUsable();
}

TbBool sprite_hd_drawing_frame(void)
{
    return hd_draw_frame;
}

/** The original frame's indices (0 where transparent, and where a literal 0 is: HD texels there are transparent, and
 *  parity mode draws them as the original draws index 0). False if the data ends early. */
static TbBool hd_decode(const unsigned char *rle, int64_t w, int64_t h, unsigned char *idx)
{
    memset(idx, 0, (size_t)(w * h));
    size_t p = 0;
    for (int64_t y = 0; y < h; y++)
    {
        int64_t x = 0;
        for (;;)
        {
            const int64_t n = (signed char)rle[p++];
            if (n == 0)
                break;
            if (n < 0)
            {
                x -= n;
                continue;
            }
            for (int64_t k = 0; k < n; k++, x++)
                if ((x >= 0) && (x < w))
                    idx[y * w + x] = rle[p + k];
            p += (size_t)n;
        }
    }
    return true;
}

/** An alpha-ramp texel's ramp delta (render_alpha_blend()), split into what it adds and what it subtracts; false for
 *  index 0 and above 64 (no ramp step: transparent). */
static TbBool hd_ramp_texel(unsigned char i, int add[3], int sub[3])
{
    static const int ramp_deltas[8][3] = { { 4, 4, 4 }, { 6, 4, 0 }, { 6, 1, 1 }, { 2, 2, 6 }, { 2, 6, 2 }, { 3, 0, 3 },
        { -2, -2, -2 }, { 6, 3, 1 } };
    if ((i < 1) || (i > 64))
        return false;
    const int ramp = (i - 1) / 8, step = (i - 1) % 8;
    for (int c = 0; c < 3; c++)
    {
        const int d = ramp_deltas[ramp][c] * step * 255 / 63;
        add[c] = (d > 0) ? d : 0;
        sub[c] = (d < 0) ? -d : 0;
    }
    return true;
}

/** The generated entry for a frame no pack covers (ASSET_PACK_FILL), added if new: made from the zip frame's true
 *  colours when SPRITE_TRUE_COLOUR has them, else from the original's indices in the reference palette's colours;
 *  additive art from its ramp deltas. -1 if none. */
static int32_t hd_generated_entry(uint64_t key, const unsigned char *rle, int64_t w, int64_t h, const struct SpriteHdZipFrame *z,
    TbBool additive)
{
    const unsigned char *palette = RendererHdReferencePalette();
    if (!hd_fill_active() || (key == 0) || (palette == NULL) || (w <= 0) || (h <= 0))
        return -1;
    const TbBool tc = !additive && (z != NULL) && (z->tc.levels > 0) && (z->tc.level_scale[0] == 1);
    const uint64_t gen_key = key ^ (additive ? HD_GEN_SALT_ADDITIVE : (tc ? HD_GEN_SALT_TRUE_COLOUR : HD_GEN_SALT_COLOUR));
    const int32_t found = sprite_pack_find(gen_key);
    if (found >= 0)
        return found;
    const size_t n = (size_t)(w * h);
    TbPixel *rgba = (TbPixel *)malloc(n * sizeof(TbPixel));
    TbPixel *sub = additive ? (TbPixel *)malloc(n * sizeof(TbPixel)) : NULL;
    unsigned char *idx = tc ? NULL : (unsigned char *)malloc(n);
    int32_t e = -1;
    if ((rgba != NULL) && (!additive || (sub != NULL)) && (tc || (idx != NULL)))
    {
        if (tc)
        {
            memcpy(rgba, z->tc.level[0], n * sizeof(TbPixel));
        }
        else
        {
            hd_decode(rle, w, h, idx);
            for (size_t i = 0; i < n; i++)
            {
                int a[3], m[3];
                if (!additive)
                    rgba[i] = (idx[i] != 0) ? resolve_indexed_pixel(idx[i], palette) : TbPixel_Transparent;
                else if (hd_ramp_texel(idx[i], a, m))
                {
                    rgba[i] = TbPixel_RGBA((uint8_t)a[0], (uint8_t)a[1], (uint8_t)a[2], 255);
                    sub[i] = TbPixel_RGBA((uint8_t)m[0], (uint8_t)m[1], (uint8_t)m[2], 255);
                }
                else
                {
                    rgba[i] = sub[i] = TbPixel_Transparent;
                }
            }
        }
        e = sprite_pack_add_generated(gen_key, rgba, sub, w, h, additive);
        if (e >= 0)
            hd_stats.generated++;
    }
    free(rgba);
    free(sub);
    free(idx);
    return e;
}

/** KFX_SPRITE_HD_SYNTH: the frame upscaled x2 / x4, nearest neighbour, in the reference palette's colours; and its
 *  levels down to x2 (at x1 the original is drawn). */
static void hd_build_synthetic(struct SpriteHdFrame *f, const unsigned char *rle, int64_t w, int64_t h)
{
    f->state = HdF_None;
    const unsigned char *palette = RendererHdReferencePalette();
    if ((palette == NULL) || (w <= 0) || (h <= 0))
        return;
    unsigned char *idx = malloc((size_t)(w * h));
    if (idx == NULL)
        return;
    hd_decode(rle, w, h, idx);
    const int64_t s = (int64_t)1 << hd_synth_shift;
    TbPixel *top = malloc((size_t)(w * s * h * s) * sizeof(TbPixel));
    if (top == NULL)
    {
        free(idx);
        return;
    }
    for (int64_t y = 0; y < h * s; y++)
        for (int64_t x = 0; x < w * s; x++)
        {
            const unsigned char i = idx[(y / s) * w + x / s];
            TbPixel t = (i != 0) ? resolve_indexed_pixel(i, palette) : TbPixel_Transparent;
            // The pattern: every texel but the one parity mode samples (the middle of each original texel's block)
            // inverted, so parity must still draw the original and precision mustn't.
            if (hd_synth_pattern && (t.a >= 128) && (((x % s) != (s - 1) / 2) || ((y % s) != (s - 1) / 2)))
                t = TbPixel_RGBA((uint8_t)(255 - t.r), (uint8_t)(255 - t.g), (uint8_t)(255 - t.b), 255);
            top[y * w * s + x] = t;
        }
    free(idx);
    sprite_hd_build_levels(&f->syn, top, w, h, s, false);
    hd_stats.bytes += f->syn.bytes;
    f->state = HdF_Synthetic;
    f->uid = hd_next_uid++;
    hd_stats.frames++;
}

/** Fills `out` from a frame's levels (lv, uid): the level whose texel is nearest a pixel in precision mode, the finest
 *  in parity mode; false to draw the original (nearer than every level). */
static TbBool hd_fill_source(const struct SpriteHdLevels *lv, uint64_t uid, const unsigned char *rle, int64_t width,
    int64_t full_height, int64_t height, int64_t scale, TbBool additive, struct TbHdSpriteSource *out)
{
    int k = 0;
    if (!hd_parity_mode)
    {
        // The level whose texel is nearest a pixel (round(log2), as terrain's texture_hd_lod_for_texel()); the original
        // (one texel a pixel) when it is nearer than every level.
        const double magnification = (double)scale / 32.0;
        const double m = log2((magnification > 0.0) ? magnification : 1e-9);
        int best = -1;
        double best_d = 0.0;
        for (int i = 0; i < lv->levels; i++)
        {
            const double d = fabs(log2((double)lv->level_scale[i]) - m);
            if ((best < 0) || (d < best_d))
            {
                best = i;
                best_d = d;
            }
        }
        if ((best < 0) || (fabs(m) < best_d))
            return false;
        k = best;
    }
    // The texels of the original's crop; art past it (packs) around them, drawn in precision mode.
    const int64_t ls = lv->level_scale[k];
    out->stride = (width + lv->spill[0] + lv->spill[2]) * ls;
    out->texels = lv->level[k] + lv->spill[1] * ls * out->stride + lv->spill[0] * ls;
    out->additive = additive;
    out->sub_texels = (additive && (lv->sub[k] != NULL)) ? lv->sub[k] + lv->spill[1] * ls * out->stride + lv->spill[0] * ls : NULL;
    out->scale = ls;
    memcpy(out->spill, lv->spill, sizeof(out->spill));
    out->rle = rle;
    out->orig_width = width;
    out->orig_height = height;
    out->full_height = full_height;
    out->id = uid * SPRITE_HD_MAX_LEVELS + (uint64_t)k;
    out->parity = hd_parity_mode;
    hd_stats.draws++;
    return true;
}


/** The levels to draw original frame kspr_idx with (a pack entry, the synthetic store's, a zip frame's true colours),
 *  and their uid; NULL if none (yet). `counted`: a draw (true colours count in the stats). */
static const struct SpriteHdLevels *hd_frame_levels(int64_t kspr_idx, const unsigned char *rle, int64_t width, int64_t full_height,
    TbBool counted, uint64_t *uid_out)
{
    struct SpriteHdFrame *f = NULL;
    if ((kspr_idx >= 0) && (kspr_idx < KEEPSPRITE_LENGTH))
    {
        if (hd_orig == NULL)
            hd_orig = calloc(KEEPSPRITE_LENGTH, sizeof(struct SpriteHdFrame));
        if (hd_orig != NULL)
            f = &hd_orig[kspr_idx];
    }
    else if ((kspr_idx >= KEEPERSPRITE_ADD_OFFSET) && (kspr_idx < KEEPERSPRITE_ADD_OFFSET + KEEPERSPRITE_ADD_NUM))
    {
        if (hd_custom == NULL)
            hd_custom = calloc(KEEPERSPRITE_ADD_NUM, sizeof(struct SpriteHdFrame));
        if (hd_custom != NULL)
            f = &hd_custom[kspr_idx - KEEPERSPRITE_ADD_OFFSET];
    }
    if (f == NULL)
        return NULL;
    // A custom slot's zip frame: its key from the load, and its true colours (SPRITE_TRUE_COLOUR).
    struct SpriteHdZipFrame *z = NULL;
    if ((kspr_idx >= KEEPERSPRITE_ADD_OFFSET) && (hd_zip != NULL) && (hd_zip[kspr_idx - KEEPERSPRITE_ADD_OFFSET].zip != NULL))
    {
        z = &hd_zip[kspr_idx - KEEPERSPRITE_ADD_OFFSET];
        if ((z->w != width) || (z->h != full_height))
            z = NULL;
    }
    if (f->state == HdF_Untried)
    {
        if (hd_synth_shift != 0)
        {
            hd_build_synthetic(f, rle, width, full_height);
        }
        else
        {
            // The frame's content key (the original's run-length data ends its own rows) names its pack entry.
            const uint64_t key = (z != NULL) ? z->key : sprite_hd_frame_key(rle, SIZE_MAX, width, full_height);
            int32_t e = sprite_pack_find(key);
            if (e < 0)
                e = hd_generated_entry(key, rle, width, full_height, z, false); // none with ASSET_PACK_FILL off
            f->entry = e;
            f->state = (e >= 0) ? HdF_Pack : HdF_None;
        }
    }
    const struct SpriteHdLevels *lv = NULL;
    uint64_t uid = 0;
    if (f->state == HdF_Synthetic)
    {
        lv = &f->syn;
        uid = f->uid;
    }
    else if (f->state == HdF_Pack)
    {
        struct SpritePackEntry *entry = sprite_pack_entry(f->entry);
        if ((entry != NULL) && !entry->additive && (entry->w == width) && (entry->h == full_height))
        {
            if (entry->state != SpE_Built)
            {
                // Not decoded yet (or evicted): the true colours or the original this frame, its art next
                // (sprite_hd_frame_begin() publishes).
                sprite_pack_request(f->entry, true);
                if (hd_sync)
                    hd_stats.frames += (uint64_t)sprite_pack_wait_idle();
            }
            if (entry->state == SpE_Built)
            {
                entry->last_used = hd_frame_counter;
                lv = &entry->lv;
                uid = entry->uid;
            }
        }
    }
    if ((lv == NULL) && (z != NULL) && (z->tc.levels > 0) && (hd_synth_shift == 0))
    {
        lv = &z->tc;
        uid = ((uint64_t)1 << 60) + z->tc_uid; // apart from the packs' uids: both reach the GPU's cache
        if (counted)
            hd_stats.zip_draws++;
    }
    *uid_out = uid;
    return lv;
}

TbBool sprite_hd_source(int64_t kspr_idx, const unsigned char *rle, int64_t width, int64_t full_height, int64_t height,
    int64_t scale, struct TbHdSpriteSource *out)
{
    if (!hd_draw_frame || (rle == NULL) || (width <= 0) || (height <= 0) || (full_height < height))
        return false;
    uint64_t uid = 0;
    const struct SpriteHdLevels *lv = hd_frame_levels(kspr_idx, rle, width, full_height, true, &uid);
    if (lv == NULL)
        return false;
    return hd_fill_source(lv, uid, rle, width, full_height, height, scale, false, out);
}

/** The synthetic store's additive frame (alpha-ramp draws): each texel's ramp delta (render_alpha_blend()), split
 *  into what it adds and what it subtracts, upscaled; indices 0 and above 64 transparent (parity mode then writes
 *  black there, as the original does). With the pattern, every texel but the one parity samples adds its complement. */
static void hd_build_synthetic_additive(struct SpriteHdFrame *f, const unsigned char *rle, int64_t w, int64_t h)
{
    f->state = HdF_None;
    if ((w <= 0) || (h <= 0))
        return;
    unsigned char *idx = malloc((size_t)(w * h));
    if (idx == NULL)
        return;
    hd_decode(rle, w, h, idx);
    const int64_t s = (int64_t)1 << hd_synth_shift;
    TbPixel *add = malloc((size_t)(w * s * h * s) * sizeof(TbPixel));
    TbPixel *sub = malloc((size_t)(w * s * h * s) * sizeof(TbPixel));
    if ((add == NULL) || (sub == NULL))
    {
        free(idx);
        free(add);
        free(sub);
        return;
    }
    for (int64_t y = 0; y < h * s; y++)
        for (int64_t x = 0; x < w * s; x++)
        {
            const unsigned char i = idx[(y / s) * w + x / s];
            TbPixel a = TbPixel_Transparent, m = TbPixel_Transparent;
            int pa[3], pm[3];
            if (hd_ramp_texel(i, pa, pm))
            {
                for (int c = 0; c < 3; c++)
                    if (hd_synth_pattern && (((x % s) != (s - 1) / 2) || ((y % s) != (s - 1) / 2)))
                        pa[c] = 255 - pa[c];
                a = TbPixel_RGBA((uint8_t)pa[0], (uint8_t)pa[1], (uint8_t)pa[2], 255);
                m = TbPixel_RGBA((uint8_t)pm[0], (uint8_t)pm[1], (uint8_t)pm[2], 255);
            }
            add[y * w * s + x] = a;
            sub[y * w * s + x] = m;
        }
    free(idx);
    sprite_hd_build_levels(&f->syn, add, w, h, s, false);
    sprite_hd_build_sub_levels(&f->syn, sub, w, h);
    hd_stats.bytes += f->syn.bytes;
    f->state = HdF_Synthetic;
    f->uid = hd_next_uid++;
    hd_stats.frames++;
}

TbBool sprite_hd_alpha_source(int64_t kspr_idx, const unsigned char *rle, int64_t width, int64_t full_height, int64_t height,
    int64_t scale, struct TbHdSpriteSource *out)
{
    if (!hd_draw_frame || (rle == NULL) || (width <= 0) || (height <= 0) || (full_height < height))
        return false;
    struct SpriteHdFrame *f = NULL;
    if ((kspr_idx >= 0) && (kspr_idx < KEEPSPRITE_LENGTH))
    {
        if (hd_orig_alpha == NULL)
            hd_orig_alpha = calloc(KEEPSPRITE_LENGTH, sizeof(struct SpriteHdFrame));
        if (hd_orig_alpha != NULL)
            f = &hd_orig_alpha[kspr_idx];
    }
    else if ((kspr_idx >= KEEPERSPRITE_ADD_OFFSET) && (kspr_idx < KEEPERSPRITE_ADD_OFFSET + KEEPERSPRITE_ADD_NUM))
    {
        if (hd_custom_alpha == NULL)
            hd_custom_alpha = calloc(KEEPERSPRITE_ADD_NUM, sizeof(struct SpriteHdFrame));
        if (hd_custom_alpha != NULL)
            f = &hd_custom_alpha[kspr_idx - KEEPERSPRITE_ADD_OFFSET];
    }
    if (f == NULL)
        return false;
    if (f->state == HdF_Untried)
    {
        if (hd_synth_shift != 0)
        {
            hd_build_synthetic_additive(f, rle, width, full_height);
        }
        else
        {
            const uint64_t key = sprite_hd_frame_key(rle, SIZE_MAX, width, full_height);
            int32_t e = sprite_pack_find(key);
            const struct SpritePackEntry *entry = sprite_pack_entry(e);
            if ((entry == NULL) || !entry->additive) // colour art isn't for alpha ramps
                e = hd_generated_entry(key, rle, width, full_height, NULL, true);
            f->entry = e;
            f->state = (e >= 0) ? HdF_Pack : HdF_None;
        }
    }
    const struct SpriteHdLevels *lv = NULL;
    uint64_t uid = 0;
    if (f->state == HdF_Synthetic)
    {
        lv = &f->syn;
        uid = f->uid;
    }
    else if (f->state == HdF_Pack)
    {
        struct SpritePackEntry *entry = sprite_pack_entry(f->entry);
        if ((entry != NULL) && entry->additive && (entry->w == width) && (entry->h == full_height))
        {
            if (entry->state != SpE_Built)
            {
                sprite_pack_request(f->entry, true);
                if (hd_sync)
                    hd_stats.frames += (uint64_t)sprite_pack_wait_idle();
            }
            if (entry->state == SpE_Built)
            {
                entry->last_used = hd_frame_counter;
                lv = &entry->lv;
                uid = entry->uid;
            }
        }
    }
    if (lv == NULL)
        return false;
    if (!hd_fill_source(lv, uid, rle, width, full_height, height, scale, true, out))
        return false;
    hd_stats.alpha_draws++;
    return true;
}

TbBool sprite_hd_soft_shadows(void)
{
    return (keeperfx_ui_config.shadow_style == ShadowStyle_Soft) && (hd_synth_shift == 0) && !hd_parity_mode;
}

TbBool sprite_hd_shadow_art(int64_t kspr_idx, const unsigned char *rle, int64_t width, int64_t full_height, const TbPixel **texels,
    int64_t *stride, int64_t *scale, uint64_t *id)
{
    if (!hd_draw_frame || !sprite_hd_soft_shadows() || (rle == NULL) || (width <= 0) || (full_height <= 0))
        return false;
    uint64_t uid = 0;
    const struct SpriteHdLevels *lv = hd_frame_levels(kspr_idx, rle, width, full_height, false, &uid);
    if ((lv == NULL) || (lv->levels == 0))
        return false;
    // The finest level, the original's crop (art past it isn't under the shadow's quad).
    const int64_t ls = lv->level_scale[0];
    *stride = (width + lv->spill[0] + lv->spill[2]) * ls;
    *texels = lv->level[0] + lv->spill[1] * ls * (*stride) + lv->spill[0] * ls;
    *scale = ls;
    *id = uid * SPRITE_HD_MAX_LEVELS;
    return true;
}

TbBool sprite_hd_shadow_source(int64_t kspr_idx, const unsigned char *rle, int64_t width, int64_t full_height, int64_t scale,
    struct TbHdSpriteSource *out)
{
    if (!hd_draw_frame || !sprite_hd_soft_shadows() || (rle == NULL) || (width <= 0) || (full_height <= 0))
        return false;
    uint64_t uid = 0;
    const struct SpriteHdLevels *lv = hd_frame_levels(kspr_idx, rle, width, full_height, false, &uid);
    if ((lv == NULL) || (lv->levels == 0))
        return false;
    return hd_fill_source(lv, uid, rle, width, full_height, full_height, scale, false, out);
}

/******************************************************************************/

void sprite_audit_set_enabled(int enabled)
{
    audit_enabled = enabled;
    memset(&audit_cur, 0, sizeof(audit_cur));
    memset(&audit_last, 0, sizeof(audit_last));
}

int sprite_audit_is_enabled(void)
{
    return audit_enabled;
}

void sprite_audit_frame_begin(uint64_t gameturn)
{
    if (!audit_enabled)
        return;
    const uint64_t frame = audit_last.frame + 1;
    audit_last = audit_cur;
    audit_last.frame = frame;
    memset(&audit_cur, 0, sizeof(audit_cur));
    audit_cur.gameturn = gameturn;
}

int sprite_audit_bin(double magnification)
{
    if (!(magnification > 0.0))
        return 0;
    int bin = (int)floor((log2(magnification) - TERRAIN_TEXEL_LOG2_MIN) * 2.0);
    if (bin < 0)
        bin = 0;
    if (bin >= TERRAIN_TEXEL_BINS)
        bin = TERRAIN_TEXEL_BINS - 1;
    return bin;
}

void sprite_audit_draw(int64_t x, int64_t y, int64_t src_w, int64_t src_h, int64_t dst_w, int64_t dst_h)
{
    if (!audit_enabled || (src_w <= 0) || (src_h <= 0) || (dst_w <= 0) || (dst_h <= 0))
        return;
    const int64_t flags = RendererGetDrawFlags();
    enum SpriteAuditKind kind = SprAud_Solid;
    if (EngineSpriteDrawUsingAlpha)
        kind = SprAud_Alpha;
    else if ((flags & (Lb_SPRITE_TRANSPAR4 | Lb_SPRITE_TRANSPAR8)) != 0)
        kind = SprAud_Ghost;
    else if ((flags & Lb_SPRITE_REMAP) != 0)
        kind = SprAud_Remap;
    audit_cur.draws++;
    audit_cur.kind[kind]++;
    if ((flags & Lb_SPRITE_FLIP_HORIZ) != 0)
        audit_cur.flipped++;
    const double mag_x = (double)dst_w / (double)src_w;
    const double mag_y = (double)dst_h / (double)src_h;
    const double mag = (mag_x > mag_y) ? mag_x : mag_y;
    if (mag > audit_cur.mag_max)
        audit_cur.mag_max = mag;
    const int64_t x0 = (x > 0) ? x : 0;
    const int64_t y0 = (y > 0) ? y : 0;
    int64_t x1 = x + dst_w;
    int64_t y1 = y + dst_h;
    if (x1 > lbDisplay.GraphicsWindowWidth)
        x1 = lbDisplay.GraphicsWindowWidth;
    if (y1 > lbDisplay.GraphicsWindowHeight)
        y1 = lbDisplay.GraphicsWindowHeight;
    if ((x1 <= x0) || (y1 <= y0))
        return;
    const double area = (double)(x1 - x0) * (double)(y1 - y0);
    audit_cur.area += area;
    audit_cur.mag_hist[sprite_audit_bin(mag)] += area;
}

const struct SpriteAuditStats *sprite_audit_last(void)
{
    return &audit_last;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
