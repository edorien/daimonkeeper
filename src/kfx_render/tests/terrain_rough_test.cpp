// kfx_render: terrain_rough.c, the pure functions behind rough terrain
// (docs/newFeatures/terrain-v2/02-design.md section 4): which way a lattice
// vertex may move and is pulled, how deep, and how a subdivided triangle is
// cut so its coarse edges still meet an undivided neighbour. Cells come from
// a small synthetic grid, not the map.
#include <catch2/catch_test_macros.hpp>

#include "terrain_rough.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace {
const int64_t N = TERRAIN_SUBDIV;

/** Cells keyed by (x, y, level); anything not set is open. */
struct Grid {
    std::map<std::tuple<int64_t, int64_t, int64_t>, TerrainCellClass> cells;
    std::map<std::pair<int64_t, int64_t>, int64_t> weights; ///< columns not set weigh TERRAIN_WEIGHT_ONE
    void set(int64_t x, int64_t y, int64_t lvl, TerrainCellClass c) { cells[std::make_tuple(x, y, lvl)] = c; }
    TerrainCellSource source() { return TerrainCellSource{ &Grid::cls, this, &Grid::weight }; }
    static int64_t weight(void *ctx, int64_t x, int64_t y)
    {
        const Grid *g = static_cast<const Grid *>(ctx);
        auto it = g->weights.find(std::make_pair(x, y));
        return it == g->weights.end() ? TERRAIN_WEIGHT_ONE : it->second;
    }
    static TerrainCellClass cls(void *ctx, int64_t x, int64_t y, int64_t lvl)
    {
        const Grid *g = static_cast<const Grid *>(ctx);
        auto it = g->cells.find(std::make_tuple(x, y, lvl));
        return it == g->cells.end() ? TCC_Open : it->second;
    }
};

/** A corridor along y: rough rock for x < 5 (levels 0..4), the floor cube at level 0 for x >= 5 of
 *  the given class (or none, for liquid: then level -1 is the plane-pinned surface). */
Grid corridor(TerrainCellClass floor, bool liquid = false)
{
    Grid g;
    for (int64_t y = 0; y < 10; y++)
    {
        for (int64_t x = 0; x < 5; x++)
        {
            for (int64_t l = -1; l < 5; l++)
                g.set(x, y, l, TCC_Rough);
        }
        for (int64_t x = 5; x < 10; x++)
        {
            if (liquid)
                g.set(x, y, -1, TCC_PlanePinned);
            else
            {
                g.set(x, y, -1, floor);
                g.set(x, y, 0, floor);
            }
        }
    }
    return g;
}

void dir_of(Grid &g, int64_t fx, int64_t fy, int64_t fz, int64_t out[3])
{
    TerrainCellSource src = g.source();
    terrain_rough_pull_dir(&src, fx, fy, fz, terrain_rough_move_mask(&src, fx, fy, fz), out);
}

TerrainMoveMask mask_of(Grid &g, int64_t fx, int64_t fy, int64_t fz)
{
    TerrainCellSource src = g.source();
    return terrain_rough_move_mask(&src, fx, fy, fz);
}
}

TEST_CASE("terrain_rough: a wall face vertex is pulled straight into the rock", "[terrain_rough]")
{
    Grid g = corridor(TCC_RoughFloor);
    int64_t d[3];
    // On the face x = 5 subtiles, inside a subtile along y and inside cube level 2.
    dir_of(g, 5 * N, 3 * N + 1, 2 * N + 1, d);
    CHECK(d[0] == -TERRAIN_DIR_ONE);
    CHECK(d[1] == 0);
    CHECK(d[2] == 0);
    CHECK(mask_of(g, 5 * N, 3 * N + 1, 2 * N + 1) == TMM_All);
}

TEST_CASE("terrain_rough: a path floor vertex dips straight down", "[terrain_rough]")
{
    Grid g = corridor(TCC_RoughFloor);
    int64_t d[3];
    dir_of(g, 7 * N + 1, 3 * N + 2, 1 * N, d);
    CHECK(d[0] == 0);
    CHECK(d[1] == 0);
    CHECK(d[2] == -TERRAIN_DIR_ONE);
}

TEST_CASE("terrain_rough: floors dip at most floor_amplitude, walls recess further", "[terrain_rough]")
{
    Grid g = corridor(TCC_RoughFloor);
    TerrainCellSource src = g.source();
    TerrainRoughParams prm = terrain_rough_default_params;
    prm.corner_radius = 0; // the noise alone
    int64_t deepest_floor = 0, deepest_wall = 0;
    for (int64_t fx = 5 * N + 1; fx < 9 * N; fx++)
        for (int64_t fy = 0; fy < 9 * N; fy++)
        {
            int64_t out[3];
            terrain_rough_displacement(&src, &prm, fx, fy, N, 1, 1, out);
            REQUIRE(out[2] <= 0);
            REQUIRE(-out[2] <= prm.floor_amplitude);
            deepest_floor = out[2] < deepest_floor ? out[2] : deepest_floor;
            terrain_rough_displacement(&src, &prm, 5 * N, fy, 2 * N + 1, 1, 1, out);
            deepest_wall = out[0] < deepest_wall ? out[0] : deepest_wall;
        }
    CHECK(-deepest_floor > prm.floor_amplitude / 2);
    CHECK(-deepest_wall > prm.floor_amplitude * 2);
}

TEST_CASE("terrain_rough: the path/wall join is pulled diagonally into both", "[terrain_rough]")
{
    Grid g = corridor(TCC_RoughFloor);
    int64_t d[3];
    dir_of(g, 5 * N, 3 * N + 1, 1 * N, d);
    CHECK(d[0] < 0);
    CHECK(d[1] == 0);
    CHECK(d[2] < 0);
    CHECK(d[0] == d[2]);
    CHECK(std::llabs(d[0] * d[0] + d[2] * d[2] - TERRAIN_DIR_ONE * TERRAIN_DIR_ONE) < 3 * TERRAIN_DIR_ONE);
}

TEST_CASE("terrain_rough: a claimed floor pins its edge, the wall above still moves", "[terrain_rough]")
{
    Grid g = corridor(TCC_HardPinned);
    CHECK(mask_of(g, 5 * N, 3 * N + 1, 1 * N) == TMM_None);       // the join
    CHECK(mask_of(g, 7 * N + 1, 3 * N + 1, 1 * N) == TMM_None);   // the floor itself
    CHECK(mask_of(g, 5 * N, 3 * N + 1, 1 * N + 1) == TMM_All);    // the wall, just above the join
}

