/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file light_data.c
 *     light_data support functions.
 * @par Purpose:
 *     Functions to light_data.
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
#include "light_data.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_math.h"
#include "bflib_planar.h"

#include "engine_render.h"
#include "engine_lenses.h"           // rotpers, rotpers_standard
#include "renderer/RendererManager.h" // RendererPerPixelLightingActive
#include "player_data.h"
#include "map_data.h"

#include "kfx_render_state.h"

#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "thing_list.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// Moved from game_legacy.c alongside struct LightsShadows (stage 13.3,
// docs/refactor/stage-13-enforce-and-document.md).
struct LightsShadows lish;

static void light_stat_light_map_clear_area(MapSubtlCoord x1, MapSubtlCoord y1, MapSubtlCoord x2, MapSubtlCoord y2);

/******************************************************************************/

static uint64_t light_bitmask[32];
static int64_t light_rendered_dynamic_lights;
static int64_t light_rendered_optimised_dynamic_lights;
static int64_t light_updated_stat_lights;
static int64_t light_out_of_date_stat_lights;
/******************************************************************************/

void clear_stat_light_map(void)
{
    kfx_sim_state.light_registry.global_ambient_light = 32;
    kfx_sim_state.light_registry.light_enabled = 0;
    // The whole map is zeroed below: pending area clears are moot.
    light_shading_signals.areas_count = 0;
    light_shading_signals.areas_overflowed = false;
    for (uint64_t y = 0; y < (kfx_sim_state.map_subtiles_y + 1); y++)
    {
        for (uint64_t x = 0; x < (kfx_sim_state.map_subtiles_x + 1); x++)
        {
            uint64_t i = get_subtile_number(x, y);
            lish.stat_light_map[i] = 0;
        }
    }
}

