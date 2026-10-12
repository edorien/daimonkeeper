// kfx_platform: draw_gpoly(), the software rasteriser every terrain triangle goes through, with texture
// blocks larger than 32x32 (docs/newFeatures/texture-resolution/02-design.md section 4.1, 03 section C):
// - a golden digest of what it draws from 32x32 blocks (vec_map_shift 0), so later work can't change it;
// - parity mode: a nearest-neighbour x2 / x4 upscale of a block, drawn with vec_map_shift 1 / 2, gives the same
//   pixels as the 32x32 original, for every triangle, including long thin ones (where scaling truncated vertex
//   UVs instead of the steps would move texel edges) and UVs scrolled far past the block (the wrap);
// - the wrap never reads past the block: the rest of the 256-texel-wide atlas row is poison;
// - precision mode (the HD path) shows only the block's own texels and differs from parity mode only where it
//   keeps more of a UV's fraction.
//
// To print the golden digest (only when a commit is meant to change what gpoly draws):
//     KFX_PLATFORM_GOLDEN_PRINT=1 kfx_platform_utest "[gpoly]"
#include <catch2/catch_test_macros.hpp>

#include "bflib_render.h"
#include "bflib_vidraw.h"
#include "bflib_video.h"
#include "renderer/RendererManager.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

const int64_t kW = 160;      // window width = screen pitch
const int64_t kH = 120;      // window height
const int64_t kRows = kH + 2; // the span loop may write one row below
const int64_t S = 1 << 16;    // one 32-res texel in 16.16
const unsigned char kPoison = 255;

struct Target {
    std::vector<TbPixel> pixels;
    unsigned char palette[768];
    unsigned char *saved_palette;
    Target() : pixels((size_t)(kW * kRows)) {
        for (int i = 0; i < 256; i++) {
            palette[3 * i + 0] = (unsigned char)((i * 7) & 63);
            palette[3 * i + 1] = (unsigned char)((i * 13 + 5) & 63);
            palette[3 * i + 2] = (unsigned char)((i * 29 + 11) & 63);
        }
        palette[3 * kPoison + 0] = 63; palette[3 * kPoison + 1] = 0; palette[3 * kPoison + 2] = 63; // magenta
        saved_palette = lbDisplay.Palette;
        lbDisplay.Palette = palette; // what RendererGetActivePalette() hands gpoly
        vec_screen = pixels.data(); // draw_gpoly() writes here
        vec_screen_width = kW;
        vec_window_width = kW;
        vec_window_height = kH;
        vec_mode = VM_QuadTextured;
    }
    ~Target() { lbDisplay.Palette = saved_palette; }
    void clear() {
        for (size_t i = 0; i < pixels.size(); i++)
            pixels[i] = TbPixel_RGBA(7, 9, 11, 255);
    }
    uint64_t hash() const {
        uint64_t h = 1469598103934665603ULL;
        const unsigned char *b = reinterpret_cast<const unsigned char *>(pixels.data());
        for (size_t i = 0; i < pixels.size() * sizeof(TbPixel); i++)
            h = (h ^ b[i]) * 1099511628211ULL;
        return h;
    }
};

/** A 256-texel-wide atlas page holding one block of 32 << shift texels at its left, poison elsewhere: the
 *  32x32 original's texels (indices 1..250) repeated 2^shift times each way. */
std::vector<unsigned char> page(int shift)
{
    const int side = 32 << shift;
    std::vector<unsigned char> p((size_t)256 * side, kPoison);
    for (int y = 0; y < side; y++)
        for (int x = 0; x < side; x++) {
            const int ox = x >> shift, oy = y >> shift;
            p[(size_t)y * 256 + x] = (unsigned char)(1 + ((ox * 37 + oy * 101 + ox * oy * 7) % 250));
        }
    return p;
}

struct Tri {
    int64_t x[3], y[3], u[3], v[3], s[3];
};

