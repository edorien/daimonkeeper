// kfx_platform: bflib_colour_transform.c, a palette swap as a colour function
// (docs/newFeatures/texture-resolution/02-design.md section 3.3). A transform is
// exact when it turns every entry's colour (expanded through the old palette) into
// what expanding through the new palette gives, and black stays black.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstring>
#include <vector>

#include "bflib_colour_transform.h"

namespace {
// A palette like the engine's: entry 0 black, 255 varied 6-bit colours, a few exact duplicates.
using Palette = std::array<unsigned char, 768>;

Palette base_palette()
{
    Palette p{};
    uint32_t s = 12345;
    auto next = [&s]() { s = s * 1103515245u + 12345u; return (s >> 16) & 63; };
    for (int i = 1; i < 256; i++)
    {
        p[3 * i + 0] = (unsigned char)next();
        p[3 * i + 1] = (unsigned char)next();
        p[3 * i + 2] = (unsigned char)next();
        if ((p[3 * i] | p[3 * i + 1] | p[3 * i + 2]) == 0)
            p[3 * i] = 1; // only entry 0 is black
    }
    // Duplicates, as the engine palette has (4, 50), (18, 127), ...
    std::memcpy(&p[3 * 50], &p[3 * 4], 3);
    std::memcpy(&p[3 * 127], &p[3 * 18], 3);
    return p;
}

TbPixel expand(const Palette &pal, int i)
{
    return TbPixel_RGB(chan6_to_8(pal[3 * i]), chan6_to_8(pal[3 * i + 1]), chan6_to_8(pal[3 * i + 2]));
}

bool same_rgb(TbPixel a, TbPixel b) { return (a.r == b.r) && (a.g == b.g) && (a.b == b.b); }

// Every entry 1..255 expanded through `from` and transformed equals it expanded through `to`.
void check_exact(const ColourTransform &ct, const Palette &from, const Palette &to)
{
    for (int i = 1; i < 256; i++)
    {
        INFO("entry " << i);
        CHECK(same_rgb(colour_transform_apply(&ct, expand(from, i)), expand(to, i)));
    }
    CHECK(same_rgb(colour_transform_apply(&ct, TbPixel_RGB(0, 0, 0)), TbPixel_RGB(0, 0, 0)));
}
}

TEST_CASE("colour_transform: the same palette is the identity", "[colour_transform]")
{
    const auto base = base_palette();
    ColourTransform ct = {};
    CHECK(colour_transform_build(&ct, base.data(), base.data(), nullptr) == CTM_Identity);
    CHECK(same_rgb(colour_transform_apply(&ct, TbPixel_RGB(10, 20, 30)), TbPixel_RGB(10, 20, 30)));
}

TEST_CASE("colour_transform: a per-channel fade becomes exact curves, black pinned", "[colour_transform]")
{
    const auto base = base_palette();
    // The pain fade's formula (vidfade.c, PaletteFadePlayer()) at step 60 of 120.
    auto pain = base;
    for (int i = 0; i < 256; i++)
    {
        pain[3 * i + 0] = (unsigned char)((60 * ((int)base[3 * i] - 63)) / 120 + 63);
        pain[3 * i + 1] = (unsigned char)((60 * base[3 * i + 1]) / 120);
        pain[3 * i + 2] = (unsigned char)((60 * base[3 * i + 2]) / 120);
    }
    ColourTransform ct = {};
    REQUIRE(colour_transform_build(&ct, base.data(), pain.data(), nullptr) == CTM_Curves);
    check_exact(ct, base, pain);
    // Black stays black although the fade turns entry 0 red: texel 0 shows black on the CPU too.
    CHECK(pain[0] != 0);
    // Between sample points the curves don't go backwards (the fade is monotone).
    for (int c = 0; c < 3; c++)
        for (int x = 1; x < 256; x++)
            CHECK(ct.curves[c][x] >= ct.curves[c][x - 1]);

    // The white palette: everything one colour.
    Palette white;
    white.fill(63);
    REQUIRE(colour_transform_build(&ct, base.data(), white.data(), nullptr) == CTM_Curves);
    check_exact(ct, base, white);
}

