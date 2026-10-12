/******************************************************************************/
// Free implementation of the Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_sprites_hd_pack.c
 *     HD sprite packs: the pack index and the frames' decoding.
 * @par Purpose:
 *     See engine_sprites_hd_pack.h.
 * @par Comment:
 *     The workers only read entries' fixed fields (key, crop, source, png) and the sources, and write an entry's
 *     result and its Queued -> Decoding -> Ready state under the queue lock; everything else is the main thread's.
 * @author   KeeperFX Team
 * @date     11 Oct 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "engine_sprites_hd_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>
#include <spng.h>

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_fileio.h"
#include "value_util.h"
#include "engine_hd_generate.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define PACK_KEY_RECORD 16
#define PACK_MAX_WORKERS 8

static struct SpritePackSource *pack_sources;
static int64_t pack_source_count;
static struct SpritePackEntry *pack_entries;
static int64_t pack_entry_count, pack_entry_capacity;
static char *pack_paths;                 ///< the path pool: 0-terminated paths relative to their pack
static size_t pack_paths_len, pack_paths_capacity;
static int32_t *pack_table;              ///< open addressing: entry + 1, 0 empty
static size_t pack_table_size;           ///< a power of two
static uint64_t pack_bytes;
static int64_t pack_failed;
static uint64_t pack_next_uid = 1;

// The decoding: urgent requests (a frame being drawn) first, then the background load.
static SDL_Mutex *queue_lock;
static SDL_Condition *queue_wake;        ///< work arrived, or stop
static SDL_Condition *queue_idle;        ///< a worker finished one
static int32_t *queue_urgent;            ///< a stack
static size_t queue_urgent_len, queue_urgent_capacity;
static int32_t *queue_background;        ///< a FIFO
static size_t queue_background_head, queue_background_len, queue_background_capacity;
static int64_t queue_busy;               ///< entries being decoded
static int32_t *ready_list;
static size_t ready_len, ready_capacity;
static TbBool queue_stop;
static SDL_Thread *workers[PACK_MAX_WORKERS];
static int worker_count;
static int worker_limit;                 ///< sprite_pack_set_worker_limit(); 0: the cores but one
static SDL_AtomicInt decode_us;
/******************************************************************************/

void sprite_hd_free_levels(struct SpriteHdLevels *lv)
{
    for (int k = 0; k < lv->levels; k++)
    {
        free(lv->level[k]);
        free(lv->sub[k]);
    }
    memset(lv, 0, sizeof(*lv));
}

/** A level averaging f x f texels of `src` (scale s, w x h original texels) into each: the opaque ones' mean, opaque
 *  if at least half of them are. */
static TbPixel *reduce_level(const TbPixel *src, int64_t s, int64_t f, int64_t w, int64_t h)
{
    const int64_t sw = w * s, dw = w * (s / f), dh = h * (s / f);
    TbPixel *dst = malloc((size_t)(dw * dh) * sizeof(TbPixel));
    if (dst == NULL)
        return NULL;
    for (int64_t y = 0; y < dh; y++)
        for (int64_t x = 0; x < dw; x++)
        {
            int64_t r = 0, g = 0, b = 0, n = 0;
            for (int64_t dy = 0; dy < f; dy++)
                for (int64_t dx = 0; dx < f; dx++)
                {
                    const TbPixel t = src[(f * y + dy) * sw + f * x + dx];
                    if (t.a >= 128)
                    {
                        r += t.r; g += t.g; b += t.b; n++;
                    }
                }
            dst[y * dw + x] = (2 * n >= f * f) ? TbPixel_RGBA((uint8_t)((r + n / 2) / n), (uint8_t)((g + n / 2) / n), (uint8_t)((b + n / 2) / n), 255)
                : TbPixel_Transparent;
        }
    return dst;
}

void sprite_hd_build_levels(struct SpriteHdLevels *lv, TbPixel *top, int64_t w, int64_t h, int64_t s, TbBool to_1x)
{
    memset(lv, 0, sizeof(*lv));
    lv->level[0] = top;
    lv->level_scale[0] = s;
    lv->levels = 1;
    lv->bytes = (uint64_t)(w * s * h * s) * sizeof(TbPixel);
    for (int64_t ls = s; (ls > 2) && ((ls % 2) == 0) && (lv->levels < SPRITE_HD_MAX_LEVELS); ls /= 2)
    {
        TbPixel *half = reduce_level(lv->level[lv->levels - 1], ls, 2, w, h);
        if (half == NULL)
            return;
        lv->level[lv->levels] = half;
        lv->level_scale[lv->levels] = ls / 2;
        lv->levels++;
        lv->bytes += (uint64_t)(w * (ls / 2) * h * (ls / 2)) * sizeof(TbPixel);
    }
    if (to_1x && (lv->levels < SPRITE_HD_MAX_LEVELS) && (lv->level_scale[lv->levels - 1] > 1))
    {
        const int64_t ls = lv->level_scale[lv->levels - 1];
        TbPixel *one = reduce_level(lv->level[lv->levels - 1], ls, ls, w, h);
        if (one != NULL)
        {
            lv->level[lv->levels] = one;
            lv->level_scale[lv->levels] = 1;
            lv->levels++;
            lv->bytes += (uint64_t)(w * h) * sizeof(TbPixel);
        }
    }
}

