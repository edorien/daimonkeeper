/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lvl_script_cmd_config.c
 *     Level script commands: game rules and the configuration commands (SET_*_CONFIGURATION, NEW_*_TYPE, ...).
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
#include "vidfade.h"
#include "config_lenses.h"
#include "creature_instances.h"
#include "creature_states_pray.h"
#include "custom_sprites.h"
#include "lens_api.h"
#include "vidmode.h"
#include "lvl_script_conditions.h"
#include "lvl_script_lib.h"
#include "ports/ui_port.h"
#include "lvl_script_commands_private.h"
#include "lvl_script_commands_private.h"
#include "config_state_validate.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

#define MAX_CONFIG_VALUES 4

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
            break;
        }
        else
        {
            assign_named_field_value(&named_fields_set->named_fields[property_id + i],context->value->longs[i+1],named_fields_set,id,src_str,ccf_SplitExecution|ccf_DuringLevel);
        }
    }
    // a script can set a field to any number: ones that select a function or a table entry must name one that exists
    config_state_repair(&kfx_config_state);
}

void set_trap_configuration_check(const struct ScriptLine* scline)
{
    set_config_check(&trapdoor_trap_named_fields_set, scline, "SET_TRAP_CONFIGURATION");
}

void set_room_configuration_check(const struct ScriptLine* scline)
{
    set_config_check(&terrain_room_named_fields_set, scline, "SET_ROOM_CONFIGURATION");
}

void set_hand_rule_check(const struct ScriptLine* scline)
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

void copy_creature_type_check(const struct ScriptLine* scline)
{
    script_copy_creature_type(scline->np[0],scline->tp[1]);
    return;
}

void new_creature_type_check(const struct ScriptLine* scline)
{
    script_new_creature_type(scline->tp[0]);
    return;
}

void new_room_type_check(const struct ScriptLine* scline)
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

void new_object_type_check(const struct ScriptLine* scline)
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

void new_trap_type_check(const struct ScriptLine* scline)
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

void set_trap_configuration_process(struct ScriptContext *context)
{
    set_config_process(&trapdoor_trap_named_fields_set, context, "SET_TRAP_CONFIGURATION");
}

void set_room_configuration_process(struct ScriptContext *context)
{
    set_config_process(&terrain_room_named_fields_set, context, "SET_ROOM_CONFIGURATION");
}

void set_hand_rule_process(struct ScriptContext* context)
{
    PlayerNumber plyr_idx = context->player_idx;
    int64_t crtr_id = context->value->shorts[0];
    int64_t hand_rule_action = context->value->shorts[1];
    int64_t hand_rule_slot = context->value->shorts[2];
    int64_t hand_rule_type = context->value->shorts[3];
    int64_t param = context->value->shorts[4];

    script_set_hand_rule(plyr_idx, crtr_id, hand_rule_action, hand_rule_slot, hand_rule_type, param);
}

void set_door_configuration_check(const struct ScriptLine* scline)
{
    set_config_check(&trapdoor_door_named_fields_set, scline, "SET_DOOR_CONFIGURATION");
}

void set_door_configuration_process(struct ScriptContext *context)
{
    set_config_process(&trapdoor_door_named_fields_set, context, "SET_DOOR_CONFIGURATION");
}

void set_object_configuration_check(const struct ScriptLine *scline)
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

/** A name from names or a number. */
static int64_t crconf_name_or_number(const struct NamedCommand *names, const char *text)
{
    return parameter_is_number(text) ? atoi(text) : get_id(names, text);
}

/** How to apply a flags value (SPELLIMMUNITY, the jobs): the second value, empty is 'set' (UCHAR_MAX), '1' 'add',
 *  '0' 'clear'. */
static int64_t crconf_flags_mode(const struct ScriptLine *scline)
{
    return (scline->tp[3][0] != '\0') ? atoi(scline->tp[3]) : UCHAR_MAX;
}

/* A key's check: puts its values (tp[2] on) in v[0..5]. Returns false, after logging, when the line is refused. */

/** [attributes]: the value, and a second one when given. */
static TbBool crconf_check_attributes(const struct ScriptLine *scline, int64_t v[6])
{
    v[0] = atoi(scline->tp[2]);
    if (scline->tp[3][0] != '\0')
        v[1] = atoi(scline->tp[3]);
    return true;
}

/** [jobs], [experience], [senses]: one value. */
static TbBool crconf_check_one(const struct ScriptLine *scline, int64_t v[6])
{
    v[0] = atoi(scline->tp[2]);
    return true;
}

/** [sounds]: the sample, then the count when given, and a third value when the count is given. */
static TbBool crconf_check_sounds(const struct ScriptLine *scline, int64_t v[6])
{
    v[0] = atoi(scline->tp[2]);
    if (scline->tp[3][0] != '\0')
    {
        v[1] = atoi(scline->tp[3]);
        v[2] = atoi(scline->tp[4]);
    }
    return true;
}

/** [sprites]: an animation's name or number. */
static TbBool crconf_check_sprite(const struct ScriptLine *scline, int64_t v[6])
{
    v[0] = get_anim_id_(scline->tp[2]);
    return true;
}