void light_initialise_lighting_tables(void)
{
  static const struct LightingTable values[] = {
    { 1, 2, 0, -1, 256 },
    { 1, 2, 1, 0, 256 },
    { 1, 2, 0, 1, 256 },
    { 1, 2, -1, 0, 256 },
    { 1, 2, 1, -1, 362 },
    { 1, 2, 1, 1, 362 },
    { 1, 2, -1, 1, 362 },
    { 1, 2, -1, -1, 362 },
    { 1, 3, 0, -2, 512 },
    { 1, 3, 2, 0, 512 },
    { 1, 3, 0, 2, 512 },
    { 1, 3, -2, 0, 512 },
    { 1, 3, 1, -2, 572 },
    { 1, 3, 2, -1, 572 },
    { 1, 3, 2, 1, 572 },
    { 1, 3, 1, 2, 572 },
    { 1, 3, -1, 2, 572 },
    { 1, 3, -2, 1, 572 },
    { 1, 3, -2, -1, 572 },
    { 1, 3, -1, -2, 572 },
    { 1, 4, 2, -2, 724 },
    { 1, 4, 2, 2, 724 },
    { 1, 4, -2, 2, 724 },
    { 1, 4, -2, -2, 724 },
    { 1, 4, 0, -3, 768 },
    { 1, 4, 3, 0, 768 },
    { 1, 4, 0, 3, 768 },
    { 1, 4, -3, 0, 768 },
    { 1, 4, 1, -3, 809 },
    { 1, 4, 3, -1, 809 },
    { 1, 4, 3, 1, 809 },
    { 1, 4, 1, 3, 809 },
    { 1, 4, -1, 3, 809 },
    { 1, 4, -3, 1, 809 },
    { 1, 4, -3, -1, 809 },
    { 1, 4, -1, -3, 809 },
    { 1, 4, 2, -3, 921 },
    { 1, 4, 3, -2, 921 },
    { 1, 4, 3, 2, 921 },
    { 1, 4, 2, 3, 921 },
    { 1, 4, -2, 3, 921 },
    { 1, 4, -3, 2, 921 },
    { 1, 4, -3, -2, 921 },
    { 1, 4, -2, -3, 921 },
    { 1, 5, 0, -4, 1024 },
    { 1, 5, 4, 0, 1024 },
    { 1, 5, 0, 4, 1024 },
    { 1, 5, -4, 0, 1024 },
    { 1, 5, 1, -4, 1055 },
    { 1, 5, 4, -1, 1055 },
    { 1, 5, 4, 1, 1055 },
    { 1, 5, 1, 4, 1055 },
    { 1, 5, -1, 4, 1055 },
    { 1, 5, -4, 1, 1055 },
    { 1, 5, -4, -1, 1055 },
    { 1, 5, -1, -4, 1055 },
    { 1, 5, 3, -3, 1086 },
    { 1, 5, 3, 3, 1086 },
    { 1, 5, -3, 3, 1086 },
    { 1, 5, -3, -3, 1086 },
    { 1, 5, 2, -4, 1144 },
    { 1, 5, 4, -2, 1144 },
    { 1, 5, 4, 2, 1144 },
    { 1, 5, 2, 4, 1144 },
    { 1, 5, -2, 4, 1144 },
    { 1, 5, -4, 2, 1144 },
    { 1, 5, -4, -2, 1144 },
    { 1, 5, -2, -4, 1144 },
    { 1, 6, 0, -5, 1280 },
    { 1, 6, 3, -4, 1280 },
    { 1, 6, 4, -3, 1280 },
    { 1, 6, 5, 0, 1280 },
    { 1, 6, 4, 3, 1280 },
    { 1, 6, 3, 4, 1280 },
    { 1, 6, 0, 5, 1280 },
    { 1, 6, -3, 4, 1280 },
    { 1, 6, -4, 3, 1280 },
    { 1, 6, -5, 0, 1280 },
    { 1, 6, -4, -3, 1280 },
    { 1, 6, -3, -4, 1280 },
    { 1, 6, 1, -5, 1305 },
    { 1, 6, 5, -1, 1305 },
    { 1, 6, 5, 1, 1305 },
    { 1, 6, 1, 5, 1305 },
    { 1, 6, -1, 5, 1305 },
    { 1, 6, -5, 1, 1305 },
    { 1, 6, -5, -1, 1305 },
    { 1, 6, -1, -5, 1305 },
    { 1, 6, 2, -5, 1377 },
    { 1, 6, 5, -2, 1377 },
    { 1, 6, 5, 2, 1377 },
    { 1, 6, 2, 5, 1377 },
    { 1, 6, -2, 5, 1377 },
    { 1, 6, -5, 2, 1377 },
    { 1, 6, -5, -2, 1377 },
    { 1, 6, -2, -5, 1377 },
    { 1, 7, 4, -4, 1448 },
    { 1, 7, 4, 4, 1448 },
    { 1, 7, -4, 4, 1448 },
    { 1, 7, -4, -4, 1448 },
    { 1, 7, 3, -5, 1491 },
    { 1, 7, 5, -3, 1491 },
    { 1, 7, 5, 3, 1491 },
    { 1, 7, 3, 5, 1491 },
    { 1, 7, -3, 5, 1491 },
    { 1, 7, -5, 3, 1491 },
    { 1, 7, -5, -3, 1491 },
    { 1, 7, -3, -5, 1491 },
    { 1, 7, 0, -6, 1536 },
    { 1, 7, 6, 0, 1536 },
    { 1, 7, 0, 6, 1536 },
    { 1, 7, -6, 0, 1536 },
    { 1, 7, 1, -6, 1556 },
    { 1, 7, 6, -1, 1556 },
    { 1, 7, 6, 1, 1556 },
    { 1, 7, 1, 6, 1556 },
    { 1, 7, -1, 6, 1556 },
    { 1, 7, -6, 1, 1556 },
    { 1, 7, -6, -1, 1556 },
    { 1, 7, -1, -6, 1556 },
    { 1, 7, 2, -6, 1618 },
    { 1, 7, 6, -2, 1618 },
    { 1, 7, 6, 2, 1618 },
    { 1, 7, 2, 6, 1618 },
    { 1, 7, -2, 6, 1618 },
    { 1, 7, -6, 2, 1618 },
    { 1, 7, -6, -2, 1618 },
    { 1, 7, -2, -6, 1618 },
    { 1, 7, 4, -5, 1636 },
    { 1, 7, 5, -4, 1636 },
    { 1, 7, 5, 4, 1636 },
    { 1, 7, 4, 5, 1636 },
    { 1, 7, -4, 5, 1636 },
    { 1, 7, -5, 4, 1636 },
    { 1, 7, -5, -4, 1636 },
    { 1, 7, -4, -5, 1636 },
    { 1, 8, 3, -6, 1717 },
    { 1, 8, 6, -3, 1717 },
    { 1, 8, 6, 3, 1717 },
    { 1, 8, 3, 6, 1717 },
    { 1, 8, -3, 6, 1717 },
    { 1, 8, -6, 3, 1717 },
    { 1, 8, -6, -3, 1717 },
    { 1, 8, -3, -6, 1717 },
    { 1, 8, 0, -7, 1792 },
    { 1, 8, 7, 0, 1792 },
    { 1, 8, 0, 7, 1792 },
    { 1, 8, -7, 0, 1792 },
    { 1, 8, 1, -7, 1809 },
    { 1, 8, 7, -1, 1809 },
    { 1, 8, 7, 1, 1809 },
    { 1, 8, 1, 7, 1809 },
    { 1, 8, -1, 7, 1809 },
    { 1, 8, -7, 1, 1809 },
    { 1, 8, -7, -1, 1809 },
    { 1, 8, -1, -7, 1809 },
    { 1, 8, 5, -5, 1810 },
    { 1, 8, 5, 5, 1810 },
    { 1, 8, -5, 5, 1810 },
    { 1, 8, -5, -5, 1810 },
    { 1, 8, 4, -6, 1843 },
    { 1, 8, 6, -4, 1843 },
    { 1, 8, 6, 4, 1843 },
    { 1, 8, 4, 6, 1843 },
    { 1, 8, -4, 6, 1843 },
    { 1, 8, -6, 4, 1843 },
    { 1, 8, -6, -4, 1843 },
    { 1, 8, -4, -6, 1843 },
    { 1, 8, 2, -7, 1863 },
    { 1, 8, 7, -2, 1863 },
    { 1, 8, 7, 2, 1863 },
    { 1, 8, 2, 7, 1863 },
    { 1, 8, -2, 7, 1863 },
    { 1, 8, -7, 2, 1863 },
    { 1, 8, -7, -2, 1863 },
    { 1, 8, -2, -7, 1863 },
    { 1, 8, 3, -7, 1947 },
    { 1, 8, 7, -3, 1947 },
    { 1, 8, 7, 3, 1947 },
    { 1, 8, 3, 7, 1947 },
    { 1, 8, -3, 7, 1947 },
    { 1, 8, -7, 3, 1947 },
    { 1, 8, -7, -3, 1947 },
    { 1, 8, -3, -7, 1947 },
    { 1, 9, 5, -6, 1998 },
    { 1, 9, 6, -5, 1998 },
    { 1, 9, 6, 5, 1998 },
    { 1, 9, 5, 6, 1998 },
    { 1, 9, -5, 6, 1998 },
    { 1, 9, -6, 5, 1998 },
    { 1, 9, -6, -5, 1998 },
    { 1, 9, -5, -6, 1998 },
    { 1, 9, 0, -8, 2048 },
    { 1, 9, 8, 0, 2048 },
    { 1, 9, 0, 8, 2048 },
    { 1, 9, -8, 0, 2048 },
    { 1, 9, 4, -7, 2063 },
    { 1, 9, 7, -4, 2063 },
    { 1, 9, 7, 4, 2063 },
    { 1, 9, 4, 7, 2063 },
    { 1, 9, -4, 7, 2063 },
    { 1, 9, -7, 4, 2063 },
    { 1, 9, -7, -4, 2063 },
    { 1, 9, -4, -7, 2063 },
    { 1, 9, 1, -8, 2064 },
    { 1, 9, 8, -1, 2064 },
    { 1, 9, 8, 1, 2064 },
    { 1, 9, 1, 8, 2064 },
    { 1, 9, -1, 8, 2064 },
    { 1, 9, -8, 1, 2064 },
    { 1, 9, -8, -1, 2064 },
    { 1, 9, -1, -8, 2064 },
    { 1, 9, 2, -8, 2111 },
    { 1, 9, 8, -2, 2111 },
    { 1, 9, 8, 2, 2111 },
    { 1, 9, 2, 8, 2111 },
    { 1, 9, -2, 8, 2111 },
    { 1, 9, -8, 2, 2111 },
    { 1, 9, -8, -2, 2111 },
    { 1, 9, -2, -8, 2111 },
    { 1, 9, 6, -6, 2172 },
    { 1, 9, 6, 6, 2172 },
    { 1, 9, -6, 6, 2172 },
    { 1, 9, -6, -6, 2172 },
    { 1, 9, 3, -8, 2187 },
    { 1, 9, 8, -3, 2187 },
    { 1, 9, 8, 3, 2187 },
    { 1, 9, 3, 8, 2187 },
    { 1, 9, -3, 8, 2187 },
    { 1, 9, -8, 3, 2187 },
    { 1, 9, -8, -3, 2187 },
    { 1, 9, -3, -8, 2187 },
    { 1, 9, 5, -7, 2198 },
    { 1, 9, 7, -5, 2198 },
    { 1, 9, 7, 5, 2198 },
    { 1, 9, 5, 7, 2198 },
    { 1, 9, -5, 7, 2198 },
    { 1, 9, -7, 5, 2198 },
    { 1, 9, -7, -5, 2198 },
    { 1, 9, -5, -7, 2198 },
    { 1, 10, 4, -8, 2289 },
    { 1, 10, 8, -4, 2289 },
    { 1, 10, 8, 4, 2289 },
    { 1, 10, 4, 8, 2289 },
    { 1, 10, -4, 8, 2289 },
    { 1, 10, -8, 4, 2289 },
    { 1, 10, -8, -4, 2289 },
    { 1, 10, -4, -8, 2289 },
    { 1, 10, 0, -9, 2304 },
    { 1, 10, 9, 0, 2304 },
    { 1, 10, 0, 9, 2304 },
    { 1, 10, -9, 0, 2304 },
    { 1, 10, 1, -9, 2317 },
    { 1, 10, 9, -1, 2317 },
    { 1, 10, 9, 1, 2317 },
    { 1, 10, 1, 9, 2317 },
    { 1, 10, -1, 9, 2317 },
    { 1, 10, -9, 1, 2317 },
    { 1, 10, -9, -1, 2317 },
    { 1, 10, -1, -9, 2317 },
    { 1, 10, 2, -9, 2358 },
    { 1, 10, 6, -7, 2358 },
    { 1, 10, 7, -6, 2358 },
    { 1, 10, 9, -2, 2358 },
    { 1, 10, 9, 2, 2358 },
    { 1, 10, 7, 6, 2358 },
    { 1, 10, 6, 7, 2358 },
    { 1, 10, 2, 9, 2358 },
    { 1, 10, -2, 9, 2358 },
    { 1, 10, -6, 7, 2358 },
    { 1, 10, -7, 6, 2358 },
    { 1, 10, -9, 2, 2358 },
    { 1, 10, -9, -2, 2358 },
    { 1, 10, -7, -6, 2358 },
    { 1, 10, -6, -7, 2358 },
    { 1, 10, -2, -9, 2358 },
    { 1, 10, 5, -8, 2415 },
    { 1, 10, 8, -5, 2415 },
    { 1, 10, 8, 5, 2415 },
    { 1, 10, 5, 8, 2415 },
    { 1, 10, -5, 8, 2415 },
    { 1, 10, -8, 5, 2415 },
    { 1, 10, -8, -5, 2415 },
    { 1, 10, -5, -8, 2415 },
    { 1, 10, 3, -9, 2427 },
    { 1, 10, 9, -3, 2427 },
    { 1, 10, 9, 3, 2427 },
    { 1, 10, 3, 9, 2427 },
    { 1, 10, -3, 9, 2427 },
    { 1, 10, -9, 3, 2427 },
    { 1, 10, -9, -3, 2427 },
    { 1, 10, -3, -9, 2427 },
    { 1, 11, 4, -9, 2518 },
    { 1, 11, 9, -4, 2518 },
    { 1, 11, 9, 4, 2518 },
    { 1, 11, 4, 9, 2518 },
    { 1, 11, -4, 9, 2518 },
    { 1, 11, -9, 4, 2518 },
    { 1, 11, -9, -4, 2518 },
    { 1, 11, -4, -9, 2518 },
    { 1, 11, 7, -7, 2534 },
    { 1, 11, 7, 7, 2534 },
    { 1, 11, -7, 7, 2534 },
    { 1, 11, -7, -7, 2534 },
    { 1, 11, 0, -10, 2560 },
    { 1, 11, 6, -8, 2560 },
    { 1, 11, 8, -6, 2560 },
    { 1, 11, 10, 0, 2560 },
    { 1, 11, 8, 6, 2560 },
    { 1, 11, 6, 8, 2560 },
    { 1, 11, 0, 10, 2560 },
    { 1, 11, -6, 8, 2560 },
    { 1, 11, -8, 6, 2560 },
    { 1, 11, -10, 0, 2560 },
    { 1, 11, -8, -6, 2560 },
    { 1, 11, -6, -8, 2560 },
    { 1, 11, 1, -10, 2572 },
    { 1, 11, 10, -1, 2572 },
    { 1, 11, 10, 1, 2572 },
    { 1, 11, 1, 10, 2572 },
    { 1, 11, -1, 10, 2572 },
    { 1, 11, -10, 1, 2572 },
    { 1, 11, -10, -1, 2572 },
    { 1, 11, -1, -10, 2572 },
    { 1, 11, 2, -10, 2610 },
    { 1, 11, 10, -2, 2610 },
    { 1, 11, 10, 2, 2610 },
    { 1, 11, 2, 10, 2610 },
    { 1, 11, -2, 10, 2610 },
    { 1, 11, -10, 2, 2610 },
    { 1, 11, -10, -2, 2610 },
    { 1, 11, -2, -10, 2610 },
    { 1, 11, 5, -9, 2634 },
    { 1, 11, 9, -5, 2634 },
    { 1, 11, 9, 5, 2634 },
    { 1, 11, 5, 9, 2634 },
    { 1, 11, -5, 9, 2634 },
    { 1, 11, -9, 5, 2634 },
    { 1, 11, -9, -5, 2634 },
    { 1, 11, -5, -9, 2634 },
    { 1, 11, 3, -10, 2670 },
    { 1, 11, 10, -3, 2670 },
    { 1, 11, 10, 3, 2670 },
    { 1, 11, 3, 10, 2670 },
    { 1, 11, -3, 10, 2670 },
    { 1, 11, -10, 3, 2670 },
    { 1, 11, -10, -3, 2670 },
    { 1, 11, -3, -10, 2670 },
    { 1, 12, 7, -8, 2721 },
    { 1, 12, 8, -7, 2721 },
    { 1, 12, 8, 7, 2721 },
    { 1, 12, 7, 8, 2721 },
    { 1, 12, -7, 8, 2721 },
    { 1, 12, -8, 7, 2721 },
    { 1, 12, -8, -7, 2721 },
    { 1, 12, -7, -8, 2721 },
    { 1, 12, 4, -10, 2755 },
    { 1, 12, 10, -4, 2755 },
    { 1, 12, 10, 4, 2755 },
    { 1, 12, 4, 10, 2755 },
    { 1, 12, -4, 10, 2755 },
    { 1, 12, -10, 4, 2755 },
    { 1, 12, -10, -4, 2755 },
    { 1, 12, -4, -10, 2755 },
    { 1, 12, 6, -9, 2765 },
    { 1, 12, 9, -6, 2765 },
    { 1, 12, 9, 6, 2765 },
    { 1, 12, 6, 9, 2765 },
    { 1, 12, -6, 9, 2765 },
    { 1, 12, -9, 6, 2765 },
    { 1, 12, -9, -6, 2765 },
    { 1, 12, -6, -9, 2765 },
    { 1, 12, 0, -11, 2816 },
    { 1, 12, 11, 0, 2816 },
    { 1, 12, 0, 11, 2816 },
    { 1, 12, -11, 0, 2816 },
    { 1, 12, 1, -11, 2827 },
    { 1, 12, 11, -1, 2827 },
    { 1, 12, 11, 1, 2827 },
    { 1, 12, 1, 11, 2827 },
    { 1, 12, -1, 11, 2827 },
    { 1, 12, -11, 1, 2827 },
    { 1, 12, -11, -1, 2827 },
    { 1, 12, -1, -11, 2827 },
    { 1, 12, 2, -11, 2861 },
    { 1, 12, 11, -2, 2861 },
    { 1, 12, 11, 2, 2861 },
    { 1, 12, 2, 11, 2861 },
    { 1, 12, -2, 11, 2861 },
    { 1, 12, -11, 2, 2861 },
    { 1, 12, -11, -2, 2861 },
    { 1, 12, -2, -11, 2861 },
    { 1, 12, 5, -10, 2862 },
    { 1, 12, 10, -5, 2862 },
    { 1, 12, 10, 5, 2862 },
    { 1, 12, 5, 10, 2862 },
    { 1, 12, -5, 10, 2862 },
    { 1, 12, -10, 5, 2862 },
    { 1, 12, -10, -5, 2862 },
    { 1, 12, -5, -10, 2862 },
    { 1, 12, 8, -8, 2896 },
    { 1, 12, 8, 8, 2896 },
    { 1, 12, -8, 8, 2896 },
    { 1, 12, -8, -8, 2896 },
    { 1, 12, 3, -11, 2916 },
    { 1, 12, 11, -3, 2916 },
    { 1, 12, 11, 3, 2916 },
    { 1, 12, 3, 11, 2916 },
    { 1, 12, -3, 11, 2916 },
    { 1, 12, -11, 3, 2916 },
    { 1, 12, -11, -3, 2916 },
    { 1, 12, -3, -11, 2916 },
    { 1, 12, 7, -9, 2918 },
    { 1, 12, 9, -7, 2918 },
    { 1, 12, 9, 7, 2918 },
    { 1, 12, 7, 9, 2918 },
    { 1, 12, -7, 9, 2918 },
    { 1, 12, -9, 7, 2918 },
    { 1, 12, -9, -7, 2918 },
    { 1, 12, -7, -9, 2918 },
    { 1, 13, 6, -10, 2982 },
    { 1, 13, 10, -6, 2982 },
    { 1, 13, 10, 6, 2982 },
    { 1, 13, 6, 10, 2982 },
    { 1, 13, -6, 10, 2982 },
    { 1, 13, -10, 6, 2982 },
    { 1, 13, -10, -6, 2982 },
    { 1, 13, -6, -10, 2982 },
    { 1, 12, 4, -11, 2996 },
    { 1, 12, 11, -4, 2996 },
    { 1, 12, 11, 4, 2996 },
    { 1, 12, 4, 11, 2996 },
    { 1, 12, -4, 11, 2996 },
    { 1, 12, -11, 4, 2996 },
    { 1, 12, -11, -4, 2996 },
    { 1, 12, -4, -11, 2996 },
    { 1, 13, 0, -12, 3072 },
    { 1, 13, 12, 0, 3072 },
    { 1, 13, 0, 12, 3072 },
    { 1, 13, -12, 0, 3072 },
    { 1, 13, 8, -9, 3079 },
    { 1, 13, 9, -8, 3079 },
    { 1, 13, 9, 8, 3079 },
    { 1, 13, 8, 9, 3079 },
    { 1, 13, -8, 9, 3079 },
    { 1, 13, -9, 8, 3079 },
    { 1, 13, -9, -8, 3079 },
    { 1, 13, -8, -9, 3079 },
    { 1, 13, 1, -12, 3082 },
    { 1, 13, 12, -1, 3082 },
    { 1, 13, 12, 1, 3082 },
    { 1, 13, 1, 12, 3082 },
    { 1, 13, -1, 12, 3082 },
    { 1, 13, -12, 1, 3082 },
    { 1, 13, -12, -1, 3082 },
    { 1, 13, -1, -12, 3082 },
    { 1, 13, 5, -11, 3091 },
    { 1, 13, 11, -5, 3091 },
    { 1, 13, 11, 5, 3091 },
    { 1, 13, 5, 11, 3091 },
    { 1, 13, -5, 11, 3091 },
    { 1, 13, -11, 5, 3091 },
    { 1, 13, -11, -5, 3091 },
    { 1, 13, -5, -11, 3091 },
    { 1, 13, 2, -12, 3113 },
    { 1, 13, 12, -2, 3113 },
    { 1, 13, 12, 2, 3113 },
    { 1, 13, 2, 12, 3113 },
    { 1, 13, -2, 12, 3113 },
    { 1, 13, -12, 2, 3113 },
    { 1, 13, -12, -2, 3113 },
    { 1, 13, -2, -12, 3113 },
    { 1, 13, 7, -10, 3123 },
    { 1, 13, 10, -7, 3123 },
    { 1, 13, 10, 7, 3123 },
    { 1, 13, 7, 10, 3123 },
    { 1, 13, -7, 10, 3123 },
    { 1, 13, -10, 7, 3123 },
    { 1, 13, -10, -7, 3123 },
    { 1, 13, -7, -10, 3123 },
    { 1, 13, 3, -12, 3166 },
    { 1, 13, 12, -3, 3166 },
    { 1, 13, 12, 3, 3166 },
    { 1, 13, 3, 12, 3166 },
    { 1, 13, -3, 12, 3166 },
    { 1, 13, -12, 3, 3166 },
    { 1, 13, -12, -3, 3166 },
    { 1, 13, -3, -12, 3166 },
    { 1, 13, 6, -11, 3204 },
    { 1, 13, 11, -6, 3204 },
    { 1, 13, 11, 6, 3204 },
    { 1, 13, 6, 11, 3204 },
    { 1, 13, -6, 11, 3204 },
    { 1, 13, -11, 6, 3204 },
    { 1, 13, -11, -6, 3204 },
    { 1, 13, -6, -11, 3204 },
    { 1, 13, 4, -12, 3237 },
    { 1, 13, 12, -4, 3237 },
    { 1, 13, 12, 4, 3237 },
    { 1, 13, 4, 12, 3237 },
    { 1, 13, -4, 12, 3237 },
    { 1, 13, -12, 4, 3237 },
    { 1, 13, -12, -4, 3237 },
    { 1, 13, -4, -12, 3237 },
    { 1, 14, 9, -9, 3258 },
    { 1, 14, 9, 9, 3258 },
    { 1, 14, -9, 9, 3258 },
    { 1, 14, -9, -9, 3258 },
    { 1, 14, 8, -10, 3273 },
    { 1, 14, 10, -8, 3273 },
    { 1, 14, 10, 8, 3273 },
    { 1, 14, 8, 10, 3273 },
    { 1, 14, -8, 10, 3273 },
    { 1, 14, -10, 8, 3273 },
    { 1, 14, -10, -8, 3273 },
    { 1, 14, -8, -10, 3273 },
    { 1, 14, 5, -12, 3324 },
    { 1, 14, 12, -5, 3324 },
    { 1, 14, 12, 5, 3324 },
    { 1, 14, 5, 12, 3324 },
    { 1, 14, -5, 12, 3324 },
    { 1, 14, -12, 5, 3324 },
    { 1, 14, -12, -5, 3324 },
    { 1, 14, -5, -12, 3324 },
    { 1, 14, 0, -13, 3328 },
    { 1, 14, 13, 0, 3328 },
    { 1, 14, 0, 13, 3328 },
    { 1, 14, -13, 0, 3328 },
    { 1, 14, 7, -11, 3332 },
    { 1, 14, 11, -7, 3332 },
    { 1, 14, 11, 7, 3332 },
    { 1, 14, 7, 11, 3332 },
    { 1, 14, -7, 11, 3332 },
    { 1, 14, -11, 7, 3332 },
    { 1, 14, -11, -7, 3332 },
    { 1, 14, -7, -11, 3332 },
    { 1, 14, 1, -13, 3337 },
    { 1, 14, 13, -1, 3337 },
    { 1, 14, 13, 1, 3337 },
    { 1, 14, 1, 13, 3337 },
    { 1, 14, -1, 13, 3337 },
    { 1, 14, -13, 1, 3337 },
    { 1, 14, -13, -1, 3337 },
    { 1, 14, -1, -13, 3337 },
    { 1, 14, 2, -13, 3366 },
    { 1, 14, 13, -2, 3366 },
    { 1, 14, 13, 2, 3366 },
    { 1, 14, 2, 13, 3366 },
    { 1, 14, -2, 13, 3366 },
    { 1, 14, -13, 2, 3366 },
    { 1, 14, -13, -2, 3366 },
    { 1, 14, -2, -13, 3366 },
    { 1, 14, 3, -13, 3415 },
    { 1, 14, 13, -3, 3415 },
    { 1, 14, 13, 3, 3415 },
    { 1, 14, 3, 13, 3415 },
    { 1, 14, -3, 13, 3415 },
    { 1, 14, -13, 3, 3415 },
    { 1, 14, -13, -3, 3415 },
    { 1, 14, -3, -13, 3415 },
    { 1, 14, 6, -12, 3434 },
    { 1, 14, 12, -6, 3434 },
    { 1, 14, 12, 6, 3434 },
    { 1, 14, 6, 12, 3434 },
    { 1, 14, -6, 12, 3434 },
    { 1, 14, -12, 6, 3434 },
    { 1, 14, -12, -6, 3434 },
    { 1, 14, -6, -12, 3434 },
    { 1, 14, 9, -10, 3441 },
    { 1, 14, 10, -9, 3441 },
    { 1, 14, 10, 9, 3441 },
    { 1, 14, 9, 10, 3441 },
    { 1, 14, -9, 10, 3441 },
    { 1, 14, -10, 9, 3441 },
    { 1, 14, -10, -9, 3441 },
    { 1, 14, -9, -10, 3441 },
    { 1, 14, 4, -13, 3479 },
    { 1, 14, 13, -4, 3479 },
    { 1, 14, 13, 4, 3479 },
    { 1, 14, 4, 13, 3479 },
    { 1, 14, -4, 13, 3479 },
    { 1, 14, -13, 4, 3479 },
    { 1, 14, -13, -4, 3479 },
    { 1, 14, -4, -13, 3479 },
    { 1, 14, 8, -11, 3480 },
    { 1, 14, 11, -8, 3480 },
    { 1, 14, 11, 8, 3480 },
    { 1, 14, 8, 11, 3480 },
    { 1, 14, -8, 11, 3480 },
    { 1, 14, -11, 8, 3480 },
    { 1, 14, -11, -8, 3480 },
    { 1, 14, -8, -11, 3480 },
    { 1, 15, 7, -12, 3554 },
    { 1, 15, 12, -7, 3554 },
    { 1, 15, 12, 7, 3554 },
    { 1, 15, 7, 12, 3554 },
    { 1, 15, -7, 12, 3554 },
    { 1, 15, -12, 7, 3554 },
    { 1, 15, -12, -7, 3554 },
    { 1, 15, -7, -12, 3554 },
    { 1, 15, 5, -13, 3563 },
    { 1, 15, 13, -5, 3563 },
    { 1, 15, 13, 5, 3563 },
    { 1, 15, 5, 13, 3563 },
    { 1, 15, -5, 13, 3563 },
    { 1, 15, -13, 5, 3563 },
    { 1, 15, -13, -5, 3563 },
    { 1, 15, -5, -13, 3563 },
    { 1, 15, 0, -14, 3584 },
    { 1, 15, 14, 0, 3584 },
    { 1, 15, 0, 14, 3584 },
    { 1, 15, -14, 0, 3584 },
    { 1, 15, 1, -14, 3592 },
    { 1, 15, 14, -1, 3592 },
    { 1, 15, 14, 1, 3592 },
    { 1, 15, 1, 14, 3592 },
    { 1, 15, -1, 14, 3592 },
    { 1, 15, -14, 1, 3592 },
    { 1, 15, -14, -1, 3592 },
    { 1, 15, -1, -14, 3592 },
    { 1, 15, 2, -14, 3619 },
    { 1, 15, 14, -2, 3619 },
    { 1, 15, 14, 2, 3619 },
    { 1, 15, 2, 14, 3619 },
    { 1, 15, -2, 14, 3619 },
    { 1, 15, -14, 2, 3619 },
    { 1, 15, -14, -2, 3619 },
    { 1, 15, -2, -14, 3619 },
    { 1, 15, 10, -10, 3620 },
    { 1, 15, 10, 10, 3620 },
    { 1, 15, -10, 10, 3620 },
    { 1, 15, -10, -10, 3620 },
    { 1, 15, 9, -11, 3635 },
    { 1, 15, 11, -9, 3635 },
    { 1, 15, 11, 9, 3635 },
    { 1, 15, 9, 11, 3635 },
    { 1, 15, -9, 11, 3635 },
    { 1, 15, -11, 9, 3635 },
    { 1, 15, -11, -9, 3635 },
    { 1, 15, -9, -11, 3635 },
    { 1, 15, 3, -14, 3662 },
    { 1, 15, 14, -3, 3662 },
    { 1, 15, 14, 3, 3662 },
    { 1, 15, 3, 14, 3662 },
    { 1, 15, -3, 14, 3662 },
    { 1, 15, -14, 3, 3662 },
    { 1, 15, -14, -3, 3662 },
    { 1, 15, -3, -14, 3662 },
    { 1, 15, 6, -13, 3664 },
    { 1, 15, 13, -6, 3664 },
    { 1, 15, 13, 6, 3664 },
    { 1, 15, 6, 13, 3664 },
    { 1, 15, -6, 13, 3664 },
    { 1, 15, -13, 6, 3664 },
    { 1, 15, -13, -6, 3664 },
    { 1, 15, -6, -13, 3664 },
    { 1, 15, 8, -12, 3687 },
    { 1, 15, 12, -8, 3687 },
    { 1, 15, 12, 8, 3687 },
    { 1, 15, 8, 12, 3687 },
    { 1, 15, -8, 12, 3687 },
    { 1, 15, -12, 8, 3687 },
    { 1, 15, -12, -8, 3687 },
    { 1, 15, -8, -12, 3687 },
    { 1, 15, 4, -14, 3727 },
    { 1, 15, 14, -4, 3727 },
    { 1, 15, 14, 4, 3727 },
    { 1, 15, 4, 14, 3727 },
    { 1, 15, -4, 14, 3727 },
    { 1, 15, -14, 4, 3727 },
    { 1, 15, -14, -4, 3727 },
    { 1, 15, -4, -14, 3727 },
    { 1, 15, 7, -13, 3774 },
    { 1, 15, 13, -7, 3774 },
    { 1, 15, 13, 7, 3774 },
    { 1, 15, 7, 13, 3774 },
    { 1, 15, -7, 13, 3774 },
    { 1, 15, -13, 7, 3774 },
    { 1, 15, -13, -7, 3774 },
    { 1, 15, -7, -13, 3774 },
    { 1, 15, 10, -11, 3800 },
    { 1, 15, 11, -10, 3800 },
    { 1, 15, 11, 10, 3800 },
    { 1, 15, 10, 11, 3800 },
    { 1, 15, -10, 11, 3800 },
    { 1, 15, -11, 10, 3800 },
    { 1, 15, -11, -10, 3800 },
    { 1, 15, -10, -11, 3800 },
    { 1, 15, 5, -14, 3803 },
    { 1, 15, 14, -5, 3803 },
    { 1, 15, 14, 5, 3803 },
    { 1, 15, 5, 14, 3803 },
    { 1, 15, -5, 14, 3803 },
    { 1, 15, -14, 5, 3803 },
    { 1, 15, -14, -5, 3803 },
    { 1, 15, -5, -14, 3803 },
    { 1, 15, 0, -15, 3840 },
    { 1, 15, 15, 0, 3840 },
    { 1, 15, 0, 15, 3840 },
    { 1, 15, -15, 0, 3840 },
  };

  memcpy(lish.lighting_tables, values, sizeof(values));
  lish.lighting_tables_count = sizeof(values) / sizeof(*values);
}

