// kfx_pathfinding: ariadne_update.c -- the second of the "big three"
// (1,773 lines). Five small functions are pure kfx_pathfinding_state
// bookkeeping with no map/callback dependency at all:
// ariadne_set_navigation_map_size/ariadne_reset_navigation_map (a real,
// if small, memory-safety-relevant round trip -- reset only clears the
// sized-in area, not the whole backing array) and the
// map_changed_for_navigation flag trio.
//
// The rest of this file (triangulate_map/init_navigation/
// update_navigation_triangulation, ~900 of its ~936 lines) drives the
// real Delaunay triangulation algorithm (tri_set_rectangle,
// triangulation_init/_initxy/_border_init, fringe_get_rectangle,
// border_clip_horizontal/_vertical, uniform_area_colour, ...) entirely
// through pathfinding_world's map callbacks and kfx_pathfinding_state's
// navigation_map -- every one of those helpers is `static` to this file
// (confirmed via `nm`), so init_navigation()/update_navigation_
// triangulation() are the only way to reach any of it. This is the
// "genuine harness change" this library's big three needed: driving the
// full real triangulation algorithm over pathfinding_fake_world.h's grid
// fake with a small, uniform, all-open-floor map. This one call also
// transitively exercises a meaningful chunk of ariadne_navitree.c
// (delaunay_seeded), ariadne_tringls.c, ariadne_points.c,
// ariadne_edge.c, and ariadne_findcache.c, since all of those are real
// (non-test-fixture) triangulation machinery this drives for the first
// time from a genuinely-computed, non-hand-poked triangle set.
//
// init_navigation()'s own triangulate_area() call always forces "whole
// map" mode (its start_x/start_y are always 0, which is always < 1,
// unconditionally re-deriving [0, get_map_size()+1) bounds regardless of
// what kfx_pathfinding_state.navigation_map_size_x/y were set to) -- so
// this fixture sets the fake grid's logical map size *and*
// navigation_map_size to size+1 (matching that self-correction) purely
// so navmap_tile_number()'s row stride lines up with what the algorithm
// actually iterates, avoiding a silent index mismatch rather than an
// outright crash (kfx_pathfinding_state.navigation_map is a fixed
// 511*511 array either way).
//
// tri_initialised (a function-static bool gating triangulation_initxy)
// only resets on process start, not per test -- by design, the same way
// production code only bootstraps the coordinate system once per game
// session. This file's tests tolerate running in any order/repeatedly
// (verified via the 3x --order rand check) precisely because
// triangulate_area's own "whole map" self-correction re-derives full
// bounds fresh on every call regardless of that one-time flag.
#include <catch2/catch_test_macros.hpp>

#include "ariadne_update.h"
#include "kfx_pathfinding_state.h"
#include "ariadne.h" // nav_map_initialised
#include "ariadne_tringls.h" // Triangles/count_Triangles/TRIANLGLES_COUNT
#include "pathfinding_fake_world.h"

using namespace pf_fake;

namespace {
// A small, uniform, all-open-floor map, sized (logical_size+1) in both the
// fake's own get_map_size_x/y and kfx_pathfinding_state's navigation_map_size
// -- see file header for why the two must agree.
struct TriangulationFixture : GridWorldFixture {
    static constexpr int64_t kLogicalSize = 8;
    TriangulationFixture() {
        grid.size_x = kLogicalSize;
        grid.size_y = kLogicalSize;
        for (int64_t y = 0; y < kLogicalSize; y++) {
            for (int64_t x = 0; x < kLogicalSize; x++) {
                grid.at(x, y).floor_filled_subtiles = 1;
                grid.at(x, y).unsafe = false;
                grid.at(x, y).map_flags = 0;
                grid.at(x, y).walkable = true;
            }
        }
        ariadne_set_navigation_map_size(kLogicalSize + 1, kLogicalSize + 1);
    }
};
} // namespace

TEST_CASE("ariadne_set_navigation_map_size/ariadne_reset_navigation_map round-trip through a small sized area", "[kfx_pathfinding][ariadne_update]") {
    ariadne_set_navigation_map_size(3, 2);
    CHECK(kfx_pathfinding_state.navigation_map_size_x == 3);
    CHECK(kfx_pathfinding_state.navigation_map_size_y == 2);

    // Poison the sized-in area plus one cell just past it.
    for (int64_t i = 0; i < 3 * 2; i++) {
        kfx_pathfinding_state.navigation_map[i] = 0xAA;
    }
    kfx_pathfinding_state.navigation_map[3 * 2] = 0xBB; // outside the sized area

    ariadne_reset_navigation_map();
    for (int64_t i = 0; i < 3 * 2; i++) {
        CHECK(kfx_pathfinding_state.navigation_map[i] == 0);
    }
    // Reset only clears cells within navigation_map_size_x*_y -- this
    // one is outside that bound and must be untouched.
    CHECK(kfx_pathfinding_state.navigation_map[3 * 2] == 0xBB);
}

