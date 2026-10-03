/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_creature.c
 *     Creature names, appearance and parameters configuration loading functions.
 * @par Purpose:
 *     Support of configuration files for creatures list.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     25 May 2009 - 03 Aug 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "config_creature.h"
#include "globals.h"

#include "bflib_basics.h"
#include "bflib_math.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"

#include "globals.h"
#include "config.h"
#include "config_terrain.h"
#include "config_strings.h"
#include "config_crtrstates.h"
#include "config_translation.h"
#include "kfx_config_state.h"
#include "config_funcnames.h"
#include "ports/render_port.h"
#include "post_inc.h"

// RoK_NONE from kfx_sim's room_data.h; bit-identical sentinel for "no room".
#define ROOM_KIND_NONE 0
// CrInst_NULL from kfx_sim's creature_instances.h; bit-identical sentinel
// for "no instance".
#define CREATURE_INSTANCE_NULL 0
// creature_instances_func_type and the other name tables used below are
// in config_funcnames.c.

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
static TbBool load_creaturetypes_config_file(const char *fname, int64_t flags);

const struct ConfigFileData keeper_creaturetp_file_data = {
    .filename = "creature.cfg",
    .load_func = load_creaturetypes_config_file,
    .pre_load_func = NULL,
    .post_load_func = NULL,
};

const struct NamedCommand creaturetype_common_commands[] = {
  {"CREATURES",              1},
  {"JOBSCOUNT",              2},
  {"ANGERJOBSCOUNT",         3},
  {"ATTACKPREFERENCESCOUNT", 4},
  {"SPRITESIZE",             5},
  {NULL,                     0},
  };

const struct NamedCommand creaturetype_experience_commands[] = {
  {"PAYINCREASEONEXP",             1},
  {"SPELLDAMAGEINCREASEONEXP",     2},
  {"RANGEINCREASEONEXP",           3},
  {"JOBVALUEINCREASEONEXP",        4},
  {"HEALTHINCREASEONEXP",          5},
  {"STRENGTHINCREASEONEXP",        6},
  {"DEXTERITYINCREASEONEXP",       7},
  {"DEFENSEINCREASEONEXP",         8},
  {"LOYALTYINCREASEONEXP",         9},
  {"ARMOURINCREASEONEXP",         10},
  {"SIZEINCREASEONEXP",           11},
  {"EXPFORHITTINGINCREASEONEXP",  12},
  {"TRAININGCOSTINCREASEONEXP",   13},
  {"SCAVENGINGCOSTINCREASEONEXP", 14},
  {NULL,                           0},
  };

const struct NamedCommand creaturetype_instance_commands[] = {
  {"Name",            1},
  {"Time",            2},
  {"ActionTime",      3},
  {"ResetTime",       4},
  {"FPTime",          5},
  {"FPActiontime",    6},
  {"FPResettime",     7},
  {"ForceVisibility", 8},
  {"TooltipTextID",   9},
  {"SymbolSprites",  10},
  {"Graphics",       11},
  {"Function",       12},
  {"RangeMin",       13},
  {"RangeMax",       14},
  {"Properties",     15},
  {"FpinstantCast",  16},
  {"PrimaryTarget",  17},
  {"ValidateSourceFunc",   18},
  {"ValidateTargetFunc",   19},
  {"SearchTargetsFunc",    20},
  {"PostalPriority",       21},
  {"NoAnimationLoop",      22},
  {"FPAllowSelfCastWhileFrozen",   23},
  {"FPAllowSelfCastWhenChicken",   24},
  {NULL,              0},
  };

const struct NamedCommand creaturetype_instance_properties[] = {
  {"REPEAT_TRIGGER",       InstPF_RepeatTrigger},
  {"RANGED_ATTACK",        InstPF_RangedAttack},
  {"MELEE_ATTACK",         InstPF_MeleeAttack},
  {"RANGED_DEBUFF",        InstPF_RangedDebuff},
  {"SELF_BUFF",            InstPF_SelfBuff},
  {"DANGEROUS",            InstPF_Dangerous},
  {"DESTRUCTIVE",          InstPF_Destructive},
  {"DISARMING",            InstPF_Disarming},
  {"DISPLAY_SWIPE",        InstPF_UsesSwipe},
  {"RANGED_BUFF",          InstPF_RangedBuff},
  {"NEEDS_TARGET",         InstPF_NeedsTarget},
  {NULL,                     0},
  };

const struct NamedCommand creaturetype_job_commands[] = {
  {"NAME",              1},
  {"RELATEDROOMROLE",   2},
  {"RELATEDEVENT",      3},
  {"ASSIGN",            4},
  {"INITIALSTATE",      5},
  {"CONTINUESTATE",     6},
  {"PLAYERFUNCTIONS",   7},
  {"COORDSFUNCTIONS",   8},
  {"PROPERTIES",        9},
  {NULL,                0},
  };

const struct NamedCommand creaturetype_job_assign[] = {
  {"HUMAN_DROP",             JoKF_AssignHumanDrop},
  {"COMPUTER_DROP",          JoKF_AssignComputerDrop},
  {"CREATURE_INIT",          JoKF_AssignCeatureInit},
  {"AREA_WITHIN_ROOM",       JoKF_AssignAreaWithinRoom},
  {"AREA_OUTSIDE_ROOM",      JoKF_AssignAreaOutsideRoom},
  {"BORDER_ONLY",            JoKF_AssignOnAreaBorder},
  {"CENTER_ONLY",            JoKF_AssignOnAreaCenter},
  {"WHOLE_AREA",             JoKF_AssignOnAreaBorder|JoKF_AssignOnAreaCenter},
  {"OWNED_CREATURES",        JoKF_OwnedCreatures},
  {"ENEMY_CREATURES",        JoKF_EnemyCreatures},
  {"OWNED_DIGGERS",          JoKF_OwnedDiggers},
  {"ENEMY_DIGGERS",          JoKF_EnemyDiggers},
  {"ONE_TIME",               JoKF_AssignOneTime},
  {"NEEDS_HAVE_JOB",         JoKF_NeedsHaveJob},
  {NULL,                     0},
  };

const struct NamedCommand creaturetype_job_properties[] = {
  {"WORK_BORDER_ONLY",       JoKF_WorkOnAreaBorder},
  {"WORK_CENTER_ONLY",       JoKF_WorkOnAreaCenter},
  {"WORK_WHOLE_AREA",        JoKF_WorkOnAreaBorder|JoKF_WorkOnAreaCenter},
  {"NEEDS_CAPACITY",         JoKF_NeedsCapacity},
  {"NO_SELF_CONTROL",        JoKF_NoSelfControl},
  {"NO_GROUPS",              JoKF_NoGroups},
  {"ALLOW_CHICKENIZED",      JoKF_AllowChickenized},
  {NULL,                     0},
  };

const struct NamedCommand creaturetype_angerjob_commands[] = {
  {"NAME",            1},
  {NULL,              0},
  };

const struct NamedCommand creaturetype_attackpref_commands[] = {
  {"NAME",            1},
  {NULL,              0},
  };

const struct NamedCommand creature_graphics_desc[] = {
  {"STAND",             1+CGI_Stand       },
  {"AMBULATE",          1+CGI_Ambulate    },
  {"DRAG",              1+CGI_Drag        },
  {"ATTACK",            1+CGI_Attack      },
  {"DIG",               1+CGI_Dig         },
  {"SMOKE",             1+CGI_Smoke       },
  {"RELAX",             1+CGI_Relax       },
  {"PRETTYDANCE",       1+CGI_PrettyDance },
  {"GOTHIT",            1+CGI_GotHit      },
  {"POWERGRAB",         1+CGI_PowerGrab   },
  {"GOTSLAPPED",        1+CGI_GotSlapped  },
  {"CELEBRATE",         1+CGI_Celebrate   },
  {"SLEEP",             1+CGI_Sleep       },
  {"EATCHICKEN",        1+CGI_EatChicken  },
  {"TORTURE",           1+CGI_Torture     },
  {"SCREAM",            1+CGI_Scream      },
  {"DROPDEAD",          1+CGI_DropDead    },
  {"DEADSPLAT",         1+CGI_DeadSplat   },
  {"ROAR",              1+CGI_Roar        }, // Was previously GFX18.
  {"QUERYSYMBOL",       1+CGI_QuerySymbol }, // Icon
  {"HANDSYMBOL",        1+CGI_HandSymbol  }, // Icon
  {"PISS",              1+CGI_Piss        }, // Was previously GFX21.
  {"CASTSPELL",         1+CGI_CastSpell   },
  {"RANGEDATTACK",      1+CGI_RangedAttack},
  {"CUSTOM",            1+CGI_Custom      },
  {NULL,                                 0},
  };

const struct NamedCommand instance_range_desc[] = {
  {"MAX",          INT_MAX},
  {"MIN",                0},
  {NULL,                -1},
};

const struct NamedCommand spawn_type_desc[] = {
  {"NONE",            SpwnT_None        },
  {"0",               SpwnT_None        },
  {"DEFAULT",         SpwnT_Default     },
  {"1",               SpwnT_Default     },
  {"JUMP",            SpwnT_Jump        },
  {"2",               SpwnT_Jump        },
  {"FALL",            SpwnT_Fall        },
  {"3",               SpwnT_Fall        },
  {"INIT",            SpwnT_Initialize  },
  {"INITIALIZE",      SpwnT_Initialize  },
  {"4",               SpwnT_Initialize  },
  {NULL,             -1                 },
};

/******************************************************************************/
struct NamedCommand creature_desc[CREATURE_TYPES_MAX];
struct NamedCommand instance_desc[INSTANCE_TYPES_MAX];
struct NamedCommand creaturejob_desc[INSTANCE_TYPES_MAX];
struct NamedCommand angerjob_desc[INSTANCE_TYPES_MAX];
struct NamedCommand attackpref_desc[INSTANCE_TYPES_MAX];

ThingModel breed_activities[CREATURE_TYPES_MAX];

