/******************************************************************************/
// Free implementation of the Dungeon Keeper strategy game.
/******************************************************************************/
/** @file terrain_rough.h
 *     Rough-terrain displacement: the pure functions.
 * @par Purpose:
 *     Unclaimed rock looks hand-dug: the faces of rough cells are subdivided
 *     and their vertices pulled into the rock by a noise depth
 *     (docs/newFeatures/terrain-v2/02-design.md section 4). Everything here is
 *     a function of a fine-lattice position (terrain_lattice.h) and the cells
 *     around it, so two faces sharing a vertex always compute the same
 *     displacement for it.
 *     Cells come from a TerrainCellSource: the map in the game
 *     (terrain_rough_map_cell_class), a small grid in the unit tests.
 * @par Comment:
 *     Integer arithmetic only.
 * @author   KeeperFX Team
 * @date     04 Oct 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_TERRAIN_ROUGH_H
#define DK_TERRAIN_ROUGH_H

#include <stdint.h>
#include "terrain_lattice.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What a cube-sized cell (subtile column, cube level) does to the vertices on its corners. */
enum TerrainCellClass {
    TCC_Open = 0,     ///< not solid
    TCC_Rough,        ///< natural rock: vertices on it may move sideways into it
    TCC_RoughFloor,   ///< an unclaimed path floor cube: vertices on it may also dip into it
    TCC_Shapeable,    ///< impenetrable rock and gems: their corners round (terrain_rough_shape()), and they
                      ///< get shapeable_percent of the noise (the map's border ring is hard-pinned)
    TCC_PlanePinned,  ///< the surface under water or lava: vertices may slide sideways, not up or down
    TCC_HardPinned,   ///< anything else solid (claimed, fortified, doors...): vertices stay
};

typedef enum TerrainCellClass (*TerrainCellClassFn)(void *ctx, int64_t stl_x, int64_t stl_y, int64_t level);
/** How rough a subtile column still is, 0..TERRAIN_WEIGHT_ONE: a wall being reinforced smooths out
 *  before it turns into a fortified one. */
typedef int64_t (*TerrainCellWeightFn)(void *ctx, int64_t stl_x, int64_t stl_y);

/** A full weight: as rough as the cell's class makes it. */
#define TERRAIN_WEIGHT_ONE 1024

struct TerrainCellSource {
    TerrainCellClassFn cls;
    void *ctx;
    TerrainCellWeightFn weight; ///< NULL: every cell at TERRAIN_WEIGHT_ONE
};

/** Axes a vertex may move along. */
enum TerrainMoveMask {
    TMM_None = 0,
    TMM_Horizontal = 1,
    TMM_Vertical = 2,
    TMM_All = 3,
};

/** The game's cells: the map as the local player sees it, classed by terrain.cfg's Roughness for the
 *  slab kind (unrevealed ground counts as rough rock, so the pattern doesn't give away what's hidden). */
enum TerrainCellClass terrain_rough_map_cell_class(void *ctx, int64_t stl_x, int64_t stl_y, int64_t level);
/** The game's weights: TERRAIN_WEIGHT_ONE less the reinforcement progress of the column's slab, as
 *  far as the local player can see it. */
int64_t terrain_rough_map_cell_weight(void *ctx, int64_t stl_x, int64_t stl_y);
extern const struct TerrainCellSource terrain_rough_map_source;

/* Reinforcement easing (render only, nothing saved): while imps reinforce a rough wall its roughness
 * eases out with their progress, and relaxes back if they stop before the wall is fortified.
 * terrain_rough_frame_begin() reads the reinforcing imps once per game turn. */

/** How far a slab's reinforcement has gone, as drawn, 0..TERRAIN_WEIGHT_ONE. */
int64_t terrain_rough_reinforcement_progress(int64_t slb_x, int64_t slb_y);
/** Holds a slab at a progress whatever the imps do (a test's fixed scene); a negative progress lets
 *  go of it. */
void terrain_rough_reinforcement_pin(int64_t slb_x, int64_t slb_y, int64_t progress);
/** Forgets every slab's progress, pinned ones included. */
void terrain_rough_reinforcement_clear(void);

/** Map units of one fine-lattice step (a subtile or cube is 256). */
#define TERRAIN_FINE_STEP (256 / TERRAIN_SUBDIV)
/** Width of the floor beyond a rounded corner's faces that stretches to meet the arc, map units (it
 *  stays inside the open subtiles next to the faces). */