static void light_stat_light_map_clear_area(MapSubtlCoord start_stl_x, MapSubtlCoord start_stl_y, MapSubtlCoord end_stl_x, MapSubtlCoord end_stl_y)
{
  MapSubtlCoord stl_x,stl_y_min_1,stl_x_min_1,stl_y;
  int64_t *light_map;
  if ( end_stl_y >= start_stl_y )
  {
    for (stl_y = start_stl_y; stl_y <= end_stl_y; stl_y++)
    {
      if ( end_stl_x >= start_stl_x )
      {
        stl_y_min_1 = stl_y - 1;
        if ( stl_y - 1 <= 0 )
        {
          stl_y_min_1 = 0;
        }
        for (stl_x = start_stl_x; stl_x <= end_stl_x; stl_x++)
        {
          light_map = &lish.stat_light_map[get_subtile_number(stl_x,stl_y)];
          stl_x_min_1 = stl_x - 1;
          if ( stl_x_min_1 < 0 )
          {
            stl_x_min_1 = 0;
          }
          struct Column *Col1 = get_map_column(get_map_block_at(stl_x,      stl_y));
          struct Column *Col2 = get_map_column(get_map_block_at(stl_x,      stl_y_min_1));
          struct Column *Col3 = get_map_column(get_map_block_at(stl_x_min_1,stl_y));
          struct Column *Col4 = get_map_column(get_map_block_at(stl_x_min_1,stl_y_min_1));
          if ( (!column_invalid(Col1)) && (!column_invalid(Col2)) && (!column_invalid(Col3)) && (!column_invalid(Col4)) )
          {
            *light_map = kfx_sim_state.light_registry.global_ambient_light << 8;
          }
          else
          {
            *light_map = 0;
          }
        }
      }
    }
  }
}