// Moved from kfx_game's lvl_script_commands.c (stage 13.3) -- purely a
// creature_desc/CREATURE_NOT_A_DIGGER lookup, no real kfx_game
// dependency. See docs/refactor/stage-13-enforce-and-document.md.
ThingModel parse_creature_name(const char *creature_name)
{
    ThingModel ret = get_rid(creature_desc, creature_name);
    if (ret == -1)
    {
        if (0 == strcasecmp(creature_name, "ANY_CREATURE"))
        {
            return CREATURE_NOT_A_DIGGER; //For scripts, when we say 'ANY_CREATURE' we exclude diggers.
        }
    }
    return ret;
}

// Moved from kfx_sim's creature_graphics.c (stage 13.3) -- purely a
// kfx_config_state.conf.crtr_conf.creature_graphics[] write, no real
// kfx_sim dependency. See docs/refactor/stage-13-enforce-and-document.md.
void set_creature_model_graphics(int64_t crmodel, int64_t seq_idx, uint64_t val)
{
    if (seq_idx >= CREATURE_GRAPHICS_INSTANCES)
    {
        ERRORLOG("Invalid model %" PRId64 " graphics sequence %" PRIu64, (int64_t)(crmodel), (uint64_t)(seq_idx));
        return;
    }
    if ((crmodel < 0) || (crmodel >= kfx_config_state.conf.crtr_conf.model_count))
    {
        ERRORLOG("Invalid model %" PRId64 " graphics sequence %" PRIu64, (int64_t)(crmodel), (uint64_t)(seq_idx));
        return;
    }
    kfx_config_state.conf.crtr_conf.creature_graphics[crmodel][seq_idx] = val;
}
/******************************************************************************/
// creature_job_player_assign_func_type/creature_job_player_check_func_type/
// creature_job_coords_check_func_type/creature_job_coords_assign_func_type
// are in config_funcnames.c.

const struct NamedCommand mevents_desc[] = {
    {"MEVENT_NOTHING",         EvKind_Nothing},
    {"MEVENT_HEARTATTACKED",   EvKind_HeartAttacked},
    {"MEVENT_ENEMYFIGHT",      EvKind_EnemyFight},
    {"MEVENT_OBJECTIVE",       EvKind_Objective},
    {"MEVENT_BREACH",          EvKind_Breach},
    {"MEVENT_NEWROOMRESRCH",   EvKind_NewRoomResrch},
    {"MEVENT_NEWCREATURE",     EvKind_NewCreature},
    {"MEVENT_NEWSPELLRESRCH",  EvKind_NewSpellResrch},
    {"MEVENT_NEWTRAP",         EvKind_NewTrap},
    {"MEVENT_NEWDOOR",         EvKind_NewDoor},
    {"MEVENT_CREATRSCAVENGED", EvKind_CreatrScavenged},
    {"MEVENT_TREASUREROOMFULL",EvKind_TreasureRoomFull},
    {"MEVENT_CREATUREPAYDAY",  EvKind_CreaturePayday},
    {"MEVENT_AREADISCOVERED",  EvKind_AreaDiscovered},
    {"MEVENT_SPELLPICKEDUP",   EvKind_SpellPickedUp},
    {"MEVENT_ROOMTAKENOVER",   EvKind_RoomTakenOver},
    {"MEVENT_CREATRISANNOYED", EvKind_CreatrIsAnnoyed},
    {"MEVENT_NOMORELIVINGSET", EvKind_NoMoreLivingSet},
    {"MEVENT_ALARMTRIGGERED",  EvKind_AlarmTriggered},
    {"MEVENT_ROOMUNDERATTACK", EvKind_RoomUnderAttack},
    {"MEVENT_NEEDTREASUREROOM",EvKind_NeedTreasureRoom},
    {"MEVENT_INFORMATION",     EvKind_Information},
    {"MEVENT_ROOMLOST",        EvKind_RoomLost},
    {"MEVENT_CREATRHUNGRY",    EvKind_CreatrHungry},
    {"MEVENT_TRAPCRATEFOUND",  EvKind_TrapCrateFound},
    {"MEVENT_DOORCRATEFOUND",  EvKind_DoorCrateFound},
    {"MEVENT_DNSPECIALFOUND",  EvKind_DnSpecialFound},
    {"MEVENT_QUICKINFORMATION",EvKind_QuickInformation},
    {"MEVENT_FRIENDLYFIGHT",   EvKind_FriendlyFight},
    {"MEVENT_WORKROMUNREACHBL",EvKind_WorkRoomUnreachable},
    {"MEVENT_STRGROMUNREACHBL",EvKind_StorageRoomUnreachable},
    {NULL,                    0},
};

/******************************************************************************/
/**
 * Returns CreatureModelConfig of given creature model.
 */
struct CreatureModelConfig *creature_stats_get(ThingModel crconf_idx)
{
  if ((crconf_idx < 1) || (crconf_idx >= CREATURE_TYPES_MAX))
    return &kfx_config_state.conf.crtr_conf.model[0];
  return &kfx_config_state.conf.crtr_conf.model[crconf_idx];
}

/**
 * Returns if given CreatureModelConfig pointer is incorrect.
 */
TbBool creature_stats_invalid(const struct CreatureModelConfig *crconf)
{
  return (crconf <= &kfx_config_state.conf.crtr_conf.model[0]) || (crconf == NULL);
}

void check_and_auto_fix_stats(void)
{
    SYNCDBG(8,"Starting for %" PRId64 " models",(int64_t)kfx_config_state.conf.crtr_conf.model_count);
    for (int64_t model = 0; model < kfx_config_state.conf.crtr_conf.model_count; model++)
    {
        struct CreatureModelConfig* crconf = creature_stats_get(model);
        if ( (crconf->lair_size <= 0) && (crconf->toking_recovery <= 0) && (crconf->heal_requirement != 0) )
        {
            ERRORLOG("Creature model %" PRId64 " (%s) has no LairSize and no TokingRecovery but has HealRequirment - Fixing", (int64_t)model, creature_code_name(model));
            crconf->heal_requirement = 0;
        }
        if (crconf->heal_requirement > crconf->heal_threshold)
        {
            ERRORLOG("Creature model %" PRId64 " (%s) Heal Requirment > Heal Threshold - Fixing", (int64_t)model, creature_code_name(model));
            crconf->heal_threshold = crconf->heal_requirement;
        }
        if ( (crconf->hunger_rate != 0) && (crconf->hunger_fill == 0) )
        {
            ERRORLOG("Creature model %" PRId64 " (%s) HungerRate > 0 & Hunger Fill = 0 - Fixing", (int64_t)model, creature_code_name(model));
            crconf->hunger_fill = 1;
        }
        if ((crconf->grow_up >= kfx_config_state.conf.crtr_conf.model_count) && !(crconf->grow_up == CREATURE_NOT_A_DIGGER))
        {
            ERRORLOG("Creature model %" PRId64 " (%s) Invalid GrowUp model - Fixing", (int64_t)model, creature_code_name(model));
            crconf->grow_up = 0;
        }
        if (crconf->grow_up > 0)
        {
            if (crconf->grow_up_level > CREATURE_MAX_LEVEL)
            {
                ERRORLOG("Creature model %" PRId64 " (%s) GrowUp & GrowUpLevel invalid - Fixing", (int64_t)model, creature_code_name(model));
                crconf->grow_up_level = CREATURE_MAX_LEVEL;
            }
        }
        if (crconf->rebirth > CREATURE_MAX_LEVEL)
        {
            ERRORLOG("Creature model %" PRId64 " (%s) Rebirth Invalid - Fixing", (int64_t)model, creature_code_name(model));
            crconf->rebirth = 0;
        }
        for (int64_t i = 0; i < LEARNED_INSTANCES_COUNT; i++)
        {
            int64_t n = crconf->learned_instance_level[i];
            if (crconf->learned_instance_id[i] != CREATURE_INSTANCE_NULL)
            {
                if ((n < 1) || (n > CREATURE_MAX_LEVEL))
                {
                    ERRORLOG("Creature model %" PRId64 " (%s) Learn Level for Instance slot %" PRId64 " Invalid - Fixing", (int64_t)model, creature_code_name(model), (int64_t)(i+1));
                    crconf->learned_instance_level[i] = 1;
                }
            } else
            {
                if (n != 0)
                {
                    ERRORLOG("Creature model %" PRId64 " (%s) Learn Level for Empty Instance slot %" PRId64 " - Fixing", (int64_t)model, creature_code_name(model), (int64_t)(i+1));
                    crconf->learned_instance_level[i] = 0;
                }
            }
        }
    }
    SYNCDBG(9,"Finished");
}