TEST_CASE("terrain_rough: beside water the wall recesses sideways down to the waterline", "[terrain_rough]")
{
    Grid g = corridor(TCC_Open, true);
    int64_t d[3];
    CHECK(mask_of(g, 5 * N, 3 * N + 1, 0) == TMM_Horizontal);
    dir_of(g, 5 * N, 3 * N + 1, 0, d);
    CHECK(d[0] == -TERRAIN_DIR_ONE);
    CHECK(d[1] == 0);
    CHECK(d[2] == 0);
    // The water surface itself doesn't move.
    CHECK(mask_of(g, 7 * N + 1, 3 * N + 1, 0) == TMM_None);
}

TEST_CASE("terrain_rough: a reinforced neighbour pins the shared edge", "[terrain_rough]")
{
    Grid g = corridor(TCC_RoughFloor);
    for (int64_t l = -1; l < 5; l++)
        g.set(4, 6, l, TCC_HardPinned);
    // Vertical edge between the rough column (4,5) and the reinforced (4,6), on the face x = 5.
    CHECK(mask_of(g, 5 * N, 6 * N, 2 * N + 1) == TMM_None);
    CHECK(mask_of(g, 5 * N, 5 * N + 1, 2 * N + 1) == TMM_All);
}

TEST_CASE("terrain_rough: a convex corner is pulled diagonally into its column", "[terrain_rough]")
{
    Grid g;
    for (int64_t l = 0; l < 5; l++)
        g.set(5, 5, l, TCC_Rough);
    int64_t d[3];
    dir_of(g, 5 * N, 5 * N, 2 * N + 1, d);   // the column's (-x, -y) vertical edge
    CHECK(d[0] > 0);
    CHECK(d[1] > 0);
    CHECK(d[0] == d[1]);
    CHECK(d[2] == 0);
    dir_of(g, 6 * N, 6 * N, 2 * N + 1, d);   // its (+x, +y) edge
    CHECK(d[0] < 0);
    CHECK(d[1] < 0);
}

TEST_CASE("terrain_rough: the top of undug rock stays flat, its edge follows the wall", "[terrain_rough]")
{
    Grid g = corridor(TCC_RoughFloor);
    int64_t d[3];
    // On top of the rock (level 5 boundary), away from its edge: nothing pulls.
    dir_of(g, 2 * N + 1, 3 * N + 1, 5 * N, d);
    CHECK((d[0] == 0 && d[1] == 0 && d[2] == 0));
    // Its edge above the wall face: sideways into the rock, not down...
    dir_of(g, 5 * N, 3 * N + 1, 5 * N, d);
    CHECK(d[0] == -TERRAIN_DIR_ONE);
    CHECK(d[2] == 0);
    // ...and, being an edge, by less than a fine cell, so it can't pass the next vertex on the top.
    TerrainCellSource src = g.source();
    TerrainRoughParams prm = terrain_rough_default_params;
    prm.corner_radius = 0; // the noise alone
    prm.amplitude = 128;
    for (int64_t fy = 0; fy < 9 * N; fy++)
    {
        int64_t out[3];
        terrain_rough_displacement(&src, &prm, 5 * N, fy, 5 * N, 1, 1, out);
        REQUIRE(std::llabs(out[0]) <= TERRAIN_EDGE_MOVE_CAP);
    }
}

TEST_CASE("terrain_rough: nothing moves without a rough cell, or when pulls cancel", "[terrain_rough]")
{
    Grid g;
    int64_t d[3];
    CHECK(mask_of(g, 4, 4, 4) == TMM_None);
    dir_of(g, 4, 4, 4, d);
    CHECK((d[0] == 0 && d[1] == 0 && d[2] == 0));
    // Inside solid rock: every touching cell is rough, the pulls cancel.
    for (int64_t x = 0; x < 3; x++)
        for (int64_t y = 0; y < 3; y++)
            for (int64_t l = 0; l < 3; l++)
                g.set(x, y, l, TCC_Rough);
    dir_of(g, N, N, N, d);
    CHECK((d[0] == 0 && d[1] == 0 && d[2] == 0));
}

TEST_CASE("terrain_rough: edge and corner vertices move less than a fine cell along any axis", "[terrain_rough]")
{
    // The top of an inside corner of a two-cube wall: pulled along the bisector, into the rock.
    Grid g;
    for (int64_t l = 0; l < 2; l++)
    {
        g.set(4, 4, l, TCC_Rough);
        g.set(4, 5, l, TCC_Rough);
        g.set(5, 4, l, TCC_Rough);
    }
    TerrainCellSource src = g.source();
    TerrainRoughParams prm = terrain_rough_default_params;
    prm.corner_radius = 0; // the noise alone
    prm.amplitude = 128;
    int64_t out[3];
    for (int64_t fz = 2 * N; fz <= 2 * N; fz++)
    {
        terrain_rough_displacement(&src, &prm, 5 * N, 5 * N, fz, 1, 1, out);
        CHECK(out[0] < 0);
        CHECK(out[1] < 0);
        CHECK(out[2] == 0); // rock doesn't pull down
        CHECK(std::llabs(out[0]) + std::llabs(out[1]) <= TERRAIN_EDGE_MOVE_CAP);
    }
    // A face-interior vertex keeps the full depth along its normal.
    Grid wall = corridor(TCC_RoughFloor);
    TerrainCellSource wsrc = wall.source();
    int64_t deepest = 0;
    for (int64_t fy = 0; fy < 9 * N; fy++)
    {
        if (fy % N == 0)
            continue;
        terrain_rough_displacement(&wsrc, &prm, 5 * N, fy, 2 * N + 1, 1, 1, out);
        deepest = out[0] < deepest ? out[0] : deepest;
    }
    CHECK(-deepest > TERRAIN_EDGE_MOVE_CAP);
}

TEST_CASE("terrain_rough: noise is deterministic, bounded and smooth enough not to fold", "[terrain_rough]")
{
    TerrainRoughParams prm = terrain_rough_default_params;
    prm.corner_radius = 0; // the noise alone
    const int64_t max_step = prm.amplitude * 3 / (2 * prm.period) + 1;
    for (int64_t z = -6; z < 30; z += 5)
        for (int64_t y = 0; y < 60; y += 7)
            for (int64_t x = -3; x < 60; x++)
            {
                const int64_t n0 = terrain_rough_noise(&prm, x, y, z);
                REQUIRE(n0 == terrain_rough_noise(&prm, x, y, z));
                REQUIRE(n0 >= 0);
                REQUIRE(n0 <= 65535);
                const int64_t d0 = terrain_rough_depth(&prm, x, y, z);
                REQUIRE(d0 >= 0);
                REQUIRE(d0 <= prm.amplitude);
                REQUIRE(std::llabs(terrain_rough_depth(&prm, x + 1, y, z) - d0) <= max_step);
                REQUIRE(std::llabs(terrain_rough_depth(&prm, x, y + 1, z) - d0) <= max_step);
                REQUIRE(std::llabs(terrain_rough_depth(&prm, x, y, z + 1) - d0) <= max_step);
            }
    // Not flat: the field takes a spread of values.
    int64_t lo = 65535, hi = 0;
    for (int64_t x = 0; x < 200; x++)
    {
        const int64_t v = terrain_rough_noise(&prm, x, 3 * x, 7);
        lo = v < lo ? v : lo;
        hi = v > hi ? v : hi;
    }
    CHECK(hi - lo > 30000);
}

