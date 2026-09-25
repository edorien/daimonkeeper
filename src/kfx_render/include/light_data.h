/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file light_data.h
 *     Header file for light_data.c.
 * @par Purpose:
 *     light_data functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     11 Mar 2010 - 12 May 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_LIGHT_DATA_H
#define DK_LIGHT_DATA_H

#include "globals.h"
#include "bflib_basics.h"
#include "config.h"

#define LIGHT_MAX_RANGE       256 // Large enough to cover the whole map
#define LIGHTS_COUNT         2048

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
#pragma pack(1)

struct StructureList;

enum ShadowCacheFlags {
    ShCF_Allocated = 0x01,
};

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
};

// struct InitLight moved to kfx_config_state.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- embedded by value in
// EffectConfigStats/ObjectConfigStats (kfx_config's config_effects.h/
// config_objects.h), and read by kfx_sim too; kfx_config is the
// lowest-ranked of its real consumers.

struct LightSystemState {
    int64_t bitmask[32];
    int64_t static_light_needs_updating;
    int64_t total_dynamic_lights;
    int64_t total_stat_lights;
    int64_t rendered_dynamic_lights;
    int64_t rendered_optimised_dynamic_lights;
    int64_t updated_stat_lights;
    int64_t out_of_date_stat_lights;
};

// struct LightsShadows/LightingTable/ShadowCache and the lish global
// moved here from game_legacy.h/game_lghtshdw.h (stage 13.3, docs/
// refactor/stage-13-enforce-and-document.md) -- lish was struct Game's
// last remaining field; engine_render.c/light_data.c are its actual
// functional owners (hundreds of accesses each), and struct LightsShadows
// already embedded struct Light (this file) by value, so kfx_render was
// always its true home rather than kfx_game. kfx_sim's narrow accesses
// (map_blocks.c/map_data.c/thing_list.c) are routed through
// RenderOverlayCallbacks/ConfigReloadCallbacks; kfx_net's raw-blob need
// (net_resync.cpp) is a legal downward reference now.
#define SHADOW_LIMITS_COUNT  2048
#define SHADOW_CACHE_COUNT    512

struct LightingTable { // sizeof = 8
  TbBool is_populated;
  unsigned char distance; // 2 - 15
  char delta_x; // signed
  char delta_y; // signed
  uint64_t diagonal_length;
};

struct ShadowCache { // sizeof = 129
  unsigned char flags;
  uint32_t lighting_bitmask[32]; // bit 31 .. bit 0: the algorithm is 32 bits wide
};

/**
 * Structure which stores data of lights and shadows system.
 */
struct LightsShadows {
    struct LightingTable lighting_tables[1024]; // only the first 700 elements are populated
    unsigned char shadow_limits[SHADOW_LIMITS_COUNT];
    struct Light lights[LIGHTS_COUNT];
    struct ShadowCache shadow_cache[SHADOW_CACHE_COUNT];
    int64_t stat_light_map[MAX_SUBTILES_X*MAX_SUBTILES_Y];
    int64_t global_ambient_light;
    TbBool light_enabled;
    TbBool light_auto_sync;
    TbBool lighting_tables_initialised;
    int64_t lighting_tables_count; // number of entries in lighting_tables
    int64_t subtile_lightness[MAX_SUBTILES_X*MAX_SUBTILES_Y];
};

extern struct LightsShadows lish;

/******************************************************************************/

#pragma pack()

typedef struct VALUE VALUE;

/******************************************************************************/
void clear_stat_light_map(void);
void update_light_render_area(void);
void light_delete_light(int64_t idx);
void light_initialise(void);
void light_turn_light_off(int64_t num);
void light_turn_light_on(int64_t num);
unsigned char light_get_light_intensity(int64_t idx);
void light_set_light_intensity(int64_t idx, unsigned char intensity);
int64_t light_get_light_radius(int64_t idx);
void light_set_light_radius(int64_t idx, int64_t radius);
int64_t light_create_light(struct InitLight *ilght);
TbBool light_create_light_adv(VALUE *init_data);
void light_set_light_never_cache(int64_t lgt_id);
TbBool light_is_invalid(const struct Light *lgt);
struct Light *light_allocate_light(void);
void light_free_light(struct Light *lgt);
struct ShadowCache *light_allocate_shadow_cache(void);
TbBool light_shadow_cache_invalid(struct ShadowCache *shdc);
int64_t light_shadow_cache_index(struct ShadowCache *shdc);
void light_shadow_cache_free(struct ShadowCache *shdc);
int64_t light_is_light_allocated(int64_t lgt_id);
void light_set_light_position(int64_t lgt_id, struct Coord3d *pos);
void light_reset_interpolation(int64_t lgt_id);
void light_stat_refresh();
void update_global_lighting(void);
void light_set_lights_on(char state);
void light_init_dungeon_heart(int64_t lgt_id, int64_t radius, int64_t intensity);
void light_signal_update_in_area(int64_t sx, int64_t sy, int64_t ex, int64_t ey);
void light_export_system_state(struct LightSystemState *lightst);
void light_import_system_state(const struct LightSystemState *lightst);
TbBool lights_stats_debug_dump(void);
void light_signal_stat_light_update_in_area(int64_t x1, int64_t y1, int64_t x2, int64_t y2);

int64_t light_count_lights();

// Moved from game_lghtshdw.h (stage 13.3) alongside struct LightsShadows.
int64_t get_subtile_lightness(const struct LightsShadows * lish, MapSubtlCoord stl_x, MapSubtlCoord stl_y);
void clear_subtiles_lightness(struct LightsShadows * lish);
void create_shadow_limits(struct LightsShadows * lish, int64_t start, int64_t end);
void clear_shadow_limits(struct LightsShadows * lish);
void clear_light_system(struct LightsShadows * lish);

// Registered on RenderOverlayCallbacks (src/kfx_config/include/
// render_overlay.h) so kfx_sim's map_blocks.c/thing_list.c don't need
// struct Light/struct LightsShadows visible by value to touch a single
// light's attached_slb or the light_enabled flag.
void light_set_attached_slab(int64_t lgt_id, SlabCodedCoords slb_num);
void delete_lights_attached_to_slab_in_area(SlabCodedCoords place_slbnum,
    MapSubtlCoord start_stl_x, MapSubtlCoord start_stl_y,
    MapSubtlCoord end_stl_x, MapSubtlCoord end_stl_y);
TbBool light_get_lights_enabled(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