/** QUERYSYMBOL, HANDSYMBOL: an icon's name or number. */
static TbBool crconf_check_icon(const struct ScriptLine *scline, int64_t v[6])
{
    v[0] = get_icon_id(scline->tp[2]);
    return true;
}

/** [attraction]: up to three values. */
static TbBool crconf_check_attraction(const struct ScriptLine *scline, int64_t v[6])
{
    v[0] = atoi(scline->tp[2]);
    for (int i = 1; i < 3; i++)
        if (scline->tp[2 + i][0] != '\0')
            v[i] = atoi(scline->tp[2 + i]);
    return true;
}

/** [annoyance]: two values. */
static TbBool crconf_check_annoyance(const struct ScriptLine *scline, int64_t v[6])
{
    v[0] = atoi(scline->tp[2]);
    v[1] = atoi(scline->tp[3]);
    return true;
}

/** [appearance]: three values. */
static TbBool crconf_check_appearance(const struct ScriptLine *scline, int64_t v[6])
{
    for (int i = 0; i < 3; i++)
        v[i] = atoi(scline->tp[2 + i]);
    return true;
}

static TbBool crconf_check_attack_preference(const struct ScriptLine *scline, int64_t v[6])
{
    v[0] = get_id(attackpref_desc, scline->tp[2]);
    return true;
}

static TbBool crconf_check_lair_object(const struct ScriptLine *scline, int64_t v[6])
{
    v[0] = crconf_name_or_number(object_desc, scline->tp[2]);
    return true;
}

/** PRISONKIND, TORTUREKIND: a creature's name or number. */
static TbBool crconf_check_creature_kind(const struct ScriptLine *scline, int64_t v[6])
{
    v[0] = crconf_name_or_number(creature_desc, scline->tp[2]);
    return true;
}

static TbBool crconf_check_spell_immunity(const struct ScriptLine *scline, int64_t v[6])
{
    v[0] = crconf_name_or_number(spell_effect_flags, scline->tp[2]);
    if (v[0] < 0)
    {
        SCRPTERRLOG("SpellImmunity flag %s is out of range or doesn't exist.", scline->tp[2]);
        return false;
    }
    v[1] = crconf_flags_mode(scline);
    return true;
}

/** HOSTILETOWARDS: a creature's name or number, ANY_CREATURE or NULL. */
static TbBool crconf_check_hostile_towards(const struct ScriptLine *scline, int64_t v[6])
{
    if (parameter_is_number(scline->tp[2]))
        v[0] = atoi(scline->tp[2]);
    else if (0 == strcmp(scline->tp[2], "ANY_CREATURE"))
        v[0] = CREATURE_ANY;
    else if (strcasecmp(scline->tp[2], "NULL") == 0)
        v[0] = 0;
    else
        v[0] = get_id(creature_desc, scline->tp[2]);
    return true;
}

/** PRIMARYJOBS, SECONDARYJOBS, NOTDOJOBS, STRESSFULJOBS. */
static TbBool crconf_check_jobs(const struct ScriptLine *scline, int64_t v[6])
{
    return creature_configuration_job_value(scline, creaturejob_desc, &v[0], &v[1]);
}

static TbBool crconf_check_anger_jobs(const struct ScriptLine *scline, int64_t v[6])
{
    return creature_configuration_job_value(scline, angerjob_desc, &v[0], &v[1]);
}

/** ENTRANCEROOM: up to three rooms by name. */
static TbBool crconf_check_entrance_room(const struct ScriptLine *scline, int64_t v[6])
{
    v[0] = get_id(room_desc, scline->tp[2]);
    for (int i = 1; i < 3; i++)
        if (scline->tp[2 + i][0] != '\0')
            v[i] = get_id(room_desc, scline->tp[2 + i]);
    return true;
}

/** LAIRENEMY: one to three creatures, by name or number, ANY_CREATURE or NULL. */
static TbBool crconf_check_lair_enemy(const struct ScriptLine *scline, int64_t v[6])
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
                return false;
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
                    return false;
                }
            }
        }
    }
    for (int i = 0; i < 3; i++)
        v[i] = creature_model[i];
    return true;
}

/** POWERS: an instance by name or number, and its slot. */
static TbBool crconf_check_powers(const struct ScriptLine *scline, int64_t v[6])
{
    const int64_t instance = crconf_name_or_number(instance_desc, scline->tp[2]);
    if (instance < 0)
    {
        SCRPTERRLOG("Unknown instance %s ", scline->tp[2]);
        return false;
    }
    v[0] = instance;
    if ((atoi(scline->tp[3]) >= CREATURE_MAX_LEVEL) || (atoi(scline->tp[3]) <= 0)) //Powers
    {
        SCRPTERRLOG("Value %" PRId64 " out of range, only %" PRId64 " slots for Powers.", (int64_t)(atoi(scline->tp[3])), (int64_t)(CREATURE_MAX_LEVEL - 1));
        return false;
    }
    v[1] = atoi(scline->tp[3]);
    return true;
}

