/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file room_data.h
 *     Header file for room_data.c.
 * @par Purpose:
 *     Rooms support functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     17 Apr 2009 - 14 May 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_ROOMDATA_H
#define DK_ROOMDATA_H

#include "bflib_basics.h"
#include "config_creature.h"
#include "globals.h"
#include "config_creature.h"
#include "player_data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SLAB_AROUND_COUNT      4
#define ROOMS_COUNT          511
/******************************************************************************/
enum RoomFlags {
    RoF_Allocated           = 0x01,
};

// enum RoomKinds moved to kfx_platform's globals.h (stage 13.3, docs/
// refactor/stage-13-enforce-and-document.md) -- pure ID vocabulary,
// used across every library.

enum RoomAreaChoose {
    RoArC_ANY = 0,
    RoArC_BORDER,
    RoArC_CENTER,
};

#define ROOM_EFFICIENCY_MAX 256
/******************************************************************************/
#pragma pack(1)

struct Thing;
struct Coord3d;
struct Room;
struct Dungeon;

typedef void (*Room_Update_Func)(struct Room *);

// Moved from kfx_config's config_terrain.h/.c (see docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md): every element is a
// kfx_sim function, and kfx_config never read these arrays itself, only
// defined them for this file's own room_data.c to read.
extern Room_Update_Func terrain_room_total_capacity_func_list[13];
extern Room_Update_Func terrain_room_used_capacity_func_list[10];

struct Room {
    unsigned char alloc_flags;
    RoomIndex index; // index in the rooms array
    PlayerNumber owner;
    RoomIndex prev_of_owner;
    RoomIndex next_of_owner;
    MapSubtlCoord central_stl_x;
    MapSubtlCoord central_stl_y;
    RoomKind kind;
    HitPoints health;
    int64_t total_capacity;
    int64_t used_capacity;
    /* Informs whether players are interested in that room.
     * Usually used for neutral rooms, set if a player is starting to dig to that room. */
    unsigned char player_interested[PLAYERS_COUNT];
    /** For rooms which can store things, amount of storage space, or sum of gold, used by them.
     *  Rooms which can store things are workshops, libraries, treasure rooms etc. */
    uint64_t capacity_used_for_storage;
    ThingIndex cached_nearby_creature_index;
    // Rooms with a global list: Entrances
    RoomIndex prev_of_kind;
    RoomIndex next_of_kind;
    // For rooms which store creatures, amount of each model: Lairs
    unsigned char content_per_model[CREATURE_TYPES_MAX];
    int64_t hatch_gameturn;

    SlabCodedCoords slabs_list;
    SlabCodedCoords slabs_list_tail;
    int64_t slabs_count;
    ThingIndex creatures_list;
    int64_t efficiency;
    SlabCodedCoords flame_slb;
    unsigned char flames_around_idx;
    unsigned char flame_stl;
    GameTurn creation_turn;
};


/** Max. amount of items to be repositioned in a room */
#define ROOM_REPOSITION_COUNT 16

/**
 * Structure used for repositioning things in rooms so that they're not placed in solid columns.
 */
struct RoomReposition {
    int64_t used;
    ThingModel models[ROOM_REPOSITION_COUNT];
    CrtrExpLevel exp_level[ROOM_REPOSITION_COUNT];
};

#define INVALID_ROOM (&kfx_sim_state.rooms[0])

/******************************************************************************/

#pragma pack()
/******************************************************************************/
extern int64_t const room_effect_elements[];
extern struct AroundLByte const room_spark_offset[];
/******************************************************************************/
struct Room *room_get(RoomIndex room_idx);
struct Room *subtile_room_get(MapSubtlCoord stl_x, MapSubtlCoord stl_y);
struct Room *slab_room_get(MapSlabCoord slb_x, MapSlabCoord slb_y);
TbBool room_is_invalid(const struct Room *room);
TbBool room_exists(const struct Room *room);

