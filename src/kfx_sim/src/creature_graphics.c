/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file creature_graphics.c
 *     Creature graphics support functions.
 * @par Purpose:
 *     Functions to maintain and use creature graphics sprites.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     11 Mar 2010 - 23 May 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "creature_graphics.h"

#include "globals.h"
#include "bflib_basics.h"

#include "player_data.h"
#include "thing_creature.h"
#include "thing_physics.h"
#include "config_creature.h"
#include "creature_instances.h"
#include "creature_states.h"
#include "player_instances.h"
#include "config_keeperfx.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "thing_objects.h"
#include "thing_stats.h"
#include "ports/render_port.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// Frame counts of the animations, by animation id: all the simulation needs of the sprites (they time the
// animations). kfx_render's sprite tables publish them when they load (keeper_sprites.c); the sprites
// themselves, their sizes and offsets, are kfx_render's alone (docs/refactor/sprites/02-design.md section 13).
// Loaded content, the same on every machine with the same data: not saved or resynced.
static unsigned char anim_frames[CREATURE_FRAMELIST_LENGTH];
static unsigned char anim_frames_known[CREATURE_FRAMELIST_LENGTH];
static unsigned char anim_frames_add[SIM_KEEPERSPRITE_ADD_NUM];

/******************************************************************************/
struct PickedUpOffset *get_creature_picked_up_offset(struct Thing *thing)
{
    ThingModel crmodel = thing->model;
    if ((crmodel < 1) || (crmodel >= kfx_config_state.conf.crtr_conf.model_count))
        crmodel = 0;
    struct CreatureModelConfig* crconf = creature_stats_get(crmodel);
    return &crconf->creature_picked_up_offset;
}

unsigned char keepersprite_frames(int64_t n)
{
    if (n >= SIM_KEEPERSPRITE_ADD_OFFSET && n < SIM_KEEPERSPRITE_ADD_OFFSET + SIM_KEEPERSPRITE_ADD_NUM)
    {
        return anim_frames_add[n - SIM_KEEPERSPRITE_ADD_OFFSET];
    }
    if ((n >= 0) && (n < CREATURE_FRAMELIST_LENGTH) && anim_frames_known[n])
    {
        return anim_frames[n];
    }
    ERRORLOG("Frame %" PRIu64 " out of range", (uint64_t)(n));
    return 0;
}

void keepersprite_set_frames(int64_t n, unsigned char frames)
{
    if (n >= SIM_KEEPERSPRITE_ADD_OFFSET && n < SIM_KEEPERSPRITE_ADD_OFFSET + SIM_KEEPERSPRITE_ADD_NUM)
    {
        anim_frames_add[n - SIM_KEEPERSPRITE_ADD_OFFSET] = frames;
    }
    else if ((n >= 0) && (n < CREATURE_FRAMELIST_LENGTH))
    {
        anim_frames[n] = frames;
        anim_frames_known[n] = 1;
    }
}

void keepersprite_forget_frames(int64_t n)
{
    if (n >= SIM_KEEPERSPRITE_ADD_OFFSET && n < SIM_KEEPERSPRITE_ADD_OFFSET + SIM_KEEPERSPRITE_ADD_NUM)
    {
        anim_frames_add[n - SIM_KEEPERSPRITE_ADD_OFFSET] = 0;
    }
    else if ((n >= 0) && (n < CREATURE_FRAMELIST_LENGTH))
    {
        anim_frames[n] = 0;
        anim_frames_known[n] = 0;
    }
}

int64_t get_lifespan_of_animation(int64_t ani, int64_t speed)
{
    if (speed == 0)
    {
        WARNLOG("Animation %" PRId64 " has no speed value", (int64_t)(ani));
        return keepersprite_frames(ani);
    }
    return (keepersprite_frames(ani) << 8) / speed;
}

int64_t get_creature_model_graphics(int64_t crmodel, int64_t seq_idx)
{
    if (seq_idx >= CREATURE_GRAPHICS_INSTANCES)
    {
        ERRORLOG("Invalid model %" PRId64 " graphics sequence %" PRIu64, (int64_t)(crmodel), (uint64_t)(seq_idx));
        seq_idx = 0;
    }
    if ((crmodel < 0) || (crmodel >= kfx_config_state.conf.crtr_conf.model_count))
    {
        ERRORLOG("Invalid model %" PRId64 " graphics sequence %" PRIu64, (int64_t)(crmodel), (uint64_t)(seq_idx));
        crmodel = 0;
    }
    // Backward compatibility for custom creatures. Use the attack animation if the extra animation is undefined, return 0 if the attack animation is also undefined.
    if (kfx_config_state.conf.crtr_conf.creature_graphics[crmodel][seq_idx] < 0)
    {
        if ((seq_idx >= CGI_CastSpell) && (kfx_config_state.conf.crtr_conf.creature_graphics[crmodel][CGI_Attack] > 0))
        {
            return kfx_config_state.conf.crtr_conf.creature_graphics[crmodel][CGI_Attack];
        }
        return 0;
    }
    return kfx_config_state.conf.crtr_conf.creature_graphics[crmodel][seq_idx];
}

