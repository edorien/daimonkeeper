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
    // 0x08 was LgtF_NeedUpdate: whether to shade a light again is kfx_render's (refactor pass 5, S11)
    // 0x10, 0x20 were LgtF_RadiusOscillation, LgtF_IntensityAnimation: nothing set them (refactor pass 5, S11)
    LgtF_NeverCached  = 0x40,
    // 0x80 was LgtF_OutOfDate: nothing set it
};

enum LightFlags2 {
    LgtF2_InList    = 0x01,
};

struct Light {
  unsigned char flags;
  unsigned char flags2;
  unsigned char intensity;
  unsigned char min_radius;
  int64_t index;
  int64_t shadow_index;
  SlabCodedCoords attached_slb;
  int64_t radius;
  int64_t min_intensity;
  int64_t next_in_list;
  struct Coord3d mappos;
  struct Coord3d previous_mappos;
  GameTurn last_turn_moved;
  TbBool reset_interpolation;
  // gpu-v2 lighting pass: 0,0,0 = white (see InitLight). Only the Vulkan renderer's per-pixel lighting uses it.
  unsigned char colour_r, colour_g, colour_b;
};

/**
 * The light registry, kfx_sim_state.light_registry: saved and resynced with
 * the rest of the sim state.
 *
 * Only the simulation writes it (refactor pass 5, S11): what the shading
 * keeps for each light (its range, whether to shade it again, the flicker)
 * is kfx_render_state.light_draw[], and the sim tells the shading what
 * changed through light_shading_signals. previous_mappos, last_turn_moved
 * and reset_interpolation are the sim's: where a light was last turn, and
 * whether it was teleported since, which the drawing interpolates from.
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
};

#pragma pack()

/******************************************************************************/
/**
 * Registry -> shading signals.
 *
 * A registry change that makes kfx_render's shading stale records it here:
 * an area (a slab changed under the lights, the whole map), or a light that
 * changed (created, moved, dimmed, turned on or off, deleted) with, for a
 * static light, where it was. kfx_render's light_drain_shading_signals()
 * applies them before it next shades: it clears the static light map's areas
 * and decides which lights to shade again, with the shading range it keeps
 * for each light (refactor pass 5, S11: the range and the "shade again" flag
 * were in struct Light, saved and resynced, and the renderer wrote them).
 * Same "sim signals, view polls" shape as kfx_sim_view_signals
 * (player_camera.h).
 *
 * Deliberately outside kfx_sim_state: not saved, not resynced. After a load
 * or a resync, light_registry_invalidate_shading() asks for everything to be
 * rebuilt instead.
 */
#define LIGHT_SHADING_AREAS_MAX 64

/** What an area asks for. */
enum LightShadingAreaKind {
    LgtArea_ClearMap = 0,  /**< clear the static light map there */
    LgtArea_StaticLights,  /**< the static lights shading it are shaded again (and the area cleared, if any is) */
    LgtArea_AllLights,     /**< the dynamic lights shading it too */
};

struct LightShadingArea {
    int64_t x1, y1, x2, y2;
    unsigned char kind;
};

/** What a light's change asks for (LightShadingSignals.light_changed). */
enum LightChangeFlags {
    LgtCh_Reshade = 0x01,  /**< shade it again */
    LgtCh_OwnArea = 0x02,  /**< a static light: the area it shaded, around changed_stl_x/y, is a LgtArea_StaticLights */
    LgtCh_Created = 0x04,  /**< a new light at this index: nothing drawn for the old one applies */
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
    /* Per light: what changed (LightChangeFlags), and where a static light was at the first change with
       LgtCh_OwnArea since the last drain. */
    unsigned char light_changed[LIGHTS_COUNT];
    MapSubtlCoord changed_stl_x[LIGHTS_COUNT];
    MapSubtlCoord changed_stl_y[LIGHTS_COUNT];
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
unsigned char light_get_light_intensity(int64_t idx);
void light_set_light_intensity(int64_t idx, unsigned char intensity);
int64_t light_get_light_radius(int64_t idx);
void light_set_light_radius(int64_t idx, int64_t radius);
void light_turn_light_off(int64_t num);
void light_turn_light_on(int64_t num);
void update_global_lighting(void);
void light_registry_reset_lighting(void);
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
