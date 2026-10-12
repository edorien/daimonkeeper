/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lvl_script_cmd_players.c
 *     Level script commands: players (availability, money, computer players, alliances, flags, timers, game end).
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
#include "custom_sprites.h"
#include "lvl_script_conditions.h"
#include "lvl_script_lib.h"
#include "player_utils.h"
#include "room_library.h"
#include "player_availability.h"
#include "ports/ui_port.h"
#include "lvl_script_commands_private.h"
#include "lvl_script_commands_private.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

const struct NamedCommand modifier_desc[] = {
  {"Health",          1},
  {"Strength",        2},
  {"Armour",          3},
  {"SpellDamage",     4},
  {"Speed",           5},
  {"Salary",          6},
  {"TrainingCost",    7},
  {"ScavengingCost",  8},
  {"Loyalty",         9},
  {NULL,              0},
};

const struct NamedCommand script_operator_desc[] = {
  {"SET",         1},
  {"INCREASE",    2},
  {"DECREASE",    3},
  {"MULTIPLY",    4},
  {NULL,          0},
};

const struct NamedCommand locked_desc[] = {
  {"LOCKED", 1},
  {"UNLOCKED", 0},
  {NULL, 0}
};

TbBool parse_set_varib(const char *varib_name, int64_t *varib_id, int64_t *varib_type)
{
    char c;

    *varib_id = -1;
    if (*varib_id == -1)
    {
      *varib_id = get_id(flag_desc, varib_name);
      *varib_type = SVar_FLAG;
    }
    if (*varib_id == -1)
    {
      *varib_id = get_id(campaign_flag_desc, varib_name);
      *varib_type = SVar_CAMPAIGN_FLAG;
    }
    if (*varib_id == -1)
    {
        if (2 == sscanf(varib_name, "BOX%" SCNd64 "_ACTIVATE%c", varib_id, &c) && (c == 'D'))
        {
            // activateD
            *varib_type = SVar_BOX_ACTIVATED;
        }
        else
        if (2 == sscanf(varib_name, "TRAP%" SCNd64 "_ACTIVATE%c", varib_id, &c) && (c == 'D'))
        {
            // activateD
            *varib_type = SVar_TRAP_ACTIVATED;
        }
        else
        {
            *varib_id = -1;
        }
        parse_creature_bracket_varib(varib_name, varib_id, varib_type);
    }
    if (*varib_id == -1)
    {
      SCRPTERRLOG("Unknown variable name, '%s'", varib_name);
      return false;
    }
    return true;
}

/** parse_set_varib() for a level script line: an unknown variable also goes to the compat report. */
TbBool script_parse_set_varib(const char *varib_name, int64_t *varib_id, int64_t *varib_type)
{
    if (parse_set_varib(varib_name, varib_id, varib_type))
        return true;
    compat_report_add(CompatIssue_ScriptName, varib_name, NULL, text_line_number);
    return false;
}
static TbBool get_custom_ensign_from_value(const char* txt, int16_t* ensign_id);

static TbBool get_custom_ensign_from_value(const char* txt, int16_t* ensign_id)
{
    if (txt[0] == '\0')
        return false;   
    if(strncmp(txt,"RESET",5) == 0 || strncmp(txt,"-1",2) == 0){
        *ensign_id = -1;
        return true;
    }
    int64_t idx = get_ensign_id(txt);    
    *ensign_id = CUSTOM_ENSIGN_BASE + idx;
    return true;
}

void set_heart_health_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    struct Thing* heartng = get_player_soul_container(value->longs[0]);
    struct ObjectConfigStats* objst = get_object_model_stats(heartng->model);
    if (scline->np[1] > objst->health)
    {
        SCRPTWRNLOG("Value %" PRId64 " is greater than maximum: %" PRId64, (int64_t)(scline->np[1]), (int64_t)(objst->health));
        value->longs[1] = objst->health;
    }
    else
    {
        value->longs[1] = scline->np[1];
    }
    PROCESS_SCRIPT_VALUE(scline->command);
}

void set_heart_health_process(struct ScriptContext *context)
{
    struct Thing* heartng = get_player_soul_container(context->player_idx);
    if (thing_exists(heartng))
    {
        heartng->health = (int64_t)context->value->longs[1];
    }
}

void add_heart_health_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    value->longs[1] = scline->np[1];
    value->longs[2] = scline->np[2];
    PROCESS_SCRIPT_VALUE(scline->command);
}

void add_heart_health_process(struct ScriptContext *context)
{
    PlayerNumber plyr_idx = context->player_idx;
    HitPoints healthdelta = context->value->longs[1];
    TbBool warn_on_damage = context->value->longs[2];

    add_heart_health(plyr_idx,healthdelta,warn_on_damage);
}

void lock_possession_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    int64_t locked = scline->np[1];
    if (locked == -1)
    {
        locked = get_id(locked_desc, scline->tp[1]);
        if (locked == -1)
        {
            SCRPTERRLOG("Invalid Possession lock value (%s) not recognized.", scline->tp[1]);
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }

    value->chars[1] = locked;
    PROCESS_SCRIPT_VALUE(scline->command);
}

void lock_possession_process(struct ScriptContext* context)
{
    struct PlayerInfo *player = get_player(context->player_idx);
    if (player_exists(player))
    {
        player->possession_lock = context->value->chars[1];
    }
}

void add_to_timer_check(const struct ScriptLine *scline)
{
    const char *timrname = scline->tp[1];
    int64_t timr_id = get_rid(timer_desc, timrname);
    if (timr_id == -1)
    {
        SCRPTERRLOG("Unknown timer, '%s'", timrname);
        return;
    }
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    value->longs[1] = timr_id;
    value->longs[2] = scline->np[2];
    PROCESS_SCRIPT_VALUE(scline->command);
}

void add_to_timer_process(struct ScriptContext *context)
{
   add_to_script_timer(context->player_idx, context->value->longs[1], context->value->longs[2]);
}

void add_bonus_time_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    value->longs[0] = scline->np[0];
    PROCESS_SCRIPT_VALUE(scline->command);
}

void add_bonus_time_process(struct ScriptContext *context)
{
   kfx_game_state.bonus_time += context->value->longs[0];
}

