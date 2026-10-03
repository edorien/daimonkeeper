/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lvl_filesdk1.h
 *     Header file for lvl_filesdk1.c.
 * @par Purpose:
 *     Level files reading routines fore standard DK1 levels.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     10 Mar 2009 - 20 Mar 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/

#ifndef DK_LVL_FILESDK1_H
#define DK_LVL_FILESDK1_H

#include "bflib_basics.h"
#include "globals.h"
#include "config_campaigns.h"
#include "config_strings.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define MAX_LIF_SIZE 65535
#define DEFAULT_LEVEL_VERSION 0

enum LoadMapFileFlags {
    LMFF_None     = 0x00,
    LMFF_Optional = 0x01,
};
/******************************************************************************/
extern char *level_strings[];
/******************************************************************************/
unsigned char *load_single_map_file_to_buffer(LevelNumber lvnum,const char *fext,int64_t *ldsize,int64_t flags);
int64_t get_level_number_from_file_name(const char *fname);
TbBool find_and_load_lif_files(void);
TbBool find_and_load_lof_files(void);
int64_t convert_old_column_file(LevelNumber lv_num);

TbBool load_map_file(LevelNumber lvnum);

// docs/refactor/editor/phase3/00-slice1-native-save.md -- general loader
// robustness, not editor-specific: derives every slab's columns (col_idx/
// columns_data[]) from its own kind via place_single_slab_type_on_map(),
// the same per-slab primitive create_blank_map()'s own "Pass 2" loop
// already uses to build a brand-new map's geometry from nothing but slab
// kinds. load_level_file() calls this when .dat/.clm are absent instead of
// silently proceeding with an empty column table. Returns false (without
// partially regenerating the rest of the map) if any slab's kind is out of
// range for the current config -- a genuinely corrupt/unregenerable map,
// not something to clamp-and-continue.
TbBool regenerate_derived_map_data(void);

// docs/refactor/editor/01-entry-and-editor-session.md §5 -- builds a fresh,
// editable blank map (earth interior, rock border, neutral ownership) at
// lvnum instead of reading one from disk. Called from kfx_game's
// init_level() in place of load_map_file() when a blank-map request is
// pending (see main_game.c's editor_request_blank_map()).
TbBool create_blank_map(LevelNumber lvnum, MapSlabCoord tiles_x, MapSlabCoord tiles_y, int64_t texture_set);

void load_map_string_data(struct GameCampaign *campgn, LevelNumber lvnum, int64_t fgroup);
void free_level_strings_data();
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
