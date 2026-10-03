// kfx_platform: golden hashes of the software renderer's scaled-sprite blitters (refactor pass 3,
// S08: the 36 inner functions of bflib_vidraw_spr_norm/onec/remp.c become one shared body; these
// hashes must not change when they do).
//
// Five hand-made RLE sprites are drawn through each dispatcher (normal, alpha, remap, one colour),
// with each blend (solid, TRANSPAR4, TRANSPAR8, remap) and flip (none, horizontal, vertical, both),
// at eight scales (down, up, mixed) and five positions (inside, and clipped at each edge) into a
// buffer bigger than the graphics window, pre-filled with a pattern (a third of it opaque), each once as a frame of its own
// size and once at an offset inside a larger frame. One hash per dispatcher, blend, flip, sprite and
// scale covers the ten draws, and the whole buffer (writes outside the window would show).
//
// To print the hashes (only on a commit meant to change what the blitters draw):
//     KFX_PLATFORM_GOLDEN_PRINT=1 kfx_platform_utest "[sprite_golden]"
// A timing loop for comparing speed before and after: kfx_platform_utest "[.sprite_bench]".
#include <catch2/catch_test_macros.hpp>

#include "bflib_video.h"
#include "bflib_vidraw.h"
#include "bflib_sprite.h"
#include "renderer/RendererManager.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

extern "C" TbResult LbSpriteDrawUsingScalingData(int64_t posx, int64_t posy, const struct TbSourceBuffer *src_buf);
extern "C" TbResult DrawAlphaSpriteUsingScalingData(int64_t posx, int64_t posy, const struct TbSourceBuffer *src_buf);
extern "C" TbResult LbSpriteDrawRemapUsingScalingData(int64_t posx, int64_t posy, const struct TbSourceBuffer *src_buf, const TbPixel *cmap);
extern "C" TbResult LbSpriteDrawOneColourUsingScalingData(int64_t posx, int64_t posy, const struct TbSprite *sprite, TbPixel colour);

