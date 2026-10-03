/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_camera.h
 *     Header file for engine_camera.c.
 * @par Purpose:
 *     Camera move, maintain and support functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     20 Mar 2009 - 30 Mar 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/

#ifndef DK_ENGNCAM_H
#define DK_ENGNCAM_H

#include "bflib_basics.h"
#include "globals.h"
#include "camera_data.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

struct EngineCoord;
struct M33;
struct EngineCol;
struct PlayerInfo;
struct Packet;
struct Thing;

// Camera constants; zoom max is zoomed in (everything large), zoom min is zoomed out (everything small)
// CAMERA_ZOOM_MAX/MIN, FRONTVIEW_CAMERA_ZOOM_MAX/MIN, and
// zoom_distance_setting/frontview_zoom_distance_setting moved to
// kfx_config's kfx_config_state.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- kfx_config is the lowest-ranked
// of their real consumers (config_keeperfx.c writes them; kfx_net's
// packets.c/net_game.c also read them).
#include "kfx_config_state.h"
// docs/refactor/editor/04-views-camera-overlays.md -- bumped 512 -> 2048
// (originally 64, already bumped once "for view distance") after the
// editor's own looser zoom-out (EDITOR_CAMERA_ZOOM_MIN, kfx_config_state.h)
// hit this array's real role: it isn't just a scratch buffer size, it's the
// hard ceiling (MAX_I_CAN_SEE_OVERHEAD, engine_render.c, = (MINMAX_LENGTH/
// 2)-2) on how many subtile-scale "cells" compute_cells_away() can ever
// report before find_gamut()'s per-row horizon scan just stops generating
// terrain for the rest of the screen -- found live as a clean horizontal
// dropout to black at extreme editor zoom-out on a large, open map (a
// smaller/more enclosed map may never approach the ceiling at all, which is
// exactly why gameplay's own CAMERA_ZOOM_MIN never surfaced this: the
// budget needed to stay under the clamp depends on the map's own layout,
// not just the zoom value, so no fixed EDITOR_CAMERA_ZOOM_MIN can dodge
// this for every map -- raising the ceiling itself is the real fix).
// Cost, measured directly rather than assumed: minmaxs[]/ecs1[]/ecs2[] are
// the only arrays sized off this constant (struct EngineCol is 18 *
// sizeof(EngineCoord) = 504 bytes; ecs1/ecs2 go from ~251KB each at 512 to
// ~1MB each at 2048, minmaxs[] itself from 4KB to 16KB) -- a few MB of
// static memory, all global/static (not stack, no overflow risk), and
// completely free at every zoom level compute_cells_away()'s own clamp
// (`if (ncells_a > MAX_I_CAN_SEE_OVERHEAD) ncells_a = ...`) already keeps
// well under today, i.e. every existing gameplay zoom level: the per-frame
// horizon-scan cost is bounded by however far ncells_a actually reaches,
// not by this array's max capacity, so normal (non-editor) play is
// unaffected either way.
#define MINMAX_LENGTH 2048
#define MINMAX_ALMOST_HALF ((MINMAX_LENGTH/2)-1)

struct MinMax { // sizeof = 8
    int64_t min;
    int64_t max;
};

/******************************************************************************/

extern struct EngineCoord object_origin;

#pragma pack()
/******************************************************************************/
extern int64_t camera_zoom;
/******************************************************************************/
// angles_to_vector()/get_angle_xy_to()/get_angle_yz_to()/get_2d_distance()/
// get_2d_distance_squared()/get_angle_xy_to_vec()/get_angle_yz_to_vec()
// moved to bflib_math.h (kfx_platform, stage 7 prep).
// project_point_to_wall_on_angle() moved to map_blocks.h (kfx_sim,
// stage 7 prep).

uint64_t scale_camera_zoom_to_screen(uint64_t zoom_lvl);



void change_engine_window_relative_size(int64_t w_delta, int64_t h_delta);
void centre_engine_window(void);


/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
