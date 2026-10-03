/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file roomspace_prediction.h
 *     Client-side dig prediction overlay for roomspace highlighting.
 */
/******************************************************************************/
#ifndef DK_ROOMSPACE_PREDICTION_H
#define DK_ROOMSPACE_PREDICTION_H

#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct RoomSpace;

/* input_lag_turns: kfx_net_state.input_lag_turns, from the caller. */
void update_local_dig_tag_prediction(int64_t input_lag_turns);
unsigned char get_local_dig_prediction_render_flags(MapSubtlCoord stl_x, MapSubtlCoord stl_y, unsigned char base_map_flags);
void update_local_dig_prediction_cursor_preview(int64_t input_lag_turns);
struct RoomSpace *get_local_dig_prediction_render_roomspace(struct RoomSpace *roomspace);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