void init_creature_model_stats(ThingModel crmodel)
{
    int64_t n;
    struct CreatureModelConfig *crconf = creature_stats_get(crmodel);
    // Attributes block.
    crconf->health = 100;
    crconf->heal_requirement = 1;
    crconf->heal_threshold = 1;
    crconf->strength = 1;
    crconf->armour = 0;
    crconf->dexterity = 0;
    crconf->fear_wounded = 12;
    crconf->fear_stronger = 65000;
    crconf->fearsome_factor = 100;
    crconf->defense = 0;
    crconf->luck = 0;
    crconf->sleep_recovery = 1;
    crconf->toking_recovery = 0;
    crconf->hunger_rate = 1;
    crconf->hunger_fill = 1;
    crconf->lair_size = 1;
    crconf->hurt_by_lava = 1;
    crconf->base_speed = 32;
    crconf->gold_hold = 100;
    crconf->size_xy = 1;
    crconf->size_z = 1;
    crconf->attack_preference = 0;
    crconf->pay = 1;
    crconf->slaps_to_kill = 10;
    crconf->damage_to_boulder = 4;
    crconf->thing_size_xy = 128;
    crconf->thing_size_z = 64;
    crconf->bleeds = true;
    crconf->humanoid_creature = true;
    crconf->piss_on_dead = false;
    crconf->flying = false;
    crconf->can_see_invisible = false;
    crconf->can_go_locked_doors = false;
    crconf->prison_kind = 0;
    crconf->torture_kind = 0;
    crconf->immunity_flags = 0;
    for (n = 0; n < CREATURE_TYPES_MAX; n++)
    {
        crconf->hostile_towards[n] = 0;
    }
    crconf->namestr_idx = 0;
    crconf->model_flags = 0;
    // Attraction block.
    for (n = 0; n < ENTRANCE_ROOMS_COUNT; n++)
    {
        crconf->entrance_rooms[n] = 0;
        crconf->entrance_slabs_req[n] = 0;
    }
    crconf->entrance_score = 10;
    crconf->scavenge_require = 1;
    crconf->torture_break_time = 1;
    // Annoyance block.
    for (n = 0; n < LAIR_ENEMY_MAX; n++)
    {
        crconf->lair_enemy[n] = 0;
    }
    crconf->annoy_eat_food = 0;
    crconf->annoy_will_not_do_job = 0;
    crconf->annoy_in_hand = 0;
    crconf->annoy_no_lair = 0;
    crconf->annoy_no_hatchery = 0;
    crconf->annoy_woken_up = 0;
    crconf->annoy_on_dead_enemy = 0;
    crconf->annoy_sulking = 0;
    crconf->annoy_no_salary = 0;
    crconf->annoy_slapped = 0;
    crconf->annoy_on_dead_friend = 0;
    crconf->annoy_in_torture = 0;
    crconf->annoy_in_temple = 0;
    crconf->annoy_sleeping = 0;
    crconf->annoy_got_wage = 0;
    crconf->annoy_win_battle = 0;
    crconf->annoy_untrained_time = 0;
    crconf->annoy_untrained = 0;
    crconf->annoy_others_leaving = 0;
    crconf->annoy_job_stress = 0;
    crconf->annoy_going_postal = 0;
    crconf->annoy_queue = 0;
    crconf->annoy_level = 0;
    crconf->jobs_anger = 0;
    // Senses block.
    crconf->hearing = 12;
    crconf->base_eye_height = 256;
    crconf->field_of_view = 1024;
    crconf->eye_effect = 0;
    crconf->max_turning_speed = 15;
    // Appearance block.
    crconf->walking_anim_speed = 32;
    crconf->fixed_anim_speed = false;
    crconf->visual_range = 18;
    crconf->swipe_idx = 0;
    crconf->natural_death_kind = Death_Normal;
    crconf->shot_shift_x = 0;
    crconf->shot_shift_y = 0;
    crconf->shot_shift_z = 0;
    crconf->footstep_pitch = 100;
    crconf->corpse_vanish_effect = 0;
    crconf->status_offset = 32;
    // Experience block.
    for (n = 0; n < LEARNED_INSTANCES_COUNT; n++)
    {
        crconf->learned_instance_id[n] = 0;
        crconf->learned_instance_level[n] = 0;
    }
    for (n = 0; n < CREATURE_MAX_LEVEL; n++)
    {
        crconf->to_level[n] = 0;
    }
    crconf->grow_up = 0;
    crconf->grow_up_level = 0;
    for (n = 0; n < SLEEP_XP_COUNT; n++)
    {
        crconf->sleep_exp_slab[n] = 0;
        crconf->sleep_experience[n] = 0;
    }
    crconf->exp_for_hitting = 0;
    crconf->rebirth = 0;
    // Jobs block.
    crconf->job_primary = 0;
    crconf->job_secondary = 0;
    crconf->jobs_not_do = 0;
    crconf->job_stress = 0;
    crconf->training_value = 0;
    crconf->training_cost = 0;
    crconf->scavenge_value = 0;
    crconf->scavenger_cost = 0;
    crconf->research_value = 0;
    crconf->manufacture_value = 0;
    crconf->partner_training = 0;
}

/* Initialize all creature model stats, called only once when first loading a map. */
void init_all_creature_model_stats(void)
{
    for (int64_t i = 0; i < CREATURE_TYPES_MAX; i++)
    {
        init_creature_model_stats(i);
    }
}

void init_creature_model_graphics(void)
{
    for (int64_t i = 0; i < CREATURE_TYPES_MAX; i++)
    {
        for (int64_t k = 0; k < CREATURE_GRAPHICS_INSTANCES; k++)
        {
            kfx_config_state.conf.crtr_conf.creature_graphics[i][k] = -1;
        }
    }
}

TbBool is_creature_model_wildcard(ThingModel crmodel)
{
    if((crmodel == CREATURE_ANY) || (crmodel == CREATURE_NOT_A_DIGGER) || (crmodel == CREATURE_DIGGER))
    {
        return true;
    }
    return false;
}

/**
 * Returns Code Name (name to use in script file) of given creature model.
 */
const char *creature_code_name(ThingModel crmodel)
{
    const char* name = get_conf_parameter_text(creature_desc, crmodel);
    if (name[0] != '\0')
        return name;
    return "INVALID";
}

/**
 * Returns the creature associated with a given model name.
 * Linear lookup time so don't use in tight loop.
 * @param name
 * @return
 */
int64_t creature_model_id(const char * name)
{
    for (int64_t i = 0; i < kfx_config_state.conf.crtr_conf.model_count; ++i)
    {
        if (strncmp(name, kfx_config_state.conf.crtr_conf.model[i].name, COMMAND_WORD_LEN) == 0) {
            return i;
        }
    }

    return -1;
}

TbBool parse_creaturetypes_common_blocks(char *buf, int64_t len, const char *config_textname, int64_t flags)
{
    // Initialize block data
    if ((flags & CnfLd_AcceptPartial) == 0)
    {
        kfx_config_state.conf.crtr_conf.model_count = 1;
        kfx_config_state.conf.crtr_conf.jobs_count = 1;
        kfx_config_state.conf.crtr_conf.angerjobs_count = 1;
        kfx_config_state.conf.crtr_conf.attacktypes_count = 1;
        kfx_config_state.conf.crtr_conf.special_digger_good = 0;
        kfx_config_state.conf.crtr_conf.special_digger_evil = 0;
        kfx_config_state.conf.crtr_conf.spectator_breed = 0;
        kfx_config_state.conf.crtr_conf.sprite_size = 300;
        for (int64_t i = 0; i < CREATURE_TYPES_MAX; i++)
        {
          memset(kfx_config_state.conf.crtr_conf.model[i].name, 0, COMMAND_WORD_LEN);
          creature_desc[i].name = NULL;
          creature_desc[i].num = 0;
        }
    }
    creature_desc[CREATURE_TYPES_MAX - 1].name = NULL; // must be null for get_id
    snprintf(kfx_config_state.conf.crtr_conf.model[0].name, COMMAND_WORD_LEN, "%s", "NOCREATURE");
    // Find the block
    const char * block_name = "common";
    int64_t pos = 0;
    int64_t k = find_conf_block(buf, &pos, len, block_name);
    if (k < 0)
    {
        if ((flags & CnfLd_AcceptPartial) == 0)
            WARNMSG("Block [%s] not found in %s file.", block_name, config_textname);
        return false;
    }
#define COMMAND_TEXT(cmd_num) get_conf_parameter_text(creaturetype_common_commands,cmd_num)
    while (pos<len)
    {
        // Finding command number in this line
        int64_t cmd_num = recognize_conf_command(buf, &pos, len, creaturetype_common_commands);
        // Now store the config item in correct place
        if (cmd_num == ccr_endOfBlock) break; // if next block starts
        int64_t n = 0;
        char word_buf[COMMAND_WORD_LEN];
        switch (cmd_num)
        {
        case 1: // CREATURES
            kfx_config_state.conf.crtr_conf.model_count = 1;
            if ((flags & CnfLd_AcceptPartial) != 0) {
                for (int64_t i = 1; i < CREATURE_TYPES_MAX; i++) {
                    memset(kfx_config_state.conf.crtr_conf.model[i].name, 0, COMMAND_WORD_LEN);
                    creature_desc[i - 1].name = NULL;
                    creature_desc[i - 1].num = 0;
                }
            }
            while (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0) {
              n = kfx_config_state.conf.crtr_conf.model_count;
              if (n >= CREATURE_TYPES_MAX) {
                  CONFWRNLOG("Too many species defined with \"%s\" in [%s] block of %s file.",
                      COMMAND_TEXT(cmd_num), block_name, config_textname);
                  break;
              }
              snprintf(kfx_config_state.conf.crtr_conf.model[n].name, COMMAND_WORD_LEN, "%s", word_buf);
              creature_desc[n - 1].name = kfx_config_state.conf.crtr_conf.model[n].name;
              creature_desc[n - 1].num = n;
              kfx_config_state.conf.crtr_conf.model_count++;
            }
            break;
        case 2: // JOBSCOUNT
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              if ((k > 0) && (k <= INSTANCE_TYPES_MAX))
              {
                kfx_config_state.conf.crtr_conf.jobs_count = k;
                n++;
              }
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 3: // ANGERJOBSCOUNT
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              if ((k > 0) && (k <= INSTANCE_TYPES_MAX))
              {
                kfx_config_state.conf.crtr_conf.angerjobs_count = k;
                n++;
              }
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 4: // ATTACKPREFERENCESCOUNT
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              if ((k > 0) && (k <= INSTANCE_TYPES_MAX))
              {
                kfx_config_state.conf.crtr_conf.attacktypes_count = k;
                n++;
              }
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 5: // SPRITESIZE
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                if ((k > 0) && (k <= 1024))
                {
                    kfx_config_state.conf.crtr_conf.sprite_size = k;
                    n++;
                }
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case ccr_comment:
            break;
        case ccr_endOfFile:
            break;
        default:
            CONFWRNLOG("Unrecognized command (%" PRId64 ") in [%s] block of %s file.",
                (int64_t)(cmd_num), block_name, config_textname);
            break;
        }
        skip_conf_to_next_line(buf,&pos,len);
    }
#undef COMMAND_TEXT
    if (kfx_config_state.conf.crtr_conf.model_count < 1)
    {
        WARNLOG("No creature species defined in [%s] block of %s file.",
            block_name, config_textname);
    }
    return true;
}

