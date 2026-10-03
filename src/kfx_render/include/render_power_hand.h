/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file render_power_hand.h
 *     Header file for render_power_hand.c.
 * @par Purpose:
 *     Drawing the local player's power hand (moved from kfx_sim's
 *     power_hand.h, docs/refactor-pass2/stage-06-presentation-out-of-sim.md).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_RENDER_POWER_HAND_H
#define DK_RENDER_POWER_HAND_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
void draw_power_hand(void);
void draw_mini_things_in_hand(int64_t x, int64_t y);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
