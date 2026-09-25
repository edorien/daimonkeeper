/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_camera.c
 *     Camera move, maintain and support functions.
 * @par Purpose:
 *     Defines and maintains cameras.
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
#include "bflib_math.h"
#include "bflib_video.h"
#include "bflib_sprite.h"
#include "bflib_vidraw.h"
#include "bflib_planar.h"

#include "engine_lenses.h"
#include "engine_render.h"
#include "engine_redraw.h"
#include "thing_physics.h"
#include "vidmode.h"
#include "map_blocks.h"
#include "dungeon_data.h"
#include "config_settings.h"
#include "player_instances.h"
#include "player_data.h"
#include "local_camera.h"
#include "render_overlay.h"
#include "kfx_sim_state.h"
#include "thing_objects.h"
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

static void view_set_camera_position(struct Camera *cam, MapCoord x, MapCoord y)
{
    cam->mappos.x.val = clamp(x, 0, kfx_sim_state.map_subtiles_x * COORD_PER_STL - 1);
    cam->mappos.y.val = clamp(y, 0, kfx_sim_state.map_subtiles_y * COORD_PER_STL - 1);
}

void view_zoom_camera_in(struct Camera *cam, int64_t limit_max, int64_t limit_min)
{
    int64_t new_zoom;
    int64_t old_zoom = get_camera_zoom(cam);
    switch (cam->view_mode)
    {
    case PVM_IsoWibbleView:
    case PVM_IsoStraightView:
        new_zoom = (100 * old_zoom) / 85;
        if (new_zoom == old_zoom)
            new_zoom++;
        if (new_zoom < limit_min) {
            new_zoom = limit_min;
        } else
        if (new_zoom > limit_max) {
            new_zoom = limit_max;
        }
        break;
    case PVM_ParchmentView:
        new_zoom = (5 * old_zoom) / 4;
        if (new_zoom == old_zoom)
            new_zoom++;
        if (new_zoom < 16) {
            new_zoom = 16;
        } else
        if (new_zoom > 1024) {
            new_zoom = 1024;
        }
        break;
    case PVM_FrontView:
        new_zoom = (100 * old_zoom) / 85;
        if (new_zoom == old_zoom)
            new_zoom++;
        if (new_zoom < FRONTVIEW_CAMERA_ZOOM_MIN) { //Originally 16384, adjusted for view distance
            new_zoom = FRONTVIEW_CAMERA_ZOOM_MIN;
        } else
        if (new_zoom > FRONTVIEW_CAMERA_ZOOM_MAX) {
            new_zoom = FRONTVIEW_CAMERA_ZOOM_MAX;
        }
        break;
    default:
        new_zoom = old_zoom;
    }
    set_camera_zoom(cam, new_zoom);
}

void set_camera_zoom(struct Camera *cam, int64_t new_zoom)
{
    if (cam == NULL)
      return;
    switch (cam->view_mode)
    {
    case PVM_IsoWibbleView:
    case PVM_FrontView:
    case PVM_IsoStraightView:
        cam->zoom = new_zoom;
        break;
    case PVM_ParchmentView:
        cam->mappos.z.val = new_zoom;
        break;
    }
}

void view_zoom_camera_out(struct Camera *cam, int64_t limit_max, int64_t limit_min)
{
    int64_t new_zoom;
    int64_t old_zoom = get_camera_zoom(cam);
    switch (cam->view_mode)
    {
    case PVM_IsoWibbleView:
    case PVM_IsoStraightView:
        new_zoom = (85 * old_zoom) / 100;
        if (new_zoom == old_zoom)
            new_zoom--;
        if (new_zoom < limit_min) {
            new_zoom = limit_min;
        } else
        if (new_zoom > limit_max) {
            new_zoom = limit_max;
        }
        break;
    case PVM_ParchmentView:
        new_zoom = (4 * old_zoom) / 5;
        if (new_zoom == old_zoom)
            new_zoom--;
        if (new_zoom < 16) {
            new_zoom = 16;
        } else
        if (new_zoom > 1024) {
            new_zoom = 1024;
        }
        break;
    case PVM_FrontView:
        new_zoom = (85 * old_zoom) / 100;
        if (new_zoom == old_zoom)
            new_zoom--;
        if (new_zoom < max(FRONTVIEW_CAMERA_ZOOM_MIN, kfx_config_state.frontview_zoom_distance_setting)) {
            new_zoom = max(FRONTVIEW_CAMERA_ZOOM_MIN, kfx_config_state.frontview_zoom_distance_setting);
        } else
        if (new_zoom > FRONTVIEW_CAMERA_ZOOM_MAX) {
            new_zoom = FRONTVIEW_CAMERA_ZOOM_MAX;
        }
        break;
    default:
        new_zoom = old_zoom;
    }
    set_camera_zoom(cam, new_zoom);
}