static TbBool crconf_check_powers_level_required(const struct ScriptLine *scline, int64_t v[6])
{
    if ((atoi(scline->tp[2]) <= 0) || (atoi(scline->tp[2]) > CREATURE_MAX_LEVEL)) //value
    {
        SCRPTERRLOG("Value %" PRId64 " out of range, only %" PRId64 " levels for PowersLevelRequired supported", (int64_t)(atoi(scline->tp[2])), (int64_t)(CREATURE_MAX_LEVEL));
        return false;
    }
    if ((atoi(scline->tp[3]) > CREATURE_MAX_LEVEL) || (atoi(scline->tp[3]) <= 0)) //slot
    {
        SCRPTERRLOG("Value %" PRId64 " out of range, only %" PRId64 " levels for PowersLevelRequired supported", (int64_t)(atoi(scline->tp[3])), (int64_t)(CREATURE_MAX_LEVEL));
        return false;
    }
    v[0] = atoi(scline->tp[2]);
    v[1] = atoi(scline->tp[3]);
    return true;
}

static TbBool crconf_check_levels_train_values(const struct ScriptLine *scline, int64_t v[6])
{
    if (atoi(scline->tp[2]) < 0) //value
    {
        SCRPTERRLOG("Value %" PRId64 " out of range.", (int64_t)(atoi(scline->tp[2])));
        return false;
    }
    if ((atoi(scline->tp[3]) <= 0) || (atoi(scline->tp[3]) > CREATURE_MAX_LEVEL)) //slot
    {
        SCRPTERRLOG("Value %" PRId64 " out of range, only %" PRId64 " levels for LevelsTrainValues supported", (int64_t)(atoi(scline->tp[3])), (int64_t)(CREATURE_MAX_LEVEL - 1));
        return false;
    }
    v[0] = atoi(scline->tp[2]);
    v[1] = atoi(scline->tp[3]);
    return true;
}

/** GROWUP: the training value, the creature grown into (name, number or NULL) and, for one, its level. */
static TbBool crconf_check_grow_up(const struct ScriptLine *scline, int64_t v[6])
{
    v[0] = atoi(scline->tp[2]);
    ThingModel creature_model = 0;
    if (parameter_is_number(scline->tp[3]))
    {
        creature_model = atoi(scline->tp[3]);
        if (creature_model > CREATURE_TYPES_MAX)
        {
            SCRPTERRLOG("Value %" PRId64 " out of range.", (int64_t)(atoi(scline->tp[3])));
            return false;
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
                return false;
            }
        }
    }
    v[1] = creature_model;
    int64_t level = 0;
    if (v[1] > 0)
    {
        level = atoi(scline->tp[4]);
        if ((level < 1) || (level > CREATURE_MAX_LEVEL))
        {
            SCRPTERRLOG("Value %" PRId64 " out of range.", (int64_t)(atoi(scline->tp[4])));
            return false;
        }
    }
    v[2] = level;
    return true;
}

/** SLEEPEXPERIENCE: up to three slab kinds by name, each with its experience (a negative one is taken as 0). */
static TbBool crconf_check_sleep_experience(const struct ScriptLine *scline, int64_t v[6])
{
    for (int i = 0; i < 3; i++)
    {
        const char *slab_name = scline->tp[2 + 2 * i];
        const int64_t slabtype = ((i == 0) || (slab_name[0] != '\0')) ? get_id(slab_desc, slab_name) : 0;
        if (slabtype < 0)
        {
            SCRPTERRLOG("Unknown slab type %s.", slab_name);
            return false;
        }
        v[2 * i] = slabtype;
        v[2 * i + 1] = atoi(scline->tp[3 + 2 * i]);
        if (v[2 * i + 1] < 0)
        {
            SCRPTERRLOG("Slab sleep experience value (%s %" PRId64 ") must be 0 or greater.", (i == 0) ? slab_name : scline->tp[3 + 2 * i], (int64_t)(v[2 * i + 1]));
            v[2 * i + 1] = 0;
        }
    }
    return true;
}

static TbBool crconf_check_natural_death_kind(const struct ScriptLine *scline, int64_t v[6])
{
    v[0] = get_id(creature_deathkind_desc, scline->tp[2]);
    return true;
}

static TbBool crconf_check_eye_effect(const struct ScriptLine *scline, int64_t v[6])
{
    v[0] = get_id(lenses_desc, scline->tp[2]);
    return true;
}

