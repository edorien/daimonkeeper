/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file player_availability.c
 *     Per-player room/power/trap/door/creature availability.
 * @par Purpose:
 *     What a player may build, cast, place or attract, and the setters
 *     level scripts and cheats use to change it. Moved from kfx_config's
 *     config_terrain.c/config_magic.c/config_trapdoor.c/config_creature.c
 *     (docs/refactor-pass2/stage-05-config-gameplay-to-sim.md): they read
 *     and write struct Dungeon/struct PlayerInfo, which kfx_sim owns, and
 *     every caller ranks at or above kfx_sim.
 * @par Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "player_availability.h"

#include <inttypes.h>
#include "globals.h"
#include "bflib_basics.h"
#include "config.h"
#include "config_creature.h"
#include "config_magic.h"
#include "config_terrain.h"
#include "config_trapdoor.h"
#include "dungeon_data.h"
#include "kfx_config_state.h"
#include "magic_powers.h"
#include "player_computer_types.h"
#include "player_data.h"
#include "thing_stats.h"
#include "ports/ui_port.h"
#include "ports/ai_port.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// Rooms (from config_terrain.c)

/**
 * Makes all rooms to be available to research for the player.
 */
TbBool make_all_rooms_researchable(PlayerNumber plyr_idx)
{
    if (!player_has_valid_dungeon(plyr_idx)) {
        ERRORDBG(11,"Cannot do; player %" PRId64 " has no dungeon",(int64_t)plyr_idx);
        return false;
    }
    set_all_room_resrchable(plyr_idx);
    return true;
}

/**
 * Sets room availability state.
 */
TbBool set_room_available(PlayerNumber plyr_idx, RoomKind rkind, int64_t resrch, int64_t avail)
{
    // note that we can't get_players_num_dungeon() because players
    // may be uninitialized yet when this is called.
    if (!player_has_valid_dungeon(plyr_idx)) {
        ERRORDBG(11,"Cannot do; player %" PRId64 " has no dungeon",(int64_t)plyr_idx);
        return false;
    }
    if (rkind >= kfx_config_state.conf.slab_conf.room_types_count)
    {
        ERRORLOG("Can't add incorrect room %" PRId64 " to player %" PRId64,(int64_t)rkind, (int64_t)plyr_idx);
        return false;
    }
    if (set_room_resrchable_and_buildable(plyr_idx, rkind, resrch, avail))
    {
        struct Computer2* comp = get_computer_player(plyr_idx);
        if (comp != NULL)
        {
            ai_reactivate_build_process(comp, rkind);
        }
    }

    return true;
}

/**
 * Returns if the room can be built by a player.
 * Checks only if it's available and if the player is 'alive'.
 * Doesn't check if the player has enough money or map position is on correct spot.
 */
TbBool is_room_available(PlayerNumber plyr_idx, RoomKind rkind)
{
    // Check if the player even have a dungeon, and has a heart to build rooms
    if (!player_has_valid_dungeon_with_heart(plyr_idx)) {
        return false;
    }
    if (rkind >= kfx_config_state.conf.slab_conf.room_types_count)
    {
      ERRORLOG("Incorrect room %" PRId64 " (player %" PRId64 ")",(int64_t)rkind, (int64_t)plyr_idx);
      return false;
    }
    if (get_room_buildable(plyr_idx, rkind)) {
        return true;
    }
    return false;
}

/**
 * Returns if the room can be or already is obtained by a player.
 */
TbBool is_room_obtainable(PlayerNumber plyr_idx, RoomKind rkind)
{
    // Check if the player even has a dungeon, and has a heart to build rooms
    if (!players_num_dungeon_valid_with_heart(plyr_idx)) {
        return false;
    }
    if (rkind >= kfx_config_state.conf.slab_conf.room_types_count) {
        ERRORLOG("Incorrect room %" PRIu64 " (player %" PRId64 ")",(uint64_t)(rkind), (int64_t)(plyr_idx));
        return false;
    }
    return get_room_buildable(plyr_idx, rkind) || get_room_resrchable(plyr_idx, rkind);
}