#define TERRAIN_CORNER_BAND 256
/** Subtiles round an inside corner that must be clean (open floor off it, rock elsewhere) for it to
 *  be carved into a bay. */
#define TERRAIN_BAY_BLOCK 3
/** Furthest back into the floor a bay's rays start, map units: half a slab, so the bays of one dug slab
 *  don't reach into each other. */
#define TERRAIN_BAY_RAY_MAX 384
/** How deep the arch over a dead end (a TERRAIN_BAY_BLOCK wide face between two bays) digs past its
 *  face at the middle, percent of the bay radius. */
#define TERRAIN_CAP_DEPTH_PERCENT 60
/** Most an edge or corner vertex moves: sideways (both horizontal axes together), and up or down
 *  (see terrain_rough_displacement()). */
#define TERRAIN_EDGE_MOVE_CAP (TERRAIN_FINE_STEP * 3 / 4)

/** Fixed-point unit length of a pull direction. */
#define TERRAIN_DIR_ONE 1024

struct TerrainRoughParams {
    int64_t amplitude;  ///< deepest recess, map units (0 switches the displacement off)
    int64_t floor_amplitude; ///< deepest dip of a floor, map units: creatures still stand at the
                             ///< floor's own height, so this stays near the wibble's vertical +-21
    int64_t period;     ///< noise lattice spacing, in fine cells
    int64_t shapeable_percent; ///< noise on impenetrable rock and gems, percent of a rough wall's (30)
    int64_t corner_radius; ///< radius convex rock corners are rounded to, map units (0: no rounding);
                           ///< half a slab (384) makes a lone tile a circle
    uint32_t seed;
};

/** Starting values (02-design.md section 4.4): 96 units deep, floors 24, a 6-fine-cell (512 unit) period,
 *  corners rounded to 320. */
extern const struct TerrainRoughParams terrain_rough_default_params;

/** Which terrain triangles the renderer subdivides. */
enum TerrainSubdivMode {
    TSM_Off = 0,  ///< none: today's mesh
    TSM_Gated,    ///< those touching a corner that may move, with coarse edges kept where they meet the rest
    TSM_All,      ///< every one (a debug reference: no subdivided/whole boundaries anywhere)
};

/** Render-side switches; not saved. */
struct TerrainRoughSettings {
    enum TerrainSubdivMode mode;
    struct TerrainRoughParams params;
    int walls;       ///< rough walls recess (off: wall kinds are hard-pinned)
    int floors;      ///< rough floors dip (off: path floor cubes are plane-pinned, like a liquid surface)
    int ao_percent;  ///< a vertex recessed by the full amplitude is this much darker
    int normal_percent; ///< how much a wall vertex's shade follows its displaced normal (the engine's
                        ///< facing-the-camera light, per vertex instead of per face), percent
    int from_config; ///< take mode, floors and depth from the ROUGH_TERRAIN options each frame (a test or
                     ///< the KFX_ROUGH_* environment switches set this to 0 to set them themselves)
};
extern struct TerrainRoughSettings terrain_rough_settings;

/** Starts a frame: forgets the map cells cached by terrain_rough_map_cell_class(). */
void terrain_rough_frame_begin(void);

/** How the cells touching fine-lattice vertex (fx, fy, fz) let it move. */
enum TerrainMoveMask terrain_rough_move_mask(const struct TerrainCellSource *src, int64_t fx, int64_t fy, int64_t fz);

/** Direction into the solid cells touching the vertex, restricted to `mask` and normalised to
 *  TERRAIN_DIR_ONE; all zero when nothing pulls (no solid cell, or pulls that cancel out). Only
 *  floor cubes pull down: rock pulls sideways, so the top of undug ground stays flat while its
 *  edge follows the carved wall below it. Axes: x and y are map x and y, z is height (up). */
void terrain_rough_pull_dir(const struct TerrainCellSource *src, int64_t fx, int64_t fy, int64_t fz,
    enum TerrainMoveMask mask, int64_t dir[3]);

/** Noise in [0, 65535] at a lattice point: trilinear, smoothstepped value noise with one lattice
 *  cell every `period` fine cells. */
int64_t terrain_rough_noise(const struct TerrainRoughParams *prm, int64_t fx, int64_t fy, int64_t fz);

/** Recess depth in [0, amplitude] map units. */
int64_t terrain_rough_depth(const struct TerrainRoughParams *prm, int64_t fx, int64_t fy, int64_t fz);