uint64_t compute_room_max_health(int64_t slabs_count,int64_t efficiency);
void count_slabs_all_only(struct Room *room);
void count_slabs_all_wth_effcncy(struct Room *room);
void count_slabs_no_min_wth_effcncy(struct Room *room);
void count_slabs_div2_wth_effcncy(struct Room *room);
void count_slabs_div2_nomin_effcncy(struct Room *room);
void count_slabs_mul2_wth_effcncy(struct Room *room);
void count_slabs_pow2_wth_effcncy(struct Room *room);
void set_room_efficiency(struct Room *room);
void do_room_recalculation(struct Room* room);
int64_t get_room_slabs_count(PlayerNumber plyr_idx, RoomKind rkind);
int64_t get_room_of_role_slabs_count(PlayerNumber plyr_idx, RoomRole rrole);
int64_t get_room_kind_used_capacity_fraction(PlayerNumber plyr_idx, RoomKind room_kind);
void get_room_kind_total_and_used_capacity(struct Dungeon *dungeon, RoomKind room_kind, int64_t *total_cap, int64_t *used_cap);
void get_room_kind_total_used_and_storage_capacity(struct Dungeon *dungeon, RoomKind room_kind, int64_t *total_cap, int64_t *used_cap, int64_t *storaged_cap);
TbBool thing_is_on_any_room_tile(const struct Thing *thing);
TbBool thing_is_on_own_room_tile(const struct Thing *thing);
struct Room *get_room_thing_is_on(const struct Thing *thing);
struct Room *get_room_at_pos(struct Coord3d *pos);
void reinitialise_map_rooms(void);
struct Thing *find_gold_hoarde_at(MapSubtlCoord stl_x, MapSubtlCoord stl_y);

// Finding position within room
TbBool find_random_valid_position_for_thing_in_room(struct Thing *thing, struct Room *room, struct Coord3d *pos);
TbBool find_first_valid_position_for_thing_anywhere_in_room(const struct Thing *thing, struct Room *room, struct Coord3d *pos);
TbBool find_random_position_at_area_of_room(struct Coord3d *pos, const struct Room *room, unsigned char room_area, struct Thing *thing);

// Finding a room for a thing
TbBool creature_can_get_to_any_of_players_rooms(struct Thing *thing, PlayerNumber owner);
struct Room *find_room_of_role_with_spare_room_item_capacity(PlayerNumber plyr_idx, RoomRole rrole);
struct Room *find_nth_room_of_owner_with_spare_item_capacity_starting_with(int64_t room_idx, int64_t n, int64_t spare);
struct Room *find_room_of_role_with_spare_capacity(PlayerNumber owner, RoomRole rrole, int64_t spare);
struct Room *find_nth_room_of_owner_with_spare_capacity_starting_with(int64_t room_idx, int64_t n, int64_t spare);
struct Room *find_room_of_role_with_most_spare_capacity(const struct Dungeon *dungeon,RoomRole rrole, int64_t *total_spare_cap);
struct Room *find_room_nearest_to_position(PlayerNumber plyr_idx, RoomKind rkind, const struct Coord3d *pos, int64_t *room_distance);
// Finding a navigable room for a thing
struct Room *find_room_of_role_for_thing_with_used_capacity(const struct Thing *creatng, PlayerNumber plyr_idx, RoomRole rrole, unsigned char nav_flags, int64_t min_used_cap);
struct Room *find_random_room_of_role_with_used_capacity_creature_can_navigate_to(struct Thing *thing, PlayerNumber owner, RoomRole rrole, unsigned char nav_flags);
struct Room *find_nearest_room_of_role_for_thing_with_spare_capacity(struct Thing *thing, signed char owner, RoomRole rrole, unsigned char nav_flags, int64_t spare);

void create_room_flag(struct Room *room);
void delete_room_flag(struct Room *room);
struct Room *allocate_free_room_structure(void);
int64_t i_can_allocate_free_room_structure(void);
void add_slab_to_room_tiles_list(struct Room *room, MapSlabCoord slb_x, MapSlabCoord slb_y);
void remove_slab_from_room_tiles_list(struct Room *room, MapSlabCoord slb_x, MapSlabCoord slb_y);
TbBool add_slab_list_to_room_tiles_list(struct Room *room, SlabCodedCoords slb_num);
void delete_all_room_structures(void);
void delete_room_structure(struct Room *room);
void delete_room_slabbed_objects(SlabCodedCoords slb_num);
struct Room *link_adjacent_rooms_of_type(PlayerNumber owner, MapSubtlCoord x, MapSubtlCoord y, RoomKind rkind);
struct Room *create_room(PlayerNumber owner, RoomKind rkind, MapSubtlCoord stl_x, MapSubtlCoord stl_y);
TbBool update_room_contents(struct Room *room);
struct Room *get_room_of_given_role_for_thing(const struct Thing *thing, const struct Dungeon *dungeon, RoomRole rrole, int64_t needed_capacity);
struct Room *place_room(PlayerNumber owner, RoomKind rkind, MapSubtlCoord stl_x, MapSubtlCoord stl_y);
TbBool slab_is_area_outer_border(MapSlabCoord slb_x, MapSlabCoord slb_y);
TbBool slab_is_area_inner_fill(MapSlabCoord slb_x, MapSlabCoord slb_y);
MapCoordDelta get_distance_to_room(const struct Coord3d *pos, const struct Room *room);