void sprite_hd_build_sub_levels(struct SpriteHdLevels *lv, TbPixel *top, int64_t w, int64_t h)
{
    if ((top == NULL) || (lv->levels == 0))
    {
        free(top);
        return;
    }
    lv->sub[0] = top;
    lv->bytes += (uint64_t)(w * lv->level_scale[0] * h * lv->level_scale[0]) * sizeof(TbPixel);
    for (int k = 1; k < lv->levels; k++)
    {
        const int64_t from = lv->level_scale[k - 1], to = lv->level_scale[k];
        lv->sub[k] = reduce_level(lv->sub[k - 1], from, from / to, w, h);
        if (lv->sub[k] != NULL)
            lv->bytes += (uint64_t)(w * to * h * to) * sizeof(TbPixel);
    }
}

/******************************************************************************/

/** A whole file into a new buffer (0-terminated), or NULL. */
static unsigned char *read_file(const char *fname, size_t *len)
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

/** The next line of `text` (from *pos) into `line`, trimmed of spaces, tabs and CR; false at the end. */
static TbBool next_line(const char *text, size_t len, size_t *pos, char *line, size_t size)
{
    if (*pos >= len)
        return false;
    size_t end = *pos;
    while ((end < len) && (text[end] != '\n'))
        end++;
    size_t a = *pos, b = end;
    *pos = end + 1;
    while ((a < b) && ((text[a] == ' ') || (text[a] == '\t') || (text[a] == '\r')))
        a++;
    while ((b > a) && ((text[b - 1] == ' ') || (text[b - 1] == '\t') || (text[b - 1] == '\r')))
        b--;
    size_t n = b - a;
    if (n >= size)
        n = size - 1;
    memcpy(line, text + a, n);
    line[n] = '\0';
    return true;
}

static uint32_t pool_add(const char *path)
{
    const size_t n = strlen(path) + 1;
    if (pack_paths_len + n > pack_paths_capacity)
    {
        size_t cap = (pack_paths_capacity == 0) ? 65536 : pack_paths_capacity * 2;
        while (cap < pack_paths_len + n)
            cap *= 2;
        char *grown = (char *)realloc(pack_paths, cap);
        if (grown == NULL)
            return UINT32_MAX;
        pack_paths = grown;
        pack_paths_capacity = cap;
    }
    memcpy(pack_paths + pack_paths_len, path, n);
    const uint32_t off = (uint32_t)pack_paths_len;
    pack_paths_len += n;
    return off;
}

static size_t table_slot(uint64_t key)
{
    return (size_t)((key ^ (key >> 29)) * 0x9E3779B97F4A7C15ULL) & (pack_table_size - 1);
}

/** The entry with `key` in a table of `size` slots over `entries`, or -1. */
static int32_t table_find(const int32_t *table, size_t size, const struct SpritePackEntry *entries, uint64_t key)
{
    if ((table == NULL) || (key == 0))
        return -1;
    for (size_t slot = (size_t)((key ^ (key >> 29)) * 0x9E3779B97F4A7C15ULL) & (size - 1);; slot = (slot + 1) & (size - 1))
    {
        const int32_t e = table[slot];
        if (e == 0)
            return -1;
        if (entries[e - 1].key == key)
            return e - 1;
    }
}

int32_t sprite_pack_find(uint64_t key)
{
    return table_find(pack_table, pack_table_size, pack_entries, key);
}

/** Puts every entry in the table again (it grew). */
static TbBool table_rebuild(size_t size)
{
    int32_t *table = (int32_t *)calloc(size, sizeof(int32_t));
    if (table == NULL)
        return false;
    free(pack_table);
    pack_table = table;
    pack_table_size = size;
    for (int64_t e = 0; e < pack_entry_count; e++)
    {
        size_t slot = table_slot(pack_entries[e].key);
        while (pack_table[slot] != 0)
            slot = (slot + 1) & (pack_table_size - 1);
        pack_table[slot] = (int32_t)(e + 1);
    }
    return true;
}

static TbBool add_entry(const struct SpritePackEntry *entry)
{
    if (sprite_pack_find(entry->key) >= 0)
        return false; // an earlier pack has this key
    if (pack_entry_count == pack_entry_capacity)
    {
        const int64_t cap = (pack_entry_capacity == 0) ? 4096 : pack_entry_capacity * 2;
        struct SpritePackEntry *grown = (struct SpritePackEntry *)realloc(pack_entries, (size_t)cap * sizeof(struct SpritePackEntry));
        if (grown == NULL)
            return false;
        pack_entries = grown;
        pack_entry_capacity = cap;
    }
    if ((size_t)(pack_entry_count + 1) * 2 > pack_table_size)
    {
        if (!table_rebuild((pack_table_size == 0) ? 16384 : pack_table_size * 2))
            return false;
    }
    pack_entries[pack_entry_count] = *entry;
    size_t slot = table_slot(entry->key);
    while (pack_table[slot] != 0)
        slot = (slot + 1) & (pack_table_size - 1);
    pack_table[slot] = (int32_t)(pack_entry_count + 1);
    pack_entry_count++;
    return true;
}

