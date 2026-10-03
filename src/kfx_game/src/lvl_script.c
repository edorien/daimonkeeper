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
#include <math.h>

#include "lvl_script.h"
#include "config_keeperfx.h"

#include "globals.h"
#include "player_instances.h"
#include "player_data.h"
#include "player_utils.h"
#include "thing_effects.h"
#include "bflib_math.h"
#include "engine_camera.h"
#include "lvl_filesdk1.h"
#include "level_script_override.h"
#include "main_game.h"

#include "lvl_script_conditions.h"
#include "lvl_script_value.h"
#include "lvl_script_commands_old.h"
#include "lvl_script_commands.h"
#include "game_lifecycle.h"
#include "creature_states_hero.h"
#include "kfx_game_state.h"
#include "ports/ui_port.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
extern TbBool luascript_loaded;
unsigned char next_command_reusable;
/******************************************************************************/

enum TokenType {
    TkInvalid = 0,
    TkCommand,
    TkString,
    TkOperator,
    TkNumber,
    TkOpen,
    TkClose,
    TkComma,
    TkEnd,
    TkRangeOperator, // ~ operator
};

struct CommandToken {
    enum TokenType type;
    char *start;
    char *end;
};

char* get_next_token(char *data, struct CommandToken *token)
{
    char *p = data;
    while (*p != 0)
    {
        size_t step = strspn(p, "\n\r \t");
        if (step == 0)
        {
            if (*p >= 0) // Erase non ASCII stuff from commands
            {
                break;
            }
            step = 1;
        }
        p += step;
    }
    token->start = p;
    if (isalnum(*p))
    {
        for (;isalnum(*p) || (*p == '[') || (*p == ']') || (*p == '_'); p++)
        {
            *p = (char)toupper(*p);
        }
        if (((p - token->start) == 3) && (strncmp(token->start, "REM", 3) == 0))
        {
            for (;*p; p++)
            {
                // empty
            }
            token->end = p;
            token->type = TkEnd;
            return p;
        }
        token->type = TkCommand;
    }
    else if (*p == '-') // Either operator or digit
    {
        p++;
        if (isdigit(*p))
        {
            for (;isdigit(*p); p++)
            {
                // empty
            }

            token->type = TkNumber;
        }
        else
        {
            token->type = TkOperator;
        }
    }
    else if (*p == '"')
    {
        p++;
        token->start = p;
        for (; *p != '"'; p++)
        {
            if (*p == 0)
            {
                token->type = TkInvalid;
                token->end = p;
                return p;
            }
        }
        token->end = p;
        p++;
        token->type = TkString;
        return p;
    }
    else if ( (*p == '=') || (*p == '!') || (*p == '<') || (*p == '>'))
    {
        p++;
        if (*p == '=')
        {
            p++;
        }
        token->type = TkOperator;
    }
    else if (*p == '~')
    {
        p++;
        token->type = TkRangeOperator;
    }
    else if (*p == '(')
    {
        p++;
        token->type = TkOpen;
    }
    else if (*p == ')')
    {
        p++;
        token->type = TkClose;
    }
    else if (*p == ',')
    {
        p++;
        token->type = TkComma;
    }
    else if (*p == 0)
    {
        token->type = TkEnd;
    }
    else
    {
        token->type = TkInvalid;
    }
    token->end = p;
    return p;
}

static struct CommandDesc const *find_command_desc(const struct CommandToken *token, const struct CommandDesc *cmdlist_desc)
{
    const struct CommandDesc* cmnd_desc = NULL;
    int64_t token_len = token->end - token->start;
    for (int64_t i = 0; cmdlist_desc[i].textptr != NULL; i++)
    {
        if ((cmdlist_desc[i].textptr[token_len] == 0) && (strncmp(cmdlist_desc[i].textptr, token->start, token_len) == 0))
        {
            cmnd_desc = &cmdlist_desc[i];
            break;
        }
    }
    return cmnd_desc;
}

/**
 * Returns if the command is 'preloaded'. Preloaded commands are initialized
 * before the whole level data is loaded.
 */
TbBool script_is_preloaded_command(int64_t cmnd_index)
{
    switch (cmnd_index)
    {
        case Cmd_LEVEL_VERSION:
        case Cmd_NEW_TRAP_TYPE:
        case Cmd_NEW_OBJECT_TYPE:
        case Cmd_NEW_ROOM_TYPE:
        case Cmd_NEW_CREATURE_TYPE:
        case Cmd_COPY_CREATURE_TYPE:
            return true;
        default:
            return false;
    }
}

#define get_players_range(plr_range_id, plr_start, plr_end) get_players_range_f(plr_range_id, plr_start, plr_end, __func__, text_line_number)
int64_t get_players_range_f(int64_t plr_range_id, int64_t *plr_start, int64_t *plr_end, const char *func_name, int64_t ln_num)
{
    *plr_start = 0;
    *plr_end = 0;
    if (plr_range_id < 0)
    {
        return -1;
    }
    if (plr_range_id == ALL_PLAYERS)
    {
        *plr_start = 0;
        *plr_end = PLAYERS_COUNT;
        return plr_range_id;
    } else
    if (plr_range_id < PLAYERS_COUNT)
    {
        *plr_start = plr_range_id;
        *plr_end = (*plr_start) + 1;
        return plr_range_id;
    }
    return -2;
}

int64_t script_strtol(const char *text, char **endptr, int64_t base)
{
    return LbStrToI32(text, endptr, base);
}

int64_t script_atol(const char *text)
{
    return LbAtoI32(text);
}