TbBool initialise_map_rooms(void);
void init_room_sparks(struct Room *room);
void replace_room_slab(struct Room *room, MapSlabCoord slb_x, MapSlabCoord slb_y, unsigned char owner, unsigned char a5);
void delete_room_slab_when_no_free_room_structures(MapCoord slb_x, MapCoord slb_y, unsigned char gnd_slab);
int64_t calculate_room_efficiency(const struct Room *room);
void kill_room_slab_and_contents(PlayerNumber plyr_idx, MapSlabCoord slb_x, MapSlabCoord slb_y);
void free_room_structure(struct Room *room);
void reset_creatures_rooms(struct Room *room);

TbBool remove_item_from_room_capacity(struct Room *room);
TbBool add_item_to_room_capacity(struct Room *room, TbBool force);
TbBool room_has_enough_free_capacity_for_creature_job(const struct Room *room, const struct Thing *creatng, CreatureJob jobpref);

int64_t count_slabs_of_room_type(PlayerNumber plyr_idx, RoomKind rkind);
int64_t claim_enemy_room(struct Room *room,struct Thing *claimtng);
int64_t claim_room(struct Room *room,struct Thing *claimtng);
int64_t take_over_room(struct Room* room, PlayerNumber newowner);
TbBool remove_room_from_players_list(struct Room* room, PlayerNumber plyr_idx);
void destroy_room_leaving_unclaimed_ground(struct Room *room, TbBool create_rubble);
TbBool create_effects_on_room_slabs(struct Room *room, ThingModel effkind, int64_t effrange, PlayerNumber effowner);
TbBool clear_dig_on_room_slabs(struct Room *room, PlayerNumber plyr_idx);
void do_room_integration(struct Room *room);
void destroy_dungeon_heart_room(PlayerNumber plyr_idx, const struct Thing *heartng);

void update_room_total_capacity(struct Room *room);
int64_t reinitialise_rooms_of_kind(RoomKind rkind);
int64_t recalculate_effeciency_for_rooms_of_kind(RoomKind rkind);

TbBool find_random_valid_position_for_thing_in_room_avoiding_object_excluding_room_slab(struct Thing *thing, struct Room *room, struct Coord3d *pos, int64_t slbnum);

/* MOVE TO room_list.c/h */
struct Room *find_nearest_room_of_role_for_thing_with_spare_item_capacity(struct Thing *thing, PlayerNumber plyr_idx, RoomRole rrole, unsigned char nav_flags);
struct Room *find_random_room_of_role_for_thing(struct Thing *thing, PlayerNumber owner, RoomRole rkind, unsigned char nav_flags);
struct Room *find_random_room_of_role_for_thing_with_spare_room_item_capacity(struct Thing *thing, PlayerNumber owner, RoomRole rrole, unsigned char nav_flags);
struct Room *pick_random_room_of_role(PlayerNumber plyr_idx, RoomRole rrole);

void redraw_slab_map_elements(MapSlabCoord slb_x, MapSlabCoord slb_y);

TbBool store_reposition_entry(struct RoomReposition * rrepos, ThingModel tngmodel);
void init_reposition_struct(struct RoomReposition * rrepos);
TbBool store_creature_reposition_entry(struct RoomReposition * rrepos, ThingModel tngmodel, CrtrExpLevel exp_level);
int64_t get_required_room_capacity_for_object(RoomRole room_role, ThingModel objmodel, ThingModel relmodel);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
