/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file light_registry.c
 *     The light registry: creating, moving and deleting lights.
 * @par Purpose:
 *     Moved from kfx_render's light_data.c (refactor pass 2, S11,
 *     docs/refactor-pass2/stage-11-lighting-split.md). The lights live in
 *     kfx_sim_state.light_registry; the shading built from them stays in
 *     kfx_render, which learns what to rebuild from light_shading_signals.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     11 Mar 2010 - 12 May 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "light_registry.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_math.h"

#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "thing_data.h"
#include "thing_list.h"
#include "thing_stats.h"
#include "value_util.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct LightShadingSignals light_shading_signals;

/******************************************************************************/
struct Light *light_allocate_light(void)
{
    for (int64_t i = 1; i < LIGHTS_COUNT; i++)
    {
        struct Light* lgt = &kfx_sim_state.light_registry.lights[i];
        if ((lgt->flags & LgtF_Allocated) == 0)
        {
            lgt->flags |= LgtF_Allocated;
            lgt->index = i;
            return lgt;
        }
    }
    return NULL;
}

int64_t light_count_lights()
{
    int64_t cnt = 0;
    for (int64_t i = 1; i < LIGHTS_COUNT; i++)
    {
        struct Light *lgt = &kfx_sim_state.light_registry.lights[i];
        if (lgt->flags & LgtF_Allocated)
        {
            cnt++;
        }
    }
    return cnt;
}

void light_free_light(struct Light *lgt)
{
    memset(lgt, 0, sizeof(struct Light));
}

TbBool light_is_invalid(const struct Light *lgt)
{
    if (lgt == NULL)
        return true;
    if ((lgt < &kfx_sim_state.light_registry.lights[1]) || (lgt > &kfx_sim_state.light_registry.lights[LIGHTS_COUNT-1]))
        return true;
    return false;
}

/**
 * Hands out one of kfx_render's shadow caches for a dynamic light.
 * @return The cache's index, or 0 if all are taken.
 */
int64_t light_allocate_shadow_cache(void)
{
    for (int64_t i = 1; i < SHADOW_CACHE_COUNT; i++)
    {
        if (kfx_sim_state.light_registry.shadow_cache_used[i] == 0)
        {
            kfx_sim_state.light_registry.shadow_cache_used[i] = 1;
            return i;
        }
    }
    return 0;
}

void light_free_shadow_cache(int64_t shadow_index)
{
    if ((shadow_index > 0) && (shadow_index < SHADOW_CACHE_COUNT))
        kfx_sim_state.light_registry.shadow_cache_used[shadow_index] = 0;
}

TbBool light_add_light_to_list(struct Light *lgt, struct StructureList *list)
{
  if (flag_is_set(lgt->flags2,LgtF2_InList))
  {
    ERRORLOG("Light is already in list");
    return false;
  }
  list->count++;
  set_flag(lgt->flags2,LgtF2_InList);
  lgt->next_in_list = list->index;
  list->index = lgt->index;
  return true;
}

int64_t light_create_light(struct InitLight *ilght)
{
    struct Light* lgt = light_allocate_light();
    if (light_is_invalid(lgt)) {
        return 0;
    }
    if (ilght->is_dynamic)
    {
        int64_t shadow_index = light_allocate_shadow_cache();
        if (shadow_index == 0)
        {
            ERRORDBG(11,"Cannot allocate cache for dynamic light");
            light_free_light(lgt);
            return 0;
        }
        kfx_sim_state.light_registry.total_dynamic_lights++;
        lgt->shadow_index = shadow_index;
        light_add_light_to_list(lgt, &kfx_sim_state.thing_lists[TngList_DynamLights]);
    } else
    {
        kfx_sim_state.light_registry.total_stat_lights++;
        light_add_light_to_list(lgt, &kfx_sim_state.thing_lists[TngList_StaticLights]);
        kfx_sim_state.light_registry.stat_light_needs_updating = 1;
    }
    lgt->flags |= LgtF_CanTurnOff;
    lgt->flags |= LgtF_NeedUpdate;
    lgt->mappos.x.val = ilght->mappos.x.val;
    lgt->mappos.y.val = ilght->mappos.y.val;
    lgt->mappos.z.val = ilght->mappos.z.val;
    lgt->radius = ilght->radius;
    lgt->intensity = ilght->intensity;
    lgt->colour_r = ilght->colour_r; lgt->colour_g = ilght->colour_g; lgt->colour_b = ilght->colour_b;
    lgt->flags2 |= ilght->flags << 1;
    lgt->reset_interpolation = true;
    lgt->last_turn_moved = 0;

    set_flag_value(lgt->flags, LgtF_Dynamic, ilght->is_dynamic);
    lgt->attached_slb = ilght->attached_slb;
    return lgt->index;
}

