/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_camera.c
 *     Engine-window and camera-zoom helpers for the renderer.
 * @par Purpose:
 *     What is left here after refactor pass 2's S07 moved the synced player
 *     cameras to kfx_sim's player_camera.c: the engine window size and
 *     position, and scaling a zoom level to the screen. camera_zoom is the
 *     zoom the 3D engine draws with.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     20 Mar 2009 - 30 Mar 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "engine_camera.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_video.h"

#include "engine_redraw.h"
#include "player_data.h"
#include "kfx_sim_state.h"
#include "ports/ui_port.h"
#include "local_state.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/******************************************************************************/
int64_t camera_zoom;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
/******************************************************************************/

// angles_to_vector()/get_angle_xy_to_vec()/get_angle_yz_to_vec()/
// get_angle_xy_to()/get_angle_yz_to()/get_2d_distance()/
// get_2d_distance_squared() moved to bflib_math.c (kfx_platform, stage 7
// prep) -- pure geometry/trig with no camera or rendering dependency,
// used pervasively by kfx_sim (creature AI, pathfinding).
// project_point_to_wall_on_angle() moved to map_blocks.c (kfx_sim) --
// its only callers are kfx_sim (thing_creature.c/thing_traps.c) and it
// depends on kfx_sim's point_in_map_is_solid(). See
// docs/refactor/stage-07-kfx-render.md.


/** Scales camera zoom for current screen resolution.
 *
 * @param zoom_lvl Unscaled zoom level.
 * @return Zoom level scaled with use of current units_per_pixel value.
 */
uint64_t scale_camera_zoom_to_screen(uint64_t zoom_lvl)
{
    return scale_fixed_DK_value(zoom_lvl);
}

void change_engine_window_relative_size(int64_t w_delta, int64_t h_delta)
{
    setup_engine_window(local_state.engine_window_x, local_state.engine_window_y,
        local_state.engine_window_width+w_delta, local_state.engine_window_height+h_delta);
}

void centre_engine_window(void)
{
    int64_t window_center_x;
    int64_t window_center_y;
    if ((kfx_sim_state.operation_flags & GOF_ShowGui) != 0) {
      int64_t status_panel_width = ui_get_status_panel_width();
      window_center_x = (MyScreenWidth-local_state.engine_window_width-status_panel_width) / 2 + status_panel_width;
    }
    else
      window_center_x = (MyScreenWidth-local_state.engine_window_width) / 2;
    window_center_y = (MyScreenHeight-local_state.engine_window_height) / 2;
    setup_engine_window(window_center_x, window_center_y, local_state.engine_window_width, local_state.engine_window_height);
}

/******************************************************************************/