void set_player_colour_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    int64_t color_idx = get_rid(cmpgn_human_player_options, scline->tp[1]);
    if (scline->np[0] == kfx_config_state.neutral_player_num)
    {
        SCRPTERRLOG("Can't change color of Neutral player.");
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    if (color_idx == -1)
    {
        if (parameter_is_number(scline->tp[1]))
        {
            color_idx = atoi(scline->tp[1]);
        }
        else
        {
            SCRPTERRLOG("Invalid color: '%s'", scline->tp[1]);
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }
    value->bytes[0] = (unsigned char)color_idx;
    PROCESS_SCRIPT_VALUE(scline->command);
}

void set_player_colour_process(struct ScriptContext *context)
{
    if (context->player_idx == PLAYER_NEUTRAL)
    {
        return;
    }
    set_player_colour(context->player_idx, context->value->bytes[0]);
}

/** SET_PLAYER_MODIFIER, ADD_TO_PLAYER_MODIFIER: the modifier and the value; only SET refuses a negative value. */
static void player_modifier_check(const struct ScriptLine* scline, TbBool refuses_negative)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    int64_t mdfrdesc = get_id(modifier_desc, scline->tp[1]);
    int64_t mdfrval = scline->np[2];
    const char *mdfrname = get_conf_parameter_text(modifier_desc,mdfrdesc);
    if (mdfrdesc == -1)
    {
        SCRPTERRLOG("Unknown Player Modifier '%s'.", scline->tp[1]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    if (refuses_negative && (mdfrval < 0))
    {
        SCRPTERRLOG("Value %" PRId64 " out of range for Player Modifier '%s'.", (int64_t)(mdfrval), mdfrname);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    if (scline->np[0] == kfx_config_state.neutral_player_num)
    {
        SCRPTERRLOG("Can't manipulate Player Modifier '%s', player %" PRId64 " has no dungeon.", mdfrname, (int64_t)(scline->np[0]));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->shorts[0] = mdfrdesc;
    value->shorts[1] = mdfrval;
    PROCESS_SCRIPT_VALUE(scline->command);
}

void set_player_modifier_check(const struct ScriptLine* scline)
{
    player_modifier_check(scline, true);
}

void set_player_modifier_process(struct ScriptContext* context)
{
    struct Dungeon* dungeon;
    int64_t mdfrdesc = context->value->shorts[0];
    int64_t mdfrval = context->value->shorts[1];
    const char *mdfrname = get_conf_parameter_text(modifier_desc,mdfrdesc);
    PlayerNumber plyr_idx = context->player_idx;
    dungeon = get_dungeon(plyr_idx);
    switch (mdfrdesc)
    {
        case 1: // Health
            SCRIPTDBG(7,"Changing Player Modifier '%s' of player %" PRId64 " from %" PRId64 " to %" PRId64 ".", mdfrname, (int64_t)plyr_idx, (int64_t)(dungeon->modifier.health), (int64_t)(mdfrval));
            dungeon->modifier.health = mdfrval;
            do_to_players_all_creatures_of_model(plyr_idx, CREATURE_ANY, update_relative_creature_health);
            break;
        case 2: // Strength
            SCRIPTDBG(7,"Changing Player Modifier '%s' of player %" PRId64 " from %" PRId64 " to %" PRId64 ".", mdfrname, (int64_t)plyr_idx, (int64_t)(dungeon->modifier.strength), (int64_t)(mdfrval));
            dungeon->modifier.strength = mdfrval;
            break;
        case 3: // Armour
            SCRIPTDBG(7,"Changing Player Modifier '%s' of player %" PRId64 " from %" PRId64 " to %" PRId64 ".", mdfrname, (int64_t)plyr_idx, (int64_t)(dungeon->modifier.armour), (int64_t)(mdfrval));
            dungeon->modifier.armour = mdfrval;
            break;
        case 4: // SpellDamage
            SCRIPTDBG(7,"Changing Player Modifier '%s' of player %" PRId64 " from %" PRId64 " to %" PRId64 ".", mdfrname, (int64_t)plyr_idx, (int64_t)(dungeon->modifier.spell_damage), (int64_t)(mdfrval));
            dungeon->modifier.spell_damage = mdfrval;
            break;
        case 5: // Speed
            SCRIPTDBG(7,"Changing Player Modifier '%s' of player %" PRId64 " from %" PRId64 " to %" PRId64 ".", mdfrname, (int64_t)plyr_idx, (int64_t)(dungeon->modifier.speed), (int64_t)(mdfrval));
            dungeon->modifier.speed = mdfrval;
            do_to_players_all_creatures_of_model(plyr_idx, CREATURE_ANY, update_creature_speed);
            break;
        case 6: // Salary
            SCRIPTDBG(7,"Changing Player Modifier '%s' of player %" PRId64 " from %" PRId64 " to %" PRId64 ".", mdfrname, (int64_t)plyr_idx, (int64_t)(dungeon->modifier.pay), (int64_t)(mdfrval));
            dungeon->modifier.pay = mdfrval;
            break;
        case 7: // TrainingCost
            SCRIPTDBG(7,"Changing Player Modifier '%s' of player %" PRId64 " from %" PRId64 " to %" PRId64 ".", mdfrname, (int64_t)plyr_idx, (int64_t)(dungeon->modifier.training_cost), (int64_t)(mdfrval));
            dungeon->modifier.training_cost = mdfrval;
            break;
        case 8: // ScavengingCost
            SCRIPTDBG(7,"Changing Player Modifier '%s' of player %" PRId64 " from %" PRId64 " to %" PRId64 ".", mdfrname, (int64_t)plyr_idx, (int64_t)(dungeon->modifier.scavenging_cost), (int64_t)(mdfrval));
            dungeon->modifier.scavenging_cost = mdfrval;
            break;
        case 9: // Loyalty
            SCRIPTDBG(7,"Changing Player Modifier '%s' of player %" PRId64 " from %" PRId64 " to %" PRId64 ".", mdfrname, (int64_t)plyr_idx, (int64_t)(dungeon->modifier.loyalty), (int64_t)(mdfrval));
            dungeon->modifier.loyalty = mdfrval;
            break;
        default:
            WARNMSG("Unsupported Player Modifier, command %" PRId64 ".", (int64_t)(mdfrdesc));
            break;
    }
}

void add_to_player_modifier_check(const struct ScriptLine* scline)
{
    player_modifier_check(scline, false);
}

void add_to_player_modifier_process(struct ScriptContext* context)
{
    struct Dungeon* dungeon;
    int64_t mdfrdesc = context->value->shorts[0];
    int64_t mdfrval = context->value->shorts[1];
    int64_t mdfradd;
    const char *mdfrname = get_conf_parameter_text(modifier_desc,mdfrdesc);
    PlayerNumber plyr_idx = context->player_idx;
    dungeon = get_dungeon(plyr_idx);
    switch (mdfrdesc)
    {
        case 1: // Health
            mdfradd = dungeon->modifier.health + mdfrval;
            if (mdfradd >= 0) {
                SCRIPTDBG(7,"Adding %" PRId64 " to Player %" PRId64 " Modifier '%s'.", (int64_t)(mdfrval), (int64_t)plyr_idx, mdfrname);
                dungeon->modifier.health = mdfradd;
                do_to_players_all_creatures_of_model(plyr_idx, CREATURE_ANY, update_relative_creature_health);
            } else {
                SCRPTERRLOG("Player %" PRId64 " Modifier '%s' may not be negative. Tried to add %" PRId64 " to value %" PRId64, (int64_t)plyr_idx, mdfrname, (int64_t)(mdfrval), (int64_t)(dungeon->modifier.health));
            }
            break;
        case 2: // Strength
            mdfradd = dungeon->modifier.strength + mdfrval;
            if (mdfradd >= 0) {
                SCRIPTDBG(7,"Adding %" PRId64 " to Player %" PRId64 " Modifier '%s'.", (int64_t)(mdfrval), (int64_t)plyr_idx, mdfrname);
                dungeon->modifier.strength = mdfradd;
            } else {
                SCRPTERRLOG("Player %" PRId64 " Modifier '%s' may not be negative. Tried to add %" PRId64 " to value %" PRId64, (int64_t)plyr_idx, mdfrname, (int64_t)(mdfrval), (int64_t)(dungeon->modifier.strength));
            }
            break;
        case 3: // Armour
            mdfradd = dungeon->modifier.armour + mdfrval;
            if (mdfradd >= 0) {
                SCRIPTDBG(7,"Adding %" PRId64 " to Player %" PRId64 " Modifier '%s'.", (int64_t)(mdfrval), (int64_t)plyr_idx, mdfrname);
                dungeon->modifier.armour = mdfradd;
            } else {
                SCRPTERRLOG("Player %" PRId64 " Modifier '%s' may not be negative. Tried to add %" PRId64 " to value %" PRId64, (int64_t)plyr_idx, mdfrname, (int64_t)(mdfrval), (int64_t)(dungeon->modifier.armour));
            }
            break;
        case 4: // SpellDamage
            mdfradd = dungeon->modifier.spell_damage + mdfrval;
            if (mdfradd >= 0) {
                SCRIPTDBG(7,"Adding %" PRId64 " to Player %" PRId64 " Modifier '%s'.", (int64_t)(mdfrval), (int64_t)plyr_idx, mdfrname);
                dungeon->modifier.spell_damage = mdfradd;
            } else {
                SCRPTERRLOG("Player %" PRId64 " Modifier '%s' may not be negative. Tried to add %" PRId64 " to value %" PRId64, (int64_t)plyr_idx, mdfrname, (int64_t)(mdfrval), (int64_t)(dungeon->modifier.spell_damage));
            }
            break;
        case 5: // Speed
            mdfradd = dungeon->modifier.speed + mdfrval;
            if (mdfradd >= 0) {
                SCRIPTDBG(7,"Adding %" PRId64 " to Player %" PRId64 " Modifier '%s'.", (int64_t)(mdfrval), (int64_t)plyr_idx, mdfrname);
                dungeon->modifier.speed = mdfradd;
                do_to_players_all_creatures_of_model(plyr_idx, CREATURE_ANY, update_creature_speed);
            } else {
                SCRPTERRLOG("Player %" PRId64 " Modifier '%s' may not be negative. Tried to add %" PRId64 " to value %" PRId64, (int64_t)plyr_idx, mdfrname, (int64_t)(mdfrval), (int64_t)(dungeon->modifier.speed));
            }
            break;
        case 6: // Salary
            mdfradd = dungeon->modifier.pay + mdfrval;
            if (mdfradd >= 0) {
                SCRIPTDBG(7,"Adding %" PRId64 " to Player %" PRId64 " Modifier '%s'.", (int64_t)(mdfrval), (int64_t)plyr_idx, mdfrname);
                dungeon->modifier.pay = mdfradd;
            } else {
                SCRPTERRLOG("Player %" PRId64 " Modifier '%s' may not be negative. Tried to add %" PRId64 " to value %" PRId64, (int64_t)plyr_idx, mdfrname, (int64_t)(mdfrval), (int64_t)(dungeon->modifier.pay));
            }
            break;
        case 7: // TrainingCost
            mdfradd = dungeon->modifier.training_cost + mdfrval;
            if (mdfradd >= 0) {
                SCRIPTDBG(7,"Adding %" PRId64 " to Player %" PRId64 " Modifier '%s'.", (int64_t)(mdfrval), (int64_t)plyr_idx, mdfrname);
                dungeon->modifier.training_cost = mdfradd;
            } else {
                SCRPTERRLOG("Player %" PRId64 " Modifier '%s' may not be negative. Tried to add %" PRId64 " to value %" PRId64, (int64_t)plyr_idx, mdfrname, (int64_t)(mdfrval), (int64_t)(dungeon->modifier.training_cost));
            }
            break;
        case 8: // ScavengingCost
            mdfradd = dungeon->modifier.scavenging_cost + mdfrval;
            if (mdfradd >= 0) {
                SCRIPTDBG(7,"Adding %" PRId64 " to Player %" PRId64 " Modifier '%s'.", (int64_t)(mdfrval), (int64_t)plyr_idx, mdfrname);
                dungeon->modifier.scavenging_cost = mdfradd;
            } else {
                SCRPTERRLOG("Player %" PRId64 " Modifier '%s' may not be negative. Tried to add %" PRId64 " to value %" PRId64, (int64_t)plyr_idx, mdfrname, (int64_t)(mdfrval), (int64_t)(dungeon->modifier.scavenging_cost));
            }
            break;
        case 9: // Loyalty
            mdfradd = dungeon->modifier.loyalty + mdfrval;
            if (mdfradd >= 0) {
                SCRIPTDBG(7,"Adding %" PRId64 " to Player %" PRId64 " Modifier '%s'.", (int64_t)(mdfrval), (int64_t)plyr_idx, mdfrname);
                dungeon->modifier.loyalty = mdfradd;
            } else {
                SCRPTERRLOG("Player %" PRId64 " Modifier '%s' may not be negative. Tried to add %" PRId64 " to value %" PRId64, (int64_t)plyr_idx, mdfrname, (int64_t)(mdfrval), (int64_t)(dungeon->modifier.loyalty));
            }
            break;
        default:
            WARNMSG("Unsupported Player Modifier, command %" PRId64 ".", (int64_t)(mdfrdesc));
            break;
    }
}

void computer_player_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    int64_t plr_range_id = scline->np[0];
    const char* comp_model = scline->tp[1];
    int64_t plr_start;
    int64_t plr_end;
    char model = 0;
    char type = PT_Keeper;
    TbBool toggle = true;

    if (kfx_game_state.level_file_version == 0 && plr_range_id == PLAYER_GOOD)
    {
        SCRPTERRLOG("PLAYER_GOOD COMPUTER_PLAYER cannot be set in level version 0.");
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    if (get_players_range(plr_range_id, &plr_start, &plr_end) < 0)
    {
        SCRPTERRLOG("Given owning player range %" PRId64 " is not supported in this command", (int64_t)plr_range_id);
        DEALLOCATE_SCRIPT_VALUE
    }
    for (int64_t i = plr_start; i < plr_end; i++)
    {
        set_flag(value->shorts[2], to_flag(i));
    }
    if (parameter_is_number(comp_model))
    {
        model = atoi(comp_model);
    }
    else if (strcasecmp(comp_model, "ROAMING") == 0)
    {
        type = PT_Roaming;
    }
    else if (strcasecmp(comp_model, "OFF") == 0)
    {
        toggle = false;
    }
    else
    {
        SCRPTERRLOG("invalid COMPUTER_PLAYER param '%s'", comp_model);
        DEALLOCATE_SCRIPT_VALUE
    }

    value->bytes[0] = plr_start;
    value->bytes[1] = plr_end;
    value->bytes[2] = type;
    value->bytes[3] = model;
    value->bytes[6] = toggle;
    PROCESS_SCRIPT_VALUE(scline->command);
}

void computer_player_process(struct ScriptContext* context)
{
    char plr_start = context->value->bytes[0];
    char plr_end = context->value->bytes[1];
    char playertype = context->value->bytes[2];
    char model = context->value->bytes[3];
    int64_t owner_flags = context->value->shorts[2];
    TbBool toggle = context->value->bytes[6];
    struct PlayerInfo* player = INVALID_PLAYER;
    for (int64_t i = plr_start; i < plr_end; i++)
    {
        if (i == PLAYER_NEUTRAL)
        {
            continue;
        }
        if (playertype == PT_Roaming)
        {
            //kill the old computer first, in case he was already active.
            script_support_setup_player_as_zombie_keeper(i);

            player = get_player(i);
            player->player_type = PT_Roaming;
            player->allocflags |= PlaF_Allocated;
            player->allocflags |= PlaF_CompCtrl;
            player->id_number = i;
        }
        else
        {
            if (flag_is_set(owner_flags, to_flag(i)))
            {
                if (toggle == true)
                {
                    script_support_setup_player_as_computer_keeper(i, model);
                    player = get_player(i);
                    struct Dungeon* dungeon = get_dungeon(i);
                    dungeon->turns_between_entrance_generation = player->generate_speed;
                    init_creature_states_for_player(i);
                    post_init_player(player);
                }
                else
                {
                    script_support_setup_player_as_zombie_keeper(i);
                }
            }
        }
        recalculate_player_creature_digger_lists(i);
    }
}

void set_computer_globals_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    int64_t plr_range_id = scline->np[0];

    int64_t plr_start;
    int64_t plr_end;
    if (get_players_range(plr_range_id, &plr_start, &plr_end) < 0) {
        SCRPTERRLOG("Given owning player range %" PRId64 " is not supported in this command", (int64_t)plr_range_id);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    value->shorts[0] = plr_start;
    value->shorts[1] = plr_end;
    value->longs[1] = scline->np[1];
    value->longs[2] = scline->np[2];
    value->longs[3] = scline->np[3];
    value->longs[4] = scline->np[4];
    value->longs[5] = scline->np[5];
    value->longs[6] = scline->np[6];
    value->longs[7] = -1;
    if (scline->np[7] != '\0')
    {
        value->longs[7] = scline->np[7];
    }
    PROCESS_SCRIPT_VALUE(scline->command);
}

void set_computer_globals_process(struct ScriptContext* context)
{
    computer_set_globals(context->value->shorts[0], context->value->shorts[1],
        context->value->longs[1], context->value->longs[2], context->value->longs[3], context->value->longs[4],
        context->value->longs[5], context->value->longs[6], context->value->longs[7]);
}

/** SET_COMPUTER_PROCESS and SET_COMPUTER_CHECKS: a player range, the name and five numbers. */
/** SET_COMPUTER_PROCESS's, _CHECKS' and _EVENT's check: a player range, the process, check or event name (tp[1]) and
 *  five values. A player of the range without a computer is skipped when the command runs. */
static void computer_config_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    int64_t plr_range_id = scline->np[0];

    int64_t plr_start;
    int64_t plr_end;
    if (get_players_range(plr_range_id, &plr_start, &plr_end) < 0) {
        SCRPTERRLOG("Given owning player range %" PRId64 " is not supported in this command", (int64_t)plr_range_id);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->shorts[0] = plr_start;
    value->shorts[1] = plr_end;
    value->longs[1] = scline->np[2];
    value->longs[2] = scline->np[3];
    value->longs[3] = scline->np[4];
    value->longs[4] = scline->np[5];
    value->longs[5] = scline->np[6];
    value->longs[6] = script_strdup(scline->tp[1]);
    if (value->longs[6] < 0) {
        SCRPTERRLOG("Run out script strings space");
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    PROCESS_SCRIPT_VALUE(scline->command);
}

void set_computer_process_check(const struct ScriptLine* scline)
{
    computer_config_check(scline);
}

void set_computer_process_process(struct ScriptContext* context)
{
    int64_t plr_start = context->value->shorts[0];
    int64_t plr_end = context->value->shorts[1];
    const char* procname = script_strval(context->value->longs[6]);
    int64_t n = computer_set_process_config(plr_start, plr_end, procname, context->value->longs[1],
        context->value->longs[2], context->value->longs[3], context->value->longs[4], context->value->longs[5], true);
    if (n == 0)
    {
        SCRIPTDBG(6, "No computer process found named '%s' in players %" PRId64 " to %" PRId64, procname, (int64_t)plr_start, (int64_t)plr_end - 1);
        return;
    }
    SCRIPTDBG(6, "Altered %" PRId64 " processes named '%s'", (int64_t)(n), procname);
}

void set_computer_checks_process(struct ScriptContext* context)
{
    int64_t plr_start = context->value->shorts[0];
    int64_t plr_end = context->value->shorts[1];
    const char* chkname = script_strval(context->value->longs[6]);
    int64_t n = computer_set_check_config(plr_start, plr_end, chkname, context->value->longs[1],
        context->value->longs[2], context->value->longs[3], context->value->longs[4], context->value->longs[5], true, true);
    if (n == 0)
    {
        SCRPTERRLOG("No computer check found named '%s' in players %" PRId64 " to %" PRId64, chkname, (int64_t)plr_start, (int64_t)plr_end - 1);
        return;
    }
    SCRIPTDBG(6, "Altered %" PRId64 " checks named '%s'", (int64_t)(n), chkname);
}

void set_computer_event_check(const struct ScriptLine* scline)
{
    computer_config_check(scline);
}

void set_computer_event_process(struct ScriptContext* context)
{
    int64_t plr_start = context->value->shorts[0];
    int64_t plr_end = context->value->shorts[1];
    const char* evntname = script_strval(context->value->longs[6]);
    int64_t test_interval = context->value->longs[1];
    int64_t primary_parameter = context->value->longs[2];
    int64_t secondary_parameter = context->value->longs[3];
    int64_t tertiary_parameter = context->value->longs[4];
    int64_t last_test_gameturn = context->value->longs[5];

    int64_t n = 0;
    for (int64_t i = plr_start; i < plr_end; i++)
    {
        struct Computer2* comp = get_computer_player(i);
        if (computer_player_invalid(comp)) {
            continue;
        }
        for (int64_t k = 0; k < COMPUTER_EVENTS_COUNT; k++)
        {
            struct ComputerEvent* event = &comp->events[k];
            if (event->name[0] == '\0')
                break;
            if (strcasecmp(evntname, event->name) == 0)
            {
                if (kfx_game_state.level_file_version > 0)
                {
                    SCRPTLOG("Changing computer %" PRId64 " event '%s' config from (%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ") to (%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ")",
                        (int64_t)i, event->name,
                        (int64_t)event->test_interval, (int64_t)event->primary_parameter, (int64_t)event->secondary_parameter, (int64_t)event->tertiary_parameter, (int64_t)event->last_test_gameturn,
                        (int64_t)test_interval, (int64_t)primary_parameter, (int64_t)secondary_parameter, (int64_t)tertiary_parameter, (int64_t)last_test_gameturn);
                    event->test_interval = test_interval;
                    event->primary_parameter = primary_parameter;
                    event->secondary_parameter = secondary_parameter;
                    event->tertiary_parameter = tertiary_parameter;
                    event->last_test_gameturn = last_test_gameturn;
                    n++;
                }
                else
                {
                    SCRPTLOG("Changing computer %" PRId64 " event '%s' config from (%" PRId64 ",%" PRId64 ") to (%" PRId64 ",%" PRId64 ")", (int64_t)i, event->name,
                        (int64_t)event->primary_parameter, (int64_t)event->secondary_parameter, (int64_t)test_interval, (int64_t)primary_parameter);
                    event->primary_parameter = test_interval;
                    event->secondary_parameter = primary_parameter;
                    n++;
                }
            }
        }
    }
    if (n == 0)
    {
        SCRPTERRLOG("No computer event found named '%s' in players %" PRId64 " to %" PRId64, evntname, (int64_t)plr_start, (int64_t)plr_end - 1);
        return;
    }
    SCRIPTDBG(6, "Altered %" PRId64 " events named '%s'", (int64_t)(n), evntname);
}

void set_next_level_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    int64_t next_level = scline->np[0];
    TbBool correct = false;

    if (!is_campaign_level(kfx_sim_state.loaded_level_number))
    {
        SCRPTERRLOG("Script command %s only functions in campaigns.", scline->tcmnd);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    for (int64_t i = 0; i < CAMPAIGN_LEVELS_COUNT; i++)
    {
        if (campaign.single_levels[i] == next_level)
        {
            correct = true;
            break;
        }
    }
    if (correct == false)
    {
        SCRPTERRLOG("Cannot find level number '%" PRId64 "' in single levels of campaign.",(int64_t)(next_level));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    value->shorts[1] = next_level;
    if (is_bonus_level(kfx_sim_state.loaded_level_number) || is_extra_level(kfx_sim_state.loaded_level_number))
    {
        value->shorts[2] = true; // On bonus levels we have to force moving on.
    }
    PROCESS_SCRIPT_VALUE(scline->command);
}

void set_next_level_process(struct ScriptContext* context)
{
    TbBool force_now = context->value->shorts[2];
    intralvl.next_level = context->value->shorts[1];
    if (force_now)
    {
        set_continue_level_number(intralvl.next_level);
    }
}

void set_level_ensign_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    int64_t lvlnum = scline->np[0];
    if (!is_campaign_level(lvlnum))
    {
        SCRPTERRLOG("Script command %s only functions in campaigns.", scline->tcmnd);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    if (scline->tp[1][0] != '\0' && !get_custom_ensign_from_value(scline->tp[1], &value->shorts[2]))
    {
        SCRPTERRLOG("Invalid custom ensign (%s)", scline->tp[1]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->shorts[1] = lvlnum;
    PROCESS_SCRIPT_VALUE(scline->command);
}

void set_level_ensign_process(struct ScriptContext* context)
{
    set_level_ensign(context->value->shorts[1], context->value->shorts[2]);
}

void show_bonus_level_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    int64_t bonus_level = scline->np[0];

    if (!is_campaign_level(kfx_sim_state.loaded_level_number))
    {
        SCRPTERRLOG("Script command %s only functions in campaigns.", scline->tcmnd);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    if (!is_bonus_level(bonus_level))
    {
        SCRPTERRLOG("Level %" PRId64 " not found as bonus level in campaign.", (int64_t)(bonus_level));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    value->shorts[1] = bonus_level;
    PROCESS_SCRIPT_VALUE(scline->command);
}

void show_bonus_level_process(struct ScriptContext* context)
{
    set_bonus_level_visibility(context->value->shorts[1], 1);
}
void hide_bonus_level_process(struct ScriptContext* context)
{
    set_bonus_level_visibility(context->value->shorts[1], 0);
}

/******************************************************************************/
/* The original game's commands (refactor pass 4, S09): were lvl_script_commands_old.c's command_*() (now the
 * checks) and script_process_value()'s switch (now the processes). A command's values are kept in the script value
 * as before (command_add_value(): longs[0..2], so a saved level in progress keeps working). */

void room_available_check(const struct ScriptLine *scline)
{
    int64_t room_id = get_rid(room_desc, scline->tp[1]);
    if (room_id == -1)
    {
      SCRPTERRLOG("Unknown room name, '%s'", scline->tp[1]);
      return;
    }
    command_add_value(scline->command, scline->np[0], room_id, scline->np[2], scline->np[3]);
}

void room_available_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    set_room_available(context->player_idx, value->longs[0], value->longs[1], value->longs[2]);
}

void trap_available_check(const struct ScriptLine *scline)
{
    int64_t trap_id = get_rid(trap_desc, scline->tp[1]);
    if (trap_id == -1)
    {
      SCRPTERRLOG("Unknown trap, '%s'", scline->tp[1]);
      return;
    }
    command_add_value(scline->command, scline->np[0], trap_id, scline->np[2], scline->np[3]);
}

void trap_available_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    if (!set_trap_buildable_and_add_to_amount(context->player_idx, value->longs[0], value->longs[1], value->longs[2])) {
        WARNLOG("Setting trap %s availability for player %" PRId64 " failed.",trap_code_name(value->longs[0]),(int64_t)context->player_idx);
    }
}

void door_available_check(const struct ScriptLine *scline)
{
    int64_t door_id = get_rid(door_desc, scline->tp[1]);
    if (door_id == -1)
    {
        SCRPTERRLOG("Unknown door, '%s'", scline->tp[1]);
        return;
    }
    command_add_value(scline->command, scline->np[0], door_id, scline->np[2], scline->np[3]);
}

void door_available_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    set_door_buildable_and_add_to_amount(context->player_idx, value->longs[0], value->longs[1], value->longs[2]);
}

/**
 * Updates amount of RESEARCH points needed for the item to be researched.
 * Will not reorder the RESEARCH items.
 */
void research_check(const struct ScriptLine *scline)
{
    int64_t item_type = get_rid(research_desc, scline->tp[1]);
    int64_t item_id = get_research_id(item_type, scline->tp[2], "command_research");
    if (item_id < 0)
      return;
    command_add_value(scline->command, scline->np[0], item_type, item_id, scline->np[3]);
}

void research_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    if (!update_or_add_players_research_amount(context->player_idx, value->longs[0], value->longs[1], value->longs[2])) {
        WARNLOG("Updating research points for type %" PRId64 " kind %" PRId64 " of player %" PRId64 " failed.",(int64_t)value->longs[0],(int64_t)value->longs[1],(int64_t)context->player_idx);
    }
}

/**
 * Updates amount of RESEARCH points needed for the item to be researched.
 * Reorders the RESEARCH items - needs all items to be re-added.
 */
void research_order_check(const struct ScriptLine *scline)
{
    int64_t plr_start;
    int64_t plr_end;
    if (get_players_range(scline->np[0], &plr_start, &plr_end) < 0) {
        SCRPTERRLOG("Given owning player range %" PRId64 " is not supported in this command",(int64_t)scline->np[0]);
        return;
    }
    for (int64_t i = plr_start; i < plr_end; i++)
    {
        struct Dungeon* dungeon = get_dungeon(i);
        if (dungeon_invalid(dungeon))
            continue;
        if (dungeon->research_num >= DUNGEON_RESEARCH_COUNT)
        {
          SCRPTERRLOG("Too many RESEARCH ITEMS, for player %" PRId64, (int64_t)(i));
          return;
        }
    }
    int64_t item_type = get_rid(research_desc, scline->tp[1]);
    int64_t item_id = get_research_id(item_type, scline->tp[2], "command_research_order");
    if (item_id < 0)
      return;
    command_add_value(scline->command, scline->np[0], item_type, item_id, scline->np[3]);
}

void research_order_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    if (!research_overriden_for_player(context->player_idx))
      remove_all_research_from_player(context->player_idx);
    add_research_to_player(context->player_idx, value->longs[0], value->longs[1], value->longs[2]);
}

/** True for the last player in the value's range: what the cases did after their loop over the players. */
static TbBool legacy_value_last_player(const struct ScriptContext *context)
{
    int64_t plr_start;
    int64_t plr_end;
    if (get_players_range(context->value->plyr_range, &plr_start, &plr_end) < 0)
        return false;
    return (context->player_idx == plr_end - 1);
}

void set_timer_check(const struct ScriptLine *scline)
{
    int64_t timr_id = get_rid(timer_desc, scline->tp[1]);
    if (timr_id == -1)
    {
        SCRPTERRLOG("Unknown timer, '%s'", scline->tp[1]);
        return;
    }
    command_add_value(scline->command, scline->np[0], timr_id, 0, 0);
}

void set_timer_process(struct ScriptContext *context)
{
    restart_script_timer(context->player_idx, context->value->longs[0]);
}

/** SET_FLAG, ADD_TO_FLAG, RANDOMISE_FLAG: the flag (any variable a script can set) and the value. */
static void legacy_flag_value(const struct ScriptLine *scline)
{
    int64_t flg_id;
    int64_t flag_type;
    if (!script_parse_set_varib(scline->tp[1], &flg_id, &flag_type))
    {
        SCRPTERRLOG("Unknown flag, '%s'", scline->tp[1]);
        return;
    }
    command_add_value(scline->command, scline->np[0], flg_id, scline->np[2], flag_type);
}

void set_flag_check(const struct ScriptLine *scline)
{
    legacy_flag_value(scline);
}

void set_flag_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    set_variable(context->player_idx, value->longs[2], value->longs[0], value->longs[1]);
}

void add_to_flag_check(const struct ScriptLine *scline)
{
    legacy_flag_value(scline);
}

void add_to_flag_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    set_variable(context->player_idx, value->longs[2], value->longs[0], get_condition_value(context->player_idx, value->longs[2], value->longs[0]) + value->longs[1]);
}

void randomise_flag_check(const struct ScriptLine *scline)
{
    legacy_flag_value(scline);
}

/** RANDOMISE_FLAG: from 1 to the value, or, for 0, to the flag's own value. */
void randomise_flag_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    if (value->longs[1] == 0)
    {
        int64_t current_flag_val = get_condition_value(context->player_idx, value->longs[2], value->longs[0]);
        set_variable(context->player_idx, value->longs[2], value->longs[0], GAME_RANDOM(current_flag_val) + 1);
    }
    else
    {
        set_variable(context->player_idx, value->longs[2], value->longs[0], GAME_RANDOM(value->longs[1]) + 1);
    }
}

/** SET_CAMPAIGN_FLAG, ADD_TO_CAMPAIGN_FLAG: the campaign flag and the value. */
static void legacy_campaign_flag_value(const struct ScriptLine *scline)
{
    int64_t flg_id = get_rid(campaign_flag_desc, scline->tp[1]);
    if (flg_id == -1)
    {
        SCRPTERRLOG("Unknown campaign flag, '%s'", scline->tp[1]);
        return;
    }
    command_add_value(scline->command, scline->np[0], flg_id, scline->np[2], 0);
}

void set_campaign_flag_check(const struct ScriptLine *scline)
{
    legacy_campaign_flag_value(scline);
}

void set_campaign_flag_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    intralvl.campaign_flags[context->player_idx][value->longs[0]] = saturate_set_signed(value->longs[1], 32);
}

