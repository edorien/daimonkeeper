/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_objects.c
 *     Object things configuration loading functions.
 * @par Purpose:
 *     Support of configuration files for object things.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     11 Jun 2012 - 16 Aug 2012
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "config_objects.h"
#include "globals.h"

#include "bflib_basics.h"
#include "bflib_dernc.h"
#include "bflib_sound.h"

#include "config.h"
#include "config_creature.h"
#include "config_sounds.h"
#include "config_terrain.h"
#include "kfx_config_state.h"
#include "config_strings.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// Duplicated from kfx_sim's thing_objects.h (enum ObjectsDrawClasses's
// ODC_Default) rather than included, to avoid a kfx_config -> kfx_sim
// layering violation for a single stable default-value constant. See
// docs/refactor/stage-13-enforce-and-document.md.
#define OBJECTS_ODC_DEFAULT 0x02

struct NamedCommand object_desc[OBJECT_TYPES_MAX];
/******************************************************************************/
static TbBool load_objects_config_file(const char *fname, int64_t flags);

const struct ConfigFileData keeper_objects_file_data = {
    .filename = "objects.cfg",
    .load_func = load_objects_config_file,
    .pre_load_func = NULL,
    .post_load_func = NULL,
};

const struct NamedCommand objects_properties_commands[] = {
  {"EXISTS_ONLY_IN_ROOM",     OMF_ExistsOnlyInRoom    },
  {"DESTROYED_ON_ROOM_CLAIM", OMF_DestroyedOnRoomClaim},
  {"CHOWNED_ON_ROOM_CLAIM",   OMF_ChOwnedOnRoomClaim  },
  {"DESTROYED_ON_ROOM_PLACE", OMF_DestroyedOnRoomPlace},
  {"BUOYANT",                 OMF_Buoyant             },
  {"BEATING",                 OMF_Beating             },
  {"HEART",                   OMF_Heart               },
  {"HOLD_IN_HAND",            OMF_HoldInHand          },
  {"IGNORED_BY_IMPS",         OMF_IgnoredByImps       },
  {"SLAPPABLE",               OMF_Slappable           },
  {NULL,                      0},
  };

const struct NamedCommand objects_genres_desc[] = {
  {"NONE",            OCtg_None},
  {"DECORATION",      OCtg_Decoration},
  {"FURNITURE",       OCtg_Furniture},
  {"VALUABLE",        OCtg_Valuable},
  {"SPELLBOOK",       OCtg_Spellbook},
  {"SPECIALBOX",      OCtg_SpecialBox},
  {"WORKSHOPBOX",     OCtg_WrkshpBox},
  {"TREASURE_HOARD",  OCtg_GoldHoard},
  {"FOOD",            OCtg_Food},
  {"POWER",           OCtg_Power},
  {"LAIR_TOTEM",      OCtg_LairTotem},
  {"EFFECT",          OCtg_Effect},
  {"HEROGATE",        OCtg_HeroGate},
  {NULL,              0},
  };

// Moved from kfx_sim's thing_objects.c -- this file (its UPDATEFUNCTION
// field parser) is its only real consumer. Index order must stay in sync
// with thing_objects.c's static object_update_functions[] array. See
// docs/refactor/stage-13-enforce-and-document.md.
const struct NamedCommand object_update_functions_desc[] = {
  {"UPDATE_DUNGEON_HEART",   1},
  {"UPDATE_CALL_TO_ARMS",    2},
  {"UPDATE_ARMOUR",          3},
  {"UPDATE_OBJECT_SCALE",    4},
  {"UPDATE_POWER_SIGHT",     5},
  {"UPDATE_POWER_LIGHTNING", 6},
  {"NULL",                   0},
  {NULL,                  0},
  };

