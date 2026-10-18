/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file ariadne_saved_state.c
 *     Ariadne's navigation mesh, saved and resynced with the game.
 * @par Purpose:
 *     See ariadne_saved_state.h (refactor pass 4, P4-F7).
 */
/******************************************************************************/
#include "pre_inc.h"
#include <stdio.h>
#include <stdlib.h>
#include "ariadne_saved_state.h"

#include <string.h>

#include "ariadne.h"
#include "kfx_pathfinding_state.h"
#include "ariadne_tringls.h"
#include "ariadne_state_check.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct SizeCursor { size_t n; };
struct WriteCursor { unsigned char *p; };
struct ReadCursor { const unsigned char *p; };

static void visit_size(void *ctx, void *data, size_t size)
{
    (void)data;
    ((struct SizeCursor *)ctx)->n += size;
}

static void visit_write(void *ctx, void *data, size_t size)
{
    struct WriteCursor *c = (struct WriteCursor *)ctx;
    memcpy(c->p, data, size);
    c->p += size;
}

static void visit_read(void *ctx, void *data, size_t size)
{
    struct ReadCursor *c = (struct ReadCursor *)ctx;
    memcpy(data, c->p, size);
    c->p += size;
}

static void visit_all(AriadneStateVisitor visit, void *ctx)
{
    visit(ctx, &kfx_pathfinding_state, sizeof(kfx_pathfinding_state));
    ariadne_tringls_visit_saved_state(visit, ctx);
    ariadne_points_visit_saved_state(visit, ctx);
    ariadne_regions_visit_saved_state(visit, ctx);
    ariadne_findcache_visit_saved_state(visit, ctx);
}

void ariadne_saved_state_visit(AriadneStateVisitor visit, void *ctx)
{
    visit_all(visit, ctx);
}

void ariadne_saved_state_restored(void)
{
    ariadne_update_restored_state_ready();
    set_nav_rule_default();
}

size_t ariadne_saved_state_size(void)
{
    struct SizeCursor c = {0};
    visit_all(visit_size, &c);
    return c.n;
}

void ariadne_saved_state_write(void *buf)
{
    struct WriteCursor c = {(unsigned char *)buf};
    visit_all(visit_write, &c);
}

void ariadne_saved_state_read(const void *buf)
{
    struct ReadCursor c = {(const unsigned char *)buf};
    visit_all(visit_read, &c);
    ariadne_saved_state_restored();
}
/* The checks over the live mesh: the pathfinding state's own numbers, then each module's tables. */
static int check_live(struct StateCheck *c)
{
    ARI_RANGE(c, kfx_pathfinding_state.navigation_map_size_x, 0, MAX_SUBTILES_X, "ariadne.navigation_map_size_x", 0);
    ARI_RANGE(c, kfx_pathfinding_state.navigation_map_size_y, 0, MAX_SUBTILES_Y, "ariadne.navigation_map_size_y", 0);
    ARI_RANGE(c, kfx_pathfinding_state.map_subtiles_x, 0, MAX_SUBTILES_X, "ariadne.map_subtiles_x", 0);
    ARI_RANGE(c, kfx_pathfinding_state.map_subtiles_y, 0, MAX_SUBTILES_Y, "ariadne.map_subtiles_y", 0);
    return ariadne_tringls_check_saved_state(c) && ariadne_points_check_saved_state(c)
        && ariadne_regions_check_saved_state(c) && ariadne_findcache_check_saved_state(c);
}

int ariadne_saved_state_check(const void *buf, char *why, size_t why_size)
{
    const size_t size = ariadne_saved_state_size();
    unsigned char *backup = (unsigned char *)malloc(size);
    if (backup == NULL)
    {
        if ((why != NULL) && (why_size > 0))
            snprintf(why, why_size, "no memory to check the navigation mesh");
        return 0;
    }
    // Try it on the live tables, then put the live mesh back exactly as it was (no post-read hooks: nothing is
    // declared ready), whatever the verdict.
    struct WriteCursor w = {backup};
    visit_all(visit_write, &w);
    struct ReadCursor r = {(const unsigned char *)buf};
    visit_all(visit_read, &r);
    if ((why != NULL) && (why_size > 0))
        why[0] = '\0';
    struct StateCheck chk = { why, why_size, NULL, 0 };
    const int ok = check_live(&chk);
    struct ReadCursor back = {backup};
    visit_all(visit_read, &back);
    free(backup);
    return ok;
}

int64_t ariadne_live_state_survey(char *report, size_t report_size)
{
    struct StateSurveyEntry entries[STATE_SURVEY_MAX];
    memset(entries, 0, sizeof(entries));
    struct StateCheck chk = { NULL, 0, entries, 0 };
    check_live(&chk);
    return state_check_report(&chk, report, report_size);
}

/* What a level's mesh starts from: the saved state's parts as start-up left them, restored at a level's start
 * before the level builds its mesh, so no part of the previous level's mesh is left (refactor pass 5, S12; P5-F22
 * and P5-F28 were two such parts). Only the data: the level's own set-up follows. */
static unsigned char *ariadne_level_baseline = NULL;

void ariadne_take_level_baseline(void)
{
    if (ariadne_level_baseline == NULL)
        ariadne_level_baseline = (unsigned char *)malloc(ariadne_saved_state_size());
    if (ariadne_level_baseline != NULL)
        ariadne_saved_state_write(ariadne_level_baseline);
}

void ariadne_reset_for_level(void)
{
    if (ariadne_level_baseline == NULL)
        return;
    struct ReadCursor c = {(const unsigned char *)ariadne_level_baseline};
    visit_all(visit_read, &c);
    // the stand-in for an invalid triangle, outside the saved state: as the process started it
    memset(&bad_triangle, 0, sizeof(bad_triangle));
    // and the mesh is built from nothing, as in a process's first level
    ariadne_update_reset_for_level();
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
