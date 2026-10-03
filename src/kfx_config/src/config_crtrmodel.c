/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_crtrmodel.c
 *     Specific creature model configuration loading functions.
 * @par Purpose:
 *     Support of configuration files for specific creatures.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     25 May 2009 - 26 Jul 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "config_crtrmodel.h"
#include "globals.h"

#include "bflib_basics.h"
#include "bflib_math.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"

#include "config.h"
#include "config_campaigns.h"
#include "config_creature.h"
#include "config_magic.h"
#include "config_terrain.h"
#include "config_lenses.h"
#include "config_translation.h"
#include "config_objects.h"
#include "kfx_config_state.h"
// Literal-dup of kfx_sim's creature_control.h CreatureSoundTypes values
// (only reachable transitively) -- kept local to this .c file (not
// config_creature.h) since kfx_sim files that include both
// config_creature.h and the real creature_control.h directly would
// collide with a shared-header enum redeclaration. See docs/refactor/
// stage-13-enforce-and-document.md.
#define CrSnd_Hit     1
#define CrSnd_Happy   2
#define CrSnd_Sad     3
#define CrSnd_Hang    4
#define CrSnd_Drop    5
#define CrSnd_Torture 6
#define CrSnd_Slap    7
#define CrSnd_Die     8
#define CrSnd_Foot    9
#define CrSnd_Fight   10
#define CrSnd_Piss    11

// remove_creature_lair()/update_creature_health_to_max()/
// update_relative_creature_health()/do_to_players_all_creatures_of_model()/
// do_to_all_things_of_class_and_model()/recalculate_all_creature_digger_lists()/
// update_speed_of_player_creatures_of_model() (all kfx_sim) are reached
// through SimPort instead of same-file bare-extern
// forward-declarations. See docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md.
#include "ports/ui_port.h"
#include "ports/render_port.h"
#include "ports/sim_port.h"
#include "compat_report.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

const struct NamedCommand creatmodel_attributes_commands[] = {
  {"NAME",                1}, // Deprecated entry kept to avoid noisy errors from old configs and scripts.
  {"HEALTH",              2},
  {"HEALREQUIREMENT",     3},
  {"HEALTHRESHOLD",       4},
  {"STRENGTH",            5},
  {"ARMOUR",              6},
  {"DEXTERITY",           7},
  {"FEARWOUNDED",         8},
  {"FEARSTRONGER",        9},
  {"DEFENCE",            10},
  {"LUCK",               11},
  {"RECOVERY",           12},
  {"HUNGERRATE",         13},
  {"HUNGERFILL",         14},
  {"LAIRSIZE",           15},
  {"HURTBYLAVA",         16},
  {"BASESPEED",          17},
  {"GOLDHOLD",           18},
  {"SIZE",               19},
  {"ATTACKPREFERENCE",   20},
  {"PAY",                21},
  {"HEROVSKEEPERCOST",   22}, // Removed.
  {"SLAPSTOKILL",        23},
  {"CREATURELOYALTY",    24},
  {"LOYALTYLEVEL",       25},
  {"DAMAGETOBOULDER",    26},
  {"THINGSIZE",          27},
  {"PROPERTIES",         28},
  {"NAMETEXTID",         29},
  {"FEARSOMEFACTOR",     30},
  {"TOKINGRECOVERY",     31},
  {"CORPSEVANISHEFFECT", 32},
  {"FOOTSTEPPITCH",      33},
  {"LAIROBJECT",         34},
  {"PRISONKIND",         35},
  {"TORTUREKIND",        36},
  {"SPELLIMMUNITY",      37},
  {"HOSTILETOWARDS",     38},
  {NULL,                  0},
  };

const struct NamedCommand creatmodel_properties_commands[] = {
  {"BLEEDS",             1},
  {"UNAFFECTED_BY_WIND", 2}, // Deprecated, but retained in NamedCommand for backward compatibility.
  {"IMMUNE_TO_GAS",      3}, // Deprecated, but retained in NamedCommand for backward compatibility.
  {"HUMANOID_SKELETON",  4},
  {"PISS_ON_DEAD",       5},
  {"FLYING",             7},
  {"SEE_INVISIBLE",      8},
  {"PASS_LOCKED_DOORS",  9},
  {"SPECIAL_DIGGER",    10},
  {"ARACHNID",          11},
  {"DIPTERA",           12},
  {"LORD",              13},
  {"SPECTATOR",         14},
  {"EVIL",              15},
  {"NEVER_CHICKENS",    16}, // Deprecated, but retained in NamedCommand for backward compatibility.
  {"IMMUNE_TO_BOULDER", 17},
  {"NO_CORPSE_ROTTING", 18},
  {"NO_ENMHEART_ATTCK", 19},
  {"TREMBLING_FAT",     20},
  {"FEMALE",            21},
  {"INSECT",            22},
  {"ONE_OF_KIND",       23},
  {"NO_IMPRISONMENT",   24},
  {"IMMUNE_TO_DISEASE", 25}, // Deprecated, but retained in NamedCommand for backward compatibility.
  {"ILLUMINATED",       26},
  {"ALLURING_SCVNGR",   27},
  {"NO_RESURRECT",      28},
  {"NO_TRANSFER",       29},
  {"TREMBLING",         30},
  {"FAT",               31},
  {"NO_STEAL_HERO",     32},
  {"PREFER_STEAL",      33},
  {"EVENTFUL_DEATH",    34},
  {"DIGGING_CREATURE",  35},
  {"NO_HEALTH_FLOWER",  36},
  {"CANNOT_PICK_UP",    37},
  {"DROP_ON_PATH",      38},
  {"CANNOT_POSSESS",    39},
  {NULL,                 0},
  };

const struct NamedCommand creatmodel_attraction_commands[] = {
  {"ENTRANCEROOM",       1},
  {"ROOMSLABSREQUIRED",  2},
  {"BASEENTRANCESCORE",  3},
  {"SCAVENGEREQUIREMENT",4},
  {"TORTURETIME",        5},
  {NULL,                 0},
  };

const struct NamedCommand creatmodel_annoyance_commands[] = {
  {"EATFOOD",              1},
  {"WILLNOTDOJOB",         2},
  {"INHAND",               3},
  {"NOLAIR",               4},
  {"NOHATCHERY",           5},
  {"WOKENUP",              6},
  {"STANDINGONDEADENEMY",  7},
  {"SULKING",              8},
  {"NOSALARY",             9},
  {"SLAPPED",             10},
  {"STANDINGONDEADFRIEND",11},
  {"INTORTURE",           12},
  {"INTEMPLE",            13},
  {"SLEEPING",            14},
  {"GOTWAGE",             15},
  {"WINBATTLE",           16},
  {"UNTRAINED",           17},
  {"OTHERSLEAVING",       18},
  {"JOBSTRESS",           19},
  {"QUEUE",               20},
  {"LAIRENEMY",           21},
  {"ANNOYLEVEL",          22},
  {"ANGERJOBS",           23},
  {"GOINGPOSTAL",         24},
  {NULL,                   0},
  };

const struct NamedCommand creatmodel_senses_commands[] = {
  {"HEARING",              1},
  {"EYEHEIGHT",            2},
  {"FIELDOFVIEW",          3},
  {"EYEEFFECT",            4},
  {"MAXANGLECHANGE",       5},
  {NULL,                   0},
  };

const struct NamedCommand creatmodel_appearance_commands[] = {
  {"WALKINGANIMSPEED",     1},
  {"VISUALRANGE",          2},
  {"POSSESSSWIPEINDEX",    3},
  {"NATURALDEATHKIND",     4},
  {"SHOTORIGIN",           5},
  {"CORPSEVANISHEFFECT",   6},
  {"FOOTSTEPPITCH",        7},
  {"PICKUPOFFSET",         8},
  {"STATUSOFFSET",         9},
  {"TRANSPARENCYFLAGS",   10},
  {"FIXEDANIMSPEED",      11},
  {NULL,                   0},
  };

const struct NamedCommand creature_deathkind_desc[] = {
    {"NORMAL",          Death_Normal},
    {"FLESHEXPLODE",    Death_FleshExplode},
    {"GASFLESHEXPLODE", Death_GasFleshExplode},
    {"SMOKEEXPLODE",    Death_SmokeExplode},
    {"ICEEXPLODE",      Death_IceExplode},
    {NULL,              0},
    };


const struct NamedCommand creatmodel_experience_commands[] = {
  {"POWERS",               1},
  {"POWERSLEVELREQUIRED",  2},
  {"LEVELSTRAINVALUES",    3},
  {"GROWUP",               4},
  {"SLEEPEXPERIENCE",      5},
  {"EXPERIENCEFORHITTING", 6},
  {"REBIRTH",              7},
  {NULL,                   0},
  };

const struct NamedCommand creatmodel_jobs_commands[] = {
  {"PRIMARYJOBS",          1},
  {"SECONDARYJOBS",        2},
  {"NOTDOJOBS",            3},
  {"STRESSFULJOBS",        4},
  {"TRAININGVALUE",        5},
  {"TRAININGCOST",         6},
  {"SCAVENGEVALUE",        7},
  {"SCAVENGERCOST",        8},
  {"RESEARCHVALUE",        9},
  {"MANUFACTUREVALUE",    10},
  {"PARTNERTRAINING",     11},
  {NULL,                   0},
  };