TEST_CASE("colour_transform: a non-separable swap becomes an exact 64^3 table", "[colour_transform]")
{
    const auto base = base_palette();
    // A freeze-like palette: each entry's new colour from all three old channels.
    auto blue = base;
    for (int i = 1; i < 256; i++)
    {
        const int lum = (base[3 * i] * 3 + base[3 * i + 1] * 6 + base[3 * i + 2]) / 10;
        blue[3 * i + 0] = (unsigned char)(lum / 3);
        blue[3 * i + 1] = (unsigned char)(lum / 2);
        blue[3 * i + 2] = (unsigned char)(lum > 0 ? lum : 1);
    }
    std::vector<uint8_t> nearest(COLOUR_TRANSFORM_TABLE_SIZE);
    std::vector<uint8_t> table(4 * COLOUR_TRANSFORM_TABLE_SIZE);
    const auto t0 = std::chrono::steady_clock::now();
    colour_transform_nearest_entries(base.data(), nearest.data());
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    WARN("nearest-entry table: " << ms << " ms");

    ColourTransform ct = {};
    CHECK(colour_transform_build(&ct, base.data(), blue.data(), nearest.data()) == CTM_Unsupported); // no table given
    ct.table = table.data();
    REQUIRE(colour_transform_build(&ct, base.data(), blue.data(), nearest.data()) == CTM_Table);
    check_exact(ct, base, blue);
    // A colour no entry has takes its nearest entry's new colour.
    const int e = 100;
    const TbPixel near_e = TbPixel_RGB((uint8_t)(chan6_to_8(base[3 * e]) ^ 1), chan6_to_8(base[3 * e + 1]), chan6_to_8(base[3 * e + 2]));
    const int key = ((near_e.r >> 2) << 12) | ((near_e.g >> 2) << 6) | (near_e.b >> 2);
    CHECK(same_rgb(colour_transform_apply(&ct, near_e), expand(blue, nearest[key])));
}

TEST_CASE("colour_transform: swaps no colour function reproduces are unsupported", "[colour_transform]")
{
    const auto base = base_palette();
    ColourTransform ct = {};
    std::vector<uint8_t> nearest(COLOUR_TRANSFORM_TABLE_SIZE), table(4 * COLOUR_TRANSFORM_TABLE_SIZE);
    colour_transform_nearest_entries(base.data(), nearest.data());
    ct.table = table.data();
    SECTION("two entries of one colour get different new colours")
    {
        auto to = base;
        to[3 * 50] = (unsigned char)((base[3 * 4] + 7) & 63); // 50 duplicates 4 in base
        CHECK(colour_transform_build(&ct, base.data(), to.data(), nearest.data()) == CTM_Unsupported);
    }
    SECTION("a black entry other than 0 stops being black")
    {
        auto from = base;
        from[3 * 200] = from[3 * 200 + 1] = from[3 * 200 + 2] = 0;
        auto to = from;
        to[3 * 200] = 5;
        CHECK(colour_transform_build(&ct, from.data(), to.data(), nearest.data()) == CTM_Unsupported);
    }
}

TEST_CASE("colour_transform_nearest_entries: an entry's colour finds an entry of that colour, never 0", "[colour_transform]")
{
    const auto base = base_palette();
    std::vector<uint8_t> nearest(COLOUR_TRANSFORM_TABLE_SIZE);
    colour_transform_nearest_entries(base.data(), nearest.data());
    for (int i = 1; i < 256; i++)
    {
        const int key = (base[3 * i] << 12) | (base[3 * i + 1] << 6) | base[3 * i + 2];
        INFO("entry " << i);
        CHECK(std::memcmp(&base[3 * nearest[key]], &base[3 * i], 3) == 0);
    }
    CHECK(nearest[0] != 0);
}
