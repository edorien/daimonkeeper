/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lvl_script_cmd_creatures.c
 *     Level script commands: creatures (adding, killing, changing, moving, the creature pool).
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
#include "config_keeperfx.h"
#include "game_saves.h"
#include "lvl_script_conditions.h"
#include "lvl_script_lib.h"
#include "player_utils.h"
#include "power_specials.h"
#include "lvl_script_value.h"
#include "room_entrance.h"
#include "thing_effects.h"
#include "player_availability.h"
#include "list_walk.h"
#include "lvl_script_commands_private.h"
#include "lvl_script_commands_private.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

const struct NamedCommand tendency_desc[] = {
  {"IMPRISON",         1},
  {"FLEE",             2},
  {NULL,               0},
};

/**
 * Transfers creatures for a player
 * @param plyr_idx target player
 * @param crmodel the creature model to transfer
 * @param criteria the creature selection criterion
 * @param count the amount of units to transfer
 */
static int64_t script_transfer_creature(PlayerNumber plyr_idx, ThingModel crmodel, int64_t criteria, int64_t count)
{
    int64_t transferred = 0;
    struct Thing* thing;
    struct Dungeon* dungeon;
    struct CreatureControl* cctrl;
    for (int64_t i = 0; i < count; i++)
    {
        thing = script_get_creature_by_criteria(plyr_idx, crmodel, criteria);
        cctrl = creature_control_get_from_thing(thing);
        if ((!thing_exists(thing)) && (i == 0))
        {
            SYNCDBG(5, "No matching player %" PRId64 " creature of model %" PRId64 " found to transfer.", (int64_t)plyr_idx, (int64_t)crmodel);
            break;
        }

        if (add_transfered_creature(plyr_idx, thing->model, cctrl->exp_level, creature_kept_name(thing)))
        {
            transferred++;
            dungeon = get_dungeon(plyr_idx);
            dungeon->creatures_transferred++;
            remove_thing_from_power_hand_list(thing, plyr_idx);
            struct SpecialConfigStats* specst = get_special_model_stats(SpcKind_TrnsfrCrtr);
            create_used_effect_or_element(&thing->mappos, specst->effect_id, plyr_idx, thing->index);
            kill_creature(thing, INVALID_THING, -1, CrDed_NoEffects | CrDed_NotReallyDying);
        }
    }
    return transferred;
}

void script_transfer_creature_check(const struct ScriptLine* scline)
{
    int64_t crtr_id = parse_creature_name(scline->tp[1]);
    int64_t count = scline->np[3];
    if (crtr_id == CREATURE_NONE)
    {
        SCRPTERRLOG("Unknown creature, '%s'", scline->tp[1]);
        return;
    }
    int64_t select_id = parse_criteria(scline->tp[2]);
    if (select_id == -1) {
        SCRPTERRLOG("Unknown select criteria, '%s'", scline->tp[2]);
        return;
    }
    if (scline->np[3] == '\0')
    {
        count = 1;
    }
    if (count == 0)
    {
        SCRPTERRLOG("Transferring 0 creatures of type '%s'", scline->tp[1]);
    }
    if (count > 255)
    {
        SCRPTWRNLOG("Trying to transfer %" PRId64 " creatures out of a possible 255",(int64_t)(count));
        count = 255;
    }
    command_add_value(Cmd_TRANSFER_CREATURE, scline->np[0], crtr_id, select_id, count);
}

void script_transfer_creature_process(struct ScriptContext* context)
{
    script_transfer_creature(context->player_idx, context->value->longs[0], context->value->longs[1], context->value->longs[2]);
}

void change_creatures_annoyance_check(const struct ScriptLine* scline)
{
    int64_t crtr_id = parse_creature_name(scline->tp[1]);
    if (crtr_id == CREATURE_NONE)
    {
        SCRPTERRLOG("Unknown creature, '%s'", scline->tp[1]);
        return;
    }
    int64_t op_id = get_rid(script_operator_desc, scline->tp[2]);
    if (op_id == -1)
    {
        SCRPTERRLOG("Invalid operation for changing creatures' annoyance: '%s'", scline->tp[2]);
        return;
    }
    command_add_value(Cmd_CHANGE_CREATURES_ANNOYANCE, scline->np[0], crtr_id, op_id, scline->np[3]);
}

void change_creatures_annoyance_process(struct ScriptContext* context)
{
    script_change_creatures_annoyance(context->player_idx, context->value->longs[0], context->value->longs[1], context->value->longs[2]);
}

