/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file dungeon_data.c
 *     Dungeon data structures definitions.
 * @par Purpose:
 *     Defines functions for dungeon-related structures support.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     10 Nov 2009 - 20 Jan 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "dungeon_data.h"

#include <inttypes.h>
#include "globals.h"
#include "bflib_basics.h"
#include "config_terrain.h"
#include "player_instances.h"
#include "bflib_joyst.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "magic_powers.h"
#include "ports/audio_port.h"
#include "post_inc.h"

/******************************************************************************/
struct Dungeon bad_dungeon;
/******************************************************************************/
struct Dungeon *get_players_num_dungeon_f(PlayerNumber plyr_idx,const char *func_name)
{
    struct PlayerInfo* player = get_player(plyr_idx);
    PlayerNumber plyr_num = player->id_number;
    if (player_invalid(player) || (plyr_num < 0) || (plyr_num >= DUNGEONS_COUNT))
    {
        ERRORMSG("%s: Tried to get players %" PRId64 " non-existing dungeon %" PRId64 "!",func_name,(int64_t)plyr_idx,(int64_t)plyr_num);
        return INVALID_DUNGEON;
    }
    if (plyr_num != player->id_number)
    {
        WARNDBG(7,"%s: Player number(%" PRId64 ") differ from index(%" PRId64 ")!",func_name,(int64_t)plyr_num,(int64_t)plyr_idx);
    }
    return &(kfx_sim_state.dungeon[(int64_t)plyr_num]);
}

struct Dungeon *get_players_dungeon_f(const struct PlayerInfo *player,const char *func_name)
{
    PlayerNumber plyr_num = player->id_number;
    if (player_invalid(player) || (plyr_num < 0) || (plyr_num >= DUNGEONS_COUNT))
    {
        ERRORLOG("%s: Tried to get non-existing dungeon %" PRId64 "!",func_name,(int64_t)plyr_num);
        return INVALID_DUNGEON;
    }
    return &(kfx_sim_state.dungeon[(int64_t)plyr_num]);
}

struct Dungeon *get_dungeon_f(PlayerNumber plyr_num,const char *func_name)
{
    if ((plyr_num < 0) || (plyr_num >= DUNGEONS_COUNT))
    {
        ERRORLOG("%s: Tried to get non-existing dungeon %" PRId64 "!", func_name, (int64_t)(plyr_num));
        return INVALID_DUNGEON;
    }
    return &(kfx_sim_state.dungeon[(int64_t)plyr_num]);
}

TbBool dungeon_invalid(const struct Dungeon *dungeon)
{
    if (dungeon == INVALID_DUNGEON)
        return true;
    return (dungeon < &kfx_sim_state.dungeon[0]);
}

void clear_dungeons(void)
{
  SYNCDBG(6,"Starting");
  for (int64_t i = 0; i < DUNGEONS_COUNT; i++)
  {
      memset(&kfx_sim_state.dungeon[i], 0, sizeof(struct Dungeon));
      kfx_sim_state.dungeon[i].owner = PLAYERS_COUNT;
  }
  memset(&bad_dungeon, 0, sizeof(struct Dungeon));
  bad_dungeon.owner = PLAYERS_COUNT;
}

void decrease_dungeon_area(PlayerNumber plyr_idx, int64_t value)
{
    if (plyr_idx == kfx_config_state.neutral_player_num)
        return;
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    if (dungeon->total_area < value)
      dungeon->total_area = 0;
    else
      dungeon->total_area -= value;
}

void increase_room_area(PlayerNumber plyr_idx, int64_t value)
{
    if (plyr_idx == kfx_config_state.neutral_player_num)
        return;
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    dungeon->room_manage_area += value;
    dungeon->total_area += value;
}

void decrease_room_area(PlayerNumber plyr_idx, int64_t value)
{
    if (plyr_idx == kfx_config_state.neutral_player_num)
        return;
    struct Dungeon* dungeon = get_dungeon(plyr_idx);

    if (dungeon->room_manage_area < value)
      dungeon->room_manage_area = 0;
    else
      dungeon->room_manage_area -= value;

    if (dungeon->total_area < value)
      dungeon->total_area = 0;
    else
      dungeon->total_area -= value;
}

void increase_dungeon_area(PlayerNumber plyr_idx, int64_t value)
{
    if (plyr_idx == kfx_config_state.neutral_player_num)
        return;
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    dungeon->total_area += value;
}

