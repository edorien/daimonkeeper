// kfx_platform: renderer/software/bflib_vidraw.c -- the real-rendering-
// surface half of this file (LbSpriteDraw*/LbDrawBoxImmediate/etc.,
// which write through lbDisplay.WScreen) is the "significant harness
// change" boundary this project has consistently declined. But the
// LbSpriteSetScaling*Array family is different: pure fixed-point
// (16.16) scan-conversion math writing into a caller-owned int32_t[]
// array, no screen buffer touched at all -- confirmed by reading every
// line, not assumed. First coverage this 2,117-line file has ever had.
//
// Test values deliberately use swidth==dwidth (a 1:1 identity mapping,
// factor exactly 0x10000) rather than an arbitrary ratio -- the
// fixed-point arithmetic is still exercised for real (shift/round/
// accumulate), but the expected per-step output reduces to "one unit
// wide, starting at x, incrementing by one each step", which is safe to
// hand-verify without silently reimplementing the function's own math
// as the "expected" value.
#include <catch2/catch_test_macros.hpp>

#include "bflib_vidraw.h"

TEST_CASE("LbSpriteSetScalingWidthClippedArray produces a 1-wide-per-step identity mapping when swidth==dwidth", "[kfx_platform][bflib_vidraw]") {
    int64_t arr[6] = {0};
    LbSpriteSetScalingWidthClippedArray(arr, 5, 3, 3, 100);
    CHECK(arr[0] == 5); CHECK(arr[1] == 1);
    CHECK(arr[2] == 6); CHECK(arr[3] == 1);
    CHECK(arr[4] == 7); CHECK(arr[5] == 1);
}

TEST_CASE("LbSpriteSetScalingWidthClippedArray clips both start and end independently to [0, gwidth]", "[kfx_platform][bflib_vidraw]") {
    int64_t arr[6] = {0};
    // Same identity mapping as above (x=5, positions 5,6,7) but gwidth=6
    // this time -- step 2 (6..7) and step 3 (7..8) both get clamped.
    LbSpriteSetScalingWidthClippedArray(arr, 5, 3, 3, 6);
    CHECK(arr[0] == 5); CHECK(arr[1] == 1); // 5..6, fully within [0,6]
    CHECK(arr[2] == 6); CHECK(arr[3] == 0); // 6..7 -> end clipped to 6, zero width
    CHECK(arr[4] == 6); CHECK(arr[5] == 0); // 7..8 -> start and end both clipped to 6
}

TEST_CASE("LbSpriteSetScalingWidthSimpleArray matches the clipped variant's identity mapping when nothing needs clipping", "[kfx_platform][bflib_vidraw]") {
    int64_t arr[6] = {0};
    LbSpriteSetScalingWidthSimpleArray(arr, 5, 3, 3);
    CHECK(arr[0] == 5); CHECK(arr[1] == 1);
    CHECK(arr[2] == 6); CHECK(arr[3] == 1);
    CHECK(arr[4] == 7); CHECK(arr[5] == 1);
}

TEST_CASE("LbSpriteSetScalingHeightClippedArray produces the same identity mapping as the width variant", "[kfx_platform][bflib_vidraw]") {
    int64_t arr[6] = {0};
    LbSpriteSetScalingHeightClippedArray(arr, 5, 3, 3, 100);
    CHECK(arr[0] == 5); CHECK(arr[1] == 1);
    CHECK(arr[2] == 6); CHECK(arr[3] == 1);
    CHECK(arr[4] == 7); CHECK(arr[5] == 1);
}

TEST_CASE("LbSpriteSetScalingHeightSimpleArray matches the clipped variant's identity mapping when nothing needs clipping", "[kfx_platform][bflib_vidraw]") {
    int64_t arr[6] = {0};
    LbSpriteSetScalingHeightSimpleArray(arr, 5, 3, 3);
    CHECK(arr[0] == 5); CHECK(arr[1] == 1);
    CHECK(arr[2] == 6); CHECK(arr[3] == 1);
    CHECK(arr[4] == 7); CHECK(arr[5] == 1);
}

TEST_CASE("LbSpriteClearScalingWidthArray/HeightArray zero-fill their position+length pairs", "[kfx_platform][bflib_vidraw]") {
    int64_t warr[6] = {1, 2, 3, 4, 5, 6};
    LbSpriteClearScalingWidthArray(warr, 3);
    for (int64_t i = 0; i < 6; i++) CHECK(warr[i] == 0);

    int64_t harr[6] = {1, 2, 3, 4, 5, 6};
    LbSpriteClearScalingHeightArray(harr, 3);
    for (int64_t i = 0; i < 6; i++) CHECK(harr[i] == 0);
}