void add_to_campaign_flag_check(const struct ScriptLine *scline)
{
    legacy_campaign_flag_value(scline);
}

void add_to_campaign_flag_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    int64_t i = context->player_idx;
    intralvl.campaign_flags[i][value->longs[0]] = saturate_set_signed(intralvl.campaign_flags[i][value->longs[0]] + value->longs[1], 32);
}

void export_variable_check(const struct ScriptLine *scline)
{
    int64_t src_type;
    int64_t src_id;
    // Recognize flag
    int64_t flg_id = get_rid(campaign_flag_desc, scline->tp[2]);
    if (flg_id == -1)
    {
        SCRPTERRLOG("Unknown CAMPAIGN FLAG, '%s'", scline->tp[2]);
        return;
    }
    if (!script_parse_get_varib(scline->tp[1], &src_id, &src_type, kfx_game_state.level_file_version))
    {
        SCRPTERRLOG("Unknown VARIABLE, '%s'", scline->tp[1]);
        return;
    }
    command_add_value(scline->command, scline->np[0], src_type, src_id, flg_id);
}

void export_variable_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    int64_t i = context->player_idx;
    SYNCDBG(8, "Setting campaign flag[%" PRId64 "][%" PRId64 "] to %" PRId64 ".", (int64_t)(i), (int64_t)(value->longs[2]), (int64_t)(get_condition_value(i, value->longs[0], value->longs[1])));
    intralvl.campaign_flags[i][value->longs[2]] = get_condition_value(i, value->longs[0], value->longs[1]);
}