const struct NamedCommand creatmodel_sounds_commands[] = {
  {"HIT",                  CrSnd_Hit},
  {"HAPPY",                CrSnd_Happy},
  {"SAD",                  CrSnd_Sad},
  {"HANG",                 CrSnd_Hang},
  {"DROP",                 CrSnd_Drop},
  {"TORTURE",              CrSnd_Torture},
  {"SLAP",                 CrSnd_Slap},
  {"DIE",                  CrSnd_Die},
  {"FOOT",                 CrSnd_Foot},
  {"FIGHT",                CrSnd_Fight},
  {"PISS",                 CrSnd_Piss},
  {NULL,                   0},
  };

/******************************************************************************/

void strtolower(char * str) {
    for (; *str; ++str) {
        *str = tolower(*str);
    }
}

/******************************************************************************/
// The model config blocks as NamedField tables (refactor pass 3, S04). Plain
// numbers are value_creature_number/assign_creature_number rows (below); keys
// with their own rules keep them in a parse function, which writes the model
// itself and returns NAMFIELD_KEEP.

/** Whether plain numbers keep KeeperFX's atoi() rules: the CREATURE_STATS_WRAP classic bug. */
static TbBool creature_stats_wrap(void)
{
    return flag_is_set(kfx_config_state.conf.rules[0].gameplay.classic_bugs_flags, ClscBug_CreatureStatsWrap);
}

/**
 * A plain number: limited to its field's range with a warning, and text refused with a warning (0),
 * as in the other config files. With the CREATURE_STATS_WRAP classic bug, read with atoi() (text as
 * its leading number, or 0) as the hand-written parsers did (pass 3 finding F10).
 */
int64_t value_creature_number(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    if (creature_stats_wrap())
        return value_atoi(named_field, value_text, named_fields_set, idx, src_str, flags);
    return value_default(named_field, value_text, named_fields_set, idx, src_str, flags);
}

/** Stores value_creature_number()'s value; with CREATURE_STATS_WRAP, with C's conversion (so out of range wraps). */
static void assign_creature_number(const struct NamedField* named_field, int64_t value, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    if (creature_stats_wrap())
        assign_cast(named_field, value, named_fields_set, idx, src_str, flags);
    else
        assign_default(named_field, value, named_fields_set, idx, src_str, flags);
}

static int64_t* get_creaturemodel_count(void) { return &kfx_config_state.conf.crtr_conf.model_count; }
static void* get_creaturemodel_base(void) { return kfx_config_state.conf.crtr_conf.model; }

/** The per-creature model files' set: one CreatureModelConfig per creature model index. */
const struct NamedFieldSet creaturemodel_named_fields_set = {
    get_creaturemodel_count,
    "",
    NULL,
    NULL,
    CREATURE_TYPES_MAX,
    sizeof(struct CreatureModelConfig),
    get_creaturemodel_base,
};

#define CRPROP_FIELD(member) offsetof(struct CreatureModelConfig, member)

/** The creature properties (creatmodel_properties_commands' numbers) and what each switches: model flags, immunity
 *  flags, or a TbBool field (-1: none). The creature files' PROPERTIES and SET_CREATURE_PROPERTY read it. */
static const struct CreatureProperty creature_properties[] = {
    { 1, 0,                                  0,                 CRPROP_FIELD(bleeds)},              // BLEEDS
    { 2, 0,                                  CSAfF_Wind,        -1},                                // UNAFFECTED_BY_WIND
    { 3, 0,                                  CSAfF_PoisonCloud, -1},                                // IMMUNE_TO_GAS
    { 4, 0,                                  0,                 CRPROP_FIELD(humanoid_creature)},   // HUMANOID_SKELETON
    { 5, 0,                                  0,                 CRPROP_FIELD(piss_on_dead)},        // PISS_ON_DEAD
    { 7, 0,                                  0,                 CRPROP_FIELD(flying)},              // FLYING
    { 8, 0,                                  0,                 CRPROP_FIELD(can_see_invisible)},   // SEE_INVISIBLE
    { 9, 0,                                  0,                 CRPROP_FIELD(can_go_locked_doors)}, // PASS_LOCKED_DOORS
    {10, CMF_IsSpecDigger,                   0,                 -1},                                // SPECIAL_DIGGER
    {11, CMF_IsArachnid,                     0,                 -1},                                // ARACHNID
    {12, CMF_IsDiptera,                      0,                 -1},                                // DIPTERA
    {13, CMF_IsLordOfLand,                   0,                 -1},                                // LORD
    {14, CMF_IsSpectator,                    0,                 -1},                                // SPECTATOR
    {15, CMF_IsEvil,                         0,                 -1},                                // EVIL
    {16, 0,                                  CSAfF_Chicken,     -1},                                // NEVER_CHICKENS
    {17, CMF_ImmuneToBoulder,                0,                 -1},                                // IMMUNE_TO_BOULDER
    {18, CMF_NoCorpseRotting,                0,                 -1},                                // NO_CORPSE_ROTTING
    {19, CMF_NoEnmHeartAttack,               0,                 -1},                                // NO_ENMHEART_ATTCK
    {20, CMF_Trembling | CMF_Fat,            0,                 -1},                                // TREMBLING_FAT
    {21, CMF_Female,                         0,                 -1},                                // FEMALE
    {22, CMF_Insect,                         0,                 -1},                                // INSECT
    {23, CMF_OneOfKind,                      0,                 -1},                                // ONE_OF_KIND
    {24, CMF_NoImprisonment,                 0,                 -1},                                // NO_IMPRISONMENT
    {25, 0,                                  CSAfF_Disease,     -1},                                // IMMUNE_TO_DISEASE
    {26, 0,                                  0,                 CRPROP_FIELD(illuminated)},         // ILLUMINATED
    {27, 0,                                  0,                 CRPROP_FIELD(entrance_force)},      // ALLURING_SCVNGR
    {28, CMF_NoResurrect,                    0,                 -1},                                // NO_RESURRECT
    {29, CMF_NoTransfer,                     0,                 -1},                                // NO_TRANSFER
    {30, CMF_Trembling,                      0,                 -1},                                // TREMBLING
    {31, CMF_Fat,                            0,                 -1},                                // FAT
    {32, CMF_NoStealHero,                    0,                 -1},                                // NO_STEAL_HERO
    {33, CMF_PreferSteal,                    0,                 -1},                                // PREFER_STEAL
    {34, CMF_EventfulDeath,                  0,                 -1},                                // EVENTFUL_DEATH
    {35, CMF_IsDiggingCreature,              0,                 -1},                                // DIGGING_CREATURE
    {36, CMF_NoHealthFlower,                 0,                 -1},                                // NO_HEALTH_FLOWER
    {37, CMF_CannotPickUp,                   0,                 -1},                                // CANNOT_PICK_UP
    {38, CMF_DropOnPath,                     0,                 -1},                                // DROP_ON_PATH
    {39, CMF_CannotPossess,                  0,                 -1},                                // CANNOT_POSSESS
};

const struct CreatureProperty *creature_property_get(int64_t property)
{
    for (size_t i = 0; i < sizeof(creature_properties) / sizeof(creature_properties[0]); i++)
        if (creature_properties[i].num == property)
            return &creature_properties[i];
    return NULL;
}

TbBool creature_property_set(struct CreatureModelConfig *crconf, int64_t property, int64_t val)
{
    const struct CreatureProperty *prop = creature_property_get(property);
    if (prop == NULL)
        return false;
    if (prop->field >= 0)
    {
        *(TbBool *)((char *)crconf + prop->field) = (TbBool)val;
        return true;
    }
    if (val >= 1)
    {
        set_flag(crconf->model_flags, prop->model_flags);
        set_flag(crconf->immunity_flags, prop->immunity_flags);
    }
    else
    {
        clear_flag(crconf->model_flags, prop->model_flags);
        clear_flag(crconf->immunity_flags, prop->immunity_flags);
    }
    return true;
}

/** Properties: clears the flags the list sets (not the immunities), then sets each named one. */
static int64_t value_creature_properties(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    struct CreatureModelConfig* crconf = creature_stats_get(idx);
    char word_buf[COMMAND_WORD_LEN];
    int64_t pos = 0;
    const int64_t len = (int64_t)strlen(value_text);
    crconf->bleeds = false;
    crconf->humanoid_creature = false;
    crconf->piss_on_dead = false;
    crconf->flying = false;
    crconf->can_see_invisible = false;
    crconf->can_go_locked_doors = false;
    crconf->model_flags = 0;
    while (get_conf_parameter_single(value_text,&pos,len,word_buf,sizeof(word_buf)) > 0)
    {
        if (!creature_property_set(crconf, get_id(creatmodel_properties_commands, word_buf), 1))
        {
          CONFWRNLOG("Incorrect value of \"%s\" parameter \"%s\" in [%s] block of %s %s file.",
              named_field->name, word_buf, "attributes", creature_code_name(idx), src_str);
        }
    }
    return NAMFIELD_KEEP;
}

