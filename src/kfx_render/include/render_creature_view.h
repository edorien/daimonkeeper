/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file render_creature_view.h
 *     Header file for render_creature_view.c.
 * @par Purpose:
 *     Drawing the first-person possession view (moved from kfx_sim's
 *     thing_creature.h, docs/refactor-pass2/stage-06-presentation-out-of-sim.md).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_RENDER_CREATURE_VIEW_H
#define DK_RENDER_CREATURE_VIEW_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct Thing;

void draw_creature_view(struct Thing *thing);
void draw_swipe_graphic(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
