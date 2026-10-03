/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file player_instances.h
 *     Header file for player_instances.c.
 * @par Purpose:
 *     Player instances support and switching code.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     10 Mar 2009 - 20 Mar 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_PLYR_INSTNC_H
#define DK_PLYR_INSTNC_H

#include "bflib_basics.h"
#include "globals.h"
#include "thing_list.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// enum PlayerNames moved to globals.h (stage 13) -- see
// docs/refactor/stage-13-enforce-and-document.md.

enum PlayerInstanceNum {
    PI_Unset = 0,
    PI_Grab,
    PI_Drop,
    PI_Whip,
    PI_WhipEnd,
    PI_DirctCtrl,
    PI_PsngrCtrl,
    PI_DirctCtLeave,
    PI_PsngrCtLeave,
    PI_QueryCrtr,
    PI_UnqueryCrtr,
    PI_HeartZoom,
    PI_HeartZoomOut,
    PI_CrCtrlFade,
    PI_MapFadeTo,
    PI_MapFadeFrom,
    PI_ZoomToPos,
    PI_UnusedSlot17,
    PI_UnusedSlot18,
};
/******************************************************************************/
#pragma pack(1)

struct Thing;
struct PlayerInfo;

typedef int64_t (*InstncInfo_Func)(struct PlayerInfo *player, int64_t *n);

struct PlayerInstanceInfo { // sizeof = 44
  int64_t length_turns;
  int64_t instance_state;
  InstncInfo_Func start_cb;
  InstncInfo_Func maintain_cb;
  InstncInfo_Func end_cb;
  int64_t start_callback_parameters[2];
  unsigned char extra_callback_data[8];
  int64_t maintain_end_callback_parameter;
  int64_t reserved_callback_parameter;
};

#define PLAYER_INSTANCES_COUNT 19
#define PLAYER_INSTANCES_COUNT_OLD 17

/******************************************************************************/

#pragma pack()
/******************************************************************************/
extern struct PlayerInstanceInfo player_instance_info[PLAYER_INSTANCES_COUNT];
/******************************************************************************/
void set_player_instance(struct PlayerInfo *player, int64_t ninum, TbBool force);
void turn_off_query(PlayerNumber plyr_idx);
int64_t filter_creatures_owned_by_keepers(const struct Thing *thing, MaxTngFilterParam param, int64_t a3);
void process_player_instance(struct PlayerInfo *player);
void process_player_instances(void);

void leave_creature_as_passenger(struct PlayerInfo *player, struct Thing *thing);
void leave_creature_as_controller(struct PlayerInfo *player, struct Thing *thing);
#define set_selected_creature(player,thing) set_selected_creature_f(player, thing, __func__)
TbBool set_selected_creature_f(struct PlayerInfo *player, struct Thing *thing, const char *func_name);
#define set_selected_thing(player,thing) set_selected_thing_f(player, thing, __func__)
TbBool set_selected_thing_f(struct PlayerInfo *player, struct Thing *thing, const char *func_name);
TbBool clear_selected_thing(struct PlayerInfo *player);

TbBool is_thing_directly_controlled(const struct Thing *thing);
TbBool is_thing_passenger_controlled(const struct Thing *thing);
TbBool is_thing_some_way_controlled(const struct Thing *thing);
TbBool is_thing_directly_controlled_by_player(const struct Thing *thing, PlayerNumber plyr_idx);
TbBool is_thing_passenger_controlled_by_player(const struct Thing *thing, PlayerNumber plyr_idx);

void set_player_zoom_to_position(struct PlayerInfo *player,struct Coord3d *pos);

struct Room *player_build_room_at(MapSubtlCoord stl_x, MapSubtlCoord stl_y, PlayerNumber plyr_idx, RoomKind rkind, int64_t slabs_left);
TbBool player_place_trap_at(MapSubtlCoord stl_x, MapSubtlCoord stl_y, PlayerNumber plyr_idx, ThingModel tngmodel);
TbBool player_place_trap_without_check_at(MapSubtlCoord stl_x, MapSubtlCoord stl_y, PlayerNumber plyr_idx, ThingModel tngmodel, TbBool free);
/** Places a trap at exactly this subtile, whatever the trap's own PlaceOnSubtile setting: shipped campaign
 *  maps have several traps per slab (level editor). */
TbBool player_place_trap_at_subtile_without_check(MapSubtlCoord stl_x, MapSubtlCoord stl_y, PlayerNumber plyr_idx, ThingModel tngmodel, TbBool free);
TbBool player_place_door_at(MapSubtlCoord stl_x, MapSubtlCoord stl_y, PlayerNumber plyr_idx, ThingModel tngmodel);
TbBool player_place_door_without_check_at(MapSubtlCoord stl_x, MapSubtlCoord stl_y, PlayerNumber plyr_idx, ThingModel tngmodel, TbBool free);

void level_lost_go_first_person(PlayerNumber plyr_idx);
// docs/refactor/editor/04-views-camera-overlays.md -- View > 1st Person.
// Same spectator-creature setup as level_lost_go_first_person() above, but
// spawns at an explicit position instead of searching for an existing
// owned creature to derive one from -- an editor session's map may have no
// creatures at all (a fresh New Map), where level_lost_go_first_person()
// would silently no-op.
void level_editor_go_spectator_at(PlayerNumber plyr_idx, MapCoord pos_x, MapCoord pos_y);
int64_t packet_place_door(MapSubtlCoord stl_x, MapSubtlCoord stl_y, PlayerNumber plyr_idx, ThingModel tngmodel, TbBool allowed);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
