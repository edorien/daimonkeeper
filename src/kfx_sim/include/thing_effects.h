/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file thing_effects.h
 *     Header file for thing_effects.c.
 * @par Purpose:
 *     Effect generators and effect elements support functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     01 Jan 2010 - 12 Jan 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_TNGEFFECT_H
#define DK_TNGEFFECT_H

#include "globals.h"
#include "bflib_basics.h"

#include "map_data.h"
#include "thing_list.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
// enum ThingHitTypes, enum ThingEffectKind, and enum ThingEffectElements
// moved to globals.h (stage 13) so config_effects.c/config_magic.c/
// config_trapdoor.c can reference the THit_*/TngEff_*/TngEffElm_*
// vocabulary without depending on this simulation header -- see
// docs/refactor/stage-13-enforce-and-document.md.

enum AreaAffectTypes {
    AAffT_None = 0,
    AAffT_GasDamage,
    AAffT_GasDamageEffect,
    AAffT_GasEffect,
    AAffT_WOPDamage,
};


/******************************************************************************/
#pragma pack(1)

struct Thing;

#pragma pack()
/******************************************************************************/
extern const int64_t birth_effect_element[];
/******************************************************************************/
struct EffectElementConfigStats *get_effect_element_model_stats(ThingModel tngmodel);

TbBool thing_is_effect(const struct Thing *thing);
struct Thing *create_effect(const struct Coord3d *pos, ThingModel effmodel, PlayerNumber owner);
struct Thing *create_effect_generator(struct Coord3d *pos, ThingModel model, int64_t range, int64_t owner, int64_t parent_idx);
struct Thing *create_effect_element(const struct Coord3d *pos, ThingModel eelmodel, PlayerNumber owner);
struct Thing* create_used_effect_or_element(const struct Coord3d* pos, EffectOrEffElModel effect_id, PlayerNumber plyr_idx, ThingIndex parent_idx);
TngUpdateRet update_effect_element(struct Thing *thing);
TngUpdateRet update_effect(struct Thing *thing);
TngUpdateRet process_effect_generator(struct Thing *thing);
void process_spells_affected_by_effect_elements(struct Thing *thing);
TbBool destroy_effect_thing(struct Thing *thing);
struct Thing *create_price_effect(const struct Coord3d *pos, int64_t plyr_idx, int64_t price);
void process_fx_lines();
struct Thing *script_create_effect(struct Coord3d *pos, EffectOrEffElModel mdl, int64_t val);
void create_effects_line(TbMapLocation from, TbMapLocation to, char curvature, unsigned char spatial_stepping, unsigned char temporal_stepping, EffectOrEffElModel effct_id);

TbBool area_effect_can_affect_thing(const struct Thing *thing, HitTargetFlags hit_targets, PlayerNumber shot_owner);
TbBool effect_can_affect_thing(struct Thing *efftng, struct Thing *thing);
int64_t explosion_affecting_area(struct Thing *tngsrc, const struct Coord3d *pos, MapCoord max_dist,
    HitPoints max_damage, int64_t blow_strength, HitTargetFlags hit_targets);
    
TbBool explosion_affecting_door(struct Thing *tngsrc, struct Thing *tngdst, const struct Coord3d *pos,
    MapCoordDelta max_dist, HitPoints max_damage, int64_t blow_strength, PlayerNumber owner);

void give_shooter_drained_health(struct Thing *shooter, HitPoints health_delta);
void process_keeper_spell_aura(struct Thing *thing);
void affect_nearby_stuff_with_vortex(struct Thing *thing);
void affect_nearby_friends_with_alarm(struct Thing *traptng);

TngUpdateRet damage_creatures_with_physical_force(struct Thing *thing, ModTngFilterParam param);
TbBool valid_cave_in_position(PlayerNumber plyr_idx, MapSubtlCoord stl_x, MapSubtlCoord stl_y);
int64_t update_cave_in(struct Thing *thing);

void draw_flame_breath(struct Coord3d *pos1, struct Coord3d *pos2, int64_t delta_step, int64_t num_per_step, int64_t ef_or_efel_model, ThingIndex parent_idx);
void draw_lightning(const struct Coord3d *pos1, const struct Coord3d *pos2, int64_t eeinterspace, EffectOrEffElModel ef_or_efel_model);

extern unsigned char temp_pal[768];
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