int64_t sprite_pack_open(const char *dir, TbBool content)
{
    char fname[1280];
    snprintf(fname, sizeof(fname), "%s/pack.toml", dir);
    if (!LbFileExists(fname))
    {
        if (!content)
            WARNMSG("HD sprites: no pack \"%s\" (no pack.toml)", dir);
        return -1;
    }
    struct SpritePackSource src;
    memset(&src, 0, sizeof(src));
    snprintf(src.dir, sizeof(src.dir), "%s", dir);
    src.content = content;
    src.original_at_1x = true;
    VALUE root;
    if (!load_toml_file(fname, &root, 0))
        return -1;
    VALUE *v = value_dict_get(&root, "scale");
    if (value_type(v) == VALUE_INT32)
        src.scale = value_int32(v);
    v = value_dict_get(&root, "use_original_at_1x");
    if (value_type(v) == VALUE_BOOL)
        src.original_at_1x = value_bool(v) ? true : false;
    v = value_dict_get(&root, "name");
    if (value_type(v) == VALUE_STRING)
        snprintf(src.name, sizeof(src.name), "%s", value_string(v));
    value_fini(&root);
    if ((src.scale < 2) || (src.scale > 4))
    {
        WARNMSG("HD sprites: \"%s\": scale must be 2, 3 or 4, not %" PRId64 "; pack ignored", fname, src.scale);
        return -1;
    }
    // keys/animlist.idx: one record a frame line, in animlist order.
    snprintf(fname, sizeof(fname), "%s/keys/animlist.idx", dir);
    size_t keys_len;
    unsigned char *keys = read_file(fname, &keys_len);
    snprintf(fname, sizeof(fname), "%s/animlist.txt", dir);
    size_t list_len;
    char *list = (char *)read_file(fname, &list_len);
    if ((keys == NULL) || (list == NULL))
    {
        WARNMSG("HD sprites: \"%s\" has no %s; pack ignored", dir, (list == NULL) ? "animlist.txt" : "keys/animlist.idx");
        free(keys);
        free(list);
        return -1;
    }
    const int64_t first_entry = pack_entry_count;
    int64_t frames = 0;
    size_t lpos = 0;
    if ((list_len >= 3) && ((unsigned char)list[0] == 0xEF) && ((unsigned char)list[1] == 0xBB) && ((unsigned char)list[2] == 0xBF))
        lpos = 3;
    char filelist[512];
    TbBool bad = false;
    const uint16_t source_id = (uint16_t)pack_source_count;
    while (!bad && next_line(list, list_len, &lpos, filelist, sizeof(filelist)))
    {
        if ((filelist[0] == '\0') || (filelist[0] == '#'))
            continue;
        char fl_name[1280];
        snprintf(fl_name, sizeof(fl_name), "%s/%s", dir, filelist);
        size_t fl_len;
        char *fl = (char *)read_file(fl_name, &fl_len);
        if (fl == NULL)
        {
            WARNMSG("HD sprites: \"%s\" lists %s, which isn't there; pack ignored", dir, filelist);
            bad = true;
            break;
        }
        // The filelist's folder: its PNGs are named relative to it.
        char folder[512];
        snprintf(folder, sizeof(folder), "%s", filelist);
        char *slash = strrchr(folder, '/');
        if (slash != NULL)
            slash[1] = '\0';
        else
            folder[0] = '\0';
        size_t fpos = 0;
        if ((fl_len >= 3) && ((unsigned char)fl[0] == 0xEF) && ((unsigned char)fl[1] == 0xBB) && ((unsigned char)fl[2] == 0xBF))
            fpos = 3;
        char line[512];
        TbBool header = true;
        TbBool additive = false;
        while (next_line(fl, fl_len, &fpos, line, sizeof(line)))
        {
            if (line[0] == '\0')
                continue;
            if (header)
            {
                // "animation_<n> <rotable> <frames> <offset_x> <offset_y>": the original's, for check; a sixth word
                // "additive": the filelist's art is additive, for alpha-ramp draws (Phase 9).
                header = false;
                char words[6][64];
                additive = (sscanf(line, "%63s %63s %63s %63s %63s %63s", words[0], words[1], words[2], words[3], words[4], words[5]) == 6)
                    && (strcmp(words[5], "additive") == 0);
                continue;
            }
            char png[256];
            if (sscanf(line, "%255s", png) != 1)
                continue;
            if ((size_t)(frames + 1) * PACK_KEY_RECORD > keys_len)
            {
                frames++;
                continue; // counted, reported below
            }
            const unsigned char *rec = keys + frames * PACK_KEY_RECORD;
            struct SpritePackEntry e;
            memset(&e, 0, sizeof(e));
            for (int i = 7; i >= 0; i--)
                e.key = (e.key << 8) | rec[i];
            e.crop_x = (uint16_t)(rec[8] | (rec[9] << 8));
            e.crop_y = (uint16_t)(rec[10] | (rec[11] << 8));
            e.w = (uint16_t)(rec[12] | (rec[13] << 8));
            e.h = (uint16_t)(rec[14] | (rec[15] << 8));
            e.source = source_id;
            e.additive = additive ? 1 : 0;
            frames++;
            if ((e.key == 0) || (e.w == 0) || (e.h == 0))
                continue;
            char rel[800];
            snprintf(rel, sizeof(rel), "%s%s", folder, png);
            e.png = pool_add(rel);
            if (e.png == UINT32_MAX)
            {
                bad = true;
                break;
            }
            if (add_entry(&e))
                src.indexed++;
        }
        free(fl);
    }
    free(list);
    free(keys);
    if (!bad && ((size_t)frames * PACK_KEY_RECORD != keys_len))
    {
        WARNMSG("HD sprites: \"%s\": keys/animlist.idx has %" PRIu64 " records for %" PRId64 " frame lines (stale keys: rerun spritepack.py keys); pack ignored",
            dir, (uint64_t)(keys_len / PACK_KEY_RECORD), frames);
        bad = true;
    }
    if (bad)
    {
        // Drop what this pack added: the entries after first_entry.
        pack_entry_count = first_entry;
        table_rebuild((pack_table_size == 0) ? 16384 : pack_table_size);
        return -1;
    }
    src.frames = frames;
    struct SpritePackSource *grown = (struct SpritePackSource *)realloc(pack_sources, (size_t)(pack_source_count + 1) * sizeof(struct SpritePackSource));
    if (grown == NULL)
        return -1;
    pack_sources = grown;
    pack_sources[pack_source_count++] = src;
    JUSTLOG("HD sprites: %s \"%s\"%s%s%s: x%" PRId64 ", %" PRId64 " frame lines, %" PRId64 " entries indexed",
        content ? "content folder" : "pack", dir, src.name[0] ? " (" : "", src.name, src.name[0] ? ")" : "", src.scale, frames, src.indexed);
    return src.indexed;
}