namespace {

const int kScreenW = 160, kScreenH = 120;
const int kWinX = 8, kWinY = 6, kWinW = 144, kWinH = 108;

struct Fnv {
    uint64_t v = 1469598103934665603ULL;
    void add(const void *p, size_t n) {
        const unsigned char *b = static_cast<const unsigned char *>(p);
        for (size_t i = 0; i < n; i++) { v ^= b[i]; v *= 1099511628211ULL; }
    }
};

/** RLE as the blitters read it: per row, n>0 then n pixel bytes, n<0 skips -n, 0 ends the row. */
std::vector<unsigned char> encode(const std::vector<std::vector<int>> &rows) {
    std::vector<unsigned char> out;
    for (const auto &row : rows) {
        size_t i = 0;
        while (i < row.size()) {
            size_t j = i;
            if (row[i] == 0) {
                while (j < row.size() && row[j] == 0 && j - i < 127) j++;
                if (j < row.size()) out.push_back((unsigned char)(signed char)-(int)(j - i));
            } else {
                while (j < row.size() && row[j] != 0 && j - i < 127) j++;
                out.push_back((unsigned char)(j - i));
                for (size_t k = i; k < j; k++) out.push_back((unsigned char)row[k]);
            }
            i = j;
        }
        out.push_back(0);
    }
    return out;
}

struct TestSprite {
    std::string name;
    int w, h;
    std::vector<unsigned char> rle;
};

std::vector<TestSprite> make_sprites() {
    std::vector<TestSprite> s;
    auto add = [&](const std::string &name, const std::vector<std::vector<int>> &rows) {
        s.push_back({name, (int)rows[0].size(), (int)rows.size(), encode(rows)});
    };
    {   // mixed runs and gaps, 16x12
        std::vector<std::vector<int>> r(12, std::vector<int>(16, 0));
        for (int y = 0; y < 12; y++)
            for (int x = 0; x < 16; x++)
                r[y][x] = ((x + y) % 5 == 0) ? 0 : 1 + ((x * 7 + y * 13) % 254);
        add("mixed", r);
    }
    {   // fully solid 30x20
        std::vector<std::vector<int>> r(20, std::vector<int>(30, 0));
        for (int y = 0; y < 20; y++)
            for (int x = 0; x < 30; x++)
                r[y][x] = 1 + ((x * 3 + y * 5) % 250);
        add("solid", r);
    }
    {   // tall thin 3x40 with a gap column
        std::vector<std::vector<int>> r(40, std::vector<int>(3, 0));
        for (int y = 0; y < 40; y++) { r[y][0] = 10 + y; r[y][2] = 200 - y; }
        add("tall", r);
    }
    {   // single pixel
        add("dot", {{77}});
    }
    {   // leading/trailing transparency and an empty row, 9x6
        add("gaps", {{0, 0, 3, 4, 5, 0, 0, 0, 9},
                     {0, 0, 0, 0, 0, 0, 0, 0, 0},
                     {1, 0, 1, 0, 1, 0, 1, 0, 1},
                     {0, 250, 251, 252, 253, 254, 255, 0, 0},
                     {0, 0, 0, 0, 7, 0, 0, 0, 0},
                     {2, 2, 2, 2, 2, 2, 2, 2, 2}});
    }
    return s;
}

struct Harness {
    std::vector<TbPixel> screen = std::vector<TbPixel>((size_t)kScreenW * kScreenH);
    unsigned char palette[768];
    TbPixel remap[256];
    TbPixel *saved_wscreen, *saved_window;
    int64_t saved[6];
    unsigned char *saved_palette;
    Harness() {
        for (int i = 0; i < 768; i++) palette[i] = (unsigned char)((i * 37 + 11) % 64);
        for (int i = 0; i < 256; i++) remap[i] = TbPixel_RGBA((uint8_t)(255 - i), (uint8_t)(i * 3), (uint8_t)(i ^ 0x5a), (uint8_t)(i | 0x80));
        saved_wscreen = lbDisplay.WScreen;
        saved_window = lbDisplay.GraphicsWindowPtr;
        saved[0] = lbDisplay.GraphicsScreenWidth; saved[1] = lbDisplay.GraphicsScreenHeight;
        saved[2] = lbDisplay.GraphicsWindowX; saved[3] = lbDisplay.GraphicsWindowY;
        saved[4] = lbDisplay.GraphicsWindowWidth; saved[5] = lbDisplay.GraphicsWindowHeight;
        saved_palette = lbDisplay.Palette;
        lbDisplay.WScreen = screen.data();
        lbDisplay.GraphicsWindowPtr = screen.data() + kWinY * kScreenW + kWinX;
        lbDisplay.GraphicsScreenWidth = kScreenW; lbDisplay.GraphicsScreenHeight = kScreenH;
        lbDisplay.GraphicsWindowX = kWinX; lbDisplay.GraphicsWindowY = kWinY;
        lbDisplay.GraphicsWindowWidth = kWinW; lbDisplay.GraphicsWindowHeight = kWinH;
        lbDisplay.Palette = palette;
    }
    ~Harness() {
        lbDisplay.WScreen = saved_wscreen;
        lbDisplay.GraphicsWindowPtr = saved_window;
        lbDisplay.GraphicsScreenWidth = saved[0]; lbDisplay.GraphicsScreenHeight = saved[1];
        lbDisplay.GraphicsWindowX = saved[2]; lbDisplay.GraphicsWindowY = saved[3];
        lbDisplay.GraphicsWindowWidth = saved[4]; lbDisplay.GraphicsWindowHeight = saved[5];
        lbDisplay.Palette = saved_palette;
        RendererSetDrawFlags(0);
    }
    void fill() {
        for (size_t i = 0; i < screen.size(); i++)
            screen[i] = TbPixel_RGBA((uint8_t)(i * 7), (uint8_t)(i * 13 + 5), (uint8_t)(i * 3 + 100),
                (uint8_t)(i % 3 == 0 ? 255 : 200 + i % 50)); // opaque and translucent: the blends differ
    }
};

enum Dispatcher { D_Norm, D_Alpha, D_RemapDispatcher, D_OneColour };
struct Mode { const char *name; Dispatcher disp; int64_t flags; };
const Mode kModes[] = {
    {"norm-solid", D_Norm, 0},
    {"norm-trans4", D_Norm, Lb_SPRITE_TRANSPAR4},
    {"norm-trans8", D_Norm, Lb_SPRITE_TRANSPAR8},
    {"norm-remapflag", D_Norm, Lb_SPRITE_REMAP},
    {"alpha", D_Alpha, 0},
    {"remap", D_RemapDispatcher, 0},
    {"remap-trans4", D_RemapDispatcher, Lb_SPRITE_TRANSPAR4},
    {"remap-trans8", D_RemapDispatcher, Lb_SPRITE_TRANSPAR8},
    {"onecolour-solid", D_OneColour, 0},
    {"onecolour-trans4", D_OneColour, Lb_SPRITE_TRANSPAR4},
    {"onecolour-trans8", D_OneColour, Lb_SPRITE_TRANSPAR8},
};
const struct { const char *name; int64_t flags; } kFlips[] = {
    {"lr", 0}, {"rl", Lb_SPRITE_FLIP_HORIZ}, {"vflip", Lb_SPRITE_FLIP_VERTIC}, {"rl-vflip", Lb_SPRITE_FLIP_HORIZ | Lb_SPRITE_FLIP_VERTIC},
};
// Destination size as a fraction of the source: {num_w, den_w, num_h, den_h}.
const struct { const char *name; int nw, dw, nh, dh; } kScales[] = {
    {"x1", 1, 1, 1, 1}, {"down-half", 1, 2, 1, 2}, {"down-2of3", 2, 3, 2, 3}, {"up-x2", 2, 1, 2, 1},
    {"up-x3", 3, 1, 3, 1}, {"up-1.5", 3, 2, 3, 2}, {"mixed-wide", 2, 1, 1, 2}, {"mixed-tall", 1, 2, 5, 2},
};

/**
 * Draws as the game does: the scaling data places the sprite's frame (frame_w x frame_h, scaled to
 * dw x dhh) at screen x,y, and the dispatcher gets the sprite's offset inside that frame (off_x, off_y),
 * like engine_render.c's keeper sprites; the Immediate wrappers use a frame of the sprite's own size
 * and offset 0,0.
 */
void draw(Harness &h, const Mode &m, int64_t flip, const TestSprite &spr, int x, int y, int dw, int dhh,
    int off_x, int off_y) {
    RendererSetDrawFlags(m.flags | flip);
    const int frame_w = spr.w + off_x + (off_x ? 1 : 0), frame_h = spr.h + off_y + (off_y ? 1 : 0);
    LbSpriteSetScalingData(x, y, frame_w, frame_h, dw * frame_w / spr.w, dhh * frame_h / spr.h);
    x = off_x;
    y = off_y;
    const struct TbSourceBuffer src = {spr.rle.data(), (uint64_t)spr.w, (uint64_t)spr.h, 0};
    switch (m.disp) {
    case D_Norm:
        if ((m.flags & Lb_SPRITE_REMAP) != 0)
            lbSpriteReMapPtr = h.remap;
        LbSpriteDrawUsingScalingData(x, y, &src);
        break;
    case D_Alpha:
        DrawAlphaSpriteUsingScalingData(x, y, &src);
        break;
    case D_RemapDispatcher:
        LbSpriteDrawRemapUsingScalingData(x, y, &src, h.remap);
        break;
    case D_OneColour: {
        struct TbSprite sprite;
        std::memset(&sprite, 0, sizeof(sprite));
        sprite.Data = const_cast<unsigned char *>(spr.rle.data());
        sprite.SWidth = spr.w;
        sprite.SHeight = spr.h;
        LbSpriteDrawOneColourUsingScalingData(x, y, &sprite, TbPixel_RGBA(90, 180, 45, 255));
        break;
    }
    }
}

} // namespace

