/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lvl_script_cmd_messages.c
 *     Level script commands: objectives, information, messages, on-screen timers and variables.
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
#include "main_game.h"
#include "bflib_sndlib.h"
#include "config_translation.h"
#include "custom_sprites.h"
#include "lvl_script_conditions.h"
#include "lvl_script_lib.h"
#include "ports/ui_port.h"
#include "lvl_script_commands_private.h"
#include "lvl_script_commands_private.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

const struct NamedCommand msgtype_desc[] = {
  {"SPEECH",           1},
  {"SOUND",            2},
  {NULL,               0},
};

static TbBool get_custom_icon_from_value(const char* txt, int16_t* icon_idx);

/** The DISPLAY_*OBJECTIVE and DISPLAY_*INFORMATION checks: the text (tp[0]); from argument `a` a location (`at_location`)
 *  or a subtile position; then an icon in argument `icon_arg` (-1: none). An objective's location must be given; an
 *  information's is ALL_PLAYERS when not given (and its position form checks that). */
static void objective_or_information_check(const struct ScriptLine *scline, int64_t plr_range_id, int a, TbBool at_location,
    TbBool information, int icon_arg)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, plr_range_id);
    TextStringId msg_num = get_string_id_by_alias(scline->tp[0]);
    if (msg_num < 0)
    {
        SCRPTERRLOG("Invalid TEXT number");
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    MapSubtlCoord x = 0, y = 0;
    TbMapLocation location = 0;
    if (information)
    {
        const char *where = "ALL_PLAYERS";
        if (at_location)
        {
            if (scline->tp[a][0] != '\0')
                where = scline->tp[a];
        }
        else
        {
            x = scline->np[a];
            y = scline->np[a + 1];
        }
        if (!get_map_location_id(where, &location))
        {
            SCRPTERRLOG("Invalid location (%s)", scline->tp[a]);
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }
    else if (at_location)
    {
        if (!get_map_location_id(scline->tp[a], &location))
        {
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }
    else
    {
        x = scline->np[a];
        y = scline->np[a + 1];
    }
    value->shorts[0] = msg_num;
    value->ulongs[1] = location;
    value->shorts[4] = x;
    value->shorts[5] = y;
    value->shorts[6] = -1;
    if (icon_arg >= 0)
    {
        const char *icon = scline->tp[icon_arg];
        if (icon[0] != '\0' && !get_custom_icon_from_value(icon, &value->shorts[6]))
        {
            SCRPTERRLOG("Invalid custom icon (%s)", icon);
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }
    PROCESS_SCRIPT_VALUE(scline->command);
}

void display_objective_check(const struct ScriptLine *scline)
{
    const TbBool at_location = (scline->command == Cmd_DISPLAY_OBJECTIVE);
    objective_or_information_check(scline, ALL_PLAYERS, 1, at_location, false, at_location ? 2 : 3);
}

void display_objective_process(struct ScriptContext *context)
{
    set_general_objective_with_icon(context->value->shorts[0],
        context->player_idx,
        context->value->ulongs[1],
        context->value->shorts[4],
        context->value->shorts[5],
        context->value->shorts[6]);
}

void display_player_objective_check(const struct ScriptLine* scline)
{
    const TbBool at_location = (scline->command == Cmd_DISPLAY_PLAYER_OBJECTIVE);
    objective_or_information_check(scline, scline->np[1], 2, at_location, false, at_location ? 3 : 4);
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

void quick_objective_check(const struct ScriptLine* scline)
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

void quick_objective_process(struct ScriptContext* context)
{
    process_objective_with_icon(
        kfx_sim_state.quick_messages[context->value->shorts[0] % QUICK_MESSAGES_COUNT],
        context->player_idx,
        context->value->ulongs[1],
        context->value->shorts[4],
        context->value->shorts[5],
        context->value->shorts[6]);
}

void quick_player_objective_check(const struct ScriptLine* scline)
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

void quick_information_check(const struct ScriptLine* scline)
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

void quick_information_process(struct ScriptContext* context)
{
    set_quick_information_with_icon(context->value->shorts[0], context->player_idx, context->value->ulongs[1], context->value->shorts[4], context->value->shorts[5], context->value->shorts[6]);
}

void quick_player_information_check(const struct ScriptLine* scline)
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

void display_information_check(const struct ScriptLine* scline)
{
    objective_or_information_check(scline, ALL_PLAYERS, 1, (scline->command == Cmd_DISPLAY_INFORMATION), true, 2);
}

void display_information_process(struct ScriptContext* context)
{
    set_general_information_with_icon(context->value->shorts[0], context->player_idx,
        context->value->ulongs[1], context->value->shorts[4], context->value->shorts[5], context->value->shorts[6]);
}

void display_player_information_check(const struct ScriptLine* scline)
{
    objective_or_information_check(scline, scline->np[1], 2, (scline->command == Cmd_DISPLAY_PLAYER_INFORMATION), true, -1);
}

void heart_lost_quick_objective_check(const struct ScriptLine *scline)
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

void heart_lost_quick_objective_process(struct ScriptContext *context)
{
    kfx_sim_state.heart_lost_display_message = true;
    kfx_game_state.heart_lost_quick_message = true;
    kfx_game_state.heart_lost_message_id = context->value->longs[0];
    kfx_game_state.heart_lost_message_target = context->value->longs[2];
}

void heart_lost_objective_check(const struct ScriptLine *scline)
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

void heart_lost_objective_process(struct ScriptContext *context)
{
    kfx_sim_state.heart_lost_display_message = true;
    kfx_game_state.heart_lost_quick_message = false;
    kfx_game_state.heart_lost_message_id = context->value->longs[0];
    kfx_game_state.heart_lost_message_target = context->value->longs[1];
}

void display_timer_check(const struct ScriptLine *scline)
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

void display_timer_process(struct ScriptContext *context)
{
    kfx_game_state.script_timer_player = context->player_idx;
    kfx_game_state.script_timer_id = context->value->bytes[1];
    kfx_game_state.script_timer_limit = context->value->longs[1];
    kfx_game_state.timer_real = context->value->bytes[2];
    kfx_game_state.flags_gui |= GGUI_ScriptTimer;
}

void display_variable_check(const struct ScriptLine *scline)
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

void display_variable_process(struct ScriptContext *context)
{
    script_display_variable(context->player_idx, context->value->bytes[2], context->value->longs[1],
        context->value->longs[2], context->value->bytes[1], false, -1);
}

void display_variable_with_label_check(const struct ScriptLine *scline)
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

void display_variable_with_label_process(struct ScriptContext *context)
{
    script_display_variable(context->player_idx, context->value->bytes[2], context->value->longs[1], 0, 0, true,
        context->value->shorts[4]);
}
void display_countdown_check(const struct ScriptLine *scline)
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

void cmd_no_param_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    PROCESS_SCRIPT_VALUE(scline->command);
}

void hide_timer_process(struct ScriptContext *context)
{
   kfx_game_state.flags_gui &= ~GGUI_ScriptTimer;
}

void hide_variable_check(const struct ScriptLine *scline)
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

void hide_variable_process(struct ScriptContext *context)
{
    const int64_t varib_type = context->value->longs[2];
    const int64_t varib_id = context->value->longs[1];
    const int64_t player_idx = context->value->longs[0];
    if ((varib_id > -1) && (varib_type > -1))
        script_hide_variable(player_idx, varib_type, varib_id);
    else
        script_hide_variable(player_idx, -1, -1);
}

void set_box_tooltip_check(const struct ScriptLine* scline)
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


void set_box_tooltip_process(struct ScriptContext* context)
{
    int64_t idx = context->value->shorts[0];
    snprintf(kfx_sim_state.box_tooltip[idx], MESSAGE_TEXT_LEN, "%s", script_strval(context->value->longs[2]));
}

void set_box_tooltip_id_check(const struct ScriptLine *scline)
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

void set_box_tooltip_id_process(struct ScriptContext* context)
{
    int64_t idx = context->value->shorts[0];
    int64_t string = context->value->shorts[1];
    snprintf(kfx_sim_state.box_tooltip[idx], MESSAGE_TEXT_LEN, "%s", get_string(string));
}

void set_music_check(const struct ScriptLine *scline)
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

void set_music_process(struct ScriptContext *context)
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

void play_message_check(const struct ScriptLine *scline)
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

void play_message_process(struct ScriptContext *context)
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

void quick_message_check(const struct ScriptLine* scline)
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

void quick_message_process(struct ScriptContext* context)
{
    ui_message_add(context->value->chars[6], context->value->shorts[4], kfx_sim_state.quick_messages[context->value->ulongs[0]]);
}

void display_message_check(const struct ScriptLine* scline)
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

void display_message_process(struct ScriptContext* context)
{
    ui_message_add(context->value->chars[7], context->value->shorts[4], get_string(context->value->ulongs[0]));
}

void clear_message_check(const struct ScriptLine* scline)
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

void clear_message_process(struct ScriptContext* context)
{
    unsigned char count = min(context->value->chars[1], kfx_sim_state.active_messages_count);
    for (int64_t k = kfx_sim_state.active_messages_count-1; k >= (kfx_sim_state.active_messages_count-count); k--)
    {
        kfx_sim_state.messages[k].expiration_turn = get_gameturn();
    }
}

void tutorial_flash_button_check(const struct ScriptLine* scline)
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

void tutorial_flash_button_process(struct ScriptContext* context)
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

/** PRINT, MESSAGE: Dungeon Keeper Beta's; not supported. */
void print_check(const struct ScriptLine *scline)
{
    SCRPTWRNLOG("Command '%s' is only supported in Dungeon Keeper Beta", "PRINT");
}

void message_check(const struct ScriptLine *scline)
{
    SCRPTWRNLOG("Command '%s' is only supported in Dungeon Keeper Beta", "MESSAGE");
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