static const struct NamedField objects_named_fields[] = {
    //name                     //pos    //field                                                                 //default //min     //max    //NamedCommand
    {"NAME",                     0, field_t(struct ObjectConfigStats, code_name),                     0, INT32_MIN,UINT32_MAX, object_desc,                 value_name,      assign_null},
    {"GENRE",                    0, field_t(struct ObjectConfigStats, genre),                         0, INT32_MIN,UINT32_MAX, objects_genres_desc,         value_default,   assign_default},
    {"RELATEDCREATURE",          0, field_t(struct ObjectConfigStats, related_creatr_model),          0, INT32_MIN,UINT32_MAX, creature_desc,               value_default,   assign_default},
    {"PROPERTIES",              -1, field_t(struct ObjectConfigStats, model_flags),                   0, INT32_MIN,UINT32_MAX, objects_properties_commands, value_flagsfield,assign_default},
    {"ANIMATIONID",              0, field_t(struct ObjectConfigStats, sprite_anim_idx),               0, INT32_MIN,UINT32_MAX, NULL,                        value_animid,    assign_animid},
    {"HANDANIMATIONID",          0, field_t(struct ObjectConfigStats, sprite_anim_idx_in_hand),       0, INT32_MIN,UINT32_MAX, NULL,                        value_animid,    assign_animid},
    {"ANIMATIONSPEED",           0, field_t(struct ObjectConfigStats, anim_speed),                    0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"SIZE_XY",                  0, field_t(struct ObjectConfigStats, size_xy),                       0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"SIZE_YZ",                  0, field_t(struct ObjectConfigStats, size_z),                        0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"SIZE_Z",                   0, field_t(struct ObjectConfigStats, size_z),                        0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"MAXIMUMSIZE",              0, field_t(struct ObjectConfigStats, sprite_size_max),               0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"DESTROYONLIQUID",          0, field_t(struct ObjectConfigStats, destroy_on_liquid),             0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"DESTROYONLAVA",            0, field_t(struct ObjectConfigStats, destroy_on_lava),               0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"HEALTH",                   0, field_t(struct ObjectConfigStats, health),                        0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"FALLACCELERATION",         0, field_t(struct ObjectConfigStats, fall_acceleration),             0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"LIGHTUNAFFECTED",          0, field_t(struct ObjectConfigStats, light_unaffected),              0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"LIGHTINTENSITY",           0, field_t(struct ObjectConfigStats, ilght.intensity),               0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"LIGHTRADIUS",              0, field_t(struct ObjectConfigStats, ilght.radius),                  0, INT32_MIN,UINT32_MAX, NULL,                        value_stltocoord,assign_default},
    {"LIGHTISDYNAMIC",           0, field_t(struct ObjectConfigStats, ilght.is_dynamic),              0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"MAPICON",                  0, field_t(struct ObjectConfigStats, map_icon),                      0, INT32_MIN,UINT32_MAX, NULL,                        value_icon,      assign_icon},
    {"HANDICON",                 0, field_t(struct ObjectConfigStats, hand_icon),                     0, INT32_MIN,UINT32_MAX, NULL,                        value_icon,      assign_icon},
    {"PICKUPOFFSET",             0, field_t(struct ObjectConfigStats, object_picked_up_offset.delta_x), 0,SHRT_MIN,SHRT_MAX, NULL,                        value_default,   assign_default},
    {"PICKUPOFFSET",             1, field_t(struct ObjectConfigStats, object_picked_up_offset.delta_y), 0,SHRT_MIN,SHRT_MAX, NULL,                        value_default,   assign_default},
    {"TOOLTIPTEXTID",            0, field_t(struct ObjectConfigStats, tooltip_stridx),     GUIStr_Empty, SHRT_MIN, SHRT_MAX, NULL,                        value_stringId,   assign_default},
    {"TOOLTIPTEXTID",            1, field_t(struct ObjectConfigStats, tooltip_optional),              0,        0,        1, NULL,                        value_default,   assign_default},
    {"AMBIENCESOUND",            0, field_t(struct ObjectConfigStats, fp_smpl_idx),                   0,        0,UINT32_MAX, NULL,                        value_sound_id,   assign_default},
    {"UPDATEFUNCTION",           0, field_t(struct ObjectConfigStats, updatefn_idx),                  0, INT32_MIN,UINT32_MAX, object_update_functions_desc,value_function,  assign_default},
    {"DRAWCLASS",                0, field_t(struct ObjectConfigStats, draw_class),  OBJECTS_ODC_DEFAULT, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"PERSISTENCE",              0, field_t(struct ObjectConfigStats, persistence),                   0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"IMMOBILE",                 0, field_t(struct ObjectConfigStats, immobile),                      0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"INITIALSTATE",             0, field_t(struct ObjectConfigStats, initial_state),                 0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"RANDOMSTARTFRAME",         0, field_t(struct ObjectConfigStats, random_start_frame),            0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"TRANSPARENCYFLAGS",        0, field_t(struct ObjectConfigStats, transparency_flags),            0, INT32_MIN,UINT32_MAX, NULL,                        value_transpflg, assign_default},
    {"EFFECTBEAM",               0, field_t(struct ObjectConfigStats, effect.beam),                   0, INT32_MIN,UINT32_MAX, NULL,                        value_effOrEffEl,assign_default},
    {"EFFECTPARTICLE",           0, field_t(struct ObjectConfigStats, effect.particle),               0, INT32_MIN,UINT32_MAX, NULL,                        value_effOrEffEl,assign_default},
    {"EFFECTEXPLOSION1",         0, field_t(struct ObjectConfigStats, effect.explosion1),             0, INT32_MIN,UINT32_MAX, NULL,                        value_effOrEffEl,assign_default},
    {"EFFECTEXPLOSION2",         0, field_t(struct ObjectConfigStats, effect.explosion2),             0, INT32_MIN,UINT32_MAX, NULL,                        value_effOrEffEl,assign_default},
    {"EFFECTSPACING",            0, field_t(struct ObjectConfigStats, effect.spacing),                0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"EFFECTSOUND",              0, field_t(struct ObjectConfigStats, effect.sound_idx),              0, INT32_MIN,UINT32_MAX, NULL,                        value_sound_id,   assign_default},
    {"EFFECTSOUND",              1, field_t(struct ObjectConfigStats, effect.sound_range),            0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"FLAMEANIMATIONID",         0, field_t(struct ObjectConfigStats, flame.animation_id),            0, INT32_MIN,UINT32_MAX, NULL,                        value_animid,    assign_animid},
    {"FLAMEANIMATIONSPEED",      0, field_t(struct ObjectConfigStats, flame.anim_speed),              0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"FLAMEANIMATIONSIZE",       0, field_t(struct ObjectConfigStats, flame.sprite_size),             0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"FLAMEANIMATIONOFFSET",     0, field_t(struct ObjectConfigStats, flame.fp_add_x),                0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"FLAMEANIMATIONOFFSET",     1, field_t(struct ObjectConfigStats, flame.fp_add_y),                0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"FLAMEANIMATIONOFFSET",     2, field_t(struct ObjectConfigStats, flame.td_add_x),                0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"FLAMEANIMATIONOFFSET",     3, field_t(struct ObjectConfigStats, flame.td_add_y),                0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"FLAMETRANSPARENCYFLAGS",   0, field_t(struct ObjectConfigStats, flame.transparency_flags),      0, INT32_MIN,UINT32_MAX, NULL,                        value_transpflg, assign_default},
    {"LIGHTRED",                 0, field_t(struct ObjectConfigStats, ilght.colour_r),                0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"LIGHTGREEN",               0, field_t(struct ObjectConfigStats, ilght.colour_g),                0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"LIGHTBLUE",                0, field_t(struct ObjectConfigStats, ilght.colour_b),                0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {"LIGHTFLAGS",               0, field_t(struct ObjectConfigStats, ilght.flags),                   0, INT32_MIN,UINT32_MAX, NULL,                        value_default,   assign_default},
    {NULL},
};

static int64_t* get_objects_count(void) { return &kfx_config_state.conf.object_conf.object_types_count; }
static void* get_objects_base(void) { return kfx_config_state.conf.object_conf.object_cfgstats; }

const struct NamedFieldSet objects_named_fields_set = {
    get_objects_count,
    "object",
    objects_named_fields,
    object_desc,
    OBJECT_TYPES_MAX,
    sizeof(kfx_config_state.conf.object_conf.object_cfgstats[0]),
    get_objects_base,
};

/******************************************************************************/
struct ObjectConfigStats *get_object_model_stats(ThingModel tngmodel)
{
    if (tngmodel >= kfx_config_state.conf.object_conf.object_types_count)
        return &kfx_config_state.conf.object_conf.object_cfgstats[0];
    return &kfx_config_state.conf.object_conf.object_cfgstats[tngmodel];
}

ThingClass crate_to_workshop_item_class(ThingModel tngmodel)
{
    if ((tngmodel <= 0) || (tngmodel >= kfx_config_state.conf.object_conf.object_types_count))
        return kfx_config_state.conf.object_conf.workshop_object_class[0];
    return kfx_config_state.conf.object_conf.workshop_object_class[tngmodel];
}

ThingModel crate_to_workshop_item_model(ThingModel tngmodel)
{
    if ((tngmodel <= 0) || (tngmodel >= kfx_config_state.conf.object_conf.object_types_count))
        return kfx_config_state.conf.object_conf.object_to_door_or_trap[0];
    return kfx_config_state.conf.object_conf.object_to_door_or_trap[tngmodel];
}

static TbBool load_objects_config_file(const char *fname, int64_t flags)
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

    parse_named_field_blocks(buf, len, fname, flags, &objects_named_fields_set);
    //Freeing and exiting
    KfxFree(buf);
    return true;
}

// update_all_objects_of_model() moved to kfx_sim's thing_objects.c -- it
// iterates and mutates live struct Thing state (draw refresh, light
// create/delete, dungeon-heart backup tracking), not config loading.
// See docs/refactor/stage-13-enforce-and-document.md.

/**
 * Returns Code Name (name to use in script file) of given object model.
 */
const char *object_code_name(ThingModel tngmodel)
{
    const char* name = get_conf_parameter_text(object_desc, tngmodel);
    if (name[0] != '\0')
        return name;
    return "INVALID";
}

/**
 * Returns the object model identifier for a given code name (found in script file).
 * Linear running time.
 * @param code_name
 * @return A positive integer for the object model if found, otherwise -1
 */
ThingModel object_model_id(const char * code_name)
{
    for (int64_t i = 0; i < kfx_config_state.conf.object_conf.object_types_count; ++i)
    {
        if (strncasecmp(kfx_config_state.conf.object_conf.object_cfgstats[i].code_name, code_name,
                COMMAND_WORD_LEN) == 0) {
            return i;
        }
    }

    return -1;
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
