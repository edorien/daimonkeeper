/******************************************************************************/
// Free implementation of the Dungeon Keeper strategy game.
/******************************************************************************/
/** @file terrain_audit.c
 *     Debug audit of the terrain mesh drawn in one frame.
 * @par Purpose:
 *     See terrain_audit.h.
 * @par Comment:
 *     The tables are allocated on first enable and reused frame to frame:
 *     an entry belongs to the current frame when its generation matches.
 * @author   KeeperFX Team
 * @date     04 Oct 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "terrain_audit.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <SDL3/SDL_timer.h>

#include "globals.h"
#include "bflib_basics.h"
#include "post_inc.h"

/******************************************************************************/
#define AUDIT_VERTEX_BITS 18
#define AUDIT_EDGE_BITS 20
#define AUDIT_VERTEX_SLOTS ((size_t)1 << AUDIT_VERTEX_BITS)
#define AUDIT_EDGE_SLOTS ((size_t)1 << AUDIT_EDGE_BITS)
/** Mismatches logged in detail per frame; the rest are only counted. */
#define AUDIT_MISMATCH_LOG_MAX 8

#define FNV_OFFSET 14695981039346656037ULL
#define FNV_PRIME 1099511628211ULL

struct AuditVertexSlot {
    TerrainLatticeId id;
    uint32_t gen;
    int64_t view_width;
    int64_t view_height;
    int64_t z;
};

struct AuditEdgeSlot {
    TerrainLatticeId a;
    TerrainLatticeId b;
    uint32_t gen;
    uint32_t count;
};

static int audit_enabled;
static int audit_logging;
static int audit_in_frame;
static uint32_t audit_gen;
static struct AuditVertexSlot *audit_vertices;
static struct AuditEdgeSlot *audit_edges;
static size_t *audit_edge_used;
static size_t audit_edge_used_count;
static struct TerrainAuditStats audit_cur;
static struct TerrainAuditStats audit_last;
static struct TerrainAuditCamera audit_pin;
static int audit_pinned;
static int audit_flat;

static int audit_edge_is_tjunction(TerrainLatticeId a, TerrainLatticeId b);
static int64_t audit_t_begin;
static int64_t audit_t_drawlist;

static int64_t audit_now_us(void)
{
    // SDL's clock: timespec_get() isn't in every C runtime (mingw's i686 msvcrt lacks it).
    return (int64_t)(SDL_GetTicksNS() / 1000);
}

/******************************************************************************/
static uint64_t audit_mix(uint64_t h)
{
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}

static int audit_alloc(void)
{
    if (audit_vertices != NULL)
        return 1;
    audit_vertices = (struct AuditVertexSlot *)calloc(AUDIT_VERTEX_SLOTS, sizeof(struct AuditVertexSlot));
    audit_edges = (struct AuditEdgeSlot *)calloc(AUDIT_EDGE_SLOTS, sizeof(struct AuditEdgeSlot));
    audit_edge_used = (size_t *)calloc(AUDIT_EDGE_SLOTS, sizeof(size_t));
    if ((audit_vertices == NULL) || (audit_edges == NULL) || (audit_edge_used == NULL))
    {
        free(audit_vertices);
        free(audit_edges);
        free(audit_edge_used);
        audit_vertices = NULL;
        audit_edges = NULL;
        audit_edge_used = NULL;
        ERRORLOG("Terrain audit: can't allocate its tables");
        return 0;
    }
    return 1;
}

void terrain_audit_set_enabled(int enabled)
{
    if (enabled && !audit_alloc())
        enabled = 0;
    audit_enabled = enabled;
    audit_in_frame = 0;
}

int terrain_audit_is_enabled(void)
{
    return audit_enabled;
}

void terrain_audit_set_logging(int enabled)
{
    audit_logging = enabled;
}

void terrain_audit_frame_begin(uint64_t gameturn)
{
    if (!audit_enabled)
        return;
    audit_gen++;
    if (audit_gen == 0)
    {
        // Wrapped: every stored generation could now look current.
        memset(audit_vertices, 0, AUDIT_VERTEX_SLOTS * sizeof(struct AuditVertexSlot));
        memset(audit_edges, 0, AUDIT_EDGE_SLOTS * sizeof(struct AuditEdgeSlot));
        audit_gen = 1;
    }
    memset(&audit_cur, 0, sizeof(audit_cur));
    audit_cur.frame = audit_last.frame + 1;
    audit_cur.gameturn = gameturn;
    audit_cur.poly_hash = FNV_OFFSET;
    audit_cur.geom_hash = FNV_OFFSET;
    audit_cur.tex_hash = FNV_OFFSET;
    audit_cur.shade_hash = FNV_OFFSET;
    audit_cur.mesh_hash = FNV_OFFSET;
    audit_edge_used_count = 0;
    audit_in_frame = 1;
    audit_t_begin = audit_now_us();
    audit_t_drawlist = audit_t_begin;
}