TbBool light_create_light_adv(VALUE *init_data)
{
    struct Light* lgt = light_allocate_light();
    if (light_is_invalid(lgt)) {
        return false;
    }
    if (value_coerce_bool(value_dict_get(init_data, "Dynamic")))
    {
        int64_t shadow_index = light_allocate_shadow_cache();
        if (shadow_index == 0)
        {
            ERRORDBG(11,"Cannot allocate cache for dynamic light");
            light_free_light(lgt);
            return false;
        }
        kfx_sim_state.light_registry.total_dynamic_lights++;
        lgt->shadow_index = shadow_index;
        light_add_light_to_list(lgt, &kfx_sim_state.thing_lists[TngList_DynamLights]);
        set_flag(lgt->flags, LgtF_Dynamic);
    }
    else
    {
        kfx_sim_state.light_registry.total_stat_lights++;
        light_add_light_to_list(lgt, &kfx_sim_state.thing_lists[TngList_StaticLights]);
        kfx_sim_state.light_registry.stat_light_needs_updating = 1;
        clear_flag(lgt->flags, LgtF_Dynamic);
    }
    lgt->flags |= LgtF_CanTurnOff;
    lgt->flags |= LgtF_NeedUpdate;
    lgt->mappos.x.val = value_read_stl_coord(value_dict_get(init_data, "SubtileX"));
    lgt->mappos.y.val = value_read_stl_coord(value_dict_get(init_data, "SubtileY"));
    lgt->mappos.z.val = value_read_stl_coord(value_dict_get(init_data, "SubtileZ"));
    lgt->radius = value_read_stl_coord(value_dict_get(init_data, "LightRange"));;
    lgt->intensity = value_uint32(value_dict_get(init_data, "LightIntensity"));
    lgt->attached_slb = value_uint32(value_dict_get(init_data, "ParentTile"));
    lgt->colour_r = value_uint32(value_dict_get(init_data, "LightRed"));
    lgt->colour_g = value_uint32(value_dict_get(init_data, "LightGreen"));
    lgt->colour_b = value_uint32(value_dict_get(init_data, "LightBlue"));
    lgt->reset_interpolation = true;
    lgt->last_turn_moved = 0;

    /*
     * TODO: not implemented yet
    unsigned long k = 2 * ilght->flags;
    lgt->flags2 = k ^ ((k ^ lgt->flags2) & 0x01);

    lgt->attached_slb = ilght->attached_slb;
     */

    return true;
}