static TbBool script_command_param_to_number(char type_chr, struct ScriptLine *scline, int64_t idx, TbBool extended)
{
    switch (toupper(type_chr))
    {
        case 'N': //Number
        {
            char* text;
            scline->np[idx] = script_strtol(scline->tp[idx], &text, 0);
            //Extended number allows for a custom sprite string
            if (!extended)
            {
                if (text != &scline->tp[idx][strlen(scline->tp[idx])])
                {
                    SCRPTWRNLOG("Numerical value \"%s\" interpreted as %" PRId64, scline->tp[idx], (int64_t)(scline->np[idx]));
                }
            }
            break;
        }
        case 'P': //Player
        {
            int64_t plr_range_id;
            if (!get_player_id(scline->tp[idx], &plr_range_id))
            {
                return false;
            }
            scline->np[idx] = plr_range_id;
            break;
        }
        case 'C': //Creature
        {
            int64_t crtr_id = get_rid(creature_desc, scline->tp[idx]);
            if (extended)
            {
                if (crtr_id == -1)
                {
                    if (0 == strcmp(scline->tp[idx], "ANY_CREATURE"))
                    {
                        crtr_id = CREATURE_ANY;
                    }
                }
            }
            if (crtr_id == -1)
            {
                SCRPTERRLOG("Unknown creature, \"%s\"", scline->tp[idx]);
                return false;
            }
            scline->np[idx] = crtr_id;
            break;
        }
        case 'R': //Room
        {
            int64_t room_id = get_rid(room_desc, scline->tp[idx]);
            if (room_id == -1)
            {
                SCRPTERRLOG("Unknown room kind, \"%s\"", scline->tp[idx]);
                return false;
            }
            scline->np[idx] = room_id;
            break;
        }
        case 'S': //Slab
        {
            int64_t slab_id = get_rid(slab_desc, scline->tp[idx]);
            if (slab_id == -1)
            {
                SCRPTERRLOG("Unknown slab kind, \"%s\"", scline->tp[idx]);
                return false;
            }
            scline->np[idx] = slab_id;
            break;
        };
        case 'L': //Location
        {
            TbMapLocation loc;
            if (!get_map_location_id(scline->tp[idx], &loc)) {
                return false;
            }
            scline->np[idx] = loc;
            break;
        }
        case 'O': //Operator
        {
            int64_t opertr_id = get_rid(comparison_desc, scline->tp[idx]);
            if (opertr_id == -1) {
                SCRPTERRLOG("Unknown operator, \"%s\"", scline->tp[idx]);
                return false;
            }
            scline->np[idx] = opertr_id;
            break;
        }
        case 'B': //Boolean
        {
            char boolean = get_rid(script_boolean_desc, scline->tp[idx]);
            if (boolean == -1)
            {
                //Extended number allows for a custom sprite string
                if (!extended)
                {
                    SCRPTERRLOG("Unknown boolean value, \"%s\"", scline->tp[idx]);
                    return false;
                }
                else
                {
                    scline->np[idx] = -1;
                    break;
                }
            }
            scline->np[idx] = (boolean == true);
            break;
        }
        case 'A': //String
            break;
        case '!': // extended sign
            return true;
        default:
        {
            SCRPTWRNLOG("Excessive parameter of command \"%s\", value \"%s\"; ignoring", scline->tcmnd, scline->tp[idx]);
            return true;
        }
    }
    return true;
}

static TbBool is_condition_met(int64_t cond_idx)
{
    if (cond_idx >= CONDITIONS_COUNT)
    {
      if (cond_idx == CONDITION_ALWAYS)
          return true;
      else
          return false;
    }
    uint64_t i = kfx_game_state.script.conditions[cond_idx].status;
    return ((i & 0x01) != 0);
}

TbBool script_command_param_to_text(char type_chr, struct ScriptLine *scline, int64_t idx)
{
    switch (toupper(type_chr))
    {
    case 'N':
        snprintf(scline->tp[idx], MAX_TEXT_LENGTH, "%" PRId64, (int64_t)(scline->np[idx]));
        break;
    case 'P':
        strcpy(scline->tp[idx], player_code_name(scline->np[idx]));
        break;
    case 'C':
        strcpy(scline->tp[idx], creature_code_name(scline->np[idx]));
        break;
    case 'R':
        strcpy(scline->tp[idx], room_code_name(scline->np[idx]));
        break;
    case 'L':
        get_map_location_code_name(scline->np[idx], scline->tp[idx]);
        break;
    case 'S':
        strcpy(scline->tp[idx], slab_code_name(scline->np[idx]));
        break;
    case 'A':
        break;
    case '!': // extended sign
        return true;
    default:
        return false;
    }
    return true;
}

static int64_t count_required_parameters(const char *args)
{
    int64_t required = 0;
    for (int64_t i = 0; i < COMMANDDESC_ARGS_COUNT; i++)
    {
        char chr = args[i];
        if (isupper(chr)) // Required arguments have upper-case type letters
        {
            required++;
        }
        else if (chr == '!')
        {
            continue;
        }
        else
            break;
    }
    return required;
}

static int64_t script_recognize_params(char **line, const struct CommandDesc *cmd_desc, struct ScriptLine *scline, int64_t *para_level, int64_t expect_level, int64_t file_version);

