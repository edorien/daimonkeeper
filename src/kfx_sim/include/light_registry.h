/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file light_registry.h
 *     Header file for light_registry.c.
 * @par Purpose:
 *     The lights themselves: which exist, where they are, how bright, and
 *     which shadow-cache slot each dynamic one holds. Moved from kfx_render's
 *     light_data.c (docs/refactor-pass2/stage-11-lighting-split.md); the
 *     shading built from them (static light map, subtile lightness, shadow
 *     caches) stays in kfx_render.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_LIGHT_REGISTRY_H
#define DK_LIGHT_REGISTRY_H

#include "globals.h"
#include "bflib_basics.h"
#include "config.h"

#define LIGHTS_COUNT         2048
#define SHADOW_CACHE_COUNT    512

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
#pragma pack(1)

struct StructureList;

enum LightFlags {
    LgtF_Allocated    = 0x01,
    LgtF_CanTurnOff   = 0x02,
    LgtF_Dynamic      = 0x04,
    LgtF_NeedUpdate   = 0x08,
    LgtF_RadiusOscillation = 0x10,
    LgtF_IntensityAnimation = 0x20,
    LgtF_NeverCached  = 0x40,
    LgtF_OutOfDate    = 0x80,
};

enum LightFlags2 {
    LgtF2_InList    = 0x01,
};

struct Light {
  unsigned char flags;
  unsigned char flags2;
  unsigned char intensity;
  unsigned char intensity_toggling_field;//toggles between 1 and 2 when flags has LgtF_IntensityAnimation
  unsigned char intensity_delta;//seems never assigned
  unsigned char range;
  unsigned char radius_oscillation_direction;
  unsigned char max_intensity;//seems never assigned
  unsigned char min_radius;
  int64_t index;
  int64_t shadow_index;
  SlabCodedCoords attached_slb;
  int64_t radius;
  int64_t force_render_update;
  int64_t radius_delta;//seems never assigned
  int64_t max_radius;//seems never assigned
  int64_t min_radius2;//seems never assigned
  int64_t min_intensity;
  int64_t next_in_list;
  struct Coord3d mappos;
  struct Coord3d previous_mappos;
  int64_t intensity_random;
  int64_t previous_intensity_random;
  GameTurn last_turn_moved;
  GameTurn last_turn_randomized;
  TbBool reset_interpolation;
  // gpu-v2 lighting pass: 0,0,0 = white (see InitLight). Only the Vulkan renderer's per-pixel lighting uses it.
  unsigned char colour_r, colour_g, colour_b;
};

/**
 * The light registry, kfx_sim_state.light_registry: saved and resynced with
 * the rest of the sim state.
 *
 * Some struct Light fields are kept up to date by kfx_render's shading
 * rather than by the sim: range (the shading radius in subtiles, which the
 * invalidation below uses too), the interpolation and flicker fields, and
 * the LgtF_NeedUpdate/LgtF_OutOfDate flags. The sim never reads them to
 * decide anything, and nothing checksums them.
 */
struct LightRegistry {
    struct Light lights[LIGHTS_COUNT];
    /* Which of kfx_render's shadow caches are handed out: a dynamic light
       holds one (Light.shadow_index), and creating one fails when none is
       free, so the sim decides the allocation; kfx_render owns the cache
       contents. Slot 0 is never used. */
    unsigned char shadow_cache_used[SHADOW_CACHE_COUNT];
    int64_t global_ambient_light;
    TbBool light_enabled;
    TbBool light_auto_sync;
    int64_t total_dynamic_lights;
    int64_t total_stat_lights;
    int64_t stat_light_needs_updating;
};

#pragma pack()

/******************************************************************************/
/**
 * Registry -> shading signals.
 *
 * A registry change that makes kfx_render's static light map stale (a static
 * light moved, dimmed, deleted, or a slab changed under the lights) records
 * the area here instead of clearing the map itself; kfx_render's
 * light_drain_shading_signals() clears those areas before it next shades,
 * and whenever kfx_render changes the registry itself. The lights whose
 * shading the areas held are flagged LgtF_NeedUpdate at the same time, so
 * the drain only has to clear. Same "sim signals, view polls" shape as
 * kfx_sim_view_signals (player_camera.h).
 *
 * Deliberately outside kfx_sim_state: not saved, not resynced. After a load
 * or a resync, light_registry_invalidate_shading() asks for everything to be
 * rebuilt instead.
 */
#define LIGHT_SHADING_AREAS_MAX 64

struct LightShadingArea {
    int64_t x1, y1, x2, y2;
};

struct LightShadingSignals {
    /* Static light map areas to clear, oldest first. */
    struct LightShadingArea areas[LIGHT_SHADING_AREAS_MAX];
    int64_t areas_count;
    /* More areas than fit: clear the whole map and flag every static
       light instead. */
    TbBool areas_overflowed;
    /* Reset every subtile's lightness (a level's map was loaded or
       cleared). */
    TbBool reset_lightness;
    /* The registry was replaced wholesale (a load, a resync): shade every
       light again, dynamic ones' shadow caches included, and rebuild the
       map and the lightness. kfx_render flags the lights when it drains
       this, so the registry itself is left exactly as it arrived. */
    TbBool registry_replaced;
};

extern struct LightShadingSignals light_shading_signals;

/******************************************************************************/
struct Light *light_allocate_light(void);
void light_free_light(struct Light *lgt);
TbBool light_is_invalid(const struct Light *lgt);
int64_t light_allocate_shadow_cache(void);
void light_free_shadow_cache(int64_t shadow_index);
TbBool light_add_light_to_list(struct Light *lgt, struct StructureList *list);
void light_remove_light_from_list(struct Light *lgt, struct StructureList *list);

int64_t light_create_light(struct InitLight *ilght);
TbBool light_create_light_adv(VALUE *init_data);
void light_delete_light(int64_t idx);
void light_initialise(void);
int64_t light_count_lights(void);
TbBool lights_stats_debug_dump(void);

void light_set_light_never_cache(int64_t lgt_id);
int64_t light_is_light_allocated(int64_t lgt_id);
void light_set_light_position(int64_t lgt_id, struct Coord3d *pos);
void light_reset_interpolation(int64_t lgt_id);
unsigned char light_get_light_intensity(int64_t idx);
void light_set_light_intensity(int64_t idx, unsigned char intensity);
int64_t light_get_light_radius(int64_t idx);
void light_set_light_radius(int64_t idx, int64_t radius);
void light_turn_light_off(int64_t num);
void light_turn_light_on(int64_t num);
void light_init_dungeon_heart(int64_t lgt_id, int64_t radius, int64_t intensity);
void light_set_attached_slab(int64_t lgt_id, SlabCodedCoords slb_num);
void delete_lights_attached_to_slab_in_area(SlabCodedCoords place_slbnum,
    MapSubtlCoord start_stl_x, MapSubtlCoord start_stl_y,
    MapSubtlCoord end_stl_x, MapSubtlCoord end_stl_y);

void light_set_lights_on(char state);
TbBool light_get_lights_enabled(void);

void light_signal_update_in_area(int64_t sx, int64_t sy, int64_t ex, int64_t ey);
void light_signal_stat_light_update_in_area(int64_t x1, int64_t y1, int64_t x2, int64_t y2);
void light_request_stat_refresh(void);
void light_request_lightness_reset(void);
void light_registry_invalidate_shading(void);
void light_registry_clear(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
