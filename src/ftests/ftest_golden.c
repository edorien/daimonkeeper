/******************************************************************************/
/** @file ftest_golden.c
 *     Golden hashes of what a script line (or anything else) does to a saved level: see ftest_golden.h.
 */
/******************************************************************************/
#include "ftest_golden.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ftest.h"

#include "ariadne_saved_state.h"
#include "config_keeperfx.h"
#include "dungeon_data.h"
#include "game_merge.h"
#include "game_saves.h"
#include "kfx_config_state.h"
#include "kfx_game_state.h"
#include "kfx_sim_state.h"
#include "lvl_script_conditions.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The state hashed, and its reference: the state right after loading the save, with a mask of the bytes that
 * differ between two loads of it. A block with a snapshot function is a copy, taken again before each use. */
struct GoldenBlock { void *ptr; size_t size; void (*snapshot)(void *buf); unsigned char *ref; unsigned char *volatile_mask; };
static struct GoldenBlock golden_blocks[5];
#define GOLDEN_BLOCKS_COUNT ((int)(sizeof(golden_blocks) / sizeof(golden_blocks[0])))

static void golden_snapshot(void)
{
    for (int i = 0; i < GOLDEN_BLOCKS_COUNT; i++)
        if (golden_blocks[i].snapshot != NULL)
            golden_blocks[i].snapshot(golden_blocks[i].ptr);
}

/** first: keep the current state as the reference; then: mark what differs from it as volatile. */
static void golden_reference(TbBool first)
{
    golden_snapshot();
    for (int i = 0; i < GOLDEN_BLOCKS_COUNT; i++)
    {
        struct GoldenBlock *b = &golden_blocks[i];
        if (first)
        {
            free(b->ref);
            free(b->volatile_mask);
            b->ref = (unsigned char *)malloc(b->size);
            b->volatile_mask = (unsigned char *)calloc(b->size, 1);
            memcpy(b->ref, b->ptr, b->size);
            continue;
        }
        const unsigned char *cur = (const unsigned char *)b->ptr;
        for (size_t k = 0; k < b->size; k++)
            if (cur[k] != b->ref[k])
                b->volatile_mask[k] = 1;
    }
}

/* KFX_FTEST_GOLDEN_CUT: byte ranges taken out of a block before hashing, as if the fields there weren't in the
 * layout. A layout commit's check (refactor pass 5, S04): the commit before it, run with the removed fields cut,
 * must print the hashes the new layout prints. "<block>:<start>:<len>:<stride>:<count>[,...]": count ranges of
 * len bytes at start, start + stride, ...; ranges don't overlap. */
struct GoldenCut { int64_t block; uint64_t start, len, stride, count; TbBool mask; };
static struct GoldenCut golden_cuts[16];
static int64_t golden_cuts_count = -1;

static void golden_cuts_read(void)
{
    if (golden_cuts_count >= 0)
        return;
    golden_cuts_count = 0;
    for (int which = 0; which < 2; which++)
    {
    const char *spec = getenv((which == 0) ? "KFX_FTEST_GOLDEN_CUT" : "KFX_FTEST_GOLDEN_MASK");
    while ((spec != NULL) && (*spec != '\0') && (golden_cuts_count < (int64_t)(sizeof(golden_cuts) / sizeof(golden_cuts[0]))))
    {
        struct GoldenCut c;
        int used = 0;
        if (sscanf(spec, "%" SCNd64 ":%" SCNu64 ":%" SCNu64 ":%" SCNu64 ":%" SCNu64 "%n", &c.block, &c.start, &c.len,
            &c.stride, &c.count, &used) != 5)
        {
            FTESTLOG("KFX_FTEST_GOLDEN_CUT: can't read \"%s\"", spec);
            break;
        }
        if (c.stride < c.len)
            c.stride = c.len;
        c.mask = (which == 1);
        golden_cuts[golden_cuts_count++] = c;
        FTESTLOG("%s from block %" PRId64 ": %" PRIu64 " x %" PRIu64 " bytes at %" PRIu64 ", every %" PRIu64,
            c.mask ? "masked" : "cut", c.block, c.count, c.len, c.start, c.stride);
        spec += used;
        if (*spec == ',')
            spec++;
    }
    }
}