std::vector<Tri> corpus()
{
    std::vector<Tri> c;
    const int64_t shade = (int64_t)40 << 16;
    // a whole block, both halves of a quad, as terrain draws them (0 .. just under 32 texels)
    const int64_t e = 0x1FFFFF;
    c.push_back({{10, 90, 90}, {10, 10, 90}, {0, e, e}, {0, 0, e}, {shade, shade, shade}});
    c.push_back({{90, 10, 10}, {90, 90, 10}, {e, 0, 0}, {e, e, 0}, {shade, shade, shade}});
    // rough terrain's half-texel inset
    c.push_back({{20, 140, 140}, {5, 5, 110}, {0x8000, e - 0x8000, e - 0x8000}, {0x8000, 0x8000, e - 0x8000}, {shade, 20 << 16, 50 << 16}});
    // long, thin triangles: many pixels per step, where a rounding difference in the steps would show
    c.push_back({{0, 159, 3}, {2, 9, 115}, {0, 31 * S, 2 * S}, {0, 1 * S, 31 * S}, {shade, shade, shade}});
    c.push_back({{1, 158, 157}, {0, 2, 119}, {3 * S + 12345, 29 * S, 31 * S - 7}, {0, 31 * S, 15 * S}, {shade, shade, shade}});
    // scrolled far past the block (liquid scroll reaches ~160 texels): the wrap
    c.push_back({{5, 150, 70}, {5, 20, 118}, {96 * S, 160 * S, 130 * S}, {64 * S, 70 * S, 150 * S}, {shade, shade, shade}});
    c.push_back({{150, 5, 70}, {115, 100, 2}, {200 * S + 777, 140 * S, 170 * S}, {33 * S, 95 * S, 40 * S}, {shade, shade, shade}});
    // magnified: few texels over many pixels, and minified: many texels per pixel
    c.push_back({{0, 159, 0}, {0, 0, 119}, {4 * S, 6 * S, 4 * S}, {4 * S, 4 * S, 6 * S}, {shade, shade, shade}});
    c.push_back({{40, 60, 45}, {40, 42, 60}, {0, 31 * S, 5 * S}, {0, 3 * S, 31 * S}, {shade, shade, shade}});
    // clipped at the window's edges
    c.push_back({{-40, 200, 80}, {-30, 20, 160}, {0, 31 * S, 15 * S}, {0, 10 * S, 31 * S}, {shade, 10 << 16, 60 << 16}});
    // random ones, from inside the window to far off it
    uint32_t r = 2024;
    auto rnd = [&r](int64_t n) { r = r * 1103515245u + 12345u; return (int64_t)((r >> 8) % (uint32_t)n); };
    for (int k = 0; k < 300; k++) {
        Tri t;
        for (int i = 0; i < 3; i++) {
            t.x[i] = rnd(260) - 50;
            t.y[i] = rnd(200) - 40;
            t.u[i] = rnd(64 * S);
            t.v[i] = rnd(64 * S);
            t.s[i] = rnd(64) << 16;
        }
        c.push_back(t);
    }
    return c;
}

void draw(Target &t, const Tri &tri, unsigned char *map, int shift, bool parity)
{
    t.clear();
    struct PolyPoint p[3];
    std::memset(p, 0, sizeof(p));
    for (int k = 0; k < 3; k++) {
        p[k].X = tri.x[k]; p[k].Y = tri.y[k];
        p[k].U = tri.u[k]; p[k].V = tri.v[k];
        p[k].S = tri.s[k];
    }
    vec_map = map;
    vec_map_shift = shift;
    vec_map_parity = parity ? 1 : 0;
    draw_gpoly(&p[0], &p[1], &p[2]);
    vec_map_shift = 0;
    vec_map_parity = 0;
}

/** 32-res texels per screen pixel along the direction a triangle is minified most: the largest singular value
 *  of its screen-to-texture map. */
double max_texels_per_pixel(const Tri &t)
{
    const double s00 = (double)(t.x[1] - t.x[0]), s10 = (double)(t.y[1] - t.y[0]);
    const double s01 = (double)(t.x[2] - t.x[0]), s11 = (double)(t.y[2] - t.y[0]);
    const double t00 = (double)(t.u[1] - t.u[0]) / S, t10 = (double)(t.v[1] - t.v[0]) / S;
    const double t01 = (double)(t.u[2] - t.u[0]) / S, t11 = (double)(t.v[2] - t.v[0]) / S;
    const double det = s00 * s11 - s01 * s10;
    if (std::fabs(det) < 1.0)
        return 1e9;
    const double j00 = (t00 * s11 - t01 * s10) / det, j01 = (t01 * s00 - t00 * s01) / det;
    const double j10 = (t10 * s11 - t11 * s10) / det, j11 = (t11 * s00 - t10 * s01) / det;
    const double q = j00 * j00 + j01 * j01 + j10 * j10 + j11 * j11, dj = j00 * j11 - j01 * j10;
    return std::sqrt((q + std::sqrt(std::fmax(q * q - 4.0 * dj * dj, 0.0))) * 0.5);
}