void compute_flag_check(const struct ScriptLine *scline)
{
    const char *flgname = scline->tp[1];
    const char *operator_name = scline->tp[2];
    int64_t src_plr_range_id = scline->np[3];
    const char *src_flgname = scline->tp[4];
    int64_t alt = scline->np[5];
    int64_t flg_id;
    int64_t flag_type;
    if (!script_parse_set_varib(flgname, &flg_id, &flag_type))
    {
        SCRPTERRLOG("Unknown target flag, '%s'", flgname);
        return;
    }

    int64_t src_flg_id;
    int64_t src_flag_type;
    // try to identify source flag as a power, if it agrees, change flag type to SVar_AVAILABLE_MAGIC, keep power id
    // with rooms, traps, doors, etc. parse_get_varib assumes we want the count flag of them. Change it later in 'alt' switch if 'available' flag is needed
    src_flg_id = get_id(power_desc, src_flgname);
    if (src_flg_id == -1)
    {
        if (!script_parse_get_varib(src_flgname, &src_flg_id, &src_flag_type, kfx_game_state.level_file_version))
        {
            SCRPTERRLOG("Unknown source flag, '%s'", src_flgname);
            return;
        }
    } else
    {
        src_flag_type = SVar_AVAILABLE_MAGIC;
    }

    int64_t op_id = get_rid(script_operator_desc, operator_name);
    if (op_id == -1)
    {
        SCRPTERRLOG("Invalid operation for modifying flag's value: '%s'", operator_name);
        return;
    }

    if (alt != 0)
    {
        switch (src_flag_type)
        {
            case SVar_CREATURE_NUM:
                src_flag_type = SVar_CONTROLS_CREATURE;
                break;
            case SVar_TOTAL_CREATURES:
                src_flag_type = SVar_CONTROLS_TOTAL_CREATURES;
                break;
            case SVar_TOTAL_DIGGERS:
                src_flag_type = SVar_CONTROLS_TOTAL_DIGGERS;
                break;
            case SVar_GOOD_CREATURES:
                src_flag_type = SVar_CONTROLS_GOOD_CREATURES;
                break;
            case SVar_EVIL_CREATURES:
                src_flag_type = SVar_CONTROLS_EVIL_CREATURES;
                break;
            case SVar_DOOR_NUM:
                src_flag_type = SVar_AVAILABLE_DOOR;
                break;
            case SVar_TRAP_NUM:
                src_flag_type = SVar_AVAILABLE_TRAP;
                break;
            case SVar_ROOM_SLABS:
                src_flag_type = SVar_AVAILABLE_ROOM;
                break;
        }
    }
    // encode 4 byte params into 4xbyte integer (from high-order bit to low-order):
    // 1st byte: src player range idx
    // 2nd byte: operation id
    // 3rd byte: flag type
    // 4th byte: src flag type
    int64_t srcplr_op_flagtype_srcflagtype = (src_plr_range_id << 24) | (op_id << 16) | (flag_type << 8) | src_flag_type;
    command_add_value(scline->command, scline->np[0], srcplr_op_flagtype_srcflagtype, flg_id, src_flg_id);
}