static TbBool process_subfunc(char **line, struct ScriptLine *scline, const struct CommandDesc *cmd_desc, const struct CommandDesc *funcmd_desc, int64_t *para_level, int64_t src, int64_t dst, int64_t file_version)
{
    struct CommandToken token;
    struct ScriptLine* funscline = (struct ScriptLine*)calloc(1, sizeof(struct ScriptLine));
    if (funscline == NULL) {
        SCRPTERRLOG("Can't allocate buffer to recognize line");
        return false;
    }
    memset(funscline, 0, sizeof(struct ScriptLine));
    memcpy(funscline->tcmnd, scline->tp[dst], MAX_TEXT_LENGTH);
    char *nxt = get_next_token(*line, &token);
    if (token.type != TkOpen)
    {
        SCRPTERRLOG("Expecting (");
        free(funscline);
        return false;
    }
    *line = nxt;
    int64_t args_count = script_recognize_params(line, funcmd_desc, funscline, para_level, *para_level, file_version);
    if (args_count < 0)
    {
        free(funscline);
        return false;
    }
    // Count valid args
    if (args_count < COMMANDDESC_ARGS_COUNT)
    {
        int64_t required = count_required_parameters(funcmd_desc->args);
        if (args_count < required)
        {
            SCRPTERRLOG("Not enough parameters for \"%s\", got only %" PRId64, funcmd_desc->textptr,(int64_t)args_count);
            free(funscline);
            return false;
        }
    }
    switch (funcmd_desc->index)
    {
        case Cmd_RANDOM:
        case Cmd_DRAWFROM:{
            // Create array of value ranges
            int64_t range_total = 0;
            int64_t fi;
            struct MinMax ranges[COMMANDDESC_ARGS_COUNT];
            TbBool is_if_statement = ((scline->command == Cmd_IF) || (scline->command == Cmd_IF_AVAILABLE) || (scline->command == Cmd_IF_CONTROLS));
            if (level_file_version > 0)
            {
                char chr = cmd_desc->args[src];
                int64_t ri;
                for (fi = 0, ri = 0; fi < COMMANDDESC_ARGS_COUNT; fi++, ri++)
                {
                    if (funscline->tp[fi][0] == '\0') {
                        break;
                    }
                    if ((toupper(chr) == 'A') && (!is_if_statement) ) //Strings don't have a range, but IF statements have 'Aa' to allow both variable compare and numbers. Numbers are allowed, 'a' is a string for sure.
                    {
                        // Values which do not support range
                        if (strcmp(funscline->tp[fi],"~") == 0) {
                            SCRPTERRLOG("Parameter %" PRId64 " of function \"%s\" within command \"%s\" does not support range", (int64_t)(fi+1), funcmd_desc->textptr, scline->tcmnd);
                            free(funscline);
                            return false;
                        }
                        // Values of that type cannot define ranges, as we cannot interpret them
                        ranges[ri].min = fi;
                        ranges[ri].max = fi;
                        range_total++;
                    } else
                    if ((ri > 0) && (strcmp(funscline->tp[fi],"~") == 0))
                    {
                        // Second step of defining range
                        ri--;
                        fi++;
                        if (funscline->tp[fi][0] != '\0')
                        {
                            funscline->np[fi] = script_atol(funscline->tp[fi]);
                        }
                        if (!script_command_param_to_number(chr, funscline, fi, false)) {
                            SCRPTERRLOG("Parameter %" PRId64 " of function \"%s\" within command \"%s\" has unexpected range end value; discarding command", (int64_t)(fi+1), funcmd_desc->textptr, scline->tcmnd);
                            free(funscline);
                            return false;
                        }
                        ranges[ri].max = funscline->np[fi];
                        if (ranges[ri].max < ranges[ri].min) {
                            SCRPTWRNLOG("Range definition in argument of function \"%s\" within command \"%s\" should have lower value first", funcmd_desc->textptr, scline->tcmnd);
                            ranges[ri].max = ranges[ri].min;
                        }
                        range_total += ranges[ri].max - ranges[ri].min; // +1 was already added
                    } else
                    {
                        // Single value or first step of defining range
                        if (!script_command_param_to_number(chr, funscline, fi, false)) {
                            SCRPTERRLOG("Parameter %" PRId64 " of function \"%s\" within command \"%s\" has unexpected value; discarding command", (int64_t)(fi+1), funcmd_desc->textptr, scline->tcmnd);
                            free(funscline);
                            return false;
                        }
                        if (funscline->np[fi] == '\0')
                        {
                            funscline->np[fi] = script_atol(funscline->tp[fi]);
                        }
                        ranges[ri].min = funscline->np[fi];
                        ranges[ri].max = funscline->np[fi];
                        range_total++;
                    }
                }
            } else
            {
                // Old RANDOM command accepts only one range, and gives only numbers
                fi = 0;
                {
                    ranges[fi].min = script_atol(funscline->tp[0]);
                    ranges[fi].max = script_atol(funscline->tp[1]);
                }
                if (ranges[fi].max < ranges[fi].min) {
                    SCRPTWRNLOG("Range definition in argument of function \"%s\" within command \"%s\" should have lower value first", funcmd_desc->textptr, scline->tcmnd);
                    ranges[fi].max = ranges[fi].min;
                }
                range_total += ranges[fi].max - ranges[fi].min + 1;
                fi++;
            }
            if (range_total <= 0) {
                SCRPTERRLOG("Arguments of function \"%s\" within command \"%s\" define no values to select from", funcmd_desc->textptr, scline->tcmnd);
                break;
            }
            if ((funcmd_desc->index != Cmd_RANDOM) && (level_file_version == 0)) {
                SCRPTERRLOG("The function \"%s\" used within command \"%s\" is not supported in old level format", funcmd_desc->textptr, scline->tcmnd);
                break;
            }
            // The new RANDOM command stores values to allow selecting different one every turn during gameplay
            if ((funcmd_desc->index == Cmd_RANDOM) && (level_file_version > 0))
            {
                //TODO RANDOM make implementation - store ranges as variable to be used for selecting random value during gameplay
                SCRPTERRLOG("The function \"%s\" used within command \"%s\" is not supported yet", funcmd_desc->textptr, scline->tcmnd);
                break;
            }
            // DRAWFROM support - select random index now
            int64_t range_index = GAME_RANDOM(range_total);
            // Get value from ranges array
            range_total = 0;
            for (fi=0; fi < COMMANDDESC_ARGS_COUNT; fi++)
            {
                if ((range_index >= range_total) && (range_index <= range_total + ranges[fi].max - ranges[fi].min))
                {
                    char chr = cmd_desc->args[src];
                    if (toupper(chr) == 'A') {
                        if (is_if_statement)
                        {
                            scline->np[dst] = ranges[fi].min + range_index - range_total;
                            snprintf(scline->tp[dst], sizeof(scline->tp[dst]), "%" PRId64, (int64_t)(scline->np[dst]));
                        }
                        else
                        {
                            strcpy(scline->tp[dst], funscline->tp[ranges[fi].min]);
                        }
                    } else {
                        scline->np[dst] = ranges[fi].min + range_index - range_total;
                        // Set text value for that number
                        script_command_param_to_text(chr, scline, dst);
                    }
                    break;
                }
                range_total += ranges[fi].max - ranges[fi].min + 1;
            }
            SCRPTLOG("Function \"%s\" returned value \"%s\"", funcmd_desc->textptr, scline->tp[dst]);
        };break;
        case Cmd_IMPORT:
        {
            int64_t player_id = get_id(player_desc, funscline->tp[0]);
            if (player_id >= PLAYERS_FOR_CAMPAIGN_FLAGS)
            {
                SCRPTERRLOG("Cannot fetch flag values for player, '%s'", funscline->tp[0]);
                strcpy(scline->tp[dst], "0");
                break;
            }
            int64_t flag_id = get_id(campaign_flag_desc, funscline->tp[1]);
            if (flag_id == -1)
            {
                SCRPTERRLOG("Unknown campaign flag name, '%s'", funscline->tp[1]);
                strcpy(scline->tp[dst], "0");
                break;
            }
            SCRPTLOG("Function \"%s\" returned value \"%" PRId64 "\"", funcmd_desc->textptr,
                     (int64_t)(intralvl.campaign_flags[player_id][flag_id]));
            snprintf(scline->tp[dst], MAX_TEXT_LENGTH, "%" PRId64, (int64_t)(intralvl.campaign_flags[player_id][flag_id]));
            break;
        }
        default:
            SCRPTWRNLOG("Parameter value \"%s\" is a command which isn't supported as function", scline->tp[dst]);
            break;
    }
    free(funscline);
    return true;
}

