// kfx_pathfinding: ariadne_regions.c.
//
// Was previously targeted (docs/refactor/todo/
// two-remaining-layering-violations.md) around navigation_regions_connected(),
// this file's reason for including kfx_sim's player_data.h (later
// kfx_config_state.h) for PLAYERS_COUNT. Upstream's "Fix pathfinding lag
// behind lava" (#5190) removed that function entirely -- its BFS moved
// into ariadne.c as a static navigation_triangle_reachable() that no
// longer takes an owner parameter at all (delegates to
// navigation_rule_normal(), which reads owner_player_navigating from
// kfx_pathfinding_state instead) -- so both the function and the header
// include it justified are gone from this file now. Not independently
// unit-tested: it's static in ariadne.c, unlike
// navigation_regions_connected() which lived here as an
// externally-linkable function.
//
// What's left to cover: region_store_init/region_get/region_put (a
// plain circular queue, no dependencies) and regions_connected()'s
// early-return bounds/blocking checks and real BFS path.
//
// regions_connected()'s "two never-yet-connected triangles" case means
// walking the real region_connect()/region_lnk() BFS over
// Triangles[].tags[] -- exactly the "needs a small triangulated
// fixture" territory stage-08's docs flagged for this library's other
// untested functions. Built the smallest possible one here: two
// triangles with a single mutual tag-link at corner 0 (all other
// corners -1, i.e. open/border edges) -- hand-traced through
// region_alloc/region_lnk/region_connect's actual bodies before
// asserting, confirmed against the real run, not assumed.
#include <catch2/catch_test_macros.hpp>

#include "ariadne_regions.h"
#include "ariadne_tringls.h"
#include "ariadne.h" // NAVMAP_FLOORHEIGHT_MAX/_MASK

#include <cstring>

namespace {
// Scratch triangle indices, far from every other ariadne_tringls_test.cpp/
// ariadne_regions_test.cpp index (0-3, TRIANLGLES_COUNT-1) to avoid
// cross-test collisions within the same binary run.
constexpr int64_t kTriA = 50000;
constexpr int64_t kTriB = 50001;

struct ResetRegions {
    ResetRegions() {
        triangulation_init_regions();
        std::memset(&Triangles[kTriA], 0, sizeof(Triangles[kTriA]));
        std::memset(&Triangles[kTriB], 0, sizeof(Triangles[kTriB]));
        Triangles[kTriA].tags[0] = -1;
        Triangles[kTriA].tags[1] = -1;
        Triangles[kTriA].tags[2] = -1;
        Triangles[kTriB].tags[0] = -1;
        Triangles[kTriB].tags[1] = -1;
        Triangles[kTriB].tags[2] = -1;
    }

    // Links A and B as mutual neighbors at corner 0 -- the minimal
    // adjacency regions_connected()'s real BFS needs to put them in the
    // same region.
    void link_a_and_b() {
        Triangles[kTriA].tags[0] = kTriB;
        Triangles[kTriB].tags[0] = kTriA;
    }
};
}

TEST_CASE("region_store_init/region_get/region_put form a plain FIFO circular queue", "[kfx_pathfinding][ariadne_regions]") {
    region_store_init();
    CHECK(region_get() == -1); // empty

    region_put(10);
    region_put(20);
    region_put(30);
    CHECK(region_get() == 10);
    CHECK(region_get() == 20);
    CHECK(region_get() == 30);
    CHECK(region_get() == -1); // drained back to empty
}

TEST_CASE("regions_connected rejects out-of-range triangle indices", "[kfx_pathfinding][ariadne_regions]") {
    CHECK_FALSE(regions_connected(-1, 0));
    CHECK_FALSE(regions_connected(0, TRIANLGLES_COUNT));
}

TEST_CASE_METHOD(ResetRegions, "regions_connected is false when either triangle is floor-height-blocked", "[kfx_pathfinding][ariadne_regions]") {
    Triangles[kTriA].tree_alt = NAVMAP_FLOORHEIGHT_MAX; // blocked (tree_alt & MASK == MAX)
    CHECK_FALSE(regions_connected(kTriA, kTriB));
}

TEST_CASE_METHOD(ResetRegions, "regions_connected finds two directly tag-linked triangles connected", "[kfx_pathfinding][ariadne_regions]") {
    link_a_and_b();
    CHECK(regions_connected(kTriA, kTriB));
}

TEST_CASE_METHOD(ResetRegions, "regions_connected reports two never-linked triangles as not connected", "[kfx_pathfinding][ariadne_regions]") {
    // Both isolated (no shared edge) -- region_connect(A) claims a fresh
    // region for A alone; B is never reached, so it stays in the
    // (different) default region.
    CHECK_FALSE(regions_connected(kTriA, kTriB));
}

// region_set_f/region_unset_f/region_unlock all mutate the module-private
// Regions[] array too (num_triangles/is_connected bookkeeping), but
// that's not exposed through any accessor -- only region_set_f's write to
// the *triangle's own* region id (via set_triangle_region_id(), already
// covered directly in ariadne_tringls_test.cpp) is externally observable
// here, through get_triangle_region_id().
TEST_CASE_METHOD(ResetRegions, "region_set updates the triangle's own region id when it actually changes", "[kfx_pathfinding][ariadne_regions]") {
    CHECK(get_triangle_region_id(kTriA) == 0); // ResetRegions leaves the triangle zeroed
    region_set(kTriA, 5);
    CHECK(get_triangle_region_id(kTriA) == 5);
}

TEST_CASE_METHOD(ResetRegions, "region_set rejects an out-of-range triangle id or region id without touching anything", "[kfx_pathfinding][ariadne_regions]") {
    region_set(-1, 5);
    region_set(TRIANLGLES_COUNT, 5);
    region_set(kTriA, REGIONS_COUNT);
    CHECK(get_triangle_region_id(kTriA) == 0); // untouched by any of the three rejected calls
}

TEST_CASE_METHOD(ResetRegions, "region_unset resets the triangle's region id back to 0", "[kfx_pathfinding][ariadne_regions]") {
    region_set(kTriA, 5);
    REQUIRE(get_triangle_region_id(kTriA) == 5);
    region_unset(kTriA, 5);
    CHECK(get_triangle_region_id(kTriA) == 0);
}

TEST_CASE_METHOD(ResetRegions, "region_unlock is a safe no-op from this test's vantage point (Regions[] itself isn't exposed)", "[kfx_pathfinding][ariadne_regions]") {
    region_set(kTriA, 5);
    region_unlock(kTriA); // must not crash; its is_connected write is unobservable here
    CHECK(get_triangle_region_id(kTriA) == 5); // unaffected -- region_unlock never touches the triangle's own id
}