/** COMPUTE_FLAG: once for the range, the source sum taken before any target changes (a target can be a source). */
void compute_flag_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    const struct ScriptValue *value = context->value;
    int64_t param1 = value->longs[0];
    int64_t param2 = value->longs[1];
    int64_t param3 = value->longs[2];
    int64_t plr_start;
    int64_t plr_end;
    get_players_range(value->plyr_range, &plr_start, &plr_end);
    int64_t src_plr_range = (param1 >> 24) & 255;
    int64_t operation = (param1 >> 16) & 255;
    unsigned char flag_type = (param1 >> 8) & 255;
    unsigned char src_flag_type = param1 & 255;
    int64_t src_plr_start, src_plr_end;
    if (get_players_range(src_plr_range, &src_plr_start, &src_plr_end) < 0)
    {
        WARNLOG("Invalid player range %" PRId64 " in VALUE command %" PRId64 ".",(int64_t)src_plr_range,(int64_t)Cmd_COMPUTE_FLAG);
        return;
    }
    int64_t sum = 0;
    for (int64_t i = src_plr_start; i < src_plr_end; i++)
    {
        sum += get_condition_value(i, src_flag_type, param3);
    }
    for (int64_t i = plr_start; i < plr_end; i++)
    {
        int64_t current_flag_val = get_condition_value(i, flag_type, param2);
        int64_t computed = sum;
        if (operation == SOpr_INCREASE) computed = current_flag_val + sum;
        if (operation == SOpr_DECREASE) computed = current_flag_val - sum;
        if (operation == SOpr_MULTIPLY) computed = current_flag_val * sum;
        SCRIPTDBG(7,"Changing player%" PRId64 "'s %" PRId64 " flag from %" PRId64 " to %" PRId64 " based on flag of type %" PRIu64 ".",
            (int64_t)(i), (int64_t)(param2), (int64_t)(current_flag_val), (int64_t)(computed), (uint64_t)(src_flag_type));
        set_variable(i, flag_type, param2, computed);
    }
}