TEST_CASE("ariadne_is_map_dirty_for_navigation/_mark/_clear round-trip the dirty flag", "[kfx_pathfinding][ariadne_update]") {
    ariadne_clear_map_dirty_for_navigation();
    CHECK_FALSE(ariadne_is_map_dirty_for_navigation());

    ariadne_mark_map_dirty_for_navigation();
    CHECK(ariadne_is_map_dirty_for_navigation());

    ariadne_clear_map_dirty_for_navigation();
    CHECK_FALSE(ariadne_is_map_dirty_for_navigation());
}

TEST_CASE_METHOD(TriangulationFixture, "init_navigation triangulates a small uniform open-floor map without error", "[kfx_pathfinding][ariadne_update][triangulation]") {
    int64_t result = init_navigation();

    CHECK(result == 1);
    CHECK(kfx_pathfinding_state.map_changed_for_navigation == 1);
    CHECK(nav_map_initialised);
    // A uniform open floor still needs at least the outer border
    // triangulated (two triangles per unit square at minimum) -- this is
    // the real algorithm actually having run, not a no-op. Asserted against
    // triangulation_init_triangles's own just-seeded baseline of 2 (see
    // ariadne_tringls_test.cpp), not a "before this call" snapshot: with
    // ariadne_test.cpp's RouteFixture now also triangulating this exact same
    // kLogicalSize=8 all-open shape via the same TriangulatedWorldFixture,
    // a "before" snapshot taken right after an identical prior triangulation
    // elsewhere in the binary is not a reliable lower bound -- re-triangulating
    // an identical-shaped map is idempotent (yields the same count both times),
    // not cumulative. Confirmed order-dependent via --order rand --rng-seed 1
    // before this fix; stable across seeds 1-3 after it.
    CHECK(count_Triangles > 2);
    CHECK(LastTriangulatedMap == kfx_pathfinding_state.navigation_map);
}

TEST_CASE_METHOD(TriangulationFixture, "init_navigation is safe to call repeatedly (tri_initialised is a one-time bootstrap, not per-call state)", "[kfx_pathfinding][ariadne_update][triangulation]") {
    CHECK(init_navigation() == 1);
    // Second call re-triangulates the same uniform map from scratch again
    // -- must not crash, hang, or leave map_changed_for_navigation unset.
    ariadne_clear_map_dirty_for_navigation();
    CHECK(init_navigation() == 1);
    CHECK(kfx_pathfinding_state.map_changed_for_navigation == 1);
}

TEST_CASE_METHOD(TriangulationFixture, "update_navigation_triangulation re-triangulates a sub-rectangle after a floor height change and reports it changed", "[kfx_pathfinding][ariadne_update][triangulation]") {
    REQUIRE(init_navigation() == 1);
    ariadne_clear_map_dirty_for_navigation();

    // Raise the floor in a small sub-area -- changes that area's nav
    // colour, which update_navigation_triangulation must detect.
    for (int64_t y = 3; y <= 5; y++) {
        for (int64_t x = 3; x <= 5; x++) {
            grid.at(x, y).floor_filled_subtiles = 4;
        }
    }

    TbBool result = update_navigation_triangulation(3, 3, 5, 5);
    CHECK(result == true);
    CHECK(kfx_pathfinding_state.map_changed_for_navigation == 1);
    // navmap_tile_number() itself is a private #define in ariadne_update.c
    // (not exposed via any header) -- reproduced here rather than faked.
    int64_t idx = 4 * kfx_pathfinding_state.navigation_map_size_x + 4;
    CHECK(kfx_pathfinding_state.navigation_map[idx] == 4);
}

TEST_CASE_METHOD(TriangulationFixture, "update_navigation_triangulation reports no change when the re-scanned area's colours are already up to date", "[kfx_pathfinding][ariadne_update][triangulation]") {
    REQUIRE(init_navigation() == 1);
    ariadne_clear_map_dirty_for_navigation();

    // Re-scan the same uniform area with nothing changed underneath it.
    TbBool result = update_navigation_triangulation(2, 2, 4, 4);
    CHECK(result == true); // always returns true regardless of `changed`
    CHECK_FALSE(kfx_pathfinding_state.map_changed_for_navigation); // ...but the dirty flag only gets set when colours actually differed
}