static void view_move_camera_on_zoom(struct Camera *cam, int64_t a, int64_t b, MapCoord x, MapCoord y)
{
    if ((x | y) < 0 || b == 0)
        return;

    const int64_t dx = x - cam->mappos.x.val;
    const int64_t dy = y - cam->mappos.y.val;
    const MapCoord new_x = cam->mappos.x.val + dx * (b - a) / b;
    const MapCoord new_y = cam->mappos.y.val + dy * (b - a) / b;
    view_set_camera_position(cam, new_x, new_y);
}

void view_zoom_camera_in_to(struct Camera *cam, int64_t limit_max, int64_t limit_min, MapCoord x, MapCoord y)
{
    const int64_t old_zoom = get_camera_zoom(cam);
    view_zoom_camera_in(cam, limit_max, limit_min);
    const int64_t new_zoom = get_camera_zoom(cam);
    view_move_camera_on_zoom(cam, old_zoom, new_zoom, x, y);
}

void view_zoom_camera_out_from(struct Camera *cam, int64_t limit_max, int64_t limit_min, MapCoord x, MapCoord y)
{
    const int64_t old_zoom = get_camera_zoom(cam);
    view_zoom_camera_out(cam, limit_max, limit_min);
    const int64_t new_zoom = get_camera_zoom(cam);
    view_move_camera_on_zoom(cam, old_zoom, new_zoom, x, y);
}

/**
 * Conducts clipping to zoom level of given camera, based on current screen mode.
 */
void update_camera_zoom_bounds(struct Camera *cam,uint64_t zoom_max,uint64_t zoom_min)
{
    SYNCDBG(7,"Starting");
    int64_t zoom_val = get_camera_zoom(cam);
    if (zoom_val < zoom_min)
    {
      zoom_val = zoom_min;
    } else
    if (zoom_val > zoom_max)
    {
      zoom_val = zoom_max;
    }
    set_camera_zoom(cam, zoom_val);
}

int64_t get_camera_zoom(struct Camera *cam)
{
    if (cam == NULL)
      return 0;
    switch (cam->view_mode)
    {
    case PVM_IsoWibbleView:
    case PVM_FrontView:
    case PVM_IsoStraightView:
        return cam->zoom;
    case PVM_ParchmentView:
        return cam->mappos.z.val;
    default:
        return 0;
    }
}


/** Scales camera zoom for current screen resolution.
 *
 * @param zoom_lvl Unscaled zoom level.
 * @return Zoom level scaled with use of current units_per_pixel value.
 */
uint64_t scale_camera_zoom_to_screen(uint64_t zoom_lvl)
{
    return scale_fixed_DK_value(zoom_lvl);
}

void view_set_camera_y_inertia(struct Camera *cam, int64_t delta, int64_t ilimit)
{
    int64_t abslimit = llabs(ilimit);
    cam->inertia_y += delta;
    if (cam->inertia_y < -abslimit) {
        cam->inertia_y = -abslimit;
    } else
    if (cam->inertia_y > abslimit) {
        cam->inertia_y = abslimit;
    }
    cam->in_active_movement_y = true;
}

void view_set_camera_x_inertia(struct Camera *cam, int64_t delta, int64_t ilimit)
{
    int64_t abslimit = llabs(ilimit);
    cam->inertia_x += delta;
    if (cam->inertia_x < -abslimit) {
        cam->inertia_x = -abslimit;
    } else
    if (cam->inertia_x > abslimit) {
        cam->inertia_x = abslimit;
    }
    cam->in_active_movement_x = true;
}

void view_set_camera_rotation_inertia(struct Camera *cam, int64_t delta, int64_t ilimit)
{
    const int64_t limit_val = llabs(ilimit);
    const int64_t new_val = delta + cam->inertia_rotation;
    cam->inertia_rotation = clamp(new_val, -limit_val, +limit_val);
    cam->in_active_movement_rotation = true;
}