void player_add_offmap_gold(PlayerNumber plyr_idx, GoldAmount value)
{
    // note that we can't get_players_num_dungeon() because players
    // may be uninitialized yet when this is called.
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    if (dungeon_invalid(dungeon)) {
        WARNLOG("Cannot give gold player %" PRId64 " with no dungeon",(int64_t)plyr_idx);
        return;
    }
    // If we're removing gold instead of adding, make sure we won't remove too much
    if ((value < 0) && (dungeon->offmap_money_owned < -value)) {
        value = -dungeon->offmap_money_owned;
    }
    dungeon->offmap_money_owned += value;
    dungeon->total_money_owned += value;
}

/** Returns if given player owns a room of given role.
 *
 * @param plyr_idx Player index being checked.
 * @param rrole Room role being checked.
 * @return
 */
TbBool player_has_room_of_role(PlayerNumber plyr_idx, RoomRole rrole)
{
    if (plyr_idx == kfx_config_state.neutral_player_num)
        return false;
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    for (RoomKind rkind = 0; rkind < kfx_config_state.conf.slab_conf.room_types_count; rkind++)
    {
        if (room_role_matches(rkind, rrole))
        {
            if (dungeon->room_list_start[rkind] > 0)
                return true;
        }
    }
    return false;
}

/** counts all discrete rooms with the given role.
 *
 * @param plyr_idx Player index being checked.
 * @param rrole Room role being checked.
 * @return
 */
int64_t count_player_discrete_rooms_with_role(PlayerNumber plyr_idx, RoomRole rrole)
{
    if (plyr_idx == kfx_config_state.neutral_player_num)
        return 0;
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    int64_t count = 0;
    for (RoomKind rkind = 0; rkind < kfx_config_state.conf.slab_conf.room_types_count; rkind++)
    {
        if (room_role_matches(rkind, rrole))
        {
            count += dungeon->room_discrete_count[rkind];
        }
    }
    return count;
}

struct Thing *get_player_soul_container(PlayerNumber plyr_idx)
{
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    if (dungeon->dnheart_idx > 0) {
        return thing_get(dungeon->dnheart_idx);
    }
    return INVALID_THING;
}

TbBool player_has_heart(PlayerNumber plyr_idx)
{
    return thing_exists(get_player_soul_container(plyr_idx));
}

void add_heart_health(PlayerNumber plyr_idx,HitPoints healthdelta,TbBool warn_on_damage)
{
    struct Thing* heartng = get_player_soul_container(plyr_idx);
    if (thing_exists(heartng))
    {
        struct ObjectConfigStats* objst = get_object_model_stats(heartng->model);
        int64_t old_health = heartng->health;
        int64_t new_health = heartng->health + healthdelta;
        if (new_health > objst->health)
        {
            SCRIPTDBG(7,"Player %" PRIu64 "'s calculated heart health (%" PRId64 ") is greater than maximum: %" PRId64, (uint64_t)(heartng->owner), (int64_t)(new_health), (int64_t)(objst->health));
            new_health = objst->health;
        }
        heartng->health = new_health;
        if (warn_on_damage)
        {
            if (heartng->health < old_health)
            {
                event_create_event_or_update_nearby_existing_event(heartng->mappos.x.val, heartng->mappos.y.val, EvKind_HeartAttacked, heartng->owner, heartng->index);
                if (is_my_player_number(heartng->owner))
                {
                    audio_output_message(SMsg_HeartUnderAttack, 400);
                    controller_rumble(50);
                }
            }
        }
    }
}


/** Returns if given dungeon contains a room of given kind.
 *
 * @param dungeon Target dungeon.
 * @param rkind Room kind being checked.
 * @return
 */
TbBool dungeon_has_room(const struct Dungeon *dungeon, RoomKind rkind)
{
    if (dungeon_invalid(dungeon)) {
        return false;
    }
    if ((rkind < 1) || (rkind >= kfx_config_state.conf.slab_conf.room_types_count)) {
        return false;
    }
    return (dungeon->room_list_start[rkind] > 0);
}

/** Returns if given dungeon contains a room of given kind.
 *
 * @param dungeon Target dungeon.
 * @param rrole Room role being checked.
 * @return
 */
TbBool dungeon_has_room_of_role(const struct Dungeon *dungeon, RoomRole rrole)
{
    if (dungeon_invalid(dungeon)) {
        return false;
    }

    for (RoomKind rkind = 0; rkind < kfx_config_state.conf.slab_conf.room_types_count; rkind++)
    {
        if(room_role_matches(rkind,rrole))
        {
            if ((rkind < 1) || (rkind >= kfx_config_state.conf.slab_conf.room_types_count)) {
                return false;
            }
            if (dungeon->room_list_start[rkind] > 0)
            {
                return true;
            }
        }
    }

    return false;

}

