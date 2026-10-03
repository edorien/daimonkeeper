/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file player_camera.c
 *     The synced player cameras (PlayerInfo.cameras[]).
 * @par Purpose:
 *     Zoom, velocity, rotation and tilt, first-person tracking, the per-turn
 *     update, and the packet camera handlers. Moved from kfx_render's
 *     engine_camera.c and kfx_net's packets.c in refactor pass 2's S07
 *     (docs/refactor-pass2/stage-07-camera-to-sim.md): the synced camera is
 *     packet-driven, advanced every turn, saved and resynced, so it is
 *     simulation state. The local, interpolated camera stays in kfx_render's
 *     local_camera.c, which reads kfx_sim_view_signals to know when to
 *     re-seed or retarget itself.
 * @par Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "player_camera.h"

#include <limits.h>
#include "globals.h"
#include "bflib_basics.h"
#include "bflib_math.h"
#include "bflib_planar.h"
#include "bflib_video.h"
#include "config_settings.h"
#include "creature_control.h"
#include "creature_states.h"
#include "config_creature.h"
#include "dungeon_data.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "map_blocks.h"
#include "map_columns.h"
#include "map_data.h"
#include "packet_data.h"
#include "player_data.h"
#include "player_instances.h"
#include "thing_data.h"
#include "thing_objects.h"
#include "thing_physics.h"
#include "ports/editor_port.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct KfxSimViewSignals kfx_sim_view_signals;
/******************************************************************************/

void signal_local_camera_sync(const struct PlayerInfo *player)
{
    kfx_sim_view_signals.camera_sync_seq[player->id_number]++;
}

void signal_local_camera_retarget(const struct PlayerInfo *player)
{
    kfx_sim_view_signals.camera_retarget_seq[player->id_number]++;
}

void view_set_camera_position(struct Camera *cam, MapCoord x, MapCoord y)
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

void view_set_camera_y_velocity(struct Camera *cam, int64_t delta, int64_t ilimit)
{
    int64_t abslimit = llabs(ilimit);
    cam->velocity_y += delta;
    if (cam->velocity_y < -abslimit) {
        cam->velocity_y = -abslimit;
    } else
    if (cam->velocity_y > abslimit) {
        cam->velocity_y = abslimit;
    }
    cam->in_active_movement_y = true;
}

void view_set_camera_x_velocity(struct Camera *cam, int64_t delta, int64_t ilimit)
{
    int64_t abslimit = llabs(ilimit);
    cam->velocity_x += delta;
    if (cam->velocity_x < -abslimit) {
        cam->velocity_x = -abslimit;
    } else
    if (cam->velocity_x > abslimit) {
        cam->velocity_x = abslimit;
    }
    cam->in_active_movement_x = true;
}

void view_set_camera_rotation_velocity(struct Camera *cam, int64_t delta, int64_t ilimit)
{
    const int64_t limit_val = llabs(ilimit);
    const int64_t new_val = delta + cam->velocity_rotation;
    cam->velocity_rotation = clamp(new_val, -limit_val, +limit_val);
    cam->in_active_movement_rotation = true;
}