int64_t get_creature_anim(struct Thing *thing, int64_t seq_idx)
{
    int64_t idx = get_creature_model_graphics(thing->model, seq_idx);
    return render_get_td_animation_sprite(idx);
}

void untint_thing(struct Thing *thing)
{
    thing->tint_colour = 0;
    thing->rendering_flags &= ~(TRF_Tint_1|TRF_Tint_2);
}

/* colour is a palette INDEX (matches Thing::tint_colour, unsigned char),
 * not a resolved TbPixel -- see possession_hit_colours' comment in
 * player_data.c. */
void tint_thing(struct Thing *thing, unsigned char colour, unsigned char tint)
{
    thing->rendering_flags ^= (thing->rendering_flags ^ (tint << 2)) & (TRF_Tint_1|TRF_Tint_2);
    thing->tint_colour = colour;
}

TbBool update_creature_anim(struct Thing *thing, int64_t speed, int64_t seq_idx)
{
    uint64_t i = get_creature_anim(thing, seq_idx);
    // Only update when it's a different sprite, or a different animation speed.
    if (i != thing->anim_sprite)
    {
        set_thing_draw(thing, i, speed, -1, -1, 0, ODC_Default);
        return true;
    }
    if ((speed != thing->anim_speed) && (speed != -1))
    {
        thing->anim_speed = speed;
        return true;
    }
    return false;
}

TbBool update_creature_animation_by_sprite(struct Thing *thing, int64_t speed, int64_t anim_idx)
{
    uint64_t i = render_get_td_animation_sprite(anim_idx);
    // Only update when it's a different sprite, or a different animation speed.
    if ((i != thing->anim_sprite) || ((speed != thing->anim_speed) && (speed != -1)))
    {
        set_thing_draw(thing, i, speed, -1, -1, 0, ODC_Default);
        return true;
    }
    return false;
}

void update_creature_rendering_flags(struct Thing *thing)
{
    // Clear related flags
    thing->rendering_flags &= ~TRF_Invisible;
    thing->rendering_flags &= ~TRF_Transpar_Flags;
    thing->rendering_flags &= ~TRF_AnimateOnce;
    // Now set only those that should be
    if ( (is_thing_directly_controlled_by_player(thing, my_player_number)) || (is_thing_passenger_controlled_by_player(thing, my_player_number)) )
    {
        thing->rendering_flags |= TRF_Invisible;
    }
    if (thing_is_creature(thing))
    {
        struct CreatureModelConfig* crconf = creature_stats_get_from_thing(thing);
        if (crconf->transparency_flags != 0)
        {
            set_flag(thing->rendering_flags, crconf->transparency_flags);
        }
    }
    if (creature_is_invisible(thing))
    {
      if (is_my_player_number(thing->owner))
      {
          thing->rendering_flags &= ~TRF_Transpar_Flags;
          thing->rendering_flags |= TRF_Transpar_4;
      } else
      {
            thing->rendering_flags |= TRF_Invisible;
            struct PlayerInfo* player = get_my_player();
            struct Thing* creatng = thing_get(player->influenced_thing_idx);
            if ((creatng != thing) && (thing_is_creature(creatng)))
            {
                if ( (is_thing_directly_controlled_by_player(creatng, player->id_number)) || (is_thing_passenger_controlled_by_player(creatng, player->id_number)) )
                {
                    if (creature_can_see_invisible(creatng))
                    {
                        thing->rendering_flags &= ~TRF_Invisible;
                        thing->rendering_flags &= ~TRF_Transpar_Flags;
                        thing->rendering_flags |= TRF_Transpar_4;
                    }
                }
            }
      }
    }
}