/** How SET_CREATURE_CONFIGURATION checks each key: by block and key name, and the block's other keys (key NULL). */
static const struct {
    int64_t block;
    const char *key;
    TbBool (*fn)(const struct ScriptLine *scline, int64_t v[6]);
} creature_configuration_checks[] = {
    {CrtConf_ATTRIBUTES, "ATTACKPREFERENCE",    crconf_check_attack_preference},
    {CrtConf_ATTRIBUTES, "LAIROBJECT",          crconf_check_lair_object},
    {CrtConf_ATTRIBUTES, "PRISONKIND",          crconf_check_creature_kind},
    {CrtConf_ATTRIBUTES, "TORTUREKIND",         crconf_check_creature_kind},
    {CrtConf_ATTRIBUTES, "SPELLIMMUNITY",       crconf_check_spell_immunity},
    {CrtConf_ATTRIBUTES, "HOSTILETOWARDS",      crconf_check_hostile_towards},
    {CrtConf_ATTRIBUTES, NULL,                  crconf_check_attributes},
    {CrtConf_JOBS,       "PRIMARYJOBS",         crconf_check_jobs},
    {CrtConf_JOBS,       "SECONDARYJOBS",       crconf_check_jobs},
    {CrtConf_JOBS,       "NOTDOJOBS",           crconf_check_jobs},
    {CrtConf_JOBS,       "STRESSFULJOBS",       crconf_check_jobs},
    {CrtConf_JOBS,       NULL,                  crconf_check_one},
    {CrtConf_SOUNDS,     NULL,                  crconf_check_sounds},
    {CrtConf_SPRITES,    "QUERYSYMBOL",         crconf_check_icon},
    {CrtConf_SPRITES,    "HANDSYMBOL",          crconf_check_icon},
    {CrtConf_SPRITES,    NULL,                  crconf_check_sprite},
    {CrtConf_ATTRACTION, "ENTRANCEROOM",        crconf_check_entrance_room},
    {CrtConf_ATTRACTION, NULL,                  crconf_check_attraction},
    {CrtConf_ANNOYANCE,  "LAIRENEMY",           crconf_check_lair_enemy},
    {CrtConf_ANNOYANCE,  "ANGERJOBS",           crconf_check_anger_jobs},
    {CrtConf_ANNOYANCE,  NULL,                  crconf_check_annoyance},
    {CrtConf_EXPERIENCE, "POWERS",              crconf_check_powers},
    {CrtConf_EXPERIENCE, "POWERSLEVELREQUIRED", crconf_check_powers_level_required},
    {CrtConf_EXPERIENCE, "LEVELSTRAINVALUES",   crconf_check_levels_train_values},
    {CrtConf_EXPERIENCE, "GROWUP",              crconf_check_grow_up},
    {CrtConf_EXPERIENCE, "SLEEPEXPERIENCE",     crconf_check_sleep_experience},
    {CrtConf_EXPERIENCE, NULL,                  crconf_check_one},
    {CrtConf_APPEARANCE, "NATURALDEATHKIND",    crconf_check_natural_death_kind},
    {CrtConf_APPEARANCE, NULL,                  crconf_check_appearance},
    {CrtConf_SENSES,     "EYEEFFECT",           crconf_check_eye_effect},
    {CrtConf_SENSES,     NULL,                  crconf_check_one},
};

void set_creature_configuration_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    int64_t creatvar = -1;
    int64_t block = -1;
    const char *key_name = NULL;
    for (size_t b = 0; b < sizeof(creature_configuration_blocks) / sizeof(creature_configuration_blocks[0]); b++)
    {
        creatvar = get_id(creature_configuration_blocks[b].keys, scline->tp[1]);
        if (creatvar != -1)
        {
            block = creature_configuration_blocks[b].block;
            key_name = get_conf_parameter_text(creature_configuration_blocks[b].keys, creatvar);
            break;
        }
    }
    if (creatvar == -1)
    {
        SCRPTERRLOG("Unknown creature configuration variable");
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    TbBool (*check)(const struct ScriptLine *scline, int64_t v[6]) = NULL;
    for (size_t c = 0; (c < sizeof(creature_configuration_checks) / sizeof(creature_configuration_checks[0])) && (check == NULL); c++)
        if ((creature_configuration_checks[c].block == block) &&
            ((creature_configuration_checks[c].key == NULL) || (strcasecmp(creature_configuration_checks[c].key, key_name) == 0)))
            check = creature_configuration_checks[c].fn;
    int64_t v[6] = {0, 0, 0, 0, 0, 0};
    if ((check != NULL) && !check(scline, v))
    {
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    int64_t config_value_primary = v[0], config_value_secondary = v[1], config_value_tertiary = v[2],
        config_value_quaternary = v[3], config_value_quinary = v[4], config_value_senary = v[5];

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

/** The keys that do more than assign their values to the creature file's rows, by block and key name (key NULL:
 *  the whole block). */
static const struct {
    int64_t block;
    const char *key;
    void (*fn)(const struct CrtConfValue *cc);
} creature_configuration_hooks[] = {
    {CrtConf_ATTRIBUTES, "NAME",                crconf_not_supported},
    {CrtConf_ATTRIBUTES, "HEALTH",              crconf_health},
    {CrtConf_ATTRIBUTES, "BASESPEED",           crconf_base_speed},
    {CrtConf_ATTRIBUTES, "CREATURELOYALTY",     crconf_not_supported},
    {CrtConf_ATTRIBUTES, "LOYALTYLEVEL",        crconf_not_supported},
    {CrtConf_ATTRIBUTES, "PROPERTIES",          crconf_not_supported},
    {CrtConf_ATTRIBUTES, "LAIROBJECT",          crconf_lair_object},
    {CrtConf_ATTRIBUTES, "SPELLIMMUNITY",       crconf_flags},
    {CrtConf_ATTRIBUTES, "HOSTILETOWARDS",      crconf_hostile_towards},
    {CrtConf_JOBS,       "PRIMARYJOBS",         crconf_flags},
    {CrtConf_JOBS,       "SECONDARYJOBS",       crconf_flags},
    {CrtConf_JOBS,       "NOTDOJOBS",           crconf_flags},
    {CrtConf_JOBS,       "STRESSFULJOBS",       crconf_flags},
    {CrtConf_ATTRACTION, "ENTRANCEROOM",        crconf_entrance_rooms},
    {CrtConf_ATTRACTION, "ROOMSLABSREQUIRED",   crconf_entrance_slabs},
    {CrtConf_ANNOYANCE,  "LAIRENEMY",           crconf_lair_enemy},
    {CrtConf_ANNOYANCE,  "ANGERJOBS",           crconf_flags},
    {CrtConf_EXPERIENCE, "POWERS",              crconf_learned_instance},
    {CrtConf_EXPERIENCE, "POWERSLEVELREQUIRED", crconf_learned_instance_level},
    {CrtConf_EXPERIENCE, "LEVELSTRAINVALUES",   crconf_to_level},
    {CrtConf_EXPERIENCE, "GROWUP",              crconf_grow_up},
    {CrtConf_EXPERIENCE, "SLEEPEXPERIENCE",     crconf_sleep_experience},
    {CrtConf_APPEARANCE, "TRANSPARENCYFLAGS",   crconf_transparency_flags},
    {CrtConf_SENSES,     "EYEEFFECT",           crconf_eye_effect},
    {CrtConf_SENSES,     "MAXANGLECHANGE",      crconf_max_angle_change},
    {CrtConf_SOUNDS,     NULL,                  crconf_sound},
    {CrtConf_SPRITES,    NULL,                  crconf_sprite},
};

void set_creature_configuration_process(struct ScriptContext* context)
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
            ((creature_configuration_hooks[h].key == NULL) || (strcasecmp(creature_configuration_hooks[h].key, cc.key_name) == 0)))
            break;
    if (h < sizeof(creature_configuration_hooks) / sizeof(creature_configuration_hooks[0]))
        creature_configuration_hooks[h].fn(&cc);
    else
        creature_configuration_assign(&cc);
    check_and_auto_fix_stats();
}