void view_set_camera_rotation_inertia_around(struct Camera *cam, int64_t delta, int64_t ilimit, MapCoord x, MapCoord y)
{
    view_set_camera_rotation_inertia(cam, delta, ilimit);
    if ((x | y) < 0)
        return;

    cam->rotation_pivot.x.val = x;
    cam->rotation_pivot.y.val = y;
    cam->use_rotation_pivot = true;
}

void view_set_camera_tilt(struct Camera *cam, unsigned char mode)
{
    int64_t tilt;
    switch (mode)
    {
        case 0: // reset
        {
            tilt = CAMERA_TILT_DEFAULT;
            break;
        }
        case 1: // up
        {
            tilt = cam->rotation_angle_y;
            if (tilt < CAMERA_TILT_MAX)
            {
                tilt++;
            }
            break;
        }
        case 2: // down
        {
            tilt = cam->rotation_angle_y;
            if (tilt > CAMERA_TILT_MIN)
            {
                tilt--;
            }
            break;
        }
        default:
        {
            return;
        }
    }
    cam->rotation_angle_y = tilt;
}

void init_player_cameras(struct PlayerInfo *player)
{
    struct Thing* heartng = get_player_soul_container(player->id_number);
    struct Camera* cam = &player->cameras[CamIV_FirstPerson];
    cam->mappos.x.val = 0;
    cam->mappos.y.val = 0;
    cam->mappos.z.val = 256;
    cam->rotation_angle_y = 0;
    cam->rotation_angle_z = 0;
    cam->horizontal_fov = first_person_horizontal_fov;
    cam->rotation_angle_x = ANGLE_EAST;
    cam->view_mode = PVM_CreatureView;

    cam = &player->cameras[CamIV_Isometric];
    cam->mappos.x.val = heartng->mappos.x.val;
    cam->mappos.y.val = heartng->mappos.y.val;
    cam->mappos.z.val = 0;
    cam->rotation_angle_z = 0;
    cam->horizontal_fov = 94;
    cam->rotation_angle_y = settings.isometric_tilt;
    cam->rotation_angle_x = DEGREES_45;
    if (settings.video_rotate_mode == 1) {
        cam->view_mode = PVM_IsoStraightView;
    } else {
        cam->view_mode = PVM_IsoWibbleView;
    }
    cam->zoom = player->isometric_view_zoom_level;

    cam = &player->cameras[CamIV_Parchment];
    cam->mappos.x.val = 0;
    cam->mappos.y.val = 0;
    cam->mappos.z.val = 32;
    cam->horizontal_fov = 94;
    cam->view_mode = PVM_ParchmentView;

    cam = &player->cameras[CamIV_FrontView];
    cam->mappos.x.val = heartng->mappos.x.val;
    cam->mappos.y.val = heartng->mappos.y.val;
    cam->mappos.z.val = 32;
    cam->horizontal_fov = 94;
    cam->view_mode = PVM_FrontView;
    cam->zoom = player->frontview_zoom_level;

    init_local_cameras(player);
}

static int64_t get_walking_bob_direction(struct Thing *thing)
{
    const int64_t anim_time = thing->anim_time;
    if ( anim_time >= 256 && anim_time < 640 )
    {
        return ( thing->anim_speed < 0 ) ? -1 : 1;
    }
    else if ( anim_time >= 1024 && anim_time < 1408 )
    {
        return ( thing->anim_speed < 0 ) ? -1 : 1;
    }
    else
    {
        return ( thing->anim_speed < 0 ) ? 1 : -1;
    }
}