TbBool player_creature_tends_to(PlayerNumber plyr_idx, int64_t tend_type)
{
    if (plyr_idx == kfx_config_state.neutral_player_num)
        return false;
    const struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    switch (tend_type)
    {
    case CrTend_Imprison:
        return ((dungeon->creature_tendencies & CrTend_Imprison) != 0);
    case CrTend_Flee:
        return ((dungeon->creature_tendencies & CrTend_Flee) != 0);
    default:
        ERRORLOG("Bad tendency type %" PRId64,(int64_t)tend_type);
        return false;
    }
}

TbBool toggle_creature_tendencies(struct PlayerInfo *player, int64_t tend_type)
{
    struct Dungeon* dungeon = get_dungeon(player->id_number);
    switch (tend_type)
    {
    case CrTend_Imprison:
        dungeon->creature_tendencies ^= CrTend_Imprison;
        return true;
    case CrTend_Flee:
        dungeon->creature_tendencies ^= CrTend_Flee;
        return true;
    case CrTend_Imprison | CrTend_Flee:
        // Toggle both tendencies when combined value is passed
        dungeon->creature_tendencies ^= (CrTend_Imprison | CrTend_Flee);
        return true;
    default:
        ERRORLOG("Can't toggle tendency; bad tendency type %" PRId64,(int64_t)tend_type);
        return false;
    }
}

TbBool set_creature_tendencies(struct PlayerInfo *player, int64_t tend_type, TbBool val)
{
    struct Dungeon* dungeon = get_dungeon(player->id_number);
    if (dungeon_invalid(dungeon)) {
        ERRORLOG("Can't set tendency; player %" PRId64 " has no dungeon.",(int64_t)player->id_number);
        return false;
    }
    switch (tend_type)
    {
    case CrTend_Imprison:
        set_flag_value(dungeon->creature_tendencies, CrTend_Imprison, val);
        return true;
    case CrTend_Flee:
        set_flag_value(dungeon->creature_tendencies, CrTend_Flee, val);
        return true;
    default:
        ERRORLOG("Can't set tendency; bad tendency type %" PRId64,(int64_t)tend_type);
        return false;
    }
}

TbBool set_trap_buildable_and_add_to_amount(PlayerNumber plyr_idx, ThingModel tngmodel, int64_t buildable, int64_t amount)
{
    if ( (tngmodel <= 0) || (tngmodel >= kfx_config_state.conf.trapdoor_conf.trap_types_count) ) {
        ERRORDBG(1,"Can't set trap availability; invalid trap kind %" PRId64 ".",(int64_t)tngmodel);
        return false;
    }
    struct Dungeon* dungeon = get_dungeon(plyr_idx);

    if (dungeon_invalid(dungeon)) {
        ERRORDBG(11,"Can't set trap availability; player %" PRId64 " has no dungeon.",(int64_t)plyr_idx);
        return false;
    }
    if (buildable)
    {
        dungeon->mnfct_info.trap_build_flags[tngmodel] |= MnfBldF_Manufacturable;
    }
    else
    {
        dungeon->mnfct_info.trap_build_flags[tngmodel] &= ~MnfBldF_Manufacturable;
    }
    dungeon->mnfct_info.trap_amount_offmap[tngmodel] += amount;
    dungeon->mnfct_info.trap_amount_placeable[tngmodel] += amount;
    if (amount > 0)
    {
        dungeon->mnfct_info.trap_build_flags[tngmodel] |= MnfBldF_Built;
    }
    return true;
}

TbBool set_door_buildable_and_add_to_amount(PlayerNumber plyr_idx, ThingModel tngmodel, int64_t buildable, int64_t amount)
{
    if ( (tngmodel <= 0) || (tngmodel >= kfx_config_state.conf.trapdoor_conf.door_types_count) ) {
        ERRORDBG(1,"Can't set door availability; invalid door kind %" PRId64 ".",(int64_t)tngmodel);
        return false;
    }
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    if (dungeon_invalid(dungeon)) {
        ERRORDBG(11,"Can't set door availability; player %" PRId64 " has no dungeon.",(int64_t)plyr_idx);
        return false;
    }
    if (buildable)
    {
        dungeon->mnfct_info.door_build_flags[tngmodel] |= MnfBldF_Manufacturable;
    }
    else
    {
       dungeon->mnfct_info.door_build_flags[tngmodel] &= ~MnfBldF_Manufacturable;
    }
    dungeon->mnfct_info.door_amount_offmap[tngmodel] += amount;
    dungeon->mnfct_info.door_amount_placeable[tngmodel] += amount;
    if (amount > 0)
      dungeon->mnfct_info.door_build_flags[tngmodel] |= MnfBldF_Built;
    return true;
}