/** SpellImmunity: a number replaces the flags, names add to them; keeps the four immunities Properties sets. */
static int64_t value_spell_immunity(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    struct CreatureModelConfig* crconf = creature_stats_get(idx);
    char word_buf[COMMAND_WORD_LEN];
    int64_t pos = 0;
    const int64_t len = (int64_t)strlen(value_text);
    int64_t n = 0;
    int64_t k;
    // Backward compatibility check.
    TbBool unaffected_by_wind = flag_is_set(crconf->immunity_flags, CSAfF_Wind);
    TbBool immune_to_gas = flag_is_set(crconf->immunity_flags, CSAfF_PoisonCloud);
    TbBool never_chickens = flag_is_set(crconf->immunity_flags, CSAfF_Chicken);
    TbBool immune_to_disease = flag_is_set(crconf->immunity_flags, CSAfF_Disease);
    crconf->immunity_flags = 0; // Clear flags, this is necessary for partial config if modder wants to remove all flags.
    // Backward compatibility fix.
    if (unaffected_by_wind) { set_flag(crconf->immunity_flags, CSAfF_Wind); }
    if (immune_to_gas) { set_flag(crconf->immunity_flags, CSAfF_PoisonCloud); }
    if (never_chickens) { set_flag(crconf->immunity_flags, CSAfF_Chicken); }
    if (immune_to_disease) { set_flag(crconf->immunity_flags, CSAfF_Disease); }
    while (get_conf_parameter_single(value_text, &pos, len, word_buf, sizeof(word_buf)) > 0)
    {
        if (parameter_is_number(word_buf))
        {
            k = atoi(word_buf);
            crconf->immunity_flags = k;
            n++;
        }
        else
        {
            k = get_id(spell_effect_flags, word_buf);
            if (k > 0)
            {
                set_flag(crconf->immunity_flags, k);
                n++;
            }
        }
    }
    if (n < 1)
    {
        CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s %s file.",
            named_field->name, "attributes", creature_code_name(idx), src_str);
    }
    return NAMFIELD_KEEP;
}

/**
 * HostileTowards, LairEnemy: up to CREATURE_TYPES_MAX creature names into the
 * ThingModel array the row points at. ANY_CREATURE is CREATURE_ANY; an unknown
 * name (or NULL) stores 0; entries past the last name keep their value.
 */
static int64_t value_creature_list(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    ThingModel* list = (ThingModel*)((char*)creature_stats_get(idx) + (ptrdiff_t)named_field->field);
    char word_buf[COMMAND_WORD_LEN];
    int64_t pos = 0;
    const int64_t len = (int64_t)strlen(value_text);
    int64_t n = 0;
    int64_t k;
    for (int64_t i = 0; i < CREATURE_TYPES_MAX; i++)
    {
        if (get_conf_parameter_single(value_text, &pos, len, word_buf, sizeof(word_buf)) > 0)
        {
            k = get_id(creature_desc, word_buf);
            if (k >= 0)
            {
                list[i] = k;
                n++;
            }
            else if (0 == strcmp(word_buf, "ANY_CREATURE"))
            {
                list[i] = CREATURE_ANY;
                n++;
            }
            else
            {
                list[i] = 0;
                if (strcasecmp(word_buf, "NULL") == 0)
                {
                    n++;
                }
            }
        }
    }
    if (n < 1)
    {
        CONFWRNLOG("Incorrect value of \"%s\" parameter in %s file of creature %s.",
            named_field->name, src_str, creature_code_name(idx));
    }
    return NAMFIELD_KEEP;
}

#define CRMODEL_FIELD(member) field_t(struct CreatureModelConfig, member)
#define CRMODEL_NONE NULL, dt_void

const struct NamedField creaturemodel_attributes_named_fields[] = {
    //name                 //pos //field                                  //default //min //max //NamedCommand                  //parse                  //assign
    {"NAME",                  0, CRMODEL_NONE,                                    0, NAMFIELD_NO_BOUNDS, NULL,                           value_ignored,           assign_null}, // defined in creature.cfg
    {"HEALTH",                0, CRMODEL_FIELD(health),                           0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"HEALREQUIREMENT",       0, CRMODEL_FIELD(heal_requirement),                 0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"HEALTHRESHOLD",         0, CRMODEL_FIELD(heal_threshold),                   0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"STRENGTH",              0, CRMODEL_FIELD(strength),                         0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"ARMOUR",                0, CRMODEL_FIELD(armour),                           0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"DEXTERITY",             0, CRMODEL_FIELD(dexterity),                        0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"FEARWOUNDED",           0, CRMODEL_FIELD(fear_wounded),                     0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"FEARSTRONGER",          0, CRMODEL_FIELD(fear_stronger),                    0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"DEFENCE",               0, CRMODEL_FIELD(defense),                          0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"LUCK",                  0, CRMODEL_FIELD(luck),                             0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"RECOVERY",              0, CRMODEL_FIELD(sleep_recovery),                   0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"HUNGERRATE",            0, CRMODEL_FIELD(hunger_rate),                      0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"HUNGERFILL",            0, CRMODEL_FIELD(hunger_fill),                      0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"LAIRSIZE",              0, CRMODEL_FIELD(lair_size),                        0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"HURTBYLAVA",            0, CRMODEL_FIELD(hurt_by_lava),                     0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"BASESPEED",             0, CRMODEL_FIELD(base_speed),                       0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"GOLDHOLD",              0, CRMODEL_FIELD(gold_hold),                        0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"SIZE",                  0, CRMODEL_FIELD(size_xy),                          0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"SIZE",                  1, CRMODEL_FIELD(size_z),                           0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"ATTACKPREFERENCE",      0, CRMODEL_FIELD(attack_preference),                0, NAMFIELD_NO_BOUNDS, attackpref_desc,                value_id_positive,       assign_cast},
    {"PAY",                   0, CRMODEL_FIELD(pay),                              0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"HEROVSKEEPERCOST",      0, CRMODEL_NONE,                                    0, NAMFIELD_NO_BOUNDS, NULL,                           value_ignored,           assign_null}, // removed
    {"SLAPSTOKILL",           0, CRMODEL_FIELD(slaps_to_kill),                    0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"CREATURELOYALTY",       0, CRMODEL_NONE,                                    0, NAMFIELD_NO_BOUNDS, NULL,                           value_ignored,           assign_null}, // unused
    {"LOYALTYLEVEL",          0, CRMODEL_NONE,                                    0, NAMFIELD_NO_BOUNDS, NULL,                           value_ignored,           assign_null}, // unused
    {"DAMAGETOBOULDER",       0, CRMODEL_FIELD(damage_to_boulder),                0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"THINGSIZE",             0, CRMODEL_FIELD(thing_size_xy),                    0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"THINGSIZE",             1, CRMODEL_FIELD(thing_size_z),                     0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"PROPERTIES",           -2, CRMODEL_FIELD(model_flags),                      0, NAMFIELD_NO_BOUNDS, creatmodel_properties_commands, value_creature_properties, assign_null},
    {"NAMETEXTID",            0, CRMODEL_FIELD(namestr_idx),                      0, NAMFIELD_NO_BOUNDS, NULL,                           value_string_id_positive, assign_cast},
    {"FEARSOMEFACTOR",        0, CRMODEL_FIELD(fearsome_factor),                  0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"TOKINGRECOVERY",        0, CRMODEL_FIELD(toking_recovery),                  0, NAMFIELD_NO_BOUNDS, NULL,                           value_creature_number, assign_creature_number},
    {"CORPSEVANISHEFFECT",    0, CRMODEL_NONE,                                    0, NAMFIELD_NO_BOUNDS, NULL,                           value_ignored,           assign_null}, // read in [appearance]
    {"FOOTSTEPPITCH",         0, CRMODEL_NONE,                                    0, NAMFIELD_NO_BOUNDS, NULL,                           value_ignored,           assign_null}, // read in [appearance]
    {"LAIROBJECT",            0, CRMODEL_FIELD(lair_object),                      0, NAMFIELD_NO_BOUNDS, object_desc,                    value_id_positive,       assign_cast},
    {"PRISONKIND",            0, CRMODEL_FIELD(prison_kind),                      0, NAMFIELD_NO_BOUNDS, creature_desc,                  value_id_positive,       assign_cast},
    {"TORTUREKIND",           0, CRMODEL_FIELD(torture_kind),                     0, NAMFIELD_NO_BOUNDS, creature_desc,                  value_id_positive,       assign_cast},
    {"SPELLIMMUNITY",        -2, CRMODEL_FIELD(immunity_flags),                   0, NAMFIELD_NO_BOUNDS, spell_effect_flags,             value_spell_immunity,    assign_null},
    {"HOSTILETOWARDS",       -2, CRMODEL_FIELD(hostile_towards),                  0, NAMFIELD_NO_BOUNDS, creature_desc,                  value_creature_list,     assign_null},
    {NULL,                    0, CRMODEL_NONE,                                    0, 0, 0, NULL,                           NULL,                    NULL},
};

/** What the attributes decide beyond the model itself: the special breeds and the start states. */
static void creaturemodel_attributes_loaded(int64_t crtr_model)
{
    struct CreatureModelConfig* crconf = creature_stats_get(crtr_model);
    // If the creature is a special breed, then update an attribute in CreatureConfig struct
    if ((crconf->model_flags & (CMF_IsSpecDigger|CMF_IsDiggingCreature)) != 0)
    {
        if ((crconf->model_flags & CMF_IsEvil) != 0) {
            kfx_config_state.conf.crtr_conf.special_digger_evil = crtr_model;
        } else {
            kfx_config_state.conf.crtr_conf.special_digger_good = crtr_model;
        }
    }
    if ((crconf->model_flags & CMF_IsSpectator) != 0)
    {
        kfx_config_state.conf.crtr_conf.spectator_breed = crtr_model;
    }
    // Set creature start states based on the flags
    if ((crconf->model_flags & (CMF_IsSpecDigger|CMF_IsDiggingCreature)) != 0)
    {
        crconf->evil_start_state = CrSt_ImpDoingNothing;
        crconf->good_start_state = CrSt_TunnellerDoingNothing;
    } else
    {
        crconf->evil_start_state = CrSt_CreatureDoingNothing;
        crconf->good_start_state = CrSt_GoodDoingNothing;
    }
}

