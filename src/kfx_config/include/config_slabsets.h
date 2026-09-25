/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_trapdoor.h
 *     Header file for config_trapdoor.c.
 * @par Purpose:
 *     Traps and doors configuration loading functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     25 May 2009 - 21 Dec 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_CFGSLABS_H
#define DK_CFGSLABS_H

#include "globals.h"
#include "bflib_basics.h"

#include "config.h"
#include "config_terrain.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
extern const struct ConfigFileData keeper_slabset_file_data;
extern const struct ConfigFileData keeper_columns_file_data;

// Moved from kfx_sim's map_columns.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- embedded by value in
// struct ColumnConfig below; kfx_config is the lowest-ranked of its real
// consumers (kfx_sim/kfx_render also read it). enum ColumnFlags and
// COLUMN_WALL_HEIGHT stay in map_columns.h since kfx_config doesn't need
// them.
#pragma pack(1)
#define COLUMNS_COUNT          16384
#define COLUMN_STACK_HEIGHT        8
struct Column { // sizeof=0x18
    int64_t use;
    unsigned char bitfields;
    int64_t solidmask;
    int64_t floor_texture;
    unsigned char orient;
    int64_t cubes[COLUMN_STACK_HEIGHT];
};
#pragma pack()

/** One column record of the original game's .clm map file: a FILE FORMAT, fixed-width on purpose (24 bytes).
 *  struct Column above is the in-memory form (64-bit fields); convert with the helpers below. */
#pragma pack(1)
struct LegacyColumn { // sizeof=0x18
    uint16_t use;
    uint8_t bitfields;
    uint16_t solidmask;
    uint16_t floor_texture;
    uint8_t orient;
    uint16_t cubes[COLUMN_STACK_HEIGHT];
};
#pragma pack()

static inline void column_from_legacy(struct Column *dst, const struct LegacyColumn *src)
{
    dst->use = src->use;
    dst->bitfields = src->bitfields;
    dst->solidmask = src->solidmask;
    dst->floor_texture = src->floor_texture;
    dst->orient = src->orient;
    for (int i = 0; i < COLUMN_STACK_HEIGHT; i++)
        dst->cubes[i] = src->cubes[i];
}

static inline void column_to_legacy(struct LegacyColumn *dst, const struct Column *src)
{
    dst->use = (uint16_t)src->use;
    dst->bitfields = src->bitfields;
    dst->solidmask = (uint16_t)src->solidmask;
    dst->floor_texture = (uint16_t)src->floor_texture;
    dst->orient = src->orient;
    for (int i = 0; i < COLUMN_STACK_HEIGHT; i++)
        dst->cubes[i] = (uint16_t)src->cubes[i];
}

struct ColumnConfig {
    int64_t columns_count;
    struct Column cols[COLUMNS_COUNT];
};

// Moved from kfx_sim's slab_data.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- config_slabsets.c writes these
// by value into kfx_sim's live slabset[]/slabobjs[] arrays while
// parsing slabset.toml/columns.toml; kfx_config is the lowest-ranked
// of their real by-value consumers. The arrays themselves stay
// kfx_sim-owned (kfx_sim_state.h) since map_blocks.c reads them
// pervasively at runtime; config_slabsets.c reaches them via
// ConfigReloadCallbacks pointer accessors.
#define SLABSET_COUNT (TERRAIN_ITEMS_MAX * SLABSETS_PER_SLAB)
#define SLABOBJS_COUNT 1024

#pragma pack(1)
struct SlabSet { // sizeof = 18
  ColumnIndex col_idx[9];
};

struct SlabObj {
  TbBool isLight;
  int64_t slabset_id;
  unsigned char stl_id;
  int64_t offset_x; // position within the subtile
  int64_t offset_y;
  int64_t offset_z;
  ThingClass class_id;
  ThingModel model; //for lights this is intencity
  unsigned char range; //radius for lights / range for effect generators
};
#pragma pack()

void clear_slabsets(void);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
