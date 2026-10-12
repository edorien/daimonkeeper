// kfx_render: engine_hd_generate.c, generated HD art (ASSET_PACK_FILL): the original enlarged with Lanczos smoothing
// and an unsharp mask (scripts/hd_resample.py in C), and the sprite pack layer's generated entries.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "bflib_vidraw.h" // struct TbHdSpriteSource
#include "config_keeperfx.h"
#include "engine_hd_generate.h"
#include "engine_sprites_hd.h"
#include "engine_sprites_hd_pack.h"
#include "game_palettes.h"
#include "renderer/RendererManager.h"

namespace {

/** w x h texels with `pad` transparent texels around them. */
struct Art {
    int64_t w, h, pad;
    std::vector<TbPixel> px;
    Art(int64_t w_, int64_t h_, int64_t pad_) : w(w_), h(h_), pad(pad_), px((size_t)((w_ + 2 * pad_) * (h_ + 2 * pad_)), TbPixel_Transparent) {}
    int64_t stride() const { return w + 2 * pad; }
    void set(int64_t x, int64_t y, TbPixel p) { px[(size_t)((y + pad) * stride() + x + pad)] = p; }
    std::vector<TbPixel> upscale(TbBool additive = false) const
    {
        std::vector<TbPixel> out((size_t)(w * HD_GENERATE_FACTOR * h * HD_GENERATE_FACTOR));
        REQUIRE(hd_generate_upscale(px.data(), stride(), w, h, pad, HD_GENERATE_FACTOR, additive, out.data(), w * HD_GENERATE_FACTOR));
        return out;
    }
};

} // namespace

TEST_CASE("Generated art: one colour stays that colour, opaque everywhere", "[kfx_render][hd_generate]")
{
    // Opaque all around (the context): nothing to smooth or sharpen.
    Art a(8, 6, HD_GENERATE_PAD);
    for (int64_t y = -a.pad; y < a.h + a.pad; y++)
        for (int64_t x = -a.pad; x < a.w + a.pad; x++)
            a.set(x, y, TbPixel_RGBA(90, 140, 30, 255));
    for (const TbPixel &p : a.upscale())
    {
        CHECK(p.a == 255);
        CHECK(((p.r == 90) && (p.g == 140) && (p.b == 30)));
    }
}

TEST_CASE("Generated art: a cut-out's edges keep its colour (no dark fringe) and nothing lands outside it", "[kfx_render][hd_generate]")
{
    Art a(12, 12, HD_GENERATE_PAD);
    for (int64_t y = 3; y < 9; y++)
        for (int64_t x = 3; x < 9; x++)
            a.set(x, y, TbPixel_RGBA(220, 40, 40, 255));
    const std::vector<TbPixel> out = a.upscale();
    const int64_t f = HD_GENERATE_FACTOR, ow = a.w * f;
    int64_t opaque = 0;
    for (int64_t y = 0; y < a.h * f; y++)
        for (int64_t x = 0; x < ow; x++)
        {
            const TbPixel p = out[(size_t)(y * ow + x)];
            const bool inside = (x >= 3 * f) && (x < 9 * f) && (y >= 3 * f) && (y < 9 * f);
            if (!inside)
            {
                CHECK(p.a == 0);
                continue;
            }
            if (p.a == 0)
            {
                CHECK(p.r == 0); // transparent texels are (0,0,0,0)
                continue;
            }
            opaque++;
            CHECK(p.r >= 200); // the edge's colour is the square's, not blended with black
            CHECK(p.g <= 60);
        }
    // Straight edges stay where they are; only the corners round off.
    CHECK(opaque > (6 * f) * (6 * f) * 9 / 10);
    CHECK(opaque < (6 * f) * (6 * f));
}

TEST_CASE("Generated art: a single texel keeps its middle", "[kfx_render][hd_generate]")
{
    Art a(5, 5, HD_GENERATE_PAD);
    a.set(2, 2, TbPixel_RGBA(250, 250, 250, 255));
    const std::vector<TbPixel> out = a.upscale();
    const int64_t f = HD_GENERATE_FACTOR, ow = a.w * f;
    int64_t opaque = 0;
    for (int64_t y = 0; y < a.h * f; y++)
        for (int64_t x = 0; x < ow; x++)
            if (out[(size_t)(y * ow + x)].a != 0)
            {
                opaque++;
                CHECK(((x >= 2 * f) && (x < 3 * f) && (y >= 2 * f) && (y < 3 * f)));
            }
    CHECK(opaque >= (f - 1) * (f - 1));
}