static int64_t script_recognize_params(char **line, const struct CommandDesc *cmd_desc, struct ScriptLine *scline, int64_t *para_level, int64_t expect_level, int64_t file_version)
{
    int64_t dst, src;
    TbBool reparse = false;
    struct CommandToken token = { 0 };
    for (dst = 0, src = 0; dst <= COMMANDDESC_ARGS_COUNT; dst++, src++)
    {
        TbBool extended = false;
        char chr = cmd_desc->args[src];
        if (token.type == TkClose)
            break;
        if (chr == '!')
        {
            dst--;
            continue;
        }
        // Read the next parameter
        const struct CommandDesc *funcmd_desc;
        char* funline = *line;
        char funcmd_buf[MAX_TEXT_LENGTH];
        memset(funcmd_buf, 0, MAX_TEXT_LENGTH);

        if (reparse)
        {
            reparse = false;
            // Access tp[dst] only if we're sure dst < COMMANDDESC_ARGS_COUNT
            strncpy(scline->tp[dst], token.start, min(token.end - token.start, MAX_TEXT_LENGTH));
        }
        else
        {
            *line = get_next_token(funline, &token);
            if ((token.type == TkInvalid) || (token.type == TkEnd) || (token.type == TkComma))
            {
                SCRPTERRLOG("Invalid token '%s'", **line? *line: "<newline>");
                dst--;
                return -1;
            }
            else if ((token.type == TkClose) && ((chr == ' ') || (chr == 0)))
            {
                break;
            }
            else if ((token.type != TkNumber) && (token.type != TkCommand) && (token.type != TkString))
            {
                // Don't show parameter index - it may be bad, as we're decreasing dst to not overflow cmd_desc->args
                SCRPTWRNLOG("Excessive parameter of command \"%s\", value \"%s\"; ignoring", scline->tcmnd, funcmd_buf);
                dst--;
                continue;
            }
            // Access tp[dst] only if we're sure dst < COMMANDDESC_ARGS_COUNT
            strncpy(scline->tp[dst], token.start, min(token.end - token.start, MAX_TEXT_LENGTH));

            funcmd_desc = find_command_desc(&token, subfunction_desc);

            if (funcmd_desc != NULL)
            {
                int64_t r = process_subfunc(line, scline, cmd_desc, funcmd_desc, para_level, src, dst, file_version);
                if (r == -1)
                    return -1;
            }
            *line = get_next_token(*line, &token);

            if (token.type == TkRangeOperator)
            {
                // Operator ~ goes to A/a chr
                reparse = true;
            }
            else if ((token.type != TkClose) && (token.type != TkComma))
            {
                if ((cmd_desc->args[src + 1] != 'O') || (token.type != TkOperator))
                {
                    SCRPTERRLOG("Unexpected token after parameter %" PRId64 " of command \"%s\", discarding command", (int64_t)(dst + 1),
                                scline->tcmnd);
                    return -1;
                }
                else if (token.type == TkOperator)
                {
                    reparse = true;
                }
            }
        }
        if (scline->tp[dst][0] == '\0') {
          break;
        }
        chr = cmd_desc->args[src];
        if (cmd_desc->args[src + 1] == '+')
        {
            // All other parameters will be same
            src -= 1;
        }
        if (cmd_desc->args[src + 1] == '!') //extended set (dst.e. ANY_CREATURE)
        {
            extended = true;
        }
        if (!script_command_param_to_number(chr, scline, dst, extended)) {
            SCRPTERRLOG("Parameter %" PRId64 " of command \"%s\", type %c, has unexpected value; discarding command", (int64_t)(dst + 1), scline->tcmnd, (int)(chr));
            return -1;
        }
    }
    return dst;
}

