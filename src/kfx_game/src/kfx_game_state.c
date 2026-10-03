/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file kfx_game_state.c
 *     Global instance for kfx_game_state.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx_game_state.h"
#include "state_versions.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct KfxGameState kfx_game_state;
// Saved/resynced as raw bytes: see state_versions.h before changing the layout.
_Static_assert(sizeof(struct KfxGameState) == KFX_GAME_STATE_SIZE, "struct KfxGameState changed size: bump KFX_GAME_STATE_VER and update KFX_GAME_STATE_SIZE in state_versions.h");
struct KfxGameLocal kfx_game_local;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