/** The corner-rounding move of fine-lattice vertex (fx, fy, fz), map units (x, y): around each convex
 *  corner of rock (one solid column of four, the others open, none of them claimed), the square of
 *  side R inside the rock is mapped onto the same square with that corner rounded to a quarter circle
 *  of radius R (every point moved along its ray from the circle's centre), and the floor in a
 *  TERRAIN_CORNER_BAND wide band beyond the faces moves with the face beside it, fading out across
 *  the band, so it stretches to meet the arc. Neither part can fold. R is
 *  corner_radius, or less: at most half of each straight face beside the corner, so a lone slab's four
 *  arcs meet as a circle. 02-design.md section 4.5, Case A. An inside corner (three columns of rock
 *  round one open floor) with clean surroundings is carved into a rounded bay instead (Case B), and
 *  the end face of a dead end, with a bay at each end, into one arch across it (Case C). */
void terrain_rough_shape(const struct TerrainCellSource *src, const struct TerrainRoughParams *prm,
    int64_t fx, int64_t fy, int64_t fz, int64_t out[2]);

/** Whether anything moves the vertex: a pull into the rock, or a rounded corner nearby (for a subtile
 *  corner, one that moves any point in the subtiles round it). A corner of a terrain triangle that
 *  moves makes the renderer cut the triangle. */
TbBool terrain_rough_vertex_moves(const struct TerrainCellSource *src, const struct TerrainRoughParams *prm,
    int64_t fx, int64_t fy, int64_t fz);

/** Displacement of fine-lattice vertex (fx, fy, fz), map units (x, y, height), scaled by the taper
 *  taper_num / taper_den (how far the vertex is from a pinned corner). Zero if it may not move.
 *  The noise part is scaled by the least weight of the solid cells touching the vertex, a corner
 *  shape by the least weight of its corner's rock columns.
 *  Sideways it goes up to amplitude, down up to floor_amplitude, plus the corner shapes
 *  (terrain_rough_shape()). Returns how deep the noise part recessed it (what shading darkens by). */
int64_t terrain_rough_displacement(const struct TerrainCellSource *src, const struct TerrainRoughParams *prm,
    int64_t fx, int64_t fy, int64_t fz, int64_t taper_num, int64_t taper_den, int64_t out[3]);

/** terrain_rough_displacement() on the map with the current settings, remembered for the frame. */
int64_t terrain_rough_map_displacement(int64_t fx, int64_t fy, int64_t fz, int64_t taper_num, int64_t taper_den, int64_t out[3]);

/** How far the drawn floor at (map_x, map_y) dips below its real height (a negative number, map
 *  units): the floor's vertical displacement at the four lattice points round it, blended. The game
 *  doesn't use it -- creatures stand at the real height -- but the first-person camera does, so the
 *  eye keeps its height above the floor it sees. floor_level is the cube level the floor's top is at. */
int64_t terrain_rough_floor_offset_src(const struct TerrainCellSource *src, const struct TerrainRoughParams *prm,
    int64_t map_x, int64_t map_y, int64_t floor_level);
/** The same on the map, with the current settings (0 unless floors are rough). */
int64_t terrain_rough_floor_offset(int64_t map_x, int64_t map_y);

/******************************************************************************/
/* Subdividing a triangle (A, B, C) into a lattice of n cuts per edge. A sub-triangle's corners are
 * given as barycentric weights (of A, B, C) that add up to n; they keep the parent's winding.
 * An edge that is "coarse" keeps no points between its ends, so it matches a neighbour that draws
 * it whole: the strip along it is fanned to the inner points instead. */

/** Most sub-triangles terrain_subdiv_triangulate() writes (n up to TERRAIN_SUBDIV_MAX_N). */
#define TERRAIN_SUBDIV_MAX_N 8
#define TERRAIN_SUBDIV_MAX_TRIS (TERRAIN_SUBDIV_MAX_N * TERRAIN_SUBDIV_MAX_N)

struct TerrainSubTri {
    unsigned char w[3][3]; ///< w[corner][0..2]: weights of A, B, C
};

/** Triangulates for n cuts (3..TERRAIN_SUBDIV_MAX_N) with fine[0..2] telling whether edges AB, BC
 *  and CA are subdivided. Returns the number of sub-triangles written, 0 if n is out of range, or
 *  1 (the parent itself) when no edge is fine. */
int terrain_subdiv_triangulate(int n, const int fine[3], struct TerrainSubTri *out);

#ifdef __cplusplus
}
#endif
#endif