TEST_CASE("Generated art: additive art adds nothing where it was transparent", "[kfx_render][hd_generate]")
{
    Art a(10, 10, HD_GENERATE_PAD);
    for (int64_t y = 4; y < 6; y++)
        for (int64_t x = 2; x < 8; x++)
            a.set(x, y, TbPixel_RGBA(120, 60, 0, 255));
    const std::vector<TbPixel> out = a.upscale(true);
    const int64_t f = HD_GENERATE_FACTOR, ow = a.w * f;
    for (int64_t y = 0; y < a.h * f; y++)
        for (int64_t x = 0; x < ow; x++)
        {
            const TbPixel p = out[(size_t)(y * ow + x)];
            if ((y < 4 * f) || (y >= 6 * f) || (x < 2 * f) || (x >= 8 * f))
                CHECK(((p.a == 0) && (p.r == 0) && (p.g == 0) && (p.b == 0)));
            else if (p.a != 0)
                CHECK(p.b <= 10); // what it adds: its own colour, smoothed (no blue appears)
        }
}

TEST_CASE("Generated art: a straight ramp comes out as the same ramp, each texel sampled at its own centre", "[kfx_render][hd_generate]")
{
    // Lanczos reproduces a straight line, and the unsharp mask leaves one alone: each HD texel X has the ramp's value
    // at (X + 0.5) / 4 - 0.5 original texels -- wherever it is in the frame (the filter weights are shared by phase).
    Art a(16, 4, HD_GENERATE_PAD);
    for (int64_t y = -a.pad; y < a.h + a.pad; y++)
        for (int64_t x = -a.pad; x < a.w + a.pad; x++)
            a.set(x, y, TbPixel_RGBA((uint8_t)(60 + 8 * x), 100, 100, 255));
    const std::vector<TbPixel> out = a.upscale();
    const int64_t f = HD_GENERATE_FACTOR, ow = a.w * f;
    int worst = 0;
    for (int64_t x = 0; x < ow; x++)
    {
        const double want = 60.0 + 8.0 * (((double)x + 0.5) / (double)f - 0.5);
        worst = std::max(worst, (int)std::lround(std::fabs(out[(size_t)(2 * f * ow + x)].r - want)));
    }
    CHECK(worst <= 1);
}

TEST_CASE("Generated sprite entries: made by the workers at x4, apart from the packs' keys, made again when evicted", "[kfx_render][hd_generate]")
{
    sprite_pack_close_all();
    std::vector<TbPixel> frame(6 * 5, TbPixel_Transparent);
    for (int i = 0; i < 6 * 5; i++)
        if ((i % 6) > 0)
            frame[(size_t)i] = TbPixel_RGBA(30, 200, 90, 255);
    const int32_t e = sprite_pack_add_generated(0xABCD, frame.data(), nullptr, 6, 5, false);
    REQUIRE(e >= 0);
    CHECK(sprite_pack_find(0xABCD) == e);
    CHECK(sprite_pack_add_generated(0xABCD, frame.data(), nullptr, 6, 5, false) == -1); // the key is taken
    CHECK(sprite_pack_add_generated(0, frame.data(), nullptr, 6, 5, false) == -1);
    const struct SpritePackEntry *entry = sprite_pack_entry(e);
    CHECK(entry->source == SPRITE_PACK_GENERATED);
    CHECK(((entry->w == 6) && (entry->h == 5) && (entry->additive == 0)));
    CHECK(entry->state == SpE_Idle);
    sprite_pack_request(e, true);
    CHECK(sprite_pack_wait_idle() == 1);
    entry = sprite_pack_entry(e);
    REQUIRE(entry->state == SpE_Built);
    // x4 and x2 (the original draws at 1x).
    REQUIRE(entry->lv.levels == 2);
    CHECK(entry->lv.level_scale[0] == HD_GENERATE_FACTOR);
    const TbPixel mid = entry->lv.level[0][(2 * 4 + 1) * 24 + 3 * 4 + 1];
    CHECK(mid.a == 255);
    CHECK(mid.g >= 180);
    CHECK(entry->lv.level[0][(2 * 4 + 1) * 24 + 1].a == 0); // the transparent first column
    // Additive art: its subtract levels too.
    std::vector<TbPixel> sub(frame.size(), TbPixel_RGBA(5, 5, 5, 255));
    const int32_t a = sprite_pack_add_generated(0xABCE, frame.data(), sub.data(), 6, 5, true);
    REQUIRE(a >= 0);
    sprite_pack_request(a, true);
    CHECK(sprite_pack_wait_idle() == 1);
    REQUIRE(sprite_pack_entry(a)->state == SpE_Built);
    CHECK(sprite_pack_entry(a)->additive == 1);
    CHECK(sprite_pack_entry(a)->lv.sub[0] != nullptr);
    // Evicted: made again when drawn, with a new id.
    const uint64_t uid = sprite_pack_entry(e)->uid;
    sprite_pack_entry(e)->last_used = 0;
    sprite_pack_entry(a)->last_used = 1;
    CHECK(sprite_pack_evict(sprite_pack_entry(a)->lv.bytes) == 1);
    CHECK(sprite_pack_entry(e)->state == SpE_Idle);
    sprite_pack_request(e, true);
    CHECK(sprite_pack_wait_idle() == 1);
    CHECK(sprite_pack_entry(e)->state == SpE_Built);
    CHECK(sprite_pack_entry(e)->uid != uid);
    // Reopening the packs drops generated entries (they are made again as frames are drawn).
    sprite_pack_reopen(nullptr, nullptr, 0);
    CHECK(sprite_pack_find(0xABCD) == -1);
    sprite_pack_close_all();
}

