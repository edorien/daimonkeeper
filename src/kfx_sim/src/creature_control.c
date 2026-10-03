/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file creature_control.c
 *     CreatureControl structure support functions.
 * @par Purpose:
 *     Functions to use CreatureControl for controlling creatures.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     23 Apr 2009 - 16 May 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "creature_control.h"
#include "globals.h"

#include "bflib_math.h"
#include "bflib_sound.h"
#include "config_creature.h"
#include "creature_states.h"
#include "creature_instances.h"
#include "thing_stats.h"
#include "thing_effects.h"
#include "player_instances.h"
#include "bflib_sound.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "config_strings.h"
#include "light_registry.h"
#include "ports/ui_port.h"
#include "ports/audio_port.h"
#include "ports/render_port.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

/******************************************************************************/
/**
 * Returns CreatureControl of given index.
 */
struct CreatureControl *creature_control_get(CctrlIndex cctrl_idx)
{
  if ((cctrl_idx < 1) || (cctrl_idx >= CREATURES_COUNT))
    return INVALID_CRTR_CONTROL;
  return &kfx_sim_state.cctrl_data[cctrl_idx];
}

/**
 * Returns CreatureControl assigned to given thing.
 * Thing must be a creature.
 */
struct CreatureControl *creature_control_get_from_thing(const struct Thing *thing)
{
  if ((thing->ccontrol_idx < 1) || (thing->ccontrol_idx >= CREATURES_COUNT))
    return INVALID_CRTR_CONTROL;
  return &kfx_sim_state.cctrl_data[thing->ccontrol_idx];
}

/**
 * Returns if given CreatureControl pointer is incorrect.
 */
TbBool creature_control_invalid(const struct CreatureControl *cctrl)
{
  if (cctrl == NULL)
    return true;
  return (cctrl <= &kfx_sim_state.cctrl_data[0]);
}

TbBool creature_control_exists(const struct CreatureControl *cctrl)
{
  if (creature_control_invalid(cctrl))
      return false;
  if ((cctrl->creature_control_flags & CCFlg_Exists) == 0)
      return false;
  return true;
}

CctrlIndex i_can_allocate_free_control_structure(void)
{
    for (CctrlIndex i = 1; i < CREATURES_COUNT; i++)
    {
        struct CreatureControl* cctrl = &kfx_sim_state.cctrl_data[i];
        if (!creature_control_invalid(cctrl))
        {
            if ((cctrl->creature_control_flags & CCFlg_Exists) == 0)
                return i;
        }
  }
  return 0;
}

struct CreatureControl *allocate_free_control_structure(void)
{
    for (int64_t i = 1; i < CREATURES_COUNT; i++)
    {
        struct CreatureControl* cctrl = &kfx_sim_state.cctrl_data[i];
        if (!creature_control_invalid(cctrl))
        {
            if ((cctrl->creature_control_flags & CCFlg_Exists) == 0)
            {
                memset(cctrl, 0, sizeof(struct CreatureControl));
                cctrl->creature_control_flags |= CCFlg_Exists;
                cctrl->index = i;
                return cctrl;
            }
      }
    }
    return NULL;
}

void delete_control_structure(struct CreatureControl *cctrl)
{
    memset(cctrl, 0, sizeof(struct CreatureControl));
}

void delete_all_control_structures(void)
{
    for (int64_t i = 1; i < CREATURES_COUNT; i++)
    {
        struct CreatureControl* cctrl = creature_control_get(i);
        if (!creature_control_invalid(cctrl))
        {
            if ((cctrl->creature_control_flags & CCFlg_Exists) != 0)
                delete_control_structure(cctrl);
      }
    }
}