TbBool parse_creaturetype_experience_blocks(char *buf, int64_t len, const char *config_textname, int64_t flags)
{
    // Initialize block data
    if ((flags & CnfLd_AcceptPartial) == 0)
    {
        kfx_config_state.conf.crtr_conf.exp.size_increase_on_exp = 0;
        kfx_config_state.conf.crtr_conf.exp.pay_increase_on_exp = CREATURE_PROPERTY_INCREASE_ON_EXP;
        kfx_config_state.conf.crtr_conf.exp.spell_damage_increase_on_exp = CREATURE_PROPERTY_INCREASE_ON_EXP;
        kfx_config_state.conf.crtr_conf.exp.range_increase_on_exp = CREATURE_PROPERTY_INCREASE_ON_EXP;
        kfx_config_state.conf.crtr_conf.exp.job_value_increase_on_exp = CREATURE_PROPERTY_INCREASE_ON_EXP;
        kfx_config_state.conf.crtr_conf.exp.health_increase_on_exp = CREATURE_PROPERTY_INCREASE_ON_EXP;
        kfx_config_state.conf.crtr_conf.exp.strength_increase_on_exp = CREATURE_PROPERTY_INCREASE_ON_EXP;
        kfx_config_state.conf.crtr_conf.exp.dexterity_increase_on_exp = CREATURE_PROPERTY_INCREASE_ON_EXP;
        kfx_config_state.conf.crtr_conf.exp.defense_increase_on_exp = CREATURE_PROPERTY_INCREASE_ON_EXP;
        kfx_config_state.conf.crtr_conf.exp.loyalty_increase_on_exp = CREATURE_PROPERTY_INCREASE_ON_EXP;
        kfx_config_state.conf.crtr_conf.exp.exp_on_hitting_increase_on_exp = CREATURE_PROPERTY_INCREASE_ON_EXP;
        kfx_config_state.conf.crtr_conf.exp.armour_increase_on_exp = 0;
        kfx_config_state.conf.crtr_conf.exp.training_cost_increase_on_exp = 0;
        kfx_config_state.conf.crtr_conf.exp.scavenging_cost_increase_on_exp = 0;
    }
    // Find the block
    const char * block_name = "experience";
    int64_t pos = 0;
    int64_t k = find_conf_block(buf, &pos, len, block_name);
    if (k < 0)
    {
        if ((flags & CnfLd_AcceptPartial) == 0)
            WARNMSG("Block [%s] not found in %s file.", block_name, config_textname);
        return false;
    }
#define COMMAND_TEXT(cmd_num) get_conf_parameter_text(creaturetype_experience_commands,cmd_num)
    while (pos<len)
    {
        // Finding command number in this line
        int64_t cmd_num = recognize_conf_command(buf, &pos, len, creaturetype_experience_commands);
        // Now store the config item in correct place
        if (cmd_num == ccr_endOfBlock) break; // if next block starts
        int64_t n = 0;
        char word_buf[COMMAND_WORD_LEN];
        switch (cmd_num)
        {
        case 1: // PAYINCREASEONEXP
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                kfx_config_state.conf.crtr_conf.exp.pay_increase_on_exp = k;
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 2: // SPELLDAMAGEINCREASEONEXP
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                kfx_config_state.conf.crtr_conf.exp.spell_damage_increase_on_exp = k;
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 3: // RANGEINCREASEONEXP
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                kfx_config_state.conf.crtr_conf.exp.range_increase_on_exp = k;
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 4: // JOBVALUEINCREASEONEXP
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                kfx_config_state.conf.crtr_conf.exp.job_value_increase_on_exp = k;
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 5: // HEALTHINCREASEONEXP
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                kfx_config_state.conf.crtr_conf.exp.health_increase_on_exp = k;
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 6: // STRENGTHINCREASEONEXP
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                kfx_config_state.conf.crtr_conf.exp.strength_increase_on_exp = k;
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 7: // DEXTERITYINCREASEONEXP
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                kfx_config_state.conf.crtr_conf.exp.dexterity_increase_on_exp = k;
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 8: // DEFENSEINCREASEONEXP
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                kfx_config_state.conf.crtr_conf.exp.defense_increase_on_exp = k;
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 9: // LOYALTYINCREASEONEXP
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                kfx_config_state.conf.crtr_conf.exp.loyalty_increase_on_exp = k;
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 10: // ARMOURINCREASEONEXP
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                kfx_config_state.conf.crtr_conf.exp.armour_increase_on_exp = k;
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 11: // SIZEINCREASEONEXP
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                kfx_config_state.conf.crtr_conf.exp.size_increase_on_exp = k;
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 12: // EXPFORHITTINGINCREASEONEXP
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                kfx_config_state.conf.crtr_conf.exp.exp_on_hitting_increase_on_exp = k;
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 13: // TRAININGCOSTINCREASEONEXP
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                kfx_config_state.conf.crtr_conf.exp.training_cost_increase_on_exp = k;
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 14: // SCAVENGINGCOSTINCREASEONEXP
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                kfx_config_state.conf.crtr_conf.exp.scavenging_cost_increase_on_exp = k;
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case ccr_comment:
            break;
        case ccr_endOfFile:
            break;
        default:
            CONFWRNLOG("Unrecognized command (%" PRId64 ") in [%s] block of %s file.",
                (int64_t)(cmd_num), block_name, config_textname);
            break;
        }
        skip_conf_to_next_line(buf,&pos,len);
    }
#undef COMMAND_TEXT
    if (kfx_config_state.conf.crtr_conf.model_count < 1)
    {
        WARNLOG("No creature species defined in [%s] block of %s file.",
            block_name, config_textname);
    }
    return true;
}

