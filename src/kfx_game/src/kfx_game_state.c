/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file kfx_game_state.c
 *     Global instance for kfx_game_state.h.
 */
/******************************************************************************/
#include "pre_inc.h"

#include <stdlib.h>
#include <string.h>

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

static struct KfxGameState *game_level_baseline = NULL;

void game_state_take_level_baseline(void)
{
    if (game_level_baseline == NULL)
        game_level_baseline = (struct KfxGameState *)malloc(sizeof(struct KfxGameState));
    if (game_level_baseline != NULL)
        memcpy(game_level_baseline, &kfx_game_state, sizeof(struct KfxGameState));
}

void game_state_reset_for_level(void)
{
    if (game_level_baseline == NULL)
        return;
    // kept: this machine's mouse and a pending single-frame step (input, not the level's)
    const TbBool frame_step = kfx_game_state.frame_step;
    const int64_t my_mouse_x = kfx_game_state.my_mouse_x;
    const int64_t my_mouse_y = kfx_game_state.my_mouse_y;
    memcpy(&kfx_game_state, game_level_baseline, sizeof(struct KfxGameState));
    kfx_game_state.frame_step = frame_step;
    kfx_game_state.my_mouse_x = my_mouse_x;
    kfx_game_state.my_mouse_y = my_mouse_y;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