/** Offset k of block i as it is without the cut ranges; UINT64_MAX when k is in one. */
static uint64_t golden_cut_offset(int i, uint64_t k)
{
    uint64_t removed = 0;
    for (int64_t n = 0; n < golden_cuts_count; n++)
    {
        const struct GoldenCut *c = &golden_cuts[n];
        if ((c->block != i) || (k < c->start))
            continue;
        uint64_t nth = (k - c->start) / c->stride;
        if (nth >= c->count)
        {
            if (!c->mask)
                removed += c->count * c->len;
            continue;
        }
        if ((k - c->start) % c->stride < c->len)
            return UINT64_MAX;
        if (!c->mask)
            removed += (nth + 1) * c->len;
    }
    return k - removed;
}

static uint64_t golden_state_hash(int64_t *changed_bytes)
{
    uint64_t r = 1469598103934665603ULL;
    int64_t n = 0;
    golden_cuts_read();
    golden_snapshot();
    for (int i = 0; i < GOLDEN_BLOCKS_COUNT; i++)
    {
        const struct GoldenBlock *b = &golden_blocks[i];
        const unsigned char *cur = (const unsigned char *)b->ptr;
        for (size_t k = 0; k < b->size; k++)
        {
            if ((cur[k] != b->ref[k]) && !b->volatile_mask[k])
            {
                uint64_t kk = (golden_cuts_count > 0) ? golden_cut_offset(i, k) : k;
                if (kk == UINT64_MAX)
                    continue;
                r = (r ^ ((uint64_t)i << 56) ^ kk ^ ((uint64_t)cur[k] << 40)) * 0x100000001b3ULL;
                n++;
            }
        }
    }
    *changed_bytes = n;
    return r;
}

TbBool ftest_golden_load(const struct FTestGolden *g)
{
    if (!load_game(g->slot))
        return false;
    clear_flag(kfx_sim_state.operation_flags, GOF_Paused);
    reset_script_conditions(); // as after reading a level script: no IF open
    return true;
}

TbBool ftest_golden_begin(struct FTestGolden *g, int64_t slot, const struct FTestGoldenExpected *expected)
{
    memset(g, 0, sizeof(*g));
    g->slot = slot;
    g->expected = expected;
    g->print = (getenv("KFX_FTEST_GOLDEN_PRINT") != NULL);
    fill_game_catalogue_slot(slot, "golden");
    set_flag(kfx_sim_state.operation_flags, GOF_Paused);
    g->saved = save_game(slot);
    clear_flag(kfx_sim_state.operation_flags, GOF_Paused);
    if (!g->saved)
    {
        FTEST_FAIL_TEST("save_game failed");
        return false;
    }
    golden_blocks[0] = (struct GoldenBlock){&kfx_sim_state, sizeof(kfx_sim_state), NULL, NULL, NULL};
    golden_blocks[1] = (struct GoldenBlock){&kfx_game_state, sizeof(kfx_game_state), NULL, NULL, NULL};
    golden_blocks[2] = (struct GoldenBlock){&kfx_config_state, sizeof(kfx_config_state), NULL, NULL, NULL};
    golden_blocks[3] = (struct GoldenBlock){&intralvl, sizeof(intralvl), NULL, NULL, NULL};
    // the pathfinding state as a save holds it: kfx_pathfinding_state and Ariadne's mesh
    if (golden_blocks[4].ptr == NULL)
    {
        golden_blocks[4].size = ariadne_saved_state_size();
        golden_blocks[4].ptr = malloc(golden_blocks[4].size);
        golden_blocks[4].snapshot = ariadne_saved_state_write;
    }
    if (!ftest_golden_load(g))
    {
        FTEST_FAIL_TEST("load_game failed");
        return false;
    }
    golden_reference(true);
    ftest_golden_load(g);
    golden_reference(false);
    // the level statistics' real times, which winning or losing writes from the wall clock
    for (int64_t i = 0; i < DUNGEONS_COUNT; i++)
    {
        struct LevelStats *st = &kfx_sim_state.dungeon[i].lvstats;
        memset(golden_blocks[0].volatile_mask + ((char *)&st->end_time - (char *)&kfx_sim_state), 1, sizeof(st->end_time));
        memset(golden_blocks[0].volatile_mask + ((char *)&st->gameplay_time - (char *)&kfx_sim_state), 1, sizeof(st->gameplay_time));
    }
    int64_t volatile_bytes = 0;
    for (int i = 0; i < GOLDEN_BLOCKS_COUNT; i++)
        for (size_t k = 0; k < golden_blocks[i].size; k++)
            volatile_bytes += golden_blocks[i].volatile_mask[k];
    FTESTLOG("%" PRId64 " bytes differ between two loads of the save; they are left out", volatile_bytes);
    return true;
}

