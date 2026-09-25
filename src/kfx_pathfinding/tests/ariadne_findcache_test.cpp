// kfx_pathfinding: ariadne_findcache.c. Same "scratch triangle far from
// triangulation_init_triangles's Triangles[0]/[1]" discipline as
// ariadne_tringls_test.cpp, since Triangles[] is one module-level array
// shared across the whole binary. triangulation_init_cache()'s own
// private find_cache[4][4] static array is likewise shared/persistent
// across every test in this binary (no reset hook exists for it), so
// every test here explicitly re-seeds it first rather than assuming a
// clean slate.
//
// triangle_brute_find8_near() had a real external-linkage definition but
// no header declaration anywhere -- added to ariadne_findcache.h, the
// usual "add the missing declaration" fix.
//
// triangle_find8()/point_find() (the actual point-location triangulation
// walk, via triangle_divide_areas_s8differ()'s barycentric-sign math)
// aren't attempted here -- reproducing the "hand-verified minimal
// two-triangle fixture" discipline ariadne_regions_test.cpp used for
// region-BFS connectivity, but for point-in-triangle geometry instead,
// is a bigger, separate undertaking deferred for now (same category as
// kfx_pathfinding's "big three").
#include <catch2/catch_test_macros.hpp>

#include "ariadne_findcache.h"
#include "ariadne_tringls.h"

namespace {
constexpr int64_t kScratchTri = TRIANLGLES_COUNT - 1;

struct ResetScratchTriangle {
    ResetScratchTriangle() {
        Triangles[kScratchTri].region_and_edgelen = 0;
        Triangles[kScratchTri].tree_alt = 0;
        Triangles[kScratchTri].tags[0] = 0;
        Triangles[kScratchTri].tags[1] = 0;
        Triangles[kScratchTri].tags[2] = 0;
    }
};
}

TEST_CASE_METHOD(ResetScratchTriangle, "triangulation_init_cache seeds every cache cell, so triangle_brute_find8_near finds it from any query position", "[kfx_pathfinding][ariadne_findcache]") {
    Triangles[kScratchTri].tree_alt = 7; // any value != NAV_COL_UNSET marks the slot as "in use"
    triangulation_init_cache(kScratchTri);

    // pos_x/pos_y >> 14 selects the cache cell; every one of the 16
    // cells was just seeded to kScratchTri, so any query position finds
    // it via the "try sibling" step on the very first check.
    CHECK(triangle_brute_find8_near(0, 0) == kScratchTri);
    CHECK(triangle_brute_find8_near(1 << 20, 1 << 20) == kScratchTri); // a different, far-away cell
}

TEST_CASE_METHOD(ResetScratchTriangle, "triangulation_init_cache with a still-unset triangle leaves triangle_brute_find8_near to fall back past the cache", "[kfx_pathfinding][ariadne_findcache]") {
    // tree_alt left at NAV_COL_UNSET (the default for an unused slot),
    // so every "is this cache cell in use?" check the function runs
    // fails, and it falls through to triangle_find_first_used() -- real
    // production logic in ariadne_tringls.c, not re-verified here, but
    // exercising this fallback path is itself the point: confirms
    // triangle_brute_find8_near doesn't just trust a stale/invalid cache
    // entry blindly.
    Triangles[kScratchTri].tree_alt = NAV_COL_UNSET;
    triangulation_init_cache(kScratchTri);

    int64_t result = triangle_brute_find8_near(0, 0);
    CHECK(result != kScratchTri);
}
