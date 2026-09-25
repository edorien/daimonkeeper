/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_arrays.h
 *     Header file for engine_arrays.c.
 * @par Purpose:
 *     Helper arrays for the engine.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     02 Apr 2010 - 06 Nov 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/

#ifndef DK_ENGNARR_H
#define DK_ENGNARR_H

#include "bflib_basics.h"
#include "globals.h"
#include "engine_render.h"
#include "kfx_config_state.h"

// All animations below this may have separate TD and FP sprites.
#define FP_TD_ANIMATION_COUNT        982
#define RANDOMISORS_LEN      512
#define RANDOMISORS_MASK   0x1ff
#define RANDOMISORS_RANGE     63

#define WIBBLE_TABLE_SIZE   128

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

struct WibbleTable {
  int64_t offset_x;
  int64_t offset_y;
  int64_t offset_z;
  int64_t lightness_offset;
  int64_t view_width_offset;
  int64_t view_height_offset;
};
/******************************************************************************/
extern int64_t randomisors[512];
extern struct WibbleTable wibble_table[WIBBLE_TABLE_SIZE];
extern int64_t floor_height_table[256];
extern int64_t lintel_top_height[256];
extern int64_t lintel_bottom_height[256];

#pragma pack()

extern int64_t td_to_fp_sprite_add[KEEPERSPRITE_ADD_NUM];
extern int64_t fp_to_td_sprite_add[KEEPERSPRITE_ADD_NUM];
/******************************************************************************/
extern int64_t floor_to_ceiling_map[TEXTURE_BLOCKS_COUNT];
extern struct WibbleTable blank_wibble_table[WIBBLE_TABLE_SIZE];
/******************************************************************************/
int64_t get_td_animation_sprite(int64_t animation_sprite);
int64_t get_render_animation_sprite(int64_t animation_sprite);

void init_fp_td_animation_conversion_tables(void);
void setup_mesh_randomizers(void);

void engine_init(void);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