void update_first_person_position(struct Camera *cam, struct Thing *thing, int64_t eye_height)
{
    if ( thing_is_creature(thing) )
    {
        struct CreatureControl *cctrl = creature_control_get_from_thing(thing);
        if (cctrl->move_speed && thing_touching_floor(thing))
            cctrl->head_bob = 16 * get_walking_bob_direction(thing);
        else
            cctrl->head_bob = 0;

        int64_t pos_x = move_coord_with_angle_x(thing->mappos.x.val,-90,thing->move_angle_xy);
        int64_t pos_y = move_coord_with_angle_y(thing->mappos.y.val,-90,thing->move_angle_xy);

        view_set_camera_position(cam, pos_x, pos_y);

        if ( (thing->movement_flags & TMvF_Flying) != 0 )
        {
            cam->mappos.z.val = thing->mappos.z.val + eye_height;
            cam->rotation_angle_z = cctrl->roll;
        }
        else
        {
            cam->mappos.z.val = cam->mappos.z.val + ((int64_t)thing->mappos.z.val + cctrl->head_bob - cam->mappos.z.val + eye_height) / 2;
            cam->rotation_angle_z = 0;
            if ( eye_height + thing->mappos.z.val <= cam->mappos.z.val )
            {
                if ( eye_height + thing->mappos.z.val + cctrl->head_bob > cam->mappos.z.val )
                    cam->mappos.z.val = eye_height + thing->mappos.z.val + cctrl->head_bob;
            }
            else
            {
                if ( eye_height + thing->mappos.z.val + cctrl->head_bob < cam->mappos.z.val )
                    cam->mappos.z.val = eye_height + thing->mappos.z.val + cctrl->head_bob;
            }
        }

        struct Map* mapblk1 = get_map_block_at(thing->mappos.x.stl.num,     thing->mappos.y.stl.num);
        struct Map* mapblk2 = get_map_block_at(thing->mappos.x.stl.num + 1, thing->mappos.y.stl.num);
        struct Map* mapblk3 = get_map_block_at(thing->mappos.x.stl.num,     thing->mappos.y.stl.num + 1);
        struct Map* mapblk4 = get_map_block_at(thing->mappos.x.stl.num + 1, thing->mappos.y.stl.num + 1);


        const int64_t ceiling = ((get_mapblk_filled_subtiles(mapblk1) * COORD_PER_STL) +
                          (get_mapblk_filled_subtiles(mapblk2) * COORD_PER_STL) +
                          (get_mapblk_filled_subtiles(mapblk3) * COORD_PER_STL) +
                          (get_mapblk_filled_subtiles(mapblk4) * COORD_PER_STL) )/4;

        if ( cam->mappos.z.val > ceiling - 64 )
            cam->mappos.z.val = ceiling - 64;

    }
    else
    {
        cam->mappos.x.val = thing->mappos.x.val;
        cam->mappos.y.val = thing->mappos.y.val;
        if ( thing_is_mature_food(thing) )
        {
            cam->mappos.z.val = thing->mappos.z.val + 240;
            cam->rotation_angle_z = 0;
            thing->move_angle_z = 0;
            if ( thing->food.possession_startup_timer )
            {
                if ( thing->food.possession_startup_timer <= 3 )
                    thing->move_angle_z = -116 * thing->food.possession_startup_timer + DEGREES_360;
                else
                    thing->move_angle_z = 116 * thing->food.possession_startup_timer + 1352;
            }
        }
        else
        {
            cam->rotation_angle_z = 0;
            if ( thing->mappos.z.val + 32 <= cam->mappos.z.val )
            {
                cam->mappos.z.val = cam->mappos.z.val + (thing->mappos.z.val - cam->mappos.z.val + 64) / 2;
                if ( thing->mappos.z.val + 64 > cam->mappos.z.val )
                    cam->mappos.z.val = thing->mappos.z.val + 64;
            }
            else
            {
                cam->mappos.z.val = cam->mappos.z.val + (thing->mappos.z.val - cam->mappos.z.val + 64) / 2;
                if ( thing->mappos.z.val + 64 < cam->mappos.z.val )
                    cam->mappos.z.val = thing->mappos.z.val + 64;
            }
        }
    }
}

void update_first_person_camera(struct Camera *cam, struct Thing *thing)
{
    int64_t eye_height = get_creature_eye_height(thing);
    update_first_person_position(cam, thing, eye_height);
    cam->rotation_angle_x = thing->move_angle_xy;
    cam->rotation_angle_y = thing->move_angle_z;
}

// Positive distance moves rightwards
static void view_move_camera_x(struct Camera *cam, int64_t distance)
{
    MapCoord pos_x;
    MapCoord pos_y;
    MapCoord parchment_pos_x;

    switch (cam->view_mode)
    {
    case PVM_IsoWibbleView:
    case PVM_FrontView:
    case PVM_IsoStraightView:
        pos_x = move_coord_with_angle_x(cam->mappos.x.val, distance, cam->rotation_angle_x + DEGREES_90);
        pos_y = move_coord_with_angle_y(cam->mappos.y.val, distance, cam->rotation_angle_x + DEGREES_90);
        view_set_camera_position(cam, pos_x, pos_y);
        break;

    case PVM_ParchmentView:
        parchment_pos_x = cam->mappos.x.val + distance;
        view_set_camera_position(cam, parchment_pos_x, cam->mappos.y.val);
    }
}

