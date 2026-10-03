/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_crtrstates.c
 *     Specific creature model configuration loading functions.
 * @par Purpose:
 *     Support of configuration files for specific creatures.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     25 May 2009 - 23 Jun 2011
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "config_crtrstates.h"
#include "globals.h"

#include "bflib_basics.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"

#include "config.h"
#include "config_creature.h"
#include "kfx_config_state.h"
// Literal-dup of kfx_sim's creature_groups.h FollowBehaviour values
// (only reachable transitively) -- kept local to this .c file (not
// config_creature.h) since kfx_sim files that include both
// config_creature.h and the real creature_groups.h directly would
// collide with a shared-header enum redeclaration. See docs/refactor/
// stage-13-enforce-and-document.md.
#define FlwB_None               0
#define FlwB_FollowLeader       1
#define FlwB_MatchWorkRoom      2
#define FlwB_JoinCombatOrFollow 3
#include "config_funcnames.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct NamedCommand creatrstate_desc[CREATURE_STATES_MAX];
/******************************************************************************/
const struct NamedCommand creature_state_types_commands[] = {
    {"Idle",        CrStTyp_Idle},
    {"Work",        CrStTyp_Work},
    {"OwnNeeds",    CrStTyp_OwnNeeds},
    {"Sleep",       CrStTyp_Sleep},
    {"Feed",        CrStTyp_Feed},
    {"FightCrtr",   CrStTyp_FightCrtr},
    {"Move",        CrStTyp_Move},
    {"GetsSalary",  CrStTyp_GetsSalary},
    {"Escape",      CrStTyp_Escape},
    {"Unconscious", CrStTyp_Unconscious},
    {"AngerJob",    CrStTyp_AngerJob},
    {"FightDoor",   CrStTyp_FightDoor},
    {"FightObj",    CrStTyp_FightObj},
    {"Called2Arms", CrStTyp_Called2Arms},
    {"Follow",      CrStTyp_Follow},
    {"DeepWork",    CrStTyp_DeepWork},
    {NULL,0}
};

const struct NamedCommand follow_behavior_commands[] = {
    {"none",                FlwB_None},
    {"FollowLeader",        FlwB_FollowLeader},
    {"MatchWorkRoom",       FlwB_MatchWorkRoom},
    {"JoinCombatOrFollow",  FlwB_JoinCombatOrFollow},
    {NULL,0}
};