static void workers_stop(void);

int64_t sprite_pack_reopen(const char *const *dirs, const TbBool *content, int64_t count)
{
    // Nothing decodes while the index changes; what the workers finished is published first, so it can carry over.
    workers_stop();
    sprite_pack_publish();
    struct SpritePackSource *old_sources = pack_sources;
    struct SpritePackEntry *old_entries = pack_entries;
    const int64_t old_entry_count = pack_entry_count;
    char *old_paths = pack_paths;
    int32_t *old_table = pack_table;
    const size_t old_table_size = pack_table_size;
    pack_sources = NULL;
    pack_source_count = 0;
    pack_entries = NULL;
    pack_entry_count = pack_entry_capacity = 0;
    pack_paths = NULL;
    pack_paths_len = pack_paths_capacity = 0;
    pack_table = NULL;
    pack_table_size = 0;
    queue_urgent_len = 0;
    queue_background_len = queue_background_head = 0;
    ready_len = 0;
    pack_bytes = 0;
    pack_failed = 0;
    SDL_SetAtomicInt(&decode_us, 0);
    int64_t indexed = 0;
    for (int64_t k = 0; k < count; k++)
    {
        const int64_t n = sprite_pack_open(dirs[k], content[k]);
        if (n > 0)
            indexed += n;
    }
    // An entry made from the same art as before (pack folder, scale, PNG and crop) keeps its texels and uid.
    for (int64_t e = 0; e < pack_entry_count; e++)
    {
        struct SpritePackEntry *ne = &pack_entries[e];
        const int32_t o = table_find(old_table, old_table_size, old_entries, ne->key);
        if ((o < 0) || (old_entries[o].state != SpE_Built) || (old_entries[o].source == SPRITE_PACK_GENERATED))
            continue;
        struct SpritePackEntry *oe = &old_entries[o];
        const struct SpritePackSource *ns = &pack_sources[ne->source], *os = &old_sources[oe->source];
        if ((strcmp(ns->dir, os->dir) != 0) || (ns->scale != os->scale) || (ns->original_at_1x != os->original_at_1x) ||
            (strcmp(pack_paths + ne->png, old_paths + oe->png) != 0) || (ne->crop_x != oe->crop_x) ||
            (ne->crop_y != oe->crop_y) || (ne->w != oe->w) || (ne->h != oe->h) || (ne->additive != oe->additive))
            continue;
        ne->lv = oe->lv;
        ne->uid = oe->uid;
        ne->last_used = oe->last_used;
        ne->state = SpE_Built;
        pack_bytes += ne->lv.bytes;
        memset(&oe->lv, 0, sizeof(oe->lv));
    }
    for (int64_t e = 0; e < old_entry_count; e++)
    {
        sprite_hd_free_levels(&old_entries[e].lv);
        sprite_hd_free_levels(&old_entries[e].result);
        free(old_entries[e].gen); // generated art is made again when drawn
    }
    free(old_entries);
    free(old_sources);
    free(old_paths);
    free(old_table);
    return indexed;
}