TEST_CASE("golden: the scaled-sprite blitters draw the same pixels", "[kfx_platform][sprite_golden]")
{
    Harness h;
    const std::map<std::string, uint64_t> expected = {
#include "sprite_blit_golden.inc"
    };
    const bool print = std::getenv("KFX_PLATFORM_GOLDEN_PRINT") != nullptr;
    const std::vector<TestSprite> sprites = make_sprites();
    int64_t checked = 0;
    for (const Mode &m : kModes)
        for (const auto &flip : kFlips)
            for (const TestSprite &spr : sprites)
                for (const auto &sc : kScales)
                {
                    const int dw = std::max(1, spr.w * sc.nw / sc.dw), dhh = std::max(1, spr.h * sc.nh / sc.dh);
                    const int positions[5][2] = {{20, 15}, {-dw / 2, 30}, {40, -dhh / 2}, {kWinW - dw / 2, 10}, {10, kWinH - dhh / 2}};
                    Fnv hash;
                    for (const auto &p : positions)
                        for (int offset = 0; offset < 2; offset++) {
                            h.fill();
                            draw(h, m, flip.flags, spr, p[0], p[1], dw, dhh, offset ? 2 : 0, offset ? 1 : 0);
                            hash.add(h.screen.data(), h.screen.size() * sizeof(TbPixel));
                        }
                    const std::string name = std::string(m.name) + "/" + flip.name + "/" + spr.name + "/" + sc.name;
                    if (print) {
                        std::printf("    {\"%s\", 0x%016llxULL},\n", name.c_str(), (unsigned long long)hash.v);
                        continue;
                    }
                    auto it = expected.find(name);
                    INFO(name);
                    REQUIRE(it != expected.end());
                    CHECK(it->second == hash.v);
                    checked++;
                }
    if (!print)
        CHECK(checked == (int64_t)expected.size());
}

TEST_CASE("the scaled-sprite blitters' speed, for comparing before and after", "[.sprite_bench]")
{
    Harness h;
    const std::vector<TestSprite> sprites = make_sprites();
    for (const Mode &m : kModes) {
        const auto start = std::chrono::steady_clock::now();
        for (int rep = 0; rep < 200; rep++)
            for (const auto &flip : kFlips)
                for (const TestSprite &spr : sprites)
                    for (const auto &sc : kScales)
                        draw(h, m, flip.flags, spr, 20, 15, std::max(1, spr.w * sc.nw / sc.dw), std::max(1, spr.h * sc.nh / sc.dh), 0, 0);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("%-18s %8.2f ms\n", m.name, ms);
    }
}