/** EntranceRoom: clears the list, then up to ENTRANCE_ROOMS_COUNT room names; unknown ones are skipped. */
static int64_t value_entrance_rooms(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    struct CreatureModelConfig* crconf = creature_stats_get(idx);
    char word_buf[COMMAND_WORD_LEN];
    int64_t pos = 0;
    const int64_t len = (int64_t)strlen(value_text);
    int64_t n = 0;
    int64_t k;
    for (k=0; k < ENTRANCE_ROOMS_COUNT; k++)
      crconf->entrance_rooms[k] = 0;
    while (get_conf_parameter_single(value_text,&pos,len,word_buf,sizeof(word_buf)) > 0)
    {
      k = get_id(room_desc, word_buf);
      if ((k >= 0) && (n < ENTRANCE_ROOMS_COUNT))
      {
        crconf->entrance_rooms[n] = k;
        n++;
      } else
      {
        CONFWRNLOG("Too many params, or incorrect value of \"%s\" parameter \"%s\", in [%s] block of %s file.",
            named_field->name, word_buf, "attraction", src_str);
      }
    }
    return NAMFIELD_KEEP;
}

/** RoomSlabsRequired: clears the list, then up to ENTRANCE_ROOMS_COUNT numbers. */
static int64_t value_entrance_slabs_required(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    struct CreatureModelConfig* crconf = creature_stats_get(idx);
    char word_buf[COMMAND_WORD_LEN];
    int64_t pos = 0;
    const int64_t len = (int64_t)strlen(value_text);
    int64_t n = 0;
    int64_t k;
    for (k=0; k < ENTRANCE_ROOMS_COUNT; k++)
      crconf->entrance_slabs_req[k] = 0;
    while (get_conf_parameter_single(value_text,&pos,len,word_buf,sizeof(word_buf)) > 0)
    {
      k = atoi(word_buf);
      if (n < ENTRANCE_ROOMS_COUNT)
      {
        crconf->entrance_slabs_req[n] = k;
        n++;
      } else
      {
        CONFWRNLOG("Too many parameters of \"%s\" in [%s] block of %s file.",
            named_field->name, "attraction", src_str);
      }
    }
    return NAMFIELD_KEEP;
}

const struct NamedField creaturemodel_attraction_named_fields[] = {
    //name                 //pos //field                                  //default //min //max //NamedCommand //parse                    //assign
    {"ENTRANCEROOM",         -2, CRMODEL_FIELD(entrance_rooms),                   0, NAMFIELD_NO_BOUNDS, room_desc,    value_entrance_rooms,          assign_null},
    {"ROOMSLABSREQUIRED",    -2, CRMODEL_FIELD(entrance_slabs_req),               0, NAMFIELD_NO_BOUNDS, NULL,         value_entrance_slabs_required, assign_null},
    {"BASEENTRANCESCORE",     0, CRMODEL_FIELD(entrance_score),                   0, NAMFIELD_NO_BOUNDS, NULL,         value_creature_number, assign_creature_number},
    {"SCAVENGEREQUIREMENT",   0, CRMODEL_FIELD(scavenge_require),                 0, NAMFIELD_NO_BOUNDS, NULL,         value_creature_number, assign_creature_number},
    {"TORTURETIME",           0, CRMODEL_FIELD(torture_break_time),               0, NAMFIELD_NO_BOUNDS, NULL,         value_creature_number, assign_creature_number},
    {NULL,                    0, CRMODEL_NONE,                                    0, 0, 0, NULL,         NULL,                          NULL},
};

const struct NamedField creaturemodel_annoyance_named_fields[] = {
    //name                 //pos //field                                  //default //min //max //NamedCommand   //parse               //assign
    {"EATFOOD",                0, CRMODEL_FIELD(annoy_eat_food),                0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"WILLNOTDOJOB",           0, CRMODEL_FIELD(annoy_will_not_do_job),         0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"INHAND",                 0, CRMODEL_FIELD(annoy_in_hand),                 0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"NOLAIR",                 0, CRMODEL_FIELD(annoy_no_lair),                 0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"NOHATCHERY",             0, CRMODEL_FIELD(annoy_no_hatchery),             0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"WOKENUP",                0, CRMODEL_FIELD(annoy_woken_up),                0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"STANDINGONDEADENEMY",    0, CRMODEL_FIELD(annoy_on_dead_enemy),           0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"SULKING",                0, CRMODEL_FIELD(annoy_sulking),                 0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"NOSALARY",               0, CRMODEL_FIELD(annoy_no_salary),               0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"SLAPPED",                0, CRMODEL_FIELD(annoy_slapped),                 0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"STANDINGONDEADFRIEND",   0, CRMODEL_FIELD(annoy_on_dead_friend),          0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"INTORTURE",              0, CRMODEL_FIELD(annoy_in_torture),              0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"INTEMPLE",               0, CRMODEL_FIELD(annoy_in_temple),               0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"SLEEPING",               0, CRMODEL_FIELD(annoy_sleeping),                0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"GOTWAGE",                0, CRMODEL_FIELD(annoy_got_wage),                0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"WINBATTLE",              0, CRMODEL_FIELD(annoy_win_battle),              0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"UNTRAINED",              0, CRMODEL_FIELD(annoy_untrained_time),          0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"UNTRAINED",              1, CRMODEL_FIELD(annoy_untrained),               0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"OTHERSLEAVING",          0, CRMODEL_FIELD(annoy_others_leaving),          0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"JOBSTRESS",              0, CRMODEL_FIELD(annoy_job_stress),              0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"QUEUE",                  0, CRMODEL_FIELD(annoy_queue),                   0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"LAIRENEMY",             -2, CRMODEL_FIELD(lair_enemy),                    0, NAMFIELD_NO_BOUNDS, creature_desc,   value_creature_list,  assign_null},
    {"ANNOYLEVEL",             0, CRMODEL_FIELD(annoy_level),                   0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {"ANGERJOBS",             -2, CRMODEL_FIELD(jobs_anger),                    0, NAMFIELD_NO_BOUNDS, angerjob_desc,   value_ids_or,         assign_cast},
    {"GOINGPOSTAL",            0, CRMODEL_FIELD(annoy_going_postal),            0, NAMFIELD_NO_BOUNDS, NULL,            value_creature_number, assign_creature_number},
    {NULL,                     0, CRMODEL_NONE,                                    0, 0, 0, NULL,            NULL,                 NULL},
};

/** MaxAngleChange: degrees, stored as the game's angle units; zero or less leaves the field as it is. */
static int64_t value_max_angle_change(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    const int64_t k = atoi(value_text);
    if (k <= 0)
    {
        CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
            named_field->name, "senses", src_str);
        return NAMFIELD_KEEP;
    }
    return (k * DEGREES_180) / 180;
}

const struct NamedField creaturemodel_senses_named_fields[] = {
    //name                 //pos //field                                  //default //min //max //NamedCommand //parse                 //assign
    {"HEARING",               0, CRMODEL_FIELD(hearing),                          0, NAMFIELD_NO_BOUNDS, NULL,         value_creature_number, assign_creature_number},
    {"EYEHEIGHT",             0, CRMODEL_FIELD(base_eye_height),                  0, NAMFIELD_NO_BOUNDS, NULL,         value_creature_number, assign_creature_number},
    {"FIELDOFVIEW",           0, CRMODEL_FIELD(field_of_view),                    0, NAMFIELD_NO_BOUNDS, NULL,         value_creature_number, assign_creature_number},
    {"EYEEFFECT",             0, CRMODEL_FIELD(eye_effect),                       0, NAMFIELD_NO_BOUNDS, lenses_desc,  value_id_nonneg,        assign_cast},
    {"MAXANGLECHANGE",        0, CRMODEL_FIELD(max_turning_speed),                0, NAMFIELD_NO_BOUNDS, NULL,         value_max_angle_change, assign_cast},
    {NULL,                    0, CRMODEL_NONE,                                    0, 0, 0, NULL,         NULL,                   NULL},
};

/** TransparencyFlags: shifted into the render flags' transparency bits; a negative value leaves the field as it is. */
static int64_t value_transparency_flags(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    const int64_t k = atoi(value_text);
    if (k < 0)
        return NAMFIELD_KEEP;
    return k<<4; // Bitshift to get the transparancy bit in the render flag
}