int64_t sprite_pack_entry_count(void)
{
    return pack_entry_count;
}

int64_t sprite_pack_source_count(void)
{
    return pack_source_count;
}

const struct SpritePackSource *sprite_pack_source(int64_t s)
{
    return ((s >= 0) && (s < pack_source_count)) ? &pack_sources[s] : NULL;
}

int32_t sprite_pack_add_generated(uint64_t key, const TbPixel *rgba, const TbPixel *sub, int64_t w, int64_t h, TbBool additive)
{
    if ((key == 0) || (rgba == NULL) || (w <= 0) || (h <= 0) || (w > 0xFFFF) || (h > 0xFFFF) || (additive && (sub == NULL)))
        return -1;
    const size_t n = (size_t)(w * h);
    TbPixel *gen = (TbPixel *)malloc((additive ? 2 * n : n) * sizeof(TbPixel));
    if (gen == NULL)
        return -1;
    memcpy(gen, rgba, n * sizeof(TbPixel));
    if (additive)
        memcpy(gen + n, sub, n * sizeof(TbPixel));
    struct SpritePackEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.key = key;
    entry.w = (uint16_t)w;
    entry.h = (uint16_t)h;
    entry.source = SPRITE_PACK_GENERATED;
    entry.additive = additive ? 1 : 0;
    entry.gen = gen;
    // The workers read pack_entries[] under the queue lock; adding may move it.
    if (queue_lock != NULL)
        SDL_LockMutex(queue_lock);
    const TbBool added = add_entry(&entry);
    const int32_t e = added ? (int32_t)(pack_entry_count - 1) : -1;
    if (queue_lock != NULL)
        SDL_UnlockMutex(queue_lock);
    if (!added)
        free(gen);
    return e;
}

struct SpritePackEntry *sprite_pack_entry(int32_t e)
{
    return ((e >= 0) && (e < pack_entry_count)) ? &pack_entries[e] : NULL;
}

uint64_t sprite_pack_bytes(void)
{
    return pack_bytes;
}

int64_t sprite_pack_failed(void)
{
    return pack_failed;
}

int64_t sprite_pack_decode_us(void)
{
    return (int64_t)SDL_GetAtomicInt(&decode_us);
}

/******************************************************************************/
// Decoding

/** Additive art's subtract image, <png>_sub.png beside it (the same canvas; none: nothing subtracted), cut as the add
 *  image was (at sx, sy, ow x oh original texels at the scale) into out's sub levels. */
static void decode_sub(const struct SpritePackSource *src, const struct SpritePackEntry *e, int64_t cw, int64_t ch, int64_t sx,
    int64_t sy, int64_t ow, int64_t oh, struct SpriteHdLevels *out)
{
    char fname[2048];
    snprintf(fname, sizeof(fname), "%s/%s", src->dir, pack_paths + e->png);
    char *dot = strrchr(fname, '.');
    if ((dot == NULL) || (strlen(fname) + 5 >= sizeof(fname)))
        return;
    snprintf(dot, sizeof(fname) - (size_t)(dot - fname), "_sub.png");
    size_t len;
    unsigned char *data = read_file(fname, &len);
    if (data == NULL)
        return;
    spng_ctx *ctx = spng_ctx_new(0);
    struct spng_ihdr ihdr;
    size_t size = 0;
    const int64_t s = src->scale;
    if ((ctx != NULL) && (spng_set_png_buffer(ctx, data, len) == 0) && (spng_get_ihdr(ctx, &ihdr) == 0) &&
        ((int64_t)ihdr.width == cw) && ((int64_t)ihdr.height == ch) && (spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &size) == 0))
    {
        unsigned char *pixels = (unsigned char *)malloc(size);
        TbPixel *top = (TbPixel *)malloc((size_t)(ow * s * oh * s) * sizeof(TbPixel));
        if ((pixels != NULL) && (top != NULL) && (spng_decode_image(ctx, pixels, size, SPNG_FMT_RGBA8, SPNG_DECODE_TRNS) == 0))
        {
            for (int64_t y = 0; y < oh * s; y++)
                memcpy(&top[y * ow * s], pixels + ((size_t)(sy + y) * (size_t)cw + (size_t)sx) * 4, (size_t)(ow * s) * 4);
            sprite_hd_build_sub_levels(out, top, ow, oh);
            top = NULL;
        }
        free(pixels);
        free(top);
    }
    if (ctx != NULL)
        spng_ctx_free(ctx);
    free(data);
}

/** The entry's art: its PNG, the original's crop cut out at the pack's scale, widened by whole original texels to
 *  any art past it (up to SPRITE_HD_MAX_SPILL a side), and its levels. False if the PNG can't be read or the crop
 *  isn't inside it. */
/** Generated art (ASSET_PACK_FILL): the original's texels enlarged by hd_generate_upscale(), with transparent context
 *  around them, and its levels (the original at 1x); additive art's add and subtract images alike. */
