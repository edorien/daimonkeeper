// kfx_pathfinding: ariadne.c -- the largest of the "big three" (3,313
// lines) previously left untouched because most of its surface needs a
// functioning PathfindingWorldCallbacks fake (51-entry interface, a
// dedicated sub-effort of its own -- docs/Architecture/
// testing-harness.md §10). Two functions turned out narrow enough not
// to need that: thing_nav_block_sizexy/thing_nav_sizexy each call
// exactly one callback (thing_get_clipbox_size) and index a private
// lookup table with clamping, testable with a minimal single-field
// PathfindingWorldCallbacks fake (copy the default table, override just
// that one field -- kfx_config's pathfinding_world_test.cpp's own
// pattern). Exact table contents are private static arrays this file
// doesn't expose, so the clamp tests assert structurally (two
// out-of-range inputs both clamp to the same table entry) rather than
// against a hard-coded magic number.
//
// A third candidate, tag_open_closed_init(), was investigated and
// dropped: it looks like a real function from a plain text search (the
// same one that turned up thing_nav_block_sizexy/thing_nav_sizexy), but
// it's actually inside a `/* TODO PATHFINDING Enable when needed ... */`
// comment block -- dead, disabled code, never compiled. Caught by the
// link failure ("undefined reference"), not assumed from reading alone.
//
// The rest of this file's coverage (added once pathfinding_fake_world.h's
// TriangulatedWorldFixture existed) drives ariadne_initialise_creature_route_f/
// creature_follow_route_to_using_gates/ariadne_count_waypoints_on_creature_route_
// to_target_f/ariadne_invalidate_creature_route across a real, genuinely
// triangulated open map. ariadne_prepare_creature_route_to_target_f (called
// from ariadne_initialise_creature_route_f) always passes path_init8_wide_f
// subroute=-2, which takes the ma_triangle_route/tree-route branch, not the
// gate_navigator_init8/route_through_gates branch -- so this file's route-init
// tests transitively cover ma_triangle_route/triangle_route_do_fwd/_bak/
// calc_intersection/cost_to_start/fits_thro/blocked_by_door_at/edge_points8/
// route_to_path/path_out_a_bit/nav_same_component/regions_connected, while the
// gate-navigator branch (creature_follow_route_to_using_gates's internal
// ariadne_get_next_position_for_route path) covers gate_navigator_init8/
// route_through_gates/gate_route_to_coords/cull_gate_to_point/
// cull_gate_to_best_point/fov_region/waypoint_normal separately.
#include <catch2/catch_test_macros.hpp>

#include "ariadne.h"
#include "pathfinding_world.h"
#include "pathfinding_fake_world.h"

using namespace pf_fake;

namespace {
int64_t fake_clipbox_size = 0;
int64_t fake_thing_get_clipbox_size(const struct Thing *) { return fake_clipbox_size; }

struct PathfindingWorldFixture {
    struct PathfindingWorldCallbacks fake;
    PathfindingWorldFixture() {
        fake = *pathfinding_world;
        fake.thing_get_clipbox_size = fake_thing_get_clipbox_size;
        set_pathfinding_world_callbacks(&fake);
    }
    ~PathfindingWorldFixture() {
        set_pathfinding_world_callbacks(nullptr);
    }
};
}

TEST_CASE_METHOD(PathfindingWorldFixture, "thing_nav_block_sizexy/thing_nav_sizexy look up the same table entry for any clipbox size below the table's length", "[kfx_pathfinding][ariadne]") {
    fake_clipbox_size = 0;
    int64_t block_at_0 = thing_nav_block_sizexy(nullptr);
    int64_t sizexy_at_0 = thing_nav_sizexy(nullptr);

    fake_clipbox_size = 1;
    CHECK(thing_nav_block_sizexy(nullptr) == block_at_0);
    CHECK(thing_nav_sizexy(nullptr) == sizexy_at_0);
}

TEST_CASE_METHOD(PathfindingWorldFixture, "thing_nav_block_sizexy/thing_nav_sizexy clamp an out-of-range clipbox size to the table's last entry", "[kfx_pathfinding][ariadne]") {
    // Two different huge indices must clamp to the exact same slot.
    fake_clipbox_size = 60000;
    int64_t block_huge = thing_nav_block_sizexy(nullptr);
    int64_t sizexy_huge = thing_nav_sizexy(nullptr);

    fake_clipbox_size = 65000;
    CHECK(thing_nav_block_sizexy(nullptr) == block_huge);
    CHECK(thing_nav_sizexy(nullptr) == sizexy_huge);
}

TEST_CASE("angle_to_quadrant buckets an angle into one of 4 quadrants, rounding at the 45-degree midpoints", "[kfx_pathfinding][ariadne]") {
    CHECK(angle_to_quadrant(0) == 0);
    CHECK(angle_to_quadrant(DEGREES_90) == 1);
    CHECK(angle_to_quadrant(DEGREES_180) == 2);
    CHECK(angle_to_quadrant(DEGREES_270) == 3);
    // Just below a full turn: (ANGLE_MASK+DEGREES_45)/DEGREES_90 == 4,
    // which wraps back to quadrant 0 via the trailing "& 3".
    CHECK(angle_to_quadrant(ANGLE_MASK) == 0);
}

