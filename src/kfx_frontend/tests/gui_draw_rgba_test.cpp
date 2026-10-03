// kfx_frontend: gui_draw.c's 32-bit image helpers used by the start-up
// splash/legal screens (front_simple.c) -- the tent-filter resize and the
// letterboxing whole-screen copy. Pure pixel arithmetic, no renderer.
#include <catch2/catch_test_macros.hpp>

#include "gui_draw.h"

#include <vector>

namespace {
TbPixel px(uint8_t v) { return TbPixel_RGBA(v, v, v, 255); }
}

TEST_CASE("resample_rgba_image keeps a flat colour flat at any size", "[gui_draw][rgba]")
{
    std::vector<TbPixel> src(7 * 5, TbPixel_RGBA(200, 100, 50, 255));
    for (auto size : {std::pair<int, int>{7, 5}, {3, 2}, {1, 1}, {20, 13}}) {
        std::vector<TbPixel> dst(size.first * size.second);
        REQUIRE(resample_rgba_image(src.data(), 7, 5, dst.data(), size.first, size.second));
        for (const TbPixel &p : dst) {
            CHECK(p.r == 200);
            CHECK(p.g == 100);
            CHECK(p.b == 50);
            CHECK(p.a == 255);
        }
    }
}

TEST_CASE("resample_rgba_image at the same size is an identity", "[gui_draw][rgba]")
{
    std::vector<TbPixel> src(4 * 3);
    for (size_t i = 0; i < src.size(); i++)
        src[i] = px((uint8_t)(i * 20));
    std::vector<TbPixel> dst(src.size());
    REQUIRE(resample_rgba_image(src.data(), 4, 3, dst.data(), 4, 3));
    for (size_t i = 0; i < src.size(); i++)
        CHECK(dst[i].r == src[i].r);
}

TEST_CASE("resample_rgba_image averages every source pixel when halving", "[gui_draw][rgba]")
{
    // 1-pixel checkerboard: skipping pixels would give pure black or white, averaging gives grey
    std::vector<TbPixel> src(8 * 8);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            src[y * 8 + x] = px(((x + y) & 1) ? 255 : 0);
    std::vector<TbPixel> dst(4 * 4);
    REQUIRE(resample_rgba_image(src.data(), 8, 8, dst.data(), 4, 4));
    for (const TbPixel &p : dst) {
        CHECK(p.r > 96);
        CHECK(p.r < 160);
    }
}

TEST_CASE("resample_rgba_image rejects empty sizes", "[gui_draw][rgba]")
{
    TbPixel one = px(1);
    CHECK_FALSE(resample_rgba_image(&one, 0, 1, &one, 1, 1));
    CHECK_FALSE(resample_rgba_image(&one, 1, 1, &one, 1, 0));
}

TEST_CASE("copy_rgba_image_buffer centres the image and blacks out the rest", "[gui_draw][rgba]")
{
    const int W = 6, H = 4;
    std::vector<TbPixel> screen(W * H, px(77));
    std::vector<TbPixel> img(2 * 2, px(255));
    REQUIRE(copy_rgba_image_buffer(screen.data(), W, H, 2, 1, img.data(), 2, 2));
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            bool inside = (x >= 2 && x < 4 && y >= 1 && y < 3);
            INFO("x=" << x << " y=" << y);
            CHECK(screen[y * W + x].r == (inside ? 255 : 0));
        }
}

TEST_CASE("copy_rgba_image_buffer clips an image larger than the buffer", "[gui_draw][rgba]")
{
    const int W = 3, H = 2;
    std::vector<TbPixel> screen(W * H, px(77));
    std::vector<TbPixel> img(5 * 4);
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 5; x++)
            img[y * 5 + x] = px((uint8_t)(y * 10 + x));
    REQUIRE(copy_rgba_image_buffer(screen.data(), W, H, -1, -1, img.data(), 5, 4));
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            CHECK(screen[y * W + x].r == (y + 1) * 10 + (x + 1));
}