void terrain_audit_frame_end(void)
{
    size_t i;
    if (!audit_enabled || !audit_in_frame)
        return;
    for (i = 0; i < audit_edge_used_count; i++)
    {
        const struct AuditEdgeSlot *e = &audit_edges[audit_edge_used[i]];
        if (e->count == 1)
        {
            audit_cur.edges_once++;
            if (audit_edge_is_tjunction(e->a, e->b))
            {
                if (audit_cur.tjunctions < AUDIT_MISMATCH_LOG_MAX)
                {
                    JUSTLOG("TRNAUDIT: T-junction (%" PRId64 ",%" PRId64 ",%" PRId64 ")-(%" PRId64 ",%" PRId64 ",%" PRId64 ")",
                        terrain_lattice_fx(e->a), terrain_lattice_fy(e->a), terrain_lattice_fz(e->a),
                        terrain_lattice_fx(e->b), terrain_lattice_fy(e->b), terrain_lattice_fz(e->b));
                }
                audit_cur.tjunctions++;
            }
        }
        else if (e->count == 2)
            audit_cur.edges_twice++;
        else
            audit_cur.edges_more++;
    }
    {
        const int64_t now = audit_now_us();
        audit_cur.build_us = audit_t_drawlist - audit_t_begin;
        audit_cur.draw_us = now - audit_t_drawlist;
    }
    audit_last = audit_cur;
    audit_in_frame = 0;
    if (audit_logging)
    {
        JUSTLOG("TRNAUDIT: frame=%" PRIu64 " turn=%" PRIu64 " polys=%" PRIu64 " hash=%016" PRIx64
            " tris=%" PRIu64 " verts=%" PRIu64 " mismatch=%" PRIu64 " edges1=%" PRIu64 " edges2=%" PRIu64
            " edges3+=%" PRIu64 " tjunction=%" PRIu64 " foldover=%" PRIu64 " overflow=%" PRIu64 " unstamped=%" PRIu64 " disagree=%" PRIu64,
            audit_last.frame, audit_last.gameturn, audit_last.polys, audit_last.poly_hash,
            audit_last.tris, audit_last.vertices, audit_last.mismatches, audit_last.edges_once,
            audit_last.edges_twice, audit_last.edges_more, audit_last.tjunctions, audit_last.foldovers, audit_last.overflow, audit_last.unstamped,
            audit_last.stamp_disagree);
    }
}

void terrain_audit_mark_drawlist(void)
{
    if (audit_in_frame)
        audit_t_drawlist = audit_now_us();
}

void terrain_audit_note_camera(int64_t x, int64_t y, int64_t z, int64_t angle, int64_t zoom)
{
    if (!audit_in_frame)
        return;
    audit_cur.cam_x = x;
    audit_cur.cam_y = y;
    audit_cur.cam_z = z;
    audit_cur.cam_angle = angle;
    audit_cur.cam_zoom = zoom;
}

static uint64_t audit_fnv(uint64_t h, const int64_t *v, size_t n)
{
    const unsigned char *p = (const unsigned char *)v;
    size_t i;
    for (i = 0; i < n * sizeof(*v); i++)
    {
        h ^= p[i];
        h *= FNV_PRIME;
    }
    return h;
}

void terrain_audit_hash_polygon(const int64_t *all, size_t n_all, const int64_t *geom, size_t n_geom,
    const int64_t *tex, size_t n_tex, const int64_t *shade, size_t n_shade)
{
    if (!audit_in_frame)
        return;
    audit_cur.poly_hash = audit_fnv(audit_cur.poly_hash, all, n_all);
    audit_cur.geom_hash = audit_fnv(audit_cur.geom_hash, geom, n_geom);
    audit_cur.tex_hash = audit_fnv(audit_cur.tex_hash, tex, n_tex);
    audit_cur.shade_hash = audit_fnv(audit_cur.shade_hash, shade, n_shade);
    audit_cur.polys++;
}