void light_stat_refresh() {
    light_request_stat_refresh();
    light_drain_shading_signals();
}

/**
 * Applies what the light registry has signalled since the last call (see
 * struct LightShadingSignals in light_registry.h): resets the lightness,
 * clears the static light map areas. Called before every shading pass, and
 * right after kfx_render changes the registry itself, so an area is always
 * cleared before any light flagged with it is shaded again.
 */
void light_drain_shading_signals(void)
{
    struct LightShadingSignals *sig = &light_shading_signals;
    if (sig->registry_replaced)
    {
        sig->registry_replaced = false;
        for (int64_t i = 1; i < LIGHTS_COUNT; i++)
        {
            struct Light *lgt = &kfx_sim_state.light_registry.lights[i];
            if ((lgt->flags & LgtF_Allocated) != 0)
            {
                lgt->flags |= LgtF_NeedUpdate;
                lgt->flags &= ~LgtF_OutOfDate;
            }
        }
        sig->areas_overflowed = true;
        sig->reset_lightness = true;
    }
    if (sig->reset_lightness)
    {
        sig->reset_lightness = false;
        clear_subtiles_lightness(&lish);
    }
    if (sig->areas_overflowed)
    {
        sig->areas_overflowed = false;
        sig->areas_count = 0;
        light_request_stat_refresh();
    }
    for (int64_t i = 0; i < sig->areas_count; i++)
    {
        const struct LightShadingArea *area = &sig->areas[i];
        light_stat_light_map_clear_area(area->x1, area->y1, area->x2, area->y2);
    }
    sig->areas_count = 0;
}

/** Builds the lighting tables the first time they're needed (was part of light_initialise()). */
static void light_initialise_shading(void)
{
    if (!lish.lighting_tables_initialised)
    {
        light_initialise_lighting_tables();
        for (int64_t i=0; i < 32; i++) {
            light_bitmask[i] = 1 << (31-i);
        }
        lish.lighting_tables_initialised = true;
    }
}

static int64_t calculate_shadow_angle(
        uint64_t pos_x,
        uint64_t pos_y,
        int64_t quadrant,
        MapSubtlCoord stl_x,
        MapSubtlCoord stl_y,
        int64_t *shadow_angle_limit_start_index,
        int64_t *shadow_angle_limit_end_index)
{
    MapSubtlCoord x = coord_subtile(pos_x);
    MapSubtlCoord y = coord_subtile(pos_y);
    int64_t shadow_end;
    int64_t result;
    int64_t shadow_start = 0;

  if ( x == stl_x )
  {
    if ( y <= stl_y )
    {
      shadow_start = LbArcTanAngle(subtile_coord(stl_x + 1, 0) - pos_x, subtile_coord(stl_y, 0) - pos_y) & ANGLE_MASK;
      shadow_end = LbArcTanAngle(subtile_coord(stl_x, 0) - pos_x, subtile_coord(stl_y, 0) - pos_y) & ANGLE_MASK;
    }
    else
    {
      shadow_start = LbArcTanAngle(subtile_coord(stl_x, 0) - pos_x - 1, subtile_coord(stl_y + 1, 0) - pos_y) & ANGLE_MASK;
      shadow_end = LbArcTanAngle(subtile_coord(stl_x + 1, 0) - pos_x, subtile_coord(stl_y + 1, 0) - pos_y) & ANGLE_MASK;
    }
  }
  else if ( y == stl_y )
  {
    if ( x <= stl_x )
    {
      shadow_start = LbArcTanAngle(subtile_coord(stl_x, 0) - pos_x, subtile_coord(stl_y, 0) - pos_y) & ANGLE_MASK;
      shadow_end = LbArcTanAngle(subtile_coord(stl_x, 0) - pos_x, subtile_coord(stl_y + 1, 0) - pos_y) & ANGLE_MASK;
    }
    else
    {
      shadow_start = LbArcTanAngle(subtile_coord(stl_x + 1, 0) - pos_x, subtile_coord(stl_y + 1, 0) - pos_y) & ANGLE_MASK;
      shadow_end = LbArcTanAngle(subtile_coord(stl_x + 1, 0) - pos_x, subtile_coord(stl_y, 0) - pos_y) & ANGLE_MASK;
    }
  }
  else
  {
    switch ( quadrant )
    {
      case 1:
        shadow_start = LbArcTanAngle(subtile_coord(stl_x, 0) - pos_x, subtile_coord(stl_y, 0) - pos_y) & ANGLE_MASK;
        shadow_end = LbArcTanAngle(subtile_coord(stl_x + 1, 0) - pos_x, subtile_coord(stl_y + 1, 0) - pos_y) & ANGLE_MASK;
        break;
      case 2:
        shadow_start = LbArcTanAngle(subtile_coord(stl_x + 1, 0) - pos_x, subtile_coord(stl_y, 0) - pos_y) & ANGLE_MASK;
        shadow_end = LbArcTanAngle(subtile_coord(stl_x, 0) - pos_x, subtile_coord(stl_y + 1, 0) - pos_y) & ANGLE_MASK;
        break;
      case 3:
        shadow_start = LbArcTanAngle(subtile_coord(stl_x + 1, 0) - pos_x, subtile_coord(stl_y + 1, 0) - pos_y) & ANGLE_MASK;
        shadow_end = LbArcTanAngle(subtile_coord(stl_x, 0) - pos_x, subtile_coord(stl_y, 0) - pos_y) & ANGLE_MASK;
        break;
      case 4:
        shadow_start = LbArcTanAngle(subtile_coord(stl_x, 0) - pos_x, subtile_coord(stl_y + 1, 0) - pos_y) & ANGLE_MASK;
        shadow_end = LbArcTanAngle(subtile_coord(stl_x + 1, 0) - pos_x, subtile_coord(stl_y, 0) - pos_y) & ANGLE_MASK;
        break;
      default:
        shadow_end = shadow_start;
        break;
    }
  }
  if ( (shadow_start / 512) << 9 != shadow_start )
    shadow_start = (shadow_start + 1) & ANGLE_MASK;
  if ( (shadow_end / 512) << 9 != shadow_end )
    shadow_end = (shadow_end - 1) & ANGLE_MASK;
  result = shadow_start;
  *shadow_angle_limit_start_index = shadow_start;
  *shadow_angle_limit_end_index = shadow_end;
  return result;
}