const struct NamedField creaturemodel_appearance_named_fields[] = {
    //name                 //pos //field                                           //default //min //max //NamedCommand           //parse                   //assign
    {"WALKINGANIMSPEED",      0, CRMODEL_FIELD(walking_anim_speed),                        0, NAMFIELD_NO_BOUNDS, NULL,                   value_creature_number, assign_creature_number},
    {"VISUALRANGE",           0, CRMODEL_FIELD(visual_range),                              0, NAMFIELD_NO_BOUNDS, NULL,                   value_creature_number, assign_creature_number},
    {"POSSESSSWIPEINDEX",     0, CRMODEL_FIELD(swipe_idx),                                 0, 0, INT64_MAX, NULL,                   value_atoi_nonneg,        assign_cast},
    {"NATURALDEATHKIND",      0, CRMODEL_FIELD(natural_death_kind),                        0, NAMFIELD_NO_BOUNDS, creature_deathkind_desc, value_id_positive,       assign_cast},
    {"SHOTORIGIN",            0, CRMODEL_FIELD(shot_shift_x),                              0, NAMFIELD_NO_BOUNDS, NULL,                   value_creature_number, assign_creature_number},
    {"SHOTORIGIN",            1, CRMODEL_FIELD(shot_shift_y),                              0, NAMFIELD_NO_BOUNDS, NULL,                   value_creature_number, assign_creature_number},
    {"SHOTORIGIN",            2, CRMODEL_FIELD(shot_shift_z),                              0, NAMFIELD_NO_BOUNDS, NULL,                   value_creature_number, assign_creature_number},
    {"CORPSEVANISHEFFECT",    0, CRMODEL_FIELD(corpse_vanish_effect),                      0, NAMFIELD_NO_BOUNDS, NULL,                   value_creature_number, assign_creature_number},
    {"FOOTSTEPPITCH",         0, CRMODEL_FIELD(footstep_pitch),                            0, NAMFIELD_NO_BOUNDS, NULL,                   value_creature_number, assign_creature_number},
    {"PICKUPOFFSET",          0, CRMODEL_FIELD(creature_picked_up_offset.delta_x),         0, NAMFIELD_NO_BOUNDS, NULL,                   value_creature_number, assign_creature_number},
    {"PICKUPOFFSET",          1, CRMODEL_FIELD(creature_picked_up_offset.delta_y),         0, NAMFIELD_NO_BOUNDS, NULL,                   value_creature_number, assign_creature_number},
    {"STATUSOFFSET",          0, CRMODEL_FIELD(status_offset),                             0, NAMFIELD_NO_BOUNDS, NULL,                   value_creature_number, assign_creature_number},
    {"TRANSPARENCYFLAGS",     0, CRMODEL_FIELD(transparency_flags),                        0, NAMFIELD_NO_BOUNDS, NULL,                   value_transparency_flags, assign_cast},
    {"FIXEDANIMSPEED",        0, CRMODEL_FIELD(fixed_anim_speed),                          0, 0, INT64_MAX, NULL,                   value_atoi_nonneg,        assign_cast},
    {NULL,                    0, CRMODEL_NONE,                                             0, 0, 0, NULL,                   NULL,                     NULL},
};

/** Powers: up to LEARNED_INSTANCES_COUNT instance names, filled from the start; unknown ones are skipped. */
static int64_t value_learned_powers(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    struct CreatureModelConfig* crconf = creature_stats_get(idx);
    char word_buf[COMMAND_WORD_LEN];
    int64_t pos = 0;
    const int64_t len = (int64_t)strlen(value_text);
    int64_t n = 0;
    int64_t k;
    while (get_conf_parameter_single(value_text,&pos,len,word_buf,sizeof(word_buf)) > 0)
    {
      k = get_id(instance_desc, word_buf);
      if ((k >= 0) && (n < LEARNED_INSTANCES_COUNT))
      {
        crconf->learned_instance_id[n] = k;
        n++;
      } else
      {
        CONFWRNLOG("Too many params, or incorrect value of \"%s\" parameter \"%s\", in [%s] block of %s file.",
            named_field->name,word_buf, "experience", src_str);
      }
    }
    return NAMFIELD_KEEP;
}

/** PowersLevelRequired: the level each of Powers needs, filled from the start; negative ones are skipped. */
static int64_t value_learned_power_levels(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    struct CreatureModelConfig* crconf = creature_stats_get(idx);
    char word_buf[COMMAND_WORD_LEN];
    int64_t pos = 0;
    const int64_t len = (int64_t)strlen(value_text);
    int64_t n = 0;
    int64_t k;
    while (get_conf_parameter_single(value_text,&pos,len,word_buf,sizeof(word_buf)) > 0)
    {
      k = atoi(word_buf);
      if ((k >= 0) && (n < LEARNED_INSTANCES_COUNT))
      {
        crconf->learned_instance_level[n] = k;
        n++;
      } else
      {
        CONFWRNLOG("Too many params, or incorrect value of \"%s\" parameter \"%s\", in [%s] block of %s file.",
            named_field->name,word_buf, "experience", src_str);
      }
    }
    return NAMFIELD_KEEP;
}

/** LevelsTrainValues: training points for each level up, filled from the start; negative ones are skipped. */
static int64_t value_levels_train_values(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    struct CreatureModelConfig* crconf = creature_stats_get(idx);
    char word_buf[COMMAND_WORD_LEN];
    int64_t pos = 0;
    const int64_t len = (int64_t)strlen(value_text);
    int64_t n = 0;
    int64_t k;
    while (get_conf_parameter_single(value_text,&pos,len,word_buf,sizeof(word_buf)) > 0)
    {
      k = atoi(word_buf);
      if ((k >= 0) && (n < CREATURE_MAX_LEVEL-1))
      {
        crconf->to_level[n] = k;
        n++;
      } else
      {
        CONFWRNLOG("Too many params, or incorrect value of \"%s\" parameter \"%s\", in [%s] block of %s file.",
            named_field->name,word_buf, "experience", src_str);
      }
    }
    return NAMFIELD_KEEP;
}

/** GrowUp: the training points for the last level, the creature it grows into (or NULL), and at what level. */
static int64_t value_grow_up(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    struct CreatureModelConfig* crconf = creature_stats_get(idx);
    char word_buf[COMMAND_WORD_LEN];
    int64_t pos = 0;
    const int64_t len = (int64_t)strlen(value_text);
    int64_t n = 0;
    int64_t k;
    if (get_conf_parameter_single(value_text,&pos,len,word_buf,sizeof(word_buf)) > 0)
    {
      k = atoi(word_buf);
      crconf->to_level[CREATURE_MAX_LEVEL-1] = k;
      n++;
    }
    if (get_conf_parameter_single(value_text,&pos,len,word_buf,sizeof(word_buf)) > 0)
    {
      k = parse_creature_name(word_buf);
      if (k >= 0)
      {
        crconf->grow_up = k;
        n++;
      } else
      {
        crconf->grow_up = 0;
        if (strcasecmp(word_buf,"NULL") == 0)
          n++;
      }
    }
    if (get_conf_parameter_single(value_text,&pos,len,word_buf,sizeof(word_buf)) > 0)
    {
      k = atoi(word_buf);
      crconf->grow_up_level = k;
      n++;
    }
    if (n < 3)
    {
      CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s file.",
          named_field->name, "experience", src_str);
    }
    return NAMFIELD_KEEP;
}

/** SleepExperience: pairs of slab kind and experience gained sleeping next to it. */
static int64_t value_sleep_experience(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    struct CreatureModelConfig* crconf = creature_stats_get(idx);
    char word_buf[COMMAND_WORD_LEN];
    int64_t pos = 0;
    const int64_t len = (int64_t)strlen(value_text);
    int64_t n = 0;
    int64_t k;
    for (uint64_t i = 0; i < SLEEP_XP_COUNT; i++)
    {
        if (get_conf_parameter_single(value_text,&pos,len,word_buf,sizeof(word_buf)) > 0)
        {
          k = get_id(slab_desc, word_buf);
          if (k >= 0)
          {
            crconf->sleep_exp_slab[i] = k;
            n++;
          } else
          {
            crconf->sleep_exp_slab[i] = 0;
          }
        }
        else
        {
            for (uint64_t j = i; j < SLEEP_XP_COUNT; j++)
            {
                crconf->sleep_exp_slab[j] = 0;
                crconf->sleep_experience[j] = 0;
            }
            break;
        }
        if (get_conf_parameter_single(value_text,&pos,len,word_buf,sizeof(word_buf)) > 0)
        {
          k = atoi(word_buf);
          if (k < 0)
          {
              ERRORLOG("Slab sleep experience value (%s %" PRId64 ") must be 0 or greater.", slab_code_name(crconf->sleep_exp_slab[i]), (int64_t)(k));
              k = 0;
          }
          crconf->sleep_experience[i] = k;
          n++;
        }
        else
        {
            crconf->sleep_experience[i] = 0;
        }
    }
    if (n < 2)
    {
      CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s file.",
          named_field->name, "experience", src_str);
    }
    return NAMFIELD_KEEP;
}

const struct NamedField creaturemodel_experience_named_fields[] = {
    //name                 //pos //field                                  //default //min //max //NamedCommand  //parse                     //assign
    {"POWERS",               -2, CRMODEL_FIELD(learned_instance_id),              0, NAMFIELD_NO_BOUNDS, instance_desc, value_learned_powers,       assign_null},
    {"POWERSLEVELREQUIRED",  -2, CRMODEL_FIELD(learned_instance_level),           0, NAMFIELD_NO_BOUNDS, NULL,          value_learned_power_levels, assign_null},
    {"LEVELSTRAINVALUES",    -2, CRMODEL_FIELD(to_level),                         0, NAMFIELD_NO_BOUNDS, NULL,          value_levels_train_values,  assign_null},
    {"GROWUP",               -2, CRMODEL_FIELD(grow_up),                          0, NAMFIELD_NO_BOUNDS, creature_desc, value_grow_up,              assign_null},
    {"SLEEPEXPERIENCE",      -2, CRMODEL_FIELD(sleep_experience),                 0, NAMFIELD_NO_BOUNDS, slab_desc,     value_sleep_experience,     assign_null},
    {"EXPERIENCEFORHITTING",  0, CRMODEL_FIELD(exp_for_hitting),                  0, NAMFIELD_NO_BOUNDS, NULL,          value_creature_number, assign_creature_number},
    {"REBIRTH",               0, CRMODEL_FIELD(rebirth),                          0, NAMFIELD_NO_BOUNDS, NULL,          value_creature_number, assign_creature_number},
    {NULL,                    0, CRMODEL_NONE,                                    0, 0, 0, NULL,          NULL,                       NULL},
};

