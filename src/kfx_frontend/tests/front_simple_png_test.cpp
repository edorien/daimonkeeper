// kfx_frontend: front_simple.c's PNG decode for the 32-bit start-up screens,
// run against the shipped files in config/fxdata/daimonkeeper/.
#include <catch2/catch_test_macros.hpp>

#include "front_simple.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
std::vector<unsigned char> slurp(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
}

TEST_CASE("decode_png_screen_rgba reads the shipped start-up screens", "[front_simple][png]")
{
    struct { const char *name; int64_t w, h; } screens[] = {
        {"splash.png", 1920, 1440}, {"splash-wide.png", 2560, 1440},
        {"legal.png", 1920, 1440}, {"legal-wide.png", 2560, 1440},
    };
    for (const auto &s : screens) {
        INFO(s.name);
        std::vector<unsigned char> data = slurp(std::string(KFX_TEST_REPO_ROOT) + "/config/fxdata/daimonkeeper/" + s.name);
        REQUIRE_FALSE(data.empty());
        int64_t w = 0, h = 0;
        TbPixel *px = decode_png_screen_rgba(data.data(), data.size(), &w, &h);
        REQUIRE(px != nullptr);
        CHECK(w == s.w);
        CHECK(h == s.h);
        // opaque, and not all black: the centre of every screen has lit artwork or text
        uint64_t sum = 0;
        for (int64_t y = h / 3; y < 2 * h / 3; y += 7)
            for (int64_t x = w / 3; x < 2 * w / 3; x += 7) {
                sum += px[y * w + x].r;
                CHECK(px[y * w + x].a == 255);
            }
        CHECK(sum > 0);
        free(px);
    }
}

TEST_CASE("decode_png_screen_rgba rejects data that isn't a PNG", "[front_simple][png]")
{
    const unsigned char junk[] = "definitely not a png";
    int64_t w = -1, h = -1;
    CHECK(decode_png_screen_rgba(junk, sizeof(junk), &w, &h) == nullptr);
    CHECK(w == -1);
    CHECK(h == -1);
}
