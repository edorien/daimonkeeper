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
#include <stdint.h>

struct StateCheck;

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

/**
 * Whether the navigation mesh in `buf` (ariadne_saved_state_size() bytes, as ariadne_saved_state_write() makes it) stays
 * inside its tables: counts within the arrays, points, neighbours, regions and free lists in range, lists that end.
 * The buffer is tried on the live structures and the live mesh put back exactly as it was, whatever the answer
 * (security pass S06). On a refusal `why` says which field.
 */
int ariadne_saved_state_check(const void *buf, char *why, size_t why_size);

/** The same checks on the live mesh, noting every kind of violation (state_check.h); for calibrating on real levels. */
int64_t ariadne_live_state_survey(char *report, size_t report_size);
/** Each module's part of the checks. @return 0 to stop (first violation), 1 to go on. */
int ariadne_tringls_check_saved_state(struct StateCheck *c);
int ariadne_points_check_saved_state(struct StateCheck *c);
int ariadne_regions_check_saved_state(struct StateCheck *c);
int ariadne_findcache_check_saved_state(struct StateCheck *c);
/** A level's mesh starts from the mesh as start-up left it (refactor pass 5, S12): the baseline is taken at the end of
 *  start-up, the reset runs at a level's start. */
void ariadne_take_level_baseline(void);
void ariadne_reset_for_level(void);

void ariadne_tringls_visit_saved_state(AriadneStateVisitor visit, void *ctx);
void ariadne_points_visit_saved_state(AriadneStateVisitor visit, void *ctx);
void ariadne_regions_visit_saved_state(AriadneStateVisitor visit, void *ctx);
void ariadne_findcache_visit_saved_state(AriadneStateVisitor visit, void *ctx);
void ariadne_update_restored_state_ready(void);
void ariadne_update_reset_for_level(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
