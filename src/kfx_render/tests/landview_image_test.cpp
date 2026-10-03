#include <catch2/catch_test_macros.hpp>

#include <spng.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <unistd.h>

#include "landview_image.h"

namespace fs = std::filesystem;

namespace {

// Encodes an image with spng: indexed 8-bit (palette given) or RGBA8.
std::vector<uint8_t> encode_png(const std::vector<uint8_t> &pixels, int w, int h, bool indexed, const std::vector<uint8_t> &palette_rgb)
{
    spng_ctx *ctx = spng_ctx_new(SPNG_CTX_ENCODER);
    spng_set_option(ctx, SPNG_ENCODE_TO_BUFFER, 1);
    struct spng_ihdr ihdr = {};
    ihdr.width = (uint32_t)w;
    ihdr.height = (uint32_t)h;
    ihdr.bit_depth = 8;
    ihdr.color_type = indexed ? SPNG_COLOR_TYPE_INDEXED : SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;
    spng_set_ihdr(ctx, &ihdr);
    if (indexed)
    {
        struct spng_plte plte = {};
        plte.n_entries = (uint32_t)(palette_rgb.size() / 3);
        for (uint32_t i = 0; i < plte.n_entries; i++)
        {
            plte.entries[i].red = palette_rgb[i * 3];
            plte.entries[i].green = palette_rgb[i * 3 + 1];
            plte.entries[i].blue = palette_rgb[i * 3 + 2];
        }
        spng_set_plte(ctx, &plte);
    }
    spng_encode_image(ctx, pixels.data(), pixels.size(), SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE);
    size_t len = 0;
    int err = 0;
    void *buf = spng_get_png_buffer(ctx, &len, &err);
    std::vector<uint8_t> out((uint8_t *)buf, (uint8_t *)buf + len);
    free(buf);
    spng_ctx_free(ctx);
    return out;
}

} // namespace

TEST_CASE("an indexed PNG keeps its pixels and gets a 6-bit palette", "[landview_image]")
{
    std::vector<uint8_t> pixels(LANDVIEW_PIXELS);
    for (size_t i = 0; i < pixels.size(); i++)
        pixels[i] = (uint8_t)((i * 7 + i / 1280) & 0xff);
    std::vector<uint8_t> pal(768);
    for (size_t i = 0; i < 256; i++)
    {
        pal[i * 3] = (uint8_t)i;
        pal[i * 3 + 1] = (uint8_t)(255 - i);
        pal[i * 3 + 2] = (uint8_t)((i * 3) & 0xff);
    }
    const std::vector<uint8_t> png = encode_png(pixels, LANDVIEW_WIDTH, LANDVIEW_HEIGHT, true, pal);
    LandviewImage img;
    std::string err;
    REQUIRE(landview_decode_png(png.data(), png.size(), img, err));
    CHECK(img.pixels == pixels);
    for (size_t i = 0; i < 768; i++)
        CHECK(img.palette[i] == (pal[i] >> 2));
}

TEST_CASE("an RGBA PNG is quantised to at most 256 colours, deterministically", "[landview_image]")
{
    std::vector<uint8_t> rgba((size_t)LANDVIEW_PIXELS * 4);
    for (int y = 0; y < LANDVIEW_HEIGHT; y++)
        for (int x = 0; x < LANDVIEW_WIDTH; x++)
        {
            uint8_t *p = &rgba[((size_t)y * LANDVIEW_WIDTH + (size_t)x) * 4];
            p[0] = (uint8_t)(x * 255 / LANDVIEW_WIDTH);
            p[1] = (uint8_t)(y * 255 / LANDVIEW_HEIGHT);
            p[2] = (uint8_t)((x + y) & 0xff);
            p[3] = 255;
        }
    const std::vector<uint8_t> png = encode_png(rgba, LANDVIEW_WIDTH, LANDVIEW_HEIGHT, false, {});
    LandviewImage a, b;
    std::string err;
    REQUIRE(landview_decode_png(png.data(), png.size(), a, err));
    REQUIRE(landview_decode_png(png.data(), png.size(), b, err));
    CHECK(a.pixels == b.pixels);
    CHECK(std::equal(a.palette, a.palette + 768, b.palette));
    CHECK(a.pixels.size() == (size_t)LANDVIEW_PIXELS);
    // The palette reproduces the picture closely: mean channel error small.
    const std::vector<uint8_t> back = landview_to_rgba(a);
    uint64_t error = 0;
    for (size_t i = 0; i < rgba.size(); i += 4)
        for (int c = 0; c < 3; c++)
            error += (uint64_t)std::abs((int)rgba[i + (size_t)c] - (int)back[i + (size_t)c]);
    CHECK(error / ((uint64_t)LANDVIEW_PIXELS * 3) < 12);
}