void view_set_camera_rotation_velocity_around(struct Camera *cam, int64_t delta, int64_t ilimit, MapCoord x, MapCoord y)
{
    view_set_camera_rotation_velocity(cam, delta, ilimit);
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

    // The local (interpolated) cameras re-seed themselves from these on
    // their next read; see struct KfxSimViewSignals.
    kfx_sim_view_signals.camera_init_seq[player->id_number]++;
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

/**
 * True when a first-person camera at (x, y, z) would sit inside, or closer than
 * FP_CAMERA_WALL_MARGIN to, a solid column that reaches up to z. Probes the
 * point and four axis-aligned neighbours so a wall alongside counts too.
 */
#define FP_CAMERA_WALL_MARGIN 56

static TbBool first_person_camera_near_wall(int64_t x, int64_t y, int64_t z)
{
    static const int64_t probe[5][2] = { {0, 0}, {FP_CAMERA_WALL_MARGIN, 0}, {-FP_CAMERA_WALL_MARGIN, 0}, {0, FP_CAMERA_WALL_MARGIN}, {0, -FP_CAMERA_WALL_MARGIN} };
    for (int64_t i = 0; i < 5; i++)
    {
        const int64_t px = x + probe[i][0];
        const int64_t py = y + probe[i][1];
        if (px < 0 || py < 0)
            return true;
        const struct Map *mapblk = get_map_block_at(coord_subtile(px), coord_subtile(py));
        if (mapblk == NULL)
            return true;
        if (get_column_floor_filled_subtiles(get_map_column(mapblk)) * COORD_PER_STL > z)
            return true;
    }
    return false;
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

        // The eye sits 90 units behind the creature: with its back to a wall that puts the
        // camera inside the wall (the near plane then clips the geometry away). Pull it
        // forward along that offset to the furthest point that clears the wall.
        if (first_person_camera_near_wall(pos_x, pos_y, cam->mappos.z.val))
        {
            int64_t back = 90;
            while (back > 0)
            {
                back -= 10;
                pos_x = move_coord_with_angle_x(thing->mappos.x.val, -back, thing->move_angle_xy);
                pos_y = move_coord_with_angle_y(thing->mappos.y.val, -back, thing->move_angle_xy);
                if (!first_person_camera_near_wall(pos_x, pos_y, cam->mappos.z.val))
                    break;
            }
            view_set_camera_position(cam, pos_x, pos_y);
        }

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

int64_t camera_move_rate(const struct Camera* cam, const struct PlayerInfo* player, TbBool speedup)
{
    int64_t inter_val;
    int64_t scroll_speed = cam->zoom;
    if (scroll_speed <= 0)
        scroll_speed = 1;
    switch (cam->view_mode)
    {
    case PVM_IsoWibbleView:
    case PVM_IsoStraightView:
        if (player->roomspace_drag_paint_mode == 1)
        {
            if (scroll_speed < 4100)
            {
                scroll_speed = 4100;
            }
        }
        inter_val = 2560000 / scroll_speed;
        break;
    case PVM_FrontView:
        if (player->roomspace_drag_paint_mode == 1)
        {
            if (scroll_speed < 16384)
            {
                scroll_speed = 16384;
            }
        }
        inter_val = 12800000 / scroll_speed;
        break;
    default:
        inter_val = 256;
        break;
    }
    if (speedup)
      inter_val *= 3;
    return inter_val;
}

void view_process_camera_velocity(struct Camera *cam)
{
    if (cam->velocity_x)
        view_move_camera_x(cam, cam->velocity_x);

    if (cam->velocity_y)
        view_move_camera_y(cam, cam->velocity_y);

    if (cam->velocity_rotation)
    {
        cam->rotation_angle_x = (cam->velocity_rotation + cam->rotation_angle_x) & ANGLE_MASK;
        if (cam->use_rotation_pivot)
        {
            const MapCoordDelta x0 = cam->mappos.x.val - cam->rotation_pivot.x.val;
            const MapCoordDelta y0 = cam->mappos.y.val - cam->rotation_pivot.y.val;
            const int64_t sin = LbSinL(cam->velocity_rotation);
            const int64_t cos = LbCosL(cam->velocity_rotation);
            const MapCoordDelta x1 = (x0 * cos - y0 * sin) >> 16;
            const MapCoordDelta y1 = (x0 * sin + y0 * cos) >> 16;
            const MapCoord new_x = (MapCoord)cam->rotation_pivot.x.val + x1;
            const MapCoord new_y = (MapCoord)cam->rotation_pivot.y.val + y1;
            view_set_camera_position(cam, new_x, new_y);
        }
    }

    if (! cam->in_active_movement_x)
        cam->velocity_x /= 2;

    if (! cam->in_active_movement_y)
        cam->velocity_y /= 2;

    if (! cam->in_active_movement_rotation)
        cam->velocity_rotation /= 2;

    if (cam->velocity_rotation == 0)
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
    cam->velocity_x = 0;
    cam->velocity_y = 0;
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

    view_process_camera_velocity(cam);
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
    if (kfx_sim_view_signals.camera_deviate_quake[dungeon->owner]) {
        kfx_sim_view_signals.camera_deviate_quake[dungeon->owner]--;
    }
    if (kfx_sim_view_signals.camera_deviate_jump[dungeon->owner] > 0) {
        kfx_sim_view_signals.camera_deviate_jump[dungeon->owner] -= 32;
    }
}

// Camera-gated sim code rule (refactor pass 2, S07): the synced cameras
// advance only for players whose camera is updated here, and since the
// spectator hand-off the local computer-controlled seat's camera advances on
// one machine only. So any_player_close_enough_to_see() and
// lightning_is_close_to_player() may gate only unsynced effects: their
// consumers must create unsynced things, use UNSYNC_RANDOM, and touch no state
// that is checksummed or read by synced code. process_effect_generator()
// checks this at Debug log levels.
void update_all_players_cameras(void)
{
  int64_t i;
  struct PlayerInfo *player;
  SYNCDBG(6,"Starting");
  for (i=0; i<PLAYERS_COUNT; i++)
  {
    player = get_player(i);
    // A CompCtrl player's camera is normally skipped -- nothing renders it -- but the local player can be
    // CompCtrl too (player_enter_spectator_mode: a human watching their own AI-handed-off seat), and their
    // camera IS on screen, so it must keep updating (first-person floating-spirit tracking in particular).
    if (player_exists(player) && (((player->allocflags & PlaF_CompCtrl) == 0) || is_my_player_number(i)))
    {
          update_player_camera(player);
    }
  }
}

// Gates unsynced effects only; see the rule above update_all_players_cameras().
TbBool any_player_close_enough_to_see(const struct Coord3d *pos)
{
    struct PlayerInfo *player;
    int64_t i;
    int64_t limit = 24 * COORD_PER_STL;
    for (i=0; i < PLAYERS_COUNT; i++)
    {
        player = get_player(i);
        // Same local-spectator exception as update_all_players_cameras() above.
        if ( (player_exists(player)) && (((player->allocflags & PlaF_CompCtrl) == 0) || is_my_player_number(i)))
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

// Gates unsynced effects only; see the rule above update_all_players_cameras().
uint64_t lightning_is_close_to_player(struct PlayerInfo *player, struct Coord3d *pos)
{
    struct Camera *camera = get_player_active_camera(player);
    if (camera == NULL)
        return false;
    return get_chessboard_distance(&camera->mappos, pos) < subtile_coord(45,0);
}

/******************************************************************************/
// Packet camera handlers (from kfx_net's packets.c).

static void process_camera_position(struct Camera* cam, const struct Packet* pckt)
{
    if ((cam->view_mode != PVM_IsoWibbleView) && (cam->view_mode != PVM_IsoStraightView) && (cam->view_mode != PVM_FrontView))
        return;
    MapCoord x;
    MapCoord y;
    if (!packet_get_camera_position(pckt, &x, &y))
        return;
    view_set_camera_position(cam, x, y);
    cam->velocity_x = 0;
    cam->velocity_y = 0;
}

void process_camera_controls(struct Camera* cam, const struct Packet* pckt, struct PlayerInfo* player)
{
    if (cam == NULL) {
        return;
    }
    process_camera_position(cam, pckt);
    if (packet_action_has_camera_angle(pckt)
     && ((cam->view_mode == PVM_IsoWibbleView) || (cam->view_mode == PVM_IsoStraightView)))
        cam->rotation_angle_x = pckt->actn_par3 & ANGLE_MASK;
    process_camera_view_controls(cam, pckt, player);
}

void process_camera_view_controls(struct Camera* cam, const struct Packet* pckt, struct PlayerInfo* player)
{
    const TbBool use_rotate_pos = flag_is_set(pckt->control_flags, PCtr_ViewRotatePos | PCtr_MapCoordsValid);
    const MapCoord rot_x = use_rotate_pos ? pckt->pos_x : -1;
    const MapCoord rot_y = use_rotate_pos ? pckt->pos_y : -1;
    if ((pckt->control_flags & PCtr_ViewRotateCCW) != 0)
    {
        switch (cam->view_mode)
        {
        case PVM_IsoWibbleView:
        case PVM_IsoStraightView:
             view_set_camera_rotation_velocity_around(cam, 16, 64, rot_x, rot_y);
            break;
        case PVM_FrontView:
            cam->rotation_angle_x = (cam->rotation_angle_x + DEGREES_90) & ANGLE_MASK;
            break;
        }
    }
    if ((pckt->control_flags & PCtr_ViewRotateCW) != 0)
    {
        switch (cam->view_mode)
        {
        case PVM_IsoWibbleView:
        case PVM_IsoStraightView:
            view_set_camera_rotation_velocity_around(cam, -16, -64, rot_x, rot_y);
            break;
        case PVM_FrontView:
            cam->rotation_angle_x = (cam->rotation_angle_x - DEGREES_90) & ANGLE_MASK;
            break;
        }
    }
    if ((pckt->control_flags & PCtr_ViewTiltUp) != 0)
    {
        switch (cam->view_mode)
        {
        case PVM_IsoWibbleView:
        case PVM_IsoStraightView:
            view_set_camera_tilt(cam, 1);
            break;
        }
    }
    if ((pckt->control_flags & PCtr_ViewTiltDown) != 0)
    {
        switch (cam->view_mode)
        {
        case PVM_IsoWibbleView:
        case PVM_IsoStraightView:
            view_set_camera_tilt(cam, 2);
            break;
        }
    }
    if ((pckt->control_flags & PCtr_ViewTiltReset) != 0)
    {
        switch (cam->view_mode)
        {
        case PVM_IsoWibbleView:
        case PVM_IsoStraightView:
            view_set_camera_tilt(cam, 0);
            break;
        }
    }
    // docs/refactor/editor/04-views-camera-overlays.md -- editor camera
    // profile. An editor session bypasses the gameplay zoom_distance_setting
    // comfort floor (which can restrict a player's own zoom-out well above
    // the true hard floor) and uses EDITOR_CAMERA_ZOOM_MIN instead -- looser
    // than CAMERA_ZOOM_MIN itself, since gameplay's own hard floor was tuned
    // for play, not for surveying a whole map while editing. zoom_max is
    // left at the gameplay value; no evidence yet that the zoom-in limit
    // needs loosening too.
    const int64_t zoom_min = editorport_is_active() ? EDITOR_CAMERA_ZOOM_MIN
        : max(CAMERA_ZOOM_MIN, kfx_config_state.zoom_distance_setting);
    const int64_t zoom_max = CAMERA_ZOOM_MAX;
    const TbBool use_zoom_pos = flag_is_set(pckt->control_flags, PCtr_ViewZoomPos | PCtr_MapCoordsValid);
    const MapCoord zoom_x = use_zoom_pos ? pckt->pos_x : -1;
    const MapCoord zoom_y = use_zoom_pos ? pckt->pos_y : -1;
    if (pckt->control_flags & PCtr_ViewZoomIn)
    {
        switch (cam->view_mode)
        {
        case PVM_IsoWibbleView:
        case PVM_IsoStraightView:
            view_zoom_camera_in_to(cam, zoom_max, zoom_min, zoom_x, zoom_y);
            update_camera_zoom_bounds(cam, zoom_max, zoom_min);
            break;
        default:
            view_zoom_camera_in_to(cam, zoom_max, zoom_min, zoom_x, zoom_y);
            break;
        }
    }
    if (pckt->control_flags & PCtr_ViewZoomOut)
    {
        switch (cam->view_mode)
        {
        case PVM_IsoWibbleView:
        case PVM_IsoStraightView:
            view_zoom_camera_out_from(cam, zoom_max, zoom_min, zoom_x, zoom_y);
            update_camera_zoom_bounds(cam, zoom_max, zoom_min);
            break;
        default:
            view_zoom_camera_out_from(cam, zoom_max, zoom_min, zoom_x, zoom_y);
            break;
        }
    }
}

static void set_all_cameras_position(struct Camera *cams, int64_t pos_x, int64_t pos_y)
{
    cams[CamIV_Parchment].mappos.x.val = pos_x;
    cams[CamIV_FrontView].mappos.x.val = pos_x;
    cams[CamIV_Isometric].mappos.x.val = pos_x;
    cams[CamIV_Parchment].mappos.y.val = pos_y;
    cams[CamIV_FrontView].mappos.y.val = pos_y;
    cams[CamIV_Isometric].mappos.y.val = pos_y;
}

static void set_all_cameras_rotation(struct Camera *cams, int64_t angle)
{
    cams[CamIV_Parchment].rotation_angle_x = angle;
    cams[CamIV_FrontView].rotation_angle_x = angle;
    cams[CamIV_Isometric].rotation_angle_x = angle;
    cams[CamIV_Isometric].velocity_rotation = 0;
}

void process_camera_action(struct Camera *cams, const struct Packet *pckt)
{
    switch (pckt->action)
    {
    case PckA_BookmarkLoad:
        set_all_cameras_position(cams, pckt->actn_par1, pckt->actn_par2);
        break;

    case PckA_SetMapRotation:
        set_all_cameras_rotation(cams, pckt->actn_par1);
        break;

    case PckA_ZoomFromMap:
        set_all_cameras_position(cams, subtile_coord_center(pckt->actn_par1), subtile_coord_center(pckt->actn_par2));
        set_all_cameras_rotation(cams, 0);
        for (int64_t i = 0; i < CamIV_EndList; i++) {
            cams[i].velocity_x = 0;
            cams[i].velocity_y = 0;
            cams[i].velocity_rotation = 0;
        }
        break;
    }
}

void process_first_person_look(struct Thing *thing, const struct Packet *pckt, int64_t current_horizontal, int64_t current_vertical, int64_t *out_horizontal, int64_t *out_vertical, int64_t *out_roll)
{
    struct CreatureModelConfig* crconf = creature_stats_get_from_thing(thing);
    int64_t maxTurnSpeed = crconf->max_turning_speed;
    if (maxTurnSpeed < 1) {
        maxTurnSpeed = 1;
    }
    int64_t horizontalTurnSpeed = pckt->pos_x;
    if (horizontalTurnSpeed < -maxTurnSpeed) {
        horizontalTurnSpeed = -maxTurnSpeed;
    } else if (horizontalTurnSpeed > maxTurnSpeed) {
        horizontalTurnSpeed = maxTurnSpeed;
    }
    int64_t verticalTurnSpeed = pckt->pos_y;
    if (verticalTurnSpeed < -maxTurnSpeed) {
        verticalTurnSpeed = -maxTurnSpeed;
    } else if (verticalTurnSpeed > maxTurnSpeed) {
        verticalTurnSpeed = maxTurnSpeed;
    }
    int64_t verticalPos = (current_vertical + verticalTurnSpeed) & ANGLE_MASK;
    int64_t lowerLimit = ANGLE_MASK - 227;
    int64_t upperLimit = 227;
    if (verticalPos > upperLimit && verticalPos < lowerLimit) {
        if (llabs(verticalPos - upperLimit) < llabs(verticalPos - lowerLimit)) {
            verticalPos = upperLimit;
        } else {
            verticalPos = lowerLimit;
        }
    }
    *out_vertical = verticalPos;
    *out_horizontal = (current_horizontal + horizontalTurnSpeed) & ANGLE_MASK;
    *out_roll = 170 * horizontalTurnSpeed / maxTurnSpeed;
}

TbBool can_process_creature_input(struct Thing *thing)
{
    if (thing->class_id != TCls_Creature) {
        return false;
    }
    if (creature_is_dying(thing)) {
        return false;
    }
    struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
    if ((cctrl->stateblock_flags != 0) || (thing->active_state == CrSt_CreatureUnconscious)) {
        return false;
    }
    return true;
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
