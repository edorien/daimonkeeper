// kfx_render: terrain_audit.c, the debug audit of one frame's terrain mesh
// (docs/newFeatures/terrain-v2/03-verification-and-phases.md section A).
// Lattice ids (terrain_lattice.h) pack and unpack, a vertex emitted twice at
// one position is fine and at two positions is a mismatch, and edges are
// counted by how many triangles share them.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

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

// Texel density (docs/newFeatures/texture-resolution/03-verification-and-phases.md section A).
TEST_CASE("terrain_texel_density: a block drawn 96 pixels wide shows 3-pixel texels", "[terrain_audit][texel]")
{
    // A whole 32-texel block (0..0x1FFFFF is just under 32.0) on a 96x96 pixel right triangle.
    const int64_t xy[3][2] = { { 10, 10 }, { 106, 10 }, { 10, 106 } };
    const int64_t uv[3][2] = { { 0, 0 }, { 32 << 16, 0 }, { 0, 32 << 16 } };
    double area = 0, texel_px = 0, rate = 0;
    REQUIRE(terrain_texel_density(xy, uv, &area, &texel_px, &rate) == 1);
    CHECK(area == Catch::Approx(96.0 * 96.0 / 2.0));
    CHECK(texel_px == Catch::Approx(3.0));
    CHECK(rate == Catch::Approx(1.0 / 3.0));
}

TEST_CASE("terrain_texel_density: anisotropy, rotation and winding", "[terrain_audit][texel]")
{
    double area = 0, texel_px = 0, rate = 0;
    SECTION("squashed: the texel is 4 pixels one way and half a pixel the other")
    {
        const int64_t xy[3][2] = { { 0, 0 }, { 128, 0 }, { 0, 16 } };
        const int64_t uv[3][2] = { { 0, 0 }, { 32 << 16, 0 }, { 0, 32 << 16 } };
        REQUIRE(terrain_texel_density(xy, uv, &area, &texel_px, &rate) == 1);
        CHECK(texel_px == Catch::Approx(4.0));   // most magnified: 128 px over 32 texels
        CHECK(rate == Catch::Approx(2.0));       // most minified: 32 texels over 16 px
    }
    SECTION("rotated 90 degrees in the texture and wound the other way: same numbers")
    {
        const int64_t xy[3][2] = { { 0, 0 }, { 0, 64 }, { 64, 0 } };
        const int64_t uv[3][2] = { { 0, 0 }, { 32 << 16, 0 }, { 0, 32 << 16 } };
        REQUIRE(terrain_texel_density(xy, uv, &area, &texel_px, &rate) == 1);
        CHECK(texel_px == Catch::Approx(2.0));
        CHECK(rate == Catch::Approx(0.5));
        CHECK(area == Catch::Approx(64.0 * 64.0 / 2.0));
    }
    SECTION("degenerate on screen or in the texture: no sample")
    {
        const int64_t line[3][2] = { { 0, 0 }, { 10, 10 }, { 20, 20 } };
        const int64_t uv[3][2] = { { 0, 0 }, { 32 << 16, 0 }, { 0, 32 << 16 } };
        const int64_t xy[3][2] = { { 0, 0 }, { 64, 0 }, { 0, 64 } };
        const int64_t flat_uv[3][2] = { { 0, 0 }, { 32 << 16, 0 }, { 64 << 16, 0 } };
        CHECK(terrain_texel_density(line, uv, &area, &texel_px, &rate) == 0);
        CHECK(terrain_texel_density(xy, flat_uv, &area, &texel_px, &rate) == 0);
    }
}

TEST_CASE("terrain_audit texel histograms: area-weighted, reset per frame, percentiles", "[terrain_audit][texel]")
{
    AuditOn on;
    terrain_audit_frame_begin(1);
    // 3-pixel texels over 4608 px of area, 1-pixel texels over 512 px.
    const int64_t big[3][2] = { { 0, 0 }, { 96, 0 }, { 0, 96 } };
    const int64_t small[3][2] = { { 0, 0 }, { 32, 0 }, { 0, 32 } };
    const int64_t uv[3][2] = { { 0, 0 }, { 32 << 16, 0 }, { 0, 32 << 16 } };
    terrain_audit_texel_triangle(big, uv, 640, 480);
    terrain_audit_texel_triangle(small, uv, 640, 480);
    terrain_audit_frame_end();
    const TerrainAuditStats *st = terrain_audit_last();
    CHECK(st->texel_polys == 2);
    CHECK(st->texel_area == Catch::Approx(4608.0 + 512.0));
    // Half-octave bins: 3 px falls in [2^1.5, 2^2), 1 px in [2^0, 2^0.5).
    CHECK(terrain_texel_percentile(st->texel_size_hist, 0.05) == Catch::Approx(exp2(0.25)));
    CHECK(terrain_texel_percentile(st->texel_size_hist, 0.50) == Catch::Approx(exp2(1.75)));
    CHECK(terrain_texel_percentile(st->texel_rate_hist, 0.50) == Catch::Approx(exp2(-1.75))); // 1/3 in [2^-2, 2^-1.5)
    double empty[TERRAIN_TEXEL_BINS] = {};
    CHECK(terrain_texel_percentile(empty, 0.5) == 0.0);
    terrain_audit_frame_begin(2);
    terrain_audit_frame_end();
    CHECK(terrain_audit_last()->texel_polys == 0);
    CHECK(terrain_audit_last()->texel_area == 0.0);
}

TEST_CASE("terrain_triangle_area_in_window: clipped to the view, off-screen parts don't count", "[terrain_audit][texel]")
{
    const int64_t inside[3][2] = { { 10, 10 }, { 106, 10 }, { 10, 106 } };
    CHECK(terrain_triangle_area_in_window(inside, 640, 480) == Catch::Approx(96.0 * 96.0 / 2.0));
    // Half of it left of x = 0: a 100x100 right triangle cut by the y axis keeps the part from 0 to 50.
    const int64_t half[3][2] = { { -50, 0 }, { 50, 0 }, { -50, 100 } };
    CHECK(terrain_triangle_area_in_window(half, 640, 480) == Catch::Approx(50.0 * 50.0 / 2.0));
    // Huge and covering the window: the window's area.
    const int64_t huge[3][2] = { { -100000, -100000 }, { 100000, -100000 }, { 0, 100000 } };
    CHECK(terrain_triangle_area_in_window(huge, 640, 480) == Catch::Approx(640.0 * 480.0));
    const int64_t outside[3][2] = { { 700, 10 }, { 800, 10 }, { 700, 100 } };
    CHECK(terrain_triangle_area_in_window(outside, 640, 480) == 0.0);
}

TEST_CASE("terrain_audit texel histograms weight by the area on screen", "[terrain_audit][texel]")
{
    AuditOn on;
    terrain_audit_frame_begin(1);
    const int64_t uv[3][2] = { { 0, 0 }, { 32 << 16, 0 }, { 0, 32 << 16 } };
    const int64_t off_screen[3][2] = { { 1000, 1000 }, { 1096, 1000 }, { 1000, 1096 } };
    const int64_t half[3][2] = { { -48, 0 }, { 48, 0 }, { -48, 96 } };
    terrain_audit_texel_triangle(off_screen, uv, 640, 480);
    terrain_audit_texel_triangle(half, uv, 640, 480);
    terrain_audit_frame_end();
    CHECK(terrain_audit_last()->texel_polys == 1);
    CHECK(terrain_audit_last()->texel_area == Catch::Approx(48.0 * 48.0 / 2.0));
}
