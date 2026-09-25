/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file kfx_pathfinding_state.h
 *     Header file for kfx_pathfinding_state.c.
 * @par Purpose:
 *     Ariadne's own navigation-map cache, migrated out of kfx_sim_state.h
 *     (see docs/refactor/stage-06a-ariadne-pathfinding-interface.md, §4 and
 *     §14's "physical split" notes). It was homed there by stage 6's field
 *     migration along with everything else map-shaped, despite being
 *     private to ariadne, not general simulation state -- nothing outside
 *     ariadne (`ariadne_update.c`'s accessors are the only entry point)
 *     ever reads or writes it directly.
 *
 *     Deliberately NOT wired into the save-game chunk format
 *     (kfx_game/src/game_saves.c) or the network resync blob
 *     (kfx_net/src/net_resync.cpp), unlike kfx_sim_state/kfx_net_state/
 *     kfx_game_state/kfx_frontend_state. Verified before making that call:
 *     both paths that apply those structs' raw blobs (game_saves.c's
 *     load_game_chunks() and net_resync.cpp's resync_game()) unconditionally
 *     call reinit_level_after_load(), which calls init_navigation() --
 *     rebuilding this cache from scratch -- before anything can read it.
 *     So navigation_map is fully redundant in both wire formats today; not
 *     persisting it avoids two speculative save/resync-format changes that
 *     could not be end-to-end tested (no live game session) for zero actual
 *     benefit. It IS zeroed in main_game.c's clear_complete_game(), matching
 *     every sibling per-library state struct there, purely for the "every
 *     state struct gets zeroed at complete-game-clear time" invariant --
 *     also functionally moot given the same recompute-before-use guarantee,
 *     but kept for consistency/least-surprise.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_KFX_PATHFINDING_STATE_H
#define DK_KFX_PATHFINDING_STATE_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct KfxPathfindingState {
    int64_t navigation_map_size_x;
    int64_t navigation_map_size_y;
    NavColour navigation_map[MAX_SUBTILES_X*MAX_SUBTILES_Y];
    TbBool map_changed_for_navigation;
};

extern struct KfxPathfindingState kfx_pathfinding_state;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
