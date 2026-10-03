/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lvl_script.c
 *     Level script commands support.
 * @par Purpose:
 *     Load, recognize and maintain the level script.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     12 Feb 2009 - 11 Apr 2014
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx_game_state.h"
#include "lvl_script_lib.h"
#include "config_keeperfx.h"

#include "bflib_sound.h"
#include "config.h"
#include "creature_states_pray.h"
#include "room_entrance.h"
#include "lvl_filesdk1.h"
#include "magic_powers.h"
#include "map_blocks.h"
#include "map_data.h"
#include "map_locations.h"
#include "player_data.h"
#include "player_utils.h"
#include "power_hand.h"
#include "power_specials.h"
#include "room_library.h"
#include "room_util.h"
#include "thing_data.h"
#include "thing_list.h"
#include "player_availability.h"

#include "ports/ui_port.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/

extern const struct CommandDesc command_desc[];
extern const struct CommandDesc dk1_command_desc[];


/******************************************************************************/

/**
 * Kills a creature which meets given criteria.
 * @param plyr_idx The player whose creature will be affected.
 * @param crmodel Model of the creature to find.
 * @param criteria Criteria, from CreatureSelectCriteria enumeration.
 * @return True if a creature was found and killed.
 */
TbBool script_kill_creature_with_criteria(PlayerNumber plyr_idx, int64_t crmodel, int64_t criteria)
{
    struct Thing *thing = script_get_creature_by_criteria(plyr_idx, crmodel, criteria);
    if (thing_is_invalid(thing)) {
        SYNCDBG(5,"No matching player %" PRId64 " creature of model %" PRId64 " (%s) found to kill",(int64_t)plyr_idx,(int64_t)crmodel, creature_code_name(crmodel));
        return false;
    }
    kill_creature(thing, INVALID_THING, -1, CrDed_NoUnconscious);
    return true;
}
/**
 * Changes owner of a creature which meets given criteria.
 * @param origin_plyr_idx The player whose creature will be affected.
 * @param dest_plyr_idx The player who will receive the creature.
 * @param crmodel Model of the creature to find.
 * @param criteria Criteria, from CreatureSelectCriteria enumeration.
 * @return True if a creature was found and changed owner.
 */
TbBool script_change_creature_owner_with_criteria(PlayerNumber origin_plyr_idx, int64_t crmodel, int64_t criteria, PlayerNumber dest_plyr_idx)
{
    struct Thing *thing = script_get_creature_by_criteria(origin_plyr_idx, crmodel, criteria);
    if (thing_is_invalid(thing)) {
        SYNCDBG(5,"No matching player %" PRId64 " creature of model %" PRId64 " (%s) found to kill",(int64_t)origin_plyr_idx,(int64_t)crmodel, creature_code_name(crmodel));
        return false;
    }
    if (is_thing_some_way_controlled(thing))
    {
        //does not kill the creature, but does the preparations needed for when it is possessed
        prepare_to_controlled_creature_death(thing);
    }
    change_creature_owner(thing, dest_plyr_idx);
    return true;
}

void script_kill_creatures(PlayerNumber plyr_idx, int64_t crmodel, int64_t criteria, int64_t copies_num)
{
    SYNCDBG(3,"Killing %" PRId64 " of %s owned by player %" PRId64 ".",(int64_t)copies_num,creature_code_name(crmodel),(int64_t)plyr_idx);
    for (int64_t i = 0; i < copies_num; i++)
    {
        script_kill_creature_with_criteria(plyr_idx, crmodel, criteria);
    }
}

/**
 * Increase level of  a creature which meets given criteria.
 * @param plyr_idx The player whose creature will be affected.
 * @param crmodel Model of the creature to find.
 * @param criteria Criteria, from CreatureSelectCriteria enumeration.
 * @return True if a creature was found and leveled.
 */
TbBool script_level_up_creature(PlayerNumber plyr_idx, int64_t crmodel, int64_t criteria, int64_t count)
{
    struct Thing *thing = script_get_creature_by_criteria(plyr_idx, crmodel, criteria);
    if (thing_is_invalid(thing)) {
        SYNCDBG(5,"No matching player %" PRId64 " creature of model %" PRId64 " (%s) found to level up",(int64_t)plyr_idx,(int64_t)crmodel, creature_code_name(crmodel));
        return false;
    }
    creature_change_multiple_levels(thing,count);
    return true;
}

/**
 * Processes given VALUE immediately.
 * This processes given script command. It is used to process VALUEs at start when they have
 * no conditions, or during the gameplay when conditions are met.
 */
void script_process_value(uint64_t var_index, uint64_t plr_range_id, struct ScriptValue *value)
{
  int64_t plr_start;
  int64_t plr_end;
  if (get_players_range(plr_range_id, &plr_start, &plr_end) < 0)
  {
      WARNLOG("Invalid player range %" PRId64 " in VALUE command %" PRId64 ".",(int64_t)plr_range_id,(int64_t)var_index);
      return;
  }
  const struct CommandDesc *desc;
  for (desc = command_desc; desc->textptr != NULL; desc++)
      if (desc->index == var_index)
          break;
  if (desc->process_fn == NULL)
  {
      WARNMSG("Unsupported Game VALUE, command %" PRIu64 ".",(uint64_t)(var_index));
      return;
  }
  // the command's process, once for each player in the range
  struct ScriptContext context;
  for (int64_t i = plr_start; i < plr_end; i++)
  {
      context.player_idx = i;
      context.value = value;
      desc->process_fn(&context);
  }
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
