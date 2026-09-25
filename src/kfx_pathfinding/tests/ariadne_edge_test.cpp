// kfx_pathfinding: ariadne_edge.c, per docs/refactor/testing/
// stage-04b-kfx-pathfinding.md's "reasonable next candidates" list
// (docs/refactor/testing/comprehensive/stage-08-comprehensive-library-passes.md).
// Same self-contained-module-with-its-own-reset idiom as
// ariadne_points.c (edge_points_clean() is the reset function, mirroring
// triangulation_initxy_points()).
#include <catch2/catch_test_macros.hpp>

#include "ariadne_edge.h"

namespace {
struct ResetEdgePoints {
    ResetEdgePoints() { edge_points_clean(); }
};
}

TEST_CASE_METHOD(ResetEdgePoints, "edge_point_add stores coordinates and returns sequential ids", "[kfx_pathfinding][ariadne_edge]") {
    int64_t id0 = edge_point_add(10, 20);
    int64_t id1 = edge_point_add(30, 40);
    CHECK(id0 == 0);
    CHECK(id1 == 1);

    struct EdgePoint *pt0 = edge_point_get(id0);
    CHECK(pt0->pt_x == 10);
    CHECK(pt0->pt_y == 20);

    struct EdgePoint *pt1 = edge_point_get(id1);
    CHECK(pt1->pt_x == 30);
    CHECK(pt1->pt_y == 40);
}

TEST_CASE_METHOD(ResetEdgePoints, "edge_point_add returns -1 once the pool is full", "[kfx_pathfinding][ariadne_edge]") {
    ix_EdgePoints = EDGE_POINTS_COUNT; // simulate a full pool without 200 real calls
    CHECK(edge_point_add(1, 1) == -1);
}

TEST_CASE_METHOD(ResetEdgePoints, "edge_point_get falls back to slot 0 for an out-of-range id", "[kfx_pathfinding][ariadne_edge]") {
    CHECK(edge_point_get(-1) == edge_point_get(0));
    CHECK(edge_point_get(EDGE_POINTS_COUNT) == edge_point_get(0));
}
