/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file local_state.h
 *     The local machine's presentation state: the predicted view type,
 *     palette fades, the map's UI hold, the minimap, the thing under the
 *     hand. Never synced, saved or checksummed. kfx_render and kfx_frontend
 *     own it; kfx_sim reaches it only through UiPort's
 *     local_view_transition and RenderPort's local_view_type_settle
 *     (refactor pass 2, S15; it was in kfx_sim's player_data.h).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_LOCAL_STATE_H
#define DK_LOCAL_STATE_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
extern struct LocalState {
    unsigned char view_type;
    TbBool tooltips_restore; /**< Used to store/restore the value of settings.tooltips_on when transitioning to/from the map. */
    TbBool status_menu_restore; /**< Used to store/restore the current status menu visibility when the map is shown/hidden. */
    TbBool status_menu_hidden_for_map; /**< The status menu is hidden for the map and status_menu_restore holds its visibility. */
    TbBool tooltips_hidden_for_map; /**< Tooltips are off for a map fade and tooltips_restore holds the setting. */
    TbBool paused_state_restore; /**< Used to restore pause state after saving */
    TbBool display_needs_update;
    int64_t local_thing_under_hand;
    TbBool swipe_sprite_drawLR; /**< Used to decide whether to draw the swipe sprite left to right (TRUE), or [default] right to left (FALSE). */
    unsigned char *lens_palette;
    unsigned char *main_palette;
    int64_t palette_fade_step_map;
    int64_t palette_fade_step_pain;
    int64_t palette_fade_step_possession;
    int64_t engine_window_width;
    int64_t engine_window_height;
    int64_t engine_window_x;
    int64_t engine_window_y;
    int64_t minimap_pos_x;
    int64_t minimap_pos_y;
    int64_t minimap_zoom;
    int64_t roomspace_size;
    // FIXME: use fixed-point precision instead
    double camera_movement_x;
    double camera_movement_y;
    TbBool camera_speedup_pressed;
    // freecam. TODO: use spectator implementation instead, once that is implemented
    TbBool replay_detached;
    unsigned char replay_view_type;
    unsigned char replay_cam_idx;
} local_state;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