/**
 * Returns if a room that has role can be built by a player.
 * Checks only if it's available and if the player is 'alive'.
 * Doesn't check if the player has enough money or map position is on correct spot.
 */
RoomKind find_first_available_roomkind_with_role(PlayerNumber plyr_idx, RoomRole rrole)
{
    // Check if the player even have a dungeon, and has a heart to build rooms
    if (!player_has_valid_dungeon_with_heart(plyr_idx)) {
        return RoK_NONE;
    }

    for (RoomKind rkind = 0; rkind < kfx_config_state.conf.slab_conf.room_types_count; rkind++)
    {
        if (room_role_matches(rkind, rrole))
        {
            if (get_room_buildable(plyr_idx, rkind))
            {
                return rkind;
            }
        }
    }
    return RoK_NONE;
}

/**
 * Returns if a room that has role can be built by a player.
 * Checks only if it's available and if the player is 'alive'.
 * Doesn't check if the player has enough money or map position is on correct spot.
 */
TbBool is_room_of_role_available(PlayerNumber plyr_idx, RoomRole rrole)
{
    if (find_first_available_roomkind_with_role(plyr_idx, rrole) > RoK_NONE)
    {
        return true;
    }
    return false;
}

/**
 * Makes all the rooms, which are researchable, to be instantly available.
 */
TbBool make_available_all_researchable_rooms(PlayerNumber plyr_idx)
{
    SYNCDBG(0,"Starting");
    // Check if the player even have a dungeon
    if (!player_has_valid_dungeon(plyr_idx)) {
        ERRORDBG(11,"Cannot do; player %" PRId64 " has no dungeon",(int64_t)plyr_idx);
        return false;
    }
    set_all_room_buildable_from_resrchable(plyr_idx);
    return true;
}

/******************************************************************************/
// Keeper powers (from config_magic.c)

/**
 * Makes all keeper spells to be available to research.
 */
TbBool make_all_powers_researchable(PlayerNumber plyr_idx)
{
    set_all_magic_resrchable_unchecked(plyr_idx);
    return true;
}

/**
 * Sets power availability state.
 */
TbBool set_power_available(PlayerNumber plyr_idx, PowerKind pwkind, int64_t resrch, int64_t avail)
{
    SYNCDBG(8,"Starting for power %" PRId64 ", player %" PRId64 ", state %" PRId64 ",%" PRId64,(int64_t)pwkind,(int64_t)plyr_idx,(int64_t)(resrch),(int64_t)(avail));
    // note that we can't get_players_num_dungeon() because players
    // may be uninitialized yet when this is called.
    if (!player_has_valid_dungeon(plyr_idx)) {
        ERRORDBG(11,"Cannot set power availability; player %" PRId64 " has no dungeon",(int64_t)plyr_idx);
        return false;
    }
    set_magic_resrchable(plyr_idx, pwkind, resrch);
    if (avail <= 0)
    {
        if (is_power_available(plyr_idx, pwkind))
        {
            remove_power_from_player(pwkind, plyr_idx);
        }
        return true;
    }
    if (is_power_available(plyr_idx, pwkind))
    {
        return true;
    }
    return add_power_to_player(pwkind, plyr_idx);
}

/**
 * Returns if the power can be used by a player.
 * Checks only if it's available and if the player is 'alive'.
 * Doesn't check if the player has enough money or map position is on correct spot.
 */
TbBool is_power_available(PlayerNumber plyr_idx, PowerKind pwkind)
{
    // Check if the player even have a dungeon
    if (!players_num_dungeon_valid(plyr_idx)) {
        return false;
    }
    //TODO POWERS Mapping child powers to their parent - remove that when magic_level array is enlarged
    {
        const struct PowerConfigStats* powerst = get_power_model_stats(pwkind);
        if (powerst->parent_power != 0)
            pwkind = powerst->parent_power;
    }
    // Player must have dungeon heart to cast spells, with no heart only floating spirit spell works
    if (!player_has_heart(plyr_idx) && (pwkind != PwrK_POSSESS)) {
        return false;
    }
    if (pwkind >= kfx_config_state.conf.magic_conf.power_types_count)
    {
        ERRORLOG("Incorrect power %" PRIu64 " (player %" PRId64 ")", (uint64_t)(pwkind), (int64_t)(plyr_idx));
        return false;
    }
    if (get_magic_level_gt0(plyr_idx, pwkind)) {
        return true;
    }
    return false;
}