// Positive distance moves downwards
static void view_move_camera_y(struct Camera *cam, int64_t distance)
{
    MapCoord pos_x;
    MapCoord pos_y;
    MapCoord parchment_pos_y;

    switch (cam->view_mode)
    {
    case PVM_IsoWibbleView:
    case PVM_FrontView:
    case PVM_IsoStraightView:
        pos_x = move_coord_with_angle_x(cam->mappos.x.val, distance, cam->rotation_angle_x + DEGREES_180);
        pos_y = move_coord_with_angle_y(cam->mappos.y.val, distance, cam->rotation_angle_x + DEGREES_180);
        view_set_camera_position(cam, pos_x, pos_y);
        break;

    case PVM_ParchmentView:
        parchment_pos_y = cam->mappos.y.val + distance;
        view_set_camera_position(cam, cam->mappos.x.val, parchment_pos_y);
    }
}

void view_process_camera_inertia(struct Camera *cam)
{
    if (cam->inertia_x)
        view_move_camera_x(cam, cam->inertia_x);

    if (cam->inertia_y)
        view_move_camera_y(cam, cam->inertia_y);

    if (cam->inertia_rotation)
    {
        cam->rotation_angle_x = (cam->inertia_rotation + cam->rotation_angle_x) & ANGLE_MASK;
        if (cam->use_rotation_pivot)
        {
            const MapCoordDelta x0 = cam->mappos.x.val - cam->rotation_pivot.x.val;
            const MapCoordDelta y0 = cam->mappos.y.val - cam->rotation_pivot.y.val;
            const int64_t sin = LbSinL(cam->inertia_rotation);
            const int64_t cos = LbCosL(cam->inertia_rotation);
            const MapCoordDelta x1 = (x0 * cos - y0 * sin) >> 16;
            const MapCoordDelta y1 = (x0 * sin + y0 * cos) >> 16;
            const MapCoord new_x = (MapCoord)cam->rotation_pivot.x.val + x1;
            const MapCoord new_y = (MapCoord)cam->rotation_pivot.y.val + y1;
            view_set_camera_position(cam, new_x, new_y);
        }
    }

    if (! cam->in_active_movement_x)
        cam->inertia_x /= 2;

    if (! cam->in_active_movement_y)
        cam->inertia_y /= 2;

    if (! cam->in_active_movement_rotation)
        cam->inertia_rotation /= 2;

    if (cam->inertia_rotation == 0)
        cam->use_rotation_pivot = false;

    cam->in_active_movement_x = false;
    cam->in_active_movement_y = false;
    cam->in_active_movement_rotation = false;
}

void view_set_camera_move_to_position(struct Camera *cam, MapCoord x, MapCoord y, MapCoordDelta *move_x, MapCoordDelta *move_y)
{
    MapCoord positions[] = {cam->mappos.x.val, cam->mappos.y.val};
    MapCoord targets[] = {x, y};
    MapCoordDelta *movement[] = {move_x, move_y};
    for (int64_t i = 0; i < 2; i++) {
        *movement[i] = max(llabs(targets[i] - positions[i]) / 8, 256);
        if (targets[i] < positions[i]) {
            *movement[i] = -*movement[i];
        }
    }
}

TbBool view_move_camera_to_position(struct Camera *cam, MapCoord x, MapCoord y, MapCoordDelta move_x, MapCoordDelta move_y)
{
    MapCoord *positions[] = {&cam->mappos.x.val, &cam->mappos.y.val};
    MapCoord targets[] = {x, y};
    MapCoordDelta movement[] = {move_x, move_y};
    cam->inertia_x = 0;
    cam->inertia_y = 0;
    for (int64_t i = 0; i < 2; i++) {
        if (llabs(*positions[i] - targets[i]) >= llabs(movement[i])) {
            *positions[i] += movement[i];
        } else {
            *positions[i] = targets[i];
        }
    }
    return (*positions[0] == targets[0]) && (*positions[1] == targets[1]);
}