TbBool parse_creaturetype_instance_blocks(char *buf, int64_t len, const char *config_textname, int64_t flags)
{
    struct CreatureInstanceConfig * inst_cfg;
    struct InstanceInfo* inst_inf;
    int64_t k = 0;
    // Initialize the array
    for (int64_t i = 0; i < INSTANCE_TYPES_MAX; i++) {
        inst_cfg = &kfx_config_state.conf.crtr_conf.instances[i];
        if (((flags & CnfLd_AcceptPartial) == 0) || (strlen(inst_cfg->name) <= 0)) {
            memset(inst_cfg->name, 0, COMMAND_WORD_LEN);
            instance_desc[i].name = inst_cfg->name;
            instance_desc[i].num = i;
            inst_inf = &kfx_config_state.conf.magic_conf.instance_info[i];
            inst_inf->instant = 0;
            inst_inf->time = 0;
            inst_inf->fp_time = 0;
            inst_inf->action_time = 0;
            inst_inf->fp_action_time = 0;
            inst_inf->reset_time = 0;
            inst_inf->fp_reset_time = 0;
            inst_inf->graphics_idx = 0;
            inst_inf->instance_property_flags = 0;
            inst_inf->force_visibility = 0;
            inst_inf->primary_target = 0;
            inst_inf->func_idx = 0;
            inst_inf->func_params[0] = 0;
            inst_inf->func_params[1] = 0;
            inst_inf->symbol_spridx = 0;
            inst_inf->tooltip_stridx = 0;
            inst_inf->range_min = -1;
            inst_inf->range_max = -1;
            inst_inf->validate_source_func = 0;
            inst_inf->validate_source_func_params[0] = 0;
            inst_inf->validate_source_func_params[1] = 0;
            inst_inf->validate_target_func = 0;
            inst_inf->validate_target_func_params[0] = 0;
            inst_inf->validate_target_func_params[1] = 0;
            inst_inf->postal_priority = 0;
            inst_inf->fp_allow_self_cast_while_frozen = 0;
            inst_inf->fp_allow_self_cast_when_chicken = 0;
        }
    }
    instance_desc[INSTANCE_TYPES_MAX - 1].name = NULL; // must be null for get_id
    // Load the file blocks
    const char * blockname = NULL;
    int64_t blocknamelen = 0;
    int64_t pos = 0;
    while (iterate_conf_blocks(buf, &pos, len, &blockname, &blocknamelen))
    {
        // look for blocks starting with "instance", followed by one or more digits
        if (blocknamelen < 9) {
            continue;
        } else if (memcmp(blockname, "instance", 8) != 0) {
            continue;
        }
        const int64_t i = natoi(&blockname[8], blocknamelen - 8);
        if (i < 0 || i >= INSTANCE_TYPES_MAX) {
            continue;
        } else if (i >= kfx_config_state.conf.crtr_conf.instances_count) {
            kfx_config_state.conf.crtr_conf.instances_count = i + 1;
        }
        inst_inf = &kfx_config_state.conf.magic_conf.instance_info[i];
        inst_cfg = &kfx_config_state.conf.crtr_conf.instances[i];
#define COMMAND_TEXT(cmd_num) get_conf_parameter_text(creaturetype_instance_commands,cmd_num)
      while (pos<len)
      {
        // Finding command number in this line
        int64_t cmd_num = recognize_conf_command(buf, &pos, len, creaturetype_instance_commands);
        // Now store the config item in correct place
        if (cmd_num == ccr_endOfBlock) break; // if next block starts
        if ((flags & CnfLd_ListOnly) != 0) {
            // In "List only" mode, accept only name command
            if (cmd_num > 1) {
                cmd_num = 0;
            }
        }
        int64_t n = 0;
        char word_buf[COMMAND_WORD_LEN];
        switch (cmd_num)
        {
        case 1: // NAME
            if (get_conf_parameter_single(buf, &pos, len, inst_cfg->name, COMMAND_WORD_LEN) <= 0)
            {
                CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.", COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
                break;
            }
            n++;
            break;
        case 2: // TIME
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              inst_inf->time = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                    COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
            }
            break;
        case 3: // ACTIONTIME
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              inst_inf->action_time = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                    COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
            }
            break;
        case 4: // RESETTIME
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              inst_inf->reset_time = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                    COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
            }
            break;
        case 5: // FPTIME
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              inst_inf->fp_time = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                    COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
            }
            break;
        case 6: // FPACTIONTIME
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              inst_inf->fp_action_time = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                    COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
            }
            break;
        case 7: // FPRESETTIME
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              inst_inf->fp_reset_time = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                    COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
            }
            break;
        case 8: // FORCEVISIBILITY
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              inst_inf->force_visibility = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                    COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
            }
            break;
        case 9: // TOOLTIPTEXTID
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = get_string_id_by_alias(word_buf);
              if (k > 0)
              {
                  inst_inf->tooltip_stridx = k;
                  n++;
              }
            }
            if (n < 1)
            {
                CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                    COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
            }
            break;
        case 10: // SYMBOLSPRITES
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = render_get_icon_id(word_buf);
              if (k >= 0)
              {
                  inst_inf->symbol_spridx = k;
                  n++;
              }
            }
            if (n < 1)
            {
                CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                    COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
            }
            break;
        case 11: // GRAPHICS
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = get_id(creature_graphics_desc, word_buf);
                if (k > 0)
                {
                    inst_inf->graphics_idx = k-1;
                    n++;
                }
            }
            if (n < 1)
            {
                CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                    COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
            }
            break;
        case 12: // FUNCTION
            k = recognize_conf_parameter(buf,&pos,len,creature_instances_func_type);
            if (k > 0)
            {
                inst_inf->func_idx = k;
                n++;
                //JUSTLOG("Function = %s %s %d",creature_instances_func_type[k-1].name,spell_code_name(inst_inf->func_params[0]),(int64_t)(inst_inf->func_params[1]));
            }
            // Second parameter may be a different thing based on first parameter
            switch (k)
            {
            case 2: // Special code for casting spell instances
                k = recognize_conf_parameter(buf,&pos,len,spell_desc);
                if (k > 0)
                {
                    inst_inf->func_params[0] = k;
                    n++;
                }
                break;
            case 3: // Special code for firing shot instances
                k = recognize_conf_parameter(buf,&pos,len,shot_desc);
                if (k > 0)
                {
                    inst_inf->func_params[0] = k;
                    n++;
                }
                break;
            default:
                if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
                {
                    k = atoi(word_buf);
                    inst_inf->func_params[0] = k;
                    n++;
                }
            }
            // Third parameter is always integer
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                inst_inf->func_params[1] = k;
                n++;
            }
            if (n < 3)
            {
                CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                    COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
            }
            break;
        case 13: //RANGEMIN
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                k = get_id(instance_range_desc, word_buf);
                if (k < 0)
                {
                    k = atoi(word_buf);
                }
                inst_inf->range_min = k;
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                    COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
            }
            break;
        case 14: //RANGEMAX
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                k = get_id(instance_range_desc, word_buf);
                if (k < 0)
                {
                    k = atoi(word_buf);
                }
                inst_inf->range_max = k;
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                    COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
            }
            break;
        case 15: // PROPERTIES
            inst_inf->instance_property_flags = 0;
            while (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = get_id(creaturetype_instance_properties, word_buf);
                if (k > 0)
                {
                    set_flag(inst_inf->instance_property_flags, k);
                }
                else
                {
                    CONFWRNLOG("Incorrect value of \"%s\" parameter \"%s\" in [%.*s] block of %s file.",
                        COMMAND_TEXT(cmd_num), word_buf, (int)(blocknamelen), blockname, config_textname);
                }
            }
            break;
        case 16: // FPINSTANTCAST
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              inst_inf->instant = (TbBool)k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                    COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
            }
            break;
        case 17: // PRIMARYTARGET
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                inst_inf->primary_target = k;
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                    COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
            }
            break;
        case 18: // ValidateSourceFunc
            k = recognize_conf_parameter(buf, &pos, len, creature_instances_validate_func_type);
            if (k > 0)
            {
                inst_inf->validate_source_func = k;
                n++;
            }
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                inst_inf->validate_source_func_params[0] = k;
                n++;
                if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
                {
                    k = atoi(word_buf);
                    inst_inf->validate_source_func_params[1] = k;
                    n++;
                }
            }
            break;
        case 19: // ValidateTargetFunc
            k = recognize_conf_parameter(buf, &pos, len, creature_instances_validate_func_type);
            if (k > 0)
            {
                inst_inf->validate_target_func = k;
                n++;
            }
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                inst_inf->validate_target_func_params[0] = k;
                n++;
                if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
                {
                    k = atoi(word_buf);
                    inst_inf->validate_target_func_params[1] = k;
                    n++;
                }
            }
            break;
        case 20: // SearchTargetsFunc
            k = recognize_conf_parameter(buf, &pos, len, creature_instances_search_targets_func_type);
            if (k > 0)
            {
                inst_inf->search_func = k;
                n++;
            }
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                inst_inf->search_func_params[0] = k;
                n++;
                if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
                {
                    k = atoi(word_buf);
                    inst_inf->search_func_params[1] = k;
                    n++;
                }
            }
            break;
        case 21: // PostalPriority
        if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                inst_inf->postal_priority = k;
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                    COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
            }
            break;
        case 22: // NoAnimationLoop
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                inst_inf->no_animation_loop = (k > 0);
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                    COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
            }
            break;
        case 23: // FPALLOWSELFCASTWHILEFROZEN
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              inst_inf->fp_allow_self_cast_while_frozen = (TbBool)k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                    COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
            }
            break;
        case 24: // FPALLOWSELFCASTWHENCHICKEN
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              inst_inf->fp_allow_self_cast_when_chicken = (TbBool)k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                    COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
            }
            break;
        case ccr_comment:
            break;
        case ccr_endOfFile:
            break;
        default:
            CONFWRNLOG("Unrecognized command (%" PRId64 ") in [%.*s] block of %s file.",
                (int64_t)(cmd_num), (int)(blocknamelen), blockname, config_textname);
            break;
        }
        skip_conf_to_next_line(buf,&pos,len);
      }
#undef COMMAND_TEXT
    }
    return true;
}

