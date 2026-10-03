/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frame_compose.h
 *     Header file for frame_compose.c.
 * @par Purpose:
 *     Composing the in-game frame, moved from kfx_render's engine_redraw.h
 *     in refactor pass 2 (S13).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_FRAME_COMPOSE_H
#define DK_FRAME_COMPOSE_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
TbBool keeper_screen_redraw(void);
void redraw_display(void);
void redraw_isometric_view(void);
void redraw_frontview(void);
void redraw_creature_view(void);
void draw_overlay_compass(int64_t base_x, int64_t base_y);
int64_t map_fade_in(int64_t palette_fade_step);
int64_t map_fade_out(int64_t palette_fade_step);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