static TbBool point_is_above_floor(MapSubtlCoord stl_x, MapSubtlCoord stl_y, MapSubtlCoord stl_z)
{
    struct Column *col = get_column_at(stl_x, stl_y);
    return (get_column_floor_filled_subtiles(col) > stl_z);
}

//used for the hand and the illuminated property of creatures
static char light_render_light_dynamic_uncached(struct Light *lgt, int64_t radius, int64_t intensity, uint64_t max_1DD41_idx)
{
    clear_shadow_limits(&lish);
    uint64_t lighting_tables_idx = get_floor_filled_subtiles_at(lgt->mappos.x.stl.num, lgt->mappos.y.stl.num);
    if ( lighting_tables_idx <= lgt->mappos.z.stl.num )
    {
        int64_t light_position_x = lgt->mappos.x.stl.pos;
        int64_t light_position_y = lgt->mappos.y.stl.pos;
        int64_t diagonal_length = LbDiagonalLength(light_position_x, light_position_y);
        int64_t lightness = intensity * (radius - diagonal_length) / radius;
        SubtlCodedCoords light_stl_num = get_subtile_number(lgt->mappos.x.stl.num,lgt->mappos.y.stl.num);
        int64_t *stl_lightness_ptr = &lish.subtile_lightness[light_stl_num];
        if ( *stl_lightness_ptr < lightness )
            *stl_lightness_ptr = lightness;
        struct LightingTable *lighting_table_pointer = &lish.lighting_tables[0];
        lighting_tables_idx = lish.lighting_tables_count;
        if ( &lish.lighting_tables[lish.lighting_tables_count] > &lish.lighting_tables[0] )
        {
            do
            {
                lighting_tables_idx = lighting_table_pointer->distance;
                if ( lighting_tables_idx > max_1DD41_idx )
                    break;
                MapSubtlCoord stl_x = lgt->mappos.x.stl.num + lighting_table_pointer->delta_x;
                MapSubtlCoord stl_y = lgt->mappos.y.stl.num + lighting_table_pointer->delta_y;
                if (!subtile_coords_invalid(stl_x, stl_y))
                {
                    int64_t quadrant;
                    int64_t shadow_angle_limit_primary_index = LbArcTanAngle((stl_x << 8) - lgt->mappos.x.val, (stl_y << 8) - lgt->mappos.y.val) & ANGLE_MASK;
                    if ( stl_x < lgt->mappos.x.stl.num )
                    {
                        if (stl_y < lgt->mappos.y.stl.num)
                        {
                            quadrant = 4;
                        }
                        else
                        {
                            quadrant = 3;
                        }
                    }
                    else
                    {
                        if (stl_y < lgt->mappos.y.stl.num)
                        {
                            quadrant = 1;
                        }
                        else
                        {
                            quadrant = 2;
                        }
                    }
                    int64_t shadow_angle_limit_secondary_index, shadow_angle_limit_tertiary_index;
                    unsigned char height = get_floor_filled_subtiles_at(stl_x, stl_y);
                    if ( lish.shadow_limits[shadow_angle_limit_primary_index] )
                    {
                        calculate_shadow_angle(lgt->mappos.x.val, lgt->mappos.y.val, quadrant, stl_x, stl_y, &shadow_angle_limit_secondary_index, &shadow_angle_limit_tertiary_index);
                        if ( (!lish.shadow_limits[shadow_angle_limit_secondary_index] || !lish.shadow_limits[shadow_angle_limit_tertiary_index])
                            && height > lgt->mappos.z.stl.num )
                        {
                            create_shadow_limits(&lish, shadow_angle_limit_secondary_index, shadow_angle_limit_tertiary_index);
                        }
                    }
                    else
                    {
                        TbBool too_high = (height > lgt->mappos.z.stl.num);
                        if ( height > lgt->mappos.z.stl.num )
                        {
                            calculate_shadow_angle(lgt->mappos.x.val, lgt->mappos.y.val, quadrant, stl_x, stl_y, &shadow_angle_limit_secondary_index, &shadow_angle_limit_tertiary_index);
                            create_shadow_limits(&lish, shadow_angle_limit_secondary_index, shadow_angle_limit_tertiary_index);
                        }
                        TbBool should_compute_lighting;
                        if ( !too_high )
                            goto compute_lighting;
                        switch ( quadrant )
                        {
                            case 1:
                            should_compute_lighting = ( get_floor_filled_subtiles_at(stl_x - 1, stl_y - 1) <= lgt->mappos.z.stl.num );
                            break;
                            case 3:
                            should_compute_lighting = ( !point_is_above_floor(stl_x, stl_y - 1, lgt->mappos.z.stl.num) );
                            break;
                            case 4:
                            should_compute_lighting = false;
                            break;
                            default:
                            should_compute_lighting = true;
                            break;
                        }
                        if ( should_compute_lighting )
                        {
                            compute_lighting:
                            {
                                uint64_t subtile_center_y = stl_y << 8;
                                uint64_t subtile_center_x = stl_x << 8;
                                int64_t distance_x = min((lgt->mappos.x.val - subtile_center_x), (subtile_center_x - lgt->mappos.x.val));
                                int64_t distance_y = min((lgt->mappos.y.val - subtile_center_y), (subtile_center_y - lgt->mappos.y.val));
                                int64_t diagonal_length2 = LbDiagonalLength(distance_x, distance_y);
                                lighting_tables_idx = intensity * max(0, radius - diagonal_length2) / radius;
                                if ( lighting_tables_idx <= kfx_sim_state.light_registry.global_ambient_light )
                                    return lighting_tables_idx;
                                int64_t *stl_lightness_ptr2 = &lish.subtile_lightness[get_subtile_number(stl_x,stl_y)];
                                if ( *stl_lightness_ptr2 < lighting_tables_idx )
                                    *stl_lightness_ptr2 = lighting_tables_idx;
                            }
                        }
                    }
                }
                lighting_table_pointer++;
                lighting_tables_idx = lish.lighting_tables_count;
            }
        while ( &lish.lighting_tables[lish.lighting_tables_count] > lighting_table_pointer );
        }
    }
    return lighting_tables_idx;
}

