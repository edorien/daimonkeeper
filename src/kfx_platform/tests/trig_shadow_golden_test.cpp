// kfx_platform: golden hashes of the software triangle rasteriser's creature shadow path (refactor pass 5, S10):
// trig() with VM_SpriteTranslucent, the one render mode the game draws with it -- every creature shadow is two of
// these triangles over whatever is already on screen, shaded by vec_shade where the shadow sprite (vec_map) is set.
//
// Each case draws one triangle into a small target with a non-uniform background and hashes the whole target
// (including the row past the window: the span loop writes one row below each scanline); the cases cover all four
// ways trig() orders a triangle (flat bottom, flat top, long edge left or right), clipping at every edge, degenerate
// and too-large triangles, a range of shades, and a few hundred random triangles from far off screen to inside.
// The digest of all the cases must not change when the rasteriser is rewritten.
//
// To print the digests (only when a commit is meant to change what shadows draw):
//     KFX_PLATFORM_GOLDEN_PRINT=1 kfx_platform_utest "[trig_golden]"
#include <catch2/catch_test_macros.hpp>

#include "bflib_render.h"
#include "bflib_vidraw.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

namespace {

const int64_t kW = 96;      // window width = screen pitch
const int64_t kH = 64;      // window height
const int64_t kRows = kH + 2;

struct Target {
    std::vector<TbPixel> pixels;
    std::vector<unsigned char> sprite;
    Target() : pixels((size_t)(kW * kRows)), sprite(256 * 256) {
        uint32_t r = 12345;
        for (size_t i = 0; i < sprite.size(); i++) {
            r = r * 1103515245u + 12345u;
            // a blobby shadow: set inside a disc, sparse noise outside
            const int64_t x = (int64_t)(i & 255) - 128, y = (int64_t)(i >> 8) - 128;
            sprite[i] = (x * x + y * y < 90 * 90) ? 1 : (unsigned char)(((r >> 16) & 7) == 0);
        }
        poly_screen = pixels.data();
        vec_screen_width = kW;
        vec_window_width = kW;
        vec_window_height = kH;
        vec_map = sprite.data();
        vec_mode = VM_SpriteTranslucent;
    }
    void clear() {
        for (int64_t y = 0; y < kRows; y++)
            for (int64_t x = 0; x < kW; x++)
                pixels[(size_t)(y * kW + x)] = TbPixel_RGBA((uint8_t)(x * 3 + 7), (uint8_t)(y * 4 + 11),
                    (uint8_t)((x ^ y) * 5), 255);
    }
    uint64_t hash() const {
        uint64_t h = 1469598103934665603ULL;
        const unsigned char *b = reinterpret_cast<const unsigned char *>(pixels.data());
        for (size_t i = 0; i < pixels.size() * sizeof(TbPixel); i++)
            h = (h ^ b[i]) * 1099511628211ULL;
        return h;
    }
};

struct Tri {
    int64_t x[3], y[3], u[3], v[3];
    int64_t shade;
};

uint64_t draw_and_hash(Target &t, const Tri &tri)
{
    t.clear();
    struct PolyPoint p[3];
    std::memset(p, 0, sizeof(p));
    for (int k = 0; k < 3; k++) {
        p[k].X = tri.x[k];
        p[k].Y = tri.y[k];
        p[k].U = tri.u[k];
        p[k].V = tri.v[k];
    }
    vec_shade = tri.shade;
    trig(&p[0], &p[1], &p[2]);
    return t.hash();
}

const int64_t S = 1 << 16; // a texel in 16.16
const size_t kMadeCases = 60;  // the cases before the random ones
const uint64_t kMadeDrawingNothing = 6; // the degenerate and too-large ones, and the one below and right of the window

/** The other winding: trig() draws only triangles facing one way. */
Tri flipped(Tri t)
{
    std::swap(t.x[1], t.x[2]); std::swap(t.y[1], t.y[2]); std::swap(t.u[1], t.u[2]); std::swap(t.v[1], t.v[2]);
    return t;
}

std::vector<Tri> cases()
{
    std::vector<Tri> c;
    // the four orderings, inside the window, at a few shades
    for (int64_t shade : {0, 7, 20, 31, 33, 45, 63}) { // (32 shades by exactly 1: no visible shadow)
        c.push_back({{10, 60, 30}, {5, 20, 50}, {0, 200 * S, 60 * S}, {0, 30 * S, 220 * S}, shade});   // long edge left
        c.push_back({{60, 10, 40}, {5, 25, 55}, {200 * S, 0, 100 * S}, {10 * S, 40 * S, 250 * S}, shade}); // long edge right
        c.push_back({{10, 70, 40}, {10, 10, 50}, {0, 255 * S, 128 * S}, {0, 0, 255 * S}, shade});     // flat top
        c.push_back({{40, 80, 10}, {8, 50, 50}, {128 * S, 255 * S, 0}, {0, 255 * S, 255 * S}, shade}); // flat bottom
    }
    // every point order of one triangle (the reordering)
    {
        const int64_t xs[3] = {12, 70, 33}, ys[3] = {6, 21, 58};
        const int perm[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
        for (const auto &pm : perm) {
            Tri t{};
            for (int k = 0; k < 3; k++) {
                t.x[k] = xs[pm[k]]; t.y[k] = ys[pm[k]];
                t.u[k] = (int64_t)pm[k] * 90 * S; t.v[k] = (int64_t)(2 - pm[k]) * 70 * S;
            }
            t.shade = 24;
            c.push_back(t);
        }
    }
    // clipped at each edge, and at several at once
    c.push_back({{-30, 40, 20}, {10, 20, 50}, {0, 100 * S, 50 * S}, {0, 20 * S, 200 * S}, 16});   // left
    c.push_back({{60, 140, 90}, {10, 20, 50}, {0, 100 * S, 50 * S}, {0, 20 * S, 200 * S}, 16});   // right
    c.push_back({{20, 70, 40}, {-25, 10, 40}, {0, 100 * S, 50 * S}, {0, 20 * S, 200 * S}, 16});   // top
    c.push_back({{20, 70, 40}, {30, 50, 110}, {0, 100 * S, 50 * S}, {0, 20 * S, 200 * S}, 16});   // bottom
    c.push_back({{-40, 150, 50}, {-30, -10, 120}, {0, 255 * S, 128 * S}, {0, 0, 255 * S}, 40});   // all four
    c.push_back({{-40, 150, 50}, {-30, 100, 120}, {0, 255 * S, 128 * S}, {0, 0, 255 * S}, 40});
    c.push_back({{20, 70, 40}, {-20, -20, 90}, {0, 255 * S, 128 * S}, {0, 0, 255 * S}, 10});       // flat top, clipped
    c.push_back({{40, 90, -10}, {-15, 80, 80}, {128 * S, 255 * S, 0}, {0, 255 * S, 255 * S}, 50}); // flat bottom, clipped
    // degenerate: a point, a line, two points the same, and outside the 16.16 range
    c.push_back({{20, 20, 20}, {20, 20, 20}, {0, 0, 0}, {0, 0, 0}, 20});
    c.push_back({{10, 30, 50}, {10, 30, 50}, {0, 0, 0}, {0, 0, 0}, 20});
    c.push_back({{10, 10, 50}, {10, 10, 40}, {0, 0, 0}, {0, 0, 0}, 20});
    // (sampling the sprite's middle: drawn, they would show)
    c.push_back({{10, 40000, 50}, {10, 20, 40}, {100 * S, 150 * S, 128 * S}, {100 * S, 128 * S, 150 * S}, 20});
    c.push_back({{10, 30, 50}, {-40000, 20, 40}, {100 * S, 150 * S, 128 * S}, {100 * S, 128 * S, 150 * S}, 20});
    // below and right of the window
    c.push_back({{10, 30, 50}, {70, 80, 90}, {100 * S, 150 * S, 128 * S}, {100 * S, 128 * S, 150 * S}, 20});
    // a creature shadow as the game draws it: a quad of two triangles (first, second, third) and (first, third, fourth)
    for (int64_t k = 0; k < 6; k++) {
        const int64_t x0 = -10 + k * 18, y0 = -6 + k * 11;
        const int64_t sz = 20 + k * 4;
        c.push_back({{x0, x0 + sz, x0 + sz}, {y0, y0, y0 + sz / 2}, {0, 255 * S, 255 * S}, {0, 0, 255 * S}, 18 + k});
        c.push_back({{x0, x0 + sz, x0}, {y0, y0 + sz / 2, y0 + sz / 2}, {0, 255 * S, 0}, {0, 255 * S, 255 * S}, 18 + k});
    }
    // random triangles, from far off screen to inside, any UVs and shades
    uint64_t r = 0x9e3779b97f4a7c15ULL;
    auto rnd = [&r](int64_t lo, int64_t hi) {
        r ^= r << 13; r ^= r >> 7; r ^= r << 17;
        return lo + (int64_t)(r % (uint64_t)(hi - lo + 1));
    };
    for (int i = 0; i < 400; i++) {
        Tri t{};
        const int64_t spread = (i < 200) ? 30 : 400;
        for (int k = 0; k < 3; k++) {
            t.x[k] = rnd(-spread, kW + spread);
            t.y[k] = rnd(-spread, kH + spread);
            t.u[k] = rnd(-300 * S, 600 * S);
            t.v[k] = rnd(-300 * S, 600 * S);
        }
        t.shade = rnd(0, 63);
        c.push_back(t);
    }
    return c;
}

} // namespace

TEST_CASE("golden: creature shadow triangles (trig, VM_SpriteTranslucent)", "[kfx_platform][trig_golden]")
{
    Target target;
    const std::vector<Tri> all = cases();
    uint64_t digest = 1469598103934665603ULL;
    uint64_t unchanged = 0, unchanged_made = 0;
    target.clear();
    const uint64_t blank = target.hash();
    for (size_t i = 0; i < all.size(); i++) {
        TbBool drew = false;
        for (const Tri &t : {all[i], flipped(all[i])}) {
            const uint64_t h = draw_and_hash(target, t);
            unchanged += (h == blank);
            drew |= (h != blank);
            for (int b = 0; b < 8; b++)
                digest = (digest ^ ((h >> (8 * b)) & 0xFF)) * 1099511628211ULL;
        }
        unchanged_made += !drew && (i < kMadeCases);
    }
    if (std::getenv("KFX_PLATFORM_GOLDEN_PRINT") != nullptr) {
        std::printf("trig shadow golden: %zu triangles, %llu draw nothing (%llu made cases in neither winding), digest 0x%016llxULL\n",
            2 * all.size(), (unsigned long long)unchanged, (unsigned long long)unchanged_made, (unsigned long long)digest);
        return;
    }
    INFO(2 * all.size() << " triangles, " << unchanged << " draw nothing; made cases drawing in neither winding: "
        << unchanged_made);
    CHECK(unchanged_made == kMadeDrawingNothing);
    CHECK(unchanged < all.size() * 3 / 2); // three quarters of the triangles: the random ones far off screen draw nothing
    CHECK(digest == 0x753c10923b086f98ULL);
}

TEST_CASE("Soft shadows: a coverage map at 2-4x that is the mask upscaled shades exactly the mask's pixels",
    "[kfx_platform][trig_golden]")
{
    // The coverage sampler (vec_map_scale, vec_map_coverage; docs/refactor/sprites/ Phase 10) picks the original texel
    // u, v name (wrapping at 256 as the mask does), then the sub-texel: an upscaled mask, full where set, must shade
    // the same pixels the same way, for every case of the golden set (random UVs far off the mask included).
    Target target;
    const std::vector<Tri> all = cases();
    for (int64_t k : {2, 3, 4})
    {
        std::vector<unsigned char> cover((size_t)(256 * k * 256 * k));
        for (int64_t y = 0; y < 256 * k; y++)
            for (int64_t x = 0; x < 256 * k; x++)
                cover[(size_t)(y * 256 * k + x)] = target.sprite[(size_t)((y / k) * 256 + x / k)] ? 255 : 0;
        size_t same = 0;
        for (const Tri &t : all)
        {
            vec_map = target.sprite.data();
            vec_map_scale = 1;
            vec_map_coverage = false;
            const uint64_t want = draw_and_hash(target, t);
            vec_map = cover.data();
            vec_map_scale = k;
            vec_map_coverage = true;
            same += (draw_and_hash(target, t) == want);
        }
        vec_map = target.sprite.data();
        vec_map_scale = 1;
        vec_map_coverage = false;
        INFO("x" << k);
        CHECK(same == all.size());
    }
}

TEST_CASE("Soft shadows: partial coverage shades part of the way", "[kfx_platform][trig_golden]")
{
    Target target;
    const int64_t k = 2;
    std::vector<unsigned char> cover((size_t)(256 * k * 256 * k), 128);
    const Tri t = {{10, 70, 40}, {10, 10, 50}, {0, 255 * S, 128 * S}, {0, 0, 255 * S}, 8};
    target.clear();
    const std::vector<TbPixel> before = target.pixels;
    vec_map = cover.data();
    vec_map_scale = k;
    vec_map_coverage = true;
    draw_and_hash(target, t);
    vec_map = target.sprite.data();
    vec_map_scale = 1;
    vec_map_coverage = false;
    int64_t changed = 0, between = 0;
    for (size_t i = 0; i < before.size(); i++)
    {
        const TbPixel a = before[i], b = target.pixels[i];
        if ((a.r == b.r) && (a.g == b.g) && (a.b == b.b))
            continue;
        changed++;
        const TbPixel full = render_shade(a, 8);
        auto mid = [](int x, int lo, int hi) { return (lo <= hi) ? (x >= lo && x <= hi) : (x >= hi && x <= lo); };
        between += mid(b.r, a.r, full.r) && mid(b.g, a.g, full.g) && mid(b.b, a.b, full.b) &&
            ((b.r != full.r) || (b.g != full.g) || (b.b != full.b) || ((a.r == full.r) && (a.g == full.g) && (a.b == full.b)));
    }
    CHECK(changed > 500);
    CHECK(between == changed);
}