struct Thing *create_and_control_creature_as_controller(struct PlayerInfo *player, ThingModel crmodel, struct Coord3d *pos)
{
    SYNCDBG(6,"Request for model %" PRId64 " (%s) at (%" PRId64 ",%" PRId64 ",%" PRId64 ")",
        (int64_t)(crmodel), creature_code_name(crmodel),(int64_t)pos->x.val,(int64_t)pos->y.val,(int64_t)pos->z.val);
    struct Thing* thing = create_creature(pos, crmodel, player->id_number);
    if (thing_is_invalid(thing))
      return INVALID_THING;
    // Do not count the spectator creature as real creature
    struct Dungeon* dungeon = get_dungeon(thing->owner);
    dungeon->num_active_creatrs--;
    dungeon->owned_creatures_of_model[thing->model]--;
    if (is_my_player(player))
    {
        ui_toggle_status_menu(0);
        ui_turn_off_roaming_menus();
    }
    const struct Camera* cam = get_player_active_camera(player);
    set_selected_creature(player, thing);
    player->view_mode_restore = cam->view_mode;
    thing->alloc_flags |= TAlF_IsControlled;
    thing->rendering_flags |= TRF_Invisible;
    struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
    cctrl->creature_state_flags |= TF2_Spectator;
    cctrl->max_speed = calculate_correct_creature_maxspeed(thing);
    set_player_mode(player, PVT_CreatureContrl);
    set_start_state(thing);
    // Preparing light object
    struct InitLight ilght;
    memset(&ilght, 0, sizeof(struct InitLight));
    ilght.mappos.x.val = thing->mappos.x.val;
    ilght.mappos.y.val = thing->mappos.y.val;
    ilght.mappos.z.val = thing->mappos.z.val;
    ilght.intensity = 36;
    ilght.flags = 1;
    ilght.is_dynamic = 1;
    ilght.radius = 10 * COORD_PER_STL;
    ilght.colour_r = kfx_config_state.conf.rules[thing->owner].gameplay.possession_light_r;
    ilght.colour_g = kfx_config_state.conf.rules[thing->owner].gameplay.possession_light_g;
    ilght.colour_b = kfx_config_state.conf.rules[thing->owner].gameplay.possession_light_b;
    thing->light_id = light_create_light(&ilght);
    if (thing->light_id != 0)
    {
        light_set_light_never_cache(thing->light_id);
    } else
    {
        ERRORLOG("Cannot allocate light to new hero");
    }
    if (is_my_player_number(thing->owner))
    {
        if (thing->class_id == TCls_Creature)
        {
            struct CreatureModelConfig* crconf = creature_stats_get_from_thing(thing);
            SYNCDBG(7,"Possessing creature '%s', eye_effect=%" PRId64, crconf->name, (int64_t)(crconf->eye_effect));
            render_setup_eye_lens(crconf->eye_effect);
        }
    }
    return thing;
}

void clear_creature_instance(struct Thing *thing)
{
    struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
    cctrl->instance_id = CrInst_NULL;
    cctrl->inst_turn = 0;
}

struct Thing *get_group_last_member(struct Thing *thing)
{
    struct Thing* ctng = thing;
    struct CreatureControl* cctrl = creature_control_get_from_thing(ctng);
    int64_t k = 0;
    while (cctrl->next_in_group > 0)
    {
        ctng = thing_get(cctrl->next_in_group);
        cctrl = creature_control_get_from_thing(ctng);
        k++;
        if (k > CREATURES_COUNT)
        {
          ERRORLOG("Infinite loop detected when sweeping creatures group");
          break;
        }
    }
    return ctng;
}

TbBool disband_creatures_group(struct Thing *thing)
{
    // Disband the group, removing creatures from end
    SYNCDBG(3,"Removing %s index %" PRId64 " owned by player %" PRId64,thing_model_name(thing),(int64_t)thing->index,(int64_t)thing->owner);
    return perform_action_on_all_creatures_in_group(thing, remove_creature_from_group_without_leader_consideration);
}

struct CreatureSound *get_creature_sound(struct Thing *thing, int64_t snd_idx)
{
    ThingModel cmodel = thing->model;
    if ((cmodel < 1) || (cmodel >= kfx_config_state.conf.crtr_conf.model_count))
    {
        ERRORLOG("Trying to get sound for undefined creature type %" PRId64,(int64_t)cmodel);
        // Return dummy element
        return &kfx_config_state.conf.crtr_conf.creature_sounds[0].foot;
    }
    switch (snd_idx)
    {
    case CrSnd_Hit:
        return &kfx_config_state.conf.crtr_conf.creature_sounds[cmodel].hit;
    case CrSnd_Happy:
        return &kfx_config_state.conf.crtr_conf.creature_sounds[cmodel].happy;
    case CrSnd_Sad:
        return &kfx_config_state.conf.crtr_conf.creature_sounds[cmodel].sad;
    case CrSnd_Hang:
        return &kfx_config_state.conf.crtr_conf.creature_sounds[cmodel].hang;
    case CrSnd_Drop:
        return &kfx_config_state.conf.crtr_conf.creature_sounds[cmodel].drop;
    case CrSnd_Torture:
        return &kfx_config_state.conf.crtr_conf.creature_sounds[cmodel].torture;
    case CrSnd_Slap:
        return &kfx_config_state.conf.crtr_conf.creature_sounds[cmodel].slap;
    case CrSnd_Die:
        return &kfx_config_state.conf.crtr_conf.creature_sounds[cmodel].die;
    case CrSnd_Foot:
        return &kfx_config_state.conf.crtr_conf.creature_sounds[cmodel].foot;
    case CrSnd_Fight:
        return &kfx_config_state.conf.crtr_conf.creature_sounds[cmodel].fight;
    case CrSnd_Piss:
        return &kfx_config_state.conf.crtr_conf.creature_sounds[cmodel].piss;
    default:
        // Return dummy element
        return &kfx_config_state.conf.crtr_conf.creature_sounds[0].foot;
    }
}

