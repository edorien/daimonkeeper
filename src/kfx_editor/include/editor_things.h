/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_things.h
 *     Thing operations of the editor that have no UI: removing the thing at a
 *     map position (right-click delete), with the same per-class teardown the
 *     Erase tool's server side uses.
 */
#ifndef DK_EDITOR_THINGS_H
#define DK_EDITOR_THINGS_H

#include "globals.h"

struct Coord3d;

/** Removes the creature, else the nearest thing, at `pos` (one undo step). Returns false if there was none. */
bool editor_delete_thing_at(const struct Coord3d *pos);

#endif