static TbBool generate_entry(const struct SpritePackEntry *e, struct SpriteHdLevels *out)
{
    const int64_t w = e->w, h = e->h, p = HD_GENERATE_PAD, f = HD_GENERATE_FACTOR, pw = w + 2 * p;
    TbPixel *padded = (TbPixel *)calloc((size_t)(pw * (h + 2 * p)), sizeof(TbPixel));
    TbPixel *top = (TbPixel *)malloc((size_t)(w * f * h * f) * sizeof(TbPixel));
    TbPixel *sub = e->additive ? (TbPixel *)malloc((size_t)(w * f * h * f) * sizeof(TbPixel)) : NULL;
    TbBool ok = (padded != NULL) && (top != NULL) && (!e->additive || (sub != NULL));
    for (int image = 0; ok && (image < (e->additive ? 2 : 1)); image++)
    {
        const TbPixel *src = e->gen + (size_t)image * (size_t)(w * h);
        for (int64_t y = 0; y < h; y++)
            memcpy(&padded[(y + p) * pw + p], &src[y * w], (size_t)w * sizeof(TbPixel));
        ok = hd_generate_upscale(padded, pw, w, h, p, f, e->additive, (image == 0) ? top : sub, w * f);
    }
    free(padded);
    if (!ok)
    {
        free(top);
        free(sub);
        return false;
    }
    sprite_hd_build_levels(out, top, w, h, f, false);
    if (e->additive)
        sprite_hd_build_sub_levels(out, sub, w, h);
    return true;
}

static TbBool decode_entry(const struct SpritePackEntry *e, struct SpriteHdLevels *out)
{
    if (e->source == SPRITE_PACK_GENERATED)
        return generate_entry(e, out);
    const struct SpritePackSource *src = &pack_sources[e->source];
    char fname[2048];
    snprintf(fname, sizeof(fname), "%s/%s", src->dir, pack_paths + e->png);
    size_t len;
    unsigned char *data = read_file(fname, &len);
    if (data == NULL)
        return false;
    TbBool ok = false;
    spng_ctx *ctx = spng_ctx_new(0);
    if (ctx != NULL)
    {
        struct spng_ihdr ihdr;
        size_t size = 0;
        spng_set_image_limits(ctx, 16384, 16384);
        if ((spng_set_png_buffer(ctx, data, len) == 0) && (spng_get_ihdr(ctx, &ihdr) == 0) &&
            (spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &size) == 0))
        {
            const int64_t s = src->scale, cw = ihdr.width, ch = ihdr.height;
            const int64_t x0 = e->crop_x * s, y0 = e->crop_y * s, w = e->w * s, h = e->h * s;
            unsigned char *pixels = (unsigned char *)malloc(size);
            if ((pixels != NULL) && (x0 + w <= cw) && (y0 + h <= ch) &&
                (spng_decode_image(ctx, pixels, size, SPNG_FMT_RGBA8, SPNG_DECODE_TRNS) == 0))
            {
                // The art's opaque box; past the crop, whole original texels a side, as far as the canvas goes.
                int64_t bx0 = cw, by0 = ch, bx1 = 0, by1 = 0;
                for (int64_t y = 0; y < ch; y++)
                    for (int64_t x = 0; x < cw; x++)
                        if (pixels[((size_t)y * (size_t)cw + (size_t)x) * 4 + 3] >= 128)
                        {
                            bx0 = min(bx0, x);
                            by0 = min(by0, y);
                            bx1 = max(bx1, x + 1);
                            by1 = max(by1, y + 1);
                        }
                int64_t spill[4] = { 0, 0, 0, 0 };
                if (bx1 > bx0)
                {
                    spill[0] = min((x0 > bx0) ? (x0 - bx0 + s - 1) / s : 0, x0 / s);
                    spill[1] = min((y0 > by0) ? (y0 - by0 + s - 1) / s : 0, y0 / s);
                    spill[2] = min((bx1 > x0 + w) ? (bx1 - x0 - w + s - 1) / s : 0, (cw - x0 - w) / s);
                    spill[3] = min((by1 > y0 + h) ? (by1 - y0 - h + s - 1) / s : 0, (ch - y0 - h) / s);
                    for (int i = 0; i < 4; i++)
                        spill[i] = min(spill[i], (int64_t)SPRITE_HD_MAX_SPILL);
                }
                const int64_t sx = x0 - spill[0] * s, sy = y0 - spill[1] * s;
                const int64_t ow = e->w + spill[0] + spill[2], oh = e->h + spill[1] + spill[3];
                TbPixel *top = (TbPixel *)malloc((size_t)(ow * s * oh * s) * sizeof(TbPixel));
                if (top != NULL)
                {
                    for (int64_t y = 0; y < oh * s; y++)
                        memcpy(&top[y * ow * s], pixels + ((size_t)(sy + y) * (size_t)cw + (size_t)sx) * 4, (size_t)(ow * s) * 4);
                    sprite_hd_build_levels(out, top, ow, oh, s, !src->original_at_1x);
                    memcpy(out->spill, spill, sizeof(spill));
                    if (e->additive)
                        decode_sub(src, e, cw, ch, sx, sy, ow, oh, out);
                    ok = true;
                }
            }
            free(pixels);
        }
        spng_ctx_free(ctx);
    }
    free(data);
    return ok;
}