void set_object_configuration_process(struct ScriptContext *context)
{
    set_config_process(&objects_named_fields_set, context,"SET_OBJECT_CONFIGURATION");

    ThingModel model = context->value->shorts[0];
    update_all_objects_of_model(model);
}

void null_process(struct ScriptContext *context)
{
}



void set_sacrifice_recipe_check(const struct ScriptLine *scline)
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

void remove_sacrifice_recipe_check(const struct ScriptLine *scline)
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

void set_sacrifice_recipe_process(struct ScriptContext *context)
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

void set_creature_instance_check(const struct ScriptLine *scline)
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

void set_creature_instance_process(struct ScriptContext *context)
{
    ThingModel crmodel = context->value->bytes[0];
    int64_t slot = context->value->bytes[1];
    int64_t instance = context->value->bytes[2];
    unsigned char level = context->value->bytes[3];

    script_set_creature_instance(crmodel, slot, instance, level);

}

void set_texture_check(const struct ScriptLine *scline)
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

void set_texture_process(struct ScriptContext *context)
{
    PlayerNumber plyr_idx = context->player_idx;
    int64_t texture_id = context->value->shorts[0];

    set_player_texture(plyr_idx, texture_id);
}

void set_power_hand_check(const struct ScriptLine *scline)
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

void set_power_hand_process(struct ScriptContext *context)
{
    int64_t hand_idx = context->value->shorts[0];
    struct PlayerInfo * player;
    player = get_player(context->player_idx);
    player->hand_idx = hand_idx;
}

void set_effectgen_configuration_check(const struct ScriptLine* scline)
{
    set_config_check(&effects_effectgenerator_named_fields_set, scline,"SET_EFFECTGEN_CONFIG");
}

void set_effectgen_configuration_process(struct ScriptContext* context)
{
    set_config_process(&effects_effectgenerator_named_fields_set, context,"SET_EFFECTGEN_CONFIG");
}

/** The power file's first row for a key (its names list, its field). */
static const struct NamedField *power_configuration_row(const char *key)
{
    for (const struct NamedField *row = magic_powers_named_fields_set.named_fields; row->name != NULL; row++)
        if (strcasecmp(row->name, key) == 0)
            return row;
    return NULL;
}

/** A SET_POWER_CONFIGURATION line being checked: the line, the power, the key and its power file row, and the
 *  script value the checks fill; `number` is what the debug log shows. */
struct PwrConfCheck {
    const struct ScriptLine *scline;
    int64_t power_id;
    const char *key;
    const struct NamedField *row;
    struct ScriptValue *value;
    int64_t number;
};

/* A key's check: fills the script value from tp[2] on. Returns false, after logging, when the line is refused. */

/** Most keys: a number. */
static TbBool pwrconf_check_number(struct PwrConfCheck *pc)
{
    pc->value->longs[2] = atoi(pc->scline->tp[2]);
    return true;
}

/** POWER, COST: the value and its slot, 1 to the array's size (Power has one more than Cost), stored 0-based. */
static TbBool pwrconf_check_slot(struct PwrConfCheck *pc)
{
    const struct PowerConfigStats *powerst = get_power_model_stats(pc->power_id);
    const int64_t slots = (strcasecmp(pc->key, "POWER") == 0) ? (int64_t)(sizeof(powerst->strength) / sizeof(powerst->strength[0]))
                                                               : (int64_t)(sizeof(powerst->cost) / sizeof(powerst->cost[0]));
    const int64_t slot = atoi(pc->scline->tp[3]);
    if ((slot < 1) || (slot > slots))
    {
        SCRPTERRLOG("Power %s slot %" PRId64 " is out of range 1-%" PRId64, pc->key, slot, slots);
        return false;
    }
    pc->value->bytes[3] = slot - 1;
    pc->value->longs[2] = atoi(pc->scline->tp[2]);
    return true;
}

