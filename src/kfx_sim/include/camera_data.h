/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file camera_data.h
 *     Camera data type shared between kfx_sim and kfx_render.
 * @par Purpose:
 *     struct PlayerInfo (kfx_sim) embeds struct Camera cameras[4] by
 *     value, so the type must live at or below kfx_sim's own layer.
 *     Moved out of engine_camera.h (stage 7 prep, docs/refactor/
 *     stage-07-kfx-render.md) -- kfx_render includes this header for the
 *     same definitions, which is a normal permitted downward reference
 *     (kfx_render depends on kfx_sim).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/

#ifndef DK_CAMERA_DATA_H
#define DK_CAMERA_DATA_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

enum CameraIndexValues {
    CamIV_Isometric = 0,
    CamIV_FirstPerson,
    CamIV_Parchment,
    CamIV_FrontView,
    CamIV_EndList
};

struct Camera {
    struct Coord3d mappos;
    unsigned char view_mode;
    int64_t rotation_angle_x;
    int64_t rotation_angle_y;
    int64_t rotation_angle_z;
    int64_t horizontal_fov; // Horizontal Field of View in degrees
    int64_t zoom;
    int64_t inertia_rotation;
    TbBool in_active_movement_rotation;
    int64_t inertia_x;
    TbBool in_active_movement_x;
    int64_t inertia_y;
    TbBool in_active_movement_y;
    TbBool use_rotation_pivot;
    struct Coord2d rotation_pivot;
};

#pragma pack()
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