const struct NamedField creaturemodel_jobs_named_fields[] = {
    //name                 //pos //field                                  //default //min //max //NamedCommand     //parse        //assign
    {"PRIMARYJOBS",          -2, CRMODEL_FIELD(job_primary),                      0, NAMFIELD_NO_BOUNDS, creaturejob_desc, value_ids_or,  assign_cast},
    {"SECONDARYJOBS",        -2, CRMODEL_FIELD(job_secondary),                    0, NAMFIELD_NO_BOUNDS, creaturejob_desc, value_ids_or,  assign_cast},
    {"NOTDOJOBS",            -2, CRMODEL_FIELD(jobs_not_do),                      0, NAMFIELD_NO_BOUNDS, creaturejob_desc, value_ids_or,  assign_cast},
    {"STRESSFULJOBS",        -2, CRMODEL_FIELD(job_stress),                       0, NAMFIELD_NO_BOUNDS, creaturejob_desc, value_ids_or,  assign_cast},
    {"TRAININGVALUE",         0, CRMODEL_FIELD(training_value),                   0, NAMFIELD_NO_BOUNDS, NULL,             value_creature_number, assign_creature_number},
    {"TRAININGCOST",          0, CRMODEL_FIELD(training_cost),                    0, NAMFIELD_NO_BOUNDS, NULL,             value_creature_number, assign_creature_number},
    {"SCAVENGEVALUE",         0, CRMODEL_FIELD(scavenge_value),                   0, NAMFIELD_NO_BOUNDS, NULL,             value_creature_number, assign_creature_number},
    {"SCAVENGERCOST",         0, CRMODEL_FIELD(scavenger_cost),                   0, NAMFIELD_NO_BOUNDS, NULL,             value_creature_number, assign_creature_number},
    {"RESEARCHVALUE",         0, CRMODEL_FIELD(research_value),                   0, NAMFIELD_NO_BOUNDS, NULL,             value_creature_number, assign_creature_number},
    {"MANUFACTUREVALUE",      0, CRMODEL_FIELD(manufacture_value),                0, NAMFIELD_NO_BOUNDS, NULL,             value_creature_number, assign_creature_number},
    {"PARTNERTRAINING",       0, CRMODEL_FIELD(partner_training),                 0, NAMFIELD_NO_BOUNDS, NULL,             value_creature_number, assign_creature_number},
    {NULL,                    0, CRMODEL_NONE,                                    0, 0, 0, NULL,             NULL,          NULL},
};

static int64_t* get_creature_graphics_count(void) { return &kfx_config_state.conf.crtr_conf.model_count; }
static void* get_creature_graphics_base(void) { return kfx_config_state.conf.crtr_conf.creature_graphics; }

/** The [sprites] block's set: one row of CREATURE_GRAPHICS_INSTANCES sprite ids per creature model. */
const struct NamedFieldSet creature_graphics_named_fields_set = {
    get_creature_graphics_count,
    "",
    NULL,
    NULL,
    CREATURE_TYPES_MAX,
    sizeof(kfx_config_state.conf.crtr_conf.creature_graphics[0]),
    get_creature_graphics_base,
};

/** A [sprites] row's field is its graphics sequence index (CGI_*), as an offset into the model's row. */
#define CRSPRITE_FIELD(seq_idx) (void*)(ptrdiff_t)((seq_idx) * sizeof(int64_t)), dt_longlong

static int64_t creature_sprite_seq(const struct NamedField* named_field)
{
    return (int64_t)((ptrdiff_t)named_field->field / (ptrdiff_t)sizeof(int64_t));
}

/** QuerySymbol, HandSymbol: an icon; INT16_MAX (bad_icon_id) when unknown. The other sprites are value_animid rows. */
static int64_t value_creature_symbol(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    const int64_t n = render_get_icon_id(value_text);
    if (n >= 0)
        return n;
    CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
               named_field->name, "sprites", src_str);
    return INT16_MAX; // bad_icon_id (kfx_render's custom_sprites.c) literal-duplicated
}

static void assign_creature_sprite(const struct NamedField* named_field, int64_t value, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    set_creature_model_graphics(idx, creature_sprite_seq(named_field), value);
}

const struct NamedField creaturemodel_sprites_named_fields[] = {
    //name              //pos //field                          //default //min //max //NamedCommand //parse                //assign
    {"STAND",           0, CRSPRITE_FIELD(CGI_Stand),         0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"AMBULATE",        0, CRSPRITE_FIELD(CGI_Ambulate),      0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"DRAG",            0, CRSPRITE_FIELD(CGI_Drag),          0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"ATTACK",          0, CRSPRITE_FIELD(CGI_Attack),        0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"DIG",             0, CRSPRITE_FIELD(CGI_Dig),           0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"SMOKE",           0, CRSPRITE_FIELD(CGI_Smoke),         0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"RELAX",           0, CRSPRITE_FIELD(CGI_Relax),         0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"PRETTYDANCE",     0, CRSPRITE_FIELD(CGI_PrettyDance),   0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"GOTHIT",          0, CRSPRITE_FIELD(CGI_GotHit),        0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"POWERGRAB",       0, CRSPRITE_FIELD(CGI_PowerGrab),     0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"GOTSLAPPED",      0, CRSPRITE_FIELD(CGI_GotSlapped),    0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"CELEBRATE",       0, CRSPRITE_FIELD(CGI_Celebrate),     0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"SLEEP",           0, CRSPRITE_FIELD(CGI_Sleep),         0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"EATCHICKEN",      0, CRSPRITE_FIELD(CGI_EatChicken),    0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"TORTURE",         0, CRSPRITE_FIELD(CGI_Torture),       0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"SCREAM",          0, CRSPRITE_FIELD(CGI_Scream),        0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"DROPDEAD",        0, CRSPRITE_FIELD(CGI_DropDead),      0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"DEADSPLAT",       0, CRSPRITE_FIELD(CGI_DeadSplat),     0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"ROAR",            0, CRSPRITE_FIELD(CGI_Roar),          0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"QUERYSYMBOL",     0, CRSPRITE_FIELD(CGI_QuerySymbol),   0, NAMFIELD_NO_BOUNDS, NULL,          value_creature_symbol, assign_creature_sprite},
    {"HANDSYMBOL",      0, CRSPRITE_FIELD(CGI_HandSymbol),    0, NAMFIELD_NO_BOUNDS, NULL,          value_creature_symbol, assign_creature_sprite},
    {"PISS",            0, CRSPRITE_FIELD(CGI_Piss),          0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"CASTSPELL",       0, CRSPRITE_FIELD(CGI_CastSpell),     0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"RANGEDATTACK",    0, CRSPRITE_FIELD(CGI_RangedAttack),  0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {"CUSTOM",          0, CRSPRITE_FIELD(CGI_Custom),        0, NAMFIELD_NO_BOUNDS, NULL,          value_animid,          assign_creature_sprite},
    {NULL,                0, NULL, dt_void,                            0, 0, 0, NULL,          NULL,                  NULL},
};

/* C bridge to sound_manager.cpp – declared here so all sound parsing helpers can use it */
#ifdef __cplusplus
extern "C" {
#endif
extern int64_t load_creature_custom_sounds(int64_t crtr_model, const char* sound_type, const char* wav_paths, int64_t count, const char* config_textname);
#ifdef __cplusplus
}
#endif

/** Returns true if the string looks like a file path rather than a numeric sound ID. */
static TbBool is_sound_file_path(const char *s)
{
    if (s == NULL || s[0] == '\0') return false;
    if (strchr(s, '/') != NULL || strchr(s, '\\') != NULL) return true;
    /* Has a dot but atoi gives 0 and doesn't start with '0' or '-' → looks like a filename */
    if (strchr(s, '.') != NULL && atoi(s) == 0 && s[0] != '0' && s[0] != '-') return true;
    return false;
}

/** Returns true if the string is the special keyword "NONE" (case-insensitive),
 *  which explicitly disables / silences a creature sound slot. */
static TbBool is_sound_none_keyword(const char *s)
{
    if (s == NULL) return false;
    return (strcasecmp(s, "NONE") == 0);
}

/**
 * Expands a base path and a count into sequential numbered file paths.
 * Finds the trailing digit run before the extension and increments it N times.
 * Example: base="creature/maiden/hit1.wav" count=4 → hit1, hit2, hit3, hit4
 * Example: base="SHE_ORC_Hit_01.mp3" count=5     → _01, _02, _03, _04, _05
 * Returns the number of paths written (capped at 32).
 */
static int64_t expand_numbered_sound_paths(const char *base, int64_t count, char out[32][512])
{
    if (base == NULL || count <= 0) return 0;
    if (count > 32) count = 32;

    /* Find extension and filename start */
    const char *slash = strrchr(base, '/');
    if (!slash) slash = strrchr(base, '\\');
    const char *fname_start = slash ? slash + 1 : base;

    const char *dot = strrchr(base, '.');
    const char *ext = (dot != NULL && dot >= fname_start) ? dot : (base + strlen(base));
    size_t base_len = (size_t)(ext - base);

    /* Find trailing digit run within the base (before extension) */
    size_t num_start = base_len;
    while (num_start > (size_t)(fname_start - base)
           && isdigit((unsigned char)base[num_start - 1]))
    {
        num_start--;
    }

    if (num_start >= base_len)
    {
        /* No trailing digits — produce count copies of the same path */
        for (int64_t i = 0; i < count; i++)
            snprintf(out[i], 512, "%s", base);
        return count;
    }

    int64_t base_num = atoi(base + num_start);
    int64_t pad_width = (int64_t)(base_len - num_start);

    for (int64_t i = 0; i < count; i++)
        snprintf(out[i], 512, "%.*s%0*" PRId64 "%s",
            (int)((int64_t)num_start), base,
            (int)(pad_width), (int64_t)(base_num + i),
            ext);
    return count;
}

/** Expands a base path + count into numbered variants then loads them as a custom creature sound.
 *  Logs the parsed paths at debug level 5, warnings on any failure. */
static void load_creature_sound_from_path(int64_t crtr_model, const char* sound_key,
    const char* base_path, int64_t file_count, const char* config_textname)
{
    char expanded[32][512];
    int64_t exp_n = expand_numbered_sound_paths(base_path, file_count, expanded);
    if (exp_n <= 0)
    {
        WARNLOG("Custom sound: failed to expand paths for %s.%s base='%s'",
            creature_code_name(crtr_model), sound_key, base_path);
        return;
    }

    SYNCDBG(5, "Custom sound: %s.%s base='%s' count=%" PRId64 " -> %" PRId64 " path(s)",
        creature_code_name(crtr_model), sound_key, base_path, (int64_t)(file_count), (int64_t)(exp_n));
    for (int64_t i = 0; i < exp_n; i++)
        SYNCDBG(7, "  [%" PRId64 "] '%s'", (int64_t)(i), expanded[i]);

    int64_t result = load_creature_custom_sounds(crtr_model, sound_key,
        (const char*)expanded, exp_n, config_textname);
    if (!result)
        WARNLOG("Custom sound: failed to load for %s.%s from '%s'",
            creature_code_name(crtr_model), sound_key, base_path);
    else
    {
        SYNCDBG(5, "Custom sound: loaded %s.%s (%" PRId64 " variant(s))",
            creature_code_name(crtr_model), sound_key, (int64_t)(exp_n));
    }
}

/**
 * A sound: a sound number and count, NONE, or a file path (numbered files: path and count). A sound number
 * doesn't replace a custom sound (a file path, stored as a negative index) that an earlier file gave, so a
 * campaign's full copy of the creature files keeps a mod's custom sounds; NONE or another path does replace
 * it. (Only Die did this until pass 3 finding F12 was fixed; the other ten took the number.)
 */
static int64_t value_creature_sound(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    struct CreatureSound* snd = (struct CreatureSound*)((char*)&kfx_config_state.conf.crtr_conf.creature_sounds[idx] + (ptrdiff_t)named_field->field);
    char word_buf[COMMAND_WORD_LEN];
    int64_t pos = 0;
    const int64_t len = (int64_t)strlen(value_text);
    int64_t n = 0;
    int64_t k;
    if (get_conf_parameter_single(value_text,&pos,len,word_buf,sizeof(word_buf)) > 0)
    {
        if (is_sound_file_path(word_buf))
        {
            char count_buf[COMMAND_WORD_LEN];
            int64_t file_count = 1;
            if (get_conf_parameter_single(value_text,&pos,len,count_buf,sizeof(count_buf)) > 0) { file_count = atoi(count_buf); n++; }
            load_creature_sound_from_path(idx, named_field->name, word_buf, file_count, src_str);
        }
        else
        {
            if (is_sound_none_keyword(word_buf)) {
                /* NONE explicitly silences this sound, overriding any custom sound */
                snd->index = 0;
            } else {
                k = atoi(word_buf);
                /* Only overwrite if not already set to custom sound (negative value) */
                if (snd->index >= 0)
                {
                    snd->index = k;
                    SYNCDBG(8, "Set %s %s sound index to %" PRId64, creature_code_name(idx), named_field->name, (int64_t)(k));
                }
                else
                {
                    SYNCDBG(0, "Preserving custom %s sound (index %" PRId64 ") for %s, ignoring %s=%" PRId64 " from %s",
                        named_field->name, (int64_t)(snd->index),
                        creature_code_name(idx), named_field->name, (int64_t)(k), src_str);
                }
                if (get_conf_parameter_single(value_text,&pos,len,word_buf,sizeof(word_buf)) > 0) {
                    k = atoi(word_buf);
                    if (snd->index >= 0)
                        snd->count = k;
                    n++;
                }
            }
            n++;
        }
    }
    if (n < 1)
    {
      CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
          named_field->name, "sounds", src_str);
    }
    return NAMFIELD_KEEP;
}