int terrain_texel_density(const int64_t xy[3][2], const int64_t uv[3][2], double *area, double *texel_px, double *texels_per_px)
{
    // Screen edges S and texture edges T (in texels) from corner 0; the screen-to-texture map is J = T S^-1.
    const double s00 = (double)(xy[1][0] - xy[0][0]), s10 = (double)(xy[1][1] - xy[0][1]);
    const double s01 = (double)(xy[2][0] - xy[0][0]), s11 = (double)(xy[2][1] - xy[0][1]);
    const double t00 = (double)(uv[1][0] - uv[0][0]) / 65536.0, t10 = (double)(uv[1][1] - uv[0][1]) / 65536.0;
    const double t01 = (double)(uv[2][0] - uv[0][0]) / 65536.0, t11 = (double)(uv[2][1] - uv[0][1]) / 65536.0;
    const double det_s = s00 * s11 - s01 * s10;
    const double det_t = t00 * t11 - t01 * t10;
    if ((fabs(det_s) < 0.5) || (fabs(det_t) < 1e-6))
        return 0;
    // S^-1 = [s11 -s01; -s10 s00] / det_s
    const double j00 = (t00 * s11 - t01 * s10) / det_s;
    const double j01 = (t01 * s00 - t00 * s01) / det_s;
    const double j10 = (t10 * s11 - t11 * s10) / det_s;
    const double j11 = (t11 * s00 - t10 * s01) / det_s;
    // Singular values of the 2x2 J: sqrt((q +- sqrt(q^2 - 4 det^2)) / 2), q the sum of squares.
    const double q = j00 * j00 + j01 * j01 + j10 * j10 + j11 * j11;
    const double det_j = j00 * j11 - j01 * j10;
    const double disc = sqrt(fmax(q * q - 4.0 * det_j * det_j, 0.0));
    const double s_max = sqrt((q + disc) * 0.5);
    const double s_min = sqrt(fmax((q - disc) * 0.5, 0.0));
    if (s_min <= 0.0)
        return 0;
    *area = fabs(det_s) * 0.5;
    *texel_px = 1.0 / s_min;
    *texels_per_px = s_max;
    return 1;
}

static int texel_bin(double v)
{
    const int bin = (int)floor((log2(v) - TERRAIN_TEXEL_LOG2_MIN) * 2.0);
    return (bin < 0) ? 0 : ((bin >= TERRAIN_TEXEL_BINS) ? (TERRAIN_TEXEL_BINS - 1) : bin);
}

double terrain_triangle_area_in_window(const int64_t xy[3][2], int64_t w, int64_t h)
{
    // Sutherland-Hodgman against the four window edges, then the shoelace formula.
    double poly[2][16][2];
    int n = 3, e, i;
    for (i = 0; i < 3; i++)
    {
        poly[0][i][0] = (double)xy[i][0];
        poly[0][i][1] = (double)xy[i][1];
    }
    for (e = 0; e < 4; e++)
    {
        const double (*in)[2] = poly[e & 1];
        double (*out)[2] = poly[(e + 1) & 1];
        const int axis = e >> 1;
        const double limit = (e & 1) ? (double)(axis ? h : w) : 0.0;
        const double sign = (e & 1) ? -1.0 : 1.0; // inside: sign * (p - limit) >= 0
        int m = 0;
        for (i = 0; i < n; i++)
        {
            const double *p = in[i];
            const double *q = in[(i + 1) % n];
            const double dp = sign * (p[axis] - limit);
            const double dq = sign * (q[axis] - limit);
            if (dp >= 0.0)
            {
                out[m][0] = p[0];
                out[m][1] = p[1];
                m++;
            }
            if ((dp >= 0.0) != (dq >= 0.0))
            {
                const double t = dp / (dp - dq);
                out[m][0] = p[0] + (q[0] - p[0]) * t;
                out[m][1] = p[1] + (q[1] - p[1]) * t;
                m++;
            }
        }
        n = m;
        if (n == 0)
            return 0.0;
    }
    {
        const double (*fin)[2] = poly[0];
        double sum = 0.0;
        for (i = 0; i < n; i++)
            sum += fin[i][0] * fin[(i + 1) % n][1] - fin[(i + 1) % n][0] * fin[i][1];
        return fabs(sum) * 0.5;
    }
}

