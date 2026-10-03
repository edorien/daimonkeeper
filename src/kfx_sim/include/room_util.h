/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file room_util.h
 *     Header file for room_util.c.
 * @par Purpose:
 *     Generic utility and maintain functions for rooms.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     07 Apr 2011 - 05 Jun 2011
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_ROOM_UTIL_H
#define DK_ROOM_UTIL_H

#include "globals.h"
#include "bflib_basics.h"
#include "room_data.h"
#include "thing_data.h"
#include "dungeon_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
#pragma pack(1)

#pragma pack()
/******************************************************************************/
void recompute_rooms_count_in_dungeons(void);
void process_rooms(void);

TbBool delete_room_slab(MapSlabCoord slb_x, MapSlabCoord slb_y, TbBool is_destroyed);
/** Paints a slab the way the level editor needs: any room on it is deleted, and when the new kind is
 *  a room the slab first goes back to plain earth, so the room is built on clean ground. Painting a
 *  room over an existing room otherwise left the middle of the area purple. */
void place_slab_type_replacing_room(SlabKind slbkind, MapSlabCoord slb_x, MapSlabCoord slb_y, PlayerNumber owner);
TbBool replace_slab_from_script(MapSlabCoord slb_x, MapSlabCoord slb_y, unsigned char slabkind);
void change_slab_owner_from_script(MapSlabCoord slb_x, MapSlabCoord slb_y, PlayerNumber plyr_idx);
TbBool check_and_asimilate_thing_by_room(struct Thing *thing);
EventIndex update_cannot_find_room_of_role_wth_spare_capacity_event(PlayerNumber plyr_idx, struct Thing *creatng, RoomRole rrole);
void query_room(struct Room *room);

/******************************************************************************/
/*
 * The storage rooms (garden, graveyard, workshop, library) recount their
 * contents and put them back after the room changes shape: items on a
 * subtile that can't hold them any more are taken out and remembered, then
 * re-created on subtiles with space. Refactor pass 3, S03: one
 * implementation, driven by a description of what the room stores.
 */
struct RoomStorageKind {
    const char *what; /**< what the room stores, for the logs */
    /** Is this thing one of the room's stored items. */
    TbBool (*is_stored)(const struct Room *room, const struct Thing *thing);
    /** Remember the item in rrepos (to re-create it) and remove it from the map. */
    void (*take_out)(struct Room *room, struct Thing *thing, struct RoomReposition *rrepos);
    /** Re-create one remembered item on a subtile; INVALID_THING if it couldn't. */
    struct Thing *(*put_back)(struct Room *room, ThingModel model, CrtrExpLevel exp_level, MapSubtlCoord stl_x, MapSubtlCoord stl_y);
    /** NULL for the common check; the library has its own. */
    int64_t (*check_subtile)(struct Room *room, MapSubtlCoord stl_x, MapSubtlCoord stl_y);
    /** What to do with items that didn't fit anywhere; NULL logs and drops them. */
    void (*overflow)(struct Room *room, struct RoomReposition *rrepos);
    unsigned char lift_out_of_floor;       /**< an item sunk into the floor is lifted and counted (workshop) */
    unsigned char tracks_storage_capacity; /**< capacity_used_for_storage follows the count (all but the graveyard) */
    unsigned char stops_when_settled;      /**< skip the second sweep when nothing waits or the room is full (graveyard) */
};

void room_storage_recount(const struct RoomStorageKind *kind, struct Room *room);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