void update_player_camera(struct PlayerInfo *player)
{
    struct Dungeon *dungeon = get_players_dungeon(player);
    struct Camera *cam = get_player_active_camera(player);

    view_process_camera_inertia(cam);
    switch (cam->view_mode)
    {
    case PVM_CreatureView:
        if (player->controlled_thing_idx > 0) {
            struct Thing *ctrltng;
            ctrltng = thing_get(player->controlled_thing_idx);
            update_first_person_camera(cam, ctrltng);
        } else
        if (player->instance_num != PI_HeartZoom) {
            ERRORLOG("Cannot go first person without controlling creature");
        }
        break;
    case PVM_IsoWibbleView:
    case PVM_IsoStraightView:
        // correct according to dissassembly
        player->cameras[CamIV_FrontView].mappos.x.val = cam->mappos.x.val;
        player->cameras[CamIV_FrontView].mappos.y.val = cam->mappos.y.val;
        break;
    case PVM_FrontView:
        // correct according to dissassembly
        player->cameras[CamIV_Isometric].mappos.x.val = cam->mappos.x.val;
        player->cameras[CamIV_Isometric].mappos.y.val = cam->mappos.y.val;
        break;
    }
    if (dungeon->camera_deviate_quake) {
        dungeon->camera_deviate_quake--;
    }
    if (dungeon->camera_deviate_jump > 0) {
        dungeon->camera_deviate_jump -= 32;
    }
}

void update_all_players_cameras(void)
{
  int64_t i;
  struct PlayerInfo *player;
  SYNCDBG(6,"Starting");
  for (i=0; i<PLAYERS_COUNT; i++)
  {
    player = get_player(i);
    if (player_exists(player) && ((player->allocflags & PlaF_CompCtrl) == 0))
    {
          update_player_camera(player);
    }
  }

  // Send catchup packets if local camera has drifted too far from packet-based camera
  send_camera_catchup_packets();
}

void set_player_cameras_position(struct PlayerInfo *player, int64_t pos_x, int64_t pos_y)
{
    player->cameras[CamIV_Parchment].mappos.x.val = pos_x;
    player->cameras[CamIV_FrontView].mappos.x.val = pos_x;
    player->cameras[CamIV_Isometric].mappos.x.val = pos_x;
    player->cameras[CamIV_Parchment].mappos.y.val = pos_y;
    player->cameras[CamIV_FrontView].mappos.y.val = pos_y;
    player->cameras[CamIV_Isometric].mappos.y.val = pos_y;
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
      int64_t status_panel_width = render_overlay->get_status_panel_width();
      window_center_x = (MyScreenWidth-local_state.engine_window_width-status_panel_width) / 2 + status_panel_width;
    }
    else
      window_center_x = (MyScreenWidth-local_state.engine_window_width) / 2;
    window_center_y = (MyScreenHeight-local_state.engine_window_height) / 2;
    setup_engine_window(window_center_x, window_center_y, local_state.engine_window_width, local_state.engine_window_height);
}

TbBool any_player_close_enough_to_see(const struct Coord3d *pos)
{
    struct PlayerInfo *player;
    int64_t i;
    int64_t limit = 24 * COORD_PER_STL;
    for (i=0; i < PLAYERS_COUNT; i++)
    {
        player = get_player(i);
        if ( (player_exists(player)) && ((player->allocflags & PlaF_CompCtrl) == 0))
        {
            struct Camera *camera = get_player_active_camera(player);
            if (camera == NULL)
                continue;
            if (camera->view_mode != PVM_FrontView)
            {
                if (camera->zoom >= CAMERA_ZOOM_MIN)
                {
                    limit = SHRT_MAX - (2 * camera->zoom);
                }
            }
            else
            {
                if (camera->zoom >= FRONTVIEW_CAMERA_ZOOM_MIN)
                {
                    limit = SHRT_MAX - (camera->zoom / 3);
                }
            }
            if (get_chessboard_distance(&camera->mappos, pos) <= limit)
            {
                return true;
            }
        }
    }
    return false;
}

uint64_t lightning_is_close_to_player(struct PlayerInfo *player, struct Coord3d *pos)
{
    struct Camera *camera = get_player_active_camera(player);
    if (camera == NULL)
        return false;
    return get_chessboard_distance(&camera->mappos, pos) < subtile_coord(45,0);
}
/******************************************************************************/