TbBool playing_creature_sound(struct Thing *thing, int64_t snd_idx)
{
    struct CreatureSound* crsound = get_creature_sound(thing, snd_idx);
    for (int64_t i = 0; i < crsound->count; i++)
    {
        if (S3DEmitterIsPlayingSample(thing->snd_emitter_id, creature_sound_unified_id(crsound, i)))
          return true;
    }
    return false;
}

void stop_creature_sound(struct Thing *thing, int64_t snd_idx)
{
    struct CreatureSound* crsound = get_creature_sound(thing, snd_idx);
    if (crsound->index == 0) {
        SYNCDBG(19,"No sample %" PRId64 " for creature %" PRId64,(int64_t)(snd_idx),(int64_t)(thing->model));
        return;
    }

    for (int64_t i = 0; i < crsound->count; i++)
    {
        SoundSmplTblID uid = creature_sound_unified_id(crsound, i);
        if (S3DEmitterIsPlayingSample(thing->snd_emitter_id, uid))
        {
            S3DDeleteSampleFromEmitter(thing->snd_emitter_id, uid);
        }
    }
}

void play_creature_sound(struct Thing *thing, int64_t snd_idx, int64_t priority, int64_t use_flags)
{
    SYNCDBG(8,"Starting");
    if (playing_creature_sound(thing, snd_idx)) {
      return;
    }
    struct CreatureSound* crsound = get_creature_sound(thing, snd_idx);
    if (crsound->index == 0) {
        SYNCDBG(19,"No sample %" PRId64 " for creature %" PRId64,(int64_t)(snd_idx),(int64_t)(thing->model));
        return;
    }
    int64_t i = SOUND_RANDOM(crsound->count);
    
    // Handle negative indices (custom sounds) differently
    // For custom sounds: -1, -2, -3, etc. represent sequential custom bank samples
    // We subtract the offset to keep them negative
    SoundSmplTblID sample_idx;
    if (crsound->index < 0) {
        sample_idx = crsound->index - i;  // -1, -2, -3, etc.
    } else {
        sample_idx = crsound->index + i;  // Regular positive indices
    }
    
    SYNCDBG(18,"Playing sample %" PRId64 " (sound type %" PRId64 ", index %" PRId64 ") for creature %" PRId64,
            (int64_t)(sample_idx), (int64_t)(snd_idx), (int64_t)(crsound->index), (int64_t)(thing->model));
    
    if ( use_flags ) {
        audio_thing_play_sample(thing, sample_idx, NORMAL_PITCH, 0, 3, 8, priority, FULL_LOUDNESS);
    } else {
        audio_thing_play_sample(thing, sample_idx, NORMAL_PITCH, 0, 3, 0, priority, FULL_LOUDNESS);
    }
}

void play_creature_sound_and_create_sound_thing(struct Thing *thing, int64_t snd_idx, int64_t sound_priority)
{
    if (playing_creature_sound(thing, snd_idx)) {
        return;
    }
    struct CreatureSound* crsound = get_creature_sound(thing, snd_idx);
    if (crsound->index == 0) {
        SYNCDBG(14,"No sample %" PRId64 " for creature %" PRId64,(int64_t)(snd_idx),(int64_t)(thing->model));
        return;
    }
    int64_t i = SOUND_RANDOM(crsound->count);
    struct Thing* efftng = create_effect(&thing->mappos, TngEff_Dummy, thing->owner);
    if (!thing_is_invalid(efftng)) {
        audio_thing_play_sample(efftng, (SoundSmplTblID)(crsound->index < 0 ? crsound->index - i : crsound->index + i),
            NORMAL_PITCH, 0, 3, 0, sound_priority, FULL_LOUDNESS);
    }
}