/**
 * Returns if the power can be or already is obtained by a player.
 */
TbBool is_power_obtainable(PlayerNumber plyr_idx, PowerKind pwkind)
{
    // Check if the player even have a dungeon
    if (!players_num_dungeon_valid(plyr_idx)) {
        return false;
    }
    //TODO POWERS Mapping child powers to their parent - remove that when magic_level array is enlarged
    {
        const struct PowerConfigStats* powerst = get_power_model_stats(pwkind);
        if (powerst->parent_power != 0)
            pwkind = powerst->parent_power;
    }
    // Player must have dungeon heart to cast spells, with no heart only floating spirit spell works
    if (!player_has_heart(plyr_idx) && (pwkind != PwrK_POSSESS)) {
        return false;
    }
    if (pwkind >= kfx_config_state.conf.magic_conf.power_types_count) {
        ERRORLOG("Incorrect power %" PRIu64 " (player %" PRId64 ")",(uint64_t)(pwkind), (int64_t)(plyr_idx));
        return false;
    }
    return get_magic_level_gt0(plyr_idx, pwkind) || get_magic_resrchable(plyr_idx, pwkind);
}

/**
 * Makes all the powers, which are researchable, to be instantly available.
 */
TbBool make_available_all_researchable_powers(PlayerNumber plyr_idx)
{
  SYNCDBG(0,"Starting");
  TbBool ret = true;
  if (!players_num_dungeon_valid(plyr_idx)) {
      ERRORDBG(11,"Cannot make research available; player %" PRId64 " has no dungeon",(int64_t)plyr_idx);
      return false;
  }
  for (int64_t i = 0; i < kfx_config_state.conf.magic_conf.power_types_count; i++)
  {
    if (get_magic_resrchable(plyr_idx, i))
    {
      ret &= add_power_to_player(i, plyr_idx);
    }
  }
  return ret;
}

/******************************************************************************/
// Traps and doors (from config_trapdoor.c)

/**
 * Returns if the trap can be placed by a player.
 * Checks only if it's available and if the player is 'alive'.
 * Doesn't check if map position is on correct spot.
 */
TbBool is_trap_placeable(PlayerNumber plyr_idx, int64_t tngmodel)
{
    // Check if the player even have a dungeon, and has a heart to place traps
    if (!players_num_dungeon_valid_with_heart(plyr_idx)) {
        return false;
    }
    if ((tngmodel <= 0) || (tngmodel >= kfx_config_state.conf.trapdoor_conf.trap_types_count)) {
        ERRORLOG("Incorrect trap %" PRId64 " (player %" PRId64 ")",(int64_t)tngmodel, (int64_t)plyr_idx);
        return false;
    }
    if (get_trap_placeable(plyr_idx, tngmodel)) {
        return true;
    }
    return false;
}

/**
 * Returns if the trap can be manufactured by a player.
 * Checks only if it's set as buildable in level script.
 * Doesn't check if player has workshop or workforce for the task.
 */
TbBool is_trap_buildable(PlayerNumber plyr_idx, int64_t tngmodel)
{
    // Check if the player even have a dungeon, and has a heart to build anything
    if (!players_num_dungeon_valid_with_heart(plyr_idx)) {
        return false;
    }
    if ((tngmodel <= 0) || (tngmodel >= kfx_config_state.conf.trapdoor_conf.trap_types_count)) {
        ERRORLOG("Incorrect trap %" PRId64 " (player %" PRId64 ")",(int64_t)tngmodel, (int64_t)plyr_idx);
        return false;
    }
    if (get_trap_manufacturable(plyr_idx, tngmodel)) {
        return true;
    }
    return false;
}

/**
 * Returns if the trap was at least once built by a player.
 */