TEST_CASE("ASSET_PACK_FILL: a frame no pack covers is drawn from generated art once the workers made it", "[kfx_render][hd_generate]")
{
    const int64_t prev_fill = keeperfx_ui_config.asset_pack_fill;
    char prev_pack[sizeof(keeperfx_ui_config.asset_pack)];
    std::memcpy(prev_pack, keeperfx_ui_config.asset_pack, sizeof(prev_pack));
    std::strcpy(keeperfx_ui_config.asset_pack, "NONE");
    // A 4 x 3 frame: each row 4 texels, then the row's end.
    const unsigned char rle[] = { 4, 10, 11, 12, 13, 0, 4, 20, 21, 22, 23, 0, 4, 30, 31, 32, 33, 0 };
    const int64_t kspr = 5, w = 4, h = 3, scale = 32 * 4; // drawn at x4: the generated level fits
    struct TbHdSpriteSource src;
    // HD sprites draw when the active palette and the HD art's reference (the game's) give a colour transform.
    static unsigned char palette[768];
    for (int i = 0; i < 768; i++)
        palette[i] = (unsigned char)((i / 3) / 4);
    unsigned char *saved_palette = lbDisplay.Palette, *saved_engine_palette = engine_palette;
    lbDisplay.Palette = palette;
    engine_palette = palette;
    RendererSetHdReferencePalette(palette);
    keeperfx_ui_config.asset_pack_fill = 0;
    sprite_hd_frame_begin();
    CHECK_FALSE(sprite_hd_source(kspr, rle, w, h, h, scale, &src)); // off: the original
    keeperfx_ui_config.asset_pack_fill = 1;
    sprite_hd_frame_begin(); // the store opened again, for generated art
    REQUIRE(sprite_hd_drawing_frame());
    {
        const TbBool first = sprite_hd_source(kspr, rle, w, h, h, scale, &src); // asks the workers for it
        sprite_pack_wait_idle();
        sprite_hd_frame_begin(); // published
        REQUIRE(sprite_hd_source(kspr, rle, w, h, h, scale, &src));
        (void)first;
        CHECK(src.scale == HD_GENERATE_FACTOR);
        CHECK(src.texels[0].a == 255);
        CHECK(sprite_hd_stats()->generated == 1);
        // Drawn again: the same art.
        const uint64_t id = src.id;
        REQUIRE(sprite_hd_source(kspr, rle, w, h, h, scale, &src));
        CHECK(src.id == id);
    }
    keeperfx_ui_config.asset_pack_fill = 0;
    sprite_hd_frame_begin(); // back to no store: the frames forgotten
    CHECK_FALSE(sprite_hd_source(kspr, rle, w, h, h, scale, &src));
    keeperfx_ui_config.asset_pack_fill = prev_fill;
    std::memcpy(keeperfx_ui_config.asset_pack, prev_pack, sizeof(prev_pack));
    lbDisplay.Palette = saved_palette;
    engine_palette = saved_engine_palette;
    RendererSetHdReferencePalette(nullptr);
    sprite_pack_close_all();
}
