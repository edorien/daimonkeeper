/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file front_input_roomspace.h
 *     Header file for front_input_roomspace.c.
 * @par Purpose:
 *     Local roomspace-cursor input (moved from kfx_sim's roomspace.h,
 *     docs/refactor-pass2/stage-06-presentation-out-of-sim.md).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_FRONT_INPUT_ROOMSPACE_H
#define DK_FRONT_INPUT_ROOMSPACE_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
void process_build_roomspace_inputs(PlayerNumber plyr_idx);
void process_sell_roomspace_inputs(PlayerNumber plyr_idx);
void process_highlight_roomspace_inputs(PlayerNumber plyr_idx);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
