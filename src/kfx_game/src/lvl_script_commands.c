/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lvl_script_commands.c
 *     Commands that can be used by level script
 * @author   KeeperFX Team
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "main_game.h"
#include <inttypes.h>
#include <math.h>
#include <string.h>
#include "bflib_sound.h"
#include "bflib_sndlib.h"
#include "vidfade.h"
#include "config.h"
#include "config_crtrmodel.h"
#include "config_keeperfx.h"
#include "config_effects.h"
#include "config_lenses.h"
#include "config_magic.h"
#include "config_players.h"
#include "config_powerhands.h"
#include "config_settings.h"
#include "config_spritecolors.h"
#include "config_trapdoor.h"
#include "config_translation.h"
#include "console_cmd.h"
#include "config_rules.h"
#include "creature_instances.h"
#include "creature_states.h"
#include "creature_states_mood.h"
#include "creature_states_pray.h"
#include "custom_sprites.h"
#include "dungeon_data.h"
#include "lens_api.h"
#include "lvl_script_commands.h"
#include "vidmode.h"
#include "game_saves.h"
#include "lvl_script_conditions.h"
#include "lvl_script_lib.h"
#include "map_blocks.h"
#include "player_instances.h"
#include "player_utils.h"
#include "power_hand.h"
#include "power_specials.h"
#include "lvl_script_value.h"
#include "magic_powers.h"
#include "player_computer.h"
#include "room_entrance.h"
#include "room_library.h"
#include "room_util.h"
#include "sounds.h"
#include "spdigger_stack.h"
#include "thing_data.h"
#include "thing_effects.h"
#include "thing_navigate.h"
#include "thing_objects.h"
#include "thing_physics.h"
#include "player_availability.h"

#include "ports/script_port.h"
#include "ports/ui_port.h"
#include "list_walk.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif


#define MAX_CONFIG_VALUES 4

const struct CommandDesc subfunction_desc[] = {
    {"RANDOM",                     "Aaaaaaaa", Cmd_RANDOM, NULL, NULL},
    {"DRAWFROM",                   "Aaaaaaaa", Cmd_DRAWFROM, NULL, NULL},
    {"IMPORT",                     "PA      ", Cmd_IMPORT, NULL, NULL},
    {NULL,                         "        ", Cmd_NONE, NULL, NULL},
  };

const struct NamedCommand controls_variable_desc[] = {
    {"TOTAL_DIGGERS",               SVar_CONTROLS_TOTAL_DIGGERS},
    {"TOTAL_CREATURES",             SVar_CONTROLS_TOTAL_CREATURES},
    {"TOTAL_DOORS",                 SVar_TOTAL_DOORS},
    {"TOTAL_TRAPS",                 SVar_TOTAL_TRAPS},
    {"TOTAL_AREA",                  SVar_TOTAL_AREA},
    {"GOOD_CREATURES",              SVar_CONTROLS_GOOD_CREATURES},
    {"EVIL_CREATURES",              SVar_CONTROLS_EVIL_CREATURES},
    {NULL,                           0},
};

const struct NamedCommand available_variable_desc[] = {
    {"TOTAL_CREATURES",             SVar_AVAILABLE_TOTAL_CREATURES},
    {"TOTAL_DOORS",                 SVar_AVAILABLE_TOTAL_DOORS},
    {"TOTAL_TRAPS",                 SVar_AVAILABLE_TOTAL_TRAPS},
    {"TOTAL_AREA",                  SVar_TOTAL_AREA},
    {NULL,                           0},
};

const struct NamedCommand comparison_desc[] = {
  {"==",     MOp_EQUAL},
  {"!=",     MOp_NOT_EQUAL},
  {"<",      MOp_SMALLER},
  {">",      MOp_GREATER},
  {"<=",     MOp_SMALLER_EQ},
  {">=",     MOp_GREATER_EQ},
  {NULL,     0},
};

const struct NamedCommand timer_desc[] = {
  {"TIMER0", 0},
  {"TIMER1", 1},
  {"TIMER2", 2},
  {"TIMER3", 3},
  {"TIMER4", 4},
  {"TIMER5", 5},
  {"TIMER6", 6},
  {"TIMER7", 7},
  {NULL,     0},
};

const struct NamedCommand flag_desc[] = {
  {"FLAG0",  0},
  {"FLAG1",  1},
  {"FLAG2",  2},
  {"FLAG3",  3},
  {"FLAG4",  4},
  {"FLAG5",  5},
  {"FLAG6",  6},
  {"FLAG7",  7},
  {NULL,     0},
};

const struct NamedCommand hand_rule_desc[] = {
  {"ALWAYS",                HandRule_Always},
  {"AGE_LOWER",             HandRule_AgeLower},
  {"AGE_HIGHER",            HandRule_AgeHigher},
  {"LEVEL_LOWER",           HandRule_LvlLower},
  {"LEVEL_HIGHER",          HandRule_LvlHigher},
  {"AT_ACTION_POINT",       HandRule_AtActionPoint},
  {"AFFECTED_BY",           HandRule_AffectedBy},
  {"WANDERING",             HandRule_Wandering},
  {"WORKING",               HandRule_Working},
  {"FIGHTING",              HandRule_Fighting},
  {"DROPPED_TIME_HIGHER",   HandRule_DroppedTimeHigher},
  {"DROPPED_TIME_LOWER",    HandRule_DroppedTimeLower},
  {"BLOCKED_FOR_PICKUP",    HandRule_BlockedPickup},
  {NULL,                    0},
};

const struct NamedCommand rule_slot_desc[] = {
  {"RULE0",  0},
  {"RULE1",  1},
  {"RULE2",  2},
  {"RULE3",  3},
  {"RULE4",  4},
  {"RULE5",  5},
  {"RULE6",  6},
  {"RULE7",  7},
  {NULL,     0},
};

const struct NamedCommand rule_action_desc[] = {
  {"DENY",      HandRuleAction_Deny},
  {"ALLOW",     HandRuleAction_Allow},
  {"ENABLE",    HandRuleAction_Enable},
  {"DISABLE",   HandRuleAction_Disable},
  {NULL,     0},
};

const struct NamedCommand hero_objective_desc[] = {
  {"STEAL_GOLD",           CHeroTsk_StealGold},
  {"STEAL_SPELLS",         CHeroTsk_StealSpells},
  {"ATTACK_ENEMIES",       CHeroTsk_AttackEnemies},
  {"ATTACK_DUNGEON_HEART", CHeroTsk_AttackDnHeart},
  {"SNIPE_DUNGEON_HEART",  CHeroTsk_SnipeDnHeart},
  {"ATTACK_ROOMS",         CHeroTsk_AttackRooms},
  {"SABOTAGE_ROOMS",       CHeroTsk_SabotageRooms},
  {"DEFEND_PARTY",         CHeroTsk_DefendParty},
  {"DEFEND_LOCATION",      CHeroTsk_DefendSpawn},
  {"DEFEND_HEART",         CHeroTsk_DefendHeart},
  {"DEFEND_ROOMS",         CHeroTsk_DefendRooms},
  {NULL,                   0},
};

const struct NamedCommand msgtype_desc[] = {
  {"SPEECH",           1},
  {"SOUND",            2},
  {NULL,               0},
};

const struct NamedCommand tendency_desc[] = {
  {"IMPRISON",         1},
  {"FLEE",             2},
  {NULL,               0},
};

const struct NamedCommand creature_select_criteria_desc[] = {
  {"MOST_EXPERIENCED",     CSelCrit_MostExperienced},
  {"MOST_EXP_WANDERING",   CSelCrit_MostExpWandering},
  {"MOST_EXP_WORKING",     CSelCrit_MostExpWorking},
  {"MOST_EXP_FIGHTING",    CSelCrit_MostExpFighting},
  {"LEAST_EXPERIENCED",    CSelCrit_LeastExperienced},
  {"LEAST_EXP_WANDERING",  CSelCrit_LeastExpWandering},
  {"LEAST_EXP_WORKING",    CSelCrit_LeastExpWorking},
  {"LEAST_EXP_FIGHTING",   CSelCrit_LeastExpFighting},
  {"NEAR_OWN_HEART",       CSelCrit_NearOwnHeart},
  {"NEAR_ENEMY_HEART",     CSelCrit_NearEnemyHeart},
  {"ON_ENEMY_GROUND",      CSelCrit_OnEnemyGround},
  {"ON_FRIENDLY_GROUND",   CSelCrit_OnFriendlyGround},
  {"ON_NEUTRAL_GROUND",    CSelCrit_OnNeutralGround},
  {"ANYWHERE",             CSelCrit_Any},
  {NULL,                   0},
};

const struct NamedCommand on_experience_desc[] = {
  {"SizeIncreaseOnExp",            1},
  {"PayIncreaseOnExp",             2},
  {"SpellDamageIncreaseOnExp",     3},
  {"RangeIncreaseOnExp",           4},
  {"JobValueIncreaseOnExp",        5},
  {"HealthIncreaseOnExp",          6},
  {"StrengthIncreaseOnExp",        7},
  {"DexterityIncreaseOnExp",       8},
  {"DefenseIncreaseOnExp",         9},
  {"LoyaltyIncreaseOnExp",        10},
  {"ExpForHittingIncreaseOnExp",  11},
  {"TrainingCostIncreaseOnExp",   12},
  {"ScavengingCostIncreaseOnExp", 13},
  {NULL,                           0},
};

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

/**
 * Text names of groups of GUI Buttons.
 */
const struct NamedCommand gui_button_group_desc[] = {
  {"MINIMAP",         GID_MINIMAP_AREA},
  {"TABS",            GID_TABS_AREA},
  {"INFO",            GID_INFO_PANE},
  {"ROOM",            GID_ROOM_PANE},
  {"POWER",           GID_POWER_PANE},
  {"TRAP",            GID_TRAP_PANE},
  {"DOOR",            GID_DOOR_PANE},
  {"CREATURE",        GID_CREATR_PANE},
  {"MESSAGE",         GID_MESSAGE_AREA},
  {NULL,               0},
};

/**
 * Text names of campaign flags.
 */
const struct NamedCommand campaign_flag_desc[] = {
  {"CAMPAIGN_FLAG0",  0},
  {"CAMPAIGN_FLAG1",  1},
  {"CAMPAIGN_FLAG2",  2},
  {"CAMPAIGN_FLAG3",  3},
  {"CAMPAIGN_FLAG4",  4},
  {"CAMPAIGN_FLAG5",  5},
  {"CAMPAIGN_FLAG6",  6},
  {"CAMPAIGN_FLAG7",  7},
  {NULL,     0},
};

const struct NamedCommand script_operator_desc[] = {
  {"SET",         1},
  {"INCREASE",    2},
  {"DECREASE",    3},
  {"MULTIPLY",    4},
  {NULL,          0},
};

const struct NamedCommand script_boolean_desc[] = {
    {"0",        0},
    {"OFF",      0},
    {"NO",       0},
    {"FALSE",    0},
    {"DISABLE",  0},
    {"DISABLED", 0},
    {"1",        1},
    {"ON",       1},
    {"YES",      1},
    {"TRUE",     1},
    {"ENABLE",   1},
    {"ENABLED",  1},
    {NULL,       0},
};

const struct NamedCommand variable_desc[] = {
    {"MONEY",                       SVar_MONEY},
    {"GAME_TURN",                   SVar_GAME_TURN},
    {"TOTAL_DIGGERS",               SVar_TOTAL_DIGGERS},
    {"TOTAL_CREATURES",             SVar_TOTAL_CREATURES},
    {"TOTAL_RESEARCH",              SVar_TOTAL_RESEARCH},
    {"TOTAL_DOORS",                 SVar_TOTAL_DOORS},
    {"TOTAL_TRAPS",                 SVar_TOTAL_TRAPS},
    {"TOTAL_AREA",                  SVar_TOTAL_AREA},
    {"TOTAL_CREATURES_LEFT",        SVar_TOTAL_CREATURES_LEFT},
    {"CREATURES_ANNOYED",           SVar_CREATURES_ANNOYED},
    {"BATTLES_LOST",                SVar_BATTLES_LOST},
    {"BATTLES_WON",                 SVar_BATTLES_WON},
    {"ROOMS_DESTROYED",             SVar_ROOMS_DESTROYED},
    {"SPELLS_STOLEN",               SVar_SPELLS_STOLEN},
    {"TIMES_BROKEN_INTO",           SVar_TIMES_BROKEN_INTO},
    {"GOLD_POTS_STOLEN",            SVar_GOLD_POTS_STOLEN},
    {"HEART_HEALTH",                SVar_HEART_HEALTH},
    {"GHOSTS_RAISED",               SVar_GHOSTS_RAISED},
    {"SKELETONS_RAISED",            SVar_SKELETONS_RAISED},
    {"VAMPIRES_RAISED",             SVar_VAMPIRES_RAISED},
    {"CREATURES_CONVERTED",         SVar_CREATURES_CONVERTED},
    {"EVIL_CREATURES_CONVERTED",    SVar_EVIL_CREATURES_CONVERTED},
    {"GOOD_CREATURES_CONVERTED",    SVar_GOOD_CREATURES_CONVERTED},
    {"TIMES_ANNOYED_CREATURE",      SVar_TIMES_ANNOYED_CREATURE},
    {"TIMES_TORTURED_CREATURE",     SVar_TIMES_TORTURED_CREATURE},
    {"TOTAL_DOORS_MANUFACTURED",    SVar_TOTAL_DOORS_MANUFACTURED},
    {"TOTAL_TRAPS_MANUFACTURED",    SVar_TOTAL_TRAPS_MANUFACTURED},
    {"TOTAL_MANUFACTURED",          SVar_TOTAL_MANUFACTURED},
    {"TOTAL_TRAPS_USED",            SVar_TOTAL_TRAPS_USED},
    {"TOTAL_DOORS_USED",            SVar_TOTAL_DOORS_USED},
    {"KEEPERS_DESTROYED",           SVar_KEEPERS_DESTROYED},
    {"CREATURES_SACRIFICED",        SVar_CREATURES_SACRIFICED},
    {"CREATURES_FROM_SACRIFICE",    SVar_CREATURES_FROM_SACRIFICE},
    {"TIMES_LEVELUP_CREATURE",      SVar_TIMES_LEVELUP_CREATURE},
    {"TOTAL_SALARY",                SVar_TOTAL_SALARY},
    {"CURRENT_SALARY",              SVar_CURRENT_SALARY},
    //{"TIMER",                     SVar_TIMER},
    {"DUNGEON_DESTROYED",           SVar_DUNGEON_DESTROYED},
    {"TOTAL_GOLD_MINED",            SVar_TOTAL_GOLD_MINED},
    //{"FLAG",                      SVar_FLAG},
    //{"ROOM",                      SVar_ROOM_SLABS},
    {"DOORS_DESTROYED",             SVar_DOORS_DESTROYED},
    {"CREATURES_SCAVENGED_LOST",    SVar_CREATURES_SCAVENGED_LOST},
    {"CREATURES_SCAVENGED_GAINED",  SVar_CREATURES_SCAVENGED_GAINED},
    {"ALL_DUNGEONS_DESTROYED",      SVar_ALL_DUNGEONS_DESTROYED},
    //{"DOOR",                      SVar_DOOR_NUM},
    {"GOOD_CREATURES",              SVar_GOOD_CREATURES},
    {"EVIL_CREATURES",              SVar_EVIL_CREATURES},
    {"TRAPS_SOLD",                  SVar_TRAPS_SOLD},
    {"DOORS_SOLD",                  SVar_DOORS_SOLD},
    {"MANUFACTURED_SOLD",           SVar_MANUFACTURED_SOLD},
    {"MANUFACTURE_GOLD",            SVar_MANUFACTURE_GOLD},
    {"TOTAL_SCORE",                 SVar_TOTAL_SCORE},
    {"BONUS_TIME",                  SVar_BONUS_TIME},
    {"CREATURES_TRANSFERRED",       SVar_CREATURES_TRANSFERRED},
    {"ACTIVE_BATTLES",              SVar_ACTIVE_BATTLES},
    {"VIEW_TYPE",                   SVar_VIEW_TYPE},
    {"TOTAL_SLAPS",                 SVar_TOTAL_SLAPS},
    {"SCORE",                       SVar_SCORE},
    {"PLAYER_SCORE",                SVar_PLAYER_SCORE},
    {"MANAGE_SCORE",                SVar_MANAGE_SCORE},
    {"CONTROLLED_THING",            SVar_CONTROLLED_THING},
    {NULL,                          0},
};


const struct NamedCommand dk1_variable_desc[] = {
    {"MONEY",                       SVar_MONEY},
    {"GAME_TURN",                   SVar_GAME_TURN},
    {"TOTAL_IMPS",                  SVar_TOTAL_DIGGERS},
    {"TOTAL_CREATURES",             SVar_CONTROLS_TOTAL_CREATURES},
    {"TOTAL_RESEARCH",              SVar_TOTAL_RESEARCH},
    {"TOTAL_DOORS",                 SVar_TOTAL_DOORS},
    {"TOTAL_AREA",                  SVar_TOTAL_AREA},
    {"TOTAL_CREATURES_LEFT",        SVar_TOTAL_CREATURES_LEFT},
    {"CREATURES_ANNOYED",           SVar_CREATURES_ANNOYED},
    {"BATTLES_LOST",                SVar_BATTLES_LOST},
    {"BATTLES_WON",                 SVar_BATTLES_WON},
    {"ROOMS_DESTROYED",             SVar_ROOMS_DESTROYED},
    {"SPELLS_STOLEN",               SVar_SPELLS_STOLEN},
    {"TIMES_BROKEN_INTO",           SVar_TIMES_BROKEN_INTO},
    {"GOLD_POTS_STOLEN",            SVar_GOLD_POTS_STOLEN},
    //{"TIMER",                     SVar_TIMER},
    {"DUNGEON_DESTROYED",           SVar_DUNGEON_DESTROYED},
    {"TOTAL_GOLD_MINED",            SVar_TOTAL_GOLD_MINED},
    //{"FLAG",                      SVar_FLAG},
    //{"ROOM",                      SVar_ROOM_SLABS},
    {"DOORS_DESTROYED",             SVar_DOORS_DESTROYED},
    {"CREATURES_SCAVENGED_LOST",    SVar_CREATURES_SCAVENGED_LOST},
    {"CREATURES_SCAVENGED_GAINED",  SVar_CREATURES_SCAVENGED_GAINED},
    {"ALL_DUNGEONS_DESTROYED",      SVar_ALL_DUNGEONS_DESTROYED},
    //{"DOOR",                      SVar_DOOR_NUM},
    {NULL,                           0},
};

const struct NamedCommand fill_desc[] = {
  {"NONE",          FillIterType_NoFill},
  {"MATCH",         FillIterType_Match},
  {"FLOOR",         FillIterType_Floor},
  {"BRIDGE",        FillIterType_FloorBridge},
  {NULL,            0},
};

const struct NamedCommand locked_desc[] = {
  {"LOCKED", 1},
  {"UNLOCKED", 0},
  {NULL, 0}
};

const struct NamedCommand is_free_desc[] = {
  {"PAID", 0},
  {"FREE", 1},
  {NULL, 0}
};

const struct NamedCommand orientation_desc[] = {
  {"North",     ANGLE_NORTH},
  {"NorthEast", ANGLE_NORTHEAST},
  {"East",      ANGLE_EAST},
  {"SouthEast", ANGLE_SOUTHEAST},
  {"South",     ANGLE_SOUTH},
  {"SouthWest", ANGLE_SOUTHWEST},
  {"West",      ANGLE_WEST},
  {"NorthWest", ANGLE_NORTHWEST},
  {NULL, 0}
};

const struct NamedCommand texture_pack_desc[] = {
  {"NONE",         0},
  {"STANDARD",     1},
  {"ANCIENT",      2},
  {"WINTER",       3},
  {"SNAKE_KEY",    4},
  {"STONE_FACE",   5},
  {"VOLUPTUOUS",   6},
  {"BIG_BREASTS",  6},
  {"ROUGH_ANCIENT",7},
  {"SKULL_RELIEF", 8},
  {"DESERT_TOMB",  9},
  {"GYPSUM",       10},
  {"LILAC_STONE",  11},
  {"SWAMP_SERPENT",12},
  {"LAVA_CAVERN",  13},
  {"LATERITE_CAVERN",14},
  {NULL,           0},
};

// Variables that could be set
/** SACRIFICED[creature] and REWARDED[creature], for the variable parsers. Returns whether the name is one. */
static TbBool parse_creature_bracket_varib(const char *varib_name, int64_t *varib_id, int64_t *varib_type)
{
    char c;
    int len = 0; // sscanf %n target
    char arg[MAX_TEXT_LENGTH];
    if (2 == sscanf(varib_name, "SACRIFICED[%n%[^]]%c", &len, arg, &c) && (c == ']'))
    {
        *varib_id = get_id(creature_desc, arg);
        *varib_type = SVar_SACRIFICED;
        return true;
    }
    if (2 == sscanf(varib_name, "REWARDED[%n%[^]]%c", &len, arg, &c) && (c == ']'))
    {
        *varib_id = get_id(creature_desc, arg);
        *varib_type = SVar_REWARDED;
        return true;
    }
    return false;
}

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

TbBool parse_get_varib(const char *varib_name, int64_t *varib_id, int64_t *varib_type, int64_t lvl_file_version)
{
    char c;
    int len = 0; // sscanf %n target
    char arg[MAX_TEXT_LENGTH];

    if (lvl_file_version > 0)
    {
        *varib_type = get_id(variable_desc, varib_name);
    } else
    {
        *varib_type = get_id(dk1_variable_desc, varib_name);
    }
    if (*varib_type == -1)
      *varib_id = -1;
    else
      *varib_id = 0;
    if (*varib_id == -1)
    {
      *varib_id = get_id(creature_desc, varib_name);
      *varib_type = SVar_CREATURE_NUM;
    }
    //TODO: list of lambdas
    if (*varib_id == -1)
    {
      *varib_id = get_id(room_desc, varib_name);
      *varib_type = SVar_ROOM_SLABS;
    }
    if (*varib_id == -1)
    {
      *varib_id = get_id(timer_desc, varib_name);
      *varib_type = SVar_TIMER;
    }
    if (*varib_id == -1)
    {
      *varib_id = get_id(flag_desc, varib_name);
      *varib_type = SVar_FLAG;
    }
    if (*varib_id == -1)
    {
      *varib_id = get_id(door_desc, varib_name);
      *varib_type = SVar_DOOR_NUM;
    }
    if (*varib_id == -1)
    {
        *varib_id = get_id(trap_desc, varib_name);
        *varib_type = SVar_TRAP_NUM;
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
        else if (2 == sscanf(varib_name, "TRAP%" SCNd64 "_ACTIVATE%c", varib_id, &c) && (c == 'D'))
        {
            // activateD
            *varib_type = SVar_TRAP_ACTIVATED;
        }
        else if (2 == sscanf(varib_name, "KEEPERS_DESTROYED[%n%[^]]%c", &len, arg, &c) && (c == ']'))
        {
            *varib_id = get_id(player_desc, arg);
            if (*varib_id == -1)
            {
                *varib_id = get_id(cmpgn_human_player_options, arg);
            }
            *varib_type = SVar_DESTROYED_KEEPER;
        }
        else
        {
            // A failed BOXn_/TRAPn_ match has already stored n (pass 3 finding F15)
            *varib_id = -1;
            parse_creature_bracket_varib(varib_name, varib_id, varib_type);
        }
    }
    if (*varib_id == -1)
    {
      SCRPTERRLOG("Unknown variable name, '%s'", varib_name);
      return false;
    }
    return true;
}

/** parse_get_varib() for a level script line: an unknown variable also goes to the compat report. */
TbBool script_parse_get_varib(const char *varib_name, int64_t *varib_id, int64_t *varib_type, int64_t lvl_file_version)
{
    if (parse_get_varib(varib_name, varib_id, varib_type, lvl_file_version))
        return true;
    compat_report_add(CompatIssue_ScriptName, varib_name, NULL, text_line_number);
    return false;
}

/** parse_set_varib() for a level script line: an unknown variable also goes to the compat report. */
TbBool script_parse_set_varib(const char *varib_name, int64_t *varib_id, int64_t *varib_type)
{
    if (parse_set_varib(varib_name, varib_id, varib_type))
        return true;
    compat_report_add(CompatIssue_ScriptName, varib_name, NULL, text_line_number);
    return false;
}

