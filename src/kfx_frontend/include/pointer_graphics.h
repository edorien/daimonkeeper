/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file pointer_graphics.h
 *     Header file for pointer_graphics.c.
 * @par Purpose:
 *     Choosing the mouse pointer, moved from kfx_render's engine_redraw.c
 *     in refactor pass 2 (S13).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_POINTER_GRAPHICS_H
#define DK_POINTER_GRAPHICS_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct PlayerInfo;

/** The spell cost (or, when negative, the charge level) to show under the
    pointer this frame; draw_spell_cursor() sets it, redraw_display() draws
    and clears it. */
extern int64_t draw_spell_cost;

void process_pointer_graphic(void);
void process_dungeon_top_pointer_graphic(struct PlayerInfo *player);
TbBool draw_spell_cursor(ThingIndex tng_idx, MapSubtlCoord stl_x, MapSubtlCoord stl_y);
int64_t get_place_room_pointer_graphics(RoomKind rkind);
int64_t get_place_trap_pointer_graphics(ThingModel trmodel);
int64_t get_place_door_pointer_graphics(ThingModel drmodel);
int64_t get_place_terrain_pointer_graphics(SlabKind skind);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
