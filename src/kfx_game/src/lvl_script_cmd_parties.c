/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lvl_script_cmd_parties.c
 *     Level script commands: parties and tunnellers.
 * @par Purpose:
 *     Their check and process functions; lvl_script_commands.c has the command tables that name them
 *     (refactor pass 5, S09: split out of it by command family).
 * @author   KeeperFX Team
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "lvl_script_conditions.h"
#include "lvl_script_lib.h"
#include "lvl_script_commands_private.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

void add_to_party_check(const struct ScriptLine *scline)
{
    int64_t party_id = get_party_index_of_name(scline->tp[0]);
    if (party_id < 0)
    {
        SCRPTERRLOG("Invalid Party:%s",scline->tp[0]);
        return;
    }
    if ((scline->np[2] < 1) || (scline->np[2] > CREATURE_MAX_LEVEL))
    {
      SCRPTERRLOG("Invalid Creature Level parameter; %" PRId64 " not in range (%" PRId64 ",%" PRId64 ")",(int64_t)(scline->np[2]),(int64_t)(1),(int64_t)(CREATURE_MAX_LEVEL));
      return;
    }
    int64_t crtr_id = get_rid(creature_desc, scline->tp[1]);
    if (crtr_id == -1)
    {
      SCRPTERRLOG("Unknown creature, '%s'", scline->tp[1]);
      return;
    }
    PlayerNumber target = -1;
    int64_t objective_id = get_objective_id_with_potential_target(scline->tp[4], &target);
    if (objective_id == -1)
    {
      SCRPTERRLOG("Unknown party member objective, '%s'", scline->tp[4]);
      return;
    }
  //SCRPTLOG("Party '%s' member kind %d, level %d",prtname,(int64_t)(crtr_id),(int64_t)(exp_level));

    if ((get_script_current_condition() == CONDITION_ALWAYS) && (next_command_reusable == 0))
    {
        add_member_to_party(party_id, crtr_id, scline->np[2], scline->np[3], objective_id, scline->np[5], target);
    } else
    {
        if (kfx_game_state.script.party_triggers_num < PARTY_TRIGGERS_COUNT)
        {
            struct PartyTrigger* pr_trig = &kfx_game_state.script.party_triggers[kfx_game_state.script.party_triggers_num];
            pr_trig->flags = TrgF_ADD_TO_PARTY;
            pr_trig->flags |= next_command_reusable ? TrgF_REUSABLE : 0;
            pr_trig->party_id = party_id;
            pr_trig->creatr_id = crtr_id;
            pr_trig->exp_level = scline->np[2];
            pr_trig->carried_gold = scline->np[3];
            pr_trig->objectv = objective_id;
            pr_trig->countdown = scline->np[5];
            pr_trig->condit_idx = get_script_current_condition();
            pr_trig->target = target;
        }
        else
        {
            SCRPTERRLOG("Max party triggers reached, failed to update %s with %s", scline->tp[0], scline->tp[1]);
        }
        kfx_game_state.script.party_triggers_num++;
    }
}

void delete_from_party_check(const struct ScriptLine *scline)
{
    int64_t party_id = get_party_index_of_name(scline->tp[0]);
    if (party_id < 0)
    {
        SCRPTERRLOG("Invalid Party:%s",scline->tp[0]);
        return;
    }
    int64_t creature_id = get_rid(creature_desc, scline->tp[1]);
    if (creature_id == -1)
    {
      SCRPTERRLOG("Unknown creature, '%s'", scline->tp[1]);
      return;
    }
    if ((get_script_current_condition() == CONDITION_ALWAYS) && (next_command_reusable == 0))
    {
        delete_member_from_party(party_id, creature_id, scline->np[2]);
    } else
    {
        if (kfx_game_state.script.party_triggers_num < PARTY_TRIGGERS_COUNT)
        {
            struct PartyTrigger* pr_trig = &kfx_game_state.script.party_triggers[kfx_game_state.script.party_triggers_num];
            pr_trig->flags = TrgF_DELETE_FROM_PARTY;
            pr_trig->flags |= next_command_reusable ? TrgF_REUSABLE : 0;
            pr_trig->party_id = party_id;
            pr_trig->creatr_id = creature_id;
            pr_trig->exp_level = scline->np[2];
            pr_trig->condit_idx = get_script_current_condition();
        }
        else
        {
            SCRPTERRLOG("Max party triggers reached, failed to update %s with %s", scline->tp[0], scline->tp[1]);
        }
        kfx_game_state.script.party_triggers_num++;
    }
}

void create_party_check(const struct ScriptLine *scline)
{
    const char *prtname = scline->tp[0];
    if (get_script_current_condition() != CONDITION_ALWAYS)
    {
        SCRPTWRNLOG("Party '%s' defined inside conditional statement",prtname);
    }
    create_party(prtname);
}

