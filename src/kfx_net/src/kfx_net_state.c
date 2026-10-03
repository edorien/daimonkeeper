/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file kfx_net_state.c
 *     Global instance for kfx_net_state.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx_net_state.h"
#include "state_versions.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct KfxNetState kfx_net_state;
// Saved/resynced as raw bytes: see state_versions.h before changing the layout.
_Static_assert(sizeof(struct KfxNetState) == KFX_NET_STATE_SIZE, "struct KfxNetState changed size: bump KFX_NET_STATE_VER and update KFX_NET_STATE_SIZE in state_versions.h");
struct KfxNetLocal kfx_net_local;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