void bonus_level_time_check(const struct ScriptLine *scline)
{
    int64_t game_turns = scline->np[0];
    int64_t real = scline->np[1];
    if (game_turns < 0)
    {
        SCRPTERRLOG("Bonus time must be nonnegative");
        return;
    }
    command_add_value(scline->command, ALL_PLAYERS, game_turns, real, 0);
}

void add_gold_to_player_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    int64_t amount = scline->np[1];
    command_add_value(scline->command, plr_range_id, amount, 0, 0);
}

/** ALLY_PLAYERS: level files before version 1 have no third value: the players ally. */
void ally_players_check(const struct ScriptLine *scline)
{
    TbBool ally = (scline->file_version > 0) ? scline->np[2] : true;
    // Verify enemy player
    int64_t plr2_id = get_players_range_single(scline->np[1]);
    if (plr2_id < 0) {
        SCRPTERRLOG("Given second player is not supported in this command");
        return;
    }
    command_add_value(scline->command, scline->np[0], plr2_id, ally, 0);
}

/** ALLY_PLAYERS: the value's first bit allies, the second locks the alliance; after the last player, the doors'
 *  navigation (allies pass each other's doors). */
void ally_players_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    int64_t i = context->player_idx;
    int64_t param1 = value->longs[0];
    int64_t param2 = value->longs[1];
    set_ally_with_player(i, param1, (param2 & 1) ? true : false);
    set_ally_with_player(param1, i, (param2 & 1) ? true : false);
    set_player_ally_locked(i, param1, (param2 & 2) ? true : false);
    set_player_ally_locked(param1, i, (param2 & 2) ? true : false);
    if (kfx_config_state.conf.rules[i].gameplay.allies_share_vision)
    {
        ui_panel_map_update(0, 0, kfx_sim_state.map_subtiles_x + 1, kfx_sim_state.map_subtiles_y + 1);
    }
    if (legacy_value_last_player(context))
        update_navigation_around_all_doors();
}