TEST_CASE("terrain_rough: displacement only ever goes into the solid, and tapers", "[terrain_rough]")
{
    Grid g = corridor(TCC_RoughFloor);
    TerrainCellSource src = g.source();
    TerrainRoughParams prm = terrain_rough_default_params;
    prm.corner_radius = 0; // the noise alone
    for (int64_t fy = 0; fy < 9 * N; fy++)
        for (int64_t fz = N; fz < 5 * N; fz++)
        {
            int64_t out[3];
            terrain_rough_displacement(&src, &prm, 5 * N, fy, fz, 1, 1, out);
            REQUIRE(out[0] <= 0);     // the rock is at lower x
            REQUIRE(out[0] >= -prm.amplitude);
            int64_t half[3];
            terrain_rough_displacement(&src, &prm, 5 * N, fy, fz, 1, 2, half);
            if ((fy % N != 0) && (fz % N != 0))
                REQUIRE(std::llabs(half[0] * 2 - out[0]) <= 2);   // inside the face: linear in the taper
            else
                REQUIRE(std::llabs(half[0]) <= std::llabs(out[0]) + 1); // edges are capped, so less than linear
            int64_t none[3];
            terrain_rough_displacement(&src, &prm, 5 * N, fy, fz, 0, 1, none);
            REQUIRE(none[0] == 0);
        }
}

