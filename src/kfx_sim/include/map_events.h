/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file map_events.h
 *     Header file for map_events.c.
 * @par Purpose:
 *     Map events support functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     11 Mar 2010 - 12 May 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_MAP_EVENTS_H
#define DK_MAP_EVENTS_H

#include "globals.h"
#include "bflib_guibtns.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define EVENT_BUTTONS_COUNT    12
#define EVENT_KIND_COUNT       36
#define EVENTS_COUNT          200
#define INVALID_EVENT &kfx_sim_state.event[0]

// enum EventKinds moved to kfx_platform's globals.h (stage 13.3) --
// pure name/ID vocabulary, no functional coupling here. The
// ComputerKeeper cross-references from the original comments:
// HeartAttacked=AttkHrt1[event4](99% defend heart), EnemyFight=Fight1/2[event7/8],
// Breach=DnBreach[event1](75% defend breach), TreasureRoomFull=RomFTrsr[event5],
// CreaturePayday=PayDay1[event14], NoMoreLivingSet=RomFLair[event6],
// RoomUnderAttack=AttkRom1/2[event2/3](75% defend room), RoomLost=RoomLost[event15],
// PrisonerStarving=MoanPris1/2/3[event16/17/18], TorturedHurt=SaveTort1[event19],
// EnemyDoor=DoorAtck1[event20].

enum EventFlags {
    EvF_Exists       = 0x0001,
    EvF_BtnFirstFall = 0x0002, /*< Informs whether the button is falling for a first time. */
    EvF_BtnFalling   = 0x0004,
};

/******************************************************************************/
#pragma pack(1)

struct Thing;
struct Dungeon;
struct PlayerInfo;

struct Event {
    unsigned char flags;
    EventIndex index;
    int64_t mappos_x;
    int64_t mappos_y;
    unsigned char owner;
    unsigned char kind;
    int64_t target;
    /** Button lifespan, decreased over time. When reaches 0, the button disappears. */
    uint64_t lifespan_turns;
    /** Custom button icon sprite index; -1 uses the default event icon. */
    int64_t icon_idx;
};

struct Bookmark {
  MapSubtlCoord x;
  MapSubtlCoord y;
  unsigned char flags;
};

#pragma pack()
/******************************************************************************/
extern struct EventTypeInfo event_button_info[EVENT_KIND_COUNT];
/******************************************************************************/
struct Event *get_event_of_type_for_player(EventKind evkind, PlayerNumber plyr_idx);
struct Event *get_event_of_target_and_type_for_player(int64_t target, EventKind evkind, PlayerNumber plyr_idx);
struct Event *get_event_nearby_of_type_for_player(MapCoord map_x, MapCoord map_y, int64_t max_dist, EventKind evkind, PlayerNumber plyr_idx);

TbBool event_is_invalid(const struct Event *event);
TbBool event_exists(const struct Event* event);
EventIndex event_create_event_or_update_nearby_existing_event(MapCoord map_x, MapCoord map_y, EventKind evkind, unsigned char dngn_id, int64_t target);
EventIndex event_create_event_or_update_same_target_existing_event(MapCoord map_x, MapCoord map_y, EventKind evkind, unsigned char dngn_id, int64_t target);
EventIndex event_create_event_or_update_old_event(MapCoord map_x, MapCoord map_y, EventKind evkind, unsigned char dngn_id, int64_t target);
void event_initialise_all(void);
int64_t event_move_player_towards_event(struct PlayerInfo *player, int64_t event_idx);
struct Event *event_create_event(MapCoord map_x, MapCoord map_y, EventKind evkind, unsigned char dngn_id, int64_t msg_id);
struct Event *event_allocate_free_event_structure(void);
void event_initialise_event(struct Event *event, MapCoord map_x, MapCoord map_y, EventKind evkind, unsigned char dngn_id, int64_t msg_id);
void event_add_to_event_buttons_list_or_replace_button(struct Event *event, struct Dungeon *dungeon);
void event_update_on_battle_removal(BattleIndex battle_idx);
void event_delete_event_structure(int64_t ev_idx);
void event_delete_event(int64_t plridx, EventIndex evidx);
void event_update_last_use(struct Event *event);
void clear_events(void);
void remove_events_thing_is_attached_to(struct Thing *thing);
ThingIndex get_thing_index_event_is_attached_to(const struct Event *event);
struct Thing *event_is_attached_to_thing(EventIndex evidx);
void maintain_my_event_list(struct Dungeon *dungeon);
void kill_oldest_my_event(struct Dungeon *dungeon);
void event_kill_all_players_events(int64_t plyr_idx);
void event_process_events(void);
void update_all_events(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
