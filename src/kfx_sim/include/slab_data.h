/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file slab_data.h
 *     Header file for slab_data.c.
 * @par Purpose:
 *     Map Slabs support functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     25 Apr 2009 - 12 May 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_SLABDATA_H
#define DK_SLABDATA_H

#include "globals.h"
#include "bflib_basics.h"
#include "slab_types.h"
#include "config_terrain.h"
// struct SlabSet/SlabObj/SLABSET_COUNT/SLABOBJS_COUNT moved to kfx_config's
// config_slabsets.h (stage 13.3) -- kfx_config embeds them by value and
// is the lowest-ranked real consumer.
#include "config_slabsets.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/

// enum SlabTypes moved to kfx_platform's globals.h (stage 13.3, docs/
// refactor/stage-13-enforce-and-document.md) -- pure ID vocabulary,
// used across every library.
// enum WlbType moved there too (Abyss dungeons, #5169) for the same
// reason: kfx_config's config_terrain.c needs it.

/******************************************************************************/
#pragma pack(1)

struct PlayerInfo;
struct Thing;

#pragma pack()
/******************************************************************************/
extern struct SlabMap bad_slabmap_block;
#define INVALID_SLABMAP_BLOCK (&bad_slabmap_block)
/******************************************************************************/
SlabCodedCoords get_slab_number(MapSlabCoord slb_x, MapSlabCoord slb_y);
MapSlabCoord slb_num_decode_x(SlabCodedCoords slb_num);
MapSlabCoord slb_num_decode_y(SlabCodedCoords slb_num);

TbBool slab_kind_is_animated(SlabKind slbkind);

struct SlabMap *get_slabmap_block(MapSlabCoord slb_x, MapSlabCoord slb_y);
struct SlabMap *get_slabmap_for_subtile(MapSubtlCoord stl_x, MapSubtlCoord stl_y);
struct SlabMap *get_slabmap_direct(SlabCodedCoords slab_num);
struct SlabMap *get_slabmap_thing_is_on(const struct Thing *thing);
TbBool slabmap_block_invalid(const struct SlabMap *slb);
TbBool slab_coords_invalid(MapSlabCoord slb_x, MapSlabCoord slb_y);
int64_t slabmap_owner(const struct SlabMap *slb);
SlabKind slabmap_kind(const struct SlabMap *slb);
struct SlabConfigStats *get_slab_stats(const struct SlabMap *slb);
void set_slab_owner(MapSlabCoord slb_x, MapSlabCoord slb_y, PlayerNumber owner);
PlayerNumber get_slab_owner_thing_is_on(const struct Thing *thing);
uint64_t slabmap_wlb(struct SlabMap *slb);
void slabmap_set_wlb(struct SlabMap *slb, uint64_t wlb_type);
SlabCodedCoords get_next_slab_number_in_room(SlabCodedCoords slab_num);
int64_t calculate_effeciency_score_for_room_slab(SlabCodedCoords slab_num, PlayerNumber plyr_idx, int64_t synergy_slab_num);

TbBool slab_is_safe_land(PlayerNumber plyr_idx, MapSlabCoord slb_x, MapSlabCoord slb_y);
TbBool slab_is_door(MapSlabCoord slb_x, MapSlabCoord slb_y);
TbBool slab_is_liquid(MapSlabCoord slb_x, MapSlabCoord slb_y);
TbBool slab_is_wall(MapSlabCoord slb_x, MapSlabCoord slb_y);
TbBool is_slab_type_walkable(SlabKind slbkind);

TbBool slab_good_for_computer_dig_path(const struct SlabMap *slb);
TbBool is_valid_hug_subtile(MapSubtlCoord stl_x, MapSubtlCoord stl_y, PlayerNumber plyr_idx);

TbBool can_build_room_at_slab(PlayerNumber plyr_idx, RoomKind rkind,
    MapSlabCoord slb_x, MapSlabCoord slb_y);

TbBool can_build_room_at_slab_fast(PlayerNumber plyr_idx, RoomKind rkind,
    MapSlabCoord slb_x, MapSlabCoord slb_y);
TbBool room_can_build_on_bridge_slab(RoomKind rkind, SlabKind slbkind);

int64_t check_room_at_slab_loose(PlayerNumber plyr_idx, RoomKind rkind,
    MapSlabCoord slb_x, MapSlabCoord slb_y, int64_t looseness);

void clear_slabs(void);
void reveal_whole_map(struct PlayerInfo *player);
void update_blocks_in_area(MapSubtlCoord sx, MapSubtlCoord sy, MapSubtlCoord ex, MapSubtlCoord ey);
void update_blocks_around_slab(MapSlabCoord slb_x, MapSlabCoord slb_y);
void update_map_collide(SlabKind slbkind, MapSubtlCoord stl_x, MapSubtlCoord stl_y);
void copy_block_with_cube_groups(int64_t itm_idx, MapSubtlCoord stl_x, MapSubtlCoord stl_y);
void do_slab_efficiency_alteration(MapSlabCoord slb_x, MapSlabCoord slb_y);
void collect_rooms_around_slab(MapSlabCoord slb_x, MapSlabCoord slb_y, struct Room** room_list, int64_t room_list_len);
void recalculate_rooms_in_list(struct Room** room_list, int64_t room_list_len);
void do_unprettying(PlayerNumber plyr_idx, MapSlabCoord slb_x, MapSlabCoord slb_y);

TbBool slab_kind_has_no_ownership(SlabKind slbkind);

TbBool players_land_by_bridgeable_slab(PlayerNumber plyr_idx, MapSlabCoord slb_x, MapSlabCoord slb_y);
TbBool slab_by_players_land(PlayerNumber plyr_idx, MapSlabCoord slb_x, MapSlabCoord slb_y);
TbBool player_can_claim_slab(PlayerNumber plyr_idx, MapSlabCoord slb_x, MapSlabCoord slb_y);
SlabKind choose_rock_type(PlayerNumber plyr_idx, MapSlabCoord slb_x, MapSlabCoord slb_y);

void set_player_texture(PlayerNumber plyr_idx, int64_t texture_id);

/******************************************************************************/
#include "roomspace.h"
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