/** KFX_FTEST_GOLDEN_DUMP=<text>: each changed byte of the cases whose name contains the text. */
static void golden_dump(const char *name)
{
    const char *dump = getenv("KFX_FTEST_GOLDEN_DUMP");
    if ((dump == NULL) || (strstr(name, dump) == NULL))
        return;
    for (int i = 0; i < GOLDEN_BLOCKS_COUNT; i++)
    {
        const struct GoldenBlock *b = &golden_blocks[i];
        const unsigned char *cur = (const unsigned char *)b->ptr;
        for (size_t k = 0; k < b->size; k++)
            if ((cur[k] != b->ref[k]) && !b->volatile_mask[k])
                FTESTLOG("DUMP %s: block %d offset %" PRIu64 ": %d -> %d", name, i, (uint64_t)k, (int)b->ref[k], (int)cur[k]);
    }
}

TbBool ftest_golden_selected(const char *name)
{
    const char *only = getenv("KFX_FTEST_GOLDEN_ONLY");
    return (only == NULL) || (strstr(name, only) != NULL);
}

void ftest_golden_check(struct FTestGolden *g, const char *name)
{
    ftest_golden_check_result(g, name, NULL);
}

void ftest_golden_check_result(struct FTestGolden *g, const char *name, const char *result)
{
    int64_t changed = 0;
    uint64_t h = golden_state_hash(&changed);
    if (result != NULL)
    {
        for (const char *p = result; *p != '\0'; p++)
            h = (h ^ (unsigned char)*p) * 0x100000001b3ULL;
        h = (h ^ 0xffULL) * 0x100000001b3ULL; // a result, even an empty one, differs from none
    }
    golden_dump(name);
    if (g->print)
    {
        char quoted[600];
        size_t q = 0;
        for (const char *p = name; (*p != '\0') && (q + 2 < sizeof(quoted)); p++)
        {
            if ((*p == '"') || (*p == '\\'))
                quoted[q++] = '\\';
            quoted[q++] = *p;
        }
        quoted[q] = '\0';
        if (result != NULL)
        {
            char shown[121];
            snprintf(shown, sizeof(shown), "%s", result);
            for (char *p = shown; *p != '\0'; p++)
                if ((*p == '\n') || (*p == '\r'))
                    *p = ' ';
            FTESTLOG("GOLDEN:    {\"%s\", 0x%016" PRIx64 "ULL}, // %" PRId64 " bytes; %s", quoted, h, changed, shown);
        }
        else
            FTESTLOG("GOLDEN:    {\"%s\", 0x%016" PRIx64 "ULL}, // %" PRId64 " bytes", quoted, h, changed);
        return;
    }
    const struct FTestGoldenExpected *e = g->expected;
    while ((e->name != NULL) && (strcmp(e->name, name) != 0))
        e++;
    if (e->name == NULL)
    {
        FTESTLOG("no golden hash for %s", name);
        g->failures++;
    }
    else if (e->hash != h)
    {
        FTESTLOG("changed: %s", name);
        g->failures++;
    }
    g->checked++;
}

void ftest_golden_finish(struct FTestGolden *g, const char *what)
{
    ftest_golden_load(g);
    if (g->print)
    {
        FTESTLOG("printed the %s golden hashes", what);
    }
    else if (g->failures > 0)
    {
        FTEST_FAIL_TEST("%" PRId64 " of %" PRId64 " %s hashes differ", g->failures, g->checked, what);
    }
    else
    {
        FTESTLOG("all %" PRId64 " %s hashes match", g->checked, what);
    }
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