bool has_poison(const Target &t, const unsigned char *palette)
{
    const TbPixel magenta = TbPixel_RGB(chan6_to_8(palette[3 * kPoison]), chan6_to_8(palette[3 * kPoison + 1]),
        chan6_to_8(palette[3 * kPoison + 2]));
    for (const TbPixel &px : t.pixels)
        if ((px.r == magenta.r) && (px.g == magenta.g) && (px.b == magenta.b))
            return true;
    return false;
}

} // namespace

TEST_CASE("draw_gpoly: 32x32 blocks draw as before (golden)", "[gpoly]")
{
    Target t;
    auto p0 = page(0);
    const auto c = corpus();
    uint64_t digest = 1469598103934665603ULL;
    for (const Tri &tri : c) {
        draw(t, tri, p0.data(), 0, false);
        digest = (digest ^ t.hash()) * 1099511628211ULL;
    }
    if (std::getenv("KFX_PLATFORM_GOLDEN_PRINT") != nullptr)
        std::printf("GOLDEN: gpoly 32x32 digest %016llx\n", (unsigned long long)digest);
    CHECK(digest == 0x91bba80e489d7f78ULL);
}

TEST_CASE("draw_gpoly: parity mode draws a nearest-neighbour upscale exactly as its 32x32 original", "[gpoly]")
{
    Target t;
    auto p0 = page(0), p1 = page(1), p2 = page(2);
    const auto c = corpus();
    for (size_t k = 0; k < c.size(); k++) {
        draw(t, c[k], p0.data(), 0, false);
        const uint64_t want = t.hash();
        draw(t, c[k], p1.data(), 1, true);
        const uint64_t x2 = t.hash();
        draw(t, c[k], p2.data(), 2, true);
        const uint64_t x4 = t.hash();
        INFO("triangle " << k);
        CHECK(x2 == want);
        CHECK(x4 == want);
    }
}

TEST_CASE("draw_gpoly: the wrap stays inside the block, at every size and in both modes", "[gpoly]")
{
    Target t;
    std::vector<unsigned char> pages[3] = { page(0), page(1), page(2) };
    const auto c = corpus();
    for (size_t k = 0; k < c.size(); k++) {
        for (int shift = 0; shift <= 2; shift++) {
            for (bool parity : { false, true }) {
                draw(t, c[k], pages[shift].data(), shift, parity);
                INFO("triangle " << k << " shift " << shift << " parity " << parity);
                CHECK_FALSE(has_poison(t, t.palette));
            }
        }
    }
}

TEST_CASE("draw_gpoly: a mip level below 32 (negative shift) wraps inside its block too", "[gpoly]")
{
    // texture resolution Phase 4: 16x16 down to 1x1 blocks, at the left of a poisoned 256-wide page.
    Target t;
    const auto c = corpus();
    for (int shift = -1; shift >= -5; shift--) {
        const int side = 32 >> -shift;
        std::vector<unsigned char> p((size_t)256 * side, kPoison);
        for (int y = 0; y < side; y++)
            for (int x = 0; x < side; x++)
                p[(size_t)y * 256 + x] = (unsigned char)(1 + ((x * 37 + y * 101) % 250));
        for (size_t k = 0; k < c.size(); k++) {
            draw(t, c[k], p.data(), shift, false);
            INFO("triangle " << k << " shift " << shift);
            CHECK_FALSE(has_poison(t, t.palette));
        }
    }
}