TbBool script_scan_line(char *line, TbBool preloaded, int64_t file_version)
{
    const struct CommandDesc *cmd_desc;
    const char *line_start = line;
    struct CommandToken token = { 0 };
    SCRIPTDBG(12,"Starting");
    struct ScriptLine* scline = (struct ScriptLine*)calloc(1, sizeof(struct ScriptLine));
    if (scline == NULL)
    {
      SCRPTERRLOG("Can't allocate buffer to recognize line");
      return false;
    }
    int64_t para_level = 0;
    memset(scline, 0, sizeof(struct ScriptLine));
    if (next_command_reusable > 0)
        next_command_reusable--;

    line = get_next_token(line, &token);
    if (token.type == TkEnd)
    {
        free(scline);
        return false;
    }
    if (token.type != TkCommand)
    {
        SCRPTERRLOG("Syntax error: Script command expected");
        free(scline);
        return false;
    }
    memcpy(scline->tcmnd, token.start, min((token.end - token.start), MAX_TEXT_LENGTH));
    if (file_version > 0)
    {
        cmd_desc = find_command_desc(&token, command_desc);
    } else
    {
        cmd_desc = find_command_desc(&token, dk1_command_desc);
    }
    if (cmd_desc == NULL)
    {
        if (isalnum(scline->tcmnd[0])) {
          SCRPTERRLOG("Invalid command, '%s' (lev ver %" PRId64 ")", scline->tcmnd, (int64_t)(file_version));
        }
        free(scline);
        return false;
    }
    SCRIPTDBG(12,"Executing command %" PRIu64,(uint64_t)(cmd_desc->index));
    // Handling comments
    if (cmd_desc->index == Cmd_REM)
    {
        free(scline);
        return false;
    }
    line = get_next_token(line, &token);
    scline->command = cmd_desc->index;
    // selecting only preloaded/not preloaded commands
    if (script_is_preloaded_command(cmd_desc->index) != preloaded)
    {
        free(scline);
        return true;
    }
    int64_t args_count;
    if (token.type == TkEnd)
    {
        args_count = 0;
    }
    else if (token.type == TkOpen)
    {
        // Recognizing parameters
        args_count = script_recognize_params(&line, cmd_desc, scline, &para_level, 0, file_version);
        if (args_count < 0)
        {
            SCRPTERRLOG("Syntax error at \"%s\"", line_start);
            SCRPTERRLOG("   near - -      %*c", (int)((int64_t) (line - line_start)), (int)('^'));
            free(scline);
            return false;
        }
    }
    else
    {
        SCRPTERRLOG("Syntax error: ( expected at \"%s\"", line_start);
        SCRPTERRLOG("   near - - - - - - -        %*c", (int)((int64_t) (line - line_start)), (int)('^'));
        free(scline);
        return false;
    }
    if (args_count < COMMANDDESC_ARGS_COUNT)
    {
        int64_t required = count_required_parameters(cmd_desc->args);
        if (args_count < required) // Required arguments have upper-case type letters
        {
            SCRPTERRLOG("Not enough parameters for \"%s\", got only %" PRId64, cmd_desc->textptr,(int64_t)args_count);
            free(scline);
            return false;
        }
    }
    if (token.type != TkEnd)
    {
        line = get_next_token(line, &token);
    }
    if (token.type != TkEnd)
    {
        SCRPTERRLOG("Syntax error: Unexpected end of line");
    }
    script_add_command(cmd_desc, scline, file_version);
    free(scline);
    SCRIPTDBG(13,"Finished");
    return true;
}

int64_t clear_script(void)
{
    memset(&kfx_game_state.script, 0, sizeof(struct LevelScript));
    set_script_current_condition(CONDITION_ALWAYS);
    text_line_number = 1;
    return true;
}

int64_t clear_quick_messages(void)
{
    for (int64_t i = 0; i < QUICK_MESSAGES_COUNT; i++)
        memset(kfx_sim_state.quick_messages[i], 0, MESSAGE_TEXT_LEN);
    return true;
}

static char* process_multiline_comment(char *buf, char *buffer_end_pointer)
{
    for (char *p = buf; p < buffer_end_pointer - 1; p++)
    {
        if ((*p == ' ') || (*p == 9)) // Tabs or spaces
            continue;
        if (p[0] == '/') // /
        {
            if (p[1] != '*') // /*
                break;
            p += 2;
            for (; p < buffer_end_pointer - 1; p++)
            {
                if ((p[0] == '*') && (p[1] == '/'))
                {
                    buf = p + 2;
                    break;
                }
            }
            break;
        }
        else
            break;
    }
    return buf;
}

/** Source of the level's classic script text: the in-memory override's masked
 * script when one is installed for this level (docs/refactor/skirmish/,
 * level_script_override.h), else map%05u.txt from disk. Either way the result
 * is a writable, zero-padded buffer the caller free()s; *len is its length.
 */
static char* load_level_script_text(int64_t lvnum, int64_t *len)
{
    if (level_script_override_matches(lvnum))
    {
        const char *masked = level_script_override_masked();
        size_t n = strlen(masked);
        char* buf = (char*)calloc(n + 16, 1);
        if (buf != NULL)
        {
            memcpy(buf, masked, n);
            *len = (int64_t)n;
            return buf;
        }
        // Out of memory: fall through to the shipped script rather than fail the level.
    }
    *len = 1;
    return (char*)load_single_map_file_to_buffer(lvnum, "txt", len, LMFF_None);
}

/** Scans the override's prelude (if any) before the file's own lines.
 * The prelude is always v1 syntax, so level_file_version is forced to 1 for
 * its lines only and restored to whatever it was on entry -- the file's own
 * version (which preload_script() leaves behind for load_script(), and which
 * the file's LEVEL_VERSION line sets) must not be disturbed. Nothing to do
 * when no override is installed for this level.
 */
