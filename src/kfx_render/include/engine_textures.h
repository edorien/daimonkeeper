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
extern long block_dimension;
/******************************************************************************/
void setup_texture_block_mem(void);
short init_animating_texture_maps(void);
short update_animating_texture_maps(void);
TbBool load_texture_map_file(unsigned long tmapidx, LevelNumber lvnum, short fgroup);
/** True when a texture pack with this index can be found through the same search the loader uses
 *  (level folder, campaign config, standard data, and the mods in between). */
TbBool texture_pack_available(unsigned long tmapidx, LevelNumber lvnum, short fgroup);

void scale_tmap2(long texture_block_index, long flags, long fade_level, long screen_x, long screen_y, long scaled_width, long scaled_height);
void draw_texture(int32_t texture_x, int32_t texture_y, int32_t texture_width, int32_t texture_height, int32_t texture_block_index, int32_t flags, int32_t fade_level);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
