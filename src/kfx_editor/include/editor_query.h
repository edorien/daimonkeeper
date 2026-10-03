/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_query.h
 *     What the Query tool shows about a thing or room, without the UI.
 */
#ifndef DK_EDITOR_QUERY_H
#define DK_EDITOR_QUERY_H

#include "globals.h"

struct Coord3d;
struct Thing;
struct Room;

struct EditorQueryResult {
    enum Kind { QR_None, QR_Thing, QR_Room } kind = QR_None;
    char title[32] = "";
    char name[64] = "";
    char owner[24] = "";
    char health[32] = "";
    char extra1[48] = "";
    char extra2[48] = "";
};


/** Looks at what is at `pos`: a creature (its index goes to *creature_idx and the result stays empty, the
 *  creature has its own panel), else the nearest thing, else the room. */
EditorQueryResult editor_query_at(const struct Coord3d *pos, ThingIndex *creature_idx);

#endif