static int64_t* get_creature_sounds_count(void) { return &kfx_config_state.conf.crtr_conf.model_count; }
static void* get_creature_sounds_base(void) { return kfx_config_state.conf.crtr_conf.creature_sounds; }

/** The [sounds] block's set: one CreatureSounds per creature model. */
const struct NamedFieldSet creature_sounds_named_fields_set = {
    get_creature_sounds_count,
    "",
    NULL,
    NULL,
    CREATURE_TYPES_MAX,
    sizeof(struct CreatureSounds),
    get_creature_sounds_base,
};

#define CRSOUND_FIELD(member) field_t(struct CreatureSounds, member)

// Row names are passed on as the sound's name for custom sound files, so they keep this spelling.
const struct NamedField creaturemodel_sounds_named_fields[] = {
    //name     //pos //field                //default //min //max //NamedCommand //parse                           //assign
    {"Hit",       -2, CRSOUND_FIELD(hit),           0, NAMFIELD_NO_BOUNDS, NULL,          value_creature_sound,             assign_null},
    {"Happy",     -2, CRSOUND_FIELD(happy),         0, NAMFIELD_NO_BOUNDS, NULL,          value_creature_sound,             assign_null},
    {"Sad",       -2, CRSOUND_FIELD(sad),           0, NAMFIELD_NO_BOUNDS, NULL,          value_creature_sound,             assign_null},
    {"Hang",      -2, CRSOUND_FIELD(hang),          0, NAMFIELD_NO_BOUNDS, NULL,          value_creature_sound,             assign_null},
    {"Drop",      -2, CRSOUND_FIELD(drop),          0, NAMFIELD_NO_BOUNDS, NULL,          value_creature_sound,             assign_null},
    {"Torture",   -2, CRSOUND_FIELD(torture),       0, NAMFIELD_NO_BOUNDS, NULL,          value_creature_sound,             assign_null},
    {"Slap",      -2, CRSOUND_FIELD(slap),          0, NAMFIELD_NO_BOUNDS, NULL,          value_creature_sound,             assign_null},
    {"Die",       -2, CRSOUND_FIELD(die),           0, NAMFIELD_NO_BOUNDS, NULL,          value_creature_sound,             assign_null},
    {"Foot",      -2, CRSOUND_FIELD(foot),          0, NAMFIELD_NO_BOUNDS, NULL,          value_creature_sound,             assign_null},
    {"Fight",     -2, CRSOUND_FIELD(fight),         0, NAMFIELD_NO_BOUNDS, NULL,          value_creature_sound,             assign_null},
    {"Piss",      -2, CRSOUND_FIELD(piss),          0, NAMFIELD_NO_BOUNDS, NULL,          value_creature_sound,             assign_null},
    {NULL,         0, NULL, dt_void,                0, 0, 0, NULL,          NULL,                             NULL},
};

static TbBool load_creaturemodel_config_file_impl(int64_t crtr_model, const char *fname, int64_t flags)
{
    SYNCDBG(0,"%s model %" PRId64 " from file \"%s\".",((flags & CnfLd_ListOnly) == 0)?"Reading":"Parsing",(int64_t)(crtr_model),fname);
    int64_t len = LbFileLengthRnc(fname);
    if (len < MIN_CONFIG_FILE_SIZE)
    {
        return false;
    }
    char* buf = (char*)KfxCalloc(len + 256, 1);
    if (buf == NULL)
        return false;
    // Loading file data
    len = LbFileLoadAt(fname, buf);
    TbBool result = (len > 0);
    // Parse blocks of the config file
    if (result)
    {
        if (parse_named_field_block(buf, len, fname, flags, "attributes", creaturemodel_attributes_named_fields, &creaturemodel_named_fields_set, crtr_model))
            creaturemodel_attributes_loaded(crtr_model);
        parse_named_field_block(buf, len, fname, flags, "attraction", creaturemodel_attraction_named_fields, &creaturemodel_named_fields_set, crtr_model);
        parse_named_field_block(buf, len, fname, flags, "annoyance", creaturemodel_annoyance_named_fields, &creaturemodel_named_fields_set, crtr_model);
        parse_named_field_block(buf, len, fname, flags, "senses", creaturemodel_senses_named_fields, &creaturemodel_named_fields_set, crtr_model);
        parse_named_field_block(buf, len, fname, flags, "appearance", creaturemodel_appearance_named_fields, &creaturemodel_named_fields_set, crtr_model);
        parse_named_field_block(buf, len, fname, flags, "experience", creaturemodel_experience_named_fields, &creaturemodel_named_fields_set, crtr_model);
        parse_named_field_block(buf, len, fname, flags, "jobs", creaturemodel_jobs_named_fields, &creaturemodel_named_fields_set, crtr_model);
        parse_named_field_block(buf, len, fname, flags, "sprites", creaturemodel_sprites_named_fields, &creature_graphics_named_fields_set, crtr_model);
        parse_named_field_block(buf, len, fname, flags, "sounds", creaturemodel_sounds_named_fields, &creature_sounds_named_fields_set, crtr_model);
    }
    // Freeing and exiting
    KfxFree(buf);
    return result;
}

/* @comment
 *     The loading items of load_creaturemodel_config and load_creaturemodel_config_for_mod need to be consistent.
 */