static void scan_level_script_prelude(int64_t lvnum, TbBool preloaded)
{
    if (!level_script_override_matches(lvnum))
        return;
    const char *src = level_script_override_prelude();
    size_t n = strlen(src);
    char* copy = (char*)calloc(n + 2, 1);
    if (copy == NULL)
        return;
    memcpy(copy, src, n);
    if (!preloaded)
        JUSTLOG("Level %" PRId64 ": running the Skirmish setup override (prelude %" PRId64 " bytes, file script masked)", (int64_t)(lvnum), (int64_t)n);
    int64_t saved_version = level_file_version;
    int64_t saved_line = text_line_number;
    level_file_version = 1;
    text_line_number = 0; // prelude errors read "line 0..n": distinct from the file's own lines
    char* line = copy;
    while (*line != '\0')
    {
        char* end = strchr(line, '\n');
        if (end != NULL)
            *end = '\0';
        script_scan_line(line, preloaded, 1);
        text_line_number++;
        if (end == NULL)
            break;
        line = end + 1;
    }
    level_file_version = saved_version;
    text_line_number = saved_line;
    free(copy);
}

static void parse_txt_data(char *script_data, int64_t script_len)
{// Process the file lines
    text_line_number = 1;
    char* buf = script_data;
    char* buffer_end_pointer = script_data + script_len;
    while (buf < buffer_end_pointer)
    {
        // Check for long comment
        buf = process_multiline_comment(buf, buffer_end_pointer);
      // Find end of the line
      int64_t lnlen = 0;
      while (&buf[lnlen] < buffer_end_pointer)
      {
        if ((buf[lnlen] == '\r') || (buf[lnlen] == '\n'))
          break;
        lnlen++;
      }
      // Get rid of the next line characters
      buf[lnlen] = 0;
      lnlen++;
      if (&buf[lnlen] < buffer_end_pointer)
      {
        if ((buf[lnlen] == '\r') || (buf[lnlen] == '\n'))
          lnlen++;
      }
      //SCRPTLOG("Analyse");
      // Analyze the line
      script_scan_line(buf, true, level_file_version);
      // Set new line start
      text_line_number++;
      buf += lnlen;
    }
    free(script_data);
}

TbBool preload_script(int64_t lvnum)
{
  SYNCDBG(7,"Starting");
  set_script_current_condition(CONDITION_ALWAYS);
  next_command_reusable = 0;
  level_file_version = DEFAULT_LEVEL_VERSION;
  clear_quick_messages();
  if (level_script_override_is_set() && !level_script_override_matches(lvnum))
  {
      // Installed for some other level and never consumed: must not leak into this one.
      WARNLOG("Discarding a stale level script override (was for level %" PRId64 ", loading %" PRId64 ")", (int64_t)level_script_override_level(), (int64_t)(lvnum));
      level_script_override_clear();
  }
  // Load the file (or the override's masked copy of it)
  int64_t script_len = 1;
  char* script_data = load_level_script_text(lvnum, &script_len);
  if (script_data == NULL)
  {
      // Here we could load lua instead
      return false;
  }
  scan_level_script_prelude(lvnum, true);
  parse_txt_data(script_data, script_len);
  SYNCDBG(8,"Finished");
  return true;
}

int64_t load_script(int64_t lvnum)
{
    SYNCDBG(7,"Starting");

    // Clear script data
    ui_gui_set_button_flashing(0, 0);
    clear_script();
    set_script_current_condition(CONDITION_ALWAYS);
    next_command_reusable = 0;
    text_line_number = 1;
    kfx_game_state.bonus_time = 0;
    kfx_game_state.flags_gui &= ~GGUI_CountdownTimer;
    kfx_sim_state.mode_flags |= MFlg_DeadBackToPool;
    reset_creature_max_levels();
    reset_script_timers_and_flags();
    reset_hand_rules();
    // Load the file (or the override's masked copy of it)
    int64_t script_len = 1;
    char* script_data = load_level_script_text(lvnum, &script_len);
    if (script_data == NULL)
    {
      level_script_override_clear();
      return false;
    }
    // Skirmish setup override: prelude first (forced v1, version restored), then the file's lines
    scan_level_script_prelude(lvnum, false);
    text_line_number = 1;
    // Process the file lines
    char* buf = script_data;
    char* buffer_end_pointer = script_data + script_len;
    while (buf < buffer_end_pointer)
    {
        buf = process_multiline_comment(buf, buffer_end_pointer);
      // Find end of the line
      char* p = buf;
      for (;p < buffer_end_pointer; p++)
      {
        if (*p == '\n')
          break;
      }
      // Get rid of the next line characters
      *p = 0;
      p++;
      if (p > buf)
      {
        if (p[-1] == '\r')
          p[-1] = 0;
      }
      // Analyze the line
      script_scan_line(buf, false, level_file_version);
      // Set new line start
      text_line_number++;
      buf = p;
    }
    free(script_data);
    // One-shot: the level's script has now been executed. Anything left installed would
    // leak into whatever level loads next.
    level_script_override_clear();
    if (kfx_game_state.script.win_conditions_num == 0 && luascript_loaded == false)
      WARNMSG("No WIN GAME conditions in script file.");
    if (get_script_current_condition() != CONDITION_ALWAYS)
      WARNMSG("Missing ENDIF's in script file.");
    JUSTLOG("Used script resources: %" PRId64 "/%" PRId64 " tunneller triggers, %" PRId64 "/%" PRId64 " party triggers, %" PRId64 "/%" PRId64 " script values, %" PRId64 "/%" PRId64 " IF conditions, %" PRId64 "/%" PRId64 " party definitions",
        (int64_t)kfx_game_state.script.tunneller_triggers_num,(int64_t)(TUNNELLER_TRIGGERS_COUNT),
        (int64_t)kfx_game_state.script.party_triggers_num,(int64_t)(PARTY_TRIGGERS_COUNT),
        (int64_t)kfx_game_state.script.values_num,(int64_t)(SCRIPT_VALUES_COUNT),
        (int64_t)kfx_game_state.script.conditions_num,(int64_t)(CONDITIONS_COUNT),
        (int64_t)kfx_game_state.script.creature_partys_num,(int64_t)(CREATURE_PARTYS_COUNT));
    return true;
}

