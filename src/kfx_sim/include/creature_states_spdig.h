/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file creature_states_spdig.h
 *     Header file for creature_states_spdig.c.
 * @par Purpose:
 *     Creature state machine functions for special diggers (imps).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   KeeperFX Team
 * @date     23 Sep 2009 - 05 Jan 2011
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_CRTRSTATESPDIG_H
#define DK_CRTRSTATESPDIG_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
#pragma pack(1)

struct Thing;
struct Room;
#pragma pack()
/******************************************************************************/
int64_t imp_arrives_at_convert_dungeon(struct Thing *thing);
int64_t imp_arrives_at_dig_or_mine(struct Thing *thing);
int64_t imp_arrives_at_improve_dungeon(struct Thing *thing);
int64_t imp_arrives_at_reinforce(struct Thing *thing);
int64_t imp_birth(struct Thing *thing);
int64_t imp_converts_dungeon(struct Thing *thing);
int64_t imp_digs_mines(struct Thing *thing);
int64_t imp_doing_nothing(struct Thing *thing);
int64_t imp_drops_gold(struct Thing *thing);
int64_t imp_improves_dungeon(struct Thing *thing);
int64_t imp_last_did_job(struct Thing *thing);
int64_t imp_picks_up_gold_pile(struct Thing *thing);
int64_t imp_reinforces(struct Thing *thing);
int64_t imp_toking(struct Thing *thing);
int64_t creature_pick_up_unconscious_body(struct Thing *thing);
int64_t creature_picks_up_corpse(struct Thing *thing);
int64_t creature_picks_up_spell_object(struct Thing *thing);
int64_t creature_picks_up_crate_for_workshop(struct Thing *thing);
int64_t creature_picks_up_trap_object(struct Thing *thing);
int64_t creature_drops_corpse_in_graveyard(struct Thing *thing);
int64_t creature_drops_crate_in_workshop(struct Thing *thing);
int64_t creature_drops_spell_object_in_library(struct Thing *thing);
int64_t creature_arms_trap(struct Thing *thing);
int64_t creature_going_to_safety_for_toking(struct Thing *thing);
int64_t check_out_available_spdigger_drop_tasks(struct Thing *digger);
TbBool creature_is_dragging_or_being_dragged(const struct Thing *thing);
TbBool creature_drop_thing_to_another_room(struct Thing* thing, struct Room* skiproom, RoomRole rrole);
TbBool set_creature_being_dragged_by(struct Thing *dragtng, struct Thing *thing);
int64_t creature_arms_trap_first_person(struct Thing *creatng);
int64_t creature_save_unconscious_creature(struct Thing *thing);
int64_t slab_is_my_door(int64_t plyr_idx, int64_t slb_x, int64_t slb_y);
int64_t digger_work_experience(struct Thing *spdigtng);
TbBool too_much_gold_lies_around_thing(const struct Thing *thing);
GoldAmount take_from_gold_pile(MapSubtlCoord stl_x, MapSubtlCoord stl_y, int64_t limit);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
