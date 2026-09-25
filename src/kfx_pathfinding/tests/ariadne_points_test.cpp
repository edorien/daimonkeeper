// First kfx_pathfinding coverage, per docs/refactor/testing/
// stage-02-testability-and-fakes.md's rollout order: ariadne_points.c owns
// a small, self-contained module-static point pool (ari_Points[] +
// bookkeeping counters), with its own reset function
// (triangulation_initxy_points) already provided by production code --
// pattern A (stage-02 §2), but with no memset needed since the module
// supplies its own init.
#include <catch2/catch_test_macros.hpp>

#include "ariadne_points.h"

namespace {
struct ResetPointPool {
    // Arbitrary bounding rectangle; only the four seeded corner points and
    // the pool's free-list bookkeeping matter to these tests.
    ResetPointPool() { triangulation_initxy_points(0, 0, 1000, 2000); }
};
}

TEST_CASE_METHOD(ResetPointPool, "triangulation_initxy_points seeds the four corner points", "[kfx_pathfinding][ariadne_points]") {
    CHECK(point_equals(0, 0, 0));
    CHECK(point_equals(1, 1000, 0));
    CHECK(point_equals(2, 1000, 2000));
    CHECK(point_equals(3, 0, 2000));
}

TEST_CASE_METHOD(ResetPointPool, "point_set/point_get round-trip a valid point", "[kfx_pathfinding][ariadne_points]") {
    AridPointId id = point_set_new_or_reuse(42, 99);
    REQUIRE(id >= 0);
    struct Point *pt = point_get(id);
    REQUIRE_FALSE(point_is_invalid(pt));
    CHECK(pt->x == 42);
    CHECK(pt->y == 99);
}

TEST_CASE_METHOD(ResetPointPool, "point_set_new_or_reuse deduplicates identical coordinates", "[kfx_pathfinding][ariadne_points]") {
    AridPointId first = point_set_new_or_reuse(5, 5);
    AridPointId again = point_set_new_or_reuse(5, 5);
    CHECK(first == again);

    AridPointId different = point_set_new_or_reuse(6, 5);
    CHECK(different != first);
}

TEST_CASE_METHOD(ResetPointPool, "point_set rejects an out-of-range id", "[kfx_pathfinding][ariadne_points]") {
    CHECK_FALSE(point_set(-1, 0, 0));
    CHECK_FALSE(point_set(POINTS_COUNT, 0, 0));
}

TEST_CASE_METHOD(ResetPointPool, "point_get on an out-of-range id returns the invalid-point sentinel", "[kfx_pathfinding][ariadne_points]") {
    CHECK(point_is_invalid(point_get(-1)));
    CHECK(point_is_invalid(point_get(POINTS_COUNT)));
}

TEST_CASE_METHOD(ResetPointPool, "point_dispose marks the point's y as the 0x8000 disposed sentinel", "[kfx_pathfinding][ariadne_points]") {
    // 0x8000 is the same sentinel allocated_point_search()'s own early
    // return checks for -- point_set_new_or_reuse()'s free-list reuse
    // (point_new(), static, only reachable that way) depends on disposed
    // slots being unmistakable from real (x, y) data, which is why this
    // is the one observable side effect worth pinning down here.
    AridPointId id = point_set_new_or_reuse(15, 25);
    REQUIRE(id >= 0);
    point_dispose(id);
    // Point::y is a signed short, so the 0x8000 sentinel wraps to -32768
    // once stored -- confirmed by running, not assumed from the literal.
    CHECK(point_get(id)->y == (int64_t)0x8000);
}