namespace {
/** Twice the signed area of a sub-triangle, drawn with A=(0,0), B=(2n,0), C=(n,n) (any affine map
 *  keeps the sign and the area ratios). */
int64_t area2(const TerrainSubTri &t)
{
    int64_t x[3], y[3];
    for (int i = 0; i < 3; i++)
    {
        x[i] = 2 * t.w[i][1] + t.w[i][2];
        y[i] = t.w[i][2];
    }
    return (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
}

using Pt = std::tuple<int, int, int>;
Pt pt(const TerrainSubTri &t, int i) { return Pt(t.w[i][0], t.w[i][1], t.w[i][2]); }
}

TEST_CASE("terrain_rough: subdivided triangles tile the parent and keep its winding", "[terrain_rough]")
{
    for (int n = 3; n <= TERRAIN_SUBDIV_MAX_N; n++)
        for (int pattern = 0; pattern < 8; pattern++)
        {
            const int fine[3] = { pattern & 1, (pattern >> 1) & 1, (pattern >> 2) & 1 };
            TerrainSubTri tris[TERRAIN_SUBDIV_MAX_TRIS];
            const int cnt = terrain_subdiv_triangulate(n, fine, tris);
            INFO("n=" << n << " pattern=" << pattern);
            if (pattern == 0)
            {
                REQUIRE(cnt == 1);
                continue;
            }
            if (pattern == 7)
                CHECK(cnt == n * n);
            REQUIRE(cnt > 0);
            REQUIRE(cnt <= TERRAIN_SUBDIV_MAX_TRIS);
            // Same winding, no slivers, areas add up to the parent's (2n * n).
            int64_t total = 0;
            for (int t = 0; t < cnt; t++)
            {
                for (int i = 0; i < 3; i++)
                    REQUIRE(tris[t].w[i][0] + tris[t].w[i][1] + tris[t].w[i][2] == n);
                REQUIRE(area2(tris[t]) > 0);
                total += area2(tris[t]);
            }
            CHECK(total == 2LL * n * n);
            // Every sub-edge is shared by two sub-triangles, except those on the parent's edges.
            std::map<std::pair<Pt, Pt>, int> uses;
            for (int t = 0; t < cnt; t++)
                for (int i = 0; i < 3; i++)
                {
                    Pt a = pt(tris[t], i), b = pt(tris[t], (i + 1) % 3);
                    if (b < a)
                        std::swap(a, b);
                    uses[std::make_pair(a, b)]++;
                }
            std::set<Pt> on_edge[3]; // points used on each parent edge, ends included
            for (const auto &u : uses)
            {
                const Pt &a = u.first.first, &b = u.first.second;
                // Which parent edge is this sub-edge on (the weight that is 0 at both ends)?
                int edge = -1;
                if (std::get<2>(a) == 0 && std::get<2>(b) == 0) edge = 0;      // AB: no C
                else if (std::get<0>(a) == 0 && std::get<0>(b) == 0) edge = 1; // BC: no A
                else if (std::get<1>(a) == 0 && std::get<1>(b) == 0) edge = 2; // CA: no B
                if (edge < 0)
                {
                    REQUIRE(u.second == 2);
                }
                else
                {
                    REQUIRE(u.second == 1);
                    on_edge[edge].insert(a);
                    on_edge[edge].insert(b);
                }
            }
            // A fine edge has all n+1 lattice points on it, a coarse one just its two ends.
            for (int e = 0; e < 3; e++)
                CHECK(on_edge[e].size() == (fine[e] ? (size_t)n + 1 : 2u));
        }
}

namespace {
/** A lone slab of rock (subtiles 3..5 square, levels 0..4) on open path floor. */
Grid lone_slab(TerrainCellClass rock, TerrainCellClass floor)
{
    Grid g;
    for (int64_t y = 0; y < 9; y++)
        for (int64_t x = 0; x < 9; x++)
        {
            const bool in = (x >= 3) && (x <= 5) && (y >= 3) && (y <= 5);
            for (int64_t l = -1; l < 5; l++)
            {
                if (in)
                    g.set(x, y, l, rock);
                else if (l <= 0)
                    g.set(x, y, l, floor);
            }
        }
    return g;
}
}

TEST_CASE("terrain_rough: a lone slab's corners round into a circle", "[terrain_rough]")
{
    Grid g = lone_slab(TCC_Rough, TCC_RoughFloor);
    TerrainCellSource src = g.source();
    TerrainRoughParams prm = terrain_rough_default_params;
    prm.corner_radius = 384;
    const double centre = 4.5 * 256; // the slab's middle, map units
    int checked = 0;
    // Every lattice point on the slab's outline, at a wall height: it ends on the circle of radius 384.
    for (int64_t f = 3 * N; f <= 6 * N; f++)
    {
        const int64_t pts[4][2] = { {f, 3 * N}, {f, 6 * N}, {3 * N, f}, {6 * N, f} };
        for (int k = 0; k < 4; k++)
        {
            int64_t s2[2];
            terrain_rough_shape(&src, &prm, pts[k][0], pts[k][1], 2 * N + 1, s2);
            const double x = pts[k][0] * 256.0 / N + s2[0] - centre;
            const double y = pts[k][1] * 256.0 / N + s2[1] - centre;
            const double r = std::sqrt(x * x + y * y);
            INFO("point (" << pts[k][0] << "," << pts[k][1] << ") lands at radius " << r);
            CHECK(std::fabs(r - 384.0) <= 3.0);
            checked++;
        }
    }
    CHECK(checked == 4 * (3 * N + 1));
}

TEST_CASE("terrain_rough: rounding only moves points into the rock, keeping their order", "[terrain_rough]")
{
    Grid g = lone_slab(TCC_Shapeable, TCC_RoughFloor);
    TerrainCellSource src = g.source();
    TerrainRoughParams prm = terrain_rough_default_params;
    prm.corner_radius = 384;
    const double centre = 4.5 * 256;
    double last_angle = -10.0;
    for (int64_t fy = 3 * N; fy <= 6 * N; fy++)
        for (int64_t fx = 3 * N; fx <= 6 * N; fx++)
        {
            int64_t s2[2];
            terrain_rough_shape(&src, &prm, fx, fy, 5 * N, s2);
            const double x0 = fx * 256.0 / N - centre, y0 = fy * 256.0 / N - centre;
            const double x1 = x0 + s2[0], y1 = y0 + s2[1];
            // Toward the middle, never out of the slab.
            REQUIRE(x1 * x1 + y1 * y1 <= x0 * x0 + y0 * y0 + 1.0);
            REQUIRE(std::fabs(x1) <= 384.0);
            REQUIRE(std::fabs(y1) <= 384.0);
        }
    // Along one face, the points stay in order around the circle.
    for (int64_t fx = 3 * N; fx <= 6 * N; fx++)
    {
        int64_t s2[2];
        terrain_rough_shape(&src, &prm, fx, 3 * N, 2 * N + 1, s2);
        const double a = std::atan2(3 * N * 256.0 / N + s2[1] - centre, fx * 256.0 / N + s2[0] - centre);
        if (fx > 3 * N)
            REQUIRE(a > last_angle);
        last_angle = a;
    }
    // Outside the corner squares nothing moves: with a 256 radius they end a subtile from each corner.
    int64_t s2[2];
    prm.corner_radius = 256;
    terrain_rough_shape(&src, &prm, 3 * N, 4 * N + 1, 2 * N + 1, s2);
    CHECK((s2[0] == 0 && s2[1] == 0));
    terrain_rough_shape(&src, &prm, 3 * N, 4 * N - 1, 2 * N + 1, s2);
    CHECK((s2[0] != 0 || s2[1] != 0));
}

TEST_CASE("terrain_rough: no rounding beside claimed floor, or with the radius off", "[terrain_rough]")
{
    TerrainRoughParams prm = terrain_rough_default_params;
    prm.corner_radius = 384;
    int64_t s2[2];
    Grid claimed = lone_slab(TCC_Rough, TCC_HardPinned);
    TerrainCellSource csrc = claimed.source();
    terrain_rough_shape(&csrc, &prm, 3 * N, 3 * N, 2 * N + 1, s2);
    CHECK((s2[0] == 0 && s2[1] == 0));
    Grid g = lone_slab(TCC_Rough, TCC_RoughFloor);
    TerrainCellSource src = g.source();
    terrain_rough_shape(&src, &prm, 3 * N, 3 * N, 2 * N + 1, s2);
    CHECK((s2[0] > 0 && s2[1] > 0));
    prm.corner_radius = 0;
    terrain_rough_shape(&src, &prm, 3 * N, 3 * N, 2 * N + 1, s2);
    CHECK((s2[0] == 0 && s2[1] == 0));
}

TEST_CASE("terrain_rough: rounding a lone slab folds no lattice triangle, floor beside it included", "[terrain_rough]")
{
    Grid g = lone_slab(TCC_Rough, TCC_PlanePinned);
    TerrainCellSource src = g.source();
    for (int64_t radius : {128, 256, 320, 384})
    {
        TerrainRoughParams prm = terrain_rough_default_params;
        prm.corner_radius = radius;
        // The plan map at floor level (fz = N) and at a wall height, over the slab and two subtiles round it.
        for (int64_t fz : {N, 2 * N + 1})
        {
            auto pos = [&](int64_t fx, int64_t fy, double &x, double &y) {
                int64_t s2[2];
                terrain_rough_shape(&src, &prm, fx, fy, fz, s2);
                x = fx * 256.0 / N + s2[0];
                y = fy * 256.0 / N + s2[1];
            };
            for (int64_t fy = 1 * N; fy < 8 * N; fy++)
                for (int64_t fx = 1 * N; fx < 8 * N; fx++)
                {
                    double ax, ay, bx, by, cx, cy, dx, dy;
                    pos(fx, fy, ax, ay);
                    pos(fx + 1, fy, bx, by);
                    pos(fx, fy + 1, cx, cy);
                    pos(fx + 1, fy + 1, dx, dy);
                    INFO("radius " << radius << " fz " << fz << " at (" << fx << "," << fy << ")");
                    // The cell's triangles keep their winding (and some area), cut either way.
                    REQUIRE((bx - ax) * (dy - ay) - (dx - ax) * (by - ay) > 1.0);
                    REQUIRE((dx - ax) * (cy - ay) - (cx - ax) * (dy - ay) > 1.0);
                    REQUIRE((bx - ax) * (cy - ay) - (cx - ax) * (by - ay) > 1.0);
                    REQUIRE((dx - bx) * (cy - by) - (cx - bx) * (dy - by) > 1.0);
                }
        }
    }
}

namespace {
/** A 3x3-subtile room (cells 4..6) in the middle of a 15x15 block of rock: all four of its corners
 *  are clean inside corners; (7, 7) is the one tested. */
Grid room_in_rock()
{
    Grid g;
    for (int64_t y = 0; y < 15; y++)
        for (int64_t x = 0; x < 15; x++)
        {
            const bool room = (x >= 4) && (x < 7) && (y >= 4) && (y < 7);
            for (int64_t l = -1; l < 5; l++)
            {
                if (!room)
                    g.set(x, y, l, TCC_Rough);
                else if (l <= 0)
                    g.set(x, y, l, TCC_PlanePinned);
            }
        }
    return g;
}
}

TEST_CASE("terrain_rough: an inside corner is carved into a bay", "[terrain_rough]")
{
    Grid g = room_in_rock();
    TerrainCellSource src = g.source();
    TerrainRoughParams prm = terrain_rough_default_params;
    prm.corner_radius = 320;
    int64_t s2[2];
    // The corner goes diagonally into the rock, by about 0.44 R.
    terrain_rough_shape(&src, &prm, 7 * N, 7 * N, 2 * N + 1, s2);
    CHECK(s2[0] > 0);
    CHECK(s2[0] == s2[1]);
    const double moved = std::sqrt(2.0) * s2[0];
    CHECK(moved > 0.35 * 320);
    CHECK(moved < 0.55 * 320);
    // Past the lips (R along the faces) nothing moves; just short of them, a little.
    terrain_rough_shape(&src, &prm, 7 * N - (320 * N + 255) / 256, 7 * N, 2 * N + 1, s2);
    CHECK((s2[0] == 0 && s2[1] == 0));
    terrain_rough_shape(&src, &prm, 7 * N - 320 * N / 256, 7 * N, 2 * N + 1, s2);
    CHECK(std::llabs(s2[0]) + std::llabs(s2[1]) > 0); // the bay meets the face at an angle
}

TEST_CASE("terrain_rough: a bay folds no lattice triangle, floor and rock top included", "[terrain_rough]")
{
    Grid g = room_in_rock();
    TerrainCellSource src = g.source();
    for (int64_t radius : {128, 256, 320, 384})
    {
        TerrainRoughParams prm = terrain_rough_default_params;
        prm.corner_radius = radius;
        for (int64_t fz : {N, 2 * N + 1, 5 * N})
        {
            auto pos = [&](int64_t fx, int64_t fy, double &x, double &y) {
                int64_t s2[2];
                terrain_rough_shape(&src, &prm, fx, fy, fz, s2);
                x = fx * 256.0 / N + s2[0];
                y = fy * 256.0 / N + s2[1];
            };
            for (int64_t fy = 1 * N; fy < 13 * N; fy++)
                for (int64_t fx = 1 * N; fx < 13 * N; fx++)
                {
                    double ax, ay, bx, by, cx, cy, dx, dy;
                    pos(fx, fy, ax, ay);
                    pos(fx + 1, fy, bx, by);
                    pos(fx, fy + 1, cx, cy);
                    pos(fx + 1, fy + 1, dx, dy);
                    INFO("radius " << radius << " fz " << fz << " at (" << fx << "," << fy << ")");
                    REQUIRE((bx - ax) * (dy - ay) - (dx - ax) * (by - ay) > 1.0);
                    REQUIRE((dx - ax) * (cy - ay) - (cx - ax) * (dy - ay) > 1.0);
                    REQUIRE((bx - ax) * (cy - ay) - (cx - ax) * (by - ay) > 1.0);
                    REQUIRE((dx - bx) * (cy - by) - (cx - bx) * (dy - by) > 1.0);
                }
        }
    }
}

TEST_CASE("terrain_rough: rock and gems get a subtler roughness than rough walls", "[terrain_rough]")
{
    // The same wall, once as rough dirt and once as shapeable rock: the rock's recess is shapeable_percent of it.
    TerrainRoughParams prm = terrain_rough_default_params;
    prm.corner_radius = 0;
    Grid dirt = corridor(TCC_RoughFloor);
    Grid rock;
    for (const auto &c : dirt.cells)
        rock.cells[c.first] = (c.second == TCC_Rough) ? TCC_Shapeable : c.second;
    TerrainCellSource dsrc = dirt.source(), rsrc = rock.source();
    int64_t deepest_dirt = 0, deepest_rock = 0;
    for (int64_t fy = 0; fy < 9 * N; fy++)
    {
        if (fy % N == 0)
            continue;
        int64_t d[3], r[3];
        terrain_rough_displacement(&dsrc, &prm, 5 * N, fy, 2 * N + 1, 1, 1, d);
        terrain_rough_displacement(&rsrc, &prm, 5 * N, fy, 2 * N + 1, 1, 1, r);
        REQUIRE(r[0] <= 0);
        REQUIRE(std::llabs(r[0]) <= std::llabs(d[0]) * prm.shapeable_percent / 100 + 1);
        deepest_dirt = d[0] < deepest_dirt ? d[0] : deepest_dirt;
        deepest_rock = r[0] < deepest_rock ? r[0] : deepest_rock;
    }
    CHECK(deepest_rock < 0);
    CHECK(-deepest_rock < -deepest_dirt / 2);
}

TEST_CASE("terrain_rough: the floor offset follows the drawn floor between lattice points", "[terrain_rough]")
{
    Grid g = corridor(TCC_RoughFloor);
    TerrainCellSource src = g.source();
    TerrainRoughParams prm = terrain_rough_default_params;
    prm.corner_radius = 0;
    // At a lattice point it is that point's dip; between, it stays within its neighbours' dips.
    for (int64_t fy = 2; fy < 8 * N; fy += 2)
        for (int64_t fx = 6 * N + 1; fx < 9 * N - 1; fx++)
        {
            int64_t a[3], b[3];
            terrain_rough_displacement(&src, &prm, fx, fy, N, N, N, a);
            terrain_rough_displacement(&src, &prm, fx + 1, fy, N, N, N, b);
            const int64_t mx = fx * 256 / N, my = fy * 256 / N;
            if ((fx * 256) % N == 0 && (fy * 256) % N == 0)
                REQUIRE(terrain_rough_floor_offset_src(&src, &prm, mx, my, 1) == a[2]);
            const int64_t mid = terrain_rough_floor_offset_src(&src, &prm, (fx * 256 + 128) / N, fy * 256 / N, 1);
            REQUIRE(mid <= 0);
            REQUIRE(mid >= -prm.floor_amplitude);
            REQUIRE(mid >= std::min(a[2], b[2]) - 2);
            REQUIRE(mid <= std::max(a[2], b[2]) + 2);
        }
}

namespace {
/** The cell's triangles of the plan map at height fz keep their winding, over [lo, hi) fine cells. */
void require_no_plan_fold(const TerrainCellSource &src, const TerrainRoughParams &prm, int64_t fz, int64_t lo, int64_t hi)
{
    auto pos = [&](int64_t fx, int64_t fy, double &x, double &y) {
        int64_t s2[2];
        terrain_rough_shape(&src, &prm, fx, fy, fz, s2);
        x = fx * 256.0 / N + s2[0];
        y = fy * 256.0 / N + s2[1];
    };
    for (int64_t fy = lo; fy < hi; fy++)
        for (int64_t fx = lo; fx < hi; fx++)
        {
            double ax, ay, bx, by, cx, cy, dx, dy;
            pos(fx, fy, ax, ay);
            pos(fx + 1, fy, bx, by);
            pos(fx, fy + 1, cx, cy);
            pos(fx + 1, fy + 1, dx, dy);
            INFO("fz " << fz << " at (" << fx << "," << fy << ")");
            REQUIRE((bx - ax) * (dy - ay) - (dx - ax) * (by - ay) > 1.0);
            REQUIRE((dx - ax) * (cy - ay) - (cx - ax) * (dy - ay) > 1.0);
            REQUIRE((bx - ax) * (cy - ay) - (cx - ax) * (by - ay) > 1.0);
            REQUIRE((dx - bx) * (cy - by) - (cx - bx) * (dy - by) > 1.0);
        }
}
}

namespace {
/** A corridor dug into a 15x15 block of rock: cells x 4..6, from a dead end at y = 4 out past y = 14.
 *  The corners at either end of its end face, (4, 4) and (7, 4), are clean inside corners. */
Grid dead_end()
{
    Grid g;
    for (int64_t y = 0; y < 15; y++)
        for (int64_t x = 0; x < 15; x++)
        {
            const bool dug = (x >= 4) && (x < 7) && (y >= 4);
            for (int64_t l = -1; l < 5; l++)
            {
                if (!dug)
                    g.set(x, y, l, TCC_Rough);
                else if (l <= 0)
                    g.set(x, y, l, TCC_PlanePinned);
            }
        }
    return g;
}
}

TEST_CASE("terrain_rough: a dead end's face is carved into one arch, deepest in the middle", "[terrain_rough]")
{
    Grid g = dead_end();
    TerrainCellSource src = g.source();
    TerrainRoughParams prm = terrain_rough_default_params;
    prm.corner_radius = 320;
    const int64_t depth = 320 * TERRAIN_CAP_DEPTH_PERCENT / 100;
    int64_t s2[2];
    // Along the end face the points go straight into the rock (-y), rising to the middle and falling
    // again: no rock left standing between two bays.
    int64_t prev = 0, deepest = 0;
    for (int64_t fx = 4 * N + 1; fx < 7 * N; fx++)
    {
        terrain_rough_shape(&src, &prm, fx, 4 * N, 2 * N + 1, s2);
        INFO("face point " << fx << " moves (" << s2[0] << "," << s2[1] << ")");
        CHECK(s2[0] == 0);
        CHECK(s2[1] < 0);
        if (2 * fx <= 11 * N)
            CHECK(-s2[1] >= prev);
        else
            CHECK(-s2[1] <= prev);
        prev = -s2[1];
        deepest = std::max(deepest, -s2[1]);
    }
    CHECK(deepest > depth * 9 / 10);
    CHECK(deepest <= depth);
    // The two ends are mirror images.
    int64_t a[2], b[2];
    terrain_rough_shape(&src, &prm, 4 * N + 2, 4 * N, 2 * N + 1, a);
    terrain_rough_shape(&src, &prm, 7 * N - 2, 4 * N, 2 * N + 1, b);
    CHECK(std::llabs(a[1] - b[1]) <= 1);
    // The side walls run straight into the arch: the old corners and the walls below them stay.
    for (int64_t fy = 4 * N; fy < 8 * N; fy++)
    {
        terrain_rough_shape(&src, &prm, 4 * N, fy, 2 * N + 1, a);
        terrain_rough_shape(&src, &prm, 7 * N, fy, 2 * N + 1, b);
        INFO("side wall point at fy " << fy);
        CHECK((a[0] == 0 && a[1] == 0));
        CHECK((b[0] == 0 && b[1] == 0));
    }
    // A pocket closed on all four sides keeps its four bays.
    Grid room = room_in_rock();
    TerrainCellSource rsrc = room.source();
    terrain_rough_shape(&rsrc, &prm, 7 * N, 7 * N, 2 * N + 1, s2);
    CHECK((s2[0] > 0 && s2[0] == s2[1]));
}

TEST_CASE("terrain_rough: a dead end's arch folds no lattice triangle, eased or not", "[terrain_rough]")
{
    Grid g = dead_end();
    TerrainCellSource src = g.source();
    for (int64_t radius : {128, 256, 320, 384})
    {
        TerrainRoughParams prm = terrain_rough_default_params;
        prm.corner_radius = radius;
        INFO("radius " << radius);
        for (int64_t fz : {N, 2 * N + 1, 5 * N})
            require_no_plan_fold(src, prm, fz, 1 * N, 13 * N);
    }
    // The end slab and the rock round it eased by different amounts.
    for (int64_t y = 0; y < 15; y++)
        for (int64_t x = 0; x < 15; x++)
            g.weights[std::make_pair(x, y)] = ((x / 3 + y / 3) % 3 == 0) ? 1024 : ((x / 3 + y / 3) % 3 == 1) ? 300 : 0;
    TerrainRoughParams prm = terrain_rough_default_params;
    prm.corner_radius = 320;
    for (int64_t fz : {N, 2 * N + 1, 5 * N})
        require_no_plan_fold(src, prm, fz, 1 * N, 13 * N);
}

TEST_CASE("terrain_rough: a wall being reinforced loses its roughness with its weight", "[terrain_rough]")
{
    TerrainRoughParams prm = terrain_rough_default_params;
    prm.corner_radius = 0;
    Grid full = corridor(TCC_RoughFloor);
    Grid eased = corridor(TCC_RoughFloor);
    // The wall columns next to the corridor, y 3..5 (one slab), reinforced partway.
    for (int64_t y = 3; y < 6; y++)
        for (int64_t x = 0; x < 5; x++)
            eased.weights[std::make_pair(x, y)] = 256;
    TerrainCellSource fsrc = full.source(), esrc = eased.source();
    int64_t deepest = 0;
    for (int64_t fy = 3 * N + 1; fy < 6 * N; fy++)
    {
        int64_t f[3], e[3];
        terrain_rough_displacement(&fsrc, &prm, 5 * N, fy, 2 * N + 1, 1, 1, f);
        terrain_rough_displacement(&esrc, &prm, 5 * N, fy, 2 * N + 1, 1, 1, e);
        INFO("fy " << fy);
        REQUIRE(std::llabs(e[0]) <= std::llabs(f[0]) / 4 + 1);
        deepest = std::min(deepest, f[0]);
    }
    CHECK(deepest < -16);
    // A vertex on the slab's edge takes the smaller weight; past it, the wall is as rough as ever.
    for (int64_t fy : {int64_t(3 * N), int64_t(6 * N)})
    {
        int64_t f[3], e[3];
        terrain_rough_displacement(&fsrc, &prm, 5 * N, fy, 2 * N + 1, 1, 1, f);
        terrain_rough_displacement(&esrc, &prm, 5 * N, fy, 2 * N + 1, 1, 1, e);
        REQUIRE(std::llabs(e[0]) <= std::llabs(f[0]) / 4 + 1);
    }
    for (int64_t fy : {int64_t(2 * N + 1), int64_t(6 * N + 1)})
    {
        int64_t f[3], e[3];
        terrain_rough_displacement(&fsrc, &prm, 5 * N, fy, 2 * N + 1, 1, 1, f);
        terrain_rough_displacement(&esrc, &prm, 5 * N, fy, 2 * N + 1, 1, 1, e);
        REQUIRE(e[0] == f[0]);
    }
    // Fully reinforced: nothing on it moves (as when it turns into a fortified wall).
    for (auto &w : eased.weights)
        w.second = 0;
    for (int64_t fy = 3 * N; fy <= 6 * N; fy++)
    {
        int64_t e[3];
        terrain_rough_displacement(&esrc, &prm, 5 * N, fy, 2 * N + 1, 1, 1, e);
        REQUIRE((e[0] == 0 && e[1] == 0 && e[2] == 0));
    }
}

TEST_CASE("terrain_rough: a corner being reinforced rounds part of the way, folding nothing", "[terrain_rough]")
{
    TerrainRoughParams prm = terrain_rough_default_params;
    prm.corner_radius = 384;
    // A lone slab eased as one: its circle shrinks back toward the square.
    Grid lone = lone_slab(TCC_Rough, TCC_PlanePinned);
    TerrainCellSource lsrc = lone.source();
    int64_t full[2], half[2];
    terrain_rough_shape(&lsrc, &prm, 3 * N, 3 * N, 2 * N + 1, full);
    for (int64_t w : {0, 1, 300, 512, 900, 1023})
    {
        for (int64_t y = 3; y <= 5; y++)
            for (int64_t x = 3; x <= 5; x++)
                lone.weights[std::make_pair(x, y)] = w;
        terrain_rough_shape(&lsrc, &prm, 3 * N, 3 * N, 2 * N + 1, half);
        INFO("weight " << w);
        CHECK(std::llabs(half[0] - full[0] * w / TERRAIN_WEIGHT_ONE) <= 1);
        CHECK(std::llabs(half[1] - full[1] * w / TERRAIN_WEIGHT_ONE) <= 1);
        require_no_plan_fold(lsrc, prm, 2 * N + 1, 1 * N, 8 * N);
    }
    // A room in rock with its slabs eased by different amounts: the bays and the rock between them.
    Grid room = room_in_rock();
    TerrainCellSource rsrc = room.source();
    const int64_t slab_w[5][5] = {
        {1024, 700, 0, 1024, 512},
        {300, 1024, 1, 900, 1024},
        {1024, 64, 1024, 200, 1024},
        {512, 1024, 800, 1024, 0},
        {1024, 1024, 1024, 333, 1024},
    };
    for (int64_t y = 0; y < 15; y++)
        for (int64_t x = 0; x < 15; x++)
            room.weights[std::make_pair(x, y)] = slab_w[y / 3][x / 3];
    for (int64_t radius : {256, 320, 384})
    {
        prm.corner_radius = radius;
        INFO("radius " << radius);
        for (int64_t fz : {N, 2 * N + 1, 5 * N})
            require_no_plan_fold(rsrc, prm, fz, 1 * N, 13 * N);
    }
}

namespace {
/** Texture stretch over one face's lattice: each lattice quad's edges (both axes and a diagonal),
 *  displaced length over undisplaced, in 3-D. The texture is fixed to the lattice, so this is how
 *  much it stretches (>1) or squeezes (<1). */
struct StretchStats {
    double lo = 1e9, hi = 0;
    int n = 0, over = 0, under = 0;
    int64_t lo_at[3] = {0, 0, 0}, hi_at[3] = {0, 0, 0}; char lo_e = 0, hi_e = 0;
    void add(double r, const int64_t f[3], char e)
    {
        if (r < lo) { lo = r; lo_at[0] = f[0]; lo_at[1] = f[1]; lo_at[2] = f[2]; lo_e = e; }
        if (r > hi) { hi = r; hi_at[0] = f[0]; hi_at[1] = f[1]; hi_at[2] = f[2]; hi_e = e; }
        n++; if (r > 1.4) over++; if (r < 0.7) under++;
    }
};
/** Face: points p(a, b) = origin + a*ea + b*eb on the fine lattice, a in [0, na), b in [0, nb). */
StretchStats face_stretch(const TerrainCellSource &src, const TerrainRoughParams &prm, const int64_t origin[3],
    const int64_t ea[3], const int64_t eb[3], int64_t na, int64_t nb)
{
    StretchStats st;
    auto pos = [&](int64_t a, int64_t b, double out[3]) {
        int64_t f[3], d[3];
        for (int i = 0; i < 3; i++)
            f[i] = origin[i] + a * ea[i] + b * eb[i];
        terrain_rough_displacement(&src, &prm, f[0], f[1], f[2], 1, 1, d);
        for (int i = 0; i < 3; i++)
            out[i] = f[i] * 256.0 / N + d[i];
    };
    auto len = [](const double p[3], const double q[3]) {
        return std::sqrt((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]));
    };
    const double step = 256.0 / N;
    for (int64_t b = 0; b + 1 < nb; b++)
        for (int64_t a = 0; a + 1 < na; a++)
        {
            double p00[3], p10[3], p01[3], p11[3];
            pos(a, b, p00);
            pos(a + 1, b, p10);
            pos(a, b + 1, p01);
            pos(a + 1, b + 1, p11);
            const int64_t f[3] = { origin[0] + a * ea[0] + b * eb[0], origin[1] + a * ea[1] + b * eb[1], origin[2] + a * ea[2] + b * eb[2] };
            st.add(len(p00, p10) / step, f, 'a');
            st.add(len(p00, p01) / step, f, 'b');
            st.add(len(p00, p11) / (step * std::sqrt(2.0)), f, 'd');
        }
    return st;
}
}