static TbBool load_creaturemodel_config_for_mod(ThingModel crmodel, int64_t flags, const char *conf_fnstr, const struct ModConfigItem *mod_item)
{
    set_flag(flags, CnfLd_IgnoreErrors);

    TbBool result = false;
    const struct ModExistState *mod_state = &mod_item->state;
    char* fname = NULL;
    char mod_dir[256] = {0};
    sprintf(mod_dir, "%s/%s", MODS_DIR_NAME, mod_item->name);

    if (mod_state->crtr_data)
    {
        fname = get_mod_file_path_fmt(mod_dir, FGrp_CrtrData, "%s.cfg", conf_fnstr);
        if (fname && strlen(fname) > 0)
        {
            result |= load_creaturemodel_config_file(crmodel, fname, flags);
        }
    }

    if (mod_state->cmpg_crtrs)
    {
        fname = get_mod_file_path_fmt(mod_dir, FGrp_CmpgCrtrs,"%s.cfg",conf_fnstr);
        if (fname && strlen(fname) > 0)
        {
            result |= load_creaturemodel_config_file(crmodel, fname, flags);
        }
    }

    if (mod_state->cmpg_lvls)
    {
        fname = get_mod_file_path_fmt(mod_dir, FGrp_CmpgLvls, "map%05" PRId64 ".%s.cfg", (int64_t)(config_level_number()), conf_fnstr);
        if (fname && strlen(fname) > 0)
        {
            result |= load_creaturemodel_config_file(crmodel, fname, flags);
        }
    }

    return result;
}

static TbBool load_creaturemodel_config_for_mod_list(ThingModel crmodel, int64_t flags, const char *conf_fnstr, const struct ModConfigItem *mod_items, int64_t mod_cnt)
{
    TbBool result = false;

    for (int64_t i=0; i<mod_cnt; i++)
    {
        const struct ModConfigItem *mod_item = mod_items + i;
        if (mod_item->state.mod_dir == 0)
            continue;

        result |= load_creaturemodel_config_for_mod(crmodel, flags, conf_fnstr, mod_item);
    }

    return result;
}

/* @function description
 *     Load model configuration for a creature.
 *     Splitting ThingModel into conf_crmodel and crmodel, So specific/different configuration can be loaded for crmodel.
 * @comment
 *     The loading items of load_creaturemodel_config and load_creaturemodel_config_for_mod need to be consistent.
 */
// Names the file for the compat report while it's parsed (the legacy command
// parser doesn't know which file it's in).
TbBool load_creaturemodel_config_file(int64_t crtr_model, const char *fname, int64_t flags)
{
    compat_report_set_source(fname);
    const TbBool result = load_creaturemodel_config_file_impl(crtr_model, fname, flags);
    compat_report_set_source(NULL);
    return result;
}

TbBool load_creaturemodel_config(ThingModel conf_crmodel, ThingModel crmodel, int64_t flags)
{
    if ((flags & CnfLd_AcceptPartial) == 0)
    {
        init_creature_model_stats(crmodel);
    }
    set_flag(flags, CnfLd_AcceptPartial);

    char conf_fnstr[COMMAND_WORD_LEN];
    snprintf(conf_fnstr, COMMAND_WORD_LEN, "%s", get_conf_parameter_text(creature_desc, conf_crmodel));
    strtolower(conf_fnstr);
    if (strlen(conf_fnstr) == 0)
    {
        WARNMSG("Cannot get config file name[%" PRId64 "] for creature[%" PRId64 "].", (int64_t)(conf_crmodel), (int64_t)(crmodel));
        return false;
    }

    char* fname = get_game_file_path_fmt(FGrp_CrtrData, "%s.cfg", conf_fnstr);
    TbBool result = (fname != NULL && load_creaturemodel_config_file(crmodel, fname, flags));
    if (result)
    {
        set_flag(flags, CnfLd_IgnoreErrors);
    }

    if (mods_conf.after_base_cnt > 0)
    {
        result |= load_creaturemodel_config_for_mod_list(crmodel, flags, conf_fnstr, mods_conf.after_base_item, mods_conf.after_base_cnt);
        if (result)
        {
            set_flag(flags, CnfLd_IgnoreErrors);
        }
    }

    fname = get_game_file_path_fmt(FGrp_CmpgCrtrs,"%s.cfg",conf_fnstr);
    if (fname && strlen(fname) > 0)
    {
        result |= load_creaturemodel_config_file(crmodel, fname, flags);
        if (result)
        {
            set_flag(flags, CnfLd_IgnoreErrors);
        }
    }

    if (mods_conf.after_campaign_cnt > 0)
    {
        result |= load_creaturemodel_config_for_mod_list(crmodel, flags, conf_fnstr, mods_conf.after_campaign_item, mods_conf.after_campaign_cnt);
        if (result)
        {
            set_flag(flags, CnfLd_IgnoreErrors);
        }
    }

    fname = get_game_file_path_fmt(FGrp_CmpgLvls, "map%05" PRId64 ".%s.cfg", (int64_t)(config_level_number()), conf_fnstr);
    if (fname && strlen(fname) > 0)
    {
        result |= load_creaturemodel_config_file(crmodel, fname, flags);
        if (result)
        {
            set_flag(flags, CnfLd_IgnoreErrors);
        }
    }

    if (mods_conf.after_map_cnt > 0)
    {
        result |= load_creaturemodel_config_for_mod_list(crmodel, flags, conf_fnstr, mods_conf.after_map_item, mods_conf.after_map_cnt);
        // last one does not need to set CnfLd_IgnoreErrors
    }

    if (!result)
    {
        ERRORLOG("Unable to load a complete model config file[%s] for creature[%s].", creature_code_name(conf_crmodel), creature_code_name(crmodel));
    }
    return result;
}

TbBool load_default_creaturemodel_config(ThingModel crmodel, int64_t flags)
{
    return load_creaturemodel_config(crmodel, crmodel, flags);
}

TbBool swap_creaturemodel_config(ThingModel nwcrmodel, ThingModel crmodel, int64_t flags)
{
    return load_creaturemodel_config(nwcrmodel, crmodel, flags);
}

static void do_creature_swap(ThingModel ncrt_id, ThingModel crtr_id)
{
    if (swap_creaturemodel_config(ncrt_id, crtr_id, 0))
    {
        SCRPTLOG("Swapped creature %s out for creature %s", creature_code_name(crtr_id), creature_code_name(ncrt_id));
    }
    else
    {
        ERRORLOG("Failed to swap creature %s out for creature %s", creature_code_name(crtr_id), creature_code_name(ncrt_id));
    }
}

TbBool swap_creature(ThingModel ncrt_id, ThingModel crtr_id)
{
    if ((crtr_id < 0) || (crtr_id >= kfx_config_state.conf.crtr_conf.model_count))
    {
        ERRORLOG("Creature index %" PRId64 " is invalid", (int64_t)(crtr_id));
        return false;
    }
    if ((ncrt_id < 0) || (ncrt_id >= kfx_config_state.conf.crtr_conf.model_count))
    {
        ERRORLOG("Creature index %" PRId64 " is invalid", (int64_t)(ncrt_id));
        return false;
    }
    struct CreatureModelConfig* crconf = creature_stats_get(crtr_id);
    ThingModel oldlair = crconf->lair_object;
    do_creature_swap(ncrt_id, crtr_id);
    struct CreatureModelConfig* ncrconf = creature_stats_get(crtr_id);
    ThingModel newlair = ncrconf->lair_object;
    for (PlayerNumber plyr_idx = 0; plyr_idx < PLAYERS_COUNT; plyr_idx++)
    {
        simport_do_to_players_all_creatures_of_model(plyr_idx, crtr_id, sim_port->update_relative_creature_health);
        simport_do_to_players_all_creatures_of_model(plyr_idx, crtr_id, sim_port->creature_increase_available_instances);
        simport_update_speed_of_player_creatures_of_model(plyr_idx, crtr_id);
        if (oldlair != newlair)
        {
            simport_do_to_players_all_creatures_of_model(plyr_idx, crtr_id, sim_port->remove_creature_lair);
        }
        simport_do_to_players_all_creatures_of_model(plyr_idx, crtr_id, sim_port->process_job_stress_and_going_postal);
    }

    simport_recalculate_all_creature_digger_lists();
    ui_update_creatr_model_activities_list(1);

    return true;
}

/**
 * Zeroes all the maintenance costs for all creatures.
 */
TbBool make_all_creatures_free(void)
{
    for (int64_t i = 0; i < kfx_config_state.conf.crtr_conf.model_count; i++)
    {
        struct CreatureModelConfig* crconf = creature_stats_get(i);
        crconf->training_cost = 0;
        crconf->scavenger_cost = 0;
        crconf->pay = 0;
    }
    return true;
}

/**
 * Changes max health of creatures, and updates all creatures to max.
 */
TbBool change_max_health_of_creature_kind(ThingModel crmodel, HitPoints new_max)
{
    struct CreatureModelConfig* crconf = creature_stats_get(crmodel);
    if (creature_stats_invalid(crconf)) {
        ERRORLOG("Invalid creature model %" PRId64,(int64_t)crmodel);
        return false;
    }
    SYNCDBG(3,"Changing all %s health from %" PRId64 " to %" PRId64 ".",creature_code_name(crmodel),(int64_t)crconf->health,(int64_t)new_max);
    crconf->health = saturate_set_signed(new_max, 16);
    int64_t n = simport_do_to_all_things_of_class_and_model(TCls_Creature, crmodel, sim_port->update_creature_health_to_max);
    return (n > 0);
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
