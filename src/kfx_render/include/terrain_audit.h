/******************************************************************************/
// Free implementation of the Dungeon Keeper strategy game.
/******************************************************************************/
/** @file terrain_audit.h
 *     Debug audit of the terrain mesh drawn in one frame.
 * @par Purpose:
 *     Answers "is there a crack" with numbers instead of screenshots
 *     (docs/newFeatures/terrain-v2/03-verification-and-phases.md section A):
 *     - a hash of every polygon the draw list draws, to prove a refactor
 *       changed nothing;
 *     - every terrain triangle's vertices by lattice id (terrain_lattice.h):
 *       the same id emitted at two different positions is a mismatch, and
 *       edges are counted by how many triangles use them.
 *     Off by default; costs nothing but a flag test while off.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     04 Oct 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_TERRAIN_AUDIT_H
#define DK_TERRAIN_AUDIT_H

#include <stddef.h>
#include <stdint.h>
#include "terrain_lattice.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One audited frame. */
struct TerrainAuditStats {
    uint64_t frame;          ///< audited frames so far, this one included
    uint64_t gameturn;       ///< game turn the frame was drawn on
    uint64_t polys;          ///< polygons hashed from the draw list
    uint64_t poly_hash;      ///< FNV-1a over those polygons, in draw order
    uint64_t geom_hash;      ///< ... over their screen positions and depths only
    uint64_t tex_hash;       ///< ... over their texture blocks and UVs only
    uint64_t shade_hash;     ///< ... over their shades only
    uint64_t mesh_hash;      ///< FNV-1a over the registered terrain triangles (ids and positions, no shade)
    uint64_t tris;           ///< terrain triangles registered with lattice ids
    uint64_t vertices;       ///< distinct lattice ids seen
    uint64_t mismatches;     ///< emissions of an id at a different position than its first
    uint64_t edges_once;     ///< edges used by exactly one triangle
    uint64_t edges_twice;    ///< ... by exactly two
    uint64_t edges_more;     ///< ... by three or more
    uint64_t tjunctions;     ///< edges used once although a lattice point on them is a vertex: a T-junction
    uint64_t foldovers;      ///< sub-triangles turned over against their (clearly visible) parent
    uint64_t overflow;       ///< registrations dropped because a table was full
    uint64_t unstamped;      ///< triangles with a corner the mesher never named (a stale or foreign slot)
    uint64_t stamp_disagree; ///< corners whose name doesn't match their place in the mesher's rows
    int64_t cam_x, cam_y, cam_z; ///< camera position the frame was drawn from
    int64_t cam_angle;       ///< camera rotation_angle_x
    int64_t cam_zoom;        ///< camera zoom, as drawn
    int64_t build_us;        ///< microseconds from the frame's start to its draw list (building the mesh)
    int64_t draw_us;         ///< microseconds drawing the draw list
};

/** A registered triangle corner: its lattice id and where it ended up. */
struct TerrainAuditVertex {
    TerrainLatticeId id;
    int64_t view_width;
    int64_t view_height;
    int64_t z;
};

void terrain_audit_set_enabled(int enabled);
int terrain_audit_is_enabled(void);
/** Also log one "TRNAUDIT:" line per audited frame (off by default). */
void terrain_audit_set_logging(int enabled);

void terrain_audit_frame_begin(uint64_t gameturn);
/** Records the camera the frame is drawn from (call after terrain_audit_frame_begin()). */
void terrain_audit_note_camera(int64_t x, int64_t y, int64_t z, int64_t angle, int64_t zoom);
void terrain_audit_frame_end(void);
/** The mesh is built; the draw list starts (for build_us / draw_us). */
void terrain_audit_mark_drawlist(void);

/** Feeds one drawn polygon into the frame's hashes: all its values, then the
 *  geometry, texture and shade parts on their own (so a difference can be told
 *  apart from shading that follows moving lights). */
void terrain_audit_hash_polygon(const int64_t *all, size_t n_all, const int64_t *geom, size_t n_geom,
    const int64_t *tex, size_t n_tex, const int64_t *shade, size_t n_shade);
/** Registers one emitted terrain triangle. */
void terrain_audit_triangle(const struct TerrainAuditVertex *a, const struct TerrainAuditVertex *b, const struct TerrainAuditVertex *c);

/** A camera to draw the terrain from instead of the live one, so an audited frame doesn't depend on
 *  camera input or real-time interpolation. */
struct TerrainAuditCamera {
    int64_t x, y, z;
    int64_t angle_x, angle_y, angle_z;
    int64_t zoom;
    unsigned char view_mode;
};

/** Pins the camera draw_view() uses (NULL unpins). Only while the audit is enabled. */
void terrain_audit_pin_camera(const struct TerrainAuditCamera *cam);
const struct TerrainAuditCamera *terrain_audit_pinned_camera(void);

/** A displaced sub-triangle (corners a, b, c, now at world[0..2], times TERRAIN_SUBDIV) faces the
 *  other way from its parent. */
void terrain_audit_foldover(TerrainLatticeId a, TerrainLatticeId b, TerrainLatticeId c, const int64_t world[3][3]);
/** A terrain triangle had a corner with no lattice id this frame (it isn't registered). */
void terrain_audit_unstamped(void);
/** A corner's lattice id names another subtile corner than its slot in the mesher's rows. */
void terrain_audit_stamp_disagree(TerrainLatticeId id, int64_t expected_fx, int64_t expected_fy);

/** Hole finding: while the camera is pinned, clear the view to black and draw terrain at one bright
 *  shade, so any black pixel inside it is a gap in the mesh. */
void terrain_audit_set_flat(int enabled);
int terrain_audit_flat(void);

/** Stats of the last finished frame (all zero before the first). */
const struct TerrainAuditStats *terrain_audit_last(void);

#ifdef __cplusplus
}
#endif
#endif