static void add_to_party_process(struct ScriptContext *context)
{
    struct PartyTrigger* pr_trig = context->pr_trig;
    add_member_to_party(pr_trig->party_id, pr_trig->creatr_id, pr_trig->exp_level, pr_trig->carried_gold, pr_trig->objectv, pr_trig->countdown,pr_trig->target);
}

static void process_party(struct PartyTrigger* pr_trig)
{
    struct ScriptContext context = {0};
    int64_t n = pr_trig->creatr_id;

    context.pr_trig = pr_trig;

    switch (pr_trig->flags & TrgF_COMMAND_MASK)
    {
    case TrgF_ADD_TO_PARTY:
        add_to_party_process(&context);
        break;
    case TrgF_DELETE_FROM_PARTY:
        delete_member_from_party(pr_trig->party_id, pr_trig->creatr_id, pr_trig->exp_level);
        break;
    case TrgF_CREATE_EFFECT_GENERATOR:
        SYNCDBG(6, "Adding effect generator %" PRIu64 " at location %" PRId64, (uint64_t)(pr_trig->exp_level), (int64_t)pr_trig->location);
        script_process_new_effectgen(pr_trig->exp_level, pr_trig->location, pr_trig->carried_gold);
        break;
    case TrgF_CREATE_PARTY:
        SYNCDBG(6, "Adding player %" PRId64 " party %" PRId64 " at location %" PRId64, (int64_t)pr_trig->plyr_idx, (int64_t)n, (int64_t)pr_trig->location);
        script_process_new_party(&kfx_game_state.script.creature_partys[n],
            pr_trig->plyr_idx, pr_trig->location, pr_trig->ncopies);
        break;
    case TrgF_CREATE_CREATURE:
        SCRIPTDBG(6, "Adding creature %" PRId64, (int64_t)(n));
        script_process_new_creatures(pr_trig->plyr_idx, n, pr_trig->location, pr_trig->ncopies, pr_trig->carried_gold, pr_trig->exp_level, pr_trig->spawn_type);
        break;
    }
}

void process_check_new_creature_parties(void)
{
    for (int64_t i = 0; i < kfx_game_state.script.party_triggers_num; i++)
    {
        if (i >= PARTY_TRIGGERS_COUNT)
            break;
        struct PartyTrigger* pr_trig = &kfx_game_state.script.party_triggers[i];
        if ((pr_trig->flags & TrgF_DISABLED) == 0)
        {
            if (is_condition_met(pr_trig->condit_idx))
            {
                process_party(pr_trig);
                if ((pr_trig->flags & TrgF_REUSABLE) == 0)
                    set_flag(pr_trig->flags, TrgF_DISABLED);
            }
      }
    }
}

void process_check_new_tunneller_parties(void)
{
    for (int64_t i = 0; i < kfx_game_state.script.tunneller_triggers_num; i++)
    {
        struct TunnellerTrigger* tn_trig = &kfx_game_state.script.tunneller_triggers[i];
        if ((tn_trig->flags & TrgF_DISABLED) == 0)
        {
            if (is_condition_met(tn_trig->condit_idx))
            {
                int64_t k = tn_trig->party_id;
                if (k > 0)
                {
                    int64_t n = tn_trig->plyr_idx;
                    SCRIPTDBG(6, "Adding tunneler party %" PRId64, (int64_t)(k));
                    struct Thing* thing = script_process_new_tunneler(n, tn_trig->location, tn_trig->heading,
                        tn_trig->exp_level, tn_trig->carried_gold);
                    if (!thing_is_invalid(thing))
                    {
                        struct Thing* grptng = script_process_new_party(&kfx_game_state.script.creature_partys[k - 1], n, tn_trig->location, 1);
                        if (!thing_is_invalid(grptng))
                        {
                            add_creature_to_group_as_leader(thing, grptng);
                        }
                        else
                        {
                            WARNLOG("No party created, only lone %s", thing_model_name(thing));
                        }
                    }
                }
                else
                {
                    SCRIPTDBG(6, "Adding tunneler, heading %" PRIu64, (uint64_t)(tn_trig->heading));
                    script_process_new_tunneler(tn_trig->plyr_idx, tn_trig->location, tn_trig->heading,
                        tn_trig->exp_level, tn_trig->carried_gold);
                }
                if ((tn_trig->flags & TrgF_REUSABLE) == 0)
                    tn_trig->flags |= TrgF_DISABLED;
            }
      }
    }
}

void process_win_and_lose_conditions(PlayerNumber plyr_idx)
{
    int64_t i;
    int64_t k;
    struct PlayerInfo* player = get_player(plyr_idx);
    for (i=0; i < kfx_game_state.script.win_conditions_num; i++)
    {
        k = kfx_game_state.script.win_conditions[i];
        if (is_condition_met(k)) {
            SYNCDBG(8,"Win condition %" PRId64 " (cond. %" PRId64 ") met for player %" PRId64 ".",(int64_t)i,(int64_t)k,(int64_t)plyr_idx);
            set_player_as_won_level(player);
        }
    }
    for (i=0; i < kfx_game_state.script.lose_conditions_num; i++)
    {
        k = kfx_game_state.script.lose_conditions[i];
        if (is_condition_met(k))
        {
            SYNCDBG(8,"Lose condition %" PRId64 " (cond. %" PRId64 ") met for player %" PRId64 ".",(int64_t)i,(int64_t)k,(int64_t)plyr_idx);
            set_player_as_lost_level(player);
            setup_all_player_creatures_and_diggers_leave_or_die(plyr_idx);
        }
    }
}