TbBool is_trap_built(PlayerNumber plyr_idx, int64_t tngmodel)
{
    // Check if the player even have a dungeon
    if (!players_num_dungeon_valid(plyr_idx)) {
        return false;
    }
    if ((tngmodel <= 0) || (tngmodel >= kfx_config_state.conf.trapdoor_conf.trap_types_count)) {
        ERRORLOG("Incorrect trap %" PRId64 " (player %" PRId64 ")",(int64_t)tngmodel, (int64_t)plyr_idx);
        return false;
    }
    if (get_trap_built(plyr_idx, tngmodel)) {
        return true;
    }
    return false;
}

/**
 * Returns if the door can be placed by a player.
 * Checks only if it's available and if the player is 'alive'.
 * Doesn't check if map position is on correct spot.
 */
TbBool is_door_placeable(PlayerNumber plyr_idx, int64_t tngmodel)
{
    // Check if the player even have a dungeon, and has a heart to place doors
    if (!players_num_dungeon_valid_with_heart(plyr_idx)) {
        return false;
    }
    if ((tngmodel <= 0) || (tngmodel >= kfx_config_state.conf.trapdoor_conf.door_types_count)) {
        ERRORLOG("Incorrect door %" PRId64 " (player %" PRId64 ")",(int64_t)tngmodel, (int64_t)plyr_idx);
        return false;
    }
    if (get_door_placeable(plyr_idx, tngmodel)) {
        return true;
    }
    return false;
}

/**
 * Returns if the door can be manufactured by a player.
 * Checks only if it's set as buildable in level script.
 * Doesn't check if player has workshop or workforce for the task.
 */
TbBool is_door_buildable(PlayerNumber plyr_idx, int64_t door_idx)
{
    // Check if the player even have a dungeon, and has a heart to build anything
    if (!players_num_dungeon_valid_with_heart(plyr_idx)) {
        return false;
    }
    if ((door_idx <= 0) || (door_idx >= kfx_config_state.conf.trapdoor_conf.door_types_count)) {
        ERRORLOG("Incorrect door %" PRId64 " (player %" PRId64 ")",(int64_t)door_idx, (int64_t)plyr_idx);
        return false;
    }
    if (get_door_manufacturable(plyr_idx, door_idx)) {
        return true;
    }
    return false;
}

/**
 * Returns if the door was at least one built by a player.
 */
TbBool is_door_built(PlayerNumber plyr_idx, int64_t door_idx)
{
    // Check if the player even have a dungeon, and has a heart to build anything
    if (!players_num_dungeon_valid_with_heart(plyr_idx)) {
        return false;
    }
    if ((door_idx <= 0) || (door_idx >= kfx_config_state.conf.trapdoor_conf.door_types_count)) {
        ERRORLOG("Incorrect door %" PRId64 " (player %" PRId64 ")",(int64_t)door_idx, (int64_t)plyr_idx);
        return false;
    }
    if (get_door_built(plyr_idx, door_idx)) {
        return true;
    }
    return false;
}

/**
 * Makes all door types manufacturable.
 */
TbBool make_available_all_doors(PlayerNumber plyr_idx)
{
  SYNCDBG(0,"Starting");
  if (!players_num_dungeon_valid(plyr_idx)) {
      ERRORDBG(11,"Cannot make doors available; player %" PRId64 " has no dungeon",(int64_t)plyr_idx);
      return false;
  }
  for (int64_t i = 1; i < kfx_config_state.conf.trapdoor_conf.door_types_count; i++)
  {
    if (!set_door_buildable_and_add_to_amount(plyr_idx, i, 1, 0))
    {
        ERRORLOG("Could not make door %s available for player %" PRId64, door_code_name(i), (int64_t)(plyr_idx));
        return false;
    }
  }
  return true;
}

/**
 * Makes all trap types manufacturable.
 */
