/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file map_types.h
 *     struct Map, one subtile of the map, and pure map helpers.
 * @par Purpose:
 *     Part of kfx_model, the header-only layout library (refactor pass 2,
 *     S08, docs/refactor-pass2/stage-08-kfx-model-headers.md). Types, macros
 *     and static inline helpers only: no function prototypes, no extern data
 *     (check_layering.py --strict enforces it). The storage these types live
 *     in stays in kfx_sim, which includes this header from map_data.h.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_KFX_MODEL_MAP_TYPES_H
#define DK_KFX_MODEL_MAP_TYPES_H

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_math.h" // LbArcTanAngle, ANGLE_MASK, DEGREES_*

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

struct Map {
      unsigned char flags; // flags in enum SlabAttrFlags
      unsigned char filled_subtiles;
      unsigned char wibble_value;
      ColumnIndex col_idx;
      ThingIndex mapwho;
      PlayerBitFlags revealed;
};

#pragma pack()
/******************************************************************************/
/** The four orthogonal neighbours of a subtile, clockwise from north. */
#define SMALL_AROUND_LENGTH 4
static const struct Around small_around[SMALL_AROUND_LENGTH] = {
  { 0,-1},
  { 1, 0},
  { 0, 1},
  {-1, 0},
};

/**
 * Computes index in small_around[] array which contains coordinates directing towards given destination.
 * @param srcpos_x Source position X; either map coordinates or subtiles, but have to match type of other coords.
 * @param srcpos_y Source position Y; either map coordinates or subtiles, but have to match type of other coords.
 * @param dstpos_x Destination position X; either map coordinates or subtiles, but have to match type of other coords.
 * @param dstpos_y Destination position Y; either map coordinates or subtiles, but have to match type of other coords.
 * @return Index for small_around[] array.
 */
static inline SmallAroundIndex small_around_index_in_direction(int64_t srcpos_x, int64_t srcpos_y, int64_t dstpos_x, int64_t dstpos_y)
{
    int64_t i = ((LbArcTanAngle(dstpos_x - srcpos_x, dstpos_y - srcpos_y) & ANGLE_MASK) + DEGREES_45);
    return (i / DEGREES_90) & 3;
}

/**
 * Returns subtile coordinate for central subtile on given slab.
 */
static inline MapSubtlCoord stl_slab_center_subtile(MapSubtlCoord stl_v)
{
    return subtile_slab(stl_v)*STL_PER_SLB+1;
}

/*
 * Subtile-number encoding for a map of map_x by map_y subtiles. kfx_sim's
 * get_subtile_number()/stl_num_decode_x/y() call these with kfx_sim_state's
 * dimensions and Ariadne with the copy in kfx_pathfinding_state, so the two
 * can't drift apart.
 */
static inline SubtlCodedCoords kfx_subtile_number(MapSubtlCoord map_x, MapSubtlCoord map_y, MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{
    if (stl_x > map_x+1)
        stl_x = map_x+1;
    if (stl_y > map_y+1)
        stl_y = map_y+1;
    if (stl_x < 0)
        stl_x = 0;
    if (stl_y < 0)
        stl_y = 0;
    return stl_y*(map_x+1) + stl_x;
}

static inline MapSubtlCoord kfx_stl_num_decode_x(MapSubtlCoord map_x, SubtlCodedCoords stl_num)
{
    return stl_num % (map_x+1);
}

static inline MapSubtlCoord kfx_stl_num_decode_y(MapSubtlCoord map_x, MapSubtlCoord map_y, SubtlCodedCoords stl_num)
{
    return (stl_num/(map_x+1))%map_y;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