static TbBool queue_pop(int32_t *e)
{
    while (queue_urgent_len > 0)
    {
        const int32_t i = queue_urgent[--queue_urgent_len];
        if (pack_entries[i].state == SpE_Queued)
        {
            *e = i;
            return true;
        }
    }
    while (queue_background_len > 0)
    {
        const int32_t i = queue_background[queue_background_head];
        queue_background_head = (queue_background_head + 1) % queue_background_capacity;
        queue_background_len--;
        if (pack_entries[i].state == SpE_Queued)
        {
            *e = i;
            return true;
        }
    }
    return false;
}

static int SDLCALL decode_worker(void *data)
{
    (void)data;
    SDL_LockMutex(queue_lock);
    for (;;)
    {
        int32_t i;
        while (!queue_stop && !queue_pop(&i))
            SDL_WaitCondition(queue_wake, queue_lock);
        if (queue_stop)
            break;
        pack_entries[i].state = SpE_Decoding;
        queue_busy++;
        const struct SpritePackEntry entry = pack_entries[i]; // the fixed fields
        SDL_UnlockMutex(queue_lock);
        struct SpriteHdLevels lv;
        memset(&lv, 0, sizeof(lv));
        const uint64_t t0 = SDL_GetTicksNS();
        const TbBool ok = decode_entry(&entry, &lv);
        SDL_AddAtomicInt(&decode_us, (int)((SDL_GetTicksNS() - t0) / 1000));
        SDL_LockMutex(queue_lock);
        queue_busy--;
        if (ok)
        {
            pack_entries[i].result = lv;
            pack_entries[i].state = SpE_Ready;
        }
        else
        {
            pack_entries[i].state = SpE_Failed;
        }
        if (ready_len == ready_capacity)
        {
            const size_t cap = (ready_capacity == 0) ? 1024 : ready_capacity * 2;
            int32_t *grown = (int32_t *)realloc(ready_list, cap * sizeof(int32_t));
            if (grown != NULL)
            {
                ready_list = grown;
                ready_capacity = cap;
            }
        }
        if (ready_len < ready_capacity)
            ready_list[ready_len++] = i;
        SDL_BroadcastCondition(queue_idle);
    }
    SDL_UnlockMutex(queue_lock);
    return 0;
}

static void workers_start(void)
{
    if (worker_count > 0)
        return;
    if (queue_lock == NULL)
    {
        queue_lock = SDL_CreateMutex();
        queue_wake = SDL_CreateCondition();
        queue_idle = SDL_CreateCondition();
    }
    if ((queue_lock == NULL) || (queue_wake == NULL) || (queue_idle == NULL))
        return;
    queue_stop = false;
    int n = SDL_GetNumLogicalCPUCores() - 1;
    if (n < 1)
        n = 1;
    if (n > PACK_MAX_WORKERS)
        n = PACK_MAX_WORKERS;
    if ((worker_limit > 0) && (n > worker_limit))
        n = worker_limit;
    for (int i = 0; i < n; i++)
    {
        workers[worker_count] = SDL_CreateThread(decode_worker, "sprite_hd", NULL);
        if (workers[worker_count] != NULL)
            worker_count++;
    }
}

static void workers_stop(void)
{
    if (worker_count == 0)
        return;
    SDL_LockMutex(queue_lock);
    queue_stop = true;
    SDL_BroadcastCondition(queue_wake);
    SDL_UnlockMutex(queue_lock);
    for (int i = 0; i < worker_count; i++)
        SDL_WaitThread(workers[i], NULL);
    worker_count = 0;
    queue_stop = false;
}

static void push_background(int32_t e)
{
    if (queue_background_len == queue_background_capacity)
    {
        const size_t cap = (queue_background_capacity == 0) ? 4096 : queue_background_capacity * 2;
        int32_t *grown = (int32_t *)malloc(cap * sizeof(int32_t));
        if (grown == NULL)
            return;
        for (size_t k = 0; k < queue_background_len; k++)
            grown[k] = queue_background[(queue_background_head + k) % queue_background_capacity];
        free(queue_background);
        queue_background = grown;
        queue_background_capacity = cap;
        queue_background_head = 0;
    }
    queue_background[(queue_background_head + queue_background_len) % queue_background_capacity] = e;
    queue_background_len++;
}

static void push_urgent(int32_t e)
{
    if (queue_urgent_len == queue_urgent_capacity)
    {
        const size_t cap = (queue_urgent_capacity == 0) ? 1024 : queue_urgent_capacity * 2;
        int32_t *grown = (int32_t *)realloc(queue_urgent, cap * sizeof(int32_t));
        if (grown == NULL)
            return;
        queue_urgent = grown;
        queue_urgent_capacity = cap;
    }
    queue_urgent[queue_urgent_len++] = e;
}

