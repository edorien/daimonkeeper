/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_textures.h
 *     Header file for engine_textures.c.
 * @par Purpose:
 *     Texture blocks support.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     02 Apr 2010 - 02 May 2014
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/

#ifndef DK_ENGNTEXTR_H
#define DK_ENGNTEXTR_H

#include "bflib_basics.h"
#include "globals.h"
#include "kfx_config_state.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

extern unsigned char block_mem[TEXTURE_VARIATIONS_COUNT * TEXTURE_BLOCKS_STAT_COUNT * 32 * 32];
extern unsigned char *block_ptrs[TEXTURE_VARIATIONS_COUNT * TEXTURE_BLOCKS_COUNT];
extern int64_t block_dimension;
/******************************************************************************/
void setup_texture_block_mem(void);
int64_t init_animating_texture_maps(void);
int64_t update_animating_texture_maps(void);
TbBool load_texture_map_file(uint64_t tmapidx, LevelNumber lvnum, int64_t fgroup);
/** True when a texture pack with this index can be found through the same search the loader uses
 *  (level folder, campaign config, standard data, and the mods in between). */
TbBool texture_pack_available(uint64_t tmapidx, LevelNumber lvnum, int64_t fgroup);

void scale_tmap2(int64_t texture_block_index, int64_t flags, int64_t fade_level, int64_t screen_x, int64_t screen_y, int64_t scaled_width, int64_t scaled_height);
void draw_texture(int64_t texture_x, int64_t texture_y, int64_t texture_width, int64_t texture_height, int64_t texture_block_index, int64_t flags, int64_t fade_level);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