void move_creature_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);

    int64_t crmodel = parse_creature_name(scline->tp[1]);
    if (crmodel == CREATURE_NONE)
    {
        SCRPTERRLOG("Unknown creature, '%s'", scline->tp[1]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    int64_t select_id = parse_criteria(scline->tp[2]);
    if (select_id == -1) {
        SCRPTERRLOG("Unknown select criteria, '%s'", scline->tp[2]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    int64_t count = scline->np[3];
    if (count <= 0)
    {
        SCRPTERRLOG("Bad creatures count, %" PRId64, (int64_t)(count));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    TbMapLocation location;
    if (!get_map_location_id(scline->tp[4], &location))
    {
        SCRPTWRNLOG("Invalid location: %s", scline->tp[4]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    // An effect or an effect element (effect_or_effect_element_id(): elements are negative), 0 for none; no
    // effect named means the puff of the script's player's colour (upstream #5425).
    const char *effect_name = scline->tp[5];
    int64_t effct_id = 0;
    if (scline->tp[5][0] != '\0')
    {
        if (parameter_is_number(effect_name) && atoi(effect_name) == 0)
        {
            effct_id = 0;
        }
        else
        {
            effct_id = effect_or_effect_element_id(effect_name);
            if (effct_id == 0)
            {
                SCRPTERRLOG("Unrecognised effect: %s", effect_name);
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
        }
    }
    else
    {
        const int64_t plyr_idx = scline->np[0];
        effct_id = ((plyr_idx >= 0) && (plyr_idx < BALL_PUFF_EFFECTS_COUNT))
            ? ball_puff_effects[plyr_idx] : 0;
    }

    value->ulongs[0] = location;
    value->longs[1] = select_id;
    value->shorts[5] = effct_id;
    value->bytes[12] = count;
    value->bytes[13] = crmodel;

    PROCESS_SCRIPT_VALUE(scline->command);
}

void count_creatures_at_action_point_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);

    PlayerNumber player_id = scline->np[1];
    int64_t crmodel = parse_creature_name(scline->tp[2]);
    if (crmodel == CREATURE_NONE)
    {
        SCRPTERRLOG("Unknown creature, '%s'", scline->tp[2]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    int64_t ap_num = scline->np[0];
    char flag_player_id = scline->np[3];
    const char *flag_name = scline->tp[4];

    int64_t flag_id, flag_type;
    if (!script_parse_get_varib(flag_name, &flag_id, &flag_type, kfx_game_state.level_file_version))
    {
        SCRPTERRLOG("Unknown flag, '%s'", flag_name);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    value->shorts[0] = ap_num;
    value->bytes[2] = crmodel;
    value->chars[3] = flag_player_id;
    value->shorts[2] = flag_id;
    value->chars[6] = flag_type;
    value->longs[3] = player_id;

    PROCESS_SCRIPT_VALUE(scline->command);
}

void move_creature_process(struct ScriptContext* context)
{
    TbMapLocation location = context->value->ulongs[0];
    int64_t select_id = context->value->longs[1];
    EffectOrEffElModel effect_id = context->value->shorts[5];
    int64_t count = context->value->bytes[12];
    int64_t crmodel = context->value->bytes[13];
    PlayerNumber plyr_idx = context->player_idx;

    script_move_creature_with_criteria(plyr_idx, crmodel, select_id, location, effect_id, count);
}

void count_creatures_at_action_point_process(struct ScriptContext* context)
{
    int64_t ap_num = context->value->shorts[0];
    int64_t crmodel = context->value->bytes[2];
    int64_t flag_player_id = context->value->chars[3];
    int64_t flag_id = context->value->shorts[2];
    int64_t flag_type = context->value->chars[6];
    PlayerNumber player_id = context->value->longs[3];

    int64_t sum = 0;

    if (player_id == ALL_PLAYERS)
    {
        for (int64_t i = 0; i < PLAYERS_COUNT; i++)
        {
            sum += count_player_creatures_of_model_in_action_point(i, crmodel, action_point_number_to_index(ap_num));
        }
    }
    else
    {
        sum = count_player_creatures_of_model_in_action_point(player_id, crmodel, action_point_number_to_index(ap_num));
    }

    set_variable(flag_player_id, flag_type, flag_id, sum);
}

void level_up_players_creatures_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    int64_t crmodel = parse_creature_name(scline->tp[1]);
    char count = scline->np[2];

    if (crmodel == CREATURE_NONE)
    {
        SCRPTERRLOG("Unknown creature, '%s'", scline->tp[1]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    if (scline->np[2] == '\0')
    {
        count = 1;
    }
    if (count == 0)
    {
        SCRPTERRLOG("Trying to level up %" PRId64 " times", (int64_t)(scline->np[2]));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    value->shorts[1] = crmodel;
    value->shorts[2] = count;
    PROCESS_SCRIPT_VALUE(scline->command);
}

void level_up_players_creatures_process(struct ScriptContext* context)
{
    int64_t crmodel = context->value->shorts[1];
    int64_t count = context->value->shorts[2];
    PlayerNumber plyridx = context->player_idx;
    struct Dungeon* dungeon = get_players_num_dungeon(plyridx);
    FOR_EACH_THING(thing, thing_walk_players_creatures_of_model(dungeon, dungeon->owner, crmodel))
    {
        if (creature_matches_model(thing, crmodel))
        {
            creature_change_multiple_levels(thing, count);
        }
    }
    SYNCDBG(19, "Finished");
}

void swap_creature_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    ThingModel ncrt_id = scline->np[0];
    ThingModel crtr_id = scline->np[1];

    value->shorts[0] = ncrt_id;
    value->shorts[1] = crtr_id;
    PROCESS_SCRIPT_VALUE(scline->command);
}

void swap_creature_process(struct ScriptContext* context)
{
    ThingModel ncrt_id = context->value->shorts[0];
    ThingModel crtr_id = context->value->shorts[1];

    if (!swap_creature(ncrt_id, crtr_id))
    {
        SCRPTERRLOG("Error swapping creatures '%s'<->'%s'", creature_code_name(ncrt_id), creature_code_name(crtr_id));
    }
}

void set_generate_speed_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    if (scline->tp[1][0] == '\0')
    {
        if (scline->np[0] <= 0)
        {
            SCRPTERRLOG("Generation speed must be positive number");
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
        value->chars[2] = ALL_PLAYERS;
    }
    else
    {
        if (scline->np[0] < 0)
        {
            SCRPTERRLOG("Generation speed must be positive number");
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
        value->chars[2] = get_id(player_desc, scline->tp[1]);
        if (value->chars[2] == -1)
        {
            SCRPTERRLOG("Invalid player: %" PRId64, (int64_t)(value->chars[2]));
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }
    value->ushorts[0] = saturate_set_unsigned(scline->np[0], 16);
    PROCESS_SCRIPT_VALUE(scline->command);
}

void set_generate_speed_process(struct ScriptContext* context)
{
    struct PlayerInfo* player;
    switch (context->value->chars[2])
    {
        case ALL_PLAYERS:
        {
            for (PlayerNumber plyr_idx = 0; plyr_idx < PLAYERS_COUNT; plyr_idx++)
            {
                player = get_player(plyr_idx);
                if (!player_invalid(player))
                {
                    player->generate_speed = context->value->ushorts[0];
                }
            }
            break;
        }
        default:
        {
            player = get_player(context->value->chars[2]);
            if (!player_invalid(player))
            {
                player->generate_speed = context->value->ushorts[0];
            }
            break;
        }
    }
    update_dungeon_generation_speeds();
}

/** CREATURE_AVAILABLE: level files before version 1 gave only the forced availability (their third value). */
void creature_available_check(const struct ScriptLine *scline)
{
    int64_t crtr_id = get_rid(creature_desc, scline->tp[1]);
    if (crtr_id == -1)
    {
      SCRPTERRLOG("Unknown creature, '%s'", scline->tp[1]);
      return;
    }
    if (scline->file_version > 0)
        command_add_value(scline->command, scline->np[0], crtr_id, scline->np[2], scline->np[3]);
    else
        command_add_value(scline->command, scline->np[0], crtr_id, scline->np[3], 0);
}

void creature_available_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    if (!set_creature_available(context->player_idx, value->longs[0], value->longs[1], value->longs[2])) {
        WARNLOG("Setting creature %s availability for player %" PRId64 " failed.",creature_code_name(value->longs[0]),(int64_t)context->player_idx);
    }
}

void add_creature_to_pool_check(const struct ScriptLine *scline)
{
    const char *crtr_name = scline->tp[0];
    int64_t amount = scline->np[1];
    int64_t crtr_id = get_rid(creature_desc, crtr_name);
    if (crtr_id == -1)
    {
        SCRPTERRLOG("Unknown creature, '%s'", crtr_name);
        return;
    }
    if ((amount <= -CREATURES_COUNT) || (amount >= CREATURES_COUNT))
    {
        SCRPTERRLOG("Invalid number of '%s' creatures for pool, %" PRId64, crtr_name, (int64_t)(amount));
        return;
    }
    command_add_value(scline->command, ALL_PLAYERS, crtr_id, amount, 0);
}

void add_creature_to_pool_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    add_creature_to_pool(context->value->longs[0], context->value->longs[1]);
}

void kill_creature_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    const char *crtr_name = scline->tp[1];
    const char *criteria = scline->tp[2];
    int64_t count = scline->np[3];
    SCRIPTDBG(11, "Starting");
    if (count <= 0)
    {
        SCRPTERRLOG("Bad creatures count, %" PRId64, (int64_t)(count));
        return;
  }
  int64_t crtr_id = parse_creature_name(crtr_name);
  if (crtr_id == CREATURE_NONE) {
    SCRPTERRLOG("Unknown creature, '%s'", crtr_name);
    return;
  }
  int64_t select_id = parse_criteria(criteria);
  if (select_id == -1)
  {
    SCRPTERRLOG("Unknown select criteria, '%s'", criteria);
    return;
  }
  command_add_value(scline->command, plr_range_id, crtr_id, select_id, count);
}

void level_up_creature_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    const char *crtr_name = scline->tp[1];
    const char *criteria = scline->tp[2];
    int64_t count = scline->np[3];
    SCRIPTDBG(11, "Starting");
    if (count == 0)
    {
        SCRPTERRLOG("Bad count, %" PRId64, (int64_t)(count));
        return;
    }
    ThingModel crtr_id = parse_creature_name(crtr_name);
    if (crtr_id == CREATURE_NONE)
    {
        SCRPTERRLOG("Unknown creature, '%s'", crtr_name);
        return;
    }
    int64_t select_id = parse_criteria(criteria);
    if (select_id == -1)
    {
        SCRPTERRLOG("Unknown select criteria, '%s'", criteria);
        return;
    }
    command_add_value(scline->command, plr_range_id, crtr_id, select_id, count);
}

void kill_creature_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    script_kill_creatures(context->player_idx, value->longs[0], value->longs[1], value->longs[2]);
}

void level_up_creature_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    script_level_up_creature(context->player_idx, value->longs[0], value->longs[1], value->longs[2]);
}

void max_creatures_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    int64_t val = scline->np[1];
    command_add_value(scline->command, plr_range_id, val, 0, 0);
}

void dead_creatures_return_to_pool_check(const struct ScriptLine *scline)
{
    int64_t val = scline->np[0];
    command_add_value(scline->command, ALL_PLAYERS, val, 0, 0);
}

void set_creature_tendencies_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    const char *tendency = scline->tp[1];
    int64_t value = scline->np[2];
    int64_t tend_id = get_rid(tendency_desc, tendency);
    if (tend_id == -1)
    {
      SCRPTERRLOG("Unrecognized tendency type, '%s'", tendency);
      return;
    }
    command_add_value(scline->command, plr_range_id, tend_id, value, 0);
}

void change_creature_owner_check(const struct ScriptLine *scline)
{
    int64_t origin_plyr_idx = scline->np[0];
    const char *crtr_name = scline->tp[1];
    const char *criteria = scline->tp[2];
    int64_t dest_plyr_idx = scline->np[3];
    SCRIPTDBG(11, "Starting");
    int64_t crtr_id = parse_creature_name(crtr_name);
    if (crtr_id == CREATURE_NONE)
    {
        SCRPTERRLOG("Unknown creature, '%s'", crtr_name);
        return;
  }
  int64_t select_id = parse_criteria(criteria);
  if (select_id == -1) {
    SCRPTERRLOG("Unknown select criteria, '%s'", criteria);
    return;
  }
  command_add_value(scline->command, origin_plyr_idx, crtr_id, select_id, dest_plyr_idx);
}

void creature_entrance_level_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    unsigned char val = scline->np[1];
  command_add_value(scline->command, plr_range_id, val, 0, 0);
}

void max_creatures_process(struct ScriptContext *context)
{
    int64_t i = context->player_idx;
    int64_t param1 = context->value->longs[0];
    SYNCDBG(4,"Setting player %" PRId64 " max attracted creatures to %" PRId64 ".",(int64_t)i,(int64_t)param1);
    struct Dungeon *dungeon = get_dungeon(i);
    if (dungeon_invalid(dungeon))
        return;
    dungeon->max_creatures_attracted = param1;
}

void dead_creatures_return_to_pool_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    set_flag_value(kfx_sim_state.mode_flags, MFlg_DeadBackToPool, context->value->longs[0]);
}

void set_creature_tendencies_process(struct ScriptContext *context)
{
    struct PlayerInfo *player = get_player(context->player_idx);
    set_creature_tendencies(player, context->value->longs[0], context->value->longs[1]);
}

void change_creature_owner_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    script_change_creature_owner_with_criteria(context->player_idx, value->longs[0], value->longs[1], value->longs[2]);
}