void sprite_pack_set_worker_limit(int n)
{
    workers_stop(); // the next request starts them again, that many
    worker_limit = (n > 0) ? n : 0;
}

void sprite_pack_request(int32_t e, TbBool urgent)
{
    if ((e < 0) || (e >= pack_entry_count))
        return;
    workers_start();
    if (worker_count == 0)
        return;
    SDL_LockMutex(queue_lock);
    struct SpritePackEntry *entry = &pack_entries[e];
    if (entry->state == SpE_Idle)
    {
        entry->state = SpE_Queued;
        if (urgent)
            push_urgent(e);
        else
            push_background(e);
        SDL_SignalCondition(queue_wake);
    }
    else if ((entry->state == SpE_Queued) && urgent)
    {
        push_urgent(e); // ahead of the background load
        SDL_SignalCondition(queue_wake);
    }
    SDL_UnlockMutex(queue_lock);
}

void sprite_pack_request_all(void)
{
    workers_start();
    if (worker_count == 0)
        return;
    SDL_LockMutex(queue_lock);
    for (int64_t e = 0; e < pack_entry_count; e++)
        if (pack_entries[e].state == SpE_Idle)
        {
            pack_entries[e].state = SpE_Queued;
            push_background((int32_t)e);
        }
    SDL_BroadcastCondition(queue_wake);
    SDL_UnlockMutex(queue_lock);
}

int64_t sprite_pack_publish(void)
{
    if (queue_lock == NULL)
        return 0;
    int64_t published = 0;
    SDL_LockMutex(queue_lock);
    for (size_t k = 0; k < ready_len; k++)
    {
        struct SpritePackEntry *e = &pack_entries[ready_list[k]];
        if (e->state == SpE_Ready)
        {
            e->lv = e->result;
            memset(&e->result, 0, sizeof(e->result));
            e->state = SpE_Built;
            e->uid = pack_next_uid++;
            pack_bytes += e->lv.bytes;
            published++;
        }
        else if (e->state == SpE_Failed)
        {
            pack_failed++;
            if ((pack_failed <= 8) && (e->source != SPRITE_PACK_GENERATED))
                WARNMSG("HD sprites: can't use %s/%s (unreadable, or smaller than the original's crop at the pack's scale)",
                    pack_sources[e->source].dir, pack_paths + e->png);
        }
    }
    ready_len = 0;
    SDL_UnlockMutex(queue_lock);
    return published;
}

int64_t sprite_pack_wait_idle(void)
{
    if (queue_lock != NULL)
    {
        SDL_LockMutex(queue_lock);
        while ((queue_urgent_len > 0) || (queue_background_len > 0) || (queue_busy > 0))
            SDL_WaitCondition(queue_idle, queue_lock);
        SDL_UnlockMutex(queue_lock);
    }
    return sprite_pack_publish();
}

static int cmp_last_used(const void *a, const void *b)
{
    const uint64_t ua = pack_entries[*(const int32_t *)a].last_used, ub = pack_entries[*(const int32_t *)b].last_used;
    return (ua < ub) ? -1 : ((ua > ub) ? 1 : 0);
}

int64_t sprite_pack_evict(uint64_t target)
{
    if (pack_bytes <= target)
        return 0;
    int32_t *built = (int32_t *)malloc((size_t)pack_entry_count * sizeof(int32_t));
    if (built == NULL)
        return 0;
    int64_t n = 0;
    for (int64_t e = 0; e < pack_entry_count; e++)
        if (pack_entries[e].state == SpE_Built)
            built[n++] = (int32_t)e;
    qsort(built, (size_t)n, sizeof(int32_t), cmp_last_used);
    int64_t evicted = 0;
    for (int64_t k = 0; (k < n) && (pack_bytes > target); k++)
    {
        struct SpritePackEntry *e = &pack_entries[built[k]];
        pack_bytes -= e->lv.bytes;
        sprite_hd_free_levels(&e->lv);
        e->state = SpE_Idle;
        evicted++;
    }
    free(built);
    return evicted;
}

void sprite_pack_close_all(void)
{
    workers_stop();
    for (int64_t e = 0; e < pack_entry_count; e++)
    {
        sprite_hd_free_levels(&pack_entries[e].lv);
        sprite_hd_free_levels(&pack_entries[e].result);
        free(pack_entries[e].gen);
    }
    free(pack_entries);
    pack_entries = NULL;
    pack_entry_count = pack_entry_capacity = 0;
    free(pack_sources);
    pack_sources = NULL;
    pack_source_count = 0;
    free(pack_paths);
    pack_paths = NULL;
    pack_paths_len = pack_paths_capacity = 0;
    free(pack_table);
    pack_table = NULL;
    pack_table_size = 0;
    queue_urgent_len = 0;
    queue_background_len = queue_background_head = 0;
    ready_len = 0;
    queue_busy = 0;
    pack_bytes = 0;
    pack_failed = 0;
    SDL_SetAtomicInt(&decode_us, 0);
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