/**
 * Returns if there are any traps in the dungeon which can be put on map.
 * @param dungeon
 */
TbBool dungeon_has_any_buildable_traps(struct Dungeon *dungeon)
{
    for (ThingModel tngmodel = 1; tngmodel < kfx_config_state.conf.trapdoor_conf.trap_types_count; tngmodel++)
    {
        if ((dungeon->mnfct_info.trap_amount_stored[tngmodel] + dungeon->mnfct_info.trap_amount_offmap[tngmodel]) > 0)
            return true;

    }
    return false;
}

/**
 * Returns if there are any doors in the dungeon which can be put on map.
 * @param dungeon
 */
TbBool dungeon_has_any_buildable_doors(struct Dungeon *dungeon)
{
    for (ThingModel tngmodel = 1; tngmodel < kfx_config_state.conf.trapdoor_conf.door_types_count; tngmodel++)
    {
        if ((dungeon->mnfct_info.door_amount_stored[tngmodel] + dungeon->mnfct_info.door_amount_offmap[tngmodel]) > 0)
            return true;

    }
    return false;
}

TbBool restart_script_timer(PlayerNumber plyr_idx, int64_t timer_id)
{
    if ( (timer_id < 0) || (timer_id >= TURN_TIMERS_COUNT) ) {
        ERRORLOG("Can't restart timer; invalid timer id %" PRId64 ".",(int64_t)timer_id);
        return false;
    }
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    if (dungeon_invalid(dungeon)) {
        ERRORLOG("Can't restart timer; player %" PRId64 " has no dungeon.",(int64_t)plyr_idx);
        return false;
    }
    dungeon->turn_timers[timer_id].state = 1;
    dungeon->turn_timers[timer_id].count = get_gameturn();
    return true;
}

void add_to_script_timer(PlayerNumber plyr_idx, unsigned char timer_id, int64_t value)
{
    if (timer_id >= TURN_TIMERS_COUNT) {
        ERRORLOG("Can't manipulate timer; invalid timer id %" PRId64 ".",(int64_t)timer_id);
        return;
    }
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    if (dungeon_invalid(dungeon)) {
        ERRORLOG("Can't manipulate timer; player %" PRId64 " has no dungeon.",(int64_t)plyr_idx);
        return;
    }
    dungeon->turn_timers[timer_id].count -= value;
}

TbBool set_script_flag(PlayerNumber plyr_idx, int64_t flag_id, int64_t value)
{
    if ( (flag_id < 0) || (flag_id >= SCRIPT_FLAGS_COUNT) ) {
        ERRORLOG("Can't set flag; invalid flag id %" PRId64 ".",(int64_t)flag_id);
        return false;
    }
    struct Dungeon* dungeon       = get_dungeon(plyr_idx);
    if (dungeon_invalid(dungeon)) {
        ERRORLOG("Can't set flag; player %" PRId64 " has no dungeon",(int64_t)plyr_idx);
        return false;
    }
    dungeon->script_flags[flag_id] = value;
    return true;
}

TbBool mark_creature_joined_dungeon(struct Thing *creatng)
{
    if (creatng->owner == kfx_config_state.neutral_player_num) {
        // Neutral player has no dungeon
        return false;
    }
    struct Dungeon* dungeon = get_dungeon(creatng->owner);
    if (dungeon_invalid(dungeon)) {
        ERRORLOG("Can't mark; player %" PRId64 " has no dungeon",(int64_t)creatng->owner);
        return false;
    }
    if ((dungeon->owned_creatures_of_model[creatng->model] <= 1) && (dungeon->creature_models_joined[creatng->model] <= 0))
    {
        event_create_event(creatng->mappos.x.val, creatng->mappos.y.val, EvKind_NewCreature, creatng->owner, creatng->index);
    }
    if (dungeon->creature_models_joined[creatng->model] < 255)
    {
        dungeon->creature_models_joined[creatng->model]++;
    }
    return true;
}