/** CASTABILITY: one flag and how to apply it (1 set, 0 clear), or the flags (a number or names) replacing all. */
static TbBool pwrconf_check_castability(struct PwrConfCheck *pc)
{
    char *new_value = (char *)pc->scline->tp[2];
    long long j;
    if (pc->scline->tp[3][0] != '\0')
    {
        j = get_long_id(powermodel_castability_commands, new_value);
        if (j <= 0)
        {
            SCRPTERRLOG("Incorrect castability value: %s", new_value);
            return false;
        }
        pc->number = j;
        pc->value->chars[3] = atoi(pc->scline->tp[3]);
    }
    else
    {
        if (parameter_is_number(new_value))
        {
            pc->number = atoll(new_value);
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
                    pc->number |= j;
                } else
                {
                    SCRPTERRLOG("Incorrect castability value: %s", new_value);
                    return false;
                }
                flag = strtok_r(NULL, " ", &saveptr);
            }
        }
        pc->value->chars[3] = -1;
    }
    pc->value->ulonglongs[1] = pc->number;
    return true;
}

/** PROPERTIES: as CASTABILITY, with the properties' names. */
static TbBool pwrconf_check_properties(struct PwrConfCheck *pc)
{
    char *new_value = (char *)pc->scline->tp[2];
    int64_t k;
    if (pc->scline->tp[3][0] != '\0')
    {
        k = get_id(powermodel_properties_commands, new_value);
        if (k <= 0)
        {
            SCRPTERRLOG("Incorrect property value: %s", new_value);
            return false;
        }
        pc->number = k;
        pc->value->chars[3] = atoi(pc->scline->tp[3]);
    }
    else
    {
        if (parameter_is_number(new_value))
        {
            pc->number = atoi(new_value);
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
                    pc->number |= k;
                } else
                {
                    SCRPTERRLOG("Incorrect property value: %s", new_value);
                    return false;
                }
                flag = strtok_r(NULL, " ", &saveptr);
            }
        }
        pc->value->chars[3] = -1;
    }
    pc->value->longs[2] = pc->number;
    return true;
}

/** ARTIFACT, PLAYERSTATE, PARENTPOWER: a name from the row's list or a number one of its names has (as the power
 *  file takes it; a number past the list indexes past the arrays the value is used for). */
static TbBool pwrconf_check_listed_number(struct PwrConfCheck *pc)
{
    const char *text = pc->scline->tp[2];
    const struct NamedCommand *names = pc->row->namedCommand;
    int64_t k = get_id(names, text);
    if ((k < 0) && parameter_is_number(text))
    {
        int64_t max = -1;
        for (const struct NamedCommand *n = names; n->name != NULL; n++)
            if (n->num > max)
                max = n->num;
        k = atoi(text);
        if (k > max)
            k = -1;
    }
    if (k < 0)
    {
        SCRPTERRLOG("Incorrect %s value: %s", pc->key, text);
        return false;
    }
    pc->number = k;
    pc->value->longs[2] = k;
    return true;
}

/** CASTEXPANDFUNC, SPELL, USEFUNCTION, CREATURETYPE, COSTFORMULA: a name from the row's list. */
static TbBool pwrconf_check_name(struct PwrConfCheck *pc)
{
    const int64_t k = get_id(pc->row->namedCommand, pc->scline->tp[2]);
    if (k < 0)
    {
        SCRPTERRLOG("Incorrect %s value: %s", pc->key, pc->scline->tp[2]);
        return false;
    }
    pc->number = k;
    pc->value->longs[2] = k;
    return true;
}

/** EFFECT: an effect or effect element, by name or number. */
static TbBool pwrconf_check_effect(struct PwrConfCheck *pc)
{
    const int64_t k = effect_or_effect_element_id(pc->scline->tp[2]);
    if (k == 0)
    {
        SCRPTERRLOG("Unrecognised effect: %s", pc->scline->tp[2]);
        return false;
    }
    pc->number = k;
    pc->value->longs[2] = k;
    return true;
}

/** SYMBOLSPRITES: the big and medium symbols, each a sprite's name or number, as the power file's. */
static TbBool pwrconf_check_symbol_sprites(struct PwrConfCheck *pc)
{
    pc->value->longs[1] = render_get_icon_id(pc->scline->tp[2]);
    pc->value->longs[2] = render_get_icon_id(pc->scline->tp[3]);
    return true;
}

/** POINTERSPRITES: a sprite's name or number, as the power file's. */
static TbBool pwrconf_check_icon(struct PwrConfCheck *pc)
{
    pc->value->longs[2] = render_get_icon_id(pc->scline->tp[2]);
    return true;
}