static char light_render_light_dynamic(struct Light *lgt, int64_t radius, int64_t render_intensity, uint64_t lighting_tables_idx)
{
    unsigned char *shadow_limits;
    struct LightsShadows *plish = &lish;
    struct ShadowCache *shadow_cache = &plish->shadow_cache[lgt->shadow_index];
    clear_shadow_limits(plish);
    memset(shadow_cache->lighting_bitmask, 0, sizeof(shadow_cache->lighting_bitmask));
    const struct Column *col = get_column_at(lgt->mappos.x.val + 1, lgt->mappos.y.val + 1);
    SubtlCodedCoords stl_num = get_subtile_number(lgt->mappos.x.stl.num, lgt->mappos.y.stl.num);
    if (get_column_floor_filled_subtiles(col) <= lgt->mappos.z.stl.num)
    {
        shadow_cache->lighting_bitmask[lighting_tables_idx] |= 1 << (31 - lighting_tables_idx);
        int64_t diagonal_length = LbDiagonalLength(lgt->mappos.x.stl.pos, lgt->mappos.y.stl.pos);
        int64_t intensity = render_intensity * (radius - diagonal_length) / radius;
        if (plish->subtile_lightness[stl_num] < intensity)
        {
            plish->subtile_lightness[stl_num] = intensity;
        }
        struct LightingTable *lighting_table = &plish->lighting_tables[0];
        stl_num = get_subtile_number(plish->lighting_tables_count, stl_num_decode_y(stl_num));
        if (&plish->lighting_tables[plish->lighting_tables_count] > &plish->lighting_tables[0])
        {
            do
            {
                stl_num = lighting_table->distance;
                if (stl_num > lighting_tables_idx)
                {
                    break;
                }
                MapSubtlCoord stl_y = lighting_table->delta_y + lgt->mappos.y.stl.num;
                MapSubtlCoord stl_x = lighting_table->delta_x + lgt->mappos.x.stl.num;
                if (subtile_has_slab(stl_x, stl_y))
                {
                    uint64_t coord_y = stl_y << 8; // must be unsigned
                    uint64_t coord_x = stl_x << 8; // must be unsigned
                    int64_t angle = LbArcTanAngle(coord_x - lgt->mappos.x.val, coord_y - lgt->mappos.y.val) & ANGLE_MASK;
                    int64_t quadrant;
                    if (stl_x < lgt->mappos.x.stl.num)
                    {
                        quadrant = (stl_y < lgt->mappos.y.stl.num) + 3;
                    }
                    else
                    {
                        quadrant = 2 - (stl_y < lgt->mappos.y.stl.num);
                    }
                    unsigned char shadow_limit = plish->shadow_limits[angle];
                    int64_t shadow_angle_limit_index;
                    int64_t shadow_angle_limit_secondary_index;
                    if (shadow_limit)
                    {
                        calculate_shadow_angle(lgt->mappos.x.val, lgt->mappos.y.val, quadrant, stl_x, stl_y, &shadow_angle_limit_index, &shadow_angle_limit_secondary_index);
                        const struct Column *col2 = get_column_at(stl_x + 1, stl_y + 1);
                        if (((!plish->shadow_limits[shadow_angle_limit_index]) || (!plish->shadow_limits[shadow_angle_limit_secondary_index])) && (get_column_floor_filled_subtiles(col2) > lgt->mappos.z.stl.num))
                        {
                            create_shadow_limits(plish, shadow_angle_limit_index, shadow_angle_limit_secondary_index);
                        }
                    }
                    else
                    {
                        const struct Column *col3 = get_column_at(stl_x, stl_y);
                        int64_t height = get_column_floor_filled_subtiles(col3);
                        TbBool too_high = (height > lgt->mappos.z.stl.num);
                        uint64_t shadow;
                        if (too_high)
                        {
                            calculate_shadow_angle(lgt->mappos.x.val, lgt->mappos.y.val, quadrant, stl_x, stl_y, &shadow_angle_limit_index, &shadow_angle_limit_secondary_index);
                            if (shadow_angle_limit_secondary_index < shadow_angle_limit_index)
                            {
                                memset(&plish->shadow_limits[shadow_angle_limit_index], 1u, ANGLE_MASK - shadow_angle_limit_index);
                                shadow = shadow_angle_limit_secondary_index;
                                shadow_limits = &plish->shadow_limits[0];
                            }
                            else
                            {
                                shadow_limits = &plish->shadow_limits[shadow_angle_limit_index];
                                shadow = shadow_angle_limit_secondary_index - shadow_angle_limit_index;
                            }
                            memset(shadow_limits, 1u, shadow);
                        }
                        TbBool bool_2;
                        if (too_high)
                        {
                            switch (quadrant)
                            {
                                case 1:
                                {
                                    const struct Column *col4 = get_column_at(stl_x, stl_y + 1);
                                    bool_2 = (get_column_floor_filled_subtiles(col4) <= lgt->mappos.z.stl.num);
                                    break;
                                }
                                case 3:
                                {
                                    bool_2 = (!point_is_above_floor(stl_x, stl_y - 1, lgt->mappos.z.stl.num));
                                    break;
                                }
                                case 4:
                                {
                                    bool_2 = false;
                                    break;
                                }
                                default:
                                {
                                    bool_2 = true;
                                    break;
                                }
                            }
                        }
                        else
                        {
                            bool_2 = false;
                        }
                        if (!too_high || bool_2)
                        {
                            MapCoordDelta some_delta_x = min((lgt->mappos.x.val - coord_x), (coord_x - lgt->mappos.x.val));
                            MapCoordDelta some_delta_y = min((lgt->mappos.y.val - coord_y), (coord_y - lgt->mappos.y.val));
                            int64_t length = LbDiagonalLength(some_delta_x, some_delta_y);
                            stl_num = render_intensity * (radius - length) / radius;
                            if (stl_num <= kfx_sim_state.light_registry.global_ambient_light)
                            {
                                return stl_num;
                            }
                            shadow_cache->lighting_bitmask[lighting_tables_idx + lighting_table->delta_y] |= 1 << (31 - lighting_table->delta_x - (char)lighting_tables_idx);
                            SubtlCodedCoords next_stl = get_subtile_number(stl_x, stl_y);
                            if (plish->subtile_lightness[next_stl] < stl_num)
                            {
                                plish->subtile_lightness[next_stl] = stl_num;
                            }
                        }
                    }
                }
                lighting_table++;
                stl_num = get_subtile_number(plish->lighting_tables_count, stl_num_decode_y(stl_num));
            } while (&plish->lighting_tables[plish->lighting_tables_count] > lighting_table);
        }
    }
    return stl_num;
}

static int64_t light_render_light_static(struct Light *lgt, int64_t radius, int64_t intensity, SubtlCodedCoords stl_num)
{
    struct LightsShadows *plish = &lish;
    clear_shadow_limits(plish);
    struct Column *col = get_column_at(lgt->mappos.x.stl.num, lgt->mappos.y.stl.num);
    int64_t floor_filled_stls = get_column_floor_filled_subtiles(col);
    if (floor_filled_stls <= lgt->mappos.z.stl.num)
    {
        int64_t x = lgt->mappos.x.stl.pos;
        int64_t y = lgt->mappos.y.stl.pos;
        int64_t diagonal_length = LbDiagonalLength(x, y);
        int64_t lightness = intensity * (radius - diagonal_length) / radius;
        uint64_t light_map_idx = get_subtile_number(lgt->mappos.x.stl.num,lgt->mappos.y.stl.num);
        if (plish->stat_light_map[light_map_idx] < lightness)
        {
            plish->stat_light_map[light_map_idx] = lightness;
        }
        uint64_t lighting_table_idx = 0;
        for (floor_filled_stls = plish->lighting_tables_count;
             plish->lighting_tables_count > lighting_table_idx;
             floor_filled_stls = plish->lighting_tables_count)
        {
            floor_filled_stls = plish->lighting_tables[lighting_table_idx].distance;
            if (floor_filled_stls > stl_num)
            {
                break;
            }
            MapSubtlCoord stl_x = plish->lighting_tables[lighting_table_idx].delta_x + lgt->mappos.x.stl.num;
            MapSubtlCoord stl_y = plish->lighting_tables[lighting_table_idx].delta_y + lgt->mappos.y.stl.num;
            if (!subtile_coords_invalid(stl_x, stl_y))
            {
                unsigned char quadrant;
                if (stl_x < lgt->mappos.x.stl.num)
                {
                    quadrant = (stl_y < lgt->mappos.y.stl.num) + 3;
                }
                else
                {
                    quadrant = 2 - (stl_y < lgt->mappos.y.stl.num);
                }
                MapCoord coord_x = subtile_coord(stl_x, 0);
                MapCoord coord_y = subtile_coord(stl_y, 0);
                int64_t angle = LbArcTanAngle(coord_x - lgt->mappos.x.val, coord_y - lgt->mappos.y.val) & ANGLE_MASK;
                unsigned char shadow_limit = plish->shadow_limits[angle];
                int64_t shadow_start, shadow_end;
                col = get_column_at(stl_x, stl_y);
                if (shadow_limit)
                {
                    calculate_shadow_angle(lgt->mappos.x.val, lgt->mappos.y.val, quadrant, stl_x, stl_y, &shadow_start, &shadow_end);
                    if (((!plish->shadow_limits[shadow_start]) || (!plish->shadow_limits[shadow_end])) && (get_column_floor_filled_subtiles(col) > lgt->mappos.z.stl.num))
                    {
                        create_shadow_limits(plish, shadow_start, shadow_end);
                    }
                }
                else
                {
                    int64_t height = get_column_floor_filled_subtiles(col);
                    TbBool too_high = (height > lgt->mappos.z.stl.num);
                    if (height > lgt->mappos.z.stl.num)
                    {
                        calculate_shadow_angle(lgt->mappos.x.val, lgt->mappos.y.val, quadrant, stl_x, stl_y, &shadow_start, &shadow_end);
                        create_shadow_limits(plish, shadow_start, shadow_end);
                    }
                    TbBool should_compute_lighting = false;

                    if (too_high)
                    {
                        switch (quadrant)
                        {
                            case 1:
                            {
                                should_compute_lighting = (get_column_floor_filled_subtiles(col) <= lgt->mappos.z.stl.num);
                                break;
                            }
                            case 3:
                            {
                                should_compute_lighting = (!point_is_above_floor(stl_x, stl_y - 1, lgt->mappos.z.stl.num));
                                break;
                            }
                            case 4:
                            {
                                should_compute_lighting = false;
                                break;
                            }
                            default:
                            {
                                should_compute_lighting = true;
                                break;
                            }
                        }
                    }

                    if ( (should_compute_lighting) || (!too_high) )
                    {
                        floor_filled_stls = intensity * (radius - plish->lighting_tables[lighting_table_idx].diagonal_length) / radius;
                        if (floor_filled_stls <= kfx_sim_state.light_registry.global_ambient_light)
                            return floor_filled_stls;
                        SubtlCodedCoords next_stl = get_subtile_number(stl_x,stl_y);
                        if (plish->stat_light_map[next_stl] < floor_filled_stls)
                            plish->stat_light_map[next_stl] = floor_filled_stls;
                    }
                }
            }
            lighting_table_idx++;
        }
    }
    return floor_filled_stls;
}


