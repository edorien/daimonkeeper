/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file kfx_sim_state.c
 *     Global instance for kfx_sim_state.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx_sim_state.h"
#include "state_versions.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct KfxSimState kfx_sim_state;
// Saved/resynced as raw bytes: see state_versions.h before changing the layout.
_Static_assert(sizeof(struct KfxSimState) == KFX_SIM_STATE_SIZE, "struct KfxSimState changed size: bump KFX_SIM_STATE_VER and update KFX_SIM_STATE_SIZE in state_versions.h");

/******************************************************************************/
#ifdef __cplusplus
}
#endif