TbBool parse_creaturetype_job_blocks(char *buf, int64_t len, const char *config_textname, int64_t flags)
{
    struct CreatureJobConfig *jobcfg;
    int64_t k = 0;
    // Initialize the array
    if ((flags & CnfLd_AcceptPartial) == 0) {
        for (int64_t i = 0; i < INSTANCE_TYPES_MAX; i++) {
            jobcfg = &kfx_config_state.conf.crtr_conf.jobs[i];
            memset(jobcfg->name, 0, COMMAND_WORD_LEN);
            jobcfg->room_role = RoRoF_None;
            jobcfg->initial_crstate = CrSt_Unused;
            jobcfg->continue_crstate = CrSt_Unused;
            jobcfg->job_flags = 0;
            jobcfg->func_plyr_check_idx = 0;
            jobcfg->func_plyr_assign_idx = 0;
            jobcfg->func_cord_check_idx = 0;
            jobcfg->func_cord_assign_idx = 0;
            creaturejob_desc[i].name = kfx_config_state.conf.crtr_conf.jobs[i].name;
            creaturejob_desc[i].num = (1 << (i-1)); // creature jobs are a bit mask
        }
    }
    creaturejob_desc[INSTANCE_TYPES_MAX - 1].name = NULL; // must be null for get_id
    // Load the file blocks
    const char * blockname = NULL;
    int64_t blocknamelen = 0;
    int64_t pos = 0;
    TbBool seen[INSTANCE_TYPES_MAX];
    memset(seen, 0, sizeof(seen));
    while (iterate_conf_blocks(buf, &pos, len, &blockname, &blocknamelen))
    {
        // look for blocks starting with "job", followed by one or more digits
        if (blocknamelen < 4) {
            continue;
        } else if (memcmp(blockname, "job", 3) != 0) {
            continue;
        }
        const int64_t i = natoi(&blockname[3], blocknamelen - 3);
        if (i < 0 || i >= INSTANCE_TYPES_MAX) {
            continue;
        } else if (i >= kfx_config_state.conf.crtr_conf.jobs_count) {
            kfx_config_state.conf.crtr_conf.jobs_count = i + 1;
        }
        jobcfg = &kfx_config_state.conf.crtr_conf.jobs[i];
        seen[i] = true;
#define COMMAND_TEXT(cmd_num) get_conf_parameter_text(creaturetype_job_commands,cmd_num)
        while (pos<len)
        {
            // Finding command number in this line
            int64_t cmd_num = recognize_conf_command(buf, &pos, len, creaturetype_job_commands);
            // Now store the config item in correct place
            if (cmd_num == ccr_endOfBlock) break; // if next block starts
            if ((flags & CnfLd_ListOnly) != 0) {
                // In "List only" mode, accept only name command
                if (cmd_num > 1) {
                    cmd_num = 0;
                }
            }
            int64_t n = 0;
            char word_buf[COMMAND_WORD_LEN];
            switch (cmd_num)
            {
            case 1: // NAME
                if (get_conf_parameter_single(buf,&pos,len,kfx_config_state.conf.crtr_conf.jobs[i].name,COMMAND_WORD_LEN) <= 0)
                {
                    CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                        COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
                    break;
                }
                n++;
                break;
            case 2: // RELATEDROOMROLE
                jobcfg->room_role = RoRoF_None;
                if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
                {
                    k = get_id(room_roles_desc, word_buf);
                    if (k >= 0)
                    {
                        jobcfg->room_role = k;
                        n++;
                    } else
                    {
                        if (strcasecmp(word_buf,"NULL") == 0)
                            n++;
                    }
                }
                if (n < 1)
                {
                  CONFWRNLOG("Incorrect value of \"%s\" parameter in [%.*s] block of %s file.",
                      COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
                }
                break;
            case 3: // RELATEDEVENT
                jobcfg->event_kind = 0;
                if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
                {
                    k = get_id(mevents_desc, word_buf);
                    if (k >= 0)
                    {
                        jobcfg->event_kind = k;
                        n++;
                    }
                }
                if (n < 1)
                {
                  CONFWRNLOG("Incorrect value of \"%s\" parameter in [%.*s] block of %s file.",
                      COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
                }
                break;
            case 4: // ASSIGN
                jobcfg->job_flags &= ~(JoKF_AssignHumanDrop|JoKF_AssignComputerDrop|JoKF_AssignCeatureInit|
                    JoKF_AssignAreaWithinRoom|JoKF_AssignAreaOutsideRoom|JoKF_AssignOnAreaBorder|JoKF_AssignOnAreaCenter|
                    JoKF_OwnedCreatures|JoKF_EnemyCreatures|JoKF_OwnedDiggers|JoKF_EnemyDiggers|
                    JoKF_AssignOneTime|JoKF_NeedsHaveJob);
                while (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
                {
                    k = get_id(creaturetype_job_assign, word_buf);
                    if (k > 0)
                    {
                        jobcfg->job_flags |= k;
                      n++;
                    } else {
                        CONFWRNLOG("Incorrect value of \"%s\" parameter \"%s\" in [%.*s] block of %s file.",
                            COMMAND_TEXT(cmd_num), word_buf, (int)(blocknamelen), blockname, config_textname);
                        break;
                    }
                }
                break;
            case 5: // INITIALSTATE
                jobcfg->initial_crstate = CrSt_Unused;
                if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
                {
                    k = get_id(creatrstate_desc, word_buf);
                    if (k >= 0)
                    {
                        jobcfg->initial_crstate = k;
                        n++;
                    } else
                    {
                        if (strcasecmp(word_buf,"NONE") == 0)
                            n++;
                    }
                }
                if (n < 1)
                {
                  CONFWRNLOG("Incorrect value of \"%s\" parameter in [%.*s] block of %s file.",
                      COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
                }
                break;
            case 6: // CONTINUESTATE
                jobcfg->continue_crstate = CrSt_Unused;
                if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
                {
                    k = get_id(creatrstate_desc, word_buf);
                    if (k >= 0)
                    {
                        jobcfg->continue_crstate = k;
                        n++;
                    } else
                    {
                        if (strcasecmp(word_buf,"NONE") == 0)
                            n++;
                    }
                }
                if (n < 1)
                {
                  CONFWRNLOG("Incorrect value of \"%s\" parameter in [%.*s] block of %s file.",
                      COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
                }
                break;
            case 7: // PLAYERFUNCTIONS
                jobcfg->func_plyr_check_idx = 0;
                jobcfg->func_plyr_assign_idx = 0;
                k = recognize_conf_parameter(buf,&pos,len,creature_job_player_check_func_type);
                if (k > 0)
                {
                    jobcfg->func_plyr_check_idx = k;
                    n++;
                }
                k = recognize_conf_parameter(buf,&pos,len,creature_job_player_assign_func_type);
                if (k > 0)
                {
                    jobcfg->func_plyr_assign_idx = k;
                    n++;
                }
                if (n < 2)
                {
                    CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                        COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
                }
                break;
            case 8: // COORDSFUNCTIONS
                jobcfg->func_cord_check_idx = 0;
                jobcfg->func_cord_assign_idx = 0;
                k = recognize_conf_parameter(buf,&pos,len,creature_job_coords_check_func_type);
                if (k > 0)
                {
                    jobcfg->func_cord_check_idx = k;
                    n++;
                }
                k = recognize_conf_parameter(buf,&pos,len,creature_job_coords_assign_func_type);
                if (k > 0)
                {
                    jobcfg->func_cord_assign_idx = k;
                    n++;
                }
                if (n < 2)
                {
                    CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                        COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
                }
                break;
            case 9: // PROPERTIES
                jobcfg->job_flags &= ~(JoKF_WorkOnAreaBorder|JoKF_WorkOnAreaCenter|JoKF_NeedsCapacity|JoKF_NoSelfControl|JoKF_NoGroups|JoKF_AllowChickenized);
                while (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
                {
                    k = get_id(creaturetype_job_properties, word_buf);
                    if (k > 0)
                    {
                        jobcfg->job_flags |= k;
                      n++;
                    } else {
                        CONFWRNLOG("Incorrect value of \"%s\" parameter \"%s\" in [%.*s] block of %s file.",
                            COMMAND_TEXT(cmd_num), word_buf, (int)(blocknamelen), blockname,config_textname);
                        break;
                    }
                }
                break;
            case ccr_comment:
                break;
            case ccr_endOfFile:
                break;
            default:
                CONFWRNLOG("Unrecognized command (%" PRId64 ") in [%.*s] block of %s file.",
                    (int64_t)(cmd_num), (int)(blocknamelen), blockname, config_textname);
                break;
            }
            skip_conf_to_next_line(buf,&pos,len);
        }
        if (((jobcfg->job_flags & JoKF_NeedsHaveJob) != 0) && ((jobcfg->job_flags & JoKF_AssignOneTime) != 0))
        {
            WARNLOG("Job configured to need to have worker primary or secondary job set, but is one time job which cannot; in [%.*s] block of %s file.",
                (int)(blocknamelen), blockname, config_textname);
        }
#undef COMMAND_TEXT
    }
    if ((flags & CnfLd_AcceptPartial) == 0) {
        TbBool jobs_missing = false;
        char block_buf[COMMAND_WORD_LEN];
        for (int64_t i = 0; i < kfx_config_state.conf.crtr_conf.jobs_count; i++) {
            if (!seen[i]) {
                snprintf(block_buf, sizeof(block_buf), "job%" PRId64, (int64_t)(i));
                jobs_missing = true;
                WARNMSG("Block [%s] not found in %s file.", block_buf, config_textname);
            }
        }
        return !jobs_missing;
    }
    return true;
}

TbBool parse_creaturetype_angerjob_blocks(char *buf, int64_t len, const char *config_textname, int64_t flags)
{
    struct CreatureAngerJobConfig *agjobcfg;
    // Initialize the array
    if ((flags & CnfLd_AcceptPartial) == 0) {
        for (int64_t i = 0; i < INSTANCE_TYPES_MAX; i++) {
            agjobcfg = &kfx_config_state.conf.crtr_conf.angerjobs[i];
            memset(agjobcfg->name, 0, COMMAND_WORD_LEN);
            angerjob_desc[i].name = agjobcfg->name;
            angerjob_desc[i].num = (1 << (i-1)); // anger jobs are a bit mask
        }
    }
    // arr_size = kfx_config_state.conf.crtr_conf.angerjobs_count;
    angerjob_desc[INSTANCE_TYPES_MAX - 1].name = NULL; // must be null for get_id
    // Load the file blocks
    const char * blockname = NULL;
    int64_t blocknamelen = 0;
    int64_t pos = 0;
    TbBool seen[INSTANCE_TYPES_MAX];
    memset(seen, 0, sizeof(seen));
    while (iterate_conf_blocks(buf, &pos, len, &blockname, &blocknamelen))
    {
        // look for blocks starting with "angerjob", followed by one or more digits
        if (blocknamelen < 9) {
            continue;
        } else if (memcmp(blockname, "angerjob", 8) != 0) {
            continue;
        }
        const int64_t i = natoi(&blockname[8], blocknamelen - 8);
        if (i < 0 || i >= INSTANCE_TYPES_MAX) {
            continue;
        } else if (i >= kfx_config_state.conf.crtr_conf.angerjobs_count) {
            kfx_config_state.conf.crtr_conf.angerjobs_count = i + 1;
        }
        agjobcfg = &kfx_config_state.conf.crtr_conf.angerjobs[i];
        seen[i] = true;
#define COMMAND_TEXT(cmd_num) get_conf_parameter_text(creaturetype_angerjob_commands,cmd_num)
        while (pos<len)
        {
            // Finding command number in this line
            int64_t cmd_num = recognize_conf_command(buf, &pos, len, creaturetype_angerjob_commands);
            // Now store the config item in correct place
            if (cmd_num == ccr_endOfBlock) break; // if next block starts
            if ((flags & CnfLd_ListOnly) != 0) {
                // In "List only" mode, accept only name command
                if (cmd_num > 1) {
                    cmd_num = 0;
                }
            }
            switch (cmd_num)
            {
            case 1: // NAME
                if (get_conf_parameter_single(buf,&pos,len,kfx_config_state.conf.crtr_conf.angerjobs[i].name,COMMAND_WORD_LEN) <= 0)
                {
                    CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                        COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
                    break;
                }
                break;
            case ccr_comment:
                break;
            case ccr_endOfFile:
                break;
            default:
                CONFWRNLOG("Unrecognized command (%" PRId64 ") in [%.*s] block of %s file.",
                    (int64_t)(cmd_num), (int)(blocknamelen), blockname, config_textname);
                break;
            }
            skip_conf_to_next_line(buf,&pos,len);
        }
#undef COMMAND_TEXT
    }
    if ((flags & CnfLd_AcceptPartial) == 0) {
        TbBool jobs_missing = false;
        char block_buf[COMMAND_WORD_LEN];
        for (int64_t i = 0; i < kfx_config_state.conf.crtr_conf.angerjobs_count; i++) {
            if (!seen[i]) {
                snprintf(block_buf, sizeof(block_buf), "angerjob%" PRId64, (int64_t)(i));
                jobs_missing = true;
                WARNMSG("Block [%s] not found in %s file.", block_buf, config_textname);
            }
        }
        return !jobs_missing;
    }
    return true;
}

TbBool parse_creaturetype_attackpref_blocks(char *buf, int64_t len, const char *config_textname, int64_t flags)
{
    struct CommandWord * attacktype;
    // Initialize the array
    if ((flags & CnfLd_AcceptPartial) == 0) {
        for (int64_t i = 0; i < INSTANCE_TYPES_MAX; i++) {
            attacktype = &kfx_config_state.conf.crtr_conf.attacktypes[i];
            memset(attacktype->text, 0, COMMAND_WORD_LEN);
            attackpref_desc[i].name = attacktype->text;
            attackpref_desc[i].num = i;
        }
    }
    attackpref_desc[INSTANCE_TYPES_MAX - 1].name = NULL; // must be null for get_id
    // Load the file blocks
    const char * blockname = NULL;
    int64_t blocknamelen = 0;
    int64_t pos = 0;
    TbBool seen[INSTANCE_TYPES_MAX];
    memset(seen, 0, sizeof(seen));
    while (iterate_conf_blocks(buf, &pos, len, &blockname, &blocknamelen))
    {
        // look for blocks starting with "attackpref", followed by one or more digits
        if (blocknamelen < 11) {
            continue;
        } else if (memcmp(blockname, "attackpref", 10) != 0) {
            continue;
        }
        const int64_t i = natoi(&blockname[10], blocknamelen - 10);
        if (i < 0 || i >= INSTANCE_TYPES_MAX) {
            continue;
        } else if (i >= kfx_config_state.conf.crtr_conf.attacktypes_count) {
            kfx_config_state.conf.crtr_conf.attacktypes_count = i + 1;
        }
        seen[i] = true;
        attacktype = &kfx_config_state.conf.crtr_conf.attacktypes[i];
#define COMMAND_TEXT(cmd_num) get_conf_parameter_text(creaturetype_attackpref_commands,cmd_num)
        while (pos<len)
        {
            // Finding command number in this line
            int64_t cmd_num = recognize_conf_command(buf, &pos, len, creaturetype_attackpref_commands);
            // Now store the config item in correct place
            if (cmd_num == ccr_endOfBlock) break; // if next block starts
            if ((flags & CnfLd_ListOnly) != 0) {
                // In "List only" mode, accept only name command
                if (cmd_num > 1) {
                    cmd_num = 0;
                }
            }
            switch (cmd_num)
            {
            case 1: // NAME
                if (get_conf_parameter_single(buf,&pos,len, attacktype->text,COMMAND_WORD_LEN) <= 0)
                {
                    CONFWRNLOG("Couldn't read \"%s\" parameter in [%.*s] block of %s file.",
                        COMMAND_TEXT(cmd_num), (int)(blocknamelen), blockname, config_textname);
                    break;
                }
                break;
            case ccr_comment:
                break;
            case ccr_endOfFile:
                break;
            default:
                CONFWRNLOG("Unrecognized command (%" PRId64 ") in [%.*s] block of %s file.",
                    (int64_t)(cmd_num), (int)(blocknamelen), blockname, config_textname);
                break;
            }
            skip_conf_to_next_line(buf,&pos,len);
        }
#undef COMMAND_TEXT
    }
    if ((flags & CnfLd_AcceptPartial) == 0) {
        TbBool jobs_missing = false;
        char block_buf[COMMAND_WORD_LEN];
        for (int64_t i = 0; i < kfx_config_state.conf.crtr_conf.attacktypes_count; i++) {
            if (!seen[i]) {
                snprintf(block_buf, sizeof(block_buf), "attackpref%" PRId64, (int64_t)(i));
                jobs_missing = true;
                WARNMSG("Block [%s] not found in %s file.", block_buf, config_textname);
            }
        }
        return !jobs_missing;
    }
    return true;
}

static TbBool load_creaturetypes_config_file(const char *fname, int64_t flags)
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

    if ((flags & CnfLd_AcceptPartial) == 0)
    {
        for (int64_t i = 0; i < INSTANCE_TYPES_MAX; i++)
        {
                instance_desc[i].name = kfx_config_state.conf.crtr_conf.instances[i].name;
                instance_desc[i].num = i;
                kfx_config_state.conf.magic_conf.instance_info[i].instant = 0;
                kfx_config_state.conf.magic_conf.instance_info[i].time = 0;
                kfx_config_state.conf.magic_conf.instance_info[i].fp_time = 0;
                kfx_config_state.conf.magic_conf.instance_info[i].action_time = 0;
                kfx_config_state.conf.magic_conf.instance_info[i].fp_action_time = 0;
                kfx_config_state.conf.magic_conf.instance_info[i].reset_time = 0;
                kfx_config_state.conf.magic_conf.instance_info[i].fp_reset_time = 0;
                kfx_config_state.conf.magic_conf.instance_info[i].graphics_idx = 0;
                kfx_config_state.conf.magic_conf.instance_info[i].postal_priority = 0;
                kfx_config_state.conf.magic_conf.instance_info[i].instance_property_flags = 0;
                kfx_config_state.conf.magic_conf.instance_info[i].force_visibility = 0;
                kfx_config_state.conf.magic_conf.instance_info[i].primary_target = 0;
                kfx_config_state.conf.magic_conf.instance_info[i].func_idx = 0;
                kfx_config_state.conf.magic_conf.instance_info[i].func_params[0] = 0;
                kfx_config_state.conf.magic_conf.instance_info[i].func_params[1] = 0;
                kfx_config_state.conf.magic_conf.instance_info[i].symbol_spridx = 0;
                kfx_config_state.conf.magic_conf.instance_info[i].tooltip_stridx = 0;
                kfx_config_state.conf.magic_conf.instance_info[i].range_min = 0;
                kfx_config_state.conf.magic_conf.instance_info[i].range_max = 0;
                kfx_config_state.conf.magic_conf.instance_info[i].no_animation_loop = false;
        }
    }
    // Loading file data
    len = LbFileLoadAt(fname, buf);
    TbBool result = (len > 0);
    // Parse blocks of the config file
    if (result)
    {
        result = parse_creaturetypes_common_blocks(buf, len, fname, flags);
        if ((flags & CnfLd_AcceptPartial) != 0)
            result = true;
        if (!result)
          WARNMSG("Parsing file \"%s\" common blocks failed.",fname);
    }
    if ((result) && ((flags & CnfLd_ListOnly) == 0)) // This block doesn't have anything we'd like to parse in list mode
    {
        result = parse_creaturetype_experience_blocks(buf, len, fname, flags);
        if ((flags & CnfLd_AcceptPartial) != 0)
            result = true;
        if (!result)
          WARNMSG("Parsing file \"%s\" experience block failed.",fname);
    }
    if (result)
    {
        result = parse_creaturetype_instance_blocks(buf, len, fname, flags);
        if ((flags & CnfLd_AcceptPartial) != 0)
            result = true;
        if (!result)
          WARNMSG("Parsing file \"%s\" instance blocks failed.",fname);
    }
    if (result)
    {
        result = parse_creaturetype_job_blocks(buf, len, fname, flags);
        if ((flags & CnfLd_AcceptPartial) != 0)
            result = true;
        if (!result)
          WARNMSG("Parsing file \"%s\" job blocks failed.",fname);
    }
    if (result)
    {
        result = parse_creaturetype_angerjob_blocks(buf, len, fname, flags);
        if ((flags & CnfLd_AcceptPartial) != 0)
            result = true;
        if (!result)
          WARNMSG("Parsing file \"%s\" angerjob blocks failed.",fname);
    }
    if (result)
    {
        result = parse_creaturetype_attackpref_blocks(buf, len, fname, flags);
        if ((flags & CnfLd_AcceptPartial) != 0)
            result = true;
        if (!result)
          WARNMSG("Parsing file \"%s\" attackpref blocks failed.",fname);
    }
    //Freeing and exiting
    KfxFree(buf);
    return result;
}

ThingModel get_creature_model_with_model_flags(uint64_t needflags)
{
    for (ThingModel crmodel = 0; crmodel < kfx_config_state.conf.crtr_conf.model_count; crmodel++)
    {
        if ((kfx_config_state.conf.crtr_conf.model[crmodel].model_flags & needflags) == needflags) {
            return crmodel;
        }
    }
    return 0;
}

struct CreatureInstanceConfig *get_config_for_instance(CrInstance inst_id)
{
    if ((inst_id < 0) || (inst_id >= kfx_config_state.conf.crtr_conf.instances_count)) {
        return &kfx_config_state.conf.crtr_conf.instances[0];
    }
    return &kfx_config_state.conf.crtr_conf.instances[inst_id];
}

/**
 * Returns Code Name (name to use in script file) of given creature instance.
 */
const char *creature_instance_code_name(CrInstance inst_id)
{
    struct CreatureInstanceConfig* crinstcfg = get_config_for_instance(inst_id);
    const char* name = crinstcfg->name;
    if (name[0] != '\0')
        return name;
    return "INVALID";
}

struct CreatureJobConfig *get_config_for_job(CreatureJob job_flags)
{
    int64_t i = 0;
    uint64_t k = job_flags;
    while (k)
    {
        k >>= 1;
        i++;
    }
    if (i >= kfx_config_state.conf.crtr_conf.jobs_count) {
        return &kfx_config_state.conf.crtr_conf.jobs[0];
    }
    return &kfx_config_state.conf.crtr_conf.jobs[i];
}

/**
 * Returns a job creature can do in a room of given role, or anywhere else.
 * @param rrole Room roles for which at least one needs to match the job to be returned.
 * @param required_kind_flags Only jobs which have all of the flags set can be returned.
 *     For example, to only include jobs which can be assigned by dropping creatures by computer player,
 *     use JoKF_AssignComputerDrop flag.
 * @param has_jobs Primary and secondary jobs of a creature to be assigned; if only jobs which have
 *     no need to be in primary/secondary list should be qualified, this can be Job_NULL.
 * @return A single job flag.
 */
CreatureJob get_job_for_room_role(RoomRole rrole, uint64_t required_kind_flags, CreatureJob has_jobs)
{
    if (rrole != 0)
    {
        for (int64_t i = 0; i < kfx_config_state.conf.crtr_conf.jobs_count; i++)
        {
            struct CreatureJobConfig* jobcfg = &kfx_config_state.conf.crtr_conf.jobs[i];
            if ((jobcfg->job_flags & required_kind_flags) == required_kind_flags)
            {
                CreatureJob new_job = 1ULL << (i - 1);
                if (((jobcfg->job_flags & JoKF_NeedsHaveJob) == 0) || ((has_jobs & new_job) != 0))
                {
                    if (((jobcfg->room_role & rrole) != 0) || ((jobcfg->job_flags & JoKF_AssignAreaOutsideRoom) != 0)) {
                        return new_job;
                    }
                }
            }
        }
    }
    return Job_NULL;
}

/**
 * Returns a job creature can do in a room, or anywhere else.
 * @param rkind Room kind for which job is to be returned.
 * @param required_kind_flags Only jobs which have all of the flags set can be returned.
 *     For example, to only include jobs which can be assigned by dropping creatures by computer player,
 *     use JoKF_AssignComputerDrop flag.
 * @param has_jobs Primary and secondary jobs of a creature to be assigned; if only jobs which have
 *     no need to be in primary/secondary list should be qualified, this can be Job_NULL.
 * @return A single job flag.
 */
CreatureJob get_job_for_room(RoomKind rkind, uint64_t required_kind_flags, CreatureJob has_jobs)
{
    return get_job_for_room_role(get_room_roles(rkind), required_kind_flags, has_jobs);
}

/**
 * Returns a job creature can do in a room with given role.
 * @param rrole Room roles for which at least one needs to match the job to be returned.
 * @param qualify_flags Only jobs which have at least one of the flags set can be returned.
 * @param prevent_flags Only jobs which have none of the flags set can be returned.
 * @return A single job flag.
 */
CreatureJob get_job_which_qualify_for_room_role(RoomRole rrole, uint64_t qualify_flags, uint64_t prevent_flags)
{
    if (rrole == RoRoF_None) {
        return Job_NULL;
    }
    for (int64_t i = 0; i < kfx_config_state.conf.crtr_conf.jobs_count; i++)
    {
        struct CreatureJobConfig* jobcfg = &kfx_config_state.conf.crtr_conf.jobs[i];
        if ((jobcfg->job_flags & qualify_flags) != 0)
        {
            if ((jobcfg->job_flags & prevent_flags) == 0)
            {
                if ((jobcfg->room_role & rrole) != 0) {
                    return 1ULL << (i-1);
                }
            }
        }
    }
    return Job_NULL;
}

/**
 * Returns a job creature can do in a room.
 * @param rkind Room kind for which job is to be returned.
 * @param qualify_flags Only jobs which have at least one of the flags set can be returned.
 * @param prevent_flags Only jobs which have none of the flags set can be returned.
 * @return A single job flag.
 */
CreatureJob get_job_which_qualify_for_room(RoomKind rkind, uint64_t qualify_flags, uint64_t prevent_flags)
{
    return get_job_which_qualify_for_room_role(get_room_roles(rkind), qualify_flags, prevent_flags);
}

/**
 * Returns jobs which creatures owned by enemy players may be assigned to do work in rooms of specific role.
 * @param rrole Room roles for which at least one needs to match the job to be returned.
 * @return Job flags matching.
 */
CreatureJob get_jobs_enemies_may_do_in_room_role(RoomRole rrole)
{
    CreatureJob jobpref = Job_NULL;
    for (int64_t i = 0; i < kfx_config_state.conf.crtr_conf.jobs_count; i++)
    {
        struct CreatureJobConfig* jobcfg = &kfx_config_state.conf.crtr_conf.jobs[i];
        // Accept only jobs in given room
        if ((jobcfg->room_role & rrole) != 0)
        {
            // Check whether enemies can do this job
            if ((jobcfg->job_flags & (JoKF_EnemyCreatures|JoKF_EnemyDiggers)) != 0)
            {
                jobpref |= 1ULL << (i-1);
            }
        }
    }
    return jobpref;
}

/**
 * Returns jobs which creatures owned by enemy players may be assigned to do work in specific room.
 * @param rkind Room kind to be checked.
 * @return Job flags matching.
 */
CreatureJob get_jobs_enemies_may_do_in_room(RoomKind rkind)
{
    return get_jobs_enemies_may_do_in_room_role(get_room_roles(rkind));
}

/**
 * Returns first room kind which matches role from given job.
 * Note that more than one room kind may have given role, so use
 * of this function should be limited.
 * @param job_flags
 * @return
 */
RoomKind get_first_room_kind_for_job(CreatureJob job_flags)
{
    struct CreatureJobConfig* jobcfg = get_config_for_job(job_flags);
    for (RoomKind rkind = 0; rkind < kfx_config_state.conf.slab_conf.room_types_count; rkind++)
    {
        if (room_role_matches(rkind, jobcfg->room_role))
            return rkind;
    }
    return ROOM_KIND_NONE;
}

/**
 * Returns room role from given job.
 * @param job_flags
 * @return
 */
RoomRole get_room_role_for_job(CreatureJob job_flags)
{
    struct CreatureJobConfig* jobcfg = get_config_for_job(job_flags);
    return jobcfg->room_role;
}

EventKind get_event_for_job(CreatureJob job_flags)
{
    struct CreatureJobConfig* jobcfg = get_config_for_job(job_flags);
    return jobcfg->event_kind;
}

CrtrStateId get_initial_state_for_job(CreatureJob jobpref)
{
    struct CreatureJobConfig* jobcfg = get_config_for_job(jobpref);
    return jobcfg->initial_crstate;
}

uint64_t get_flags_for_job(CreatureJob jobpref)
{
    struct CreatureJobConfig* jobcfg = get_config_for_job(jobpref);
    return jobcfg->job_flags;
}

int64_t get_required_room_capacity_for_job(CreatureJob jobpref, ThingModel crmodel)
{
    struct CreatureJobConfig* jobcfg = get_config_for_job(jobpref);
    switch (jobcfg->room_role)
    {
    case RoRoF_None:
        WARNLOG("Job needs capacity but has no related room role.");
        return 0;
    case RoRoF_LairStorage:
    case RoRoF_CrHealSleep:
    {
        struct CreatureModelConfig* crconf = creature_stats_get(crmodel);
        return crconf->lair_size;
    }
    default:
        break;
    }
    if ((jobcfg->job_flags & JoKF_NeedsCapacity) == 0)
    {
        return 0;
    }
    return 1;
}

CrtrStateId get_arrive_at_state_for_job(CreatureJob jobpref)
{
    struct CreatureJobConfig* jobcfg = get_config_for_job(jobpref);
    return jobcfg->initial_crstate;
}

CrtrStateId get_continue_state_for_job(CreatureJob jobpref)
{
    struct CreatureJobConfig* jobcfg = get_config_for_job(jobpref);
    return jobcfg->continue_crstate;
}

CreatureJob get_job_for_creature_state(CrtrStateId crstate_id)
{
    if (crstate_id == CrSt_Unused) {
        return Job_NULL;
    }
    for (int64_t i = 0; i < kfx_config_state.conf.crtr_conf.jobs_count; i++)
    {
        struct CreatureJobConfig* jobcfg = &kfx_config_state.conf.crtr_conf.jobs[i];
        //TODO CREATURE_JOBS Add other job-related states here
        if ((jobcfg->initial_crstate == crstate_id)
         || (jobcfg->continue_crstate == crstate_id)) {
            return 1ULL << (i-1);
        }
    }
    // Some additional hacks
    switch (crstate_id)
    {
    case CrSt_CreatureEat:
    case CrSt_CreatureEatingAtGarden:
    case CrSt_CreatureToGarden:
    case CrSt_CreatureArrivedAtGarden:
        return Job_TAKE_FEED;
    case CrSt_CreatureWantsSalary:
    case CrSt_CreatureTakeSalary:
        return Job_TAKE_SALARY;
    case CrSt_CreatureSleep:
    case CrSt_CreatureGoingHomeToSleep:
    case CrSt_AtLairToSleep:
    case CrSt_CreatureChooseRoomForLairSite:
    case CrSt_CreatureAtNewLair:
    case CrSt_CreatureWantsAHome:
    case CrSt_CreatureChangeLair:
    case CrSt_CreatureAtChangedLair:
        return Job_TAKE_SLEEP;
    default:
        break;
    }
    return Job_NULL;
}

/**
 * Returns Code Name (name to use in script file) of given creature model.
 */
const char *creature_job_code_name(CreatureJob job_flag)
{
    struct CreatureJobConfig* jobcfg = get_config_for_job(job_flag);
    const char* name = jobcfg->name;
    if (name[0] != '\0')
        return name;
    return "INVALID";
}

/**
 * Gives the job which can cause creature stress in specific room.
 *
 * @param job_flags Primary job flags of a creature kind to be checked.
 * @param rkind Room kind to be checked.
 * @return Returns a single job flag, or Job_NULL.
 */
CreatureJob get_creature_job_causing_stress(CreatureJob job_flags, RoomKind rkind)
{
    // Allowing one-time jobs to be stressful would make this job selection ambiguous
    // TODO CREATURE_JOBS it would be better to get stressful job based on creature state, not on room
    CreatureJob qualified_job = get_job_which_qualify_for_room(rkind, JoKF_OwnedCreatures | JoKF_OwnedDiggers, JoKF_AssignOneTime);
    return (job_flags & qualified_job);
}

/**
 * Gives the job which can cause creature going postal in specific room.
 *
 * @param job_flags Primary job flags of a creature kind to be checked.
 * @param rkind Room kind to be checked.
 * @return Returns a single job flag, or Job_NULL.
 */
CreatureJob get_creature_job_causing_going_postal(CreatureJob job_flags, RoomKind rkind)
{
    CreatureJob qualified_job = get_job_which_qualify_for_room(rkind, JoKF_OwnedCreatures | JoKF_OwnedDiggers, JoKF_EnemyCreatures | JoKF_EnemyDiggers | JoKF_AssignOneTime | JoKF_NoSelfControl);
    return (job_flags & qualified_job);
}

#ifdef __cplusplus
}
#endif