static TbBool light_pp_skip_dynamic; /* see the per-pixel lighting block above light_render_area() */
static TbBool light_has_colour(const struct Light *lgt);
static char light_render_light(struct Light* lgt)
{
  const struct Coord3d original_mappos = lgt->mappos;
  if (lgt->reset_interpolation)
  {
      lgt->reset_interpolation = false;
      lgt->previous_mappos = lgt->mappos;
  }
  lgt->mappos.x.val = interpolate_synced(lgt->previous_mappos.x.val, lgt->mappos.x.val);
  lgt->mappos.y.val = interpolate_synced(lgt->previous_mappos.y.val, lgt->mappos.y.val);

  TbBool is_dynamic = (lgt->flags & LgtF_Dynamic) != 0;
  int64_t intensity;
  int64_t radius = lgt->radius;
  int64_t render_radius = radius;
  int64_t render_intensity;

  if ( (lgt->flags2 & 0xFE) != 0 )
  {
    if (get_gameturn() != lgt->last_turn_randomized)
    {
      lgt->previous_intensity_random = lgt->intensity_random;
      lgt->intensity_random = UNSYNC_RANDOM(513);
    }
    lgt->last_turn_randomized = get_gameturn();

    int64_t rand_minimum = (lgt->intensity - 1) << 8;
    intensity = (lgt->intensity << 8) + 257;
    render_intensity = rand_minimum + interpolate_synced(lgt->previous_intensity_random, lgt->intensity_random);
  }
  else
  {
    intensity = lgt->intensity << 8;
    render_intensity = intensity;
  }
  if ( is_dynamic )
  {
    if ( radius < lgt->min_radius * COORD_PER_STL)
      render_radius = lgt->min_radius * COORD_PER_STL;
    if ( intensity < lgt->min_intensity << 8 )
      intensity = lgt->min_intensity << 8;
  }
  if (render_radius == 0)
  {
      ERRORLOG("Light %" PRId64 " has no radius, deleting", (int64_t)(lgt->index));
      light_delete_light(lgt->index);
      light_drain_shading_signals();
      return 0;
  }
  uint64_t lighting_tables_idx;
  if ( intensity >= kfx_sim_state.light_registry.global_ambient_light << 8 )
  {
      int64_t subtile_radius = (render_radius / 256);
      if (subtile_radius == 0)
          subtile_radius++;
      int64_t intensity_per_tile = intensity / subtile_radius;
      if (intensity_per_tile == 0)
          intensity_per_tile++;

    lighting_tables_idx = ((intensity - (kfx_sim_state.light_registry.global_ambient_light << 8)) / intensity_per_tile) + 1;
    if ( lighting_tables_idx > 31 )
      lighting_tables_idx = 31;
  }
  else
  {
    lighting_tables_idx = 0;
  }

  lgt->range = lighting_tables_idx;

  if ( (radius > 0) && (render_intensity > 0) )
  {
    if ( is_dynamic && light_pp_skip_dynamic )
    {
      /* per-pixel lighting: this light is evaluated on the GPU, not baked into the grid */
    }
    else if ( !is_dynamic && light_pp_skip_dynamic && light_has_colour(lgt) )
    {
      /* per-pixel lighting: a coloured static light is evaluated on the GPU, not baked into the grid */
    }
    else if ( is_dynamic )
    {
      if ( (lgt->flags & LgtF_NeverCached) != 0 )
      {
        lighting_tables_idx = light_render_light_dynamic_uncached(lgt, radius, render_intensity, lighting_tables_idx);
      }
      else if ( (lgt->flags & LgtF_NeedUpdate) != 0 )
      {
        lighting_tables_idx = light_render_light_dynamic(lgt, radius, render_intensity, lighting_tables_idx);
        lgt->flags &= ~LgtF_NeedUpdate;
      }
      else
      {
        int64_t lighting_radius = lighting_tables_idx << 8;

        MapCoord x_start = lgt->mappos.x.val - lighting_radius;
        if ( x_start < 0 )
          x_start = 0;
        MapCoord y_start = lgt->mappos.y.val - lighting_radius;
        if ( y_start < 0 )
          y_start = 0;

        MapCoord x_end = lgt->mappos.x.val + lighting_radius;
        if ( x_end > ((kfx_sim_state.map_subtiles_x + 1) * COORD_PER_STL) - 1)
          x_end = ((kfx_sim_state.map_subtiles_x + 1) * COORD_PER_STL - 1);
        MapCoord y_end = lgt->mappos.y.val + lighting_radius;

        // Stop flickering of dynamic lights while delta time is enabled. Most noticeable with lava effect.
        if (is_dynamic)
        {
          x_start = ((x_start >> 8) << 8);
          y_start = ((y_start >> 8) << 8);
          x_end = ((x_end >> 8) << 8);
          y_end = ((y_end >> 8) << 8);
        }

        if ( y_end > ((kfx_sim_state.map_subtiles_y + 1) * COORD_PER_STL - 1) )
          y_end = ((kfx_sim_state.map_subtiles_y + 1) * COORD_PER_STL - 1);
        MapSubtlCoord stl_x = coord_subtile(x_start);
        MapSubtlCoord stl_y = coord_subtile(y_start);
        int64_t cache_x = stl_x + lighting_tables_idx - lgt->mappos.x.stl.num;
        int64_t cache_y = stl_y + lighting_tables_idx - lgt->mappos.y.stl.num;
        int64_t row_offset = stl_x - coord_subtile(x_end) + kfx_sim_state.map_subtiles_x;
        int64_t* lightness = &lish.subtile_lightness[get_subtile_number(stl_x, stl_y)];
        struct ShadowCache *shdc = &lish.shadow_cache[lgt->shadow_index];
        lighting_tables_idx = shdc->lighting_bitmask[cache_y];
        if ( y_end >= y_start )
        {
          uint32_t * shadow_cache_pointer = &shdc->lighting_bitmask[cache_y];
          MapCoord y = y_start;
          do
          {
            MapCoord x = x_start;
            for (int64_t i = cache_x; x <= x_end; i++)
            {
              if ( (light_bitmask[i] & lighting_tables_idx) != 0 )
              {
                struct Coord3d pos;
                pos.x.val = x;
                pos.y.val = y;
                MapCoordDelta dist = get_2d_distance(&lgt->mappos, &pos);
                int64_t new_lightness = render_intensity * (radius - dist) / radius;
                if ( *lightness < new_lightness )
                  *lightness = new_lightness;
              }
              x += COORD_PER_STL;
              lightness++;
            }

            lightness += row_offset;
            y += COORD_PER_STL;
            lighting_tables_idx = shadow_cache_pointer[1];
            shadow_cache_pointer++;
          }
          while ( y_end >= y );
        }
      }
    }
    else
    {
      lighting_tables_idx = light_render_light_static(lgt, radius, render_intensity, lighting_tables_idx);
    }
  }
  lgt->mappos = original_mappos;
  return lighting_tables_idx;
}

/* gpu-v2 Phase C.5 lighting pass (per-pixel lighting, Vulkan renderer only).
 * When active for a frame the dynamic lights are not rendered into the
 * per-subtile lightness grid at all (their state -- interpolation, flicker,
 * radius oscillation -- is still advanced by light_render_light(), only the
 * grid/shadow-cache work is skipped). Instead their parameters are collected
 * for the GPU, which evaluates each per pixel and casts a shadow ray from the
 * pixel to the light through a height field of the map's solid columns
 * (rebuilt here each frame around the camera). Static lights stay baked. */
#define LIGHT_PP_MAX 256
#define LIGHT_PP_FLOATS 8 /* x, y, z, radius, intensity, r, g, b */
static float light_pp_lights[LIGHT_PP_MAX * LIGHT_PP_FLOATS];
static int64_t light_pp_count = 0;
static unsigned char light_pp_heights[(MAX_SUBTILES_X + 1) * (MAX_SUBTILES_Y + 1)];
static int64_t light_pp_grid_w = 0;
static int64_t light_pp_grid_h = 0;
static TbBool light_pp_valid = false;

TbBool light_perpixel_active(void)
{
    // Only the standard perspective is supported by the GPU position reconstruction.
    return RendererPerPixelLightingActive() && (rotpers == rotpers_standard);
}

TbBool light_perpixel_get(const float **lights, int64_t *count, const unsigned char **heights, int64_t *grid_w, int64_t *grid_h)
{
    if (!light_pp_valid)
        return false;
    *lights = light_pp_lights;
    *count = light_pp_count;
    *heights = light_pp_heights;
    *grid_w = light_pp_grid_w;
    *grid_h = light_pp_grid_h;
    return true;
}

/* Record a dynamic light for per-pixel evaluation. Called after light_render_light(),
 * so the interpolation/randomisation state it maintains is current; mirrors the
 * (radius, render_intensity) it passed to the classic renderer. */
/* A light with an explicit colour is evaluated per pixel even when static (see light_render_light). */
static TbBool light_has_colour(const struct Light *lgt)
{
    return (lgt->colour_r | lgt->colour_g | lgt->colour_b) != 0;
}

static void light_perpixel_add(const struct Light *lgt)
{
    if (light_pp_count >= LIGHT_PP_MAX || (lgt->flags & LgtF_Allocated) == 0 || lgt->radius <= 0)
        return;
    int64_t render_intensity = lgt->intensity << 8;
    if ((lgt->flags2 & 0xFE) != 0)
        render_intensity = ((lgt->intensity - 1) << 8) + interpolate_synced(lgt->previous_intensity_random, lgt->intensity_random);
    if (render_intensity <= 0)
        return;
    float *out = &light_pp_lights[light_pp_count * LIGHT_PP_FLOATS];
    const TbBool dynamic = (lgt->flags & LgtF_Dynamic) != 0;
    out[0] = dynamic ? (float)interpolate_synced(lgt->previous_mappos.x.val, lgt->mappos.x.val) : (float)lgt->mappos.x.val;
    out[1] = dynamic ? (float)interpolate_synced(lgt->previous_mappos.y.val, lgt->mappos.y.val) : (float)lgt->mappos.y.val;
    out[2] = (float)lgt->mappos.z.val;
    out[3] = (float)lgt->radius;
    out[4] = (float)render_intensity / 256.0f; // 8.8 lightness -> 0..63 shade units
    if (light_has_colour(lgt))
    {
        out[5] = lgt->colour_r / 255.0f; out[6] = lgt->colour_g / 255.0f; out[7] = lgt->colour_b / 255.0f;
    } else
    {
        out[5] = out[6] = out[7] = 1.0f; // classic white
    }
    light_pp_count++;
}

/* Solid column height (in subtiles) of every subtile in the area -- what a shadow ray must clear. */
static void light_perpixel_build_heights(MapSubtlCoord startx, MapSubtlCoord starty, MapSubtlCoord endx, MapSubtlCoord endy)
{
    light_pp_grid_w = kfx_sim_state.map_subtiles_x + 1;
    light_pp_grid_h = kfx_sim_state.map_subtiles_y + 1;
    memset(light_pp_heights, 0, (size_t)(light_pp_grid_w * light_pp_grid_h));
    for (MapSubtlCoord y = starty; y <= endy; y++)
    {
        for (MapSubtlCoord x = startx; x <= endx; x++)
        {
            const struct Map *mapblk = get_map_block_at(x, y);
            if (mapblk == NULL)
                continue;
            const int64_t h = get_column_floor_filled_subtiles(get_map_column(mapblk));
            light_pp_heights[get_subtile_number(x, y)] = (unsigned char)((h > 255) ? 255 : ((h < 0) ? 0 : h));
        }
    }
}