TEST_CASE("terrain_rough: texture stretch survey", "[.][stretch]")
{
    struct Variant { const char *name; int64_t amplitude; int64_t radius; };
    const Variant variants[] = { {"noise only", 96, 0}, {"corners only", 0, 320}, {"both", 96, 320} };
    Grid corr = corridor(TCC_RoughFloor);
    Grid lone = lone_slab(TCC_Rough, TCC_RoughFloor);
    Grid room = room_in_rock();
    Grid dead = dead_end();
    struct Face { const char *name; Grid *g; int64_t o[3], ea[3], eb[3], na, nb; };
    const Face faces[] = {
        {"corridor wall (x=5)",   &corr, {5 * N, 1 * N, N},     {0, 1, 0}, {0, 0, 1}, 8 * N, 4 * N + 1},
        {"corridor rock top",     &corr, {1 * N, 1 * N, 5 * N}, {1, 0, 0}, {0, 1, 0}, 4 * N + 1, 8 * N},
        {"corridor floor",        &corr, {5 * N, 1 * N, N},     {1, 0, 0}, {0, 1, 0}, 4 * N, 8 * N},
        {"lone slab wall (y=3)",  &lone, {3 * N, 3 * N, N},     {1, 0, 0}, {0, 0, 1}, 3 * N + 1, 4 * N + 1},
        {"lone slab top",         &lone, {3 * N, 3 * N, 5 * N}, {1, 0, 0}, {0, 1, 0}, 3 * N + 1, 3 * N + 1},
        {"lone slab floor round", &lone, {1 * N, 1 * N, N},     {1, 0, 0}, {0, 1, 0}, 7 * N + 1, 7 * N + 1},
        {"room wall (y=7)",       &room, {4 * N, 7 * N, N},     {1, 0, 0}, {0, 0, 1}, 3 * N + 1, 4 * N + 1},
        {"room floor (bays)",     &room, {4 * N, 4 * N, N},     {1, 0, 0}, {0, 1, 0}, 3 * N + 1, 3 * N + 1},
        {"room rock top",         &room, {1 * N, 1 * N, 5 * N}, {1, 0, 0}, {0, 1, 0}, 13 * N, 13 * N},
        {"dead end wall (y=4)",   &dead, {4 * N, 4 * N, N},     {1, 0, 0}, {0, 0, 1}, 3 * N + 1, 4 * N + 1},
        {"dead end floor (arch)", &dead, {4 * N, 4 * N, N},     {1, 0, 0}, {0, 1, 0}, 3 * N + 1, 3 * N + 1},
        {"dead end rock top",     &dead, {1 * N, 1 * N, 5 * N}, {1, 0, 0}, {0, 1, 0}, 9 * N, 6 * N},
    };
    for (const Variant &v : variants)
        for (const Face &f : faces)
        {
            TerrainRoughParams prm = terrain_rough_default_params;
            prm.amplitude = v.amplitude;
            prm.floor_amplitude = v.amplitude / 4;
            prm.corner_radius = v.radius;
            TerrainCellSource src = f.g->source();
            StretchStats st = face_stretch(src, prm, f.o, f.ea, f.eb, f.na, f.nb);
            printf("%-13s %-22s min %.2f max %.2f  >1.4: %4.1f%%  <0.7: %4.1f%%   min at (%lld,%lld,%lld)%c max at (%lld,%lld,%lld)%c\n", v.name, f.name, st.lo, st.hi,
                100.0 * st.over / st.n, 100.0 * st.under / st.n, (long long)st.lo_at[0], (long long)st.lo_at[1], (long long)st.lo_at[2], st.lo_e,
                (long long)st.hi_at[0], (long long)st.hi_at[1], (long long)st.hi_at[2], st.hi_e);
        }
}