void init_dungeon_essential_position(struct Dungeon *dungeon)
{
    struct Room* room = room_get(dungeon->room_list_start[RoK_DUNGHEART]);
    for (RoomKind rkind = 1; rkind < kfx_config_state.conf.slab_conf.room_types_count; rkind++)
    {
        if (!room_is_invalid(room))
            break;
        room = room_get(dungeon->room_list_start[rkind]);
    }
    if (room_is_invalid(room)) {
        dungeon->essential_pos.x.val = subtile_coord_center(kfx_sim_state.map_subtiles_x/2);
        dungeon->essential_pos.y.val = subtile_coord_center(kfx_sim_state.map_subtiles_y/2);
        dungeon->essential_pos.z.val = subtile_coord(0,1);
        return;
    }
    dungeon->essential_pos.x.val = subtile_coord_center(room->central_stl_x);
    dungeon->essential_pos.y.val = subtile_coord_center(room->central_stl_y);
    dungeon->essential_pos.z.val = subtile_coord(0,1);
}

void init_dungeons_essential_position(void)
{
    for (int64_t i = 0; i < DUNGEONS_COUNT; i++)
    {
        struct Dungeon* dungeon = get_dungeon(i);
        init_dungeon_essential_position(dungeon);
    }
}

const struct Coord3d *dungeon_get_essential_pos(PlayerNumber plyr_idx)
{
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    if (dungeon->dnheart_idx > 0) {
        struct Thing* heartng = thing_get(dungeon->dnheart_idx);
        if (thing_exists(heartng)) {
            return &heartng->mappos;
        }
    }
    return &dungeon->essential_pos;
}

void init_dungeons(void)
{
    for (int64_t i = 0; i < DUNGEONS_COUNT; i++)
    {
        struct Dungeon* dungeon = get_dungeon(i);
        dungeon->num_active_diggers = 0;
        dungeon->num_active_creatrs = 0;
        dungeon->creatr_list_start = 0;
        dungeon->digger_list_start = 0;
        dungeon->owner = i;
        dungeon->max_creatures_attracted = kfx_config_state.conf.rules[i].rooms.default_max_crtrs_gen_entrance;
        dungeon->dead_creatures_count = 0;
        dungeon->dead_creature_idx = 0;
        /** Player modifier default value is set to 100. */
        dungeon->modifier.health = 100;
        dungeon->modifier.strength = 100;
        dungeon->modifier.armour = 100;
        dungeon->modifier.spell_damage = 100;
        dungeon->modifier.speed = 100;
        dungeon->modifier.pay = 100;
        dungeon->modifier.training_cost = 100;
        dungeon->modifier.scavenging_cost = 100;
        dungeon->modifier.loyalty = 100;
        dungeon->color_idx = i;
        memset(dungeon->creature_models_joined, 0, CREATURE_TYPES_MAX);
    }
}

// Narrow per-kind availability/build-state accessors, used by
// player_availability.c (they were the DungeonAvailabilityCallbacks
// entries until refactor pass 2's S05 moved their callers into kfx_sim).
TbBool player_has_valid_dungeon(PlayerNumber plyr_idx)
{
    return !dungeon_invalid(get_dungeon(plyr_idx));
}

TbBool player_has_valid_dungeon_with_heart(PlayerNumber plyr_idx)
{
    return !dungeon_invalid(get_dungeon(plyr_idx)) && player_has_heart(plyr_idx);
}

TbBool players_num_dungeon_valid(PlayerNumber plyr_idx)
{
    return !dungeon_invalid(get_players_num_dungeon(plyr_idx));
}

TbBool players_num_dungeon_valid_with_heart(PlayerNumber plyr_idx)
{
    return !dungeon_invalid(get_players_num_dungeon(plyr_idx)) && player_has_heart(plyr_idx);
}

void set_creature_availability(PlayerNumber plyr_idx, ThingModel crtr_model, int64_t can_be_avail, int64_t force_avail)
{
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    dungeon->creature_allowed[crtr_model] = can_be_avail;
    dungeon->creature_force_enabled[crtr_model] = force_avail;
}

void try_set_backup_heart_idx(PlayerNumber owner, ThingIndex thing_idx)
{
    struct Dungeon* dungeon = get_dungeon(owner);
    if ((thing_idx != dungeon->dnheart_idx) && (dungeon->backup_heart_idx == 0))
    {
        dungeon->backup_heart_idx = thing_idx;
    }
}