TEST_CASE("a flat colour quantises to one entry", "[landview_image]")
{
    std::vector<uint8_t> rgba((size_t)16 * 16 * 4, 255);
    for (size_t i = 0; i < 16 * 16; i++)
    {
        rgba[i * 4] = 200;
        rgba[i * 4 + 1] = 100;
        rgba[i * 4 + 2] = 50;
    }
    LandviewImage img;
    landview_quantise(rgba.data(), 16, 16, img);
    CHECK(std::set<uint8_t>(img.pixels.begin(), img.pixels.end()).size() == 1);
}

TEST_CASE("a PNG of the wrong size is refused with the sizes", "[landview_image]")
{
    const std::vector<uint8_t> png = encode_png(std::vector<uint8_t>(10 * 10 * 4, 0), 10, 10, false, {});
    LandviewImage img;
    std::string err;
    CHECK_FALSE(landview_decode_png(png.data(), png.size(), img, err));
    CHECK(err.find("10 x 10") != std::string::npos);
    CHECK(err.find("1280 x 960") != std::string::npos);
    const uint8_t junk[4] = {1, 2, 3, 4};
    CHECK_FALSE(landview_decode_png(junk, 4, img, err));
}

TEST_CASE("the loader prefers a PNG, and falls back to .raw + .pal", "[landview_image]")
{
    const fs::path dir = fs::temp_directory_path() / ("kfx_landview_test_" + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::vector<uint8_t> raw(LANDVIEW_PIXELS, 3), pal(768, 0);
    pal[9] = 40; // entry 3, red
    std::ofstream((dir / "a.raw").string(), std::ios::binary).write((const char *)raw.data(), (long)raw.size());
    std::ofstream((dir / "a.pal").string(), std::ios::binary).write((const char *)pal.data(), (long)pal.size());
    LandviewImage img;
    std::string err;
    bool png = true;
    REQUIRE(landview_load((dir / "a").string(), img, err, &png));
    CHECK_FALSE(png);
    CHECK(img.pixels[0] == 3);
    CHECK(img.palette[9] == 40);

    std::vector<uint8_t> px(LANDVIEW_PIXELS, 1), rgb(768, 0);
    rgb[3] = 252;
    const std::vector<uint8_t> file = encode_png(px, LANDVIEW_WIDTH, LANDVIEW_HEIGHT, true, rgb);
    std::ofstream((dir / "a.png").string(), std::ios::binary).write((const char *)file.data(), (long)file.size());
    REQUIRE(landview_load((dir / "a").string(), img, err, &png));
    CHECK(png);
    CHECK(img.pixels[0] == 1);
    CHECK(img.palette[3] == 63);

    // The C entry the game uses.
    std::vector<uint8_t> out_px(LANDVIEW_PIXELS), out_pal(768);
    CHECK(landview_load_png_indexed((dir / "a").string().c_str(), out_px.data(), out_pal.data()) == 1);
    CHECK(landview_load_png_indexed((dir / "nothing").string().c_str(), out_px.data(), out_pal.data()) == 0);
    fs::remove_all(dir);
}
