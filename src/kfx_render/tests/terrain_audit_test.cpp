// kfx_render: terrain_audit.c, the debug audit of one frame's terrain mesh
// (docs/newFeatures/terrain-v2/03-verification-and-phases.md section A).
// Lattice ids (terrain_lattice.h) pack and unpack, a vertex emitted twice at
// one position is fine and at two positions is a mismatch, and edges are
// counted by how many triangles share them.
#include <catch2/catch_test_macros.hpp>

#include "terrain_audit.h"
#include "terrain_lattice.h"

namespace {
struct AuditOn {
    AuditOn() { terrain_audit_set_enabled(1); terrain_audit_pin_camera(nullptr); }
    ~AuditOn() { terrain_audit_set_enabled(0); }
};

TerrainAuditVertex vtx(int64_t cx, int64_t cy, int64_t level, int64_t sx, int64_t sy)
{
    TerrainAuditVertex v;
    v.id = terrain_lattice_corner_id(cx, cy, level, TLT_Grid);
    v.view_width = sx;
    v.view_height = sy;
    v.z = 100;
    return v;
}
}

TEST_CASE("terrain_lattice ids round-trip, including negative levels", "[terrain_audit]")
{
    const TerrainLatticeId id = terrain_lattice_id(1234, 56, -7, TLT_Ceiling);
    CHECK(terrain_lattice_fx(id) == 1234);
    CHECK(terrain_lattice_fy(id) == 56);
    CHECK(terrain_lattice_fz(id) == -7);
    CHECK(terrain_lattice_tag(id) == TLT_Ceiling);
    const TerrainLatticeId corner = terrain_lattice_corner_id(10, 20, 3, TLT_Grid);
    CHECK(terrain_lattice_fx(corner) == 10 * TERRAIN_SUBDIV);
    CHECK(terrain_lattice_fz(corner) == 3 * TERRAIN_SUBDIV);
    CHECK(terrain_lattice_corner_id(10, 20, 3, TLT_Grid) != terrain_lattice_corner_id(10, 20, 3, TLT_Ceiling));
}

TEST_CASE("terrain_audit counts shared edges of a quad", "[terrain_audit]")
{
    AuditOn on;
    terrain_audit_frame_begin(7);
    // A quad as two triangles sharing the diagonal (0,0)-(1,1).
    TerrainAuditVertex a = vtx(0, 0, 1, 0, 0), b = vtx(1, 0, 1, 10, 0), c = vtx(1, 1, 1, 10, 10), d = vtx(0, 1, 1, 0, 10);
    terrain_audit_triangle(&a, &b, &c);
    terrain_audit_triangle(&c, &d, &a);
    terrain_audit_frame_end();
    const TerrainAuditStats *st = terrain_audit_last();
    CHECK(st->gameturn == 7);
    CHECK(st->tris == 2);
    CHECK(st->vertices == 4);
    CHECK(st->mismatches == 0);
    CHECK(st->edges_twice == 1);
    CHECK(st->edges_once == 4);
    CHECK(st->edges_more == 0);
}

TEST_CASE("terrain_audit reports one id drawn at two positions", "[terrain_audit]")
{
    AuditOn on;
    terrain_audit_frame_begin(1);
    TerrainAuditVertex a = vtx(0, 0, 1, 0, 0), b = vtx(1, 0, 1, 10, 0), c = vtx(1, 1, 1, 10, 10);
    TerrainAuditVertex c_moved = c;
    c_moved.view_width += 1;
    TerrainAuditVertex d = vtx(0, 1, 1, 0, 10);
    terrain_audit_triangle(&a, &b, &c);
    terrain_audit_triangle(&c_moved, &d, &a);
    terrain_audit_frame_end();
    CHECK(terrain_audit_last()->mismatches == 1);
    // The next frame starts clean.
    terrain_audit_frame_begin(2);
    terrain_audit_triangle(&a, &b, &c);
    terrain_audit_frame_end();
    CHECK(terrain_audit_last()->mismatches == 0);
    CHECK(terrain_audit_last()->vertices == 3);
}

TEST_CASE("terrain_audit hashes are order-sensitive and split by part", "[terrain_audit]")
{
    AuditOn on;
    const int64_t p1[] = {1, 2, 3}, p2[] = {4, 5, 6};
    terrain_audit_frame_begin(1);
    terrain_audit_hash_polygon(p1, 3, p1, 3, p2, 3, p2, 3);
    terrain_audit_hash_polygon(p2, 3, p2, 3, p2, 3, p2, 3);
    terrain_audit_frame_end();
    const TerrainAuditStats first = *terrain_audit_last();
    terrain_audit_frame_begin(1);
    terrain_audit_hash_polygon(p2, 3, p2, 3, p2, 3, p2, 3);
    terrain_audit_hash_polygon(p1, 3, p1, 3, p2, 3, p2, 3);
    terrain_audit_frame_end();
    const TerrainAuditStats second = *terrain_audit_last();
    CHECK(first.polys == 2);
    CHECK(first.poly_hash != second.poly_hash);
    CHECK(first.geom_hash != second.geom_hash);
    CHECK(first.tex_hash == second.tex_hash);
    CHECK(first.shade_hash == second.shade_hash);
}

TEST_CASE("terrain_audit records nothing while disabled, and pins only while enabled", "[terrain_audit]")
{
    terrain_audit_set_enabled(0);
    TerrainAuditCamera cam = {1, 2, 3, 4, 5, 6, 7, 0};
    terrain_audit_pin_camera(&cam);
    CHECK(terrain_audit_pinned_camera() == nullptr);
    terrain_audit_set_enabled(1);
    REQUIRE(terrain_audit_pinned_camera() != nullptr);
    CHECK(terrain_audit_pinned_camera()->zoom == 7);
    terrain_audit_pin_camera(nullptr);
    CHECK(terrain_audit_pinned_camera() == nullptr);
    terrain_audit_set_enabled(0);
    const uint64_t frames = terrain_audit_last()->frame;
    terrain_audit_frame_begin(3);
    terrain_audit_frame_end();
    CHECK(terrain_audit_last()->frame == frames);
}
