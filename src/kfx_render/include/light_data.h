/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file light_data.h
 *     Header file for light_data.c.
 * @par Purpose:
 *     Lighting: shades the map from the lights in kfx_sim's light registry
 *     (light_registry.h) -- static light map, subtile lightness, shadow
 *     caches, per-pixel light lists.
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
#include "light_registry.h"

#define LIGHT_MAX_RANGE       256 // Large enough to cover the whole map

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
#pragma pack(1)

// struct LightsShadows/LightingTable/ShadowCache and the lish global
// moved here from game_legacy.h/game_lghtshdw.h (stage 13.3, docs/
// refactor/stage-13-enforce-and-document.md); the lights themselves left
// for kfx_sim's light_registry.h in refactor pass 2 (S11).
#define SHADOW_LIMITS_COUNT  2048

struct LightingTable { // sizeof = 8
  TbBool is_populated;
  unsigned char distance; // 2 - 15
  char delta_x; // signed
  char delta_y; // signed
  uint64_t diagonal_length;
};

/* One dynamic light's cached shadow; Light.shadow_index picks it. Which are
   in use is the registry's business (LightRegistry.shadow_cache_used). */
struct ShadowCache {
  uint32_t lighting_bitmask[32]; // bit 31 .. bit 0: the algorithm is 32 bits wide
};

/**
 * kfx_render's lighting: the shading built from the light registry
 * (kfx_sim_state.light_registry), all of it rebuildable from the registry
 * and the map, so none of it is saved or resynced. The lights themselves and
 * the ambient/enabled settings moved to the registry in refactor pass 2
 * (S11, docs/refactor-pass2/stage-11-lighting-split.md).
 */
struct LightsShadows {
    struct LightingTable lighting_tables[1024]; // only the first 700 elements are populated
    unsigned char shadow_limits[SHADOW_LIMITS_COUNT];
    struct ShadowCache shadow_cache[SHADOW_CACHE_COUNT];
    int64_t stat_light_map[MAX_SUBTILES_X*MAX_SUBTILES_Y];
    TbBool lighting_tables_initialised;
    int64_t lighting_tables_count; // number of entries in lighting_tables
    int64_t subtile_lightness[MAX_SUBTILES_X*MAX_SUBTILES_Y];
};

extern struct LightsShadows lish;

/******************************************************************************/

#pragma pack()

/******************************************************************************/
void clear_stat_light_map(void);
void update_light_render_area(void);
void light_stat_refresh();
void light_drain_shading_signals(void);
void update_global_lighting(void);

// gpu-v2 Phase C.5 lighting pass -- see light_data.c. light_perpixel_active(): per-pixel lighting
// is on for this frame (Vulkan renderer, standard perspective). light_perpixel_get(): the dynamic
// lights collected by the last light_render_area() as x,y,z,radius,intensity floats (5 per light),
// plus the solid-column height of every subtile in subtiles (grid_w x grid_h bytes, indexed
// by subtile number); false if the last update was classic.
TbBool light_perpixel_active(void);
TbBool light_perpixel_get(const float **lights, int64_t *count, const unsigned char **heights, int64_t *grid_w, int64_t *grid_h);

// Moved from game_lghtshdw.h (stage 13.3) alongside struct LightsShadows.
int64_t get_subtile_lightness(const struct LightsShadows * lish, MapSubtlCoord stl_x, MapSubtlCoord stl_y);
void clear_subtiles_lightness(struct LightsShadows * lish);
void create_shadow_limits(struct LightsShadows * lish, int64_t start, int64_t end);
void clear_shadow_limits(struct LightsShadows * lish);
void clear_light_system(struct LightsShadows * lish);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
