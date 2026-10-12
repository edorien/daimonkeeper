/******************************************************************************/
// Free implementation of the Dungeon Keeper strategy game.
/******************************************************************************/
/** @file terrain_lattice.h
 *     Identity of terrain mesh vertices on the fine lattice.
 * @par Purpose:
 *     Every terrain vertex -- a subtile corner the mesher computes, or a point
 *     added when a face is subdivided -- is named by its integer position on a
 *     lattice TERRAIN_SUBDIV times finer than the subtile/cube grid. Two faces
 *     that need "the same" vertex ask for the same id, which is what lets the
 *     mesh audit (terrain_audit.h) check that they also got the same position.
 *     docs/newFeatures/terrain-v2/02-design.md section 1.
 * @par Comment:
 *     Header-only.
 * @author   KeeperFX Team
 * @date     04 Oct 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_TERRAIN_LATTICE_H
#define DK_TERRAIN_LATTICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Cuts per subtile edge (and per cube height). Code must not assume 3. */
#define TERRAIN_SUBDIV 3

/** Which family of vertex slots an id names. Grid vertices sit on the subtile
 *  corner / cube level grid (abyss wall slots below level 0 use negative z);
 *  the ceiling has a slot of its own whose height isn't a cube level. */
enum TerrainLatticeTag {
    TLT_Grid = 0,
    TLT_Ceiling = 1,
};

typedef uint64_t TerrainLatticeId;

/** Packs a fine-lattice position: x and y as unsigned 16 bits, z as signed 16 bits, plus a tag. */
static inline TerrainLatticeId terrain_lattice_id(int64_t fx, int64_t fy, int64_t fz, enum TerrainLatticeTag tag)
{
    return ((uint64_t)(uint16_t)fx << 48) | ((uint64_t)(uint16_t)fy << 32)
         | ((uint64_t)(uint16_t)(int16_t)fz << 16) | (uint64_t)(uint16_t)tag;
}

/** The id of a subtile corner (corner_x, corner_y) at cube level `level`. */
static inline TerrainLatticeId terrain_lattice_corner_id(int64_t corner_x, int64_t corner_y, int64_t level, enum TerrainLatticeTag tag)
{
    return terrain_lattice_id(corner_x * TERRAIN_SUBDIV, corner_y * TERRAIN_SUBDIV, level * TERRAIN_SUBDIV, tag);
}

static inline int64_t terrain_lattice_fx(TerrainLatticeId id) { return (int64_t)(uint16_t)(id >> 48); }
static inline int64_t terrain_lattice_fy(TerrainLatticeId id) { return (int64_t)(uint16_t)(id >> 32); }
static inline int64_t terrain_lattice_fz(TerrainLatticeId id) { return (int64_t)(int16_t)(uint16_t)(id >> 16); }
static inline enum TerrainLatticeTag terrain_lattice_tag(TerrainLatticeId id) { return (enum TerrainLatticeTag)(uint16_t)id; }

#ifdef __cplusplus
}
#endif
#endif