TbBool make_available_all_traps(PlayerNumber plyr_idx)
{
  SYNCDBG(0,"Starting");
  if (!players_num_dungeon_valid(plyr_idx)) {
      ERRORDBG(11,"Cannot make traps available; player %" PRId64 " has no dungeon",(int64_t)plyr_idx);
      return false;
  }
  for (int64_t i = 1; i < kfx_config_state.conf.trapdoor_conf.trap_types_count; i++)
  {
    if (!set_trap_buildable_and_add_to_amount(plyr_idx, i, 1, 0))
    {
        ERRORLOG("Could not make trap %s available for player %" PRId64, trap_code_name(i), (int64_t)(plyr_idx));
        return false;
    }
  }
  return true;
}

/******************************************************************************/
// Creatures (from config_creature.c)

/**
 * Sets creature availability state.
 */
TbBool set_creature_available(PlayerNumber plyr_idx, ThingModel crtr_model, int64_t can_be_avail, int64_t force_avail)
{
    // note that we can't get_players_num_dungeon() because players
    // may be uninitialized yet when this is called.
    if (!player_has_valid_dungeon(plyr_idx)) {
        ERRORDBG(11,"Cannot set %s availability; player %" PRId64 " has no dungeon.",thing_class_and_model_name(TCls_Creature, crtr_model),(int64_t)plyr_idx);
        return false;
    }
    if ((crtr_model < 1) || (crtr_model >= kfx_config_state.conf.crtr_conf.model_count)) {
        ERRORDBG(4,"Cannot set creature availability; player %" PRId64 ", invalid model %" PRId64 ".",(int64_t)plyr_idx,(int64_t)crtr_model);
        return false;
    }
    if (force_avail < 0)
        force_avail = 0;
    if (force_avail >= CREATURES_COUNT)
        force_avail = CREATURES_COUNT-1;
    SYNCDBG(7,"Setting %s availability for player %" PRId64 " to allowed=%" PRId64 ", forced=%" PRId64 ".",thing_class_and_model_name(TCls_Creature, crtr_model),(int64_t)plyr_idx,(int64_t)can_be_avail,(int64_t)force_avail);
    set_creature_availability(plyr_idx, crtr_model, can_be_avail, force_avail);
    return true;
}

void update_players_special_digger_model(PlayerNumber plyr_idx, ThingModel new_dig_model)
{

    ThingModel old_dig_model = get_players_special_digger_model(plyr_idx);
    if (old_dig_model == new_dig_model)
    {
        return;
    }
    get_player(plyr_idx)->special_digger = new_dig_model;

    if (plyr_idx == my_player_number)
    {
        for (size_t i = 0; i < CREATURE_TYPES_MAX; i++)
        {
            if (breed_activities[i] == old_dig_model)
                breed_activities[i] = new_dig_model;
            else if (breed_activities[i] == new_dig_model)
                breed_activities[i] = old_dig_model;
        }
        ui_update_creatr_model_activities_list(1);
    }


}

ThingModel get_players_special_digger_model(PlayerNumber plyr_idx)
{
    ThingModel current_digger = get_player(plyr_idx)->special_digger;

    if(current_digger != 0)
        return current_digger;

    ThingModel crmodel;

    if (player_is_roaming(plyr_idx))
    {
        crmodel = kfx_config_state.conf.crtr_conf.special_digger_good;
        if (crmodel == 0)
        {
            WARNLOG("Heroes (player %" PRId64 ") have no digger breed!",(int64_t)plyr_idx);
            crmodel = kfx_config_state.conf.crtr_conf.special_digger_evil;
        }
    } else
    {
        crmodel = kfx_config_state.conf.crtr_conf.special_digger_evil;
        if (crmodel == 0)
        {
            WARNLOG("Keepers have no digger breed!");
            crmodel = kfx_config_state.conf.crtr_conf.special_digger_good;
        }
    }
    return crmodel;
}

ThingModel get_players_spectator_model(PlayerNumber plyr_idx)
{
    ThingModel breed = kfx_config_state.conf.crtr_conf.spectator_breed;
    if (breed == 0)
    {
        WARNLOG("There is no spectator breed for player %" PRId64 "!",(int64_t)plyr_idx);
        breed = kfx_config_state.conf.crtr_conf.special_digger_good;
    }
    return breed;
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
