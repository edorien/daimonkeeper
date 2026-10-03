/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_redraw.h
 *     Header file for engine_redraw.c.
 * @par Purpose:
 *     Functions to redraw the engine screen.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     06 Nov 2010 - 03 Jul 2011
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/

#ifndef DK_ENGNRDRAW_H
#define DK_ENGNRDRAW_H

#include "bflib_basics.h"
#include "globals.h"
#include "bflib_video.h"
#include "bflib_netsp.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

struct PlayerInfo;
struct Camera;
struct Coord3d;

#pragma pack()
/******************************************************************************/
/******************************************************************************/
void setup_engine_window(int64_t x1, int64_t y1, int64_t x2, int64_t y2);
void store_engine_window(TbGraphicsWindow *ewnd,int64_t divider);

void set_engine_view(struct PlayerInfo *player, int64_t val);

void map_fade(TbPixel *outbuf, TbPixel *srcbuf1, TbPixel *srcbuf2, int64_t a6, int64_t const xmax, int64_t const ymax, int64_t a9);
void smooth_screen_area(TbPixel *a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6);

TbBool players_cursor_is_at_top_of_view(void);
TbBool engine_point_to_map(struct Camera *camera, int64_t screen_x, int64_t screen_y, int64_t *map_x, int64_t *map_y);
TbBool screen_to_map(struct Camera *camera, int64_t screen_x, int64_t screen_y, struct Coord3d *mappos);
void update_local_mouse_light(void);
void update_mouse_light(NetUserId user);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