TEST_CASE_METHOD(TriangulatedWorldFixture, "ariadne_invalidate_creature_route zeroes the creature's whole Ariadne state", "[kfx_pathfinding][ariadne]") {
    FakeThing thing;
    thing.arid.total_waypoints = 5;
    thing.arid.current_waypoint = 2;
    thing.arid.move_speed = 32;

    AriadneReturn ret = ariadne_invalidate_creature_route(as_thing(thing));
    CHECK(ret == AridRet_OK);
    CHECK(thing.arid.total_waypoints == 0);
    CHECK(thing.arid.current_waypoint == 0);
    CHECK(thing.arid.move_speed == 0);
}

namespace {
struct RouteFixture : TriangulatedWorldFixture {
    FakeThing thing;
    RouteFixture() {
        REQUIRE(init_navigation() == 1);
        thing.pos.x.val = subtile_coord_center(2);
        thing.pos.y.val = subtile_coord_center(2);
        thing.pos.z.val = 0;
        thing.clipbox_size = 0;
    }
    struct Thing *t() { return as_thing(thing); }
};
}

TEST_CASE_METHOD(RouteFixture, "ariadne_initialise_creature_route_f finds a real route across an open triangulated map", "[kfx_pathfinding][ariadne][triangulation]") {
    struct Coord3d target{};
    target.x.val = subtile_coord_center(6);
    target.y.val = subtile_coord_center(6);
    target.z.val = 0;

    AriadneReturn ret = ariadne_initialise_creature_route(t(), &target, 32, AridRtF_Default);
    CHECK(ret == AridRet_OK);
    CHECK(thing.arid.total_waypoints > 0);
    CHECK(thing.arid.stored_waypoints > 0);
    CHECK(thing.arid.current_waypoint == 0);
    CHECK(thing.arid.endpos.x.val == target.x.val);
    CHECK(thing.arid.endpos.y.val == target.y.val);
}

TEST_CASE_METHOD(RouteFixture, "ariadne_initialise_creature_route_f takes the already-at-target shortcut when thing is already at pos", "[kfx_pathfinding][ariadne][triangulation]") {
    struct Coord3d target = thing.pos; // same x/y as the thing's own starting position

    AriadneReturn ret = ariadne_initialise_creature_route(t(), &target, 32, AridRtF_Default);
    CHECK(ret == AridRet_OK);
    // ariadne_prepare_creature_route_target_reached's own documented shape:
    // a single-waypoint route sitting at the source position.
    CHECK(thing.arid.total_waypoints == 1);
    CHECK(thing.arid.stored_waypoints == 1);
    CHECK(thing.arid.current_waypoint == 0);
}

TEST_CASE_METHOD(RouteFixture, "ariadne_count_waypoints_on_creature_route_to_target_f reports a positive waypoint count without touching the creature's own Ariadne state", "[kfx_pathfinding][ariadne][triangulation]") {
    struct Coord3d src = thing.pos;
    struct Coord3d dst{};
    dst.x.val = subtile_coord_center(6);
    dst.y.val = subtile_coord_center(6);
    dst.z.val = 0;

    int64_t waypoints = ariadne_count_waypoints_on_creature_route_to_target_f(t(), &src, &dst, AridRtF_Default, "test");
    CHECK(waypoints > 0);
    // This function works on a throwaway local struct Path -- the thing's
    // own Ariadne state (still default-zeroed by RouteFixture) must be untouched.
    CHECK(thing.arid.total_waypoints == 0);
}

TEST_CASE_METHOD(RouteFixture, "creature_follow_route_to_using_gates advances a creature with an already-initialised route toward its next waypoint", "[kfx_pathfinding][ariadne][triangulation]") {
    struct Coord3d target{};
    target.x.val = subtile_coord_center(6);
    target.y.val = subtile_coord_center(6);
    target.z.val = 0;
    REQUIRE(ariadne_initialise_creature_route(t(), &target, 32, AridRtF_Default) == AridRet_OK);

    struct Coord3d nextpos{};
    AriadneReturn ret = creature_follow_route_to_using_gates(t(), &target, &nextpos, 32, AridRtF_Default);
    // AridRet_OK (still travelling) or AridRet_FinalOK (arrived in one step)
    // are both real, valid outcomes on a short open-map hop -- what matters
    // is that a real next step was computed, not a specific arrival state.
    CHECK((ret == AridRet_OK || ret == AridRet_FinalOK));
    // nextpos must have actually been set to somewhere other than the
    // all-zero Coord3d it started as.
    CHECK((nextpos.x.val != 0 || nextpos.y.val != 0));
}
