// kfx_frontend: copy_raw8_image_buffer_rect's confinement/clip/pan
// arithmetic (gui_draw.c). Verified against resolve_indexed_pixel() with
// a palette this fixture sets directly via LbPaletteStore -- exact
// resolved RGB values aren't asserted as literals (that would couple this
// test to chan6_to_8's rounding), only that copy_raw8_image_buffer_rect
// resolves the same way resolve_indexed_pixel() itself does, and that its
// own confinement/clip/pan logic is correct.
#include <catch2/catch_test_macros.hpp>

#include "gui_draw.h"
#include "renderer/RendererManager.h"
#include "bflib_video.h"

#include <vector>
#include <cstring>

namespace {
// A small buffer wide enough to hold a rect plus a margin on every side,
// so "untouched outside the rect" is actually checkable.
constexpr int64_t BUF_W = 20;
constexpr int64_t BUF_H = 20;
const TbPixel SENTINEL = {99, 99, 99, 99};

struct RectBlitFixture {
    std::vector<TbPixel> dst;

    RectBlitFixture() : dst(BUF_W * BUF_H, SENTINEL) {
        // Deterministic palette: index i -> (i*20, i*20, i*20) once
        // resolve_indexed_pixel's chan6_to_8 expansion runs -- exact
        // values aren't asserted, just self-consistency with
        // resolve_indexed_pixel() computed the same way.
        unsigned char pal[PALETTE_SIZE] = {0};
        for (int64_t i = 0; i < 8; i++) {
            pal[3*i+0] = (unsigned char)(i * 8);
            pal[3*i+1] = (unsigned char)(i * 8);
            pal[3*i+2] = (unsigned char)(i * 8);
        }
        LbPaletteStore(pal);
    }

    TbPixel at(int64_t x, int64_t y) const { return dst[y * BUF_W + x]; }
    TbPixel resolved(unsigned char index) const {
        return resolve_indexed_pixel(index, RendererGetActivePalette());
    }
};
}

TEST_CASE_METHOD(RectBlitFixture, "copy_raw8_image_buffer_rect confines drawing to the given rect, leaving outside pixels untouched", "[kfx_frontend][gui_draw]") {
    unsigned char src[4] = {1, 1, 1, 1}; // 2x2, all index 1
    TbBool ok = copy_raw8_image_buffer_rect(dst.data(), BUF_W, BUF_H,
        /*rect*/ 5, 5, 4, 4,
        /*dst size*/ 4, 4, /*pan*/ 0, 0,
        src, 2, 2);
    REQUIRE(ok);
    // Outside the rect: still the sentinel.
    CHECK(TbPixel_Equal(at(0, 0), SENTINEL));
    CHECK(TbPixel_Equal(at(19, 19), SENTINEL));
    CHECK(TbPixel_Equal(at(4, 5), SENTINEL));  // just left of the rect
    CHECK(TbPixel_Equal(at(9, 5), SENTINEL));  // just right of the rect (rect is [5,9))
    // Inside the rect: resolved source colour, not the sentinel.
    CHECK(TbPixel_Equal(at(6, 6), resolved(1)));
}

TEST_CASE_METHOD(RectBlitFixture, "copy_raw8_image_buffer_rect draws each source pixel resolved through the active palette", "[kfx_frontend][gui_draw]") {
    unsigned char src[4] = {2, 3, 4, 5}; // 2x2: top-left=2, top-right=3, bot-left=4, bot-right=5
    TbBool ok = copy_raw8_image_buffer_rect(dst.data(), BUF_W, BUF_H,
        0, 0, 10, 10,
        /*dst size*/ 2, 2, /*pan*/ 0, 0,
        src, 2, 2);
    REQUIRE(ok);
    CHECK(TbPixel_Equal(at(0, 0), resolved(2)));
    CHECK(TbPixel_Equal(at(1, 0), resolved(3)));
    CHECK(TbPixel_Equal(at(0, 1), resolved(4)));
    CHECK(TbPixel_Equal(at(1, 1), resolved(5)));
}

TEST_CASE_METHOD(RectBlitFixture, "copy_raw8_image_buffer_rect clears the rect's own margin when the drawn image doesn't fill it", "[kfx_frontend][gui_draw]") {
    unsigned char src[1] = {6};
    // dst image is only 3x3, well inside a 10x10 rect -- top-left corner
    // of the rect should be cleared to black (the fill colour used for
    // margins), not left as the sentinel.
    TbBool ok = copy_raw8_image_buffer_rect(dst.data(), BUF_W, BUF_H,
        0, 0, 10, 10,
        3, 3, /*pan*/ 0, 0,
        src, 1, 1);
    REQUIRE(ok);
    TbPixel black = TbPixel_RGB(0, 0, 0);
    CHECK(TbPixel_Equal(at(0, 0), resolved(6))); // inside the drawn 3x3 image
    CHECK(TbPixel_Equal(at(5, 5), black)); // margin below/right of the 3x3 image, still inside the rect
    CHECK(TbPixel_Equal(at(10, 5), SENTINEL)); // outside the rect entirely
}

TEST_CASE_METHOD(RectBlitFixture, "copy_raw8_image_buffer_rect pans a source image larger than the rect via a negative offset", "[kfx_frontend][gui_draw]") {
    // A 4x4 source, drawn at 1:1 (dst size == src size), panned so its
    // top-left 2 rows/cols are scrolled out of view above/left of the rect.
    unsigned char src[16];
    for (int64_t i = 0; i < 16; i++) src[i] = (unsigned char)i;
    TbBool ok = copy_raw8_image_buffer_rect(dst.data(), BUF_W, BUF_H,
        /*rect*/ 5, 5, 2, 2,
        /*dst size*/ 4, 4, /*pan*/ -2, -2,
        src, 4, 4);
    REQUIRE(ok);
    // rect-local (0,0) == absolute buffer pixel (5,5) == source pixel (2,2) == index 10.
    CHECK(TbPixel_Equal(at(5, 5), resolved(10)));
    CHECK(TbPixel_Equal(at(6, 6), resolved(15)));
}

TEST_CASE_METHOD(RectBlitFixture, "copy_raw8_image_buffer_rect clips at the destination buffer's own edges", "[kfx_frontend][gui_draw]") {
    unsigned char src[4] = {7, 7, 7, 7};
    // Rect extends past the bottom-right of the whole BUF_W x BUF_H buffer.
    TbBool ok = copy_raw8_image_buffer_rect(dst.data(), BUF_W, BUF_H,
        BUF_W - 2, BUF_H - 2, 10, 10,
        4, 4, 0, 0,
        src, 2, 2);
    REQUIRE(ok);
    // Should not have crashed/corrupted -- last valid pixel still resolves.
    CHECK(TbPixel_Equal(at(BUF_W - 1, BUF_H - 1), resolved(7)));
}

TEST_CASE_METHOD(RectBlitFixture, "copy_raw8_image_buffer_rect rejects a degenerate or fully-offscreen rect", "[kfx_frontend][gui_draw]") {
    unsigned char src[1] = {1};
    CHECK_FALSE(copy_raw8_image_buffer_rect(dst.data(), BUF_W, BUF_H, 5, 5, 0, 0, 1, 1, 0, 0, src, 1, 1));
    CHECK_FALSE(copy_raw8_image_buffer_rect(dst.data(), BUF_W, BUF_H, BUF_W + 5, 5, 4, 4, 1, 1, 0, 0, src, 1, 1));
}