TEST_CASE("draw_gpoly: precision mode differs from parity mode only by texels' neighbours", "[gpoly]")
{
    // Precision mode keeps the vertex UVs' fractions that parity mode (like the 32-res path) truncates, so with
    // fractional UVs the two can be up to a whole 32-res texel apart: that is the precision gained. With vertex UVs
    // on whole 32-res texels both start from the same UVs and only the steps' rounding differs, so a pixel can
    // move at most to the next texel: every pixel precision mode draws differently must show a texel some pixel
    // next to it shows in parity mode. That holds where a 128-res texel covers at least a pixel in every
    // direction (minified, the steps pick texels no neighbour shows); pixels on the triangle's edge are left out
    // (a neighbour there is background). The triangles are drawn at one shade, so a pixel's colour names its
    // texel.
    Target t;
    auto p2 = page(2);
    const auto c = corpus();
    size_t differing = 0, total = 0, checked = 0;
    for (size_t k = 0; k < c.size(); k++) {
        if (max_texels_per_pixel(c[k]) > 0.25) // a 128-res texel narrower than a pixel in some direction
            continue;
        checked++;
        Tri whole = c[k];
        for (int i = 0; i < 3; i++) {
            whole.u[i] &= ~(S - 1);
            whole.v[i] &= ~(S - 1);
            whole.s[i] = (int64_t)40 << 16; // one shade: a pixel's colour is its texel's
        }
        draw(t, whole, p2.data(), 2, true);
        const std::vector<TbPixel> parity = t.pixels;
        draw(t, whole, p2.data(), 2, false);
        for (int64_t y = 0; y < kH; y++) {
            for (int64_t x = 0; x < kW; x++) {
                const TbPixel a = t.pixels[(size_t)(y * kW + x)];
                const TbPixel b = parity[(size_t)(y * kW + x)];
                if ((a.r != 7) || (a.g != 9) || (a.b != 11))
                    total++;
                if ((a.r == b.r) && (a.g == b.g) && (a.b == b.b))
                    continue;
                bool edge = false;
                for (int64_t dy = -1; dy <= 1; dy++)
                    for (int64_t dx = -1; dx <= 1; dx++) {
                        const int64_t nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= kW || ny >= kH)
                            continue;
                        const TbPixel n = parity[(size_t)(ny * kW + nx)];
                        edge |= (n.r == 7) && (n.g == 9) && (n.b == 11);
                    }
                if (edge)
                    continue;
                differing++;
                bool near = false;
                for (int64_t dy = -1; dy <= 1 && !near; dy++)
                    for (int64_t dx = -1; dx <= 1 && !near; dx++) {
                        const int64_t nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= kW || ny >= kH)
                            continue;
                        const TbPixel n = parity[(size_t)(ny * kW + nx)];
                        near = (n.r == a.r) && (n.g == a.g) && (n.b == a.b);
                    }
                INFO("triangle " << k << " at " << x << "," << y);
                CHECK(near);
            }
        }
    }
    WARN("precision mode: " << checked << " magnified triangles, " << differing << " of " << total
        << " drawn pixels differ from parity mode");
    CHECK(checked >= 5);
    CHECK(differing < total / 4);
}

namespace {
/** An indexed page expanded to TbPixels through `palette` (index 0: (0,0,0,0), as the indexed path draws it). */
std::vector<TbPixel> rgba_page(const std::vector<unsigned char> &page, const unsigned char *palette)
{
    std::vector<TbPixel> out(page.size());
    for (size_t i = 0; i < page.size(); i++)
        out[i] = expand_indexed_pixel(page[i], palette);
    return out;
}
}

TEST_CASE("draw_gpoly: a true-colour block draws as its indexed original, sized and under a palette effect", "[gpoly]")
{
    // texture resolution 03 section C: vec_map_rgba with the original's colours, at its own size and as a
    // nearest-neighbour x4 upscale in parity mode, draws the indexed original's pixels; with the active palette
    // a fade of the reference one, the RGBA path's colour transform gives what expanding through the fade gives.
    Target t;
    auto p0 = page(0), p2 = page(2);
    const auto c = corpus();
    unsigned char reference[768];
    std::memcpy(reference, t.palette, sizeof(reference));
    const std::vector<TbPixel> rgba0 = rgba_page(p0, reference), rgba2 = rgba_page(p2, reference);
    unsigned char faded[768];
    for (int i = 0; i < 768; i++)
        faded[i] = (unsigned char)((reference[i] * 3) / 5); // per channel: curves
    for (int effect = 0; effect < 2; effect++) {
        RendererSetHdReferencePalette(reference);
        std::memcpy(t.palette, effect ? faded : reference, sizeof(t.palette)); // the active palette
        REQUIRE(RendererHdUsable());
        CHECK((RendererHdColourTransform() != nullptr) == (effect != 0));
        for (size_t k = 0; k < c.size(); k++) {
            draw(t, c[k], p0.data(), 0, false);
            const uint64_t want = t.hash();
            vec_map_rgba = rgba0.data();
            draw(t, c[k], p0.data(), 0, false);
            const uint64_t got0 = t.hash();
            vec_map_rgba = rgba2.data();
            draw(t, c[k], p2.data(), 2, true);
            const uint64_t got2 = t.hash();
            vec_map_rgba = nullptr;
            INFO("triangle " << k << " effect " << effect);
            CHECK(got0 == want);
            CHECK(got2 == want);
        }
    }
    RendererSetHdReferencePalette(nullptr);
    std::memcpy(t.palette, reference, sizeof(t.palette));
}