void process_values(void)
{
    for (int64_t i = 0; i < kfx_game_state.script.values_num; i++)
    {
        struct ScriptValue* value = &kfx_game_state.script.values[i];
        if ((value->flags & TrgF_DISABLED) == 0)
        {
            if (is_condition_met(value->condit_idx))
            {
                script_process_value(value->valtype, value->plyr_range, value->longs[0], value->longs[1], value->longs[2], value);
                if ((value->flags & TrgF_REUSABLE) == 0)
                  set_flag(value->flags, TrgF_DISABLED);
            }
        }
    }
}


void process_level_script(void)
{
  SYNCDBG(6,"Starting");
  struct PlayerInfo *player;
  player = get_my_player();
  TbBool process_script = false;
  if (network_is_active()) {
      process_script = true;
  }
  if (player->victory_state == VicS_Undecided) {
      process_script = true;
  }
  if ((kfx_sim_state.system_flags & GSF_RunAfterVictory) != 0) {
      process_script = true;
  }
  // In network games every peer must keep executing scripts after the local player's defeat.
  if (process_script) {
      process_conditions();
      process_check_new_creature_parties();
    //script_process_messages(); is not here, but it is in beta - check why
      process_check_new_tunneller_parties();
      process_values();
      process_win_and_lose_conditions(my_player_number); //player->id_number may be uninitialized yet
    //  show_onscreen_msg(8, "Flags %d %d %d %d %d %d", (int64_t)(kfx_sim_state.dungeon[0].script_flags[0]),(int64_t)(kfx_sim_state.dungeon[0].script_flags[1]),
    //    kfx_sim_state.dungeon[0].script_flags[2],kfx_sim_state.dungeon[0].script_flags[3],kfx_sim_state.dungeon[0].script_flags[4],kfx_sim_state.dungeon[0].script_flags[5]);
  }
  SYNCDBG(19,"Finished");
}

// Moved from kfx_sim's creature_groups.c (stage 13.4, docs/refactor/
// stage-13-enforce-and-document.md) -- level-script party-definition CRUD,
// not simulation logic; every caller was already kfx_game/kfx_script.

int64_t get_party_index_of_name(const char *prtname)
{
    for (int64_t i = 0; i < kfx_game_state.script.creature_partys_num; i++)
    {
        struct Party* party = &kfx_game_state.script.creature_partys[i];
        if (strcasecmp(party->prtname, prtname) == 0)
            return i;
    }
    return -1;
}

TbBool create_party(const char *prtname)
{
    if (kfx_game_state.script.creature_partys_num >= CREATURE_PARTYS_COUNT)
    {
        SCRPTERRLOG("Too many partys in script");
        return false;
    }
    struct Party* party = (&kfx_game_state.script.creature_partys[kfx_game_state.script.creature_partys_num]);
    snprintf(party->prtname, sizeof(party->prtname), "%s", prtname);
    party->members_num = 0;
    kfx_game_state.script.creature_partys_num++;
    return true;
}

TbBool add_member_to_party(int64_t party_id, int64_t crtr_model, CrtrExpLevel exp_level, int64_t carried_gold, int64_t objctv_id, int64_t countdown, PlayerNumber target)
{
    if ((party_id < 0) && (party_id >= CREATURE_PARTYS_COUNT))
    {
        ERRORLOG("Party:%" PRId64 " is not defined", (int64_t)(party_id));
        return false;
    }
    struct Party* party = &kfx_game_state.script.creature_partys[party_id];
    if (party->members_num >= GROUP_MEMBERS_COUNT)
    {
      ERRORLOG("Too many creatures in party '%s' (limit is %" PRId64 " members)",
          party->prtname, (int64_t)(GROUP_MEMBERS_COUNT));
      return false;
    }
    struct PartyMember* member = &(party->members[party->members_num]);
    member->flags &= ~TrgF_DISABLED;
    member->crtr_kind = crtr_model;
    member->carried_gold = carried_gold;
    member->exp_level = exp_level-1;
    member->is_active = 1;
    member->objectv = objctv_id;
    member->countdown = countdown;
    party->members_num++;
    member->target = target;
    return true;
}

TbBool delete_member_from_party(int64_t party_id, int64_t crtr_model, CrtrExpLevel exp_level)
{
    if ((party_id < 0) && (party_id >= CREATURE_PARTYS_COUNT))
    {
        ERRORLOG("Party:%" PRId64 " is not defined", (int64_t)(party_id));
        return false;
    }
    struct Party* party = &kfx_game_state.script.creature_partys[party_id];

    for (int64_t i = 0; i < party->members_num; i++)
    {
        struct PartyMember* member = &(party->members[i]);
        if ((member->crtr_kind == crtr_model) && (member->exp_level == (exp_level-1)))
        {
            memmove(member, member + 1, sizeof(*member) * (party->members_num - i - 1));
            party->members_num--;
            return true;
        }
    }
    return false;
}

struct Thing* script_process_new_tunneller_party(PlayerNumber plyr_idx, int64_t prty_id, TbMapLocation location, TbMapLocation heading, CrtrExpLevel exp_level, uint64_t carried_gold)
{
    struct Thing* ldthing = script_process_new_tunneler(plyr_idx, location, heading, exp_level, carried_gold);
    if (thing_is_invalid(ldthing))
    {
        ERRORLOG("Couldn't create tunneling group leader");
        return INVALID_THING;
    }
    struct Thing* gpthing = script_process_new_party(&kfx_game_state.script.creature_partys[prty_id], plyr_idx, location, 1);
    if (thing_is_invalid(gpthing))
    {
        ERRORLOG("Couldn't create creature group");
        return ldthing;
    }
    add_creature_to_group_as_leader(ldthing, gpthing);

    return ldthing;
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