void update_creature_graphic_anim(struct Thing *thing)
{
    int64_t i;

    TRACE_THING(thing);
    struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
    struct CreatureModelConfig* crconf = creature_stats_get_from_thing(thing);
    struct InstanceInfo* inst_inf;
    if ((thing->size_change & TSC_ChangeSize) != 0)
    {
      thing->size_change &= ~TSC_ChangeSize;
    } else
    if ((thing->active_state == CrSt_CreatureHeroEntering) && (cctrl->countdown >= 0))
    {
      thing->rendering_flags |= TRF_Invisible;
    } else
    if (!creature_under_spell_effect(thing, CSAfF_Chicken))
    {
        inst_inf = creature_instance_info_get(cctrl->instance_id);
        if (cctrl->instance_id != CrInst_NULL)
        {
          if (cctrl->instance_id == CrInst_TORTURED)
          {
              thing->rendering_flags &= ~(TRF_Transpar_Flags);
          }
          if (!creature_under_spell_effect(thing, CSAfF_Freeze))
          {
              update_creature_anim(thing, cctrl->instance_anim_step_turns, inst_inf->graphics_idx);
          }
          else
          {
              update_creature_anim(thing, 0, inst_inf->graphics_idx);
          }
        } else
        if ((cctrl->frozen_on_hit != 0) || creature_is_dying(thing) || creature_under_spell_effect(thing, CSAfF_Freeze))
        {
            update_creature_anim(thing, 256, CGI_GotHit);
        } else
        if (flag_is_set(cctrl->stateblock_flags, CCSpl_ChickenRel))
        {
            update_creature_anim(thing, 256, CGI_Stand);
        } else
        if (thing->active_state == CrSt_CreatureSlapCowers)
        {
            update_creature_anim(thing, 256, CGI_GotSlapped);
        } else
        if (thing->active_state == CrSt_CreatureRoar)
        {
            update_creature_anim(thing, 128, CGI_Roar);
        } else
        if (thing->active_state == CrSt_CreaturePiss)
        {
            update_creature_anim(thing, 128, CGI_Piss);
        } else
        if (thing->active_state == CrSt_CreatureUnconscious)
        {
            update_creature_anim(thing, 64, CGI_DropDead);
            thing->rendering_flags |= TRF_AnimateOnce;
        } else
        if (thing->active_state == CrSt_CreatureSleep)
        {
            thing->rendering_flags &= ~(TRF_Transpar_Flags);
            update_creature_anim(thing, 128, CGI_Sleep);
        } else
        if (cctrl->distance_to_destination == 0)
        {
            update_creature_anim(thing, crconf->walking_anim_speed, CGI_Stand);
        } else
        if (!thing_touching_floor(thing))
        {
            i = (((int64_t)cctrl->distance_to_destination) << 8) / (crconf->walking_anim_speed + 1);
            update_creature_anim(thing, i, CGI_Stand);
        } else
        if ((cctrl->dragtng_idx != 0) && (thing_get(cctrl->dragtng_idx)->state_flags & TF1_IsDragged1))
        {
            i = (((int64_t)cctrl->distance_to_destination) << 8) / (crconf->walking_anim_speed+1);
            update_creature_anim(thing, i, CGI_Drag);
        } else
        if (crconf->fixed_anim_speed)
        {
            update_creature_anim(thing, 256, CGI_Ambulate);
        } else
        {
            i = (((int64_t)cctrl->distance_to_destination) << 8) / (crconf->walking_anim_speed + 1);
            if (!update_creature_anim(thing, i, CGI_Ambulate))
            {
                thing->anim_speed = i;
            }
        }
    } else // chickened
    {
        thing->rendering_flags &= ~(TRF_Transpar_Flags);
        if (cctrl->distance_to_destination == 0)
        {
            update_creature_animation_by_sprite(thing, 256, 820);
        } else
        if (!thing_touching_floor(thing))
        {
            update_creature_animation_by_sprite(thing, 256, 820);
        } else
        if (crconf->fixed_anim_speed)
        {
            update_creature_animation_by_sprite(thing, 256, 819);
        } else
        {
            i = (((int64_t)cctrl->distance_to_destination) << 8) / (crconf->walking_anim_speed+1);
            if (!update_creature_animation_by_sprite(thing, i, 819))
            {
                thing->anim_speed = i;
            }
        }
    }
}


void update_creature_graphic_tint(struct Thing *thing)
{
    struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
    if (creature_under_spell_effect(thing, CSAfF_Freeze))
    {
        // tint level 2 (TRF_Tint_2) -> renderer applies the stronger blend, so
        // the pale-blue freeze tint stays visible in the true-colour renderer.
        tint_thing(thing, kfx_sim_state.colours[4][4][15], 2);
    } else
    if (((cctrl->combat_flags & CmbtF_Melee) == 0) && ((cctrl->combat_flags & CmbtF_Ranged) == 0))
    {
        untint_thing(thing);
    } else
    if (((get_gameturn() % 3) == 0) || is_hero_thing(thing))
    {
        untint_thing(thing);
    } else
    {
        tint_thing(thing, possession_hit_colours[get_player_color_idx(thing->owner)], 1);
    }
}

void set_creature_graphic(struct Thing *thing)
{
    update_creature_rendering_flags(thing);
    update_creature_graphic_anim(thing);
    // Update tint
    update_creature_graphic_tint(thing);
}

void init_censorship(void)
{
  if ( censorship_enabled() )
  {
    // Modification for Dark Mistress
      set_creature_model_graphics(20, 14, 48);
  }
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