void terrain_audit_texel_triangle(const int64_t xy[3][2], const int64_t uv[3][2], int64_t view_w, int64_t view_h)
{
    double area, texel_px, texels_per_px;
    if (!audit_in_frame)
        return;
    if (!terrain_texel_density(xy, uv, &area, &texel_px, &texels_per_px))
        return;
    area = terrain_triangle_area_in_window(xy, view_w, view_h);
    if (area <= 0.0)
        return;
    audit_cur.texel_polys++;
    audit_cur.texel_area += area;
    audit_cur.texel_size_hist[texel_bin(texel_px)] += area;
    audit_cur.texel_rate_hist[texel_bin(texels_per_px)] += area;
}

double terrain_texel_percentile(const double hist[TERRAIN_TEXEL_BINS], double p)
{
    double total = 0.0, below = 0.0;
    int i;
    for (i = 0; i < TERRAIN_TEXEL_BINS; i++)
        total += hist[i];
    if (total <= 0.0)
        return 0.0;
    for (i = 0; i < TERRAIN_TEXEL_BINS - 1; i++)
    {
        below += hist[i];
        if (below >= p * total)
            break;
    }
    return exp2(TERRAIN_TEXEL_LOG2_MIN + (i + 0.5) * 0.5);
}

static int audit_vertex_exists(TerrainLatticeId id)
{
    size_t mask = AUDIT_VERTEX_SLOTS - 1;
    size_t slot = (size_t)audit_mix(id) & mask;
    size_t probe;
    for (probe = 0; probe < AUDIT_VERTEX_SLOTS; probe++)
    {
        const struct AuditVertexSlot *s = &audit_vertices[slot];
        if (s->gen != audit_gen)
            return 0;
        if (s->id == id)
            return 1;
        slot = (slot + 1) & mask;
    }
    return 0;
}

/** An edge drawn by one triangle only, spanning whole lattice steps of TERRAIN_SUBDIV, while the first
 *  lattice point along it is a vertex of some other triangle: the far side cut the edge, this side didn't. */
static int audit_edge_is_tjunction(TerrainLatticeId a, TerrainLatticeId b)
{
    const int64_t dx = terrain_lattice_fx(b) - terrain_lattice_fx(a);
    const int64_t dy = terrain_lattice_fy(b) - terrain_lattice_fy(a);
    const int64_t dz = terrain_lattice_fz(b) - terrain_lattice_fz(a);
    if ((terrain_lattice_tag(a) != TLT_Grid) || (terrain_lattice_tag(b) != TLT_Grid))
        return 0;
    if ((dx % TERRAIN_SUBDIV != 0) || (dy % TERRAIN_SUBDIV != 0) || (dz % TERRAIN_SUBDIV != 0))
        return 0;
    return audit_vertex_exists(terrain_lattice_id(terrain_lattice_fx(a) + dx / TERRAIN_SUBDIV,
        terrain_lattice_fy(a) + dy / TERRAIN_SUBDIV, terrain_lattice_fz(a) + dz / TERRAIN_SUBDIV, TLT_Grid));
}

static void audit_vertex(const struct TerrainAuditVertex *v)
{
    size_t mask = AUDIT_VERTEX_SLOTS - 1;
    size_t slot = (size_t)audit_mix(v->id) & mask;
    size_t probe;
    for (probe = 0; probe < AUDIT_VERTEX_SLOTS; probe++)
    {
        struct AuditVertexSlot *s = &audit_vertices[slot];
        if (s->gen != audit_gen)
        {
            s->gen = audit_gen;
            s->id = v->id;
            s->view_width = v->view_width;
            s->view_height = v->view_height;
            s->z = v->z;
            audit_cur.vertices++;
            return;
        }
        if (s->id == v->id)
        {
            if ((s->view_width != v->view_width) || (s->view_height != v->view_height) || (s->z != v->z))
            {
                if (audit_cur.mismatches < AUDIT_MISMATCH_LOG_MAX)
                {
                    JUSTLOG("TRNAUDIT: mismatch id=(%" PRId64 ",%" PRId64 ",%" PRId64 ",%d) first=(%" PRId64 ",%" PRId64 ",%" PRId64
                        ") now=(%" PRId64 ",%" PRId64 ",%" PRId64 ")",
                        terrain_lattice_fx(v->id), terrain_lattice_fy(v->id), terrain_lattice_fz(v->id), (int)terrain_lattice_tag(v->id),
                        s->view_width, s->view_height, s->z, v->view_width, v->view_height, v->z);
                }
                audit_cur.mismatches++;
            }
            return;
        }
        slot = (slot + 1) & mask;
    }
    audit_cur.overflow++;
}

