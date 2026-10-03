/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file player_colours.h
 *     The colours the map, the panels and the power hand draw each player in.
 */
/******************************************************************************/
#ifndef DK_PLAYER_COLOURS_H
#define DK_PLAYER_COLOURS_H

#include "bflib_basics.h"
#include "globals.h"
#include "bflib_video.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/** One entry per player colour index (get_player_color_idx()). */
extern const TbPixel player_path_colours[];
extern const TbPixel player_room_colours[];
extern const TbPixel player_flash_colours[];
extern const TbPixel player_highlight_colours[];

TbPixel get_player_path_colour(int64_t owner);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
