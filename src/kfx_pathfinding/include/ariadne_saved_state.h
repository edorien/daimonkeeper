/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file ariadne_saved_state.h
 *     Ariadne's navigation mesh, saved and resynced with the game.
 * @par Purpose:
 *     The mesh (triangles, points, regions, the point-location cache, and
 *     kfx_pathfinding_state's navigation map) is updated incrementally as the
 *     map changes, so it is not a function of the map: a mesh built again
 *     from the map is a different mesh, and creatures' paths follow it
 *     differently. A loaded or resynced game gets the same mesh back instead
 *     of a rebuilt one (refactor pass 4, P4-F7). Each Ariadne file lists its
 *     own part with a *_visit_saved_state() function; the block is their
 *     concatenation, KFX_ARIADNE_STATE_SIZE bytes (state_versions.h).
 * @par Comment:
 *     Per-search and per-update scratch (the search tree, the heap, the
 *     border and edge lists, the Delaunay stack) is not part of it.
 */
/******************************************************************************/
#ifndef DK_ARIADNE_SAVED_STATE_H
#define DK_ARIADNE_SAVED_STATE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/** Called with each piece of the saved state in turn: its address and size. */
typedef void (*AriadneStateVisitor)(void *ctx, void *data, size_t size);

/** Size of the saved state block. */
size_t ariadne_saved_state_size(void);
/** Writes the saved state to buf, ariadne_saved_state_size() bytes. */
void ariadne_saved_state_write(void *buf);
/** Restores the saved state from buf, then sets up what init_navigation() would besides the mesh. */
void ariadne_saved_state_read(const void *buf);

void ariadne_tringls_visit_saved_state(AriadneStateVisitor visit, void *ctx);
void ariadne_points_visit_saved_state(AriadneStateVisitor visit, void *ctx);
void ariadne_regions_visit_saved_state(AriadneStateVisitor visit, void *ctx);
void ariadne_findcache_visit_saved_state(AriadneStateVisitor visit, void *ctx);
void ariadne_update_restored_state_ready(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