static void audit_edge(TerrainLatticeId a, TerrainLatticeId b)
{
    size_t mask = AUDIT_EDGE_SLOTS - 1;
    size_t slot;
    size_t probe;
    if (a > b)
    {
        TerrainLatticeId t = a;
        a = b;
        b = t;
    }
    slot = (size_t)audit_mix(a ^ audit_mix(b)) & mask;
    for (probe = 0; probe < AUDIT_EDGE_SLOTS; probe++)
    {
        struct AuditEdgeSlot *s = &audit_edges[slot];
        if (s->gen != audit_gen)
        {
            s->gen = audit_gen;
            s->a = a;
            s->b = b;
            s->count = 1;
            audit_edge_used[audit_edge_used_count++] = slot;
            return;
        }
        if ((s->a == a) && (s->b == b))
        {
            s->count++;
            return;
        }
        slot = (slot + 1) & mask;
    }
    audit_cur.overflow++;
}

void terrain_audit_triangle(const struct TerrainAuditVertex *a, const struct TerrainAuditVertex *b, const struct TerrainAuditVertex *c)
{
    if (!audit_in_frame)
        return;
    {
        const int64_t v[12] = { (int64_t)a->id, a->view_width, a->view_height, a->z, (int64_t)b->id, b->view_width, b->view_height, b->z,
            (int64_t)c->id, c->view_width, c->view_height, c->z };
        audit_cur.mesh_hash = audit_fnv(audit_cur.mesh_hash, v, 12);
    }
    audit_cur.tris++;
    audit_vertex(a);
    audit_vertex(b);
    audit_vertex(c);
    audit_edge(a->id, b->id);
    audit_edge(b->id, c->id);
    audit_edge(c->id, a->id);
}

void terrain_audit_pin_camera(const struct TerrainAuditCamera *cam)
{
    audit_pinned = (cam != NULL);
    if (cam != NULL)
        audit_pin = *cam;
}

void terrain_audit_set_flat(int enabled)
{
    audit_flat = enabled;
}

int terrain_audit_flat(void)
{
    return audit_enabled && audit_pinned && audit_flat;
}

const struct TerrainAuditCamera *terrain_audit_pinned_camera(void)
{
    return (audit_enabled && audit_pinned) ? &audit_pin : NULL;
}

void terrain_audit_foldover(TerrainLatticeId a, TerrainLatticeId b, TerrainLatticeId c, const int64_t world[3][3])
{
    if (!audit_in_frame)
        return;
    if (audit_cur.foldovers < AUDIT_MISMATCH_LOG_MAX)
    {
        JUSTLOG("TRNAUDIT: fold (%" PRId64 ",%" PRId64 ",%" PRId64 ") (%" PRId64 ",%" PRId64 ",%" PRId64 ") (%" PRId64 ",%" PRId64 ",%" PRId64 ")"
            " now at (%" PRId64 ",%" PRId64 ",%" PRId64 ") (%" PRId64 ",%" PRId64 ",%" PRId64 ") (%" PRId64 ",%" PRId64 ",%" PRId64 ")",
            terrain_lattice_fx(a), terrain_lattice_fy(a), terrain_lattice_fz(a), terrain_lattice_fx(b), terrain_lattice_fy(b), terrain_lattice_fz(b),
            terrain_lattice_fx(c), terrain_lattice_fy(c), terrain_lattice_fz(c),
            world[0][0], world[0][1], world[0][2], world[1][0], world[1][1], world[1][2], world[2][0], world[2][1], world[2][2]);
    }
    audit_cur.foldovers++;
}

void terrain_audit_unstamped(void)
{
    if (audit_in_frame)
        audit_cur.unstamped++;
}

void terrain_audit_stamp_disagree(TerrainLatticeId id, int64_t expected_fx, int64_t expected_fy)
{
    if (!audit_in_frame)
        return;
    if (audit_cur.stamp_disagree < AUDIT_MISMATCH_LOG_MAX)
    {
        JUSTLOG("TRNAUDIT: corner id=(%" PRId64 ",%" PRId64 ",%" PRId64 ",%d) sits where (%" PRId64 ",%" PRId64 ") belongs",
            terrain_lattice_fx(id), terrain_lattice_fy(id), terrain_lattice_fz(id), (int)terrain_lattice_tag(id), expected_fx, expected_fy);
    }
    audit_cur.stamp_disagree++;
}

const struct TerrainAuditStats *terrain_audit_last(void)
{
    return &audit_last;
}
/******************************************************************************/