void add_party_to_level_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    const char *prtname = scline->tp[1];
    const char *locname = scline->tp[2];
    int64_t ncopies = scline->np[3];
    TbMapLocation location;
    if (ncopies < 1)
    {
        SCRPTERRLOG("Invalid NUMBER parameter");
        return;
    }
    if (kfx_game_state.script.party_triggers_num >= PARTY_TRIGGERS_COUNT)
    {
        SCRPTERRLOG("Too many ADD_CREATURE commands in script");
        return;
    }
    // Verify player
    int64_t plr_id = get_players_range_single(plr_range_id);
    if (plr_id < 0) {
        SCRPTERRLOG("Given owning player is not supported in this command");
        return;
    }
    // Recognize place where party is created
    if (!get_map_location_id(locname, &location))
        return;
    // Recognize party name
    int64_t prty_id = get_party_index_of_name(prtname);
    if (prty_id < 0)
    {
        SCRPTERRLOG("Party of requested name, '%s', is not defined",prtname);
        return;
    }
    if ((get_script_current_condition() == CONDITION_ALWAYS) && (next_command_reusable == 0))
    {
        struct Party* party = &kfx_game_state.script.creature_partys[prty_id];
        script_process_new_party(party, plr_id, location, ncopies);
    } else
    {
        if (kfx_game_state.script.party_triggers_num < PARTY_TRIGGERS_COUNT)
        {
            struct PartyTrigger* pr_trig = &kfx_game_state.script.party_triggers[kfx_game_state.script.party_triggers_num];
            pr_trig->flags = TrgF_CREATE_PARTY;
            pr_trig->flags |= next_command_reusable ? TrgF_REUSABLE : 0;
            pr_trig->plyr_idx = plr_id;
            pr_trig->creatr_id = prty_id;
            pr_trig->location = location;
            pr_trig->ncopies = ncopies;
            pr_trig->condit_idx = get_script_current_condition();
        }
        else
        {
            SCRPTERRLOG("Max party triggers reached, failed to add party %s", prtname);
        }
        kfx_game_state.script.party_triggers_num++;
    }
}

/** ADD_TUNNELLER_TO_LEVEL's and ADD_TUNNELLER_PARTY_TO_LEVEL's common arguments: the level, room for one more
 *  trigger, one player, where the tunneller appears and where it heads; false (after the error) if one is wrong. */
static TbBool tunneller_check(int64_t plr_range_id, const char *locname, const char *objectv, int64_t target,
    CrtrExpLevel exp_level, int64_t *plr_id, TbMapLocation *location, TbMapLocation *heading)
{
    if ((exp_level < 1) || (exp_level > CREATURE_MAX_LEVEL))
    {
        SCRPTERRLOG("Invalid CREATURE LEVEL parameter");
        return false;
    }
    if (kfx_game_state.script.tunneller_triggers_num >= TUNNELLER_TRIGGERS_COUNT)
    {
        SCRPTERRLOG("Too many ADD_TUNNELLER commands in script");
        return false;
    }
    // Verify player
    *plr_id = get_players_range_single(plr_range_id);
    if (*plr_id < 0) {
        SCRPTERRLOG("Given owning player is not supported in this command");
        return false;
    }
    // Recognize place where party is created
    if (!get_map_location_id(locname, location))
        return false;
    // Recognize place where party is going
    if (!get_map_heading_id(objectv, target, heading))
        return false;
    return true;
}

/** A tunneller (party_id 0) or a tunneller party (its index + 1) to send when the current condition is met. */
static void tunneller_add_trigger(int64_t plr_id, TbMapLocation location, TbMapLocation heading, CrtrExpLevel exp_level,
    uint64_t carried_gold, int64_t party_id)
{
    struct TunnellerTrigger* tn_trig = &kfx_game_state.script.tunneller_triggers[kfx_game_state.script.tunneller_triggers_num % TUNNELLER_TRIGGERS_COUNT];
    set_flag_value(tn_trig->flags, TrgF_REUSABLE, next_command_reusable);
    clear_flag(tn_trig->flags, TrgF_DISABLED);
    tn_trig->plyr_idx = plr_id;
    tn_trig->location = location;
    tn_trig->heading = heading;
    tn_trig->carried_gold = carried_gold;
    tn_trig->exp_level = exp_level-1;
    tn_trig->party_id = party_id;
    tn_trig->condit_idx = get_script_current_condition();
    kfx_game_state.script.tunneller_triggers_num++;
}

void add_tunneller_to_level_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    const char *locname = scline->tp[1];
    const char *objectv = scline->tp[2];
    int64_t target = scline->np[3];
    CrtrExpLevel exp_level = scline->np[4];
    uint64_t carried_gold = scline->np[5];
    TbMapLocation location;
    TbMapLocation heading;
    int64_t plr_id;
    if (!tunneller_check(plr_range_id, locname, objectv, target, exp_level, &plr_id, &location, &heading))
        return;
    if (get_script_current_condition() == CONDITION_ALWAYS)
    {
        script_process_new_tunneler(plr_id, location, heading, exp_level-1, carried_gold);
    } else
    {
        tunneller_add_trigger(plr_id, location, heading, exp_level, carried_gold, 0);
    }
}

void add_tunneller_party_to_level_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    const char *prtname = scline->tp[1];
    const char *locname = scline->tp[2];
    const char *objectv = scline->tp[3];
    int64_t target = scline->np[4];
    CrtrExpLevel exp_level = scline->np[5];
    uint64_t carried_gold = scline->np[6];
    TbMapLocation location;
    TbMapLocation heading;
    int64_t plr_id;
    if (!tunneller_check(plr_range_id, locname, objectv, target, exp_level, &plr_id, &location, &heading))
        return;
    // Recognize party name
    int64_t prty_id = get_party_index_of_name(prtname);
    if (prty_id < 0)
    {
        SCRPTERRLOG("Party of requested name, '%s', is not defined", prtname);
        return;
    }
    struct Party* party = &kfx_game_state.script.creature_partys[prty_id];
    if (party->members_num >= GROUP_MEMBERS_COUNT-1)
    {
        SCRPTERRLOG("Party too big for ADD_TUNNELLER (Max %" PRId64 " members)", (int64_t)(GROUP_MEMBERS_COUNT-1));
        return;
    }
    // Either add the party or add item to conditional triggers list
    if (get_script_current_condition() == CONDITION_ALWAYS)
    {
        script_process_new_tunneller_party(plr_id, prty_id, location, heading, exp_level-1, carried_gold);
    } else
    {
        tunneller_add_trigger(plr_id, location, heading, exp_level, carried_gold, prty_id+1);
    }
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