TEST_CASE("terrain_rough: textures stretch and squeeze within limits", "[terrain_rough]")
{
    // The texture is fixed to the lattice, so a lattice edge's displaced length over its own is how far
    // the texture on it stretches. Limits at the defaults (depth 96, corners 320), with some margin:
    // - straight edges: the band spreads an edge's move over the floor or top beside it;
    // - convex corners: the face squeezes evenly round the arc (pi / 4);
    // - bays: the floor stretches over its whole run from the rays' start;
    // - a dead end's arch: steepest beside the side walls, where it leaves them square to the face.
    TerrainRoughParams prm = terrain_rough_default_params;
    Grid corr = corridor(TCC_RoughFloor);
    Grid corr_plane = corridor(TCC_PlanePinned);
    Grid lone = lone_slab(TCC_Rough, TCC_PlanePinned);
    Grid room = room_in_rock();
    Grid dead = dead_end();
    struct Case { const char *name; Grid *g; int64_t o[3], ea[3], eb[3], na, nb; double lo, hi; };
    const Case cases[] = {
        {"corridor wall",              &corr,       {5 * N, 1 * N, N},     {0, 1, 0}, {0, 0, 1}, 8 * N, 4 * N + 1, 0.80, 1.30},
        {"corridor rock top",          &corr,       {1 * N, 1 * N, 5 * N}, {1, 0, 0}, {0, 1, 0}, 4 * N + 1, 8 * N, 0.70, 1.15},
        {"corridor rough floor",       &corr,       {5 * N, 1 * N, N},     {1, 0, 0}, {0, 1, 0}, 4 * N, 8 * N, 0.80, 1.30},
        {"corridor plane-pinned floor", &corr_plane, {5 * N, 1 * N, N},    {1, 0, 0}, {0, 1, 0}, 4 * N, 8 * N, 0.80, 1.30},
        {"lone slab wall",             &lone,       {3 * N, 3 * N, N},     {1, 0, 0}, {0, 0, 1}, 3 * N + 1, 4 * N + 1, 0.50, 1.20},
        {"lone slab floor round",      &lone,       {1 * N, 1 * N, N},     {1, 0, 0}, {0, 1, 0}, 7 * N + 1, 7 * N + 1, 0.55, 1.80},
        {"room floor (bays)",          &room,       {4 * N, 4 * N, N},     {1, 0, 0}, {0, 1, 0}, 3 * N + 1, 3 * N + 1, 0.85, 2.10},
        {"room wall (bay)",            &room,       {4 * N, 7 * N, N},     {1, 0, 0}, {0, 0, 1}, 3 * N + 1, 4 * N + 1, 0.85, 1.90},
        {"dead end floor (arch)",      &dead,       {4 * N, 4 * N, N},     {1, 0, 0}, {0, 1, 0}, 3 * N + 1, 3 * N + 1, 0.65, 2.20},
        {"dead end wall (arch)",       &dead,       {4 * N, 4 * N, N},     {1, 0, 0}, {0, 0, 1}, 3 * N + 1, 4 * N + 1, 0.85, 2.05},
    };
    for (const Case &c : cases)
    {
        TerrainCellSource src = c.g->source();
        StretchStats st = face_stretch(src, prm, c.o, c.ea, c.eb, c.na, c.nb);
        INFO(c.name << ": " << st.lo << " .. " << st.hi);
        CHECK(st.lo >= c.lo);
        CHECK(st.hi <= c.hi);
    }
}