static void set_config_check(const struct NamedFieldSet* named_fields_set, const struct ScriptLine* scline, const char* src_str)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    const char* id_str = scline->tp[0];
    const char* property = scline->tp[1];
    const char* valuestrings[MAX_CONFIG_VALUES] = {scline->tp[2],scline->tp[3],scline->tp[4],scline->tp[5]};

    int64_t id = get_id(named_fields_set->names, id_str);
    if (id == -1)
    {
        SCRPTERRLOG("Unknown %s, '%s'",named_fields_set->block_basename, id_str);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    int64_t property_id = get_named_field_id(named_fields_set->named_fields, property);
    if (property_id == -1)
    {
        SCRPTERRLOG("Unknown property, '%s'", property);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    if (id > named_fields_set->max_count)
    {
        SCRPTERRLOG("'%s%" PRId64 "' is out of range",named_fields_set->block_basename, (int64_t)(id));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    const struct NamedField* field = &named_fields_set->named_fields[property_id];

    char concatenated_values[MAX_TEXT_LENGTH];
    if (field->argnum == -1)
    {
        snprintf(concatenated_values, sizeof(concatenated_values), "%s %s %s %s", scline->tp[2],scline->tp[3],scline->tp[4],scline->tp[5]);
        value->longs[1] = parse_named_field_value(field, concatenated_values,named_fields_set,id,src_str,ccf_SplitExecution|ccf_DuringLevel);
    }
    else
    {
        for (size_t i = 0; i < MAX_CONFIG_VALUES; i++)
        {
            if(valuestrings[i][0] == '\0')
            {
                break;
            }

            if( named_fields_set->named_fields[property_id + i].name == NULL || (strcmp(named_fields_set->named_fields[property_id + i].name, named_fields_set->named_fields[property_id].name) != 0))
            {
                SCRPTERRLOG("more values then expected for property: '%s' '%s'", property, valuestrings[i]);
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
            else if (valuestrings[i][0] == '\0')
            {
                break;
            }
            value->longs[1 + i] = parse_named_field_value(&named_fields_set->named_fields[property_id + i], valuestrings[i],named_fields_set,id,src_str,ccf_SplitExecution|ccf_DuringLevel);
        }
    }

    value->shorts[0] = id;
    value->shorts[1] = property_id;
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void set_config_process(const struct NamedFieldSet* named_fields_set, struct ScriptContext* context, const char* src_str)
{
    int64_t id          = context->value->shorts[0];
    int64_t property_id = context->value->shorts[1];

    for (size_t i = 0; i < MAX_CONFIG_VALUES; i++)
    {
        if( named_fields_set->named_fields[property_id + i].name == NULL ||
            (strcmp(named_fields_set->named_fields[property_id + i].name, named_fields_set->named_fields[property_id].name) != 0))
        {
            return;
        }
        else
        {
            assign_named_field_value(&named_fields_set->named_fields[property_id + i],context->value->longs[i+1],named_fields_set,id,src_str,ccf_SplitExecution|ccf_DuringLevel);
        }
    }
}

static void add_to_party_check(const struct ScriptLine *scline)
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

static void delete_from_party_check(const struct ScriptLine *scline)
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

static TbBool get_custom_icon_from_value(const char* txt, int16_t* icon_idx);
static TbBool get_custom_ensign_from_value(const char* txt, int16_t* ensign_id);

static void display_objective_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, ALL_PLAYERS);

    TextStringId  msg_num = get_string_id_by_alias(scline->tp[0]);

    if ((msg_num < 0))
    {
        SCRPTERRLOG("Invalid TEXT number");
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    MapSubtlCoord x = 0, y = 0;
    TbMapLocation location = 0;
    if ((msg_num < 0))
    {
        SCRPTERRLOG("Invalid TEXT number");
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    if (scline->command == Cmd_DISPLAY_OBJECTIVE)
    {
        const char *where = scline->tp[1];
        if (!get_map_location_id(where, &location))
        {
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }
    else
    {
        x = scline->np[1];
        y = scline->np[2];
    }
    value->shorts[0] = msg_num;
    value->ulongs[1] = location;
    value->shorts[4] = x;
    value->shorts[5] = y;
    value->shorts[6] = -1;

    const char *icon = (scline->command == Cmd_DISPLAY_OBJECTIVE)
        ? scline->tp[2] : scline->tp[3];
    if (icon[0] != '\0' && !get_custom_icon_from_value(icon, &value->shorts[6]))
    {
        SCRPTERRLOG("Invalid custom icon (%s)", icon);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    PROCESS_SCRIPT_VALUE(scline->command);
}

static void display_objective_process(struct ScriptContext *context)
{
    set_general_objective_with_icon(context->value->shorts[0],
        context->player_idx,
        context->value->ulongs[1],
        context->value->shorts[4],
        context->value->shorts[5],
        context->value->shorts[6]);
}

static void display_player_objective_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[1]);
    TextStringId  msg_num = get_string_id_by_alias(scline->tp[0]);
    MapSubtlCoord x = 0, y = 0;
    TbMapLocation location = 0;
    if ((msg_num < 0))
    {
        DEALLOCATE_SCRIPT_VALUE
        SCRPTERRLOG("Invalid TEXT number");
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    if (scline->command == Cmd_DISPLAY_PLAYER_OBJECTIVE)
    {
        const char* where = scline->tp[2];
        if (!get_map_location_id(where, &location))
        {
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }
    else
    {
        x = scline->np[2];
        y = scline->np[3];
    }
    value->shorts[0] = msg_num;
    value->ulongs[1] = location;
    value->shorts[4] = x;
    value->shorts[5] = y;
    value->shorts[6] = -1;

    const char *icon = (scline->command == Cmd_DISPLAY_PLAYER_OBJECTIVE)
        ? scline->tp[3] : scline->tp[4];
    if (icon[0] != '\0' && !get_custom_icon_from_value(icon, &value->shorts[6]))
    {
        SCRPTERRLOG("Invalid custom icon (%s)", icon);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    PROCESS_SCRIPT_VALUE(scline->command);
}

/**
 * What QUICK_OBJECTIVE, QUICK_INFORMATION, QUICK_PLAYER_OBJECTIVE and QUICK_PLAYER_INFORMATION (and their
 * _WITH_POS forms) share: stores the message text and fills value's message number, location and position
 * (no icon: -1). `arg` is 1 for the player forms, whose arguments after the number are one further on;
 * `at_location` is the plain form (a location name) rather than _WITH_POS (x and y). Returns false, after
 * logging, when the line is refused; the caller then gives the script value back.
 */
static TbBool quick_message_fill(const struct ScriptLine* scline, struct ScriptValue* value, int arg,
    TbBool at_location, const char* kind, const char* kind_capitalised)
{
    int64_t idx = scline->np[0];
    if ((idx < 0) || (idx >= QUICK_MESSAGES_COUNT))
    {
        SCRPTERRLOG("Invalid %s ID number (%" PRId64 ")", kind, (int64_t)(idx));
        return false;
    }
    const char* msgtext = scline->tp[1 + arg];

    if (strlen(msgtext) >= MESSAGE_TEXT_LEN)
    {
        SCRPTWRNLOG("%s TEXT too long; truncating to %" PRId64 " characters", kind_capitalised, (int64_t)(MESSAGE_TEXT_LEN - 1));
    }
    if ((kfx_sim_state.quick_messages[idx][0] != '\0') && (strcmp(kfx_sim_state.quick_messages[idx], msgtext) != 0))
    {
        SCRPTWRNLOG("Quick Message no %" PRId64 " overwritten by different text", (int64_t)(idx));
    }
    MapSubtlCoord x = 0, y = 0;
    TbMapLocation location = 0;
    const char* where = "ALL_PLAYERS";

    snprintf(kfx_sim_state.quick_messages[idx], MESSAGE_TEXT_LEN, "%s", msgtext);

    if (at_location)
    {
        if (scline->tp[2 + arg][0] != '\0')
        {
            where = scline->tp[2 + arg];
        }
    }
    else
    {
        x = scline->np[2 + arg];
        y = scline->np[3 + arg];
    }
    if (!get_map_location_id(where, &location))
    {
        SCRPTERRLOG("Invalid location (%s)", scline->tp[2 + arg]);
        return false;
    }

    value->shorts[0] = idx;
    value->ulongs[1] = location;
    value->shorts[4] = x;
    value->shorts[5] = y;
    value->shorts[6] = -1;
    return true;
}

static void quick_objective_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, ALL_PLAYERS);
    const TbBool at_location = (scline->command == Cmd_QUICK_OBJECTIVE);
    if (!quick_message_fill(scline, value, 0, at_location, "objective", "Objective"))
    {
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    const char *icon = at_location ? scline->tp[3] : scline->tp[4];
    if (icon[0] != '\0' && !get_custom_icon_from_value(icon, &value->shorts[6]))
    {
        SCRPTERRLOG("Invalid custom icon (%s)", icon);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    PROCESS_SCRIPT_VALUE(scline->command);
}

static void quick_objective_process(struct ScriptContext* context)
{
    process_objective_with_icon(
        kfx_sim_state.quick_messages[context->value->shorts[0] % QUICK_MESSAGES_COUNT],
        context->player_idx,
        context->value->ulongs[1],
        context->value->shorts[4],
        context->value->shorts[5],
        context->value->shorts[6]);
}

static void quick_player_objective_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[1]);
    const TbBool at_location = (scline->command == Cmd_QUICK_PLAYER_OBJECTIVE);
    if (!quick_message_fill(scline, value, 1, at_location, "objective", "Objective"))
    {
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    const char *icon = at_location ? scline->tp[4] : scline->tp[5];
    if (icon[0] != '\0' && !get_custom_icon_from_value(icon, &value->shorts[6]))
    {
        SCRPTERRLOG("Invalid custom icon (%s)", icon);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    PROCESS_SCRIPT_VALUE(scline->command);
}

static TbBool get_custom_icon_from_value(const char* txt, int16_t* icon_idx)
{
    if (txt[0] == '\0')
        return false;

    int64_t idx = get_icon_id(txt);
    *icon_idx = idx;
    return true;
}

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

static void quick_information_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, ALL_PLAYERS);
    const TbBool at_location = (scline->command == Cmd_QUICK_INFORMATION);
    if (!quick_message_fill(scline, value, 0, at_location, "information", "Information"))
    {
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    // Only the plain form has an icon parameter.
    if (at_location)
    {
        if (scline->tp[3][0] != '\0' && !get_custom_icon_from_value(scline->tp[3], &value->shorts[6]))
        {
            SCRPTERRLOG("Invalid custom icon (%s)", scline->tp[3]);
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void quick_information_process(struct ScriptContext* context)
{
    set_quick_information_with_icon(context->value->shorts[0], context->player_idx, context->value->ulongs[1], context->value->shorts[4], context->value->shorts[5], context->value->shorts[6]);
}

static void quick_player_information_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[1]);
    // No icon parameter in either form.
    if (!quick_message_fill(scline, value, 1, (scline->command == Cmd_QUICK_PLAYER_INFORMATION), "information", "Information"))
    {
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void display_information_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, ALL_PLAYERS);

    TextStringId  msg_num = get_string_id_by_alias(scline->tp[0]);
    if ((msg_num < 0))
    {
        DEALLOCATE_SCRIPT_VALUE
        SCRPTERRLOG("Invalid TEXT number");
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    MapSubtlCoord x = 0,y = 0;
    TbMapLocation location = 0;
    const char* where = "ALL_PLAYERS";

    
    if (scline->command == Cmd_DISPLAY_INFORMATION)
    {
        if (scline->tp[1][0] != '\0')
        {
            where = scline->tp[1];
        }
    }
    else
    {
        x = scline->np[1];
        y = scline->np[2];
    }
    if (!get_map_location_id(where, &location))
    {
        SCRPTERRLOG("Invalid location (%s)", scline->tp[1]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    value->shorts[0] = msg_num;
    value->ulongs[1] = location;
    value->shorts[4] = x;
    value->shorts[5] = y;
    value->shorts[6] = -1;
    if (scline->tp[2][0] != '\0' && !get_custom_icon_from_value(scline->tp[2], &value->shorts[6]))
    {
        SCRPTERRLOG("Invalid custom icon (%s)", scline->tp[2]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void display_information_process(struct ScriptContext* context)
{
    set_general_information_with_icon(context->value->shorts[0], context->player_idx,
        context->value->ulongs[1], context->value->shorts[4], context->value->shorts[5], context->value->shorts[6]);
}

static void display_player_information_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[1]);

    TextStringId msg_num = get_string_id_by_alias(scline->tp[0]);
    if ((msg_num < 0))
    {
        DEALLOCATE_SCRIPT_VALUE
        SCRPTERRLOG("Invalid TEXT number");
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    MapSubtlCoord x = 0, y = 0;
    TbMapLocation location = 0;
    const char* where = "ALL_PLAYERS";


    if (scline->command == Cmd_DISPLAY_PLAYER_INFORMATION)
    {
        if (scline->tp[2][0] != '\0')
        {
            where = scline->tp[2];
        }
    }
    else
    {
        x = scline->np[2];
        y = scline->np[3];
    }
    if (!get_map_location_id(where, &location))
    {
        SCRPTERRLOG("Invalid location (%s)", scline->tp[2]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    value->shorts[0] = msg_num;
    value->ulongs[1] = location;
    value->shorts[4] = x;
    value->shorts[5] = y;
    value->shorts[6] = -1;
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void tag_map_rect_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);

    MapSlabCoord x = scline->np[1];
    MapSlabCoord y = scline->np[2];
    MapSlabDelta width;
    MapSlabDelta height;

    if (scline->np[3] != '\0')
        width = scline->np[3];
    else
        width = 1;
    if (scline->np[4] != '\0')
        height = scline->np[4];
    else
        height = 1;

    MapSlabCoord start_x = x - (width / 2);
    MapSlabCoord end_x = x + (width / 2) + (width & 1);
    MapSlabCoord start_y = y - (height / 2);
    MapSlabCoord end_y = y + (height / 2) + (height & 1);

    if (start_x < 0)
    {
        SCRPTWRNLOG("Starting X slab '%" PRId64 "' (from %" PRId64 "-%" PRId64 "/2) is out of range, fixing it to '0'.", (int64_t)(start_x), (int64_t)(x), (int64_t)(width));
        start_x = 0;
    }
    else if (start_x > kfx_sim_state.map_tiles_x)
    {
        SCRPTWRNLOG("Starting X slab '%" PRId64 "' (from %" PRId64 "-%" PRId64 "/2) is out of range, fixing it to '%" PRId64 "'.", (int64_t)(start_x), (int64_t)(x), (int64_t)(width), (int64_t)(kfx_sim_state.map_tiles_x));
        start_x = kfx_sim_state.map_tiles_x;
    }
    if (end_x < 0)
    {
        SCRPTWRNLOG("Ending X slab '%" PRId64 "' (from %" PRId64 "+%" PRId64 "/2) is out of range, fixing it to '0'.", (int64_t)(end_x), (int64_t)(x), (int64_t)(width));
        end_x = 0;
    }
    else if (end_x > kfx_sim_state.map_tiles_x)
    {
        SCRPTWRNLOG("Ending X slab '%" PRId64 "' (from %" PRId64 "+%" PRId64 "/2) is out of range, fixing it to '%" PRId64 "'.", (int64_t)(end_x), (int64_t)(x), (int64_t)(width), (int64_t)(kfx_sim_state.map_tiles_x));
        end_x = kfx_sim_state.map_tiles_x;
    }
    if (start_y < 0)
    {
        SCRPTWRNLOG("Starting Y slab '%" PRId64 "' (from %" PRId64 "-%" PRId64 "/2) is out of range, fixing it to '0'.", (int64_t)(start_y), (int64_t)(y), (int64_t)(height));
        start_y = 0;
    }
    else if (start_y > kfx_sim_state.map_tiles_y)
    {
        SCRPTWRNLOG("Starting Y slab '%" PRId64 "' (from %" PRId64 "-%" PRId64 "/2) is out of range, fixing it to '%" PRId64 "'.", (int64_t)(start_y), (int64_t)(y), (int64_t)(height), (int64_t)(kfx_sim_state.map_tiles_y));
        start_y = kfx_sim_state.map_tiles_y;
    }
    if (end_y < 0)
    {
        SCRPTWRNLOG("Ending Y slab '%" PRId64 "' (from %" PRId64 "+%" PRId64 "/2) is out of range, fixing it to '0'.", (int64_t)(end_y), (int64_t)(y), (int64_t)(height));
        end_y = 0;
    }
    else if (end_y > kfx_sim_state.map_tiles_y)
    {
        SCRPTWRNLOG("Ending Y slab '%" PRId64 "' (from %" PRId64 "+%" PRId64 "/2) is out of range, fixing it to '%" PRId64 "'.", (int64_t)(end_y), (int64_t)(y), (int64_t)(height), (int64_t)(kfx_sim_state.map_tiles_y));
        end_y = kfx_sim_state.map_tiles_y;
    }
    if ((x < 0) || (x > kfx_sim_state.map_tiles_x) || (y < 0) || (y > kfx_sim_state.map_tiles_y))
    {
        SCRPTERRLOG("Tag slabs out of range, trying to set tag center point to (%" PRId64 ",%" PRId64 ") on map that's %" PRId64 "x%" PRId64 " slabs", (int64_t)(x), (int64_t)(y), (int64_t)(kfx_sim_state.map_tiles_x), (int64_t)(kfx_sim_state.map_tiles_y));
        DEALLOCATE_SCRIPT_VALUE
            return;
    }
    value->shorts[1] = start_x;
    value->shorts[2] = end_x;
    value->shorts[3] = start_y;
    value->shorts[4] = end_y;

    PROCESS_SCRIPT_VALUE(scline->command);
}

static void tag_map_rect_process(struct ScriptContext* context)
{
    MapSlabCoord start_x = context->value->shorts[1];
    MapSlabCoord end_x = context->value->shorts[2];
    MapSlabCoord start_y = context->value->shorts[3];
    MapSlabCoord end_y = context->value->shorts[4];

    for (int64_t x = start_x; x < end_x; x++)
    {
        for (int64_t y = start_y; y < end_y; y++)
        {
            MapSubtlCoord stl_x = slab_subtile_center(x);
            MapSubtlCoord stl_y = slab_subtile_center(y);

            if (subtile_is_diggable_for_player(context->player_idx, stl_x, stl_y, false))
            {
                tag_blocks_for_digging_in_area(stl_x, stl_y, context->player_idx);
            }
        }
    }
}

static void untag_map_rect_process(struct ScriptContext* context)
{
    MapSlabCoord start_x = context->value->shorts[1];
    MapSlabCoord end_x = context->value->shorts[2];
    MapSlabCoord start_y = context->value->shorts[3];
    MapSlabCoord end_y = context->value->shorts[4];

    for (int64_t x = start_x; x < end_x; x++)
    {
        for (int64_t y = start_y; y < end_y; y++)
        {
            MapSubtlCoord stl_x = slab_subtile_center(x);
            MapSubtlCoord stl_y = slab_subtile_center(y);

            if (subtile_is_diggable_for_player(context->player_idx, stl_x, stl_y, false))
            {
                untag_blocks_for_digging_in_area(stl_x, stl_y, context->player_idx);
            }
        }
    }
}


static void conceal_map_rect_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    TbBool conceal_all = false;

    if (scline->np[5] == -1)
    {
        if ((strcmp(scline->tp[5], "") == 0))
        {
            conceal_all = false;
        }
        else if ((strcmp(scline->tp[5], "ALL") == 0))
        {
            conceal_all = true;
        }
        else
        {
            SCRPTWRNLOG("Hide value \"%s\" not recognized", scline->tp[5]);
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }
    else
    {
        conceal_all = scline->np[5];
    }

    MapSubtlCoord x = scline->np[1];
    MapSubtlCoord y = scline->np[2];
    MapSubtlDelta width = scline->np[3];
    MapSubtlDelta height = scline->np[4];

    MapSubtlCoord start_x = x - (width / 2);
    MapSubtlCoord end_x = x + (width / 2) + (width & 1);
    MapSubtlCoord start_y = y - (height / 2);
    MapSubtlCoord end_y = y + (height / 2) + (height & 1);

    if (start_x < 0)
    {
        SCRPTWRNLOG("Starting X coordinate '%" PRId64 "' (from %" PRId64 "-%" PRId64 "/2) is out of range, fixing it to '0'.", (int64_t)(start_x),(int64_t)(x),(int64_t)(width));
        start_x = 0;
    }
    else if (start_x > kfx_sim_state.map_subtiles_x)
    {
        SCRPTWRNLOG("Starting X coordinate '%" PRId64 "' (from %" PRId64 "-%" PRId64 "/2) is out of range, fixing it to '%" PRId64 "'.", (int64_t)(start_x), (int64_t)(x), (int64_t)(width), (int64_t)(kfx_sim_state.map_subtiles_x));
        start_x = kfx_sim_state.map_subtiles_x;
    }
    if (end_x < 0)
    {
        SCRPTWRNLOG("Ending X coordinate '%" PRId64 "' (from %" PRId64 "+%" PRId64 "/2) is out of range, fixing it to '0'.", (int64_t)(end_x), (int64_t)(x), (int64_t)(width));
        end_x = 0;
    }
    else if (end_x > kfx_sim_state.map_subtiles_x)
    {
        SCRPTWRNLOG("Ending X coordinate '%" PRId64 "' (from %" PRId64 "+%" PRId64 "/2) is out of range, fixing it to '%" PRId64 "'.", (int64_t)(end_x), (int64_t)(x), (int64_t)(width), (int64_t)(kfx_sim_state.map_subtiles_x));
        end_x = kfx_sim_state.map_subtiles_x;
    }
    if (start_y < 0)
    {
        SCRPTWRNLOG("Starting Y coordinate '%" PRId64 "' (from %" PRId64 "-%" PRId64 "/2) is out of range, fixing it to '0'.", (int64_t)(start_y), (int64_t)(y), (int64_t)(height));
        start_y = 0;
    }
    else if (start_y > kfx_sim_state.map_subtiles_y)
    {
        SCRPTWRNLOG("Starting Y coordinate '%" PRId64 "' (from %" PRId64 "-%" PRId64 "/2) is out of range, fixing it to '%" PRId64 "'.", (int64_t)(start_y), (int64_t)(y), (int64_t)(height), (int64_t)(kfx_sim_state.map_subtiles_y));
        start_y = kfx_sim_state.map_subtiles_y;
    }
    if (end_y < 0)
    {
        SCRPTWRNLOG("Ending Y coordinate '%" PRId64 "' (from %" PRId64 "+%" PRId64 "/2) is out of range, fixing it to '0'.", (int64_t)(end_y), (int64_t)(y), (int64_t)(height));
        end_y = 0;
    }
    else if (end_y > kfx_sim_state.map_subtiles_y)
    {
        SCRPTWRNLOG("Ending Y coordinate '%" PRId64 "' (from %" PRId64 "+%" PRId64 "/2) is out of range, fixing it to '%" PRId64 "'.", (int64_t)(end_y), (int64_t)(y), (int64_t)(height), (int64_t)(kfx_sim_state.map_subtiles_y));
        end_y = kfx_sim_state.map_subtiles_y;
    }
    if ((x < 0) || (x > kfx_sim_state.map_subtiles_x) || (y < 0) || (y > kfx_sim_state.map_subtiles_y))
    {
        SCRPTERRLOG("Conceal coordinates out of range, trying to set conceal center point to (%" PRId64 ",%" PRId64 ") on map that's %" PRId64 "x%" PRId64 " subtiles", (int64_t)(x), (int64_t)(y), (int64_t)(kfx_sim_state.map_subtiles_x), (int64_t)(kfx_sim_state.map_subtiles_y));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->shorts[1] = start_x;
    value->shorts[2] = end_x;
    value->shorts[3] = start_y;
    value->shorts[4] = end_y;
    value->shorts[5] = conceal_all;

    PROCESS_SCRIPT_VALUE(scline->command);
}

static void conceal_map_rect_process(struct ScriptContext *context)
{
    MapSubtlCoord start_x = context->value->shorts[1];
    MapSubtlCoord end_x = context->value->shorts[2];
    MapSubtlCoord start_y = context->value->shorts[3];
    MapSubtlCoord end_y = context->value->shorts[4];
    TbBool conceal_all = context->value->shorts[5];
    conceal_map_area(context->player_idx, start_x, end_x, start_y, end_y, conceal_all);
}

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

static void special_transfer_creature_process(struct ScriptContext* context)
{
    if (my_player_number == context->player_idx)
    {
        struct Thing *heartng = get_player_soul_container(context->player_idx);
        struct PlayerInfo* player = get_my_player();
        start_transfer_creature(player, heartng);
    }
}

static void special_transfer_creature_check(const struct ScriptLine* scline)
{
    command_add_value(Cmd_USE_SPECIAL_TRANSFER_CREATURE, scline->np[0],0,0,0);
}

static void script_transfer_creature_check(const struct ScriptLine* scline)
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

static void script_transfer_creature_process(struct ScriptContext* context)
{
    script_transfer_creature(context->player_idx, context->value->longs[0], context->value->longs[1], context->value->longs[2]);
}

static void change_creatures_annoyance_check(const struct ScriptLine* scline)
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

static void change_creatures_annoyance_process(struct ScriptContext* context)
{
    script_change_creatures_annoyance(context->player_idx, context->value->longs[0], context->value->longs[1], context->value->longs[2]);
}

static void set_trap_configuration_check(const struct ScriptLine* scline)
{
    set_config_check(&trapdoor_trap_named_fields_set, scline, "SET_TRAP_CONFIGURATION");
}

static void set_room_configuration_check(const struct ScriptLine* scline)
{
    set_config_check(&terrain_room_named_fields_set, scline, "SET_ROOM_CONFIGURATION");
}

static void set_hand_rule_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);

    const char *param_name = scline->tp[5];
    int64_t crtr_id = parse_creature_name(scline->tp[1]);
    int64_t hr_action, hr_slot, hr_type, param;

    if (crtr_id == CREATURE_NONE)
    {
        SCRPTERRLOG("Unknown creature, '%s'", scline->tp[1]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    hr_slot = get_id(rule_slot_desc, scline->tp[2]);
    if (hr_slot == -1) {
        SCRPTERRLOG("Invalid hand rule slot: '%s'", scline->tp[2]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    hr_action = get_id(rule_action_desc, scline->tp[3]);
    if (hr_action == -1) {
        SCRPTERRLOG("Invalid hand rule action: '%s'", scline->tp[3]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    if (hr_action == HandRuleAction_Allow || hr_action == HandRuleAction_Deny)
    {
        hr_type = get_id(hand_rule_desc, scline->tp[4]);
        if (hr_type == -1) {
            SCRPTERRLOG("Invalid hand rule: '%s'", scline->tp[4]);
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
        param = hr_type == HandRule_AffectedBy ? 0 : script_atol(param_name);
        if (hr_type == HandRule_AtActionPoint && action_point_number_to_index(param) == -1)
        {
            SCRPTERRLOG("Unknown action point param for hand rule: '%" PRId64 "'", (int64_t)(param));
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
        if (hr_type == HandRule_AffectedBy)
        {
            int64_t mag_id = get_id(spell_desc, param_name);
            if (mag_id == -1)
            {
                SCRPTERRLOG("Unknown magic, '%s'", param_name);
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
            param = mag_id;
        }
    } else
    {
        hr_type = 0;
        param = 0;
    }

    value->shorts[0] = crtr_id;
    value->shorts[1] = hr_action;
    value->shorts[2] = hr_slot;
    value->shorts[3] = hr_type;
    value->shorts[4] = param;
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void move_creature_check(const struct ScriptLine* scline)
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

    const char *effect_name = scline->tp[5];
    int64_t effct_id = 0;
    if (scline->tp[5][0] != '\0')
    {
        effct_id = get_rid(effect_desc, effect_name);
        if (effct_id == -1)
        {
            if (parameter_is_number(effect_name))
            {
                effct_id = atoi(effect_name);
            }
            else
            {
                SCRPTERRLOG("Unrecognised effect: %s", effect_name);
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
        }
    }
    else
    {
        effct_id = -1;
    }
    value->ulongs[0] = location;
    value->longs[1] = select_id;
    value->shorts[4] = effct_id;
    value->bytes[10] = count;
    value->bytes[11] = crmodel;

    PROCESS_SCRIPT_VALUE(scline->command);
}

static void count_creatures_at_action_point_check(const struct ScriptLine* scline)
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

static void copy_creature_type_check(const struct ScriptLine* scline)
{
    script_copy_creature_type(scline->np[0],scline->tp[1]);
    return;
}

static void new_creature_type_check(const struct ScriptLine* scline)
{
    script_new_creature_type(scline->tp[0]);
    return;
}

static void new_room_type_check(const struct ScriptLine* scline)
{
    if (kfx_config_state.conf.slab_conf.room_types_count >= TERRAIN_ITEMS_MAX - 1)
    {
        SCRPTERRLOG("Cannot increase room count for room type '%s', already at maximum %" PRId64 " rooms.", scline->tp[0], (int64_t)(TERRAIN_ITEMS_MAX - 1));
        return;
    }

    SCRPTLOG("Adding room type %s and increasing 'RoomsCount to %" PRId64, scline->tp[0], (int64_t)(kfx_config_state.conf.slab_conf.room_types_count + 1));
    kfx_config_state.conf.slab_conf.room_types_count++;

    struct RoomConfigStats* roomst;
    int64_t i = kfx_config_state.conf.slab_conf.room_types_count - 1;

    roomst = get_room_kind_stats(i);
    memset(roomst->code_name, 0, COMMAND_WORD_LEN);
    snprintf(roomst->code_name, COMMAND_WORD_LEN, "%s", scline->tp[0]);
    roomst->name_stridx = GUIStr_Empty;
    roomst->tooltip_stridx = GUIStr_Empty;
    roomst->creature_creation_model = 0;
    roomst->bigsym_sprite_idx = 0;
    roomst->medsym_sprite_idx = 0;
    roomst->pointer_sprite_idx = 0;
    roomst->panel_tab_idx = 0;
    roomst->ambient_snd_smp_id = 0;
    memset(&roomst->msg_needed, 0, sizeof(SpeechRef));
    memset(&roomst->msg_too_small, 0, sizeof(SpeechRef));
    memset(&roomst->msg_no_route, 0, sizeof(SpeechRef));
    roomst->roles = RoRoF_None;
    roomst->cost = 0;
    roomst->health = 0;
    room_desc[i].name = roomst->code_name;
    room_desc[i].num = i;
}

static void new_object_type_check(const struct ScriptLine* scline)
{
    if (kfx_config_state.conf.object_conf.object_types_count >= OBJECT_TYPES_MAX-1)
    {
        SCRPTERRLOG("Cannot increase object count for object type '%s', already at maximum %" PRId64 " objects.", scline->tp[0], (int64_t)(OBJECT_TYPES_MAX-1));
        return;
    }

    SCRPTLOG("Adding object type %s and increasing 'ObjectsCount to %" PRId64, scline->tp[0], (int64_t)(kfx_config_state.conf.object_conf.object_types_count + 1));
    kfx_config_state.conf.object_conf.object_types_count++;

    int64_t tmodel = kfx_config_state.conf.object_conf.object_types_count -1;
    struct ObjectConfigStats* objst = get_object_model_stats(tmodel);
    memset(objst->code_name, 0, COMMAND_WORD_LEN);
    snprintf(objst->code_name, COMMAND_WORD_LEN, "%s", scline->tp[0]);
    objst->map_icon = 0;
    objst->hand_icon = 0;
    objst->genre = 0;
    objst->draw_class = ODC_Default;
    object_desc[tmodel].name = objst->code_name;
    object_desc[tmodel].num = tmodel;
}

static void new_trap_type_check(const struct ScriptLine* scline)
{
    if (kfx_config_state.conf.trapdoor_conf.trap_types_count >= TRAPDOOR_TYPES_MAX)
    {
        SCRPTERRLOG("Cannot increase trap count for trap type '%s', already at maximum %" PRId64 " traps.", scline->tp[0], (int64_t)(TRAPDOOR_TYPES_MAX));
        return;
    }
    SCRPTLOG("Adding trap type %s and increasing 'TrapsCount to %" PRId64, scline->tp[0], (int64_t)(kfx_config_state.conf.trapdoor_conf.trap_types_count + 1));
    kfx_config_state.conf.trapdoor_conf.trap_types_count++;
    int64_t i = kfx_config_state.conf.trapdoor_conf.trap_types_count-1;
    struct TrapConfigStats *trapst = get_trap_model_stats(i);
    memset(trapst->code_name, 0, COMMAND_WORD_LEN);
    snprintf(trapst->code_name, COMMAND_WORD_LEN, "%s", scline->tp[0]);
    trapst->name_stridx = GUIStr_Empty;
    trapst->tooltip_stridx = GUIStr_Empty;
    trapst->bigsym_sprite_idx = 0;
    trapst->medsym_sprite_idx = 0;
    trapst->pointer_sprite_idx = 0;
    trapst->panel_tab_idx = 0;
    trapst->manufct_level = 0;
    trapst->manufct_required = 0;
    trapst->shots = 0;
    trapst->shots_delay = 0;
    trapst->initial_delay = 0;
    trapst->trigger_type = 0;
    trapst->activation_type = 0;
    trapst->created_itm_model = 0;
    trapst->hit_type = 0;
    trapst->hidden = true;
    trapst->slappable = 0;
    trapst->detect_invisible = true;
    trapst->notify = false;
    trapst->place_on_bridge = false;
    trapst->place_on_subtile = false;
    trapst->instant_placement = false;
    trapst->remove_once_depleted = false;
    trapst->health = 1;
    trapst->destructible = 0;
    trapst->unstable = 0;
    trapst->destroyed_effect = -39;
    trapst->size_xy = 0;
    trapst->size_z = 0;
    trapst->sprite_anim_idx = 0;
    trapst->attack_sprite_anim_idx = 0;
    trapst->recharge_sprite_anim_idx = 0;
    trapst->sprite_size_max = 0;
    trapst->anim_speed = 0;
    trapst->unanimated = 0;
    trapst->unshaded = 0;
    trapst->random_start_frame = 0;
    trapst->light_radius = 0;
    trapst->light_intensity = 0;
    trapst->light_flag = 0;
    trapst->transparency_flag = 0;
    trapst->shot_shift_x = 0;
    trapst->shot_shift_y = 0;
    trapst->shot_shift_z = 0;
    trapst->shotvector.x = 0;
    trapst->shotvector.y = 0;
    trapst->shotvector.z = 0;
    trapst->selling_value = 0;
    trapst->unsellable = false;
    trapst->place_sound_idx = 117;
    trapst->trigger_sound_idx = 176;
    trap_desc[i].name = trapst->code_name;
    trap_desc[i].num = i;
    create_manufacture_array_from_trapdoor_data();
}

static void set_trap_configuration_process(struct ScriptContext *context)
{
    set_config_process(&trapdoor_trap_named_fields_set, context, "SET_TRAP_CONFIGURATION");
}

static void set_room_configuration_process(struct ScriptContext *context)
{
    set_config_process(&terrain_room_named_fields_set, context, "SET_ROOM_CONFIGURATION");
}

static void set_hand_rule_process(struct ScriptContext* context)
{
    PlayerNumber plyr_idx = context->player_idx;
    int64_t crtr_id = context->value->shorts[0];
    int64_t hand_rule_action = context->value->shorts[1];
    int64_t hand_rule_slot = context->value->shorts[2];
    int64_t hand_rule_type = context->value->shorts[3];
    int64_t param = context->value->shorts[4];

    script_set_hand_rule(plyr_idx, crtr_id, hand_rule_action, hand_rule_slot, hand_rule_type, param);
}

static void move_creature_process(struct ScriptContext* context)
{
    TbMapLocation location = context->value->ulongs[0];
    int64_t select_id = context->value->longs[1];
    int64_t effect_id = context->value->shorts[4];
    int64_t count = context->value->bytes[10];
    int64_t crmodel = context->value->bytes[11];
    PlayerNumber plyr_idx = context->player_idx;

    script_move_creature_with_criteria(plyr_idx, crmodel, select_id, location, effect_id, count);
}

static void count_creatures_at_action_point_process(struct ScriptContext* context)
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

static void set_door_configuration_check(const struct ScriptLine* scline)
{
    set_config_check(&trapdoor_door_named_fields_set, scline, "SET_DOOR_CONFIGURATION");
}

static void set_door_configuration_process(struct ScriptContext *context)
{
    set_config_process(&trapdoor_door_named_fields_set, context, "SET_DOOR_CONFIGURATION");
}

static void create_effect_at_pos_process(struct ScriptContext* context)
{
    struct Coord3d pos;
    set_coords_to_subtile_center(&pos, context->value->shorts[1], context->value->shorts[2], 0);
    pos.z.val += get_floor_height(pos.x.stl.num, pos.y.stl.num);
    script_create_effect(&pos,context->value->shorts[0],context->value->longs[2]);

}

static void create_effect_process(struct ScriptContext *context)
{
    struct Coord3d pos;
    if (!get_coords_at_location(&pos, context->value->ulongs[1],true))
    {
        SCRPTWRNLOG("Could not find location %" PRIu64 " to create effect", (uint64_t)(context->value->ulongs[1]));
        return;
    }
    script_create_effect(&pos,context->value->shorts[0],context->value->longs[2]);

}

static void set_heart_health_check(const struct ScriptLine *scline)
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

static void set_heart_health_process(struct ScriptContext *context)
{
    struct Thing* heartng = get_player_soul_container(context->player_idx);
    if (thing_exists(heartng))
    {
        heartng->health = (int64_t)context->value->longs[1];
    }
}

static void add_heart_health_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    value->longs[1] = scline->np[1];
    value->longs[2] = scline->np[2];
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void add_heart_health_process(struct ScriptContext *context)
{
    PlayerNumber plyr_idx = context->player_idx;
    HitPoints healthdelta = context->value->longs[1];
    TbBool warn_on_damage = context->value->longs[2];

    add_heart_health(plyr_idx,healthdelta,warn_on_damage);
}

static void lock_possession_check(const struct ScriptLine* scline)
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

static void lock_possession_process(struct ScriptContext* context)
{
    struct PlayerInfo *player = get_player(context->player_idx);
    if (player_exists(player))
    {
        player->possession_lock = context->value->chars[1];
    }
}

static void heart_lost_quick_objective_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    if ((scline->np[0] < 0) || (scline->np[0] >= QUICK_MESSAGES_COUNT))
    {
        SCRPTERRLOG("Invalid QUICK OBJECTIVE number (%" PRId64 ")", (int64_t)(scline->np[0]));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    if (strlen(scline->tp[1]) >= MESSAGE_TEXT_LEN)
    {
        SCRPTWRNLOG("Objective TEXT too long; truncating to %" PRId64 " characters", (int64_t)(MESSAGE_TEXT_LEN-1));
    }
    if ((kfx_sim_state.quick_messages[scline->np[0]][0] != '\0') && (strcmp(kfx_sim_state.quick_messages[scline->np[0]],scline->tp[1]) != 0))
    {
        SCRPTWRNLOG("Quick Objective no %" PRId64 " overwritten by different text", (int64_t)(scline->np[0]));
    }
    snprintf(kfx_sim_state.quick_messages[scline->np[0]], MESSAGE_TEXT_LEN, "%s", scline->tp[1]);

    TbMapLocation location = 0;
    if (scline->tp[2][0] != '\0')
    {
        get_map_location_id(scline->tp[2], &location);
    }

    value->longs[0] = scline->np[0];
    value->ulongs[2] = location;
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void heart_lost_quick_objective_process(struct ScriptContext *context)
{
    kfx_sim_state.heart_lost_display_message = true;
    kfx_game_state.heart_lost_quick_message = true;
    kfx_game_state.heart_lost_message_id = context->value->longs[0];
    kfx_game_state.heart_lost_message_target = context->value->longs[2];
}

static void heart_lost_objective_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    value->longs[0] = scline->np[0];
    TbMapLocation location = 0;
    if (scline->tp[1][0] != '\0')
    {
        get_map_location_id(scline->tp[1], &location);
    }
    value->ulongs[1] = location;
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void heart_lost_objective_process(struct ScriptContext *context)
{
    kfx_sim_state.heart_lost_display_message = true;
    kfx_game_state.heart_lost_quick_message = false;
    kfx_game_state.heart_lost_message_id = context->value->longs[0];
    kfx_game_state.heart_lost_message_target = context->value->longs[1];
}

static void set_door_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    int64_t doorAction = get_id(locked_desc, scline->tp[0]);
    if (doorAction == -1)
    {
        SCRPTERRLOG("Set Door state %s not recognized", scline->tp[0]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    if (slab_coords_invalid(scline->np[1], scline->np[2]))
    {
        SCRPTERRLOG("Invalid slab coordinates: %" PRId64 ", %" PRId64, (int64_t)(scline->np[1]), (int64_t)(scline->np[2]));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    value->shorts[0] = doorAction;
    value->shorts[1] = scline->np[1];
    value->shorts[2] = scline->np[2];
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void set_door_process(struct ScriptContext* context)
{
    struct Thing* doortng = get_door_for_position(slab_subtile_center(context->value->shorts[1]), slab_subtile_center(context->value->shorts[2]));
    if (!thing_is_invalid(doortng))
    {
        switch (context->value->shorts[0])
        {
        case 0:
            unlock_door(doortng);
            break;
        case 1:
            lock_door(doortng);
            break;
        }
    }
}

static void place_door_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    const char* doorname = scline->tp[1];
    int64_t door_id = get_id(door_desc, doorname);

    if (door_id == -1)
    {
        SCRPTERRLOG("Unknown door, '%s'", doorname);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    if (slab_coords_invalid(scline->np[2], scline->np[3]))
    {
        SCRPTERRLOG("Invalid slab coordinates: %" PRId64 ", %" PRId64, (int64_t)(scline->np[2]), (int64_t)(scline->np[3]));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    int64_t locked = scline->np[4];
    if (locked == -1)
    {
        locked = get_id(locked_desc, scline->tp[4]);
        if (locked == -1)
        {
            SCRPTERRLOG("Door locked state %s not recognized", scline->tp[4]);
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }

    int64_t free = scline->np[5];
    if (free == -1)
    {
        free = get_id(is_free_desc, scline->tp[5]);
        if (free == -1)
        {
            SCRPTERRLOG("Place Door free state '%s' not recognized", scline->tp[5]);
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }

    value->shorts[1] = door_id;
    value->shorts[2] = scline->np[2];
    value->shorts[3] = scline->np[3];
    value->shorts[4] = locked;
    value->shorts[5] = free;
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void place_door_process(struct ScriptContext* context)
{
    ThingModel doorkind = context->value->shorts[1];
    MapSlabCoord slb_x = context->value->shorts[2];
    MapSlabCoord slb_y = context->value->shorts[3];
    TbBool locked = context->value->shorts[4];
    TbBool free = context->value->shorts[5];
    PlayerNumber plyridx = context->player_idx;

    script_place_door(plyridx, doorkind, slb_x, slb_y, locked, free);
}

static void place_trap_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    const char* trapname = scline->tp[1];
    int64_t trap_id = get_id(trap_desc, trapname);

    if (trap_id == -1)
    {
        SCRPTERRLOG("Unknown trap, '%s'", trapname);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    if (subtile_coords_invalid(scline->np[2], scline->np[3]))
    {
        SCRPTERRLOG("Invalid subtile coordinates: %" PRId64 ", %" PRId64, (int64_t)(scline->np[2]), (int64_t)(scline->np[3]));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    int64_t free = scline->np[4];
    if (free == -1)
    {
        free = get_id(is_free_desc, scline->tp[4]);
        if (free == -1)
        {
            SCRPTERRLOG("Place Trap free state '%s' not recognized", scline->tp[4]);
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }

    value->shorts[1] = trap_id;
    value->shorts[2] = scline->np[2];
    value->shorts[3] = scline->np[3];
    value->shorts[4] = free;
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void place_trap_process(struct ScriptContext* context)
{
    ThingModel trapkind = context->value->shorts[1];
    MapSubtlCoord stl_x = context->value->shorts[2];
    MapSubtlCoord stl_y = context->value->shorts[3];
    TbBool free = context->value->shorts[4];
    PlayerNumber plyridx = context->player_idx;
    script_place_trap(plyridx, trapkind, stl_x, stl_y, free);

}

static void create_effects_line_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);

    value->longs[0] = scline->np[0]; // AP `from`
    value->longs[1] = scline->np[1]; // AP `to`
    value->chars[8] = scline->np[2]; // curvature
    value->bytes[9] = scline->np[3]; // spatial stepping
    value->bytes[10] = scline->np[4]; // temporal stepping
    const char* effect_name = scline->tp[5];

    EffectOrEffElModel effct_id = effect_or_effect_element_id(effect_name);
    if (effct_id == 0)
    {
        SCRPTERRLOG("Unrecognised effect: %s", effect_name);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    value->shorts[6] = effct_id; // effect

    PROCESS_SCRIPT_VALUE(scline->command);
}

static void create_effects_line_process(struct ScriptContext *context)
{
    TbMapLocation from = context->value->longs[0];
    TbMapLocation to   = context->value->longs[1];
    char curvature = context->value->chars[8];
    unsigned char spatial_stepping = context->value->bytes[9];
    unsigned char temporal_stepping = context->value->bytes[10];
    EffectOrEffElModel effct_id = context->value->shorts[6];

    create_effects_line(from, to, curvature, spatial_stepping, temporal_stepping, effct_id);
}

static void set_object_configuration_check(const struct ScriptLine *scline)
{
    set_config_check(&objects_named_fields_set, scline, "SET_OBJECT_CONFIGURATION");
}

enum CreatureConfiguration
{
    CrtConf_NONE,
    CrtConf_ATTRIBUTES,
    CrtConf_ATTRACTION,
    CrtConf_ANNOYANCE,
    CrtConf_SENSES,
    CrtConf_APPEARANCE,
    CrtConf_EXPERIENCE,
    CrtConf_JOBS,
    CrtConf_SPRITES,
    CrtConf_SOUNDS,
    CrtConf_LISTEND
};

/** SET_CREATURE_CONFIGURATION's keys, block by block, in the order they're looked up. */
static const struct {
    const struct NamedCommand *keys;
    int64_t block;
} creature_configuration_blocks[] = {
    {creatmodel_attributes_commands, CrtConf_ATTRIBUTES},
    {creatmodel_jobs_commands, CrtConf_JOBS},
    {creatmodel_attraction_commands, CrtConf_ATTRACTION},
    {creatmodel_sounds_commands, CrtConf_SOUNDS},
    {creature_graphics_desc, CrtConf_SPRITES},
    {creatmodel_annoyance_commands, CrtConf_ANNOYANCE},
    {creatmodel_experience_commands, CrtConf_EXPERIENCE},
    {creatmodel_appearance_commands, CrtConf_APPEARANCE},
    {creatmodel_senses_commands, CrtConf_SENSES},
};

/**
 * A job-flags value of SET_CREATURE_CONFIGURATION (the Jobs keys, AngerJobs): a job name from `jobs` or a
 * number, then how to apply it (empty: set, 1: add, 0: clear). Returns false, after logging, when refused.
 */
static TbBool creature_configuration_job_value(const struct ScriptLine* scline, const struct NamedCommand *jobs,
    int64_t *config_value_primary, int64_t *config_value_secondary)
{
    if (parameter_is_number(scline->tp[2]))
    {
        *config_value_primary = atoi(scline->tp[2]);
        if ((*config_value_primary < 0) || (*config_value_primary > SHRT_MAX))
        {
            SCRPTERRLOG("Job value %" PRId64 " out of range `0~%" PRId64 "`.", (int64_t)(*config_value_primary), (int64_t)(SHRT_MAX));
            return false;
        }
    }
    else
    {
        *config_value_primary = get_id(jobs, scline->tp[2]);
        if (*config_value_primary > SHRT_MAX)
        {
            SCRPTERRLOG("Job %s not supported", creature_job_code_name(*config_value_primary));
            return false;
        }
        else if (*config_value_primary < 0)
        {
            SCRPTERRLOG("Job %s is out of range or doesn't exist.", scline->tp[2]);
            return false;
        }
    }
    // value 2: 'empty' is 'set', '1' is 'add', '0' is 'clear'.
    if (scline->tp[3][0] != '\0')
    {
        *config_value_secondary = atoi(scline->tp[3]);
    }
    else
    {
        // tp[3] is empty, set it to UCHAR_MAX to process.
        *config_value_secondary = UCHAR_MAX;
    }
    return true;
}

static void set_creature_configuration_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    int64_t creatvar = -1;
    int64_t block = -1;
    for (size_t b = 0; b < sizeof(creature_configuration_blocks) / sizeof(creature_configuration_blocks[0]); b++)
    {
        creatvar = get_id(creature_configuration_blocks[b].keys, scline->tp[1]);
        if (creatvar != -1)
        {
            block = creature_configuration_blocks[b].block;
            break;
        }
    }
    if (creatvar == -1)
    {
        SCRPTERRLOG("Unknown creature configuration variable");
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    int64_t config_value_primary = 0, config_value_secondary = 0, config_value_tertiary = 0, config_value_quaternary = 0, config_value_quinary = 0, config_value_senary = 0;
    if (block == CrtConf_ATTRIBUTES)
    {
        if (creatvar == 20) // ATTACKPREFERENCE
        {
            config_value_primary = get_id(attackpref_desc, scline->tp[2]);
        }
        else if (creatvar == 34) // LAIROBJECT
        {
            if (parameter_is_number(scline->tp[2])) // Support name or number for lair object.
            {
                config_value_primary = atoi(scline->tp[2]);
            }
            else
            {
                config_value_primary = get_id(object_desc, scline->tp[2]);
            }
        }
        else if ((creatvar == 35) || (creatvar == 36)) // PRISONKIND or TORTUREKIND
        {
            if (parameter_is_number(scline->tp[2])) // Support name or number for prison kind or torture kind.
            {
                config_value_primary = atoi(scline->tp[2]);
            }
            else
            {
                config_value_primary = get_id(creature_desc, scline->tp[2]);
            }
        }
        else if (creatvar == 37) // SPELLIMMUNITY
        {
            if (parameter_is_number(scline->tp[2]))
            {
                config_value_primary = atoi(scline->tp[2]);
            }
            else
            {
                config_value_primary = get_id(spell_effect_flags, scline->tp[2]);
            }
            if (config_value_primary < 0)
            {
                SCRPTERRLOG("SpellImmunity flag %s is out of range or doesn't exist.", scline->tp[2]);
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
            // value 2: 'empty' is 'set', '1' is 'add', '0' is 'clear'.
            if (scline->tp[3][0] != '\0')
            {
                config_value_secondary = atoi(scline->tp[3]);
            }
            else
            {
                // tp[3] is empty, set it to UCHAR_MAX to process.
                config_value_secondary = UCHAR_MAX;
            }
        }
        else if (creatvar == 38) // HOSTILETOWARDS
        {
            if (parameter_is_number(scline->tp[2])) // Support name or number for hostile towards.
            {
                config_value_primary = atoi(scline->tp[2]);
            }
            else if (0 == strcmp(scline->tp[2], "ANY_CREATURE")) // Support ANY_CREATURE for hostile towards.
            {
                config_value_primary = CREATURE_ANY;
            }
            else if (strcasecmp(scline->tp[2], "NULL") == 0)  // Support NULL for hostile towards.
            {
                config_value_primary = 0;
            }
            else
            {
                config_value_primary = get_id(creature_desc, scline->tp[2]);
            }
        }
        else
        {
            config_value_primary = atoi(scline->tp[2]);
            if (scline->tp[3][0] != '\0')
            {
                config_value_secondary = atoi(scline->tp[3]);
            }
            // nothing there that would need the third value.
        }
    }
    else if (block == CrtConf_JOBS)
    {
        if ((creatvar > 0) && (creatvar <= 4)) // Jobs
        {
            if (!creature_configuration_job_value(scline, creaturejob_desc, &config_value_primary, &config_value_secondary))
            {
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
        }
        else
        {
            config_value_primary = atoi(scline->tp[2]);
            // Nothing there that would need the second or third value.
        }
    }
    else if (block == CrtConf_SOUNDS)
    {
        config_value_primary = atoi(scline->tp[2]);
        if (scline->tp[3][0] != '\0')
        {
            config_value_secondary = atoi(scline->tp[3]);
        }
        if (scline->tp[3][0] != '\0')
        {
            config_value_tertiary = atoi(scline->tp[4]);
        }
    }
    else if (block == CrtConf_SPRITES)
    {
        if ((creatvar == (CGI_HandSymbol + 1)) || (creatvar == (CGI_QuerySymbol + 1)))
        {
            config_value_primary = get_icon_id(scline->tp[2]);
        }
        else
        {
            config_value_primary = get_anim_id_(scline->tp[2]);
        }
    }
    else if (block == CrtConf_ATTRACTION)
    {
        if (creatvar == 1) //ENTRANCEROOM
        {
            config_value_primary = get_id(room_desc, scline->tp[2]);
            if (scline->tp[3][0] != '\0')
            {
                config_value_secondary = get_id(room_desc, scline->tp[3]);
            }
            if (scline->tp[4][0] != '\0')
            {
                config_value_tertiary = get_id(room_desc, scline->tp[4]);
            }
        }
        else
        {
            config_value_primary = atoi(scline->tp[2]);
            if (scline->tp[3][0] != '\0')
            {
                config_value_secondary = atoi(scline->tp[3]);
            }
            if (scline->tp[4][0] != '\0')
            {
                config_value_tertiary = atoi(scline->tp[4]);
            }
        }
    }
    else if (block == CrtConf_ANNOYANCE)
    {
        if (creatvar == 21) //LairEnemy
        {
            ThingModel creature_model[3] = {0, 0, 0};
            for (int64_t j = 0; j < 3; j++)
            {
                //Only needs one enemy, but can do up to 3
                if ((j > 0) && (scline->tp[j + 2][0] == '\0'))
                    break;

                if (parameter_is_number(scline->tp[j + 2]))
                {
                    creature_model[j] = atoi(scline->tp[j + 2]);
                    if (creature_model[j] > CREATURE_TYPES_MAX)
                    {
                        SCRPTERRLOG("Value %" PRId64 " out of range.", (int64_t)(atoi(scline->tp[j + 2])));
                        DEALLOCATE_SCRIPT_VALUE
                        return;
                    }
                }
                else
                {
                    creature_model[j] = parse_creature_name(scline->tp[j + 2]);
                    if (creature_model[j] < 0)
                    {
                        if (0 == strcmp(scline->tp[j + 2], "ANY_CREATURE"))
                        {
                            creature_model[j] = CREATURE_ANY;
                        }
                        else if (strcasecmp(scline->tp[j + 2], "NULL") == 0)
                        {
                            creature_model[j] = 0;
                        }
                        if (creature_model[j] < 0)
                        {
                            SCRPTERRLOG("Invalid creature model %s", scline->tp[j + 2]);
                            DEALLOCATE_SCRIPT_VALUE
                            return;
                        }
                    }
                }
            }
            config_value_primary = creature_model[0];
            config_value_secondary = creature_model[1];
            config_value_tertiary = creature_model[2];
        } else
        if (creatvar == 23) // AngerJobs
        {
            if (!creature_configuration_job_value(scline, angerjob_desc, &config_value_primary, &config_value_secondary))
            {
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
        }
        else
        {
            config_value_primary = atoi(scline->tp[2]);
            config_value_secondary = atoi(scline->tp[3]);
        }
    } else
    if (block == CrtConf_EXPERIENCE)
    {
        if (creatvar == 1) // POWERS
        {
            int64_t instance = 0;
            if (!parameter_is_number(scline->tp[2]))
            {
                instance = get_id(instance_desc, scline->tp[2]);

            }
            else
            {
                instance = atoi(scline->tp[2]);
            }
            if (instance >= 0)
            {
                config_value_primary = instance;
            }
            else
            {
                SCRPTERRLOG("Unknown instance %s ", scline->tp[2]);
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
            if ((atoi(scline->tp[3]) >= CREATURE_MAX_LEVEL) || (atoi(scline->tp[3]) <= 0)) //Powers
            {
                SCRPTERRLOG("Value %" PRId64 " out of range, only %" PRId64 " slots for Powers.", (int64_t)(atoi(scline->tp[3])), (int64_t)(CREATURE_MAX_LEVEL - 1));
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
            config_value_secondary = atoi(scline->tp[3]);
        } else
        if (creatvar == 2) // POWERSLEVELREQUIRED
        {
            if ((atoi(scline->tp[2]) <= 0) || (atoi(scline->tp[2]) > CREATURE_MAX_LEVEL)) //value
            {
                SCRPTERRLOG("Value %" PRId64 " out of range, only %" PRId64 " levels for PowersLevelRequired supported", (int64_t)(atoi(scline->tp[2])), (int64_t)(CREATURE_MAX_LEVEL));
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
            if ((atoi(scline->tp[3]) > CREATURE_MAX_LEVEL) || (atoi(scline->tp[3]) <= 0)) //slot
            {
                SCRPTERRLOG("Value %" PRId64 " out of range, only %" PRId64 " levels for PowersLevelRequired supported", (int64_t)(atoi(scline->tp[3])), (int64_t)(CREATURE_MAX_LEVEL));
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
            config_value_primary = atoi(scline->tp[2]);
            config_value_secondary = atoi(scline->tp[3]);
        } else
        if (creatvar == 3) // LEVELSTRAINVALUES
        {
            if (atoi(scline->tp[2]) < 0) //value
            {
                SCRPTERRLOG("Value %" PRId64 " out of range.", (int64_t)(atoi(scline->tp[2])));
                DEALLOCATE_SCRIPT_VALUE
                    return;
            }
            if ((atoi(scline->tp[3]) <= 0) || (atoi(scline->tp[3]) > CREATURE_MAX_LEVEL)) //slot
            {
                SCRPTERRLOG("Value %" PRId64 " out of range, only %" PRId64 " levels for LevelsTrainValues supported", (int64_t)(atoi(scline->tp[3])), (int64_t)(CREATURE_MAX_LEVEL - 1));
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
            config_value_primary = atoi(scline->tp[2]);
            config_value_secondary = atoi(scline->tp[3]);
        } else
        if (creatvar == 4) // GROWUP
        {
            config_value_primary = atoi(scline->tp[2]);
            ThingModel creature_model = 0;
            if (parameter_is_number(scline->tp[3]))
            {
                creature_model = atoi(scline->tp[3]);
                if (creature_model > CREATURE_TYPES_MAX)
                {
                    SCRPTERRLOG("Value %" PRId64 " out of range.", (int64_t)(atoi(scline->tp[3])));
                    DEALLOCATE_SCRIPT_VALUE
                    return;
                }
            }
            else
            {
                creature_model = parse_creature_name(scline->tp[3]);
                if (creature_model <  0)
                {
                    if (strcasecmp(scline->tp[3], "NULL") == 0)
                    {
                        creature_model = 0;
                    }
                    if (creature_model < 0)
                    {
                        SCRPTERRLOG("Invalid creature model %s", scline->tp[3]);
                        DEALLOCATE_SCRIPT_VALUE
                        return;
                    }
                }
            }
            config_value_secondary = creature_model;
            int64_t level = 0;
            if (config_value_secondary > 0)
            {
                level = atoi(scline->tp[4]);
                if ((level < 1) || (level > CREATURE_MAX_LEVEL))
                {
                    SCRPTERRLOG("Value %" PRId64 " out of range.", (int64_t)(atoi(scline->tp[4])));
                    DEALLOCATE_SCRIPT_VALUE
                    return;
                }
            }
            config_value_tertiary = level;
        } else
        if (creatvar == 5) // SLEEPEXPERIENCE
        {
            int64_t slabtype = get_id(slab_desc, scline->tp[2]);
            if (slabtype < 0)
            {
                SCRPTERRLOG("Unknown slab type %s.", scline->tp[2]);
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
            else
            {
                config_value_primary = slabtype;
            }
            config_value_secondary = atoi(scline->tp[3]);
            if (config_value_secondary < 0)
            {
                SCRPTERRLOG("Slab sleep experience value (%s %" PRId64 ") must be 0 or greater.", scline->tp[2], (int64_t)(config_value_secondary));
                config_value_secondary = 0;
            }
            slabtype = (scline->tp[4][0] != '\0') ? get_id(slab_desc, scline->tp[4]) : 0;
            if (slabtype < 0)
            {
                if (scline->tp[4][0] != '\0')
                {
                    SCRPTERRLOG("Unknown slab type %s.", scline->tp[4]);
                    DEALLOCATE_SCRIPT_VALUE
                    return;
                }
            }
            else
            {
                config_value_tertiary = slabtype;
            }
            config_value_quaternary = atoi(scline->tp[5]);
            if (config_value_quaternary < 0)
            {
                SCRPTERRLOG("Slab sleep experience value (%s %" PRId64 ") must be 0 or greater.", scline->tp[5], (int64_t)(config_value_quaternary));
                config_value_quaternary = 0;
            }
            slabtype = (scline->tp[6][0] != '\0') ? get_id(slab_desc, scline->tp[6]) : 0;
            if (slabtype < 0)
            {
                if (scline->tp[6][0] != '\0')
                {
                    SCRPTERRLOG("Unknown slab type %s.", scline->tp[6]);
                    DEALLOCATE_SCRIPT_VALUE
                    return;
                }
            }
            else
            {
                config_value_quinary = slabtype;
            }
            config_value_senary = atoi(scline->tp[7]);
            if (config_value_senary < 0)
            {
                SCRPTERRLOG("Slab sleep experience value (%s %" PRId64 ") must be 0 or greater.", scline->tp[7], (int64_t)(config_value_senary));
                config_value_senary = 0;
            }
        }
        else
        {
            config_value_primary = atoi(scline->tp[2]);
        }
    } else
    if (block == CrtConf_APPEARANCE)
    {
        if (creatvar == 4) // NATURALDEATHKIND
        {
            config_value_primary = get_id(creature_deathkind_desc, scline->tp[2]);
        }
        else
        {
            config_value_primary = atoi(scline->tp[2]);
            config_value_secondary = atoi(scline->tp[3]);
            config_value_tertiary = atoi(scline->tp[4]);
        }
    } else
    if (block == CrtConf_SENSES)
    {
        if (creatvar == 4) // EYEEFFECT
        {
            config_value_primary = get_id(lenses_desc, scline->tp[2]);
        }
        else
        {
            config_value_primary = atoi(scline->tp[2]);
            // nothing to fill for config_value_secondary or config_value_tertiary
        }
    }

    if (config_value_primary == -1)
    {
        SCRPTERRLOG("Unknown creature configuration value %s", scline->tp[2]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    if (config_value_secondary == -1)
    {
        SCRPTERRLOG("Unknown second creature configuration value %s", scline->tp[3]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    if (config_value_tertiary == -1)
    {
        SCRPTERRLOG("Unknown third creature configuration value %s", scline->tp[3]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    value->shorts[0] = scline->np[0];
    value->shorts[1] = creatvar;
    value->shorts[2] = block;
    value->longs[2] = config_value_primary;
    value->longs[3] = config_value_secondary;
    value->longs[4] = config_value_tertiary;
    value->longs[5] = config_value_quaternary;
    value->longs[6] = config_value_quinary;
    value->longs[7] = config_value_senary;

    SCRIPTDBG(7,"Setting creature %s configuration value %" PRId64 ":%" PRId64 " to %" PRId64 " (%" PRId64 ")", creature_code_name(value->shorts[0]), (int64_t)(value->shorts[4]), (int64_t)(value->shorts[1]), (int64_t)(value->shorts[2]), (int64_t)(value->shorts[3]));

    PROCESS_SCRIPT_VALUE(scline->command);
}

/** A SET_CREATURE_CONFIGURATION value being processed: the model, its key and the values its check stored. */
struct CrtConfValue {
    ThingModel model;
    struct CreatureModelConfig *crconf;
    int64_t block;
    int64_t key;
    const char *key_name;
    int64_t v[6];
};

/** The creature file's rows for a key, block by block (the sprites and the sounds have hooks of their own). */
static const struct NamedField *const creature_configuration_rows[CrtConf_LISTEND] = {
    [CrtConf_ATTRIBUTES] = creaturemodel_attributes_named_fields,
    [CrtConf_ATTRACTION] = creaturemodel_attraction_named_fields,
    [CrtConf_ANNOYANCE]  = creaturemodel_annoyance_named_fields,
    [CrtConf_SENSES]     = creaturemodel_senses_named_fields,
    [CrtConf_APPEARANCE] = creaturemodel_appearance_named_fields,
    [CrtConf_EXPERIENCE] = creaturemodel_experience_named_fields,
    [CrtConf_JOBS]       = creaturemodel_jobs_named_fields,
};

/** Assigns the key's values to the rows named like it in rows: the value numbered as the row's position, converted
 *  the way C converts an assignment (assign_cast; not the file's own assign function, which can clamp). Returns
 *  how many rows took a value. */
static int64_t creature_configuration_assign_rows(const struct CrtConfValue *cc, const struct NamedField *rows)
{
    int64_t n = 0;
    for (const struct NamedField *row = rows; (row != NULL) && (row->name != NULL); row++)
    {
        if ((strcasecmp(row->name, cc->key_name) != 0) || (row->type == dt_void) || (row->argnum < 0) || (row->argnum >= 6))
            continue;
        assign_cast(row, cc->v[(int)row->argnum], &creaturemodel_named_fields_set, cc->model, "SET_CREATURE_CONFIGURATION", 0);
        n++;
    }
    return n;
}

/** The plain keys: their values go to the block's rows of that name, or, when the block's row holds nothing (the
 *  attributes' CORPSEVANISHEFFECT and FOOTSTEPPITCH, which the file reads in [appearance]), the first other block's. */
static void creature_configuration_assign(const struct CrtConfValue *cc)
{
    if (creature_configuration_assign_rows(cc, creature_configuration_rows[cc->block]) > 0)
        return;
    for (int64_t b = 0; b < CrtConf_LISTEND; b++)
        if ((b != cc->block) && (creature_configuration_assign_rows(cc, creature_configuration_rows[b]) > 0))
            return;
}

/** The first row named like the key, in its block. */
static const struct NamedField *creature_configuration_row(const struct CrtConfValue *cc)
{
    for (const struct NamedField *row = creature_configuration_rows[cc->block]; (row != NULL) && (row->name != NULL); row++)
        if (strcasecmp(row->name, cc->key_name) == 0)
            return row;
    return NULL;
}

static void crconf_not_supported(const struct CrtConfValue *cc)
{
    CONFWRNLOG("Attribute (%" PRId64 ") not supported", (int64_t)(cc->key));
}

/** A flags key (SPELLIMMUNITY, the jobs): its second value 0 clears the flags given, 1 sets them, anything else
 *  (no second value) replaces them. */
static void crconf_flags(const struct CrtConfValue *cc)
{
    const struct NamedField *row = creature_configuration_row(cc);
    int64_t flags = cc->v[0];
    if ((cc->v[1] == 0) || (cc->v[1] == 1))
    {
        const int64_t current = get_named_field_value(row, &creaturemodel_named_fields_set, cc->model);
        flags = (cc->v[1] == 0) ? (current & ~cc->v[0]) : (current | cc->v[0]);
    }
    assign_cast(row, flags, &creaturemodel_named_fields_set, cc->model, "SET_CREATURE_CONFIGURATION", 0);
}

static void crconf_health(const struct CrtConfValue *cc)
{
    if (cc->crconf->health == cc->v[0])
        return;
    creature_configuration_assign(cc);
    for (PlayerNumber plyr_idx = 0; plyr_idx < PLAYERS_COUNT; plyr_idx++)
        do_to_players_all_creatures_of_model(plyr_idx, cc->model, update_relative_creature_health);
}

static void crconf_base_speed(const struct CrtConfValue *cc)
{
    if (cc->crconf->base_speed == cc->v[0])
        return;
    creature_configuration_assign(cc);
    for (PlayerNumber plyr_idx = 0; plyr_idx < PLAYERS_COUNT; plyr_idx++)
        update_speed_of_player_creatures_of_model(plyr_idx, cc->model);
}

static void crconf_lair_object(const struct CrtConfValue *cc)
{
    if (cc->crconf->lair_object == cc->v[0])
        return;
    for (PlayerNumber plyr_idx = 0; plyr_idx < PLAYERS_COUNT; plyr_idx++)
        do_to_players_all_creatures_of_model(plyr_idx, cc->model, remove_creature_lair);
    creature_configuration_assign(cc);
}

/** HOSTILETOWARDS: the list is cleared, then the creature given (if any) is its first. */
static void crconf_hostile_towards(const struct CrtConfValue *cc)
{
    for (int64_t i = 0; i < CREATURE_TYPES_MAX; i++)
        cc->crconf->hostile_towards[i] = 0;
    if (cc->v[0] != 0)
        cc->crconf->hostile_towards[0] = cc->v[0];
}

static void crconf_entrance_rooms(const struct CrtConfValue *cc)
{
    for (int i = 0; i < 3; i++)
        cc->crconf->entrance_rooms[i] = cc->v[i];
}

static void crconf_entrance_slabs(const struct CrtConfValue *cc)
{
    for (int i = 0; i < 3; i++)
        cc->crconf->entrance_slabs_req[i] = cc->v[i];
}

/** LAIRENEMY: up to three creatures, the rest of the list cleared. */
static void crconf_lair_enemy(const struct CrtConfValue *cc)
{
    for (int i = 0; i < 3; i++)
        cc->crconf->lair_enemy[i] = cc->v[i];
    cc->crconf->lair_enemy[3] = 0;
    cc->crconf->lair_enemy[4] = 0;
}

/** POWERS, POWERSLEVELREQUIRED, LEVELSTRAINVALUES: a value for one slot (the second value, from 1). */
static void crconf_learned_instance(const struct CrtConfValue *cc)
{
    cc->crconf->learned_instance_id[cc->v[1] - 1] = cc->v[0];
}

static void crconf_learned_instance_level(const struct CrtConfValue *cc)
{
    cc->crconf->learned_instance_level[cc->v[1] - 1] = cc->v[0];
}

static void crconf_to_level(const struct CrtConfValue *cc)
{
    cc->crconf->to_level[cc->v[1] - 1] = cc->v[0];
}

/** GROWUP: the last level's training value, the model grown into and its level. */
static void crconf_grow_up(const struct CrtConfValue *cc)
{
    cc->crconf->to_level[CREATURE_MAX_LEVEL - 1] = cc->v[0];
    cc->crconf->grow_up = cc->v[1];
    cc->crconf->grow_up_level = cc->v[2];
}

/** SLEEPEXPERIENCE: three slab kinds, each with its experience. */
static void crconf_sleep_experience(const struct CrtConfValue *cc)
{
    for (int i = 0; i < 3; i++)
    {
        cc->crconf->sleep_exp_slab[i] = cc->v[2 * i];
        cc->crconf->sleep_experience[i] = cc->v[2 * i + 1];
    }
}

/** TRANSPARENCYFLAGS: the script gives the flags' number, unshifted. */
static void crconf_transparency_flags(const struct CrtConfValue *cc)
{
    cc->crconf->transparency_flags = cc->v[0] << 4;
}

/** MAXANGLECHANGE: degrees, kept in the game's angle units. */
static void crconf_max_angle_change(const struct CrtConfValue *cc)
{
    cc->crconf->max_turning_speed = (cc->v[0] * DEGREES_180) / 180;
}

/** EYEEFFECT: when the local player is possessing a creature of the model, its lens changes now. */
static void crconf_eye_effect(const struct CrtConfValue *cc)
{
    creature_configuration_assign(cc);
    struct Thing* thing = thing_get(get_my_player()->influenced_thing_idx);
    if (!thing_exists(thing) || (thing->model != cc->model))
        return;
    struct LensConfig* lenscfg = get_lens_config(cc->v[0]);
    initialise_eye_lenses();
    if (flag_is_set(lenscfg->flags, LCF_HasPalette))
        PaletteSetUserPalette(get_local_user(), lenscfg->palette);
    else
        PaletteSetUserPalette(get_local_user(), engine_palette);
    setup_eye_lens(cc->v[0]);
}

/** A sound key: the sample index and how many samples there are. */
static void crconf_sound(const struct CrtConfValue *cc)
{
    struct CreatureSounds *sounds = &kfx_config_state.conf.crtr_conf.creature_sounds[cc->model];
    struct CreatureSound *sound;
    switch (cc->key)
    {
    case CrSnd_Hit:     sound = &sounds->hit; break;
    case CrSnd_Happy:   sound = &sounds->happy; break;
    case CrSnd_Sad:     sound = &sounds->sad; break;
    case CrSnd_Hang:    sound = &sounds->hang; break;
    case CrSnd_Drop:    sound = &sounds->drop; break;
    case CrSnd_Torture: sound = &sounds->torture; break;
    case CrSnd_Slap:    sound = &sounds->slap; break;
    case CrSnd_Die:     sound = &sounds->die; break;
    case CrSnd_Foot:    sound = &sounds->foot; break;
    case CrSnd_Fight:   sound = &sounds->fight; break;
    case CrSnd_Piss:    sound = &sounds->piss; break;
    default:
        CONFWRNLOG("Unrecognized Spound command (%" PRId64 ")", (int64_t)(cc->key));
        return;
    }
    sound->index = cc->v[0];
    sound->count = cc->v[1];
}

static void crconf_sprite(const struct CrtConfValue *cc)
{
    set_creature_model_graphics(cc->model, cc->key - 1, cc->v[0]);
}

/** The keys that do more than assign their values to the creature file's rows (key -1: the whole block). */
static const struct {
    int64_t block;
    int64_t key;
    void (*fn)(const struct CrtConfValue *cc);
} creature_configuration_hooks[] = {
    {CrtConf_ATTRIBUTES,  1, crconf_not_supported},   // NAME
    {CrtConf_ATTRIBUTES,  2, crconf_health},
    {CrtConf_ATTRIBUTES, 17, crconf_base_speed},
    {CrtConf_ATTRIBUTES, 24, crconf_not_supported},   // CREATURELOYALTY
    {CrtConf_ATTRIBUTES, 25, crconf_not_supported},   // LOYALTYLEVEL
    {CrtConf_ATTRIBUTES, 28, crconf_not_supported},   // PROPERTIES
    {CrtConf_ATTRIBUTES, 34, crconf_lair_object},
    {CrtConf_ATTRIBUTES, 37, crconf_flags},           // SPELLIMMUNITY
    {CrtConf_ATTRIBUTES, 38, crconf_hostile_towards},
    {CrtConf_JOBS,        1, crconf_flags},           // PRIMARYJOBS
    {CrtConf_JOBS,        2, crconf_flags},           // SECONDARYJOBS
    {CrtConf_JOBS,        3, crconf_flags},           // NOTDOJOBS
    {CrtConf_JOBS,        4, crconf_flags},           // STRESSFULJOBS
    {CrtConf_ATTRACTION,  1, crconf_entrance_rooms},
    {CrtConf_ATTRACTION,  2, crconf_entrance_slabs},
    {CrtConf_ANNOYANCE,  21, crconf_lair_enemy},
    {CrtConf_ANNOYANCE,  23, crconf_flags},           // ANGERJOBS
    {CrtConf_EXPERIENCE,  1, crconf_learned_instance},
    {CrtConf_EXPERIENCE,  2, crconf_learned_instance_level},
    {CrtConf_EXPERIENCE,  3, crconf_to_level},
    {CrtConf_EXPERIENCE,  4, crconf_grow_up},
    {CrtConf_EXPERIENCE,  5, crconf_sleep_experience},
    {CrtConf_APPEARANCE, 10, crconf_transparency_flags},
    {CrtConf_SENSES,      4, crconf_eye_effect},
    {CrtConf_SENSES,      5, crconf_max_angle_change},
    {CrtConf_SOUNDS,     -1, crconf_sound},
    {CrtConf_SPRITES,    -1, crconf_sprite},
};

static void set_creature_configuration_process(struct ScriptContext* context)
{
    struct CrtConfValue cc;
    cc.model = context->value->shorts[0];
    cc.crconf = creature_stats_get(cc.model);
    cc.key = context->value->shorts[1];
    cc.block = context->value->shorts[2];
    for (int i = 0; i < 6; i++)
        cc.v[i] = context->value->longs[2 + i];
    cc.key_name = NULL;
    for (size_t b = 0; b < sizeof(creature_configuration_blocks) / sizeof(creature_configuration_blocks[0]); b++)
        if (creature_configuration_blocks[b].block == cc.block)
            cc.key_name = get_conf_parameter_text(creature_configuration_blocks[b].keys, cc.key);
    if (cc.key_name == NULL)
        cc.key_name = "";
    if ((cc.block <= CrtConf_NONE) || (cc.block >= CrtConf_LISTEND))
    {
        ERRORLOG("Trying to configure unsupported creature block (%" PRId64 ")",(int64_t)(cc.block));
        check_and_auto_fix_stats();
        return;
    }
    size_t h;
    for (h = 0; h < sizeof(creature_configuration_hooks) / sizeof(creature_configuration_hooks[0]); h++)
        if ((creature_configuration_hooks[h].block == cc.block) &&
            ((creature_configuration_hooks[h].key == cc.key) || (creature_configuration_hooks[h].key == -1)))
            break;
    if (h < sizeof(creature_configuration_hooks) / sizeof(creature_configuration_hooks[0]))
        creature_configuration_hooks[h].fn(&cc);
    else
        creature_configuration_assign(&cc);
    check_and_auto_fix_stats();
}

static void set_object_configuration_process(struct ScriptContext *context)
{
    set_config_process(&objects_named_fields_set, context,"SET_OBJECT_CONFIGURATION");

    ThingModel model = context->value->shorts[0];
    update_all_objects_of_model(model);
}

static void display_timer_check(const struct ScriptLine *scline)
{
    const char *timrname = scline->tp[1];
    char timr_id = get_rid(timer_desc, timrname);
    if (timr_id == -1)
    {
        SCRPTERRLOG("Unknown timer, '%s'", timrname);
        return;
    }
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    value->bytes[1] = timr_id;
    value->longs[1] = 0;
    value->bytes[2] = (TbBool)scline->np[2];
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void display_timer_process(struct ScriptContext *context)
{
    kfx_game_state.script_timer_player = context->player_idx;
    kfx_game_state.script_timer_id = context->value->bytes[1];
    kfx_game_state.script_timer_limit = context->value->longs[1];
    kfx_game_state.timer_real = context->value->bytes[2];
    kfx_game_state.flags_gui |= GGUI_ScriptTimer;
}

static void add_to_timer_check(const struct ScriptLine *scline)
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

static void add_to_timer_process(struct ScriptContext *context)
{
   add_to_script_timer(context->player_idx, context->value->longs[1], context->value->longs[2]);
}

static void add_bonus_time_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    value->longs[0] = scline->np[0];
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void add_bonus_time_process(struct ScriptContext *context)
{
   kfx_game_state.bonus_time += context->value->longs[0];
}

static void display_variable_check(const struct ScriptLine *scline)
{
    int64_t varib_id, varib_type;
    if (!script_parse_get_varib(scline->tp[1], &varib_id, &varib_type, kfx_game_state.level_file_version))
    {
        SCRPTERRLOG("Unknown variable, '%s'", scline->tp[1]);
        return;
    }
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    value->bytes[1] = scline->np[3];
    value->bytes[2] = varib_type;
    value->longs[1] = varib_id;
    value->longs[2] = scline->np[2];
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void display_variable_process(struct ScriptContext *context)
{
    script_display_variable(context->player_idx, context->value->bytes[2], context->value->longs[1],
        context->value->longs[2], context->value->bytes[1], false, -1);
}

static void display_variable_with_label_check(const struct ScriptLine *scline)
{
    int64_t varib_id, varib_type;
    if (!script_parse_get_varib(scline->tp[1], &varib_id, &varib_type, kfx_game_state.level_file_version))
    {
        SCRPTERRLOG("Unknown variable, '%s'", scline->tp[1]);
        return;
    }
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);

    value->bytes[2] = varib_type;
    value->longs[1] = varib_id;
    value->shorts[4] = -1;
    const char *icon = scline->tp[2];
    if (icon[0] != '\0'){        
        value->shorts[4] = get_chat_icon_sprite_idx(icon);
        if (value->shorts[4] == -1)
        {
            SCRPTERRLOG("Invalid custom icon (%s)", icon);
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }

    PROCESS_SCRIPT_VALUE(scline->command);
}

static void display_variable_with_label_process(struct ScriptContext *context)
{
    script_display_variable(context->player_idx, context->value->bytes[2], context->value->longs[1], 0, 0, true,
        context->value->shorts[4]);
}
static void display_countdown_check(const struct ScriptLine *scline)
{
    if (scline->np[2] <= 0)
    {
        SCRPTERRLOG("Can't have a countdown to %" PRId64 " turns.", (int64_t)(scline->np[2]));
        return;
    }
    const char *timrname = scline->tp[1];
    char timr_id = get_rid(timer_desc, timrname);
    if (timr_id == -1)
    {
        SCRPTERRLOG("Unknown timer, '%s'", timrname);
        return;
    }
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    value->bytes[1] = timr_id;
    value->longs[1] = scline->np[2];
    value->bytes[2] = (TbBool)scline->np[3];
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void cmd_no_param_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void hide_timer_process(struct ScriptContext *context)
{
   kfx_game_state.flags_gui &= ~GGUI_ScriptTimer;
}

static void hide_variable_check(const struct ScriptLine *scline)
{
    int64_t varib_id = -1;
    int64_t varib_type = -1;
    int64_t player_idx = -1;

    if (scline->tp[0][0] == '\0')
    {
        ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);

        value->longs[0] = -1;  // All players
        value->longs[1] = -1;  // All variables
        value->bytes[2] = -1;

        PROCESS_SCRIPT_VALUE(scline->command);
        return;
    }

    player_idx = scline->np[0];

    if (scline->tp[1][0] != '\0')
    {
        if (!script_parse_get_varib(scline->tp[1], &varib_id, &varib_type, kfx_game_state.level_file_version))
        {
            SCRPTERRLOG("Unknown variable, '%s'", scline->tp[1]);
            return;
        }
    }

    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);

    value->longs[0] = player_idx;
    value->longs[1] = varib_id;
    value->longs[2] = varib_type;

    PROCESS_SCRIPT_VALUE(scline->command);
}

static void hide_variable_process(struct ScriptContext *context)
{
    const int64_t varib_type = context->value->longs[2];
    const int64_t varib_id = context->value->longs[1];
    const int64_t player_idx = context->value->longs[0];
    if ((varib_id > -1) && (varib_type > -1))
        script_hide_variable(player_idx, varib_type, varib_id);
    else
        script_hide_variable(player_idx, -1, -1);
}

static void create_effect_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    TbMapLocation location;
    const char *effect_name = scline->tp[0];
    int64_t effct_id = effect_or_effect_element_id(effect_name);
    if (effct_id == 0)
    {
        SCRPTERRLOG("Unrecognised effect: %s", effect_name);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->shorts[0] = effct_id;
    const char *locname = scline->tp[1];
    if (!get_map_location_id(locname, &location))
    {
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->ulongs[1] = location;
    value->longs[2] = scline->np[2];
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void create_effect_at_pos_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    const char *effect_name = scline->tp[0];
    int64_t effct_id = effect_or_effect_element_id(effect_name);
    if (effct_id == 0)
    {
        SCRPTERRLOG("Unrecognised effect: %s", effect_name);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->shorts[0] = effct_id;
    if (subtile_coords_invalid(scline->np[1], scline->np[2]))
    {
        SCRPTERRLOG("Invalid coordinates: %" PRId64 ", %" PRId64, (int64_t)(scline->np[1]), (int64_t)(scline->np[2]));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->shorts[1] = scline->np[1];
    value->shorts[2] = scline->np[2];
    value->longs[2] = scline->np[3];
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void null_process(struct ScriptContext *context)
{
}



static void set_sacrifice_recipe_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);

    value->sac.action = get_rid(rules_sacrifices_commands, scline->tp[0]);
    if (value->sac.action == -1)
    {
        SCRPTERRLOG("Unexpected action:%s", scline->tp[0]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    int64_t param;
    if ((value->sac.action == SacA_CustomPunish) || (value->sac.action == SacA_CustomReward))
    {
        param = get_id(flag_desc, scline->tp[1]) + 1;
    }
    else
    {
        param = get_id(creature_desc, scline->tp[1]);
        if (param == -1)
        {
            param = get_id(sacrifice_unique_desc, scline->tp[1]);
        }
        if (param == -1)
        {
            param = get_id(spell_desc, scline->tp[1]);
        }
    }
    if (param == -1 && (strcmp(scline->tp[1], "NONE") == 0))
    {
        param = 0;
    }

    if (param < 0)
    {
        param = 0;
        value->sac.action = SacA_None;
        SCRPTERRLOG("Unexpected parameter:%s", scline->tp[1]);
    }
    value->sac.param = param;

    for (int64_t i = 0; i < MAX_SACRIFICE_VICTIMS; i++)
    {
       int64_t vi = get_rid(creature_desc, scline->tp[i + 2]);
       if (vi < 0)
         vi = 0;
       value->sac.victims[i] = vi;
    }

    PROCESS_SCRIPT_VALUE(scline->command);
}

static void remove_sacrifice_recipe_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);

    value->sac.action = SacA_None;
    value->sac.param = 0;

    for (int64_t i = 0; i < MAX_SACRIFICE_VICTIMS; i++)
    {
       int64_t vi = get_rid(creature_desc, scline->tp[i]);
       if (vi < 0)
         vi = 0;
       value->sac.victims[i] = vi;
    }

    PROCESS_SCRIPT_VALUE(scline->command);
}

static void set_sacrifice_recipe_process(struct ScriptContext *context)
{
    ThingModel victims[MAX_SACRIFICE_VICTIMS];
    int64_t action = context->value->sac.action;
    int64_t param = context->value->sac.param;

    for (int64_t i = 0; i < MAX_SACRIFICE_VICTIMS; i++)
    {
        victims[i] = context->value->sac.victims[i];
    }

    script_set_sacrifice_recipe(action, param, victims);
}

static void set_box_tooltip_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    if ((scline->np[0] < 0) || (scline->np[0] >= CUSTOM_BOX_COUNT))
    {
        SCRPTERRLOG("Invalid CUSTOM_BOX number (%" PRId64 ")", (int64_t)(scline->np[0]));
        DEALLOCATE_SCRIPT_VALUE;
        return;
    }
    value->shorts[0] = scline->np[0];

    if (strlen(scline->tp[1]) >= MESSAGE_TEXT_LEN)
    {
        SCRPTWRNLOG("Tooltip TEXT too long; truncating to %" PRId64 " characters", (int64_t)(MESSAGE_TEXT_LEN - 1));
    }
    value->longs[2] = script_strdup(scline->tp[1]);
    if (value->longs[2] < 0)
    {
        SCRPTERRLOG("Run out script strings space");
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    PROCESS_SCRIPT_VALUE(scline->command);
}


static void set_box_tooltip_process(struct ScriptContext* context)
{
    int64_t idx = context->value->shorts[0];
    snprintf(kfx_sim_state.box_tooltip[idx], MESSAGE_TEXT_LEN, "%s", script_strval(context->value->longs[2]));
}

static void set_box_tooltip_id_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    if ((scline->np[0] < 0) || (scline->np[0] >= CUSTOM_BOX_COUNT))
    {
        SCRPTERRLOG("Invalid CUSTOM_BOX number (%" PRId64 ")", (int64_t)(scline->np[0]));
        DEALLOCATE_SCRIPT_VALUE;
        return;
    }
    TextStringId str_id = get_string_id_by_alias(scline->tp[1]);
    if (str_id < 0)
    {
        SCRPTERRLOG("Unknown string '%s'", scline->tp[1]);
        DEALLOCATE_SCRIPT_VALUE;
        return;
    }
    value->shorts[0] = scline->np[0];
    value->shorts[1] = str_id;
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void set_box_tooltip_id_process(struct ScriptContext* context)
{
    int64_t idx = context->value->shorts[0];
    int64_t string = context->value->shorts[1];
    snprintf(kfx_sim_state.box_tooltip[idx], MESSAGE_TEXT_LEN, "%s", get_string(string));
}

static void change_slab_owner_check(const struct ScriptLine *scline)
{

    if (scline->np[0] < 0 || scline->np[0] > kfx_sim_state.map_tiles_x) //x coord
    {
        SCRPTERRLOG("Value '%" PRId64 "' out of range. Range 0-%" PRId64 " allowed.", (int64_t)(scline->np[0]),(int64_t)(kfx_sim_state.map_tiles_x));
        return;
    }
    if (scline->np[1] < 0 || scline->np[1] > kfx_sim_state.map_tiles_y) //y coord
    {
        SCRPTERRLOG("Value '%" PRId64 "' out of range. Range 0-%" PRId64 " allowed.", (int64_t)(scline->np[1]),(int64_t)(kfx_sim_state.map_tiles_y));
        return;
    }
    int64_t filltype = get_id(fill_desc, scline->tp[3]);
    if ((scline->tp[3][0] != '\0') && (filltype == -1))
    {
        SCRPTWRNLOG("Fill type %s not recognized", scline->tp[3]);
    }

    command_add_value(Cmd_CHANGE_SLAB_OWNER, scline->np[2], scline->np[0], scline->np[1], filltype);
}

static void change_slab_owner_process(struct ScriptContext *context)
{
    MapSlabCoord x = context->value->longs[0];
    MapSlabCoord y = context->value->longs[1];
    int64_t fill_type = context->value->longs[2];
    if (fill_type > 0)
    {
        struct CompoundCoordFilterParam iter_param;
        iter_param.plyr_idx = context->player_idx;
        iter_param.primary_number = fill_type;
        iter_param.secondary_number = get_slabmap_block(x, y)->kind;
        slabs_fill_iterate_from_slab(x, y, slabs_change_owner, &iter_param);
    } else {
        change_slab_owner_from_script(x, y, context->player_idx);
    }
}

static void change_slab_type_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);

    if (scline->np[0] < 0 || scline->np[0] > kfx_sim_state.map_tiles_x) //x coord
    {
        SCRPTERRLOG("Value '%" PRId64 "' out of range. Range 0-%" PRId64 " allowed.", (int64_t)(scline->np[0]),(int64_t)(kfx_sim_state.map_tiles_x));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    else
    {
        value->shorts[0] = scline->np[0];
    }

    if (scline->np[1] < 0 || scline->np[1] > kfx_sim_state.map_tiles_y) //y coord
    {
        SCRPTERRLOG("Value '%" PRId64 "' out of range. Range 0-%" PRId64 " allowed.", (int64_t)(scline->np[0]),(int64_t)(kfx_sim_state.map_tiles_y));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    else
    {
        value->shorts[1] = scline->np[1];
    }

    if (scline->np[2] < 0 || scline->np[2] >= kfx_config_state.conf.slab_conf.slab_types_count) //slab kind
    {
        SCRPTERRLOG("Unsupported slab '%" PRId64 "'. Slabs range 0-%" PRId64 " allowed.", (int64_t)(scline->np[2]),(int64_t)(kfx_config_state.conf.slab_conf.slab_types_count-1));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    else
    {
        value->shorts[2] = scline->np[2];
    }

    value->shorts[3] = get_id(fill_desc, scline->tp[3]);
    if ((scline->tp[3][0] != '\0') && (value->shorts[3] == -1))
    {
        SCRPTWRNLOG("Fill type %s not recognized", scline->tp[3]);
    }
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void change_slab_type_process(struct ScriptContext *context)
{
    int64_t x = context->value->shorts[0];
    int64_t y = context->value->shorts[1];
    int64_t slab_kind = context->value->shorts[2];
    int64_t fill_type = context->value->shorts[3];

    if (fill_type > 0)
    {
        struct CompoundCoordFilterParam iter_param;
        iter_param.primary_number = slab_kind;
        iter_param.secondary_number = fill_type;
        iter_param.tertiary_number = get_slabmap_block(x, y)->kind;
        slabs_fill_iterate_from_slab(x, y, slabs_change_type, &iter_param);
    }
    else
    {
        replace_slab_from_script(x, y, slab_kind);
    }
}

static void reveal_map_location_check(const struct ScriptLine *scline)
{
    TbMapLocation location;
    if (!get_map_location_id(scline->tp[1], &location)) {
        return;
    }
    command_add_value(Cmd_REVEAL_MAP_LOCATION, scline->np[0], location, scline->np[2], 0);
}

static void reveal_map_location_process(struct ScriptContext *context)
{
    TbMapLocation target = context->value->longs[0];
    SYNCDBG(0, "Revealing location type %" PRIu64, (uint64_t)(target));
    MapSubtlCoord x = 0;
    MapSubtlCoord y = 0;
    int64_t r = context->value->longs[1];
    find_map_location_coords(target, &x, &y, context->player_idx, __func__);
    if ((x == 0) && (y == 0))
    {
        WARNLOG("Can't decode location %" PRIu64, (uint64_t)(target));
        return;
    }
    if (r == -1)
    {
        struct CompoundCoordFilterParam iter_param;
        iter_param.plyr_idx = context->player_idx;
        slabs_fill_iterate_from_slab(subtile_slab(x), subtile_slab(y), slabs_reveal_slab_and_corners, &iter_param);
    } else
        reveal_map_area(context->player_idx, x-(r>>1), x+(r>>1)+(r&1), y-(r>>1), y+(r>>1)+(r&1));
}

static void player_zoom_to_check(const struct ScriptLine *scline)
{
    TbMapLocation location;
    const char *where = scline->tp[1];
    if (!get_map_location_id(where, &location) || location == MLoc_NONE) {
        SCRPTERRLOG("invalid zoom location \"%s\"",where);
        return;
    }

    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    value->longs[0] = location;
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void player_zoom_to_process(struct ScriptContext *context)
{
    TbMapLocation target = context->value->longs[0];
    struct Coord3d pos;

    find_location_pos(target, context->player_idx, &pos, __func__);
    set_player_zoom_to_position(get_player(context->player_idx),&pos);
}

static void level_up_players_creatures_check(const struct ScriptLine* scline)
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

static void level_up_players_creatures_process(struct ScriptContext* context)
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

static void use_spell_on_creature_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    int64_t crtr_id = parse_creature_name(scline->tp[1]);
    if (crtr_id == CREATURE_NONE)
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
    const char* mag_name = scline->tp[3];
    int64_t mag_id = get_rid(spell_desc, mag_name);
    CrtrExpLevel spell_level = scline->np[4];
    if (mag_id == -1)
    {
        SCRPTERRLOG("Invalid spell: %s", mag_name);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    struct SpellConfig* spconf = get_spell_config(mag_id);
    if (spconf->linked_power) // Only check for spells linked to a keeper power.
    {
        if (spell_level < 1)
        {
            SCRPTWRNLOG("Spell %s level too low: %" PRId64 ", setting to 1.", mag_name, (int64_t)(spell_level));
            spell_level = 1;
        }
        if (spell_level > (MAGIC_OVERCHARGE_LEVELS + 1)) // Creatures cast spells from level 1 to 10.
        {
            SCRPTWRNLOG("Spell %s level too high: %" PRId64 ", setting to %" PRId64 ".", mag_name, (int64_t)(spell_level), (int64_t)((MAGIC_OVERCHARGE_LEVELS + 1)));
            spell_level = MAGIC_OVERCHARGE_LEVELS;
        }
    }
    spell_level--;
    value->shorts[1] = crtr_id;
    value->shorts[2] = select_id;
    value->shorts[3] = mag_id;
    value->shorts[4] = spell_level;
    PROCESS_SCRIPT_VALUE(scline->command);
}
static void use_spell_on_creature_process(struct ScriptContext* context)
{
    ThingModel crmodel = context->value->shorts[1];
    int64_t select_id = context->value->shorts[2];
    SpellKind spell_idx = context->value->shorts[3];
    CrtrExpLevel overchrg = context->value->shorts[4];
    script_use_spell_on_creature_with_criteria(context->player_idx, crmodel, select_id, spell_idx, overchrg);
}

static void use_spell_on_players_creatures_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    int64_t crtr_id = parse_creature_name(scline->tp[1]);
    if (crtr_id == CREATURE_NONE)
    {
        SCRPTERRLOG("Unknown creature, '%s'", scline->tp[1]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    const char *mag_name = scline->tp[2];
    int64_t mag_id = get_rid(spell_desc, mag_name);
    CrtrExpLevel spell_level = scline->np[3];
    if (mag_id == -1)
    {
        SCRPTERRLOG("Invalid spell: %s", mag_name);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    struct SpellConfig *spconf = get_spell_config(mag_id);
    if (spconf->linked_power) // Only check for spells linked to a keeper power.
    {
        if (spell_level < 1)
        {
            SCRPTWRNLOG("Spell %s level too low: %" PRId64 ", setting to 1.", mag_name, (int64_t)(spell_level));
            spell_level = 1;
        }
        if (spell_level > (MAGIC_OVERCHARGE_LEVELS + 1)) // Creatures cast spells from level 1 to 10.
        {
            SCRPTWRNLOG("Spell %s level too high: %" PRId64 ", setting to %" PRId64 ".", mag_name, (int64_t)(spell_level), (int64_t)((MAGIC_OVERCHARGE_LEVELS + 1)));
            spell_level = MAGIC_OVERCHARGE_LEVELS;
        }
    }
    spell_level--;
    value->shorts[1] = crtr_id;
    value->shorts[2] = mag_id;
    value->shorts[3] = spell_level;
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void use_spell_on_players_creatures_process(struct ScriptContext *context)
{
    int64_t crmodel = context->value->shorts[1];
    int64_t spell_idx = context->value->shorts[2];
    CrtrExpLevel overchrg = context->value->shorts[3];
    apply_spell_effect_to_players_creatures(context->player_idx, crmodel, spell_idx, overchrg);
}

static void use_power_on_players_creatures_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    int64_t crtr_id = parse_creature_name(scline->tp[1]);
    PlayerNumber caster_player = scline->np[2];
    const char* pwr_name = scline->tp[3];
    int64_t pwr_id = get_rid(power_desc, pwr_name);
    KeepPwrLevel power_level = scline->np[4];
    int64_t free = scline->np[5];
    if (free == -1)
    {
        free = get_id(is_free_desc, scline->tp[5]);
        if (free == -1)
        {
            SCRPTERRLOG("Unknown free value '%s' not recognized", scline->tp[5]);
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }

    if (crtr_id == CREATURE_NONE)
    {
        SCRPTERRLOG("Unknown creature, '%s'", scline->tp[1]);
        DEALLOCATE_SCRIPT_VALUE
    }
    if (pwr_id == -1)
    {
        SCRPTERRLOG("Invalid power: %s", pwr_name);
        DEALLOCATE_SCRIPT_VALUE
    }
    switch (pwr_id)
    {
    case PwrK_HEALCRTR:
    case PwrK_SPEEDCRTR:
    case PwrK_PROTECT:
    case PwrK_REBOUND:
    case PwrK_CONCEAL:
    case PwrK_DISEASE:
    case PwrK_CHICKEN:
    case PwrK_FREEZE:
    case PwrK_SLOW:
    case PwrK_FLIGHT:
    case PwrK_VISION:
    case PwrK_CALL2ARMS:
    case PwrK_LIGHTNING:
    case PwrK_CAVEIN:
    case PwrK_SIGHT:
    case PwrK_TIMEBOMB:
        if ((power_level < 1) || (power_level > MAGIC_OVERCHARGE_LEVELS))
        {
            SCRPTERRLOG("Power %s level %" PRId64 " out of range. Acceptible values are %" PRId64 "~%" PRId64, pwr_name, (int64_t)(power_level), (int64_t)(1), (int64_t)(MAGIC_OVERCHARGE_LEVELS));
            DEALLOCATE_SCRIPT_VALUE
        }
        power_level--; // transform human 1~9 range into computer 0~8 range
        break;
    case PwrK_SLAP:
    case PwrK_MKDIGGER:
        break;
    default:
        SCRPTERRLOG("Power not supported for this command: %s", power_code_name(pwr_id));
        DEALLOCATE_SCRIPT_VALUE
    }
    value->shorts[1] = crtr_id;
    value->shorts[2] = pwr_id;
    value->shorts[3] = power_level;
    value->shorts[4] = caster_player;
    value->shorts[5] = free;
    PROCESS_SCRIPT_VALUE(scline->command);
}

/**
 * Casts a keeper power on all creatures of a specific model, or positions of all creatures depending on the power.
 * @param crmodel The creature model to target, accepts wildcards.
 * @param pwr_idx The ID of the Keeper Power.
 * @param overchrg The overcharge level of the keeperpower. Is ignored when not applicable.
 * @param caster The player number of the player who is made to cast the spell.
 * @param free If gold is used when casting the spell. It will fail to cast if it is not free and money is not available.
 */
void cast_power_on_players_creatures(PlayerNumber plyr_idx, ThingModel crmodel, int64_t pwr_idx, KeepPwrLevel overchrg, PlayerNumber caster, TbBool free)
{
    SYNCDBG(8, "Starting");
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    FOR_EACH_THING(thing, thing_walk_players_creatures_of_model(dungeon, plyr_idx, crmodel))
    {
        if (creature_matches_model(thing, crmodel))
        {
            script_use_power_on_creature(thing, pwr_idx, overchrg, caster, free);
        }
    }
    SYNCDBG(19, "Finished");
}

static void use_power_on_players_creatures_process(struct ScriptContext* context)
{
    int64_t crmodel = context->value->shorts[1];
    int64_t pwr_idx = context->value->shorts[2];
    KeepPwrLevel overchrg = context->value->shorts[3];
    PlayerNumber caster = context->value->shorts[4];
    TbBool free = context->value->shorts[5];
    cast_power_on_players_creatures(context->player_idx, crmodel, pwr_idx, overchrg, caster, free);
}

static void set_creature_instance_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    value->bytes[0] = scline->np[0];
    value->bytes[1] = scline->np[1];
    if (scline->tp[2][0] != '\0')
    {
        int64_t instance = get_rid(instance_desc, scline->tp[2]);
        if (instance != -1)
        {
            value->bytes[2] = instance;
        }
        else
        {
            SCRPTERRLOG("Invalid instance: %s", scline->tp[2]);
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }
    value->bytes[3] = scline->np[3];
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void set_creature_instance_process(struct ScriptContext *context)
{
    ThingModel crmodel = context->value->bytes[0];
    int64_t slot = context->value->bytes[1];
    int64_t instance = context->value->bytes[2];
    unsigned char level = context->value->bytes[3];

    script_set_creature_instance(crmodel, slot, instance, level);

}


static void hide_hero_gate_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    int64_t n = scline->np[0];
    if (scline->np[0] < 0)
    {
        n = -scline->np[0];
    }
    struct Thing* thing = find_hero_gate_of_number(n);
    if (thing_is_invalid(thing))
    {
        SCRPTERRLOG("Invalid hero gate: %" PRId64, (int64_t)(scline->np[0]));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->bytes[0] = n;
    value->bytes[1] = scline->np[1];

    PROCESS_SCRIPT_VALUE(scline->command);
}

static void hide_hero_gate_process(struct ScriptContext* context)
{
    hero_gate_set_hidden(find_hero_gate_of_number(context->value->bytes[0]), context->value->bytes[1]);
}

/**
 * The right-hand side of IF, IF_AVAILABLE and IF_CONTROLS: another player's variable (tp[3] a player,
 * tp[4] the variable, parsed by the caller), or a number in tp[3].
 */
static void if_right_side(const struct ScriptLine *scline, TbBool *double_var_mode, int64_t *plr_range_id_right, int64_t *value)
{
    if (*scline->tp[4] != '\0')
    {
        *double_var_mode = true;

        if (!get_player_id(scline->tp[3], plr_range_id_right)) {

            SCRPTWRNLOG("failed to parse \"%s\" as a player", scline->tp[3]);
        }
    }
    else
    {
        *double_var_mode = false;

        char* text;
        *value = script_strtol(scline->tp[3], &text, 0);
        if (text != &scline->tp[3][strlen(scline->tp[3])]) {
            SCRPTWRNLOG("Numerical value \"%s\" interpreted as %" PRId64, scline->tp[3], (int64_t)(*value));
        }
    }
}

/**
 * Warns when an IF-family clause names a single player without a Dungeon. With `any_variable` unset,
 * variables that don't need a dungeon (as get_condition_value() reads them) don't warn.
 */
static void if_warn_player_without_dungeon(int64_t plr_range_id, int64_t varib_type, const char *clause, TbBool any_variable)
{
    int64_t plr_start;
    int64_t plr_end;
    if (get_players_range(plr_range_id, &plr_start, &plr_end) >= 0) {
        struct Dungeon* dungeon = get_dungeon(plr_start);
        if ((plr_start+1 == plr_end) && dungeon_invalid(dungeon)) {
            // Note that this list should be kept updated with the changes in get_condition_value()
            if (any_variable || ((varib_type != SVar_GAME_TURN) && (varib_type != SVar_ALL_DUNGEONS_DESTROYED)
             && (varib_type != SVar_DOOR_NUM) && (varib_type != SVar_TRAP_NUM)))
                SCRPTWRNLOG("Found player without dungeon used in %s clause in script; this will not work correctly", clause);
        }
    }
}

/** Adds an IF-family condition: against a number, or against another player's variable. */
static void if_add_condition(TbBool double_var_mode, int64_t plr_range_id, int64_t opertr_id, int64_t varib_type, int64_t varib_id,
    int64_t plr_range_id_right, int64_t varib_type_right, int64_t varib_id_right, int64_t value)
{
    if (double_var_mode)
    {
        command_add_condition_2variables(plr_range_id, opertr_id, varib_type, varib_id,plr_range_id_right, varib_type_right, varib_id_right);
    }
    else
    {
        command_add_condition(plr_range_id, opertr_id, varib_type, varib_id, value);
    }
}

static void if_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    const char *varib_name = scline->tp[1];
    const char *operatr = scline->tp[2];
    const char *varib_name_right = scline->tp[4];
    int64_t plr_range_id_right = -1;
    int64_t value = 0;
    TbBool double_var_mode = false;
    int64_t varib_type;
    int64_t varib_id;
    int64_t varib_type_right;
    int64_t varib_id_right;
    if_right_side(scline, &double_var_mode, &plr_range_id_right, &value);

    if (kfx_game_state.script.conditions_num >= CONDITIONS_COUNT)
    {
      SCRPTERRLOG("Too many (over %" PRId64 ") conditions in script", (int64_t)(CONDITIONS_COUNT));
      return;
    }
    // Recognize variable
    if (!script_parse_get_varib(varib_name, &varib_id, &varib_type, kfx_game_state.level_file_version))
    {
        return;
    }
    if (double_var_mode && !script_parse_get_varib(varib_name_right, &varib_id_right, &varib_type_right, kfx_game_state.level_file_version))
    {
        return;
    }
    // Warn if using the command for a player without Dungeon struct
    if_warn_player_without_dungeon(plr_range_id, varib_type, "IF", false);
    if (double_var_mode)
        if_warn_player_without_dungeon(plr_range_id_right, varib_type_right, "IF", false);
    // Recognize comparison
    int64_t opertr_id = get_id(comparison_desc, operatr);
    if (opertr_id == -1)
    {
      SCRPTERRLOG("Unknown comparison name, '%s'", operatr);
      return;
    }
    if_add_condition(double_var_mode, plr_range_id, opertr_id, varib_type, varib_id, plr_range_id_right, varib_type_right, varib_id_right, value);
}

static void if_available_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    const char *varib_name = scline->tp[1];
    const char *operatr = scline->tp[2];
    const char *varib_name_right = scline->tp[4];
    int64_t plr_range_id_right = -1;
    int64_t value = 0;
    TbBool double_var_mode = false;
    int64_t varib_type_right;
    int64_t varib_id_right;
    if_right_side(scline, &double_var_mode, &plr_range_id_right, &value);

    if (kfx_game_state.script.conditions_num >= CONDITIONS_COUNT)
    {
      SCRPTERRLOG("Too many (over %" PRId64 ") conditions in script", (int64_t)(CONDITIONS_COUNT));
      return;
    }
    // Recognize variable
    int64_t varib_id;
    int64_t varib_type = get_id(available_variable_desc, varib_name);
    if (varib_type == -1)
        varib_id = -1;
    else
        varib_id = 0;
    if (varib_id == -1)
    {
      varib_id = get_id(door_desc, varib_name);
      varib_type = SVar_AVAILABLE_DOOR;
    }
    if (varib_id == -1)
    {
      varib_id = get_id(trap_desc, varib_name);
      varib_type = SVar_AVAILABLE_TRAP;
    }
    if (varib_id == -1)
    {
      varib_id = get_id(room_desc, varib_name);
      varib_type = SVar_AVAILABLE_ROOM;
    }
    if (varib_id == -1)
    {
      varib_id = get_id(power_desc, varib_name);
      varib_type = SVar_AVAILABLE_MAGIC;
    }
    if (varib_id == -1)
    {
      varib_id = get_id(creature_desc, varib_name);
      varib_type = SVar_AVAILABLE_CREATURE;
    }
    if (varib_id == -1)
    {
      SCRPTERRLOG("Unrecognized VARIABLE, '%s'", varib_name);
      return;
    }
    // Recognize comparison
    int64_t opertr_id = get_id(comparison_desc, operatr);
    if (opertr_id == -1)
    {
      SCRPTERRLOG("Unknown comparison name, '%s'", operatr);
      return;
    }
    // Warn if using the command for a player without Dungeon struct
    if_warn_player_without_dungeon(plr_range_id, varib_type, "IF_AVAILABLE", true);
    if (double_var_mode && !script_parse_get_varib(varib_name_right, &varib_id_right, &varib_type_right, kfx_game_state.level_file_version))
    {
        return;
    }
    if_add_condition(double_var_mode, plr_range_id, opertr_id, varib_type, varib_id, plr_range_id_right, varib_type_right, varib_id_right, value);
}

static void if_controls_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    const char *varib_name = scline->tp[1];
    const char *operatr = scline->tp[2];
    const char *varib_name_right = scline->tp[4];
    int64_t plr_range_id_right = -1;
    int64_t value = 0;
    TbBool double_var_mode = false;
    int64_t varib_type_right = 0;
    int64_t varib_id_right = 0;
    if_right_side(scline, &double_var_mode, &plr_range_id_right, &value);

    int64_t varib_id;
    if (kfx_game_state.script.conditions_num >= CONDITIONS_COUNT)
    {
      SCRPTERRLOG("Too many (over %" PRId64 ") conditions in script", (int64_t)(CONDITIONS_COUNT));
      return;
    }
    // Recognize variable
    int64_t varib_type = get_id(controls_variable_desc, varib_name);
    if (varib_type == -1)
      varib_id = -1;
    else
      varib_id = 0;
    if (varib_id == -1)
    {
      varib_id = get_id(creature_desc, varib_name);
      varib_type = SVar_CONTROLS_CREATURE;
    }
    if (varib_id == -1)
    {
      SCRPTERRLOG("Unrecognized VARIABLE, '%s'", varib_name);
      return;
    }
    // Recognize comparison
    int64_t opertr_id = get_id(comparison_desc, operatr);
    if (opertr_id == -1)
    {
      SCRPTERRLOG("Unknown comparison name, '%s'", operatr);
      return;
    }
    // Warn if using the command for a player without Dungeon struct. (The right-hand side is checked
    // before its variable is parsed, so with type 0: kept as it was.)
    if_warn_player_without_dungeon(plr_range_id, varib_type, "IF_CONTROLS", true);
    if (double_var_mode)
        if_warn_player_without_dungeon(plr_range_id_right, varib_type_right, "IF", false);

    if (double_var_mode && !script_parse_get_varib(varib_name_right, &varib_id_right, &varib_type_right, kfx_game_state.level_file_version))
    {
        return;
    }
    if_add_condition(double_var_mode, plr_range_id, opertr_id, varib_type, varib_id, plr_range_id_right, varib_type_right, varib_id_right, value);
}

static void if_allied_check(const struct ScriptLine *scline)
{
    int64_t pA = scline->np[0];
    int64_t pB = scline->np[1];
    int64_t op = scline->np[2];
    int64_t val = scline->np[3];

    if (kfx_game_state.script.conditions_num >= CONDITIONS_COUNT)
    {
        SCRPTERRLOG("Too many (over %" PRId64 ") conditions in script", (int64_t)(CONDITIONS_COUNT));
        return;
    }

    command_add_condition(pA, op, SVar_ALLIED_PLAYER, pB, val);
}

static void set_texture_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);

    int64_t texture_id = get_rid(texture_pack_desc, scline->tp[1]);
    if (texture_id == -1)
    {
        if (parameter_is_number(scline->tp[1]))
        {
            texture_id = atoi(scline->tp[1]) + 1;
        }
        else
        {
            SCRPTERRLOG("Invalid texture pack: '%s'", scline->tp[1]);
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }
    value->shorts[0] = texture_id;
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void set_texture_process(struct ScriptContext *context)
{
    PlayerNumber plyr_idx = context->player_idx;
    int64_t texture_id = context->value->shorts[0];

    set_player_texture(plyr_idx, texture_id);
}

static void set_music_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    if (parameter_is_number(scline->tp[0])) {
        value->chars[0] = atoi(scline->tp[0]);
    } else {
        value->chars[0] = -1;
        value->longs[1] = script_strdup(scline->tp[0]);
        if (value->longs[1] < 0) {
            SCRPTERRLOG("Run out script strings space");
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void set_music_process(struct ScriptContext *context)
{
    int64_t track = context->value->chars[0];
    if ((track > 0) && (kfx_game_state.music_track == track))
    {
        return;
    }
    if (track == 0) {
        SCRPTLOG("Stopping music");
        stop_music(true);
    } else if (track < 0) {
        const char * fname = script_strval(context->value->longs[1]);
        play_music_fgroup(FGrp_CmpgMedia, fname);
    } else {
        play_music_track(track);
    }
}

static void play_message_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    int64_t msgtype_id = get_id(msgtype_desc, scline->tp[1]);
    if (msgtype_id == -1)
    {
        SCRPTERRLOG("Unrecognized message type: '%s'", scline->tp[1]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->chars[1] = msgtype_id;
    if (parameter_is_number(scline->tp[2]))
    {
        value->shorts[1] = atoi(scline->tp[2]);
        value->bytes[4] = 0;
    }
    else
    {
        value->bytes[4] = 1;
        value->longs[2] = script_strdup(scline->tp[2]);
        if (value->longs[2] < 0) {
            SCRPTERRLOG("Run out script strings space");
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void play_message_process(struct ScriptContext *context)
{
    const TbBool param_is_string = context->value->bytes[4];
    const char msgtype_id = context->value->chars[1];
    const int64_t msg_id = context->value->shorts[1];
    const char * filename = script_strval(context->value->longs[2]);


    if (context->player_idx == my_player_number)
    {
        ui_script_play_message(param_is_string,msgtype_id,msg_id,filename);
    }
}

static void set_power_hand_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);

    int64_t hand_idx = get_rid(powerhand_desc, scline->tp[1]);
    if (hand_idx == -1)
    {
        if (parameter_is_number(scline->tp[1]))
        {
            hand_idx = atoi(scline->tp[1]);
        }
        else
        {
            SCRPTERRLOG("Invalid hand_idx: '%s'", scline->tp[1]);
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }
    value->shorts[0] = hand_idx;
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void set_power_hand_process(struct ScriptContext *context)
{
    int64_t hand_idx = context->value->shorts[0];
    struct PlayerInfo * player;
    player = get_player(context->player_idx);
    player->hand_idx = hand_idx;
}

static void add_effectgen_to_level_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);

    const char* generator_name = scline->tp[0];
    const char* locname = scline->tp[1];
    int64_t range = scline->np[2];

    TbMapLocation location;
    ThingModel gen_id;
    if (parameter_is_number(generator_name))
    {
        gen_id = atoi(generator_name);
    }
    else
    {
        gen_id = get_id(effectgen_desc, generator_name);
    }
    if (gen_id <= 0)
    {
        SCRPTERRLOG("Unknown effect generator, '%s'", generator_name);
        DEALLOCATE_SCRIPT_VALUE;
        return;
    }
    if (kfx_game_state.script.party_triggers_num >= PARTY_TRIGGERS_COUNT)
    {
        SCRPTERRLOG("Too many ADD_CREATURE commands to spawn effect generator in script");
        DEALLOCATE_SCRIPT_VALUE;
        return;
    }

    // Recognize place where party is created
    if (!get_map_location_id(locname, &location))
    {
        DEALLOCATE_SCRIPT_VALUE;
        return;
    }
    value->shorts[0] = (int64_t)gen_id;
    value->ulongs[1] = location;
    value->shorts[4] = range * COORD_PER_STL;
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void add_effectgen_to_level_process(struct ScriptContext* context)
{
    ThingModel gen_id = context->value->shorts[0];
    TbMapLocation location = context->value->ulongs[1];
    int64_t range = context->value->shorts[4];
    if (get_script_current_condition() == CONDITION_ALWAYS)
    {
        script_process_new_effectgen(gen_id, location, range);
    }
    else
    {
        if (kfx_game_state.script.party_triggers_num < PARTY_TRIGGERS_COUNT)
        {
            struct PartyTrigger* pr_trig = &kfx_game_state.script.party_triggers[kfx_game_state.script.party_triggers_num];
            pr_trig->flags = TrgF_CREATE_EFFECT_GENERATOR;
            pr_trig->flags |= next_command_reusable ? TrgF_REUSABLE : 0;
            pr_trig->plyr_idx = 0; //not needed
            pr_trig->creatr_id = 0; //not needed
            pr_trig->exp_level = gen_id;
            pr_trig->carried_gold = range;
            pr_trig->location = location;
            pr_trig->ncopies = 1;
            pr_trig->condit_idx = get_script_current_condition();
        }
        else
        {
            SCRPTERRLOG("Max party triggers reached, failed to spawn effect generator");
        }
        kfx_game_state.script.party_triggers_num++;
    }
}

static void set_effectgen_configuration_check(const struct ScriptLine* scline)
{
    set_config_check(&effects_effectgenerator_named_fields_set, scline,"SET_EFFECTGEN_CONFIG");
}

static void set_effectgen_configuration_process(struct ScriptContext* context)
{
    set_config_process(&effects_effectgenerator_named_fields_set, context,"SET_EFFECTGEN_CONFIG");
}

static void set_power_configuration_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    const char *powername = scline->tp[0];
    const char *property = scline->tp[1];
    char *new_value = (char*)scline->tp[2];

    int64_t power_id = get_id(power_desc, powername);
    if (power_id == -1)
    {
        SCRPTERRLOG("Unknown power, '%s'", powername);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    int64_t powervar = get_id(magic_power_commands, property);
    if (powervar == -1)
    {
        SCRPTERRLOG("Unknown power variable: %s", new_value);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    int64_t number_value = 0;
    int64_t k;
    switch (powervar)
    {
        case 2: // Power
        case 3: // Cost
        {
            value->bytes[3] = atoi(scline->tp[3]) - 1; //-1 because we want slot 1 to 9, not 0 to 8
            value->longs[2] = atoi(new_value);
            break;
        }
        case 5: // Castability
        {
            long long j;
            if (scline->tp[3][0] != '\0')
            {
                j = get_long_id(powermodel_castability_commands, new_value);
                if (j <= 0)
                {
                    SCRPTERRLOG("Incorrect castability value: %s", new_value);
                    DEALLOCATE_SCRIPT_VALUE
                    return;
                }
                else
                {
                    number_value = j;
                }
                value->chars[3] = atoi(scline->tp[3]);
            }
            else
            {
                if (parameter_is_number(new_value))
                {
                    number_value = atoll(new_value);
                }
                else
                {
                    char *saveptr = NULL;
                    char *flag = strtok_r(new_value," ",&saveptr);
                    while ( flag != NULL )
                    {
                        j = get_long_id(powermodel_castability_commands, flag);
                        if (j > 0)
                        {
                            number_value |= j;
                        } else
                        {
                            SCRPTERRLOG("Incorrect castability value: %s", new_value);
                            DEALLOCATE_SCRIPT_VALUE
                            return;
                        }
                        flag = strtok_r(NULL, " ", &saveptr);
                    }
                }
                value->chars[3] = -1;
            }
            value->ulonglongs[1] = number_value;
            break;
        }
        case 6: // Artifact
        {
            k = get_id(object_desc, new_value);
            if (k >= 0)
            {
                  number_value = k;
            }
            value->longs[2] = number_value;
            break;
        }
        case 10: // SymbolSprites
        {
            value->longs[1] = atoi(new_value);
            value->longs[2] = atoi(scline->tp[3]);
            break;
        }
        case 14: // Properties
        {
            if (scline->tp[3][0] != '\0')
            {
                k = get_id(powermodel_properties_commands, new_value);
                if (k <= 0)
                {
                    SCRPTERRLOG("Incorrect property value: %s", new_value);
                    DEALLOCATE_SCRIPT_VALUE
                    return;
                }
                else
                {
                    number_value = k;
                }
                value->chars[3] = atoi(scline->tp[3]);
            }
            else
            {
                if (parameter_is_number(new_value))
                {
                    number_value = atoi(new_value);
                }
                else
                {
                    char *saveptr = NULL;
                    char *flag = strtok_r(new_value," ",&saveptr);
                    while ( flag != NULL )
                    {
                        k = get_id(powermodel_properties_commands, flag);
                        if (k > 0)
                        {
                            number_value |= k;
                        } else
                        {
                            SCRPTERRLOG("Incorrect property value: %s", new_value);
                            DEALLOCATE_SCRIPT_VALUE
                            return;
                        }
                        flag = strtok_r(NULL, " ", &saveptr);
                    }
                }
                value->chars[3] = -1;
            }
            value->longs[2] = number_value;
            break;
        }
        case 15: // OverchargeCheck
        {
            number_value = get_id(powermodel_expand_check_func_type,new_value);
            if (number_value < 0)
            {
                SCRPTERRLOG("Invalid OverchargeCheckt: %s", new_value);
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
            value->longs[2] = number_value;
            break;
        }
        case 16: // PlayerState
        {
            k = get_id(player_state_commands, new_value);
            if (k >= 0)
            {
                number_value = k;
            }
            value->longs[2] = number_value;
            break;
        }
        case 17: // ParentPower
        {
            k = get_id(power_desc, new_value);
            if (k >= 0)
            {
                number_value = k;
            }
            value->longs[2] = number_value;
            break;
        }
        case 20: // Spell
        {
            k = get_id(spell_desc, new_value);
            if (k >= 0)
            {
                number_value = k;
            }
            else
            {
                SCRPTERRLOG("Incorrect Spell valuet: %s", new_value);
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
            break;
        }
        case 21: // Effect
        {
            k = effect_or_effect_element_id(new_value);
            if (k == 0)
            {
                SCRPTERRLOG("Unrecognised effect: %s", new_value);
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
            else
            {
                number_value = k;
            }
            break;
        }
        case 22: // UseFunction
        {
            k = get_id(magic_use_func_commands, new_value);
            if (k >= 0)
            {
                number_value = k;
            }
            else
            {
                SCRPTERRLOG("Incorrect UseFunction: %s", new_value);
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
            break;
        }
        case 23: // CreatureType
        {
            k = get_id(creature_desc, new_value);
            if (k >= 0)
            {
                number_value = k;
            }
            else
            {
                SCRPTERRLOG("Incorrect Creature type: %s", new_value);
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
            break;
        }
        case 24: // CostFormula
        {
            k = get_id(magic_cost_formula_commands, new_value);
            if (k >= 0)
            {
                number_value = k;
            }
            else
            {
                SCRPTERRLOG("Incorrect Cost formula: %s", new_value);
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
            break;
        }
        default:
            value->longs[2] = atoi(new_value);
    }
    {
        if ( (powervar == 5) && (value->chars[3] != -1) )
        {
            SCRIPTDBG(7, "Toggling %s castability flag: %" PRId64, powername, (int64_t)(number_value));
        }
        else if ( (powervar == 14) && (value->chars[3] != -1) )
        {
            SCRIPTDBG(7, "Toggling %s property flag: %" PRId64, powername, (int64_t)(number_value));
        }
        else
        {
            SCRIPTDBG(7, "Setting power %s property %s to %" PRId64, powername, property, (int64_t)(number_value));
        }
    }
    value->shorts[0] = power_id;
    value->bytes[2] = powervar;

    PROCESS_SCRIPT_VALUE(scline->command);
}

static void set_power_configuration_process(struct ScriptContext *context)
{
    struct PowerConfigStats *powerst = get_power_model_stats(context->value->shorts[0]);
    switch (context->value->bytes[2])
    {
        case 2: // Power
            powerst->strength[context->value->bytes[3]] = context->value->longs[2];
            break;
        case 3: // Cost
            powerst->cost[context->value->bytes[3]] = context->value->longs[2];
            break;
        case 4: // Duration
            powerst->duration = context->value->longs[2];
            break;
        case 5: // Castability
        {
            unsigned long long flag = context->value->ulonglongs[1];
            if (context->value->chars[3] == 1)
            {
                set_flag(powerst->can_cast_flags, flag);
            }
            else if (context->value->chars[3] == 0)
            {
                clear_flag(powerst->can_cast_flags, flag);
            }
            else
            {
                powerst->can_cast_flags = flag;
            }
            break;
        }
        case 6: // Artifact
            powerst->artifact_model = context->value->longs[2];
            kfx_config_state.conf.object_conf.object_to_power_artifact[powerst->artifact_model] = context->value->shorts[0];
            break;
        case 7: // NameTextID
            powerst->name_stridx = context->value->longs[2];
            break;
        case 8: // TooltipTextID
            powerst->tooltip_stridx = context->value->longs[2];
            break;
        case 10: // SymbolSprites
            powerst->bigsym_sprite_idx = context->value->longs[1];
            powerst->medsym_sprite_idx = context->value->longs[2];
            break;
        case 11: // PointerSprites
            powerst->pointer_sprite_idx = context->value->longs[2];
            break;
        case 12: // PanelTabIndex
            powerst->panel_tab_idx = context->value->longs[2];
            break;
        case 13: // SoundSamples
            powerst->select_sample_idx = context->value->longs[2];
            break;
        case 14: // Properties
            if (context->value->chars[3] == 1)
            {
                set_flag(powerst->config_flags, context->value->longs[2]);
            }
            else if (context->value->chars[3] == 0)
            {
                clear_flag(powerst->config_flags, context->value->longs[2]);
            }
            else
            {
                powerst->config_flags = context->value->longs[2];
            }
            break;
        case 15: // OverchargeCheck
            powerst->overcharge_check_idx = context->value->longs[2];
            break;
        case 16: // PlayerState
            powerst->work_state = context->value->longs[2];
            break;
        case 17: // ParentPower
            powerst->parent_power = context->value->longs[2];
            break;
        case 18: // SoundPlayed
            powerst->select_sound_idx = context->value->longs[2];
            break;
        case 19: // Cooldown
            powerst->cast_cooldown = context->value->longs[2];
            break;
        case 20: // Spell
            powerst->cast_cooldown = context->value->longs[2];
            break;
        case 21: // Effect
            powerst->effect_id = context->value->longs[2];
            break;
        case 22: // UseFunction
            powerst->magic_use_func_idx = context->value->longs[2];
            break;
        case 23: // CreatureType
            powerst->creature_model = context->value->longs[2];
            break;
        case 24: // CostFormula
            powerst->cost_formula = context->value->longs[2];
            break;
        default:
            WARNMSG("Unsupported power configuration, variable %" PRId64 ".", (int64_t)(context->value->bytes[2]));
            break;
    }
    ui_update_powers_tab_to_config();
    struct PlayerInfo *player = get_my_player();
    if (player->view_type == PVT_DungeonTop)
    {
        if (ui_menu_is_active(GMnu_SPELL))
        {
            ui_turn_off_menu(GMnu_SPELL);
            ui_turn_on_menu(GMnu_SPELL);
        }
        else if (ui_menu_is_active(GMnu_SPELL2))
        {
            ui_turn_off_menu(GMnu_SPELL2);
            ui_turn_on_menu(GMnu_SPELL2);
        }
    }
}

static void set_player_colour_check(const struct ScriptLine *scline)
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

static void set_player_colour_process(struct ScriptContext *context)
{
    if (context->player_idx == PLAYER_NEUTRAL)
    {
        return;
    }
    set_player_colour(context->player_idx, context->value->bytes[0]);
}

static void set_game_rule_check(const struct ScriptLine* scline)
{
    char* rulevalue_str = malloc(MAX_TEXT_LENGTH);
    if (rulevalue_str == NULL)
        return;
    snprintf(rulevalue_str, MAX_TEXT_LENGTH, "%s", scline->tp[1]);
    PlayerNumber plyr_idx;
    if (scline->tp[2][0] == '\0')
    {
        plyr_idx = ALL_PLAYERS;
    }
    else
    {
        plyr_idx = get_id(player_desc, scline->tp[2]);
        if (plyr_idx == -1)
        {
            if (!parameter_is_number(scline->tp[2]))
            {
                SCRPTERRLOG("Invalid player: %s", scline->tp[1]);
                free(rulevalue_str);
                return;
            }
            plyr_idx = ALL_PLAYERS;
            snprintf(rulevalue_str, MAX_TEXT_LENGTH, "%s %s", scline->tp[1], scline->tp[2]);
        }
    }
    ALLOCATE_SCRIPT_VALUE(scline->command, plyr_idx);

    const char* rulename = scline->tp[0];


    int64_t rulegroup = 0;
    int64_t ruleval = 0;
    int64_t ruledesc = 0;

    for (size_t i = 0; i < sizeof(ruleblocks)/sizeof(ruleblocks[0]); i++)
    {
        ruledesc = get_named_field_id(ruleblocks[i], rulename);
        if (ruledesc != -1)
        {
            rulegroup = i;
            ruleval = parse_named_field_value(ruleblocks[i]+ruledesc, rulevalue_str,&rules_named_fields_set, 0,"SET_GAME_RULE",ccf_SplitExecution|ccf_DuringLevel);
            break;
        }
    }
    free(rulevalue_str);
    if (ruledesc == -1)
    {
        SCRPTERRLOG("Unknown Game Rule '%s'.", rulename);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    value->shorts[0] = rulegroup;
    value->shorts[1] = ruledesc;
    value->longs[1] = ruleval;
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void set_game_rule_process(struct ScriptContext* context)
{
    int64_t rulegroup = context->value->shorts[0];
    int64_t ruledesc  = context->value->shorts[1];
    int64_t rulevalue  = context->value->longs[1];


    SCRIPTDBG(7,"Changing Game Rule '%s' to %" PRId64, (ruleblocks[rulegroup]+ruledesc)->name, (int64_t)(rulevalue));

    assign_named_field_value((ruleblocks[rulegroup]+ruledesc),rulevalue,&rules_named_fields_set,context->player_idx,"SET_GAME_RULE",ccf_SplitExecution|ccf_DuringLevel);
}

static void set_increase_on_experience_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    int64_t onexpdesc = get_id(on_experience_desc, scline->tp[0]);
    if (onexpdesc == -1)
    {
        SCRPTERRLOG("Unknown variable '%s'.", scline->tp[0]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    if (scline->np[1] < 0)
    {
        SCRPTERRLOG("Value %" PRId64 " out of range for variable '%s'.", (int64_t)(scline->np[1]), scline->tp[0]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->shorts[0] = onexpdesc;
    value->shorts[1] = scline->np[1];
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void set_increase_on_experience_process(struct ScriptContext* context)
{
    int64_t variable = context->value->shorts[0];
    const char *varname = on_experience_desc[variable - 1].name;
    switch (variable)
    {
    case 1: //SizeIncreaseOnExp
        SCRIPTDBG(7,"Changing variable %s from %" PRId64 " to %" PRId64 ".", varname, (int64_t)(kfx_config_state.conf.crtr_conf.exp.size_increase_on_exp), (int64_t)(context->value->shorts[1]));
        kfx_config_state.conf.crtr_conf.exp.size_increase_on_exp = context->value->shorts[1];
        break;
    case 2: //PayIncreaseOnExp
        SCRIPTDBG(7,"Changing variable %s from %" PRId64 " to %" PRId64 ".", varname, (int64_t)(kfx_config_state.conf.crtr_conf.exp.pay_increase_on_exp), (int64_t)(context->value->shorts[1]));
        kfx_config_state.conf.crtr_conf.exp.pay_increase_on_exp = context->value->shorts[1];
        break;
    case 3: //SpellDamageIncreaseOnExp
        SCRIPTDBG(7,"Changing variable %s from %" PRId64 " to %" PRId64 ".", varname, (int64_t)(kfx_config_state.conf.crtr_conf.exp.spell_damage_increase_on_exp), (int64_t)(context->value->shorts[1]));
        kfx_config_state.conf.crtr_conf.exp.spell_damage_increase_on_exp = context->value->shorts[1];
        break;
    case 4: //RangeIncreaseOnExp
        SCRIPTDBG(7,"Changing variable %s from %" PRId64 " to %" PRId64 ".", varname, (int64_t)(kfx_config_state.conf.crtr_conf.exp.range_increase_on_exp), (int64_t)(context->value->shorts[1]));
        kfx_config_state.conf.crtr_conf.exp.range_increase_on_exp = context->value->shorts[1];
        break;
    case 5: //JobValueIncreaseOnExp
        SCRIPTDBG(7,"Changing variable %s from %" PRId64 " to %" PRId64 ".", varname, (int64_t)(kfx_config_state.conf.crtr_conf.exp.job_value_increase_on_exp), (int64_t)(context->value->shorts[1]));
        kfx_config_state.conf.crtr_conf.exp.job_value_increase_on_exp = context->value->shorts[1];
        break;
    case 6: //HealthIncreaseOnExp
        SCRIPTDBG(7,"Changing variable %s from %" PRId64 " to %" PRId64 ".", varname, (int64_t)(kfx_config_state.conf.crtr_conf.exp.health_increase_on_exp), (int64_t)(context->value->shorts[1]));
        kfx_config_state.conf.crtr_conf.exp.health_increase_on_exp = context->value->shorts[1];
        break;
    case 7: //StrengthIncreaseOnExp
        SCRIPTDBG(7,"Changing variable %s from %" PRId64 " to %" PRId64 ".", varname, (int64_t)(kfx_config_state.conf.crtr_conf.exp.strength_increase_on_exp), (int64_t)(context->value->shorts[1]));
        kfx_config_state.conf.crtr_conf.exp.strength_increase_on_exp = context->value->shorts[1];
        break;
    case 8: //DexterityIncreaseOnExp
        SCRIPTDBG(7,"Changing variable %s from %" PRId64 " to %" PRId64 ".", varname, (int64_t)(kfx_config_state.conf.crtr_conf.exp.dexterity_increase_on_exp), (int64_t)(context->value->shorts[1]));
        kfx_config_state.conf.crtr_conf.exp.dexterity_increase_on_exp = context->value->shorts[1];
        break;
    case 9: //DefenseIncreaseOnExp
        SCRIPTDBG(7,"Changing variable %s from %" PRId64 " to %" PRId64 ".", varname, (int64_t)(kfx_config_state.conf.crtr_conf.exp.defense_increase_on_exp), (int64_t)(context->value->shorts[1]));
        kfx_config_state.conf.crtr_conf.exp.defense_increase_on_exp = context->value->shorts[1];
        break;
    case 10: //LoyaltyIncreaseOnExp
        SCRIPTDBG(7,"Changing variable %s from %" PRId64 " to %" PRId64 ".", varname, (int64_t)(kfx_config_state.conf.crtr_conf.exp.loyalty_increase_on_exp), (int64_t)(context->value->shorts[1]));
        kfx_config_state.conf.crtr_conf.exp.loyalty_increase_on_exp = context->value->shorts[1];
        break;
    case 11: //ExpForHittingIncreaseOnExp
        SCRIPTDBG(7,"Changing variable %s from %" PRId64 " to %" PRId64 ".", varname, (int64_t)(kfx_config_state.conf.crtr_conf.exp.exp_on_hitting_increase_on_exp), (int64_t)(context->value->shorts[1]));
        kfx_config_state.conf.crtr_conf.exp.exp_on_hitting_increase_on_exp = context->value->shorts[1];
        break;
    case 12: //TrainingCostIncreaseOnExp
        SCRIPTDBG(7,"Changing variable %s from %" PRId64 " to %" PRId64 ".", varname, (int64_t)(kfx_config_state.conf.crtr_conf.exp.training_cost_increase_on_exp), (int64_t)(context->value->shorts[1]));
        kfx_config_state.conf.crtr_conf.exp.training_cost_increase_on_exp = context->value->shorts[1];
        break;
    case 13: //ScavengingCostIncreaseOnExp
        SCRIPTDBG(7,"Changing variable %s from %" PRId64 " to %" PRId64 ".", varname, (int64_t)(kfx_config_state.conf.crtr_conf.exp.scavenging_cost_increase_on_exp), (int64_t)(context->value->shorts[1]));
        kfx_config_state.conf.crtr_conf.exp.scavenging_cost_increase_on_exp = context->value->shorts[1];
        break;
    default:
        WARNMSG("Unsupported variable, command %" PRId64 ".", (int64_t)(context->value->shorts[0]));
        break;
    }
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

static void set_player_modifier_check(const struct ScriptLine* scline)
{
    player_modifier_check(scline, true);
}

static void set_player_modifier_process(struct ScriptContext* context)
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

static void add_to_player_modifier_check(const struct ScriptLine* scline)
{
    player_modifier_check(scline, false);
}

static void add_to_player_modifier_process(struct ScriptContext* context)
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

static void set_creature_max_level_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    int64_t crtr_id = parse_creature_name(scline->tp[1]);
    int64_t crtr_lvl = scline->np[2];
    if (crtr_id == CREATURE_NONE)
    {
        SCRPTERRLOG("Unable to manipulate max level of creature '%s', creature doesn't exist.", scline->tp[1]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    if ((crtr_lvl < -1) || (crtr_lvl > CREATURE_MAX_LEVEL))
    {
        SCRPTERRLOG("Unable to set max level of creature '%s' to %" PRId64 ", value is out of range.", creature_code_name(crtr_id), (int64_t)(crtr_lvl));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->shorts[0] = crtr_id;
    value->shorts[1] = crtr_lvl;
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void set_creature_max_level_process(struct ScriptContext* context)
{
    struct Dungeon* dungeon;
    int64_t crtr_id = context->value->shorts[0];
    int64_t crtr_lvl = context->value->shorts[1];
    PlayerNumber plyr_idx = context->player_idx;
    dungeon = get_dungeon(plyr_idx);
    if (!dungeon_invalid(dungeon))
    {
        if (!is_creature_model_wildcard(crtr_id))
        {
            if (crtr_id < kfx_config_state.conf.crtr_conf.model_count) {
                if (crtr_lvl < 0)
                {
                    crtr_lvl = CREATURE_MAX_LEVEL + 1;
                    dungeon->creature_max_level[crtr_id] = crtr_lvl;
                    SCRIPTDBG(7,"Max level of creature '%s' set to default for player %" PRId64 ".", creature_code_name(crtr_id), (int64_t)plyr_idx);
                } else {
                    dungeon->creature_max_level[crtr_id] = crtr_lvl-1;
                    SCRIPTDBG(7,"Max level of creature '%s' set to %" PRId64 " for player %" PRId64 ".", creature_code_name(crtr_id), (int64_t)(crtr_lvl), (int64_t)plyr_idx);
                }
            }
        } else
        {
            for (int64_t i = 1; i < kfx_config_state.conf.crtr_conf.model_count; i++)
            {
                if (creature_model_matches_model(i, plyr_idx , crtr_id))
                {
                    if (crtr_lvl < 0)
                    {
                        crtr_lvl = CREATURE_MAX_LEVEL + 1;
                        dungeon->creature_max_level[i] = crtr_lvl;
                        SCRIPTDBG(7,"Max level of creature '%s' set to default for player %" PRId64 ".", creature_code_name(i), (int64_t)plyr_idx);
                    } else {
                        dungeon->creature_max_level[i] = crtr_lvl-1;
                        SCRIPTDBG(7,"Max level of creature '%s' set to %" PRId64 " for player %" PRId64 ".", creature_code_name(i), (int64_t)(crtr_lvl), (int64_t)plyr_idx);
                    }
                }
            }
        }
    } else
    {
        SCRPTERRLOG("Unable to manipulate max level of creature '%s', player %" PRId64 " has no dungeon.", creature_code_name(crtr_id), (int64_t)plyr_idx);
    }
}

static void reset_or_trigger_action_point_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    int64_t apt_idx = action_point_number_to_index(scline->np[0]);
    if (!action_point_exists_idx(apt_idx))
    {
        SCRPTERRLOG("Non-existing Action Point, no %" PRId64, (int64_t)(scline->np[0]));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->longs[0] = apt_idx;
    PlayerNumber plyr_idx = (scline->tp[1][0] == '\0') ? ALL_PLAYERS : get_id(player_desc, scline->tp[1]);
    if (plyr_idx == -1)
    {
        SCRPTERRLOG("Invalid player: %s", scline->tp[1]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->chars[4] = plyr_idx;
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void reset_action_point_process(struct ScriptContext* context)
{
    action_point_reset_idx(context->value->longs[0], context->value->chars[4]);
}

static void quick_message_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    if ((scline->np[0] < 0) || (scline->np[0] >= QUICK_MESSAGES_COUNT))
    {
        SCRPTERRLOG("Invalid information ID number (%" PRId64 ")", (int64_t)(scline->np[0]));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    if (strlen(scline->tp[1]) > MESSAGE_TEXT_LEN)
    {
        SCRPTWRNLOG("Information TEXT too long; truncating to %" PRId64 " characters", (int64_t)(MESSAGE_TEXT_LEN-1));
    }
    if ((kfx_sim_state.quick_messages[scline->np[0]][0] != '\0') && (strcmp(kfx_sim_state.quick_messages[scline->np[0]],scline->tp[1]) != 0))
    {
        SCRPTWRNLOG("Quick Message no %" PRId64 " overwritten by different text", (int64_t)(scline->np[0]));
    }
    snprintf(kfx_sim_state.quick_messages[scline->np[0]], MESSAGE_TEXT_LEN, "%s", scline->tp[1]);
    value->longs[0]= scline->np[0];
	{ int64_t icon_id = 0; get_chat_icon_from_value(scline->tp[2], &icon_id, &value->chars[6]); value->shorts[4] = (int16_t)icon_id; }
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void quick_message_process(struct ScriptContext* context)
{
    ui_message_add(context->value->chars[6], context->value->shorts[4], kfx_sim_state.quick_messages[context->value->ulongs[0]]);
}

static void display_message_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);

    TextStringId msg_num = get_string_id_by_alias(scline->tp[0]);
    if (msg_num < 0)
    {
        SCRPTERRLOG("Unknown string '%s'", scline->tp[0]);
        DEALLOCATE_SCRIPT_VALUE;
        return;
    }
    value->ulongs[0] = msg_num;
    { int64_t icon_id = 0; get_chat_icon_from_value(scline->tp[1], &icon_id, &value->chars[7]); value->shorts[4] = (int16_t)icon_id; }

    PROCESS_SCRIPT_VALUE(scline->command);
}

static void display_message_process(struct ScriptContext* context)
{
    ui_message_add(context->value->chars[7], context->value->shorts[4], get_string(context->value->ulongs[0]));
}

static void clear_message_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    if ((scline->np[0] > GUI_MESSAGES_COUNT) || (scline->np[0] <= 0))
    {
        value->chars[1] = GUI_MESSAGES_COUNT;
    }
    else
    {
        value->chars[1] = scline->np[0];
    }
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void clear_message_process(struct ScriptContext* context)
{
    unsigned char count = min(context->value->chars[1], kfx_sim_state.active_messages_count);
    for (int64_t k = kfx_sim_state.active_messages_count-1; k >= (kfx_sim_state.active_messages_count-count); k--)
    {
        kfx_sim_state.messages[k].expiration_turn = get_gameturn();
    }
}

static void change_slab_texture_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    if ( (scline->np[0] < 0) || (scline->np[0] >= kfx_sim_state.map_tiles_x) || (scline->np[1] < 0) || (scline->np[1] >= kfx_sim_state.map_tiles_y) )
    {
        SCRPTERRLOG("Invalid co-ordinates: %" PRId64 ", %" PRId64, (int64_t)(scline->np[0]), (int64_t)(scline->np[1]));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    int64_t texture_id = get_id(texture_pack_desc, scline->tp[2]);
    if (texture_id == -1)
    {
        if (parameter_is_number(scline->tp[2]))
        {
            texture_id = script_atol(scline->tp[2]) + 1;
        }
        else
        {
            SCRPTERRLOG("Invalid texture pack: '%s'", scline->tp[2]);
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }
    if ( (scline->np[2] < 0) || (scline->np[2] >= TEXTURE_VARIATIONS_COUNT) )
    {
        SCRPTERRLOG("Invalid texture ID: %" PRId64, (int64_t)(scline->np[2]));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->shorts[0] = scline->np[0];
    value->shorts[1] = scline->np[1];
    value->bytes[4] = (unsigned char)texture_id;
    value->chars[5] = get_id(fill_desc, scline->tp[3]);
    if ((scline->tp[3][0] != '\0') && (value->chars[5] == -1))
    {
        SCRPTWRNLOG("Fill type %s not recognized", scline->tp[3]);
    }
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void change_slab_texture_process(struct ScriptContext* context)
{
    if (context->value->chars[5] > 0)
    {
        MapSlabCoord slb_x = context->value->shorts[0];
        MapSlabCoord slb_y = context->value->shorts[1];
        struct CompoundCoordFilterParam iter_param;
        iter_param.primary_number = context->value->bytes[4]; // new texture
        iter_param.secondary_number = context->value->chars[5]; // fill type
        iter_param.tertiary_number = get_slabmap_block(slb_x, slb_y)->kind;
        slabs_fill_iterate_from_slab(slb_x, slb_y, slabs_change_texture, &iter_param);
    }
    else
    {
        SlabCodedCoords slb_num = get_slab_number(context->value->shorts[0], context->value->shorts[1]);
        kfx_config_state.slab_ext_data[slb_num] = context->value->bytes[4];
        kfx_config_state.slab_ext_data_initial[slb_num] = context->value->bytes[4];
    }
}

static void computer_player_check(const struct ScriptLine* scline)
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

static void computer_player_process(struct ScriptContext* context)
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

static void add_object_to_level_at_pos_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    int64_t tngmodel = get_rid(object_desc, scline->tp[0]);
    if (tngmodel == -1)
    {
        SCRPTERRLOG("Unknown object: %s", scline->tp[0]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->shorts[0] = tngmodel;
    if (!subtile_coords_invalid(scline->np[1], scline->np[2]))
    {
        value->shorts[2] = scline->np[1];
        value->shorts[3] = scline->np[2];
    }
    else
    {
        SCRPTERRLOG("Invalid subtile co-ordinates: %" PRId64 ", %" PRId64, (int64_t)(scline->np[1]), (int64_t)(scline->np[2]));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->longs[2] = scline->np[3];
    PlayerNumber plyr_idx = get_rid(player_desc, scline->tp[4]); // Optional variable
    if ((plyr_idx == -1) || (plyr_idx == ALL_PLAYERS))
    {
        plyr_idx = PLAYER_NEUTRAL;
    }
    int64_t angle = 0;
    if (strcmp(scline->tp[5], "") != 0) // Optional variable
    {
        if (parameter_is_number(scline->tp[5]))
        {
            angle = atoi(scline->tp[5]) % DEGREES_360;
        }
        else
        {
            angle = get_rid(orientation_desc, scline->tp[5]);
            if (angle < 0)
            {
                SCRPTERRLOG("Unknown orientation: %s", scline->tp[5]);
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
        }
    }

    value->chars[2] = plyr_idx;
    value->shorts[6] = angle;
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void add_object_to_level_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    int64_t obj_id = get_rid(object_desc, scline->tp[0]);
    if (obj_id == -1)
    {
        SCRPTERRLOG("Unknown object, '%s'", scline->tp[0]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->shorts[0] = obj_id;
    TbMapLocation location;
    if (!get_map_location_id(scline->tp[1], &location))
    {
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->ulongs[1] = location;
    value->longs[2] = scline->np[2];
    PlayerNumber plyr_idx = get_rid(player_desc, scline->tp[3]);
    if ((plyr_idx == -1) || (plyr_idx == ALL_PLAYERS)) //Optional variable
    {
        plyr_idx = PLAYER_NEUTRAL;
    }

    int64_t angle = 0;
    if (strcmp(scline->tp[4], "") != 0) //Optional variable
    {
        if (parameter_is_number(scline->tp[4]))
        {
            angle = atoi(scline->tp[4]) % DEGREES_360;
        }
        else
        {
            angle = get_rid(orientation_desc, scline->tp[4]);
            if (angle < 0)
            {
                SCRPTERRLOG("Unknown orientation: %s", scline->tp[4]);
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
        }
    }

    value->chars[2] = plyr_idx;
    value->shorts[8] = angle;
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void add_object_to_level_process(struct ScriptContext* context)
{
    struct Coord3d pos;
    if (get_coords_at_location(&pos,context->value->ulongs[1],true))
    {
        script_process_new_object(context->value->shorts[0], pos.x.stl.num, pos.y.stl.num, context->value->longs[2], context->value->chars[2], context->value->shorts[8]);
    }
}

static void add_object_to_level_at_pos_process(struct ScriptContext* context)
{
    script_process_new_object(context->value->shorts[0], context->value->shorts[2], context->value->shorts[3], context->value->longs[2], context->value->chars[2],context->value->shorts[6]);
}

static void set_computer_globals_check(const struct ScriptLine* scline)
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

static void set_computer_globals_process(struct ScriptContext* context)
{
    computer_set_globals(context->value->shorts[0], context->value->shorts[1],
        context->value->longs[1], context->value->longs[2], context->value->longs[3], context->value->longs[4],
        context->value->longs[5], context->value->longs[6], context->value->longs[7]);
}

/** SET_COMPUTER_PROCESS and SET_COMPUTER_CHECKS: a player range, the name and five numbers. */
static void set_computer_process_check(const struct ScriptLine* scline)
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

static void set_computer_process_process(struct ScriptContext* context)
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

static void set_computer_checks_process(struct ScriptContext* context)
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

static void set_computer_event_check(const struct ScriptLine* scline)
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
    if (!player_exists(get_player(plr_range_id)))
    {
        SCRPTERRLOG("Player %" PRId64 " does not exist; cannot modify events", (int64_t)plr_range_id);
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

static void set_computer_event_process(struct ScriptContext* context)
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

static void swap_creature_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    ThingModel ncrt_id = scline->np[0];
    ThingModel crtr_id = scline->np[1];

    value->shorts[0] = ncrt_id;
    value->shorts[1] = crtr_id;
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void swap_creature_process(struct ScriptContext* context)
{
    ThingModel ncrt_id = context->value->shorts[0];
    ThingModel crtr_id = context->value->shorts[1];

    if (!swap_creature(ncrt_id, crtr_id))
    {
        SCRPTERRLOG("Error swapping creatures '%s'<->'%s'", creature_code_name(ncrt_id), creature_code_name(crtr_id));
    }
}

static void set_digger_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    ThingModel crtr_id = get_rid(creature_desc, scline->tp[1]);

    if (crtr_id == -1)
    {
        SCRPTERRLOG("Unknown creature, '%s'", scline->tp[1]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    value->shorts[0] = crtr_id;
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void set_digger_process(struct ScriptContext* context)
{
    ThingModel new_dig_model = context->value->shorts[0];
    PlayerNumber plyr_idx = context->player_idx;

    update_players_special_digger_model(plyr_idx, new_dig_model);
}

static void set_next_level_check(const struct ScriptLine* scline)
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

static void set_next_level_process(struct ScriptContext* context)
{
    TbBool force_now = context->value->shorts[2];
    intralvl.next_level = context->value->shorts[1];
    if (force_now)
    {
        set_continue_level_number(intralvl.next_level);
    }
}

static void set_level_ensign_check(const struct ScriptLine* scline)
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

static void set_level_ensign_process(struct ScriptContext* context)
{
    set_level_ensign(context->value->shorts[1], context->value->shorts[2]);
}

static void show_bonus_level_check(const struct ScriptLine* scline)
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

static void show_bonus_level_process(struct ScriptContext* context)
{
    set_bonus_level_visibility(context->value->shorts[1], 1);
}
static void hide_bonus_level_process(struct ScriptContext* context)
{
    set_bonus_level_visibility(context->value->shorts[1], 0);
}

static void run_lua_code_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    const char* code = scline->tp[0];

    value->longs[0] = script_strdup(code);
    if (value->longs[0] < 0) {
        SCRPTERRLOG("Run out script strings space");
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    PROCESS_SCRIPT_VALUE(scline->command);
}

static void run_lua_code_process(struct ScriptContext* context)
{
    const char* code = script_strval(context->value->longs[0]);
    script_execute_lua_code_from_script(code);
}

static void set_generate_speed_check(const struct ScriptLine* scline)
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

static void set_generate_speed_process(struct ScriptContext* context)
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

static void tutorial_flash_button_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    int64_t id;
    if (kfx_game_state.level_file_version > 0)
    {
        if (parameter_is_number(scline->tp[0]))
        {
            id = atoi(scline->tp[0]);
            value->shorts[0] = GID_NONE;
        }
        else
        {
            static const struct NamedCommand *desc[4] = {room_desc, power_desc, trap_desc, door_desc};
            static const int64_t btn_group[4] = {GID_ROOM_PANE, GID_POWER_PANE, GID_TRAP_PANE, GID_DOOR_PANE};
            for (int64_t i = 0; i < 4; i++)
            {
                id = get_rid(desc[i], scline->tp[0]);
                if (id >= 0)
                {
                    value->shorts[0] = btn_group[i];
                    break;
                }
            }
            if (id < 0)
            {
                SCRPTERRLOG("Unrecognised parameter: %s", scline->tp[0]);
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
        }
    }
    else
    {
        id = scline->np[0];
    }
    if (id < 0)
    {
        SCRPTERRLOG("Button ID must be positive number");
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->shorts[1] = saturate_set_signed(id, 16);
    value->longs[1] = scline->np[1];
    PROCESS_SCRIPT_VALUE(scline->command);
}

static void tutorial_flash_button_process(struct ScriptContext* context)
{
    if (kfx_game_state.level_file_version > 0)
    {
        if (context->value->shorts[0] > GID_NONE)
        {
            int64_t button_id = ui_get_button_designation(context->value->shorts[0], context->value->shorts[1]);
            if (button_id >= 0)
            {
                ui_gui_set_button_flashing(button_id, context->value->longs[1]);
            }
        }
        else
        {
            ui_gui_set_button_flashing(context->value->shorts[1], context->value->longs[1]);
        }
    }
    else
    {
        ui_gui_set_button_flashing(context->value->shorts[1], context->value->longs[1]);
    }
}

static void trigger_action_point_process(struct ScriptContext* context)
{
    action_point_trigger_idx(context->value->longs[0], context->value->chars[4]);
}

/******************************************************************************/
/* The original game's commands (refactor pass 4, S09): were lvl_script_commands_old.c's command_*() (now the
 * checks) and script_process_value()'s switch (now the processes). A command's values are kept in the script value
 * as before (command_add_value(): longs[0..2], so a saved level in progress keeps working). */

static void room_available_check(const struct ScriptLine *scline)
{
    int64_t room_id = get_rid(room_desc, scline->tp[1]);
    if (room_id == -1)
    {
      SCRPTERRLOG("Unknown room name, '%s'", scline->tp[1]);
      return;
    }
    command_add_value(scline->command, scline->np[0], room_id, scline->np[2], scline->np[3]);
}

static void room_available_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    set_room_available(context->player_idx, value->longs[0], value->longs[1], value->longs[2]);
}

/** CREATURE_AVAILABLE: level files before version 1 gave only the forced availability (their third value). */
static void creature_available_check(const struct ScriptLine *scline)
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

static void creature_available_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    if (!set_creature_available(context->player_idx, value->longs[0], value->longs[1], value->longs[2])) {
        WARNLOG("Setting creature %s availability for player %" PRId64 " failed.",creature_code_name(value->longs[0]),(int64_t)context->player_idx);
    }
}

static void magic_available_check(const struct ScriptLine *scline)
{
    int64_t mag_id = get_rid(power_desc, scline->tp[1]);
    if (mag_id == -1)
    {
      SCRPTERRLOG("Unknown magic, '%s'", scline->tp[1]);
      return;
    }
    command_add_value(scline->command, scline->np[0], mag_id, scline->np[2], scline->np[3]);
}

static void magic_available_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    if (!set_power_available(context->player_idx, value->longs[0], value->longs[1], value->longs[2])) {
        WARNLOG("Setting power %s availability for player %" PRId64 " failed.",power_code_name(value->longs[0]),(int64_t)context->player_idx);
    }
}

static void trap_available_check(const struct ScriptLine *scline)
{
    int64_t trap_id = get_rid(trap_desc, scline->tp[1]);
    if (trap_id == -1)
    {
      SCRPTERRLOG("Unknown trap, '%s'", scline->tp[1]);
      return;
    }
    command_add_value(scline->command, scline->np[0], trap_id, scline->np[2], scline->np[3]);
}

static void trap_available_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    if (!set_trap_buildable_and_add_to_amount(context->player_idx, value->longs[0], value->longs[1], value->longs[2])) {
        WARNLOG("Setting trap %s availability for player %" PRId64 " failed.",trap_code_name(value->longs[0]),(int64_t)context->player_idx);
    }
}

static void door_available_check(const struct ScriptLine *scline)
{
    int64_t door_id = get_rid(door_desc, scline->tp[1]);
    if (door_id == -1)
    {
        SCRPTERRLOG("Unknown door, '%s'", scline->tp[1]);
        return;
    }
    command_add_value(scline->command, scline->np[0], door_id, scline->np[2], scline->np[3]);
}

static void door_available_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    set_door_buildable_and_add_to_amount(context->player_idx, value->longs[0], value->longs[1], value->longs[2]);
}

/**
 * Updates amount of RESEARCH points needed for the item to be researched.
 * Will not reorder the RESEARCH items.
 */
static void research_check(const struct ScriptLine *scline)
{
    int64_t item_type = get_rid(research_desc, scline->tp[1]);
    int64_t item_id = get_research_id(item_type, scline->tp[2], "command_research");
    if (item_id < 0)
      return;
    command_add_value(scline->command, scline->np[0], item_type, item_id, scline->np[3]);
}

static void research_process(struct ScriptContext *context)
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
static void research_order_check(const struct ScriptLine *scline)
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

static void research_order_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    if (!research_overriden_for_player(context->player_idx))
      remove_all_research_from_player(context->player_idx);
    add_research_to_player(context->player_idx, value->longs[0], value->longs[1], value->longs[2]);
}

/** A process runs once for each player in the value's range: true for the first, for the commands that act once
 *  whatever the range (the cases of script_process_value()'s old switch that didn't loop over the players). */
static TbBool legacy_value_first_player(const struct ScriptContext *context)
{
    int64_t plr_start;
    int64_t plr_end;
    if (get_players_range(context->value->plyr_range, &plr_start, &plr_end) < 0)
        return false;
    return (context->player_idx == plr_start);
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

static void set_timer_check(const struct ScriptLine *scline)
{
    int64_t timr_id = get_rid(timer_desc, scline->tp[1]);
    if (timr_id == -1)
    {
        SCRPTERRLOG("Unknown timer, '%s'", scline->tp[1]);
        return;
    }
    command_add_value(scline->command, scline->np[0], timr_id, 0, 0);
}

static void set_timer_process(struct ScriptContext *context)
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

static void set_flag_check(const struct ScriptLine *scline)
{
    legacy_flag_value(scline);
}

static void set_flag_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    set_variable(context->player_idx, value->longs[2], value->longs[0], value->longs[1]);
}

static void add_to_flag_check(const struct ScriptLine *scline)
{
    legacy_flag_value(scline);
}

static void add_to_flag_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    set_variable(context->player_idx, value->longs[2], value->longs[0], get_condition_value(context->player_idx, value->longs[2], value->longs[0]) + value->longs[1]);
}

static void randomise_flag_check(const struct ScriptLine *scline)
{
    legacy_flag_value(scline);
}

/** RANDOMISE_FLAG: from 1 to the value, or, for 0, to the flag's own value. */
static void randomise_flag_process(struct ScriptContext *context)
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

static void set_campaign_flag_check(const struct ScriptLine *scline)
{
    legacy_campaign_flag_value(scline);
}

static void set_campaign_flag_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    intralvl.campaign_flags[context->player_idx][value->longs[0]] = saturate_set_signed(value->longs[1], 32);
}

static void add_to_campaign_flag_check(const struct ScriptLine *scline)
{
    legacy_campaign_flag_value(scline);
}

static void add_to_campaign_flag_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    int64_t i = context->player_idx;
    intralvl.campaign_flags[i][value->longs[0]] = saturate_set_signed(intralvl.campaign_flags[i][value->longs[0]] + value->longs[1], 32);
}

static void export_variable_check(const struct ScriptLine *scline)
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

static void export_variable_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    int64_t i = context->player_idx;
    SYNCDBG(8, "Setting campaign flag[%" PRId64 "][%" PRId64 "] to %" PRId64 ".", (int64_t)(i), (int64_t)(value->longs[2]), (int64_t)(get_condition_value(i, value->longs[0], value->longs[1])));
    intralvl.campaign_flags[i][value->longs[2]] = get_condition_value(i, value->longs[0], value->longs[1]);
}

static void compute_flag_check(const struct ScriptLine *scline)
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
static void compute_flag_process(struct ScriptContext *context)
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

static void add_creature_to_pool_check(const struct ScriptLine *scline)
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

static void add_creature_to_pool_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    add_creature_to_pool(context->value->longs[0], context->value->longs[1]);
}

/** SET_CREATURE_HEALTH, _STRENGTH, _ARMOUR, _FEAR_WOUNDED, _FEAR_STRONGER, _FEARSOME_FACTOR: the creature and a value
 *  from 0 to max (what is checked; the processes saturate to the field). */
static void legacy_creature_value(const struct ScriptLine *scline, int64_t val, int64_t max, const char *what)
{
    const char *crtr_name = scline->tp[0];
    int64_t crtr_id = get_rid(creature_desc, crtr_name);
    if (crtr_id == -1)
    {
        SCRPTERRLOG("Unknown creature, '%s'", crtr_name);
        return;
    }
    if ((val < 0) || (val > max))
    {
        SCRPTERRLOG("Invalid '%s' %s value, %" PRId64, crtr_name, what, (int64_t)(val));
        return;
    }
    command_add_value(scline->command, ALL_PLAYERS, crtr_id, val, 0);
}

static void set_creature_health_check(const struct ScriptLine *scline)
{
    legacy_creature_value(scline, scline->np[1], USHRT_MAX, "health");
}

static void set_creature_health_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    change_max_health_of_creature_kind(context->value->longs[0], context->value->longs[1]);
}

static void set_creature_strength_check(const struct ScriptLine *scline)
{
    legacy_creature_value(scline, scline->np[1], USHRT_MAX, "strength");
}

static void set_creature_strength_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    struct CreatureModelConfig *crconf = creature_stats_get(context->value->longs[0]);
    if (creature_stats_invalid(crconf))
        return;
    crconf->strength = saturate_set_unsigned(context->value->longs[1], 16);
}

static void set_creature_armour_check(const struct ScriptLine *scline)
{
    legacy_creature_value(scline, scline->np[1], UCHAR_MAX, "armour");
}

static void set_creature_armour_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    struct CreatureModelConfig *crconf = creature_stats_get(context->value->longs[0]);
    if (creature_stats_invalid(crconf))
        return;
    crconf->armour = saturate_set_unsigned(context->value->longs[1], 8);
}

/** SET_CREATURE_FEAR_WOUNDED: a percentage; level files before version 1 gave it scaled 0..255. */
static void set_creature_fear_wounded_check(const struct ScriptLine *scline)
{
    if (scline->file_version > 0)
        legacy_creature_value(scline, scline->np[1], UCHAR_MAX, "fear");
    else
        legacy_creature_value(scline, 101*scline->np[1]/255, UCHAR_MAX, "fear");
}

static void set_creature_fear_wounded_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    struct CreatureModelConfig *crconf = creature_stats_get(context->value->longs[0]);
    if (creature_stats_invalid(crconf))
        return;
    crconf->fear_wounded = saturate_set_unsigned(context->value->longs[1], 8);
}

static void set_creature_fear_stronger_check(const struct ScriptLine *scline)
{
    legacy_creature_value(scline, scline->np[1], SHRT_MAX, "fear");
}

static void set_creature_fear_stronger_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    struct CreatureModelConfig *crconf = creature_stats_get(context->value->longs[0]);
    if (creature_stats_invalid(crconf))
        return;
    crconf->fear_stronger = saturate_set_unsigned(context->value->longs[1], 16);
}

static void set_creature_fearsome_factor_check(const struct ScriptLine *scline)
{
    legacy_creature_value(scline, scline->np[1], SHRT_MAX, "fearsome");
}

static void set_creature_fearsome_factor_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    struct CreatureModelConfig *crconf = creature_stats_get(context->value->longs[0]);
    if (creature_stats_invalid(crconf))
        return;
    crconf->fearsome_factor = saturate_set_unsigned(context->value->longs[1], 16);
}

static void set_creature_property_check(const struct ScriptLine *scline)
{
    const char *crtr_name = scline->tp[0];
    const char *property = scline->tp[1];
    int64_t crtr_id = get_rid(creature_desc, crtr_name);
    if (crtr_id == -1)
    {
        SCRPTERRLOG("Unknown creature, '%s'", crtr_name);
        return;
    }
    int64_t prop_id = get_rid(creatmodel_properties_commands, property);
    if (prop_id == -1)
    {
        SCRPTERRLOG("Unknown creature property kind, \"%s\"", property);
        return;
    }
    command_add_value(scline->command, ALL_PLAYERS, crtr_id, prop_id, scline->np[2]);
}

/** SET_CREATURE_PROPERTY: through the creature files' property table; a digger property changes who digs. */
static void set_creature_property_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    const struct ScriptValue *value = context->value;
    struct CreatureModelConfig *crconf = creature_stats_get(value->longs[0]);
    if (!creature_property_set(crconf, value->longs[1], value->longs[2]))
    {
        SCRPTERRLOG("Unknown creature property '%" PRId64 "'", (int64_t)(value->longs[1]));
        return;
    }
    const struct CreatureProperty *prop = creature_property_get(value->longs[1]);
    if ((prop->model_flags & (CMF_IsSpecDigger|CMF_IsDiggingCreature)) != 0)
    {
        recalculate_all_creature_digger_lists();
        ui_update_creatr_model_activities_list(1);
    }
}

static void kill_creature_check(const struct ScriptLine *scline)
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

static void level_up_creature_check(const struct ScriptLine *scline)
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

static void use_power_on_creature_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    const char *crtr_name = scline->tp[1];
    const char *criteria = scline->tp[2];
    int64_t caster_plyr_idx = scline->np[3];
    const char *magname = scline->tp[4];
    KeepPwrLevel power_level = scline->np[5];
    const char *freestring = scline->tp[6];
  SCRIPTDBG(11, "Starting");
  if (power_level < 1)
  {
    SCRPTWRNLOG("Spell %s level too low: %" PRId64 ", setting to 1.", magname, (int64_t)(power_level));
    power_level = 1;
  }
  if (power_level > MAGIC_OVERCHARGE_LEVELS)
  {
    SCRPTWRNLOG("Spell %s level too high: %" PRId64 ", setting to %" PRId64 ".", magname, (int64_t)(power_level), (int64_t)(MAGIC_OVERCHARGE_LEVELS));
    power_level = MAGIC_OVERCHARGE_LEVELS;
  }
  power_level--;
  int64_t mag_id = get_rid(power_desc, magname);
  if (mag_id == -1)
  {
    SCRPTERRLOG("Unknown magic, '%s'", magname);
    return;
  }
  ThingModel crtr_id = parse_creature_name(crtr_name);
  if (crtr_id == CREATURE_NONE) {
    SCRPTERRLOG("Unknown creature, '%s'", crtr_name);
    return;
  }
  int64_t select_id = parse_criteria(criteria);
  if (select_id == -1) {
    SCRPTERRLOG("Unknown select criteria, '%s'", criteria);
    return;
  }
  char free;
  if (parameter_is_number(freestring))
  {
      free = atoi(freestring);
  }
  else
  {
      free = get_id(is_free_desc, freestring);
  }
  if ((free < 0) || (free > 1))
  {
      SCRPTERRLOG("Unknown free value '%s' not recognized", freestring);
      return;
  }

  // encode params: free, magic, caster, level -> into 4xbyte: FMCL
  int64_t fmcl_bytes;
  {
      signed char f = free, m = mag_id, c = caster_plyr_idx, lvl = power_level;
      fmcl_bytes = (f << 24) | (m << 16) | (c << 8) | lvl;
  }
  command_add_value(scline->command, plr_range_id, crtr_id, select_id, fmcl_bytes);
}

static void use_power_at_pos_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    int64_t stl_x = scline->np[1];
    int64_t stl_y = scline->np[2];
    const char *magname = scline->tp[3];
    KeepPwrLevel power_level = scline->np[4];
    const char *freestring = scline->tp[5];
  SCRIPTDBG(11, "Starting");
  if (power_level < 1)
  {
    SCRPTWRNLOG("Spell %s level too low: %" PRId64 ", setting to 1.", magname, (int64_t)(power_level));
    power_level = 1;
  }
  if (power_level > MAGIC_OVERCHARGE_LEVELS)
  {
    SCRPTWRNLOG("Spell %s level too high: %" PRId64 ", setting to %" PRId64 ".", magname, (int64_t)(power_level), (int64_t)(MAGIC_OVERCHARGE_LEVELS));
    power_level = MAGIC_OVERCHARGE_LEVELS;
  }
  power_level--;
  int64_t mag_id = get_rid(power_desc, magname);
  if (mag_id == -1)
  {
    SCRPTERRLOG("Unknown magic, '%s'", magname);
    return;
  }
  char free;
  if (parameter_is_number(freestring))
  {
      free = atoi(freestring);
  }
  else
  {
      free = get_id(is_free_desc, freestring);
  }
  if ((free < 0) || (free > 1))
  {
      SCRPTERRLOG("Unknown free value '%s' not recognized", freestring);
      return;
  }

  // encode params: free, magic, level -> into 3xbyte: FML
  int64_t fml_bytes;
  {
      signed char f = free, m = mag_id, lvl = power_level;
      fml_bytes = (f << 16) | (m << 8) | lvl;
  }
  command_add_value(scline->command, plr_range_id, stl_x, stl_y, fml_bytes);
}

static void use_power_at_location_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    const char *locname = scline->tp[1];
    const char *magname = scline->tp[2];
    KeepPwrLevel power_level = scline->np[3];
    const char *freestring = scline->tp[4];
  SCRIPTDBG(11, "Starting");
  if (power_level < 1)
  {
    SCRPTWRNLOG("Spell %s level too low: %" PRId64 ", setting to 1.", magname, (int64_t)(power_level));
    power_level = 1;
  }
  if (power_level > MAGIC_OVERCHARGE_LEVELS)
  {
    SCRPTWRNLOG("Spell %s level too high: %" PRId64 ", setting to %" PRId64 ".", magname, (int64_t)(power_level), (int64_t)(MAGIC_OVERCHARGE_LEVELS));
    power_level = MAGIC_OVERCHARGE_LEVELS;
  }
  power_level--;
  int64_t mag_id = get_rid(power_desc, magname);
  if (mag_id == -1)
  {
    SCRPTERRLOG("Unknown magic, '%s'", magname);
    return;
  }

  TbMapLocation location;
  if (!get_map_location_id(locname, &location))
  {
    SCRPTWRNLOG("Use power script command at invalid location: %s", locname);
    return;
  }
  char free;
  if (parameter_is_number(freestring))
  {
      free = atoi(freestring);
  }
  else
  {
      free = get_id(is_free_desc, freestring);
  }
  if ((free < 0) || (free > 1))
  {
      SCRPTERRLOG("Unknown free value '%s' not recognized", freestring);
      return;
  }

  // encode params: free, magic, level -> into 3xbyte: FML
  int64_t fml_bytes;
  {
      signed char f = free, m = mag_id, lvl = power_level;
      fml_bytes = (f << 16) | (m << 8) | lvl;
  }
  command_add_value(scline->command, plr_range_id, location, fml_bytes, 0);
}

static void use_power_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    const char *magname = scline->tp[1];
    const char *freestring = scline->tp[2];
    SCRIPTDBG(11, "Starting");
    int64_t mag_id = get_rid(power_desc, magname);
    if (mag_id == -1)
    {
        SCRPTERRLOG("Unknown magic, '%s'", magname);
        return;
    }
    char free;
    if (parameter_is_number(freestring))
    {
        free = atoi(freestring);
    }
    else
    {
        free = get_id(is_free_desc, freestring);
    }
    if ((free < 0) || (free > 1))
    {
        SCRPTERRLOG("Unknown free value '%s' not recognized", freestring);
        return;
    }
    command_add_value(scline->command, plr_range_id, mag_id, free, 0);
}

static void use_special_increase_level_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    int64_t count = scline->np[1];
    if (count == 0)
    {
        SCRPTWRNLOG("Invalid count: %" PRId64 ", setting to 1.", (int64_t)(count));
        count = 1;
    }

    if (count > 9)
    {
        SCRPTWRNLOG("Count too high: %" PRId64 ", setting to 9.", (int64_t)(count));
        count = 9;
    }

    if (count < -9)
    {
        SCRPTWRNLOG("Count too low: %" PRId64 ", setting to -9.", (int64_t)(count));
        count = -9;
    }
    command_add_value(scline->command, plr_range_id, count, 0, 0);
}

static void use_special_multiply_creatures_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    int64_t count = scline->np[1];
    if (count < 1)
    {
        SCRPTWRNLOG("Invalid count: %" PRId64 ", setting to 1.", (int64_t)(count));
        count = 1;
    }

    if (count > 9)
    {
        SCRPTWRNLOG("Count too high: %" PRId64 ", setting to 9.", (int64_t)(count));
        count = 9;
    }
    command_add_value(scline->command, plr_range_id, count, 0, 0);
}

static void make_safe_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    command_add_value(scline->command, plr_range_id, 0, 0, 0);
}

static void locate_hidden_world_check(const struct ScriptLine *scline)
{
    command_add_value(scline->command, 0, 0, 0, 0);
}

static void kill_creature_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    script_kill_creatures(context->player_idx, value->longs[0], value->longs[1], value->longs[2]);
}

static void level_up_creature_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    script_level_up_creature(context->player_idx, value->longs[0], value->longs[1], value->longs[2]);
}

static void use_power_on_creature_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    script_use_power_on_creature_matching_criterion(context->player_idx, value->longs[0], value->longs[1], value->longs[2]);
}

static void use_power_at_pos_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    script_use_power_at_pos(context->player_idx, value->longs[0], value->longs[1], value->longs[2]);
}

static void use_power_at_location_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    script_use_power_at_location(context->player_idx, value->longs[0], value->longs[1]);
}

static void use_power_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    script_use_power(context->player_idx, value->longs[0], value->longs[1]);
}

static void use_special_increase_level_process(struct ScriptContext *context)
{
    script_use_special_increase_level(context->player_idx, context->value->longs[0]);
}

static void use_special_multiply_creatures_process(struct ScriptContext *context)
{
    for (int64_t count = 0; count < context->value->longs[0]; count++)
    {
      script_use_special_multiply_creatures(context->player_idx);
    }
}

static void make_safe_process(struct ScriptContext *context)
{
    script_make_safe(context->player_idx);
}

static void locate_hidden_world_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    script_locate_hidden_world();
}

static void max_creatures_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    int64_t val = scline->np[1];
    command_add_value(scline->command, plr_range_id, val, 0, 0);
}

static void dead_creatures_return_to_pool_check(const struct ScriptLine *scline)
{
    int64_t val = scline->np[0];
    command_add_value(scline->command, ALL_PLAYERS, val, 0, 0);
}

static void bonus_level_time_check(const struct ScriptLine *scline)
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

static void add_gold_to_player_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    int64_t amount = scline->np[1];
    command_add_value(scline->command, plr_range_id, amount, 0, 0);
}

static void set_creature_tendencies_check(const struct ScriptLine *scline)
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

static void reveal_map_rect_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    int64_t x = scline->np[1];
    int64_t y = scline->np[2];
    int64_t w = scline->np[3];
    int64_t h = scline->np[4];
    command_add_value(scline->command, plr_range_id, x, y, (h<<16)+w);
}

static void computer_dig_to_location_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    const char* origin = scline->tp[1];
    const char* destination = scline->tp[2];
    TbMapLocation orig_loc;
    if (!get_map_location_id(origin, &orig_loc))
    {
        SCRPTWRNLOG("Dig to location script command has invalid source location: %s", origin);
        return;
    }
    TbMapLocation dest_loc;
    if (!get_map_location_id(destination, &dest_loc))
    {
        SCRPTWRNLOG("Dig to location script command has invalid destination location: %s", destination);
        return;
    }

    command_add_value(scline->command, plr_range_id, orig_loc, dest_loc, 0);
}

static void change_creature_owner_check(const struct ScriptLine *scline)
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

static void make_unsafe_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    command_add_value(scline->command, plr_range_id, 0, 0, 0);
}

static void creature_entrance_level_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    unsigned char val = scline->np[1];
  command_add_value(scline->command, plr_range_id, val, 0, 0);
}

/** ALLY_PLAYERS: level files before version 1 have no third value: the players ally. */
static void ally_players_check(const struct ScriptLine *scline)
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
static void ally_players_process(struct ScriptContext *context)
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

static void max_creatures_process(struct ScriptContext *context)
{
    int64_t i = context->player_idx;
    int64_t param1 = context->value->longs[0];
    SYNCDBG(4,"Setting player %" PRId64 " max attracted creatures to %" PRId64 ".",(int64_t)i,(int64_t)param1);
    struct Dungeon *dungeon = get_dungeon(i);
    if (dungeon_invalid(dungeon))
        return;
    dungeon->max_creatures_attracted = param1;
}

static void dead_creatures_return_to_pool_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    set_flag_value(kfx_sim_state.mode_flags, MFlg_DeadBackToPool, context->value->longs[0]);
}

/** BONUS_LEVEL_TIME: the countdown in turns (0: none), and from level version 1 whether it shows real time. */
static void bonus_level_time_process(struct ScriptContext *context)
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

static void add_gold_to_player_process(struct ScriptContext *context)
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

static void set_creature_tendencies_process(struct ScriptContext *context)
{
    struct PlayerInfo *player = get_player(context->player_idx);
    set_creature_tendencies(player, context->value->longs[0], context->value->longs[1]);
    if (is_my_player(player)) {
        struct Dungeon *dungeon = get_players_dungeon(player);
        kfx_sim_state.creatures_tend_imprison = ((dungeon->creature_tendencies & CrTend_Imprison) != 0);
        kfx_sim_state.creatures_tend_flee = ((dungeon->creature_tendencies & CrTend_Flee) != 0);
    }
}

/** REVEAL_MAP_RECT: the centre, and the width and height packed in the third value. */
static void reveal_map_rect_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    player_reveal_map_area(context->player_idx, value->longs[0], value->longs[1], (value->longs[2])&0xffff, (value->longs[2]>>16)&0xffff);
}

static void computer_dig_to_location_process(struct ScriptContext *context)
{
    script_computer_dig_to_location(context->player_idx, context->value->longs[0], context->value->longs[1]);
}

static void change_creature_owner_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    script_change_creature_owner_with_criteria(context->player_idx, value->longs[0], value->longs[1], value->longs[2]);
}

static void make_unsafe_process(struct ScriptContext *context)
{
    script_make_unsafe(context->player_idx);
}

/** CREATURE_ENTRANCE_LEVEL: the level, from 1 (0: unchanged), for one player or all of them. */
static void creature_entrance_level_process(struct ScriptContext *context)
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

static void create_party_check(const struct ScriptLine *scline)
{
    const char *prtname = scline->tp[0];
    if (get_script_current_condition() != CONDITION_ALWAYS)
    {
        SCRPTWRNLOG("Party '%s' defined inside conditional statement",prtname);
    }
    create_party(prtname);
}

static void add_party_to_level_check(const struct ScriptLine *scline)
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

static void add_creature_to_level_check(const struct ScriptLine *scline)
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

static void start_money_check(const struct ScriptLine *scline)
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

static void if_action_point_check(const struct ScriptLine *scline)
{
    int64_t apt_num = scline->np[0];
    int64_t plr_range_id = scline->np[1];
    if (kfx_game_state.script.conditions_num >= CONDITIONS_COUNT)
    {
        SCRPTERRLOG("Too many (over %" PRId64 ") conditions in script", (int64_t)(CONDITIONS_COUNT));
        return;
    }
    // Check the Action Point
    int64_t apt_idx = action_point_number_to_index(apt_num);
    if (!action_point_exists_idx(apt_idx))
    {
        SCRPTERRLOG("Non-existing Action Point, no %" PRId64, (int64_t)(apt_num));
        return;
    }
    command_add_condition(plr_range_id, 0, SVar_ACTION_POINT_TRIGGERED, apt_idx, 0);
}

static void add_tunneller_to_level_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    const char *locname = scline->tp[1];
    const char *objectv = scline->tp[2];
    int64_t target = scline->np[3];
    CrtrExpLevel exp_level = scline->np[4];
    uint64_t carried_gold = scline->np[5];
    TbMapLocation location;
    TbMapLocation heading;
    if ((exp_level < 1) || (exp_level > CREATURE_MAX_LEVEL))
    {
        SCRPTERRLOG("Invalid CREATURE LEVEL parameter");
        return;
    }
    if (kfx_game_state.script.tunneller_triggers_num >= TUNNELLER_TRIGGERS_COUNT)
    {
        SCRPTERRLOG("Too many ADD_TUNNELLER commands in script");
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
    // Recognize place where party is going
    if (!get_map_heading_id(objectv, target, &heading))
        return;
    if (get_script_current_condition() == CONDITION_ALWAYS)
    {
        script_process_new_tunneler(plr_id, location, heading, exp_level-1, carried_gold);
    } else
    {
        struct TunnellerTrigger* tn_trig = &kfx_game_state.script.tunneller_triggers[kfx_game_state.script.tunneller_triggers_num % TUNNELLER_TRIGGERS_COUNT];
        set_flag_value(tn_trig->flags, TrgF_REUSABLE, next_command_reusable);
        clear_flag(tn_trig->flags, TrgF_DISABLED);
        tn_trig->plyr_idx = plr_id;
        tn_trig->location = location;
        tn_trig->heading = heading;
        tn_trig->carried_gold = carried_gold;
        tn_trig->exp_level = exp_level-1;
        tn_trig->carried_gold = carried_gold;
        tn_trig->party_id = 0;
        tn_trig->condit_idx = get_script_current_condition();
        kfx_game_state.script.tunneller_triggers_num++;
    }
}

static void win_game_check(const struct ScriptLine *scline)
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

static void lose_game_check(const struct ScriptLine *scline)
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

static void add_tunneller_party_to_level_check(const struct ScriptLine *scline)
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
    if ((exp_level < 1) || (exp_level > CREATURE_MAX_LEVEL))
    {
        SCRPTERRLOG("Invalid CREATURE LEVEL parameter");
        return;
    }
    if (kfx_game_state.script.tunneller_triggers_num >= TUNNELLER_TRIGGERS_COUNT)
    {
        SCRPTERRLOG("Too many ADD_TUNNELLER commands in script");
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
    // Recognize place where party is going
    if (!get_map_heading_id(objectv, target, &heading))
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
        struct TunnellerTrigger* tn_trig = &kfx_game_state.script.tunneller_triggers[kfx_game_state.script.tunneller_triggers_num % TUNNELLER_TRIGGERS_COUNT];
        set_flag_value(tn_trig->flags, TrgF_REUSABLE, next_command_reusable);
        clear_flag(tn_trig->flags, TrgF_DISABLED);
        tn_trig->plyr_idx = plr_id;
        tn_trig->location = location;
        tn_trig->heading = heading;
        tn_trig->carried_gold = carried_gold;
        tn_trig->exp_level = exp_level-1;
        tn_trig->carried_gold = carried_gold;
        tn_trig->party_id = prty_id+1;
        tn_trig->condit_idx = get_script_current_condition();
        kfx_game_state.script.tunneller_triggers_num++;
    }
}

static void if_slab_owner_check(const struct ScriptLine *scline)
{
    MapSlabCoord slb_x = scline->np[0];
    MapSlabCoord slb_y = scline->np[1];
    int64_t plr_range_id = scline->np[2];
    if (kfx_game_state.script.conditions_num >= CONDITIONS_COUNT)
    {
        SCRPTERRLOG("Too many (over %" PRId64 ") conditions in script", (int64_t)(CONDITIONS_COUNT));
        return;
    }
    command_add_condition(slb_x, 1, SVar_SLAB_OWNER, slb_y, plr_range_id);
}

static void if_slab_type_check(const struct ScriptLine *scline)
{
    MapSlabCoord slb_x = scline->np[0];
    MapSlabCoord slb_y = scline->np[1];
    int64_t slab_type = scline->np[2];
    if (kfx_game_state.script.conditions_num >= CONDITIONS_COUNT)
    {
        SCRPTERRLOG("Too many (over %" PRId64 ") conditions in script", (int64_t)(CONDITIONS_COUNT));
        return;
    }
    command_add_condition(slb_x, 1, SVar_SLAB_TYPE, slb_y, slab_type);
}

static void endif_check(const struct ScriptLine *scline)
{
    pop_condition();
}

/** NEXT_COMMAND_REUSABLE: the next line is kept and run each time its condition is met, not once. */
static void next_command_reusable_check(const struct ScriptLine *scline)
{
    next_command_reusable = 2;
}

static void level_version_check(const struct ScriptLine *scline)
{
    kfx_game_state.level_file_version = scline->np[0];
    SCRPTLOG("Level files version %" PRId64 ".",(int64_t)(kfx_game_state.level_file_version));
}

static void run_after_victory_check(const struct ScriptLine *scline)
{
    if (scline->np[0] == 1)
    {
        kfx_sim_state.run_after_victory = true;
    }
}

/** PRINT, MESSAGE: Dungeon Keeper Beta's; not supported. */
static void print_check(const struct ScriptLine *scline)
{
    SCRPTWRNLOG("Command '%s' is only supported in Dungeon Keeper Beta", "PRINT");
}

static void message_check(const struct ScriptLine *scline)
{
    SCRPTWRNLOG("Command '%s' is only supported in Dungeon Keeper Beta", "MESSAGE");
}

/**
 * Descriptions of script commands for parser.
 * Arguments are: A-string, N-integer, C-creature model, P-player, R-room kind, L-location, O-operator, S-slab kind, B-boolean
 * Lower case letters are optional arguments, Exclamation points sets 'extended' option, for example 'ANY_CREATURE' for creatures.
 */
const struct CommandDesc command_desc[] = {
  {"CREATE_PARTY",                      "A       ", Cmd_CREATE_PARTY, &create_party_check, NULL},
  {"ADD_TO_PARTY",                      "ACNNAN  ", Cmd_ADD_TO_PARTY, &add_to_party_check, NULL},
  {"DELETE_FROM_PARTY",                 "ACN     ", Cmd_DELETE_FROM_PARTY, &delete_from_party_check, NULL},
  {"ADD_PARTY_TO_LEVEL",                "PAAN    ", Cmd_ADD_PARTY_TO_LEVEL, &add_party_to_level_check, NULL},
  {"ADD_CREATURE_TO_LEVEL",             "PCANNNa ", Cmd_ADD_CREATURE_TO_LEVEL, &add_creature_to_level_check, NULL},
  {"ADD_OBJECT_TO_LEVEL",               "AANpa   ", Cmd_ADD_OBJECT_TO_LEVEL, &add_object_to_level_check, &add_object_to_level_process},
  {"IF",                                "PAOAa   ", Cmd_IF, &if_check, NULL},
  {"IF_ACTION_POINT",                   "NP      ", Cmd_IF_ACTION_POINT, &if_action_point_check, NULL},
  {"ENDIF",                             "        ", Cmd_ENDIF, &endif_check, NULL},
  {"SET_GENERATE_SPEED",                "Np      ", Cmd_SET_GENERATE_SPEED, &set_generate_speed_check, &set_generate_speed_process},
  {"REM",                               "        ", Cmd_REM, NULL, NULL},
  {"START_MONEY",                       "PN      ", Cmd_START_MONEY, &start_money_check, NULL},
  {"ROOM_AVAILABLE",                    "PRNN    ", Cmd_ROOM_AVAILABLE, &room_available_check, &room_available_process},
  {"CREATURE_AVAILABLE",                "PCNN    ", Cmd_CREATURE_AVAILABLE, &creature_available_check, &creature_available_process},
  {"MAGIC_AVAILABLE",                   "PANN    ", Cmd_MAGIC_AVAILABLE, &magic_available_check, &magic_available_process},
  {"TRAP_AVAILABLE",                    "PANN    ", Cmd_TRAP_AVAILABLE, &trap_available_check, &trap_available_process},
  {"RESEARCH",                          "PAAN    ", Cmd_RESEARCH, &research_check, &research_process},
  {"RESEARCH_ORDER",                    "PAAN    ", Cmd_RESEARCH_ORDER, &research_order_check, &research_order_process},
  {"COMPUTER_PLAYER",                   "PA      ", Cmd_COMPUTER_PLAYER, &computer_player_check, &computer_player_process},
  {"SET_TIMER",                         "PA      ", Cmd_SET_TIMER, &set_timer_check, &set_timer_process},
  {"ADD_TUNNELLER_TO_LEVEL",            "PAANNN  ", Cmd_ADD_TUNNELLER_TO_LEVEL, &add_tunneller_to_level_check, NULL},
  {"WIN_GAME",                          "        ", Cmd_WIN_GAME, &win_game_check, NULL},
  {"LOSE_GAME",                         "        ", Cmd_LOSE_GAME, &lose_game_check, NULL},
  {"SET_FLAG",                          "PAN     ", Cmd_SET_FLAG, &set_flag_check, &set_flag_process},
  {"MAX_CREATURES",                     "PN      ", Cmd_MAX_CREATURES, &max_creatures_check, &max_creatures_process},
  {"NEXT_COMMAND_REUSABLE",             "        ", Cmd_NEXT_COMMAND_REUSABLE, &next_command_reusable_check, NULL},
  {"DOOR_AVAILABLE",                    "PANN    ", Cmd_DOOR_AVAILABLE, &door_available_check, &door_available_process},
  {"DISPLAY_OBJECTIVE",                 "Ala     ", Cmd_DISPLAY_OBJECTIVE, &display_objective_check, &display_objective_process},
  {"DISPLAY_OBJECTIVE_WITH_POS",        "ANNa    ", Cmd_DISPLAY_OBJECTIVE_WITH_POS, &display_objective_check, &display_objective_process},
  {"DISPLAY_INFORMATION",               "Ala     ", Cmd_DISPLAY_INFORMATION, &display_information_check, &display_information_process},
  {"DISPLAY_INFORMATION_WITH_POS",      "ANN     ", Cmd_DISPLAY_INFORMATION_WITH_POS, &display_information_check, &display_information_process},
  {"DISPLAY_PLAYER_OBJECTIVE",          "APla    ", Cmd_DISPLAY_PLAYER_OBJECTIVE, &display_player_objective_check, &display_objective_process},
  {"DISPLAY_PLAYER_OBJECTIVE_WITH_POS", "APNNa   ", Cmd_DISPLAY_PLAYER_OBJECTIVE_WITH_POS, &display_player_objective_check, &display_objective_process},
  {"DISPLAY_PLAYER_INFORMATION",        "APl     ", Cmd_DISPLAY_PLAYER_INFORMATION, &display_player_information_check, &display_information_process},
  {"DISPLAY_PLAYER_INFORMATION_WITH_POS", "APNN    ", Cmd_DISPLAY_PLAYER_INFORMATION_WITH_POS, &display_player_information_check, &display_information_process},
  {"QUICK_OBJECTIVE",                   "NAla    ", Cmd_QUICK_OBJECTIVE, &quick_objective_check, &quick_objective_process},
  {"QUICK_OBJECTIVE_WITH_POS",          "NANNa   ", Cmd_QUICK_OBJECTIVE_WITH_POS, &quick_objective_check, &quick_objective_process},
  {"QUICK_INFORMATION",                 "NAla    ", Cmd_QUICK_INFORMATION, &quick_information_check, &quick_information_process},
  {"QUICK_INFORMATION_WITH_POS",        "NANN    ", Cmd_QUICK_INFORMATION_WITH_POS, &quick_information_check, &quick_information_process},
  {"QUICK_PLAYER_OBJECTIVE",            "NPala   ", Cmd_QUICK_PLAYER_OBJECTIVE, &quick_player_objective_check, &quick_objective_process},
  {"QUICK_PLAYER_OBJECTIVE_WITH_POS",   "NPANNa  ", Cmd_QUICK_PLAYER_OBJECTIVE_WITH_POS, &quick_player_objective_check, &quick_objective_process},
  {"QUICK_PLAYER_INFORMATION",          "NPAl    ", Cmd_QUICK_PLAYER_INFORMATION, &quick_player_information_check, &quick_information_process},
  {"QUICK_PLAYER_INFORMATION_WITH_POS", "NPANN   ", Cmd_QUICK_PLAYER_INFORMATION_WITH_POS, &quick_player_information_check, &quick_information_process},
  {"DISPLAY_MESSAGE",                   "AA      ", Cmd_DISPLAY_MESSAGE, &display_message_check, &display_message_process},
  {"QUICK_MESSAGE",                     "NAA     ", Cmd_QUICK_MESSAGE, &quick_message_check, &quick_message_process},
  {"CLEAR_MESSAGE",                     "n       ", Cmd_CLEAR_MESSAGE, &clear_message_check, &clear_message_process},
  {"HEART_LOST_OBJECTIVE",              "Nl      ", Cmd_HEART_LOST_OBJECTIVE, &heart_lost_objective_check, &heart_lost_objective_process},
  {"HEART_LOST_QUICK_OBJECTIVE",        "NAl     ", Cmd_HEART_LOST_QUICK_OBJECTIVE, &heart_lost_quick_objective_check, &heart_lost_quick_objective_process},
  {"ADD_TUNNELLER_PARTY_TO_LEVEL",      "PAAANNN ", Cmd_ADD_TUNNELLER_PARTY_TO_LEVEL, &add_tunneller_party_to_level_check, NULL},
  {"ADD_CREATURE_TO_POOL",              "CN      ", Cmd_ADD_CREATURE_TO_POOL, &add_creature_to_pool_check, &add_creature_to_pool_process},
  {"RESET_ACTION_POINT",                "Na      ", Cmd_RESET_ACTION_POINT, &reset_or_trigger_action_point_check, &reset_action_point_process},
  {"SET_CREATURE_MAX_LEVEL",            "PC!N    ", Cmd_SET_CREATURE_MAX_LEVEL, &set_creature_max_level_check, &set_creature_max_level_process},
  {"SET_MUSIC",                         "A       ", Cmd_SET_MUSIC, &set_music_check, &set_music_process},
  {"TUTORIAL_FLASH_BUTTON",             "AN      ", Cmd_TUTORIAL_FLASH_BUTTON, &tutorial_flash_button_check, &tutorial_flash_button_process},
  {"SET_CREATURE_STRENGTH",             "CN      ", Cmd_SET_CREATURE_STRENGTH, &set_creature_strength_check, &set_creature_strength_process},
  {"SET_CREATURE_HEALTH",               "CN      ", Cmd_SET_CREATURE_HEALTH, &set_creature_health_check, &set_creature_health_process},
  {"SET_CREATURE_ARMOUR",               "CN      ", Cmd_SET_CREATURE_ARMOUR, &set_creature_armour_check, &set_creature_armour_process},
  {"SET_CREATURE_FEAR_WOUNDED",         "CN      ", Cmd_SET_CREATURE_FEAR_WOUNDED, &set_creature_fear_wounded_check, &set_creature_fear_wounded_process},
  {"SET_CREATURE_FEAR_STRONGER",        "CN      ", Cmd_SET_CREATURE_FEAR_STRONGER, &set_creature_fear_stronger_check, &set_creature_fear_stronger_process},
  {"SET_CREATURE_FEARSOME_FACTOR",      "CN      ", Cmd_SET_CREATURE_FEARSOME_FACTOR, &set_creature_fearsome_factor_check, &set_creature_fearsome_factor_process},
  {"SET_CREATURE_PROPERTY",             "CAB     ", Cmd_SET_CREATURE_PROPERTY, &set_creature_property_check, &set_creature_property_process},
  {"IF_AVAILABLE",                      "PAOAa   ", Cmd_IF_AVAILABLE, &if_available_check, NULL},
  {"IF_CONTROLS",                       "PAOAa   ", Cmd_IF_CONTROLS,  &if_controls_check, NULL},
  {"SET_COMPUTER_GLOBALS",              "PNNNNNNn", Cmd_SET_COMPUTER_GLOBALS, &set_computer_globals_check, &set_computer_globals_process},
  {"SET_COMPUTER_CHECKS",               "PANNNNN ", Cmd_SET_COMPUTER_CHECKS, &set_computer_process_check, &set_computer_checks_process},
  {"SET_COMPUTER_EVENT",                "PANNNNN ", Cmd_SET_COMPUTER_EVENT, &set_computer_event_check, &set_computer_event_process},
  {"SET_COMPUTER_PROCESS",              "PANNNNN ", Cmd_SET_COMPUTER_PROCESS, &set_computer_process_check, &set_computer_process_process},
  {"ALLY_PLAYERS",                      "PPN     ", Cmd_ALLY_PLAYERS, &ally_players_check, &ally_players_process},
  {"DEAD_CREATURES_RETURN_TO_POOL",     "B       ", Cmd_DEAD_CREATURES_RETURN_TO_POOL, &dead_creatures_return_to_pool_check, &dead_creatures_return_to_pool_process},
  {"BONUS_LEVEL_TIME",                  "Nb      ", Cmd_BONUS_LEVEL_TIME, &bonus_level_time_check, &bonus_level_time_process},
  {"SWAP_CREATURE",                     "CC      ", Cmd_SWAP_CREATURE, &swap_creature_check, &swap_creature_process},
  {"PRINT",                             "A       ", Cmd_PRINT, &print_check, NULL},
  {"MESSAGE",                           "A       ", Cmd_MESSAGE, &message_check, NULL},
  {"PLAY_MESSAGE",                      "PAA     ", Cmd_PLAY_MESSAGE, &play_message_check, &play_message_process},
  {"ADD_GOLD_TO_PLAYER",                "PN      ", Cmd_ADD_GOLD_TO_PLAYER, &add_gold_to_player_check, &add_gold_to_player_process},
  {"SET_CREATURE_TENDENCIES",           "PAB     ", Cmd_SET_CREATURE_TENDENCIES, &set_creature_tendencies_check, &set_creature_tendencies_process},
  {"REVEAL_MAP_RECT",                   "PNNNN   ", Cmd_REVEAL_MAP_RECT, &reveal_map_rect_check, &reveal_map_rect_process},
  {"CONCEAL_MAP_RECT",                  "PNNNNb! ", Cmd_CONCEAL_MAP_RECT, &conceal_map_rect_check, &conceal_map_rect_process},
  {"REVEAL_MAP_LOCATION",               "PLN     ", Cmd_REVEAL_MAP_LOCATION, &reveal_map_location_check, &reveal_map_location_process},
  {"TAG_MAP_RECT",                      "PNNnn   ", Cmd_TAG_MAP_RECT, &tag_map_rect_check, &tag_map_rect_process},
  {"UNTAG_MAP_RECT",                    "PNNnn   ", Cmd_UNTAG_MAP_RECT, &tag_map_rect_check, &untag_map_rect_process},
  {"LEVEL_VERSION",                     "N       ", Cmd_LEVEL_VERSION, &level_version_check, NULL},
  {"KILL_CREATURE",                     "PC!AN   ", Cmd_KILL_CREATURE, &kill_creature_check, &kill_creature_process},
  {"COMPUTER_DIG_TO_LOCATION",          "PLL     ", Cmd_COMPUTER_DIG_TO_LOCATION, &computer_dig_to_location_check, &computer_dig_to_location_process},
  {"USE_POWER_ON_CREATURE",             "PC!APANA", Cmd_USE_POWER_ON_CREATURE, &use_power_on_creature_check, &use_power_on_creature_process},
  {"USE_POWER_ON_PLAYERS_CREATURES",    "PC!PANB!", Cmd_USE_POWER_ON_PLAYERS_CREATURES, &use_power_on_players_creatures_check, &use_power_on_players_creatures_process},
  {"USE_POWER_AT_POS",                  "PNNANA  ", Cmd_USE_POWER_AT_POS, &use_power_at_pos_check, &use_power_at_pos_process},
  {"USE_POWER_AT_LOCATION",             "PLANA   ", Cmd_USE_POWER_AT_LOCATION, &use_power_at_location_check, &use_power_at_location_process},
  {"USE_POWER",                         "PAA     ", Cmd_USE_POWER, &use_power_check, &use_power_process},
  {"USE_SPECIAL_INCREASE_LEVEL",        "PN      ", Cmd_USE_SPECIAL_INCREASE_LEVEL, &use_special_increase_level_check, &use_special_increase_level_process},
  {"USE_SPECIAL_MULTIPLY_CREATURES",    "PN      ", Cmd_USE_SPECIAL_MULTIPLY_CREATURES, &use_special_multiply_creatures_check, &use_special_multiply_creatures_process},
  {"MAKE_SAFE",                         "P       ", Cmd_MAKE_SAFE, &make_safe_check, &make_safe_process},
  {"USE_SPECIAL_MAKE_SAFE",             "P       ", Cmd_MAKE_SAFE, &make_safe_check, &make_safe_process}, // Legacy command
  {"LOCATE_HIDDEN_WORLD",               "        ", Cmd_LOCATE_HIDDEN_WORLD, &locate_hidden_world_check, &locate_hidden_world_process},
  {"USE_SPECIAL_LOCATE_HIDDEN_WORLD",   "        ", Cmd_LOCATE_HIDDEN_WORLD, &locate_hidden_world_check, &locate_hidden_world_process}, // Legacy command
  {"USE_SPECIAL_TRANSFER_CREATURE",     "P       ", Cmd_USE_SPECIAL_TRANSFER_CREATURE, &special_transfer_creature_check, &special_transfer_creature_process},
  {"TRANSFER_CREATURE",                 "PC!An   ", Cmd_TRANSFER_CREATURE, &script_transfer_creature_check, &script_transfer_creature_process},
  {"CHANGE_CREATURES_ANNOYANCE",        "PC!AN   ", Cmd_CHANGE_CREATURES_ANNOYANCE, &change_creatures_annoyance_check, &change_creatures_annoyance_process},
  {"ADD_TO_FLAG",                       "PAN     ", Cmd_ADD_TO_FLAG, &add_to_flag_check, &add_to_flag_process},
  {"SET_CAMPAIGN_FLAG",                 "PAN     ", Cmd_SET_CAMPAIGN_FLAG, &set_campaign_flag_check, &set_campaign_flag_process},
  {"ADD_TO_CAMPAIGN_FLAG",              "PAN     ", Cmd_ADD_TO_CAMPAIGN_FLAG, &add_to_campaign_flag_check, &add_to_campaign_flag_process},
  {"EXPORT_VARIABLE",                   "PAA     ", Cmd_EXPORT_VARIABLE, &export_variable_check, &export_variable_process},
  {"RUN_AFTER_VICTORY",                 "B       ", Cmd_RUN_AFTER_VICTORY, &run_after_victory_check, NULL},
  {"SET_NEXT_LEVEL",                    "N       ", Cmd_SET_NEXT_LEVEL, &set_next_level_check, &set_next_level_process},
  {"SHOW_BONUS_LEVEL",                  "N       ", Cmd_SHOW_BONUS_LEVEL, &show_bonus_level_check, &show_bonus_level_process},
  {"HIDE_BONUS_LEVEL",                  "N       ", Cmd_HIDE_BONUS_LEVEL, &show_bonus_level_check, &hide_bonus_level_process},
  {"SET_LEVEL_ENSIGN",                  "NA      ", Cmd_SET_LEVEL_ENSIGN, &set_level_ensign_check, &set_level_ensign_process},
  {"LEVEL_UP_CREATURE",                 "PC!AN   ", Cmd_LEVEL_UP_CREATURE, &level_up_creature_check, &level_up_creature_process},
  {"LEVEL_UP_PLAYERS_CREATURES",        "PC!n    ", Cmd_LEVEL_UP_PLAYERS_CREATURES, &level_up_players_creatures_check, level_up_players_creatures_process},
  {"CHANGE_CREATURE_OWNER",             "PC!AP   ", Cmd_CHANGE_CREATURE_OWNER, &change_creature_owner_check, &change_creature_owner_process},
  {"SET_GAME_RULE",                     "AAa     ", Cmd_SET_GAME_RULE, &set_game_rule_check, &set_game_rule_process},
  {"SET_ROOM_CONFIGURATION",            "AAAan   ", Cmd_SET_ROOM_CONFIGURATION, &set_room_configuration_check, &set_room_configuration_process},
  {"SET_TRAP_CONFIGURATION",            "AAAnnn  ", Cmd_SET_TRAP_CONFIGURATION, &set_trap_configuration_check, &set_trap_configuration_process},
  {"SET_DOOR_CONFIGURATION",            "AAAn    ", Cmd_SET_DOOR_CONFIGURATION, &set_door_configuration_check, &set_door_configuration_process},
  {"SET_OBJECT_CONFIGURATION",          "AAAnnn  ", Cmd_SET_OBJECT_CONFIGURATION, &set_object_configuration_check, &set_object_configuration_process},
  {"SET_CREATURE_CONFIGURATION",        "CAAaaaaa", Cmd_SET_CREATURE_CONFIGURATION, &set_creature_configuration_check, &set_creature_configuration_process},
  {"SET_SACRIFICE_RECIPE",              "AAA+    ", Cmd_SET_SACRIFICE_RECIPE, &set_sacrifice_recipe_check, &set_sacrifice_recipe_process},
  {"REMOVE_SACRIFICE_RECIPE",           "A+      ", Cmd_REMOVE_SACRIFICE_RECIPE, &remove_sacrifice_recipe_check, &set_sacrifice_recipe_process},
  {"SET_BOX_TOOLTIP",                   "NA      ", Cmd_SET_BOX_TOOLTIP, &set_box_tooltip_check, &set_box_tooltip_process},
  {"SET_BOX_TOOLTIP_ID",                "NA      ", Cmd_SET_BOX_TOOLTIP_ID, &set_box_tooltip_id_check, &set_box_tooltip_id_process},
  {"CHANGE_SLAB_OWNER",                 "NNPa    ", Cmd_CHANGE_SLAB_OWNER, &change_slab_owner_check, &change_slab_owner_process},
  {"CHANGE_SLAB_TYPE",                  "NNSa    ", Cmd_CHANGE_SLAB_TYPE, &change_slab_type_check, &change_slab_type_process},
  {"CREATE_EFFECTS_LINE",               "LLNNNA  ", Cmd_CREATE_EFFECTS_LINE, &create_effects_line_check, &create_effects_line_process},
  {"IF_SLAB_OWNER",                     "NNP     ", Cmd_IF_SLAB_OWNER, &if_slab_owner_check, NULL},
  {"IF_SLAB_TYPE",                      "NNS     ", Cmd_IF_SLAB_TYPE, &if_slab_type_check, NULL},
  {"USE_SPELL_ON_CREATURE",             "PC!AAn  ", Cmd_USE_SPELL_ON_CREATURE, &use_spell_on_creature_check, &use_spell_on_creature_process},
  {"USE_SPELL_ON_PLAYERS_CREATURES",    "PC!An   ", Cmd_USE_SPELL_ON_PLAYERS_CREATURES, &use_spell_on_players_creatures_check, &use_spell_on_players_creatures_process},
  {"SET_HEART_HEALTH",                  "PN      ", Cmd_SET_HEART_HEALTH, &set_heart_health_check, &set_heart_health_process},
  {"ADD_HEART_HEALTH",                  "PNb     ", Cmd_ADD_HEART_HEALTH, &add_heart_health_check, &add_heart_health_process},
  {"CREATURE_ENTRANCE_LEVEL",           "PN      ", Cmd_CREATURE_ENTRANCE_LEVEL, &creature_entrance_level_check, &creature_entrance_level_process},
  {"RANDOMISE_FLAG",                    "PAn     ", Cmd_RANDOMISE_FLAG, &randomise_flag_check, &randomise_flag_process},
  {"RANDOMIZE_FLAG",                    "PAn     ", Cmd_RANDOMISE_FLAG, &randomise_flag_check, &randomise_flag_process},
  {"COMPUTE_FLAG",                      "PAAPAb  ", Cmd_COMPUTE_FLAG, &compute_flag_check, &compute_flag_process},
  {"DISPLAY_TIMER",                     "PAb     ", Cmd_DISPLAY_TIMER, &display_timer_check, &display_timer_process},
  {"ADD_TO_TIMER",                      "PAN     ", Cmd_ADD_TO_TIMER, &add_to_timer_check, &add_to_timer_process},
  {"ADD_BONUS_TIME",                    "N       ", Cmd_ADD_BONUS_TIME, &add_bonus_time_check, &add_bonus_time_process},
  {"DISPLAY_VARIABLE",                  "PAnn    ", Cmd_DISPLAY_VARIABLE, &display_variable_check, &display_variable_process},
  {"DISPLAY_VARIABLE_WITH_LABEL",       "PAa    ", Cmd_DISPLAY_VARIABLE_WITH_LABEL, &display_variable_with_label_check, &display_variable_with_label_process},
  {"DISPLAY_COUNTDOWN",                 "PANb    ", Cmd_DISPLAY_COUNTDOWN, &display_countdown_check, &display_timer_process},
  {"HIDE_TIMER",                        "        ", Cmd_HIDE_TIMER, &cmd_no_param_check, &hide_timer_process},
  {"HIDE_VARIABLE",                     "pa      ", Cmd_HIDE_VARIABLE, &hide_variable_check, &hide_variable_process},
  {"CREATE_EFFECT",                     "AAn     ", Cmd_CREATE_EFFECT, &create_effect_check, &create_effect_process},
  {"CREATE_EFFECT_AT_POS",              "ANNn    ", Cmd_CREATE_EFFECT_AT_POS, &create_effect_at_pos_check, &create_effect_at_pos_process},
  {"SET_DOOR",                          "ANN     ", Cmd_SET_DOOR, &set_door_check, &set_door_process},
  {"PLACE_DOOR",                        "PANNb!b!", Cmd_PLACE_DOOR, &place_door_check, &place_door_process},
  {"PLACE_TRAP",                        "PANNb!  ", Cmd_PLACE_TRAP, &place_trap_check, &place_trap_process },
  {"ZOOM_TO_LOCATION",                  "PL      ", Cmd_MOVE_PLAYER_CAMERA_TO, &player_zoom_to_check, &player_zoom_to_process},
  {"SET_CREATURE_INSTANCE",             "CNAN    ", Cmd_SET_CREATURE_INSTANCE, &set_creature_instance_check, &set_creature_instance_process},
  {"SET_HAND_RULE",                     "PC!Aaaa ", Cmd_SET_HAND_RULE, &set_hand_rule_check, &set_hand_rule_process},
  {"MOVE_CREATURE",                     "PC!ANLa ", Cmd_MOVE_CREATURE, &move_creature_check, &move_creature_process},
  {"COUNT_CREATURES_AT_ACTION_POINT",   "NPC!PA  ", Cmd_COUNT_CREATURES_AT_ACTION_POINT, &count_creatures_at_action_point_check, &count_creatures_at_action_point_process},
  {"IF_ALLIED",                         "PPON    ", Cmd_IF_ALLIED, &if_allied_check, NULL},
  {"SET_TEXTURE",                       "PA      ", Cmd_SET_TEXTURE, &set_texture_check, &set_texture_process},
  {"HIDE_HERO_GATE",                    "NB      ", Cmd_HIDE_HERO_GATE, &hide_hero_gate_check, &hide_hero_gate_process},
  {"NEW_TRAP_TYPE",                     "A       ", Cmd_NEW_TRAP_TYPE, &new_trap_type_check, &null_process},
  {"NEW_OBJECT_TYPE",                   "A       ", Cmd_NEW_OBJECT_TYPE, &new_object_type_check, &null_process},
  {"NEW_ROOM_TYPE",                     "A       ", Cmd_NEW_ROOM_TYPE, &new_room_type_check, &null_process},
  {"NEW_CREATURE_TYPE",                 "A       ", Cmd_NEW_CREATURE_TYPE, &new_creature_type_check, &null_process},
  {"COPY_CREATURE_TYPE",                "CA      ", Cmd_COPY_CREATURE_TYPE, &copy_creature_type_check, &null_process },
  {"SET_HAND_GRAPHIC",                  "PA      ", Cmd_SET_HAND_GRAPHIC, &set_power_hand_check, &set_power_hand_process},
  {"ADD_EFFECT_GENERATOR_TO_LEVEL",     "AAN     ", Cmd_ADD_EFFECT_GENERATOR_TO_LEVEL, &add_effectgen_to_level_check, &add_effectgen_to_level_process},
  {"SET_EFFECT_GENERATOR_CONFIGURATION","AAAnn   ", Cmd_SET_EFFECT_GENERATOR_CONFIGURATION, &set_effectgen_configuration_check, &set_effectgen_configuration_process},
  {"SET_POWER_CONFIGURATION",           "AAAa    ", Cmd_SET_POWER_CONFIGURATION, &set_power_configuration_check, &set_power_configuration_process},
  {"SET_PLAYER_COLOR",                  "PA      ", Cmd_SET_PLAYER_COLOUR, &set_player_colour_check, &set_player_colour_process},
  {"SET_PLAYER_COLOUR",                 "PA      ", Cmd_SET_PLAYER_COLOUR, &set_player_colour_check, &set_player_colour_process},
  {"MAKE_UNSAFE",                       "P       ", Cmd_MAKE_UNSAFE, &make_unsafe_check, &make_unsafe_process},
  {"SET_INCREASE_ON_EXPERIENCE",        "AN      ", Cmd_SET_INCREASE_ON_EXPERIENCE, &set_increase_on_experience_check, &set_increase_on_experience_process},
  {"SET_PLAYER_MODIFIER",               "PAN     ", Cmd_SET_PLAYER_MODIFIER, &set_player_modifier_check, &set_player_modifier_process},
  {"ADD_TO_PLAYER_MODIFIER",            "PAN     ", Cmd_ADD_TO_PLAYER_MODIFIER, &add_to_player_modifier_check, &add_to_player_modifier_process},
  {"CHANGE_SLAB_TEXTURE",               "NNAa    ", Cmd_CHANGE_SLAB_TEXTURE , &change_slab_texture_check, &change_slab_texture_process},
  {"ADD_OBJECT_TO_LEVEL_AT_POS",        "ANNNpa  ", Cmd_ADD_OBJECT_TO_LEVEL_AT_POS, &add_object_to_level_at_pos_check, &add_object_to_level_at_pos_process},
  {"LOCK_POSSESSION",                   "PB!     ", Cmd_LOCK_POSSESSION, &lock_possession_check, &lock_possession_process},
  {"SET_DIGGER",                        "PC      ", Cmd_SET_DIGGER , &set_digger_check, &set_digger_process},
  {"RUN_LUA_CODE",                      "A       ", Cmd_RUN_LUA_CODE , &run_lua_code_check, &run_lua_code_process},
  {"TRIGGER_ACTION_POINT",              "Na      ", Cmd_TRIGGER_ACTION_POINT, &reset_or_trigger_action_point_check, &trigger_action_point_process},
  {NULL,                                "        ", Cmd_NONE, NULL, NULL},
};

const struct CommandDesc dk1_command_desc[] = {
  {"CREATE_PARTY",                 "A       ", Cmd_CREATE_PARTY, &create_party_check, NULL},
  {"ADD_TO_PARTY",                 "ACNNAN  ", Cmd_ADD_TO_PARTY, &add_to_party_check, NULL},
  {"ADD_PARTY_TO_LEVEL",           "PAAN    ", Cmd_ADD_PARTY_TO_LEVEL, &add_party_to_level_check, NULL},
  {"ADD_CREATURE_TO_LEVEL",        "PCANNN  ", Cmd_ADD_CREATURE_TO_LEVEL, &add_creature_to_level_check, NULL},
  {"IF",                           "PAOAa   ", Cmd_IF, &if_check, NULL},
  {"IF_ACTION_POINT",              "NP      ", Cmd_IF_ACTION_POINT, &if_action_point_check, NULL},
  {"ENDIF",                        "        ", Cmd_ENDIF, &endif_check, NULL},
  {"SET_GENERATE_SPEED",           "N       ", Cmd_SET_GENERATE_SPEED, &set_generate_speed_check, &set_generate_speed_process},
  {"REM",                          "        ", Cmd_REM, NULL, NULL},
  {"START_MONEY",                  "PN      ", Cmd_START_MONEY, &start_money_check, NULL},
  {"ROOM_AVAILABLE",               "PRNN    ", Cmd_ROOM_AVAILABLE, &room_available_check, &room_available_process},
  {"CREATURE_AVAILABLE",           "PCNN    ", Cmd_CREATURE_AVAILABLE, &creature_available_check, &creature_available_process},
  {"MAGIC_AVAILABLE",              "PANN    ", Cmd_MAGIC_AVAILABLE, &magic_available_check, &magic_available_process},
  {"TRAP_AVAILABLE",               "PANN    ", Cmd_TRAP_AVAILABLE, &trap_available_check, &trap_available_process},
  {"RESEARCH",                     "PAAN    ", Cmd_RESEARCH_ORDER, &research_order_check, &research_order_process},
  {"COMPUTER_PLAYER",              "PN      ", Cmd_COMPUTER_PLAYER, computer_player_check, computer_player_process},
  {"SET_TIMER",                    "PA      ", Cmd_SET_TIMER, &set_timer_check, &set_timer_process},
  {"ADD_TUNNELLER_TO_LEVEL",       "PAANNN  ", Cmd_ADD_TUNNELLER_TO_LEVEL, &add_tunneller_to_level_check, NULL},
  {"WIN_GAME",                     "        ", Cmd_WIN_GAME, &win_game_check, NULL},
  {"LOSE_GAME",                    "        ", Cmd_LOSE_GAME, &lose_game_check, NULL},
  {"SET_FLAG",                     "PAN     ", Cmd_SET_FLAG, &set_flag_check, &set_flag_process},
  {"MAX_CREATURES",                "PN      ", Cmd_MAX_CREATURES, &max_creatures_check, &max_creatures_process},
  {"NEXT_COMMAND_REUSABLE",        "        ", Cmd_NEXT_COMMAND_REUSABLE, &next_command_reusable_check, NULL},
  {"DOOR_AVAILABLE",               "PANN    ", Cmd_DOOR_AVAILABLE, &door_available_check, &door_available_process},
  {"DISPLAY_OBJECTIVE",            "AA      ", Cmd_DISPLAY_OBJECTIVE, &display_objective_check, &display_objective_process},
  {"DISPLAY_OBJECTIVE_WITH_POS",   "ANN     ", Cmd_DISPLAY_OBJECTIVE_WITH_POS, &display_objective_check, &display_objective_process},
  {"DISPLAY_INFORMATION",          "A       ", Cmd_DISPLAY_INFORMATION, &display_information_check, &display_information_process},
  {"DISPLAY_INFORMATION_WITH_POS", "ANN     ", Cmd_DISPLAY_INFORMATION_WITH_POS, &display_information_check, &display_information_process},
  {"ADD_TUNNELLER_PARTY_TO_LEVEL", "PAAANNN ", Cmd_ADD_TUNNELLER_PARTY_TO_LEVEL, &add_tunneller_party_to_level_check, NULL},
  {"ADD_CREATURE_TO_POOL",         "CN      ", Cmd_ADD_CREATURE_TO_POOL, &add_creature_to_pool_check, &add_creature_to_pool_process},
  {"RESET_ACTION_POINT",           "N       ", Cmd_RESET_ACTION_POINT, &reset_or_trigger_action_point_check, &reset_action_point_process},
  {"SET_CREATURE_MAX_LEVEL",       "PC!N    ", Cmd_SET_CREATURE_MAX_LEVEL, &set_creature_max_level_check, &set_creature_max_level_process},
  {"SET_MUSIC",                    "N       ", Cmd_SET_MUSIC, NULL, NULL},
  {"TUTORIAL_FLASH_BUTTON",        "NN      ", Cmd_TUTORIAL_FLASH_BUTTON, &tutorial_flash_button_check, &tutorial_flash_button_process},
  {"SET_CREATURE_STRENGTH",        "CN      ", Cmd_SET_CREATURE_STRENGTH, &set_creature_strength_check, &set_creature_strength_process},
  {"SET_CREATURE_HEALTH",          "CN      ", Cmd_SET_CREATURE_HEALTH, &set_creature_health_check, &set_creature_health_process},
  {"SET_CREATURE_ARMOUR",          "CN      ", Cmd_SET_CREATURE_ARMOUR, &set_creature_armour_check, &set_creature_armour_process},
  {"SET_CREATURE_FEAR",            "CN      ", Cmd_SET_CREATURE_FEAR_WOUNDED, &set_creature_fear_wounded_check, &set_creature_fear_wounded_process},
  {"IF_AVAILABLE",                 "PAOAa   ", Cmd_IF_AVAILABLE, &if_available_check, NULL},
  {"SET_COMPUTER_GLOBALS",         "PNNNNNN ", Cmd_SET_COMPUTER_GLOBALS, &set_computer_globals_check, &set_computer_globals_process},
  {"SET_COMPUTER_CHECKS",          "PANNNNN ", Cmd_SET_COMPUTER_CHECKS, &set_computer_process_check, &set_computer_checks_process},
  {"SET_COMPUTER_EVENT",           "PANN    ", Cmd_SET_COMPUTER_EVENT, &set_computer_event_check, &set_computer_event_process},
  {"SET_COMPUTER_PROCESS",         "PANNNNN ", Cmd_SET_COMPUTER_PROCESS, &set_computer_process_check, &set_computer_process_process},
  {"ALLY_PLAYERS",                 "PP      ", Cmd_ALLY_PLAYERS, &ally_players_check, &ally_players_process},
  {"DEAD_CREATURES_RETURN_TO_POOL","N       ", Cmd_DEAD_CREATURES_RETURN_TO_POOL, &dead_creatures_return_to_pool_check, &dead_creatures_return_to_pool_process},
  {"BONUS_LEVEL_TIME",             "N       ", Cmd_BONUS_LEVEL_TIME, &bonus_level_time_check, &bonus_level_time_process},
  {"QUICK_OBJECTIVE",              "NAA     ", Cmd_QUICK_OBJECTIVE, &quick_objective_check, &quick_objective_process},
  {"QUICK_INFORMATION",            "NA      ", Cmd_QUICK_INFORMATION, &quick_information_check, &quick_information_process},
  {"SWAP_CREATURE",                "CC      ", Cmd_SWAP_CREATURE, &swap_creature_check, &swap_creature_process},
  {"PRINT",                        "A       ", Cmd_PRINT, &print_check, NULL},
  {"MESSAGE",                      "A       ", Cmd_MESSAGE, &message_check, NULL},
  {"LEVEL_VERSION",                "N       ", Cmd_LEVEL_VERSION, &level_version_check, NULL},
  {NULL,                           "        ", Cmd_NONE, NULL, NULL},
};

#ifdef __cplusplus
}
#endif