/** The keys SET_POWER_CONFIGURATION checks other than as a number, by name. */
static const struct {
    const char *key;
    TbBool (*fn)(struct PwrConfCheck *pc);
} power_configuration_checks[] = {
    {"POWER",          pwrconf_check_slot},
    {"COST",           pwrconf_check_slot},
    {"CASTABILITY",    pwrconf_check_castability},
    {"ARTIFACT",       pwrconf_check_listed_number},
    {"SYMBOLSPRITES",  pwrconf_check_symbol_sprites},
    {"POINTERSPRITES", pwrconf_check_icon},
    {"PROPERTIES",     pwrconf_check_properties},
    {"CASTEXPANDFUNC", pwrconf_check_name},
    {"PLAYERSTATE",    pwrconf_check_listed_number},
    {"PARENTPOWER",    pwrconf_check_listed_number},
    {"SPELL",          pwrconf_check_name},
    {"EFFECT",         pwrconf_check_effect},
    {"USEFUNCTION",    pwrconf_check_name},
    {"CREATURETYPE",   pwrconf_check_name},
    {"COSTFORMULA",    pwrconf_check_name},
};

void set_power_configuration_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    const char *powername = scline->tp[0];
    const char *property = scline->tp[1];

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
        SCRPTERRLOG("Unknown power variable: %s", property);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    struct PwrConfCheck pc = {scline, power_id, get_conf_parameter_text(magic_power_commands, powervar), NULL, value, 0};
    pc.row = power_configuration_row(pc.key);
    TbBool (*check)(struct PwrConfCheck *pc) = pwrconf_check_number;
    for (size_t c = 0; c < sizeof(power_configuration_checks) / sizeof(power_configuration_checks[0]); c++)
        if (strcasecmp(power_configuration_checks[c].key, pc.key) == 0)
            check = power_configuration_checks[c].fn;
    if (!check(&pc))
    {
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    const TbBool flag_key = (strcasecmp(pc.key, "CASTABILITY") == 0) || (strcasecmp(pc.key, "PROPERTIES") == 0);
    if (flag_key && (value->chars[3] != -1))
    {
        SCRIPTDBG(7, "Toggling %s %s flag: %" PRId64, powername, pc.key, (int64_t)(pc.number));
    }
    else
    {
        SCRIPTDBG(7, "Setting power %s property %s to %" PRId64, powername, property, (int64_t)(pc.number));
    }
    value->shorts[0] = power_id;
    value->bytes[2] = powervar;

    PROCESS_SCRIPT_VALUE(scline->command);
}

/* A key's process, for the keys that do more than assign longs[2] to the power file's row. */

static void pwrconf_strength(struct PowerConfigStats *powerst, const struct ScriptValue *value)
{
    powerst->strength[value->bytes[3]] = value->longs[2];
}

static void pwrconf_cost(struct PowerConfigStats *powerst, const struct ScriptValue *value)
{
    powerst->cost[value->bytes[3]] = value->longs[2];
}

/** CASTABILITY: chars[3] 1 sets the flags, 0 clears them, anything else replaces them. */
static void pwrconf_castability(struct PowerConfigStats *powerst, const struct ScriptValue *value)
{
    unsigned long long flag = value->ulonglongs[1];
    if (value->chars[3] == 1)
        set_flag(powerst->can_cast_flags, flag);
    else if (value->chars[3] == 0)
        clear_flag(powerst->can_cast_flags, flag);
    else
        powerst->can_cast_flags = flag;
}

static void pwrconf_properties(struct PowerConfigStats *powerst, const struct ScriptValue *value)
{
    if (value->chars[3] == 1)
        set_flag(powerst->config_flags, value->longs[2]);
    else if (value->chars[3] == 0)
        clear_flag(powerst->config_flags, value->longs[2]);
    else
        powerst->config_flags = value->longs[2];
}

/** ARTIFACT: the object becomes this power's artifact too. */
static void pwrconf_artifact(struct PowerConfigStats *powerst, const struct ScriptValue *value)
{
    powerst->artifact_model = value->longs[2];
    kfx_config_state.conf.object_conf.object_to_power_artifact[powerst->artifact_model] = value->shorts[0];
}

static void pwrconf_symbol_sprites(struct PowerConfigStats *powerst, const struct ScriptValue *value)
{
    powerst->bigsym_sprite_idx = value->longs[1];
    powerst->medsym_sprite_idx = value->longs[2];
}

static void pwrconf_not_supported(struct PowerConfigStats *powerst, const struct ScriptValue *value)
{
    WARNMSG("Unsupported power configuration, variable %" PRId64 ".", (int64_t)(value->bytes[2]));
}

/** The keys that do more than assign longs[2] to the power file's row, by name. */
static const struct {
    const char *key;
    void (*fn)(struct PowerConfigStats *powerst, const struct ScriptValue *value);
} power_configuration_hooks[] = {
    {"NAME",          pwrconf_not_supported},
    {"POWER",         pwrconf_strength},
    {"COST",          pwrconf_cost},
    {"CASTABILITY",   pwrconf_castability},
    {"ARTIFACT",      pwrconf_artifact},
    {"SYMBOLSPRITES", pwrconf_symbol_sprites},
    {"PROPERTIES",    pwrconf_properties},
};