TbBool creature_can_gain_experience(const struct Thing *thing)
{
    struct Dungeon* dungeon = get_dungeon(thing->owner);
    struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
    // Creatures which reached players max level can't be trained
    if (cctrl->exp_level >= dungeon->creature_max_level[thing->model])
        return false;
    // Creatures which reached absolute max level and have no grow up creature
    struct CreatureModelConfig* crconf = creature_stats_get_from_thing(thing);
    if ((cctrl->exp_level >= (CREATURE_MAX_LEVEL-1)) && (crconf->grow_up == 0))
        return false;
    return true;
}

// creature_own_name() and its name-part tables moved here from kfx_config's
// config_creature.c (refactor pass 2, S05): it reads and fills the live
// cctrl->creature_name buffer.
static const char *name_starts[] = {
    "B", "C", "D", "F",
    "G", "H", "J", "K",
    "L", "M", "N", "P",
    "R", "S", "T", "V",
    "Y", "Z", "Ch",
    "Sh", "Al", "Th",
};

static const char *name_vowels[] = {
    "a",  "e",  "i", "o",
    "u",  "ee", "oo",
    "oa", "ai", "ea",
};

static const char *name_consonants[] = {
    "b", "c", "d", "f",
    "g", "h", "j", "k",
    "l", "m", "n", "p",
    "r", "s", "t", "v",
    "y", "z", "ch", "sh"
};

const char *creature_own_name(const struct Thing *creatng)
{
    if ((get_creature_model_flags(creatng) & CMF_OneOfKind) != 0) {
        struct CreatureModelConfig* crconf = creature_stats_get_from_thing(creatng);
        return get_string(crconf->namestr_idx);
    }
    char* creature_name = creature_control_get_from_thing(creatng)->creature_name;
    if (creature_name[0] > 0)
    {
        return creature_name;
    }
    const char ** starts;
    int64_t starts_len;
    const char ** vowels;
    int64_t vowels_len;
    const char ** consonants;
    int64_t consonants_len;
    const char ** end_vowels;
    int64_t end_vowels_len;
    const char ** end_consonants;
    int64_t end_consonants_len;
    {
        starts = name_starts;
        starts_len = sizeof(name_starts)/sizeof(name_starts[0]);
        vowels = name_vowels;
        vowels_len = sizeof(name_vowels)/sizeof(name_vowels[0]);
        consonants = name_consonants;
        consonants_len = sizeof(name_consonants)/sizeof(name_consonants[0]);
        end_vowels = name_vowels;
        end_vowels_len = sizeof(name_vowels)/sizeof(name_vowels[0]);
        end_consonants = name_consonants;
        end_consonants_len = sizeof(name_consonants)/sizeof(name_consonants[0]);
    }
    {
        uint32_t seed = creatng->creation_turn + creatng->index
            + (creature_control_get_from_thing(creatng)->blood_type << 8);
        // Get amount of nucleus
        int64_t name_len = 0;
        {
            int64_t n = LB_RANDOM(65536, &seed);
            name_len = ((n & 7) + ((n>>8) & 7)) >> 1;
            name_len = min(max(2, name_len), 8);
        }
        // Get starting part of a name
        {
            int64_t n = LB_RANDOM(starts_len, &seed);
            const char* part = starts[n];
            str_append(creature_name, CREATURE_NAME_MAX, part);
        }
        // Append nucleus items to the name
        for (int64_t i = 0; i < name_len - 1; i++)
        {
            const char *part;
            int64_t n;
            if (i & 1) {
                n = LB_RANDOM(consonants_len, &seed);
                part = consonants[n];
            } else {
                n = LB_RANDOM(vowels_len, &seed);
                part = vowels[n];
            }
            str_append(creature_name, CREATURE_NAME_MAX, part);
        }
        {
            const char *part;
            int64_t n;
            if ((name_len & 1) == 0) {
                n = LB_RANDOM(end_consonants_len, &seed);
                part = end_consonants[n];
            } else {
                n = LB_RANDOM(end_vowels_len, &seed);
                part = end_vowels[n];
            }
            str_append(creature_name, CREATURE_NAME_MAX, part);
        }
    }
    return creature_name;
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