static void light_render_area(MapSubtlCoord startx, MapSubtlCoord starty, MapSubtlCoord endx, MapSubtlCoord endy)
{
  const TbBool per_pixel = light_perpixel_active();
  light_pp_count = 0;
  light_pp_valid = false;
  light_pp_skip_dynamic = per_pixel;
  {
    // Switching per-pixel lighting on or off changes which static lights are baked into stat_light_map
    // (coloured ones are skipped while it is on): rebuild the static map once at the transition.
    static TbBool per_pixel_before = false;
    if (per_pixel != per_pixel_before)
    {
      per_pixel_before = per_pixel;
      light_stat_refresh();
    }
  }
  struct Light *lgt;
  int64_t range;
  MapSubtlDelta half_width_y;
  MapSubtlDelta half_width_x;

  light_rendered_dynamic_lights = 0;
  light_rendered_optimised_dynamic_lights = 0;
  light_updated_stat_lights = 0;
  light_out_of_date_stat_lights = 0;
  half_width_x = (endx - startx) / 2 + 1;
  half_width_y = (endy - starty) / 2 + 1;


  // this block applies to static lights
  if ( kfx_sim_state.light_registry.light_enabled )
  {
    for ( lgt = &kfx_sim_state.light_registry.lights[kfx_sim_state.thing_lists[TngList_StaticLights].index];
          lgt > kfx_sim_state.light_registry.lights;
          lgt = &kfx_sim_state.light_registry.lights[lgt->next_in_list] )
    {
      if ( (lgt->flags & (LgtF_OutOfDate | LgtF_NeedUpdate)) != 0 )
      {
        ++light_out_of_date_stat_lights;
        range = lgt->range;



        if ( (int64_t)llabs(half_width_x + startx - lgt->mappos.x.stl.num) < half_width_x + range
          && (int64_t)llabs(half_width_y + starty - lgt->mappos.y.stl.num) < half_width_y + range )
        {
          ++light_updated_stat_lights;
          light_render_light(lgt);
          lgt->flags &= ~(LgtF_OutOfDate | LgtF_NeedUpdate);
        }
      }
    }
  }


  SubtlCodedCoords start_num = get_subtile_number(startx, starty);


  if ( starty <= endy )
  {
    int64_t *stl_lightness = &lish.subtile_lightness[start_num];
    int64_t *stat_light_map = &lish.stat_light_map[start_num];

    MapSubtlDelta y = endy - starty + 1;
    do
    {
      memcpy(stl_lightness, stat_light_map, sizeof(*stl_lightness) * (endx - startx));
      stl_lightness  += (kfx_sim_state.map_subtiles_x + 1);
      stat_light_map += (kfx_sim_state.map_subtiles_x + 1);
      --y;
    }
    while ( y );
  }

  if ( kfx_sim_state.light_registry.light_enabled )
  {
    for ( lgt = &kfx_sim_state.light_registry.lights[kfx_sim_state.thing_lists[TngList_DynamLights].index]; lgt > kfx_sim_state.light_registry.lights; lgt = &kfx_sim_state.light_registry.lights[lgt->next_in_list] )
    {
      range = lgt->range;
      if ( (int64_t)llabs(half_width_x + startx - lgt->mappos.x.stl.num) < half_width_x + range
        && (int64_t)llabs(half_width_y + starty - lgt->mappos.y.stl.num) < half_width_y + range )
      {
        ++light_rendered_dynamic_lights;
        if ( (lgt->flags & LgtF_NeedUpdate) == 0 )
          ++light_rendered_optimised_dynamic_lights;
        if ( (lgt->flags & LgtF_RadiusOscillation) != 0 )
        {
          if ( lgt->radius_oscillation_direction == 1 )
          {
            if ( lgt->radius_delta + lgt->radius >= lgt->max_radius )
            {
              lgt->radius = lgt->max_radius;
              lgt->radius_oscillation_direction = 2;
            }
            else
            {
              lgt->radius += lgt->radius_delta;
            }
          }
          else if ( lgt->radius - lgt->radius_delta <= lgt->min_radius2 )
          {
            lgt->radius = lgt->min_radius2;
            lgt->radius_oscillation_direction = 1;
          }
          else
          {
            lgt->radius -= lgt->radius_delta;
          }
          lgt->flags |= LgtF_NeedUpdate;
        }
        if ( (lgt->flags & LgtF_IntensityAnimation) != 0 )
        {
          if ( lgt->intensity_toggling_field == 1 )
          {
            if ( lgt->intensity_delta + lgt->intensity >= lgt->max_intensity )
            {
              lgt->intensity = lgt->max_intensity;
              lgt->intensity_toggling_field = 2;
            }
            else
            {
              lgt->intensity = lgt->intensity_delta + lgt->intensity;
            }
          }
          else
          {
            if ( lgt->intensity - lgt->intensity_delta <= lgt->max_intensity )
            {
              lgt->intensity = lgt->max_intensity;
              lgt->intensity_toggling_field = 1;
            }
            else
            {
              lgt->intensity = lgt->intensity - lgt->intensity_delta;
            }
          }
          lgt->flags |= LgtF_NeedUpdate;
        }
        if ( lgt->force_render_update )
        {
          lgt->flags |= LgtF_NeedUpdate;
        }
        light_render_light(lgt);
        if (per_pixel)
          light_perpixel_add(lgt);
      }
    }
  }

  light_pp_skip_dynamic = false;
  if (per_pixel)
  {
    // Coloured static lights (torches, mushrooms, ...) were skipped when baking; hand them to the GPU too.
    for ( lgt = &kfx_sim_state.light_registry.lights[kfx_sim_state.thing_lists[TngList_StaticLights].index]; lgt > kfx_sim_state.light_registry.lights; lgt = &kfx_sim_state.light_registry.lights[lgt->next_in_list] )
    {
      if (light_has_colour(lgt))
        light_perpixel_add(lgt);
    }
    light_perpixel_build_heights(startx, starty, endx, endy);
    light_pp_valid = true;
  }
}

void update_light_render_area(void)
{
    int64_t subtile_x;
    int64_t subtile_y;
    int64_t startx;
    int64_t starty;
    SYNCDBG(6,"Starting");
    light_initialise_shading();
    light_drain_shading_signals();
    struct PlayerInfo* player = get_my_player();
    if (
        player->view_mode == PVM_CreatureView ||
        player->view_mode == PVM_IsoWibbleView ||
        player->view_mode == PVM_FrontView ||
        player->view_mode == PVM_IsoStraightView
    ) {
        kfx_render_state.something_light_y = LIGHT_MAX_RANGE;
        kfx_render_state.something_light_x = LIGHT_MAX_RANGE;
    }
    int64_t delta_x = llabs(kfx_render_state.something_light_x);
    int64_t delta_y = llabs(kfx_render_state.something_light_y);
    struct Camera *camera = get_player_active_camera(player);
    // Prepare the area constraints
    if (camera != NULL)
    {
      subtile_y = camera->mappos.y.stl.num;
      subtile_x = camera->mappos.x.stl.num;
    } else
    {
      subtile_y = 0;
      subtile_x = 0;
    }
//SYNCMSG("LghtRng %d,%d CamTil %d,%d",(int64_t)(kfx_render_state.something_light_x),(int64_t)(kfx_render_state.something_light_y),(int64_t)(tile_x),(int64_t)(tile_y));
    if (subtile_y > delta_y)
    {
      starty = subtile_y - delta_y;
      if (starty > kfx_sim_state.map_subtiles_y) starty = kfx_sim_state.map_subtiles_y;
    } else
      starty = 0;
    if (subtile_x > delta_x)
    {
      startx = subtile_x - delta_x;
      if (startx > kfx_sim_state.map_subtiles_x) startx = kfx_sim_state.map_subtiles_x;
    } else
      startx = 0;
    int64_t endy = subtile_y + delta_y;
    if (endy < starty) endy = starty;
    if (endy > kfx_sim_state.map_subtiles_y) endy = kfx_sim_state.map_subtiles_y;
    int64_t endx = subtile_x + delta_x;
    if (endx < startx) endx = startx;
    if (endx > kfx_sim_state.map_subtiles_x) endx = kfx_sim_state.map_subtiles_x;
    // Set the area
    light_render_area(startx, starty, endx, endy);
}

/**
 * rules can change by dkscript/lua.
 * Checks if a gamerule for lighting has changed and updates the lights if they are.
 * This function also refreshes the light status of the map.
*/
void update_global_lighting(void)
{
    if (!kfx_sim_state.light_registry.light_auto_sync)
        return;

    // Check if any values have changed
    if (
        kfx_config_state.conf.rules[0].gameplay.global_ambient_light != kfx_sim_state.light_registry.global_ambient_light ||
        kfx_config_state.conf.rules[0].gameplay.light_enabled != kfx_sim_state.light_registry.light_enabled
    ){

        // GlobalAmbientLight
        if (kfx_config_state.conf.rules[0].gameplay.global_ambient_light != kfx_sim_state.light_registry.global_ambient_light)
        {
            kfx_sim_state.light_registry.global_ambient_light = kfx_config_state.conf.rules[0].gameplay.global_ambient_light;
        }

        // LightEnabled
        if (kfx_config_state.conf.rules[0].gameplay.light_enabled != kfx_sim_state.light_registry.light_enabled)
        {
            kfx_sim_state.light_registry.light_enabled = kfx_config_state.conf.rules[0].gameplay.light_enabled;
        }

        // Refresh the lights
        light_stat_refresh();
    }
}

// Moved from game_lghtshdw.c alongside struct LightsShadows (stage 13.3,
// docs/refactor/stage-13-enforce-and-document.md).
int64_t get_subtile_lightness(const struct LightsShadows * lish_ptr, MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{
    if (stl_x > kfx_sim_state.map_subtiles_x) stl_x = kfx_sim_state.map_subtiles_x;
    if (stl_y > kfx_sim_state.map_subtiles_y) stl_y = kfx_sim_state.map_subtiles_y;
    if (stl_x < 0)  stl_x = 0;
    if (stl_y < 0) stl_y = 0;
    return lish_ptr->subtile_lightness[get_subtile_number(stl_x,stl_y)];
}

#define MINIMUM_LIGHTNESS 8192;
void clear_subtiles_lightness(struct LightsShadows * lish_ptr)
{
    for (MapSubtlCoord y = 0; y < (kfx_sim_state.map_subtiles_y + 1); y++)
    {
        for (MapSubtlCoord x = 0; x < (kfx_sim_state.map_subtiles_x + 1); x++)
        {
            int64_t* wptr = &lish_ptr->subtile_lightness[get_subtile_number(x, y)];
            *wptr = MINIMUM_LIGHTNESS; //Unsure if this value ends up being used.
        }
    }
}

void create_shadow_limits(struct LightsShadows * lish_ptr, int64_t start, int64_t end)
{
    if (start <= end)
    {
        memset(&lish_ptr->shadow_limits[start], 1, end-start);
    } else
    {
        memset(&lish_ptr->shadow_limits[start], 1, SHADOW_LIMITS_COUNT-1-start);
        memset(&lish_ptr->shadow_limits[0], 1, end);
    }
}

void clear_shadow_limits(struct LightsShadows * lish_ptr)
{
    memset(lish_ptr->shadow_limits, 0, SHADOW_LIMITS_COUNT);
}

void clear_light_system(struct LightsShadows * lish_ptr)
{
    memset(lish_ptr, 0, sizeof(struct LightsShadows));
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