TbBool set_room_resrchable_and_buildable(PlayerNumber plyr_idx, RoomKind rkind, int64_t resrch, int64_t avail)
{
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    dungeon->room_resrchable[rkind] = resrch;
    // This doesnt reset if player has room in the past
    if (resrch != 0)
        dungeon->room_buildable[rkind] |= (avail? 1 : 0 );
    else
        dungeon->room_buildable[rkind] &= ~1;
    return (dungeon->room_buildable[rkind] & 1) != 0;
}

TbBool get_room_resrchable(PlayerNumber plyr_idx, RoomKind rkind)
{
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    return dungeon->room_resrchable[rkind] != 0;
}

void set_all_room_resrchable(PlayerNumber plyr_idx)
{
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    for (int64_t rkind = 0; rkind < kfx_config_state.conf.slab_conf.room_types_count; rkind++)
    {
        dungeon->room_resrchable[rkind] = 1;
    }
}

TbBool get_room_buildable(PlayerNumber plyr_idx, RoomKind rkind)
{
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    return (dungeon->room_buildable[rkind] & 1) != 0;
}

void set_all_room_buildable_from_resrchable(PlayerNumber plyr_idx)
{
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    for (int64_t i = 0; i < kfx_config_state.conf.slab_conf.room_types_count; i++)
    {
        if (dungeon->room_resrchable[i])
        {
            dungeon->room_buildable[i] = 1;
        }
    }
}

TbBool get_magic_resrchable(PlayerNumber plyr_idx, PowerKind pwkind)
{
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    return dungeon->magic_resrchable[pwkind] != 0;
}

void set_magic_resrchable(PlayerNumber plyr_idx, PowerKind pwkind, TbBool resrch)
{
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    dungeon->magic_resrchable[pwkind] = resrch;
}

void set_all_magic_resrchable_unchecked(PlayerNumber plyr_idx)
{
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    for (int64_t i = 0; i < kfx_config_state.conf.magic_conf.power_types_count; i++)
    {
        dungeon->magic_resrchable[i] = 1;
    }
}

TbBool get_magic_level_gt0(PlayerNumber plyr_idx, PowerKind pwkind)
{
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    return dungeon->magic_level[pwkind] > 0;
}

// These 6 assume the caller has already validated the dungeon (and
// heart, where applicable) via players_num_dungeon_valid[_with_heart]
// -- kept as separate steps rather than baked in here so config_trapdoor.c
// can preserve its original check ordering (dungeon/heart validity
// before the tngmodel/door_idx range check) byte-for-byte.
TbBool get_trap_placeable(PlayerNumber plyr_idx, int64_t tngmodel)
{
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    return dungeon->mnfct_info.trap_amount_placeable[tngmodel] > 0;
}

TbBool get_trap_manufacturable(PlayerNumber plyr_idx, int64_t tngmodel)
{
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    return (dungeon->mnfct_info.trap_build_flags[tngmodel] & MnfBldF_Manufacturable) != 0;
}

TbBool get_trap_built(PlayerNumber plyr_idx, int64_t tngmodel)
{
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    return (dungeon->mnfct_info.trap_build_flags[tngmodel] & MnfBldF_Built) != 0;
}

TbBool get_door_placeable(PlayerNumber plyr_idx, int64_t door_idx)
{
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    return dungeon->mnfct_info.door_amount_placeable[door_idx] > 0;
}

TbBool get_door_manufacturable(PlayerNumber plyr_idx, int64_t door_idx)
{
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    return (dungeon->mnfct_info.door_build_flags[door_idx] & MnfBldF_Manufacturable) != 0;
}

TbBool get_door_built(PlayerNumber plyr_idx, int64_t door_idx)
{
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    return (dungeon->mnfct_info.door_build_flags[door_idx] & MnfBldF_Built) != 0;
}

/**
 * The dungeon's money after keeping enough back for the next payday (or a
 * digger, if that costs more). Moved from the AI's player_computer.c in
 * refactor pass 2 (S14): room_jobs.c uses it too.
 */
GoldAmount get_dungeon_money_less_cost(const struct Dungeon *dungeon)
{
    // As payday need, take amount planned for next payday
    GoldAmount money_payday = dungeon->creatures_total_pay;
    // In case payday expenses are low, require enough money to make special digger
    GoldAmount money_mkdigger = compute_power_price(dungeon->owner, PwrK_MKDIGGER, 0);
    if (money_payday < money_mkdigger)
        money_payday = money_mkdigger;
    return dungeon->total_money_owned - money_payday;
}

/******************************************************************************/