/** CREATURE_ENTRANCE_LEVEL: the level, from 1 (0: unchanged), for one player or all of them. */
void creature_entrance_level_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    int64_t param1 = context->value->longs[0];
    int64_t plr_range_id = context->value->plyr_range;
    struct Dungeon *dungeon;
    if (param1 > 0)
    {
        if (plr_range_id == ALL_PLAYERS)
        {
            for (int64_t i = 0; i < PLAYERS_COUNT; i++)
            {
                dungeon = get_dungeon(i);
                if (!dungeon_invalid(dungeon))
                {
                    dungeon->creature_entrance_level = (param1 - 1);
                }
            }
        }
        else
        {
            dungeon = get_dungeon(plr_range_id);
            if (!dungeon_invalid(dungeon))
            {
                dungeon->creature_entrance_level = (param1 - 1);
            }
        }
    }
}

void add_creature_to_level_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    const char *crtr_name = scline->tp[1];
    const char *locname = scline->tp[2];
    int64_t ncopies = scline->np[3];
    CrtrExpLevel exp_level = scline->np[4];
    int64_t carried_gold = scline->np[5];
    const char *spawn_type = scline->tp[6];
    TbMapLocation location;
    if ((exp_level < 1) || (exp_level > CREATURE_MAX_LEVEL))
    {
        SCRPTERRLOG("Invalid CREATURE LEVEL parameter");
        return;
    }
    if ((ncopies <= 0) || (ncopies >= CREATURES_COUNT))
    {
        SCRPTERRLOG("Invalid number of creatures to add");
        return;
    }
    if (ncopies > kfx_config_state.conf.rules[0].gameplay.creatures_count)
    {
        SCRPTWRNLOG("Trying to add %" PRId64 " creatures which is over map limit %" PRIu64, (int64_t)(ncopies), (uint64_t)(kfx_config_state.conf.rules[0].gameplay.creatures_count));
    }
    if (kfx_game_state.script.party_triggers_num >= PARTY_TRIGGERS_COUNT)
    {
        SCRPTERRLOG("Too many ADD_CREATURE commands in script");
        return;
    }
    int64_t crtr_id = get_rid(creature_desc, crtr_name);
    if (crtr_id == -1)
    {
        SCRPTERRLOG("Unknown creature, '%s'", crtr_name);
        return;
    }
    int64_t spawn_type_id;
    if ((strcmp(spawn_type, "") == 0))
    {
        spawn_type_id = SpwnT_Default;
    }
    else
    {
        spawn_type_id = get_rid(spawn_type_desc, spawn_type);
    }
    if (spawn_type_id == -1)
    {
        SCRPTERRLOG("Unknown spawn type, '%s'", spawn_type);
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
    if (get_script_current_condition() == CONDITION_ALWAYS)
    {
        script_process_new_creatures(plr_id, crtr_id, location, ncopies, carried_gold, exp_level-1, spawn_type_id);
    } else
    {
        if (kfx_game_state.script.party_triggers_num < PARTY_TRIGGERS_COUNT)
        {
            struct PartyTrigger* pr_trig = &kfx_game_state.script.party_triggers[kfx_game_state.script.party_triggers_num];
            pr_trig->flags = TrgF_CREATE_CREATURE;
            pr_trig->flags |= next_command_reusable ? TrgF_REUSABLE : 0;

            pr_trig->plyr_idx = plr_id;
            pr_trig->creatr_id = crtr_id;
            pr_trig->exp_level = exp_level - 1;
            pr_trig->carried_gold = carried_gold;
            pr_trig->location = location;
            pr_trig->ncopies = ncopies;
            pr_trig->spawn_type = spawn_type_id;
            pr_trig->condit_idx = get_script_current_condition();
        }
        else
        {
            SCRPTERRLOG("Too many ADD_CREATURE commands in script");
        }
        kfx_game_state.script.party_triggers_num++;
    }
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
