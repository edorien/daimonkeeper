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
#include "sprite_blit_harness.h"

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
