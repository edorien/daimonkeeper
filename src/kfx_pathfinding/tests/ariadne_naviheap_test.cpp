// kfx_pathfinding: ariadne_naviheap.c, per docs/refactor/testing/
// stage-04b-kfx-pathfinding.md's "reasonable next candidates" list.
// Originally scoped to capacity/empty-state mechanics only -- the heap's
// actual priority ordering is driven by naviheap_item_tree_val(), which
// reads ariadne_navitree's tree_val[] array through naviheap_get().
// Follow-up pass (2026-08-29, once ariadne_navitree_test.cpp landed
// tree_val[]'s own fixture) adds that ordering test: a real min-heap
// round-trip over three items with distinct tree_val priorities, using
// scratch ids far from both the capacity test's 0..256 range and
// ariadne_navitree_test.cpp's own TREEITEMS_COUNT-1 scratch slot.
#include <catch2/catch_test_macros.hpp>

#include "ariadne_naviheap.h"
#include "ariadne_navitree.h" // tree_val[], for the priority-ordering test

namespace {
struct ResetNaviheap {
    ResetNaviheap() { naviheap_init(); }
};
}

TEST_CASE_METHOD(ResetNaviheap, "naviheap_empty reflects whether anything has been added", "[kfx_pathfinding][ariadne_naviheap]") {
    CHECK(naviheap_empty());
    REQUIRE(naviheap_add(1));
    CHECK_FALSE(naviheap_empty());
}

TEST_CASE_METHOD(ResetNaviheap, "naviheap_remove returns -1 on an empty heap", "[kfx_pathfinding][ariadne_naviheap]") {
    CHECK(naviheap_remove() == -1);
}

TEST_CASE_METHOD(ResetNaviheap, "naviheap_add rejects once the heap is full", "[kfx_pathfinding][ariadne_naviheap]") {
    // PATH_HEAP_LEN - 1 usable slots (index 0 unused, one more element
    // deliberately left free -- see naviheap_add()'s own comment).
    bool all_accepted = true;
    for (int64_t i = 0; i < PATH_HEAP_LEN - 1; i++) {
        all_accepted = all_accepted && naviheap_add(i);
    }
    CHECK(all_accepted);
    CHECK_FALSE(naviheap_add(9999)); // one past capacity
}

TEST_CASE_METHOD(ResetNaviheap, "naviheap_top/naviheap_remove pop items in ascending tree_val order (a real min-heap)", "[kfx_pathfinding][ariadne_naviheap]") {
    constexpr int64_t kLow = TREEITEMS_COUNT - 2;
    constexpr int64_t kMid = TREEITEMS_COUNT - 3;
    constexpr int64_t kHigh = TREEITEMS_COUNT - 4;
    tree_val[kLow] = 10;
    tree_val[kMid] = 50;
    tree_val[kHigh] = 90;

    // Added out of priority order, on purpose.
    REQUIRE(naviheap_add(kMid));
    REQUIRE(naviheap_add(kHigh));
    REQUIRE(naviheap_add(kLow));

    CHECK(naviheap_top() == kLow);
    CHECK(naviheap_remove() == kLow);
    CHECK(naviheap_top() == kMid);
    CHECK(naviheap_remove() == kMid);
    CHECK(naviheap_top() == kHigh);
    CHECK(naviheap_remove() == kHigh);
    CHECK(naviheap_empty());
}
