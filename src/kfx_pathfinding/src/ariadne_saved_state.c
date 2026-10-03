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
#include "ariadne_saved_state.h"

#include <string.h>

#include "ariadne.h"
#include "kfx_pathfinding_state.h"
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
    ariadne_update_restored_state_ready();
    set_nav_rule_default();
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