/** BONUS_LEVEL_TIME: the countdown in turns (0: none), and from level version 1 whether it shows real time. */
void bonus_level_time_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    int64_t param1 = context->value->longs[0];
    int64_t param2 = context->value->longs[1];
    if (param1 > 0) {
        kfx_game_state.bonus_time = get_gameturn() + param1;
        set_flag(kfx_game_state.flags_gui,GGUI_CountdownTimer);
    } else {
        kfx_game_state.bonus_time = 0;
        clear_flag(kfx_game_state.flags_gui,GGUI_CountdownTimer);
    }
    if (kfx_game_state.level_file_version > 0)
    {
        kfx_game_state.timer_real = (TbBool)param2;
    }
    else
    {
        kfx_game_state.timer_real = false;
    }
}

void add_gold_to_player_process(struct ScriptContext *context)
{
    int64_t i = context->player_idx;
    int64_t param1 = context->value->longs[0];
    if (param1 > SENSIBLE_GOLD)
    {
        param1 = SENSIBLE_GOLD;
        if (legacy_value_first_player(context))
            SCRPTWRNLOG("Gold added to player %" PRId64 " reduced to %" PRId64, (int64_t)context->value->plyr_range, (int64_t)(SENSIBLE_GOLD));
    }
    if (param1 >= 0)
    {
        player_add_offmap_gold(i, param1);
    }
    else
    {
        take_money_from_dungeon(i, -param1, 0);
    }
}

