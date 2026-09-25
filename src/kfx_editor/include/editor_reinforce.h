/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_reinforce.h
 *     Reinforce a player's dungeon perimeter (the original editor's `r`).
 *     fx-plans/00-audit-and-index.md item A6.
 */
#ifndef DK_EDITOR_REINFORCE_H
#define DK_EDITOR_REINFORCE_H

#include "bflib_basics.h"
#include "globals.h"

/** Turns every earth slab that touches `owner`'s claimed floor, rooms or doors
 *  (side to side) into that player's reinforced wall, the same slab an imp
 *  would leave. Journaled as one undoable step. Returns the number of slabs
 *  changed. */
int64_t editor_reinforce_perimeter(PlayerNumber owner);

#endif
