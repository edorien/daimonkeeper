/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file kfx_render_state.c
 *     Global instance for kfx_render_state.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include <string.h>

#include "kfx_render_state.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct KfxRenderState kfx_render_state;

void render_state_reset_for_level(void)
{
    kfx_render_state.pointer_x = 0;
    kfx_render_state.pointer_y = 0;
    kfx_render_state.block_pointed_at_x = 0;
    kfx_render_state.block_pointed_at_y = 0;
    kfx_render_state.pointed_at_frac_x = 0;
    kfx_render_state.pointed_at_frac_y = 0;
    kfx_render_state.top_pointed_at_x = 0;
    kfx_render_state.top_pointed_at_y = 0;
    kfx_render_state.top_pointed_at_frac_x = 0;
    kfx_render_state.top_pointed_at_frac_y = 0;
    kfx_render_state.thing_pointed_at = NULL;
    kfx_render_state.me_pointed_at = NULL;
    memset(&kfx_render_state.mouse_light_pos, 0, sizeof(kfx_render_state.mouse_light_pos));
    kfx_render_state.local_cursor_valid = false;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