int64_t value_overrides(const struct NamedField* named_field, const char* value_text,
    const struct NamedFieldSet* named_fields_set, int64_t idx,
    const char* src_str, unsigned char flags)
{
    struct CreatureStateConfig* state = &kfx_config_state.conf.crtr_conf.states[idx];

    state->override_feed          = 0;
    state->override_own_needs     = 0;
    state->override_sleep         = 0;
    state->override_fight_crtr    = 0;
    state->override_gets_salary   = 0;
    state->override_captive       = 0;
    state->override_transition    = 0;
    state->override_escape        = 0;
    state->override_unconscious   = 0;
    state->override_anger_job     = 0;
    state->override_fight_object  = 0;
    state->override_fight_door    = 0;
    state->override_call2arms     = 0;
    state->override_follow        = 0;
    state->override_deep_work     = 0;

    char buffer[256];
    strncpy(buffer, value_text, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char* token = strtok(buffer, " ");
    while (token) {
        if (strcasecmp(token, "FEED") == 0) state->override_feed = 1;
        else if (strcasecmp(token, "OWN_NEEDS") == 0) state->override_own_needs = 1;
        else if (strcasecmp(token, "SLEEP") == 0) state->override_sleep = 1;
        else if (strcasecmp(token, "FIGHT_CRTR") == 0) state->override_fight_crtr = 1;
        else if (strcasecmp(token, "GETS_SALARY") == 0) state->override_gets_salary = 1;
        else if (strcasecmp(token, "CAPTIVE") == 0) state->override_captive = 1;
        else if (strcasecmp(token, "TRANSITION") == 0) state->override_transition = 1;
        else if (strcasecmp(token, "ESCAPE") == 0) state->override_escape = 1;
        else if (strcasecmp(token, "UNCONSCIOUS") == 0) state->override_unconscious = 1;
        else if (strcasecmp(token, "ANGER_JOB") == 0) state->override_anger_job = 1;
        else if (strcasecmp(token, "FIGHT_OBJECT") == 0) state->override_fight_object = 1;
        else if (strcasecmp(token, "FIGHT_DOOR") == 0) state->override_fight_door = 1;
        else if (strcasecmp(token, "CALL2ARMS") == 0) state->override_call2arms = 1;
        else if (strcasecmp(token, "FOLLOW") == 0) state->override_follow = 1;
        else if (strcasecmp(token, "DEEP_WORK") == 0) state->override_deep_work = 1;
        else {
            WARNLOG("Unknown override name: '%s' in '%s'", token, src_str);
        }

        token = strtok(NULL, " ");
    }

    return 0;
}

// process_func_commands/cleanup_func_commands/move_from_slab_func_commands/
// move_check_func_commands (kfx_sim's creature_states.h) are genuine
// kfx_sim state, not derivable at compile time -- unlike config_terrain.c's
// function-pointer tables, these are embedded as NamedCommand pointers
// inside the (formerly const) NamedField table below, so a runtime
// callback call can't sit directly in the initializer. Instead, the
// array is left mutable, initialized to NULL here, and patched by
// resolve_crstates_func_commands_pointers() (below, wired as
// creature_states_file_data's pre_load_func) once SimPort
// is registered. See docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md.
struct NamedField crstates_states_named_fields[] = {
    {"NAME",                   0, field_t(struct CreatureStateConfig, name),                        0,        0,      0,              creatrstate_desc,  value_name,      assign_null},
    {"PROCESSFUNCTION",        0, field_t(struct CreatureStateConfig, process_state),               0,        0,      0,         NULL,  value_function,  assign_default},
    {"CLEANUPFUNCTION",        0, field_t(struct CreatureStateConfig, cleanup_state),               0,        0,      0,         NULL,  value_function,  assign_default},
    {"MOVEFROMSLABFUNCTION",   0, field_t(struct CreatureStateConfig, move_from_slab),              0,        0,      0,  NULL,  value_function,  assign_default},
    {"MOVECHECKFUNCTION",      0, field_t(struct CreatureStateConfig, move_check),                  0,        0,      0,      NULL,  value_function,  assign_default},
    {"OVERRIDES",             -1, NULL,0,                                                           0,        0,      0,                          NULL,  value_overrides, assign_null},
    {"STATETYPE",              0, field_t(struct CreatureStateConfig, state_type),                  0,        0,      0, creature_state_types_commands,  value_default,   assign_default},
    {"CAPTIVE",                0, field_t(struct CreatureStateConfig, captive),                     0,        0,      1,                          NULL,  value_default,   assign_default},
    {"TRANSITION",             0, field_t(struct CreatureStateConfig, transition),                  0,        0,      1,                          NULL,  value_default,   assign_default},
    {"FOLLOWBEHAVIOR",         0, field_t(struct CreatureStateConfig, follow_behavior),             0,        0,      0,      follow_behavior_commands,  value_default,   assign_default},
    {"BLOCKSALLSTATECHANGES",  0, field_t(struct CreatureStateConfig, blocks_all_state_changes),    0,        0,      1,                          NULL,  value_default,   assign_default},
    {"SPRITEIDX",              0, field_t(struct CreatureStateConfig, sprite_idx),                  0,        0,      0,                          NULL,  value_icon,      assign_icon},
    {"DISPLAYTHOUGHTBUBBLE",   0, field_t(struct CreatureStateConfig, display_thought_bubble),      0,        0,      1,                          NULL,  value_default,   assign_default},
    {"SNEAKY",                 0, field_t(struct CreatureStateConfig, sneaky),                      0,        0,      1,                          NULL,  value_default,   assign_default},
    {"REACTTOCTA",             0, field_t(struct CreatureStateConfig, react_to_cta),                0,        0,      1,                          NULL,  value_default,   assign_default},
    {NULL},
};

static int64_t* get_crstates_count(void) { return &kfx_config_state.conf.crtr_conf.states_count; }
static void* get_crstates_base(void) { return kfx_config_state.conf.crtr_conf.states; }


const struct NamedFieldSet crstates_states_named_fields_set = {
    get_crstates_count,
    "state",
    crstates_states_named_fields,
    creatrstate_desc,
    CREATURE_STATES_MAX,
    sizeof(kfx_config_state.conf.crtr_conf.states[0]),
    get_crstates_base,
};

static TbBool load_creaturestates_config_file(const char *fname, int64_t flags);

// Patches the NamedCommand pointers left NULL in
// crstates_states_named_fields above -- see the comment there and
// docs/refactor/todo/check-layering-symbol-level-blind-spot.md.
static void resolve_crstates_func_commands_pointers(void)
{
    crstates_states_named_fields[1].namedCommand = process_func_commands;
    crstates_states_named_fields[2].namedCommand = cleanup_func_commands;
    crstates_states_named_fields[3].namedCommand = move_from_slab_func_commands;
    crstates_states_named_fields[4].namedCommand = move_check_func_commands;
}

const struct ConfigFileData creature_states_file_data = {
    .filename = "crstates.cfg",
    .load_func = load_creaturestates_config_file,
    .pre_load_func = resolve_crstates_func_commands_pointers,
    .post_load_func = NULL,
};

/******************************************************************************/

static TbBool load_creaturestates_config_file(const char *fname, int64_t flags)
{
    SYNCDBG(0,"%s file \"%s\".",((flags & CnfLd_ListOnly) == 0)?"Reading":"Parsing",fname);
    int64_t len = LbFileLengthRnc(fname);
    if (len < MIN_CONFIG_FILE_SIZE)
    {
        if ((flags & CnfLd_IgnoreErrors) == 0)
            WARNMSG("file \"%s\" doesn't exist or is too small.",fname);
        return false;
    }
    char* buf = (char*)KfxCalloc(len + 256, 1);
    if (buf == NULL)
        return false;
    // Loading file data
    len = LbFileLoadAt(fname, buf);
    TbBool result = (len > 0);
    // Parse blocks of the config file
    parse_named_field_blocks(buf, len, fname, flags, &crstates_states_named_fields_set);

    //Freeing and exiting
    KfxFree(buf);
    return result;
}

const char *creature_state_code_name(int64_t crstate)
{
    const char* name = get_conf_parameter_text(creatrstate_desc, crstate);
    if (name[0] != '\0')
        return name;
    return "INVALID";
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