TbBool lights_stats_debug_dump(void)
{
    int64_t lights[LIGHTS_COUNT];
    int64_t lgh_things[THING_CLASSES_COUNT];
    int64_t shadowcs[SHADOW_CACHE_COUNT];
    int64_t i;
    for (i=0; i < SHADOW_CACHE_COUNT; i++)
    {
        if (kfx_sim_state.light_registry.shadow_cache_used[i] != 0)
            shadowcs[i] = -1;
        else
            shadowcs[i] = 0;
    }
    int64_t lgh_sttc = 0;
    int64_t lgh_dynm = 0;
    for (i=0; i < LIGHTS_COUNT; i++)
    {
        struct Light* lgt = &kfx_sim_state.light_registry.lights[i];
        if ((lgt->flags & LgtF_Allocated) != 0)
        {
            lights[i] = -1;
            if ((lgt->flags & LgtF_Dynamic) != 0)
                lgh_dynm++;
            else
                lgh_sttc++;
            if ( (lgt->shadow_index > 0) && (lgt->shadow_index < SHADOW_CACHE_COUNT) )
            {
                if (shadowcs[lgt->shadow_index] == -1) {
                    shadowcs[lgt->shadow_index] = i;
                } else
                if (shadowcs[lgt->shadow_index] == 0) {
                    WARNLOG("Shadow Cache %" PRId64 " is not allocated, but used by light %" PRId64 "!",(int64_t)lgt->shadow_index,(int64_t)i);
                } else {
                    WARNLOG("Shadow Cache %" PRId64 " is double-allocated, for lights %" PRId64 " and %" PRId64 "!",(int64_t)lgt->shadow_index,(int64_t)shadowcs[lgt->shadow_index],(int64_t)i);
                }
            } else
            if ((lgt->flags & LgtF_Dynamic) != 0)
            {
                WARNLOG("Dynamic light %" PRId64 " has bad Shadow Cache %" PRId64 "!",(int64_t)i,(int64_t)lgt->shadow_index);
            }
        } else {
            lights[i] = 0;
        }
    }
    for (i=1; i < THINGS_COUNT; i++)
    {
        struct Thing* thing = thing_get(i);
        if (thing_exists(thing))
        {
            if ((thing->light_id > 0) && (thing->light_id < LIGHTS_COUNT))
            {
                int64_t n = 1000 + (int64_t)thing->class_id;
                if (lights[thing->light_id] == -1) {
                    lights[thing->light_id] = n;
                } else
                if (lights[thing->light_id] == 0) {
                    WARNLOG("Light %" PRId64 " is not allocated, but used by %s!",(int64_t)thing->light_id, thing_model_name(thing));
                } else {
                    WARNLOG("Light %" PRId64 " is double-allocated, for %" PRId64 " and %" PRId64 "!",(int64_t)thing->light_id, (int64_t)lights[thing->light_id], (int64_t)n);
                }
            }

        }
    }
    int64_t lgh_used = 0;
    int64_t lgh_free = 0;
    for (i=0; i < THING_CLASSES_COUNT; i++)
        lgh_things[i] = 0;
    for (i=0; i < LIGHTS_COUNT; i++)
    {
        if (lights[i] != 0)
        {
            lgh_used++;
            if ((lights[i] > 1000) && (lights[i] < 1000+THING_CLASSES_COUNT))
                lgh_things[lights[i]-1000]++;
        } else
        {
            lgh_free++;
        }
    }
    int64_t shdc_free = 0;
    int64_t shdc_used = 0;
    int64_t shdc_linked = 0;
    for (i=0; i < SHADOW_CACHE_COUNT; i++)
    {
        if (shadowcs[i] != 0)
        {
            shdc_used++;
            if (shadowcs[i] > 0)
                shdc_linked++;
        } else {
            shdc_free++;
        }
    }
    SYNCLOG("Lights: %" PRId64 " free, %" PRId64 " used; %" PRId64 " static, %" PRId64 " dynamic; for things:%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64,(int64_t)(lgh_free),(int64_t)(lgh_used),(int64_t)(lgh_sttc),(int64_t)(lgh_dynm),(int64_t)(lgh_things[1]),(int64_t)(lgh_things[2]),(int64_t)(lgh_things[3]),(int64_t)(lgh_things[4]),(int64_t)(lgh_things[5]),(int64_t)(lgh_things[6]),(int64_t)(lgh_things[7]),(int64_t)(lgh_things[8]),(int64_t)(lgh_things[9]),(int64_t)(lgh_things[10]),(int64_t)(lgh_things[11]),(int64_t)(lgh_things[12]),(int64_t)(lgh_things[13]));
    if ((shdc_used != shdc_linked) || (shdc_used != lgh_dynm))
    {
        WARNLOG("Amount of shadow cache mismatches: %" PRId64 " free, %" PRId64 " used, %" PRId64 " linked to lights, %" PRId64 " dyn. lights.",
          (int64_t)(shdc_free),(int64_t)(shdc_used),(int64_t)(shdc_linked),(int64_t)(kfx_sim_state.light_registry.total_dynamic_lights));
    }
    if (lgh_sttc != kfx_sim_state.light_registry.total_stat_lights)
    {
        WARNLOG("Wrong global lights counter: %" PRId64 " static lights and counter says %" PRId64 ".",(int64_t)(lgh_sttc),(int64_t)(kfx_sim_state.light_registry.total_stat_lights));
    }
    if (lgh_dynm != kfx_sim_state.light_registry.total_dynamic_lights)
    {
        WARNLOG("Wrong global lights counter: %" PRId64 " dynamic lights and counter says %" PRId64 ".",(int64_t)(lgh_dynm),(int64_t)(kfx_sim_state.light_registry.total_dynamic_lights));
    }
    return false;
}

void light_set_light_never_cache(int64_t lgt_id)
{
    if (lgt_id <= 0 || lgt_id >= LIGHTS_COUNT)
    {
        ERRORLOG("Attempt to set size of invalid light %" PRId64,(int64_t)lgt_id);
        return;
    }
    struct Light* lgt = &kfx_sim_state.light_registry.lights[lgt_id];
    if ((lgt->flags & LgtF_Allocated) == 0)
    {
        ERRORLOG("Attempt to set size of unallocated light structure %" PRId64,(int64_t)lgt_id);
        return;
    }
    lgt->flags |= LgtF_NeverCached;
}

int64_t light_is_light_allocated(int64_t lgt_id)
{
    if (lgt_id <= 0 || lgt_id >= LIGHTS_COUNT)
        return false;
    struct Light* lgt = &kfx_sim_state.light_registry.lights[lgt_id];
    if ((lgt->flags & LgtF_Allocated) == 0)
        return false;
    return true;
}

void light_reset_interpolation(int64_t lgt_id)
{
    struct Light *lgt = &kfx_sim_state.light_registry.lights[lgt_id];
    lgt->reset_interpolation = true;
}

void light_set_light_position(int64_t lgt_id, struct Coord3d *pos)
{
  struct Light *lgt = &kfx_sim_state.light_registry.lights[lgt_id];

  if (get_gameturn() > lgt->last_turn_moved)
      lgt->previous_mappos = lgt->mappos;
  lgt->last_turn_moved = get_gameturn();

  if ( pos->x.val != lgt->mappos.x.val
    || pos->y.val != lgt->mappos.y.val
    || pos->z.val != lgt->mappos.z.val )
  {
    if ( (lgt->flags & LgtF_Dynamic) == 0 )
    {
      kfx_sim_state.light_registry.stat_light_needs_updating = 1;
      unsigned char range = lgt->range;
      int64_t end_y = lgt->mappos.y.stl.num + range;
      int64_t end_x = lgt->mappos.x.stl.num + range;
      if ( end_y > kfx_sim_state.map_subtiles_y )
      {
        end_y = kfx_sim_state.map_subtiles_y;
      }
      if ( end_x > kfx_sim_state.map_subtiles_x )
      {
        end_x = kfx_sim_state.map_subtiles_x;
      }
      int64_t beg_y = lgt->mappos.y.stl.num - range;
      if ( beg_y < 0 )
      {
        beg_y = 0;
      }
      int64_t beg_x = lgt->mappos.x.stl.num - range;
      if ( beg_x < 0 )
      {
        beg_x = 0;
      }
      light_signal_stat_light_update_in_area(beg_x, beg_y, end_x, end_y);
    }
    lgt->mappos.x.val = pos->x.val;
    lgt->mappos.y.val = pos->y.val;
    lgt->mappos.z.val = pos->z.val;
    lgt->flags |= LgtF_NeedUpdate;
  }
}

void light_remove_light_from_list(struct Light *lgt, struct StructureList *list)
{
  if ( list->count == 0 )
  {
      ERRORLOG("List %" PRIu64 " has no structures", (uint64_t)(list->index));
      return;
  }
  TbBool Removed = false;
  struct Light *lgt2;
  struct Light *i;
  if ( flag_is_set(lgt->flags2,LgtF2_InList) )
  {
    if ( lgt->index == list->index )
    {
      Removed = true;
      list->count--;
      list->index = lgt->next_in_list;
      lgt->next_in_list = 0;
      clear_flag(lgt->flags2,LgtF2_InList);
    }
    else
    {
      lgt2 = &kfx_sim_state.light_registry.lights[list->index];
      for ( i = 0; lgt2 != kfx_sim_state.light_registry.lights; lgt2 = &kfx_sim_state.light_registry.lights[lgt2->next_in_list] )
      {
        if ( lgt2 == lgt )
        {
          Removed = true;
          if ( i )
          {
            i->next_in_list = lgt->next_in_list;
            clear_flag(lgt->flags2,LgtF2_InList);
            list->count--;
            lgt->next_in_list = 0;
          }
          else
          {
            ERRORLOG("No prev when removing light from list");
          }
        }
        i = lgt2;
      }
    }
    if ( !Removed )
    {
      ERRORLOG("Could not find light %" PRId64 " in list", (int64_t)(lgt->index));
    }
  }
}

/**
 * Records a static light map area for kfx_render to clear before it next
 * shades (see struct LightShadingSignals). Used to clear it right here, which
 * reached into kfx_render.
 */
static void light_signal_stat_light_map_area(int64_t x1, int64_t y1, int64_t x2, int64_t y2)
{
    struct LightShadingSignals *sig = &light_shading_signals;
    if (sig->areas_overflowed)
        return;
    if (sig->areas_count > 0)
    {
        const struct LightShadingArea *last = &sig->areas[sig->areas_count - 1];
        if ((last->x1 == x1) && (last->y1 == y1) && (last->x2 == x2) && (last->y2 == y2))
            return;
    }
    if (sig->areas_count >= LIGHT_SHADING_AREAS_MAX)
    {
        sig->areas_overflowed = true;
        sig->areas_count = 0;
        return;
    }
    struct LightShadingArea *area = &sig->areas[sig->areas_count++];
    area->x1 = x1;
    area->y1 = y1;
    area->x2 = x2;
    area->y2 = y2;
}

void light_signal_stat_light_update_in_area(int64_t x1, int64_t y1, int64_t x2, int64_t y2)
{
  int64_t i = 0;
  struct Light *lgt = &kfx_sim_state.light_registry.lights[1];
  do
  {
    if ( lgt->flags & LgtF_Allocated )
    {
      if ( !(lgt->flags & LgtF_Dynamic) )
      {
        unsigned char range = lgt->range;
        MapSubtlCoord x = lgt->mappos.x.stl.num;
        MapSubtlCoord y = lgt->mappos.y.stl.num;
        if ( range + x >= x1 && x - range <= x2 && range + y >= y1 && y - range <= y2 )
        {
          kfx_sim_state.light_registry.stat_light_needs_updating = 1;
          i++;
          lgt->flags |= LgtF_NeedUpdate;
          lgt->flags &= ~LgtF_OutOfDate;
        }
      }
    }
    lgt++;
  }
  while ( lgt < &kfx_sim_state.light_registry.lights[LIGHTS_COUNT] );
  if ( i )
    light_signal_stat_light_map_area(x1, y1, x2, y2);
}

void light_signal_update_in_area(int64_t sx, int64_t sy, int64_t ex, int64_t ey)
{
  struct Light *lgt = &kfx_sim_state.light_registry.lights[1];
  do
  {
    if ( lgt->flags & LgtF_Allocated )
    {
      if ( lgt->flags & LgtF_Dynamic )
      {
        unsigned char range = lgt->range;;
        MapSubtlCoord x = lgt->mappos.x.stl.num;
        MapSubtlCoord y = lgt->mappos.y.stl.num;
        if ( range + x >= sx && x - range <= ex && range + y >= sy && y - range <= ey )
          lgt->flags |= LgtF_NeedUpdate;
      }
    }
    lgt++;
  }
  while ( lgt < &kfx_sim_state.light_registry.lights[LIGHTS_COUNT] );
  light_signal_stat_light_update_in_area(sx, sy, ex, ey);
}

static void light_signal_stat_light_update_in_own_radius(struct Light *lgt)
{
    int64_t radius = lgt->range;
    int64_t end_y = (int64_t)lgt->mappos.y.stl.num + radius;
    if (end_y >= kfx_sim_state.map_subtiles_y)
        end_y = kfx_sim_state.map_subtiles_y;
    int64_t end_x = (int64_t)lgt->mappos.x.stl.num + radius;
    if (end_x >= kfx_sim_state.map_subtiles_x)
        end_x = kfx_sim_state.map_subtiles_x;
    int64_t start_y = (int64_t)lgt->mappos.y.stl.num - radius;
    if (start_y <= 0)
        start_y = 0;
    int64_t start_x = (int64_t)lgt->mappos.x.stl.num - radius;
    if (start_x <= 0)
      start_x = 0;
    if ((end_x <= start_x) || (end_y <= start_y))
        return;
    light_signal_stat_light_update_in_area(start_x, start_y, end_x, end_y);
}

void light_turn_light_off(int64_t idx)
{
    if ((idx <= 0) || (idx >= LIGHTS_COUNT)) {
        ERRORLOG("Attempt to turn off light %" PRId64,(int64_t)idx);
        return;
    }
    struct Light* lgt = &kfx_sim_state.light_registry.lights[idx];
    if ((lgt->flags & LgtF_Allocated) == 0) {
        ERRORLOG("Attempt to turn off unallocated light structure");
        return;
    }
    if ((lgt->flags & LgtF_CanTurnOff) == 0) {
        return;
    }
    lgt->flags &= ~LgtF_CanTurnOff;
    if ((lgt->flags & LgtF_Dynamic) != 0) {
        light_remove_light_from_list(lgt, &kfx_sim_state.thing_lists[TngList_DynamLights]);
    } else {
        light_signal_stat_light_update_in_own_radius(lgt);
        light_remove_light_from_list(lgt, &kfx_sim_state.thing_lists[TngList_StaticLights]);
        kfx_sim_state.light_registry.stat_light_needs_updating = 1;
    }
}

void light_turn_light_on(int64_t idx)
{
    if ((idx <= 0) || (idx >= LIGHTS_COUNT)) {
        ERRORLOG("Attempt to turn on light %" PRId64,(int64_t)idx);
        return;
    }
    struct Light* lgt = &kfx_sim_state.light_registry.lights[idx];
    if ((lgt->flags & LgtF_Allocated) == 0) {
        ERRORLOG("Attempt to turn on unallocated light structure %" PRId64,(int64_t)idx);
        return;
    }
    if ((lgt->flags & LgtF_CanTurnOff) != 0) {
        return;
    }
    lgt->flags |= LgtF_CanTurnOff;
    lgt->reset_interpolation = true;
    if ((lgt->flags & LgtF_Dynamic) != 0)
    {
        light_add_light_to_list(lgt, &kfx_sim_state.thing_lists[TngList_DynamLights]);
        lgt->flags |= LgtF_NeedUpdate;
    } else
    {
        light_add_light_to_list(lgt, &kfx_sim_state.thing_lists[TngList_StaticLights]);
        kfx_sim_state.light_registry.stat_light_needs_updating = 1;
        lgt->flags |= LgtF_NeedUpdate;
    }
}

unsigned char light_get_light_intensity(int64_t idx)
{
  if ( idx )
  {
    if ( kfx_sim_state.light_registry.lights[idx].flags & LgtF_Allocated )
    {
      return kfx_sim_state.light_registry.lights[idx].intensity;
    }
    else
    {
      ERRORLOG("Attempt to get intensity of unallocated light structure");
      return 0;
    }
  }
  else
  {
    ERRORLOG("Attempt to get intensity of light 0");
    return 0;
  }
}

void light_set_light_intensity(int64_t idx, unsigned char intensity)
{
  struct Light *lgt = &kfx_sim_state.light_registry.lights[idx];
  int64_t x1,x2,y1,y2;
  if ( !light_is_invalid(lgt) )
  {
    if ((lgt->flags & LgtF_Allocated) != 0)
    {
      if ( lgt->intensity != intensity )
      {
        if ((lgt->flags & LgtF_Dynamic) == 0)
        {
          y2 = lgt->mappos.y.stl.num + lgt->range;
          if ( y2 > kfx_sim_state.map_subtiles_y )
            y2 = kfx_sim_state.map_subtiles_y;
          x2 = lgt->mappos.x.stl.num + lgt->range;
          if ( x2 > kfx_sim_state.map_subtiles_x )
            x2 = kfx_sim_state.map_subtiles_x;
          y1 = lgt->mappos.y.stl.num - lgt->range;
          if ( y1 < 0 )
            y1 = 0;
          x1 = lgt->mappos.x.stl.num - lgt->range;
          if ( x1 < 0 )
            x1 = 0;
          light_signal_stat_light_update_in_area(x1, y1, x2, y2);
          kfx_sim_state.light_registry.stat_light_needs_updating = 1;
        }
        lgt->intensity = intensity;
        if ( lgt->min_intensity < intensity )
          lgt->flags |= LgtF_NeedUpdate;
      }
    }
    else
    {
      ERRORLOG("Attempt to set intensity of unallocated light structure");
    }
  }
  else
  {
    ERRORLOG("Attempt to set intensity of invalid light");
  }
}

// Moved from kfx_sim's thing_creature.c (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- wraps direct
// kfx_sim_state.light_registry.lights[idx].radius reads/writes so kfx_sim doesn't need
// struct Light's full definition, just the callback.
int64_t light_get_light_radius(int64_t idx)
{
    return kfx_sim_state.light_registry.lights[idx].radius;
}

void light_set_light_radius(int64_t idx, int64_t radius)
{
    struct Light *lgt = &kfx_sim_state.light_registry.lights[idx];
    lgt->radius = radius;
}

void light_delete_light(int64_t idx)
{
    if ((idx <= 0) || (idx >= LIGHTS_COUNT)) {
        ERRORLOG("Attempt to delete light %" PRId64,(int64_t)idx);
        return;
    }
    struct Light* lgt = &kfx_sim_state.light_registry.lights[idx];
    if ((lgt->flags & LgtF_Allocated) == 0) {
        ERRORLOG("Attempt to delete unallocated light structure %" PRId64,(int64_t)idx);
        return;
    }
    if (lgt->shadow_index > 0)
    {
        light_free_shadow_cache(lgt->shadow_index);
    }
    if ((lgt->flags & LgtF_Dynamic) != 0)
    {
        kfx_sim_state.light_registry.total_dynamic_lights--;
        light_remove_light_from_list(lgt, &kfx_sim_state.thing_lists[TngList_DynamLights]);
    } else
    {
        kfx_sim_state.light_registry.total_stat_lights--;
        light_signal_stat_light_update_in_own_radius(lgt);
        light_remove_light_from_list(lgt, &kfx_sim_state.thing_lists[TngList_StaticLights]);
    }
    light_free_light(lgt);
}

void light_initialise(void)
{
    int64_t i;
    for (i=0; i < LIGHTS_COUNT; i++)
    {
        struct Light* lgt = &kfx_sim_state.light_registry.lights[i];
        if ((lgt->flags & LgtF_Allocated) != 0)
            light_delete_light(lgt->index);
    }
    // kfx_render builds its lighting tables itself, the first time it
    // shades (light_data.c's light_initialise_shading()).
    kfx_sim_state.light_registry.stat_light_needs_updating = 1;
    kfx_sim_state.light_registry.total_dynamic_lights = 0;
    kfx_sim_state.light_registry.total_stat_lights = 0;
}

void light_set_lights_on(char state)
{
    SYNCDBG(8, "Starting");
    if (state)
    {
        // Game rule
        kfx_sim_state.light_registry.global_ambient_light = kfx_config_state.conf.rules[0].gameplay.global_ambient_light;
        kfx_sim_state.light_registry.light_enabled = kfx_config_state.conf.rules[0].gameplay.light_enabled;
        kfx_sim_state.light_registry.light_auto_sync = true;
    } else
    {
        // Fullbright
        kfx_sim_state.light_registry.global_ambient_light = 32;
        kfx_sim_state.light_registry.light_enabled = 0;
        kfx_sim_state.light_registry.light_auto_sync = false;
    }

    light_request_stat_refresh();
}

void light_init_dungeon_heart(int64_t lgt_id, int64_t min_radius, int64_t min_intensity)
{
  struct Light *lgt;
  if ( lgt_id )
  {
    lgt = &kfx_sim_state.light_registry.lights[lgt_id];
    if ( lgt->flags & LgtF_Allocated )
    {
      if ( lgt->flags & LgtF_CanTurnOff )
      {
        lgt->flags &= ~LgtF_CanTurnOff;
        if ( lgt->flags & LgtF_Dynamic )
        {
          lgt->min_radius = min_radius;
          lgt->min_intensity = min_intensity;
        }
        else
        {
          ERRORLOG("Attempt to set_minimum light size to cache on non dynamic light");
        }
      }
    }
    else
    {
      ERRORLOG("Attempt to set minimum light size for unallocated light structure");
    }
  }
  else
  {
    ERRORLOG("Attempt to set minimum light size for light 0");
  }
}

// Called by kfx_sim's map_blocks.c/thing_list.c, which don't need struct Light/struct LightsShadows visible by
// value (stage 13.3, docs/refactor/stage-13-enforce-and-document.md).
void light_set_attached_slab(int64_t lgt_id, SlabCodedCoords slb_num)
{
    kfx_sim_state.light_registry.lights[lgt_id].attached_slb = slb_num;
}

void delete_lights_attached_to_slab_in_area(SlabCodedCoords place_slbnum,
    MapSubtlCoord start_stl_x, MapSubtlCoord start_stl_y,
    MapSubtlCoord end_stl_x, MapSubtlCoord end_stl_y)
{
    int64_t i;
    uint64_t k;
    i = kfx_sim_state.thing_lists[TngList_StaticLights].index;
    k = 0;
    while (i > 0)
    {
        struct Light *lgt;
        lgt = &kfx_sim_state.light_registry.lights[i];
        i = lgt->next_in_list;
        int64_t lgtstl_x = lgt->mappos.x.stl.num;
        int64_t lgtstl_y = lgt->mappos.y.stl.num;
        if (lgt->attached_slb == place_slbnum)
        {
            if ((lgtstl_x >= start_stl_x) && (lgtstl_x <= end_stl_x) && (lgtstl_y >= start_stl_y) && (lgtstl_y <= end_stl_y))
            {
                light_delete_light(lgt->index);
            }
        }
        k++;
        if (k > LIGHTS_COUNT)
        {
            ERRORLOG("Infinite loop detected when sweeping lights list");
            break;
        }
    }
}

TbBool light_get_lights_enabled(void)
{
    return kfx_sim_state.light_registry.light_enabled;
}

/**
 * Rebuild the whole static light map: what light_stat_refresh() did from the
 * registry's side -- clear it all, then mark every static light for shading.
 */
void light_request_stat_refresh(void)
{
    // Enable lights on all but bounding subtiles
    light_signal_stat_light_map_area(0, 0, kfx_sim_state.map_subtiles_x, kfx_sim_state.map_subtiles_y);
    light_signal_stat_light_update_in_area(1, 1, kfx_sim_state.map_subtiles_x, kfx_sim_state.map_subtiles_y);
}

/**
 * Reset every subtile's lightness before the next shading (a level's map was
 * loaded or cleared). Was ConfigReloadCallbacks.clear_subtiles_lightness.
 */
void light_request_lightness_reset(void)
{
    light_shading_signals.reset_lightness = true;
}

/**
 * The registry was replaced wholesale (a saved game loaded, a network
 * resync): nothing kfx_render has cached for it can be trusted, so it
 * rebuilds all of it (see LightShadingSignals.registry_replaced). Doesn't
 * touch the registry itself.
 */
void light_registry_invalidate_shading(void)
{
    light_shading_signals.areas_count = 0;
    light_shading_signals.areas_overflowed = false;
    light_shading_signals.registry_replaced = true;
}

/**
 * Forget every light and setting at once, without the per-light cleanup of
 * light_initialise() (the frontend's land view and torture screen, which
 * wipe the whole light system).
 */
void light_registry_clear(void)
{
    memset(&kfx_sim_state.light_registry, 0, sizeof(kfx_sim_state.light_registry));
    memset(&light_shading_signals, 0, sizeof(light_shading_signals));
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