void start_money_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    int64_t gold_val = scline->np[1];
    int64_t plr_start;
    int64_t plr_end;
    if (get_players_range(plr_range_id, &plr_start, &plr_end) < 0)
    {
        SCRPTERRLOG("Given owning player range %" PRId64 " is not supported in this command", (int64_t)plr_range_id);
        return;
  }
  if (get_script_current_condition() != CONDITION_ALWAYS)
  {
    SCRPTWRNLOG("Start money set inside conditional block; condition ignored");
  }
  for (int64_t i = plr_start; i < plr_end; i++)
  {
      if (gold_val > SENSIBLE_GOLD)
      {
          gold_val = SENSIBLE_GOLD;
          SCRPTWRNLOG("Gold added to player %" PRId64 " reduced to %" PRId64, (int64_t)plr_range_id, (int64_t)(SENSIBLE_GOLD));
      }
      player_add_offmap_gold(i, gold_val);
  }
}

void win_game_check(const struct ScriptLine *scline)
{
    if (get_script_current_condition() == CONDITION_ALWAYS)
    {
        SCRPTERRLOG("Command WIN GAME found with no condition");
    }
    if (kfx_game_state.script.win_conditions_num >= WIN_CONDITIONS_COUNT)
    {
        SCRPTERRLOG("Too many WIN GAME conditions in script");
        return;
    }
    kfx_game_state.script.win_conditions[kfx_game_state.script.win_conditions_num] = get_script_current_condition();
    kfx_game_state.script.win_conditions_num++;
}

void lose_game_check(const struct ScriptLine *scline)
{
  if (get_script_current_condition() == CONDITION_ALWAYS)
  {
    SCRPTERRLOG("Command LOSE GAME found with no condition");
  }
  if (kfx_game_state.script.lose_conditions_num >= WIN_CONDITIONS_COUNT)
  {
    SCRPTERRLOG("Too many LOSE GAME conditions in script");
    return;
  }
  kfx_game_state.script.lose_conditions[kfx_game_state.script.lose_conditions_num] = get_script_current_condition();
  kfx_game_state.script.lose_conditions_num++;
}

void run_after_victory_check(const struct ScriptLine *scline)
{
    if (scline->np[0] == 1)
    {
        kfx_sim_state.run_after_victory = true;
    }
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
