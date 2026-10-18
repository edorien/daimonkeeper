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
#include "lvl_script_conditions.h"
#include "lvl_script_lib.h"
#include "player_availability.h"

#include "ports/script_port.h"
#include "lvl_script_commands_private.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

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
TbBool parse_creature_bracket_varib(const char *varib_name, int64_t *varib_id, int64_t *varib_type)
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
  {"USE_SPECIAL_STEAL_HERO",            "PLa     ", Cmd_USE_SPECIAL_STEAL_HERO, &special_steal_hero_check, &special_steal_hero_process},
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