void set_power_configuration_process(struct ScriptContext *context)
{
    struct PowerConfigStats *powerst = get_power_model_stats(context->value->shorts[0]);
    const char *key = get_conf_parameter_text(magic_power_commands, context->value->bytes[2]);
    size_t h;
    for (h = 0; h < sizeof(power_configuration_hooks) / sizeof(power_configuration_hooks[0]); h++)
        if (strcasecmp(power_configuration_hooks[h].key, key) == 0)
            break;
    const struct NamedField *row = power_configuration_row(key);
    if (h < sizeof(power_configuration_hooks) / sizeof(power_configuration_hooks[0]))
        power_configuration_hooks[h].fn(powerst, context->value);
    else if (row != NULL)
        assign_cast(row, context->value->longs[2], &magic_powers_named_fields_set, context->value->shorts[0], "SET_POWER_CONFIGURATION", 0);
    else
        pwrconf_not_supported(powerst, context->value);
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

void set_game_rule_check(const struct ScriptLine* scline)
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

void set_game_rule_process(struct ScriptContext* context)
{
    int64_t rulegroup = context->value->shorts[0];
    int64_t ruledesc  = context->value->shorts[1];
    int64_t rulevalue  = context->value->longs[1];


    SCRIPTDBG(7,"Changing Game Rule '%s' to %" PRId64, (ruleblocks[rulegroup]+ruledesc)->name, (int64_t)(rulevalue));

    assign_named_field_value((ruleblocks[rulegroup]+ruledesc),rulevalue,&rules_named_fields_set,context->player_idx,"SET_GAME_RULE",ccf_SplitExecution|ccf_DuringLevel);
}

void set_increase_on_experience_check(const struct ScriptLine* scline)
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

void set_increase_on_experience_process(struct ScriptContext* context)
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

void set_creature_max_level_check(const struct ScriptLine* scline)
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

void set_creature_max_level_process(struct ScriptContext* context)
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

/** A process runs once for each player in the value's range: true for the first, for the commands that act once
 *  whatever the range (the cases of script_process_value()'s old switch that didn't loop over the players). */
TbBool legacy_value_first_player(const struct ScriptContext *context)
{
    int64_t plr_start;
    int64_t plr_end;
    if (get_players_range(context->value->plyr_range, &plr_start, &plr_end) < 0)
        return false;
    return (context->player_idx == plr_start);
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

void set_creature_health_check(const struct ScriptLine *scline)
{
    legacy_creature_value(scline, scline->np[1], USHRT_MAX, "health");
}

void set_creature_health_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    change_max_health_of_creature_kind(context->value->longs[0], context->value->longs[1]);
}

void set_creature_strength_check(const struct ScriptLine *scline)
{
    legacy_creature_value(scline, scline->np[1], USHRT_MAX, "strength");
}

void set_creature_strength_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    struct CreatureModelConfig *crconf = creature_stats_get(context->value->longs[0]);
    if (creature_stats_invalid(crconf))
        return;
    crconf->strength = saturate_set_unsigned(context->value->longs[1], 16);
}

void set_creature_armour_check(const struct ScriptLine *scline)
{
    legacy_creature_value(scline, scline->np[1], UCHAR_MAX, "armour");
}

void set_creature_armour_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    struct CreatureModelConfig *crconf = creature_stats_get(context->value->longs[0]);
    if (creature_stats_invalid(crconf))
        return;
    crconf->armour = saturate_set_unsigned(context->value->longs[1], 8);
}

/** SET_CREATURE_FEAR_WOUNDED: a percentage; level files before version 1 gave it scaled 0..255. */
void set_creature_fear_wounded_check(const struct ScriptLine *scline)
{
    if (scline->file_version > 0)
        legacy_creature_value(scline, scline->np[1], UCHAR_MAX, "fear");
    else
        legacy_creature_value(scline, 101*scline->np[1]/255, UCHAR_MAX, "fear");
}

void set_creature_fear_wounded_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    struct CreatureModelConfig *crconf = creature_stats_get(context->value->longs[0]);
    if (creature_stats_invalid(crconf))
        return;
    crconf->fear_wounded = saturate_set_unsigned(context->value->longs[1], 8);
}

void set_creature_fear_stronger_check(const struct ScriptLine *scline)
{
    legacy_creature_value(scline, scline->np[1], SHRT_MAX, "fear");
}

void set_creature_fear_stronger_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    struct CreatureModelConfig *crconf = creature_stats_get(context->value->longs[0]);
    if (creature_stats_invalid(crconf))
        return;
    crconf->fear_stronger = saturate_set_unsigned(context->value->longs[1], 16);
}

void set_creature_fearsome_factor_check(const struct ScriptLine *scline)
{
    legacy_creature_value(scline, scline->np[1], SHRT_MAX, "fearsome");
}

void set_creature_fearsome_factor_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    struct CreatureModelConfig *crconf = creature_stats_get(context->value->longs[0]);
    if (creature_stats_invalid(crconf))
        return;
    crconf->fearsome_factor = saturate_set_unsigned(context->value->longs[1], 16);
}

void set_creature_property_check(const struct ScriptLine *scline)
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
void set_creature_property_process(struct ScriptContext *context)
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

/******************************************************************************/
#ifdef __cplusplus
}
#endif
