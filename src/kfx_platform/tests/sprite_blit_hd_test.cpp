// kfx_platform: LbSpriteDrawHdUsingScalingData() (bflib_vidraw_spr_hd.c), HD sprites (docs/refactor/sprites/,
// Phase 3). In parity mode an HD frame made by upscaling the original (x2, x3, x4; with every texel but the sampled
// middle one inverted, so only the right sample passes) must draw exactly what LbSpriteDrawUsingScalingData() draws
// for the original: every blend (solid, both ghosts, a shade and a tint remap), flip, scale, clip and position of
// sprite_blit_golden_test.cpp's set, at the original's full height and cut short (the water cutoff). In precision
// mode the plain upscale must draw the original too (the geometry is the original's), and the patterned one mustn't.
#include <catch2/catch_test_macros.hpp>

#include "bflib_video.h"
#include "bflib_vidraw.h"
#include "renderer/RendererManager.h"
#include "sprite_blit_harness.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

extern "C" TbResult LbSpriteDrawUsingScalingData(int64_t posx, int64_t posy, const struct TbSourceBuffer *src_buf);

namespace {

/** The sprite's indices, row by row (0: transparent). */
std::vector<unsigned char> decode(const TestSprite &spr)
{
    std::vector<unsigned char> idx((size_t)(spr.w * spr.h), 0);
    size_t p = 0;
    for (int y = 0; y < spr.h; y++)
    {
        int x = 0;
        for (;;)
        {
            const int n = (signed char)spr.rle[p++];
            if (n == 0)
                break;
            if (n < 0) { x -= n; continue; }
            for (int k = 0; k < n; k++, x++)
                idx[(size_t)(y * spr.w + x)] = spr.rle[p + k];
            p += (size_t)n;
        }
    }
    return idx;
}

/** The sprite upscaled `s` times through `palette`; with `pattern`, every texel but the middle of its block inverted. */
std::vector<TbPixel> upscale(const TestSprite &spr, const unsigned char *palette, int s, bool pattern)
{
    const std::vector<unsigned char> idx = decode(spr);
    std::vector<TbPixel> out((size_t)(spr.w * s * spr.h * s));
    for (int y = 0; y < spr.h * s; y++)
        for (int x = 0; x < spr.w * s; x++)
        {
            const unsigned char i = idx[(size_t)((y / s) * spr.w + x / s)];
            TbPixel t = (i != 0) ? resolve_indexed_pixel(i, palette) : TbPixel_Transparent;
            if (pattern && (i != 0) && (((x % s) != (s - 1) / 2) || ((y % s) != (s - 1) / 2)))
                t = TbPixel_RGBA((uint8_t)(255 - t.r), (uint8_t)(255 - t.g), (uint8_t)(255 - t.b), 255);
            out[(size_t)(y * spr.w * s + x)] = t;
        }
    return out;
}

enum Blend { B_Solid, B_Trans4, B_Trans8, B_Shade, B_Tint };
const struct { const char *name; Blend blend; } kBlends[] = {
    {"solid", B_Solid}, {"trans4", B_Trans4}, {"trans8", B_Trans8}, {"shade", B_Shade}, {"tint", B_Tint},
};

void set_blend(Blend b, int64_t flip)
{
    int64_t flags = flip;
    switch (b)
    {
    case B_Trans4: flags |= Lb_SPRITE_TRANSPAR4; break;
    case B_Trans8: flags |= Lb_SPRITE_TRANSPAR8; break;
    case B_Shade: SetupSpriteRemapShade(20); flags |= Lb_SPRITE_REMAP; break;
    case B_Tint: SetupSpriteRemapGhost(77, SPRITE_TINT_STRONG); flags |= Lb_SPRITE_REMAP; break;
    default: break;
    }
    RendererSetDrawFlags(flags);
}

/** Draws the original (hd == nullptr) or its HD version as the game would place the original (see the golden test's
 *  draw()): returns the screen. */
std::vector<TbPixel> draw(Harness &h, Blend b, int64_t flip, const TestSprite &spr, int x, int y, int dw, int dhh, int off_x,
    int off_y, int cut, const TbHdSpriteSource *hd)
{
    h.fill();
    const int frame_w = spr.w + off_x + (off_x ? 1 : 0), frame_h = spr.h + off_y + (off_y ? 1 : 0);
    LbSpriteSetScalingData(x, y, frame_w, frame_h, dw * frame_w / spr.w, dhh * frame_h / spr.h);
    set_blend(b, flip);
    if (hd == nullptr)
    {
        const struct TbSourceBuffer src = {spr.rle.data(), (uint64_t)spr.w, (uint64_t)(spr.h - cut), 0};
        LbSpriteDrawUsingScalingData(off_x, off_y, &src);
    }
    else
    {
        REQUIRE(LbSpriteDrawHdUsingScalingData(off_x, off_y, hd));
    }
    return h.screen;
}

struct Count { int64_t cases = 0, same = 0; std::string first_diff; };

/** Every case: the original, then its HD version; counts the cases whose screens are the same. */
Count compare_all(int s, bool pattern, bool parity)
{
    Harness h;
    Count c;
    for (const TestSprite &spr : make_sprites())
    {
        const std::vector<TbPixel> texels = upscale(spr, h.palette, s, pattern);
        for (const auto &bl : kBlends)
            for (const auto &flip : kFlips)
                for (const auto &sc : kScales)
                {
                    const int dw = std::max(1, spr.w * sc.nw / sc.dw), dhh = std::max(1, spr.h * sc.nh / sc.dh);
                    const int positions[5][2] = {{20, 15}, {-dw / 2, 30}, {40, -dhh / 2}, {kWinW - dw / 2, 10}, {10, kWinH - dhh / 2}};
                    for (const auto &p : positions)
                        for (int offset = 0; offset < 2; offset++)
                            for (int cut = 0; cut < ((spr.h > 2) ? 2 : 1); cut++)
                            {
                                const int ox = offset ? 2 : 0, oy = offset ? 1 : 0, ct = cut ? 2 : 0;
                                const std::vector<TbPixel> want = draw(h, bl.blend, flip.flags, spr, p[0], p[1], dw, dhh, ox, oy, ct, nullptr);
                                TbHdSpriteSource hd = {};
                                hd.texels = texels.data();
                                hd.stride = spr.w * s;
                                hd.scale = s;
                                hd.rle = spr.rle.data();
                                hd.orig_width = spr.w;
                                hd.orig_height = spr.h - ct;
                                hd.parity = parity;
                                const std::vector<TbPixel> got = draw(h, bl.blend, flip.flags, spr, p[0], p[1], dw, dhh, ox, oy, ct, &hd);
                                c.cases++;
                                if (std::memcmp(want.data(), got.data(), want.size() * sizeof(TbPixel)) == 0)
                                    c.same++;
                                else if (c.first_diff.empty())
                                    c.first_diff = spr.name + "/" + bl.name + "/" + flip.name + "/" + sc.name + " at " + std::to_string(p[0]) + ","
                                        + std::to_string(p[1]) + " offset " + std::to_string(offset) + " cut " + std::to_string(ct);
                            }
                }
    }
    RendererSetDrawFlags(0);
    return c;
}

} // namespace

TEST_CASE("HD sprites in parity mode draw exactly what the indexed blitter draws", "[kfx_platform][sprite_golden_hd]")
{
    for (int s : {2, 3, 4})
        for (bool pattern : {false, true})
        {
            const Count c = compare_all(s, pattern, true);
            INFO("x" << s << (pattern ? " with the pattern" : "") << ": first difference " << c.first_diff);
            CHECK(c.cases > 3000);
            CHECK(c.same == c.cases);
        }
}

TEST_CASE("HD sprites in precision mode keep the original's geometry, and sample inside its texels", "[kfx_platform][sprite_golden_hd]")
{
    for (int s : {2, 4})
    {
        const Count plain = compare_all(s, false, false);
        INFO("x" << s << " plain: first difference " << plain.first_diff);
        CHECK(plain.same == plain.cases);
        const Count patterned = compare_all(s, true, false);
        INFO("x" << s << " patterned: " << patterned.same << " of " << patterned.cases << " the same");
        // Downscaled cases sample the middle texel too; every case with a texel drawn at least 2 pixels wide differs.
        CHECK(patterned.same < patterned.cases / 2);
    }
}

TEST_CASE("LbSpriteDrawHdUsingScalingData declines a remap that isn't the sprite remap table", "[kfx_platform][sprite_golden_hd]")
{
    Harness h;
    const TestSprite spr = make_sprites()[0];
    const std::vector<TbPixel> texels = upscale(spr, h.palette, 2, false);
    TbHdSpriteSource hd = {};
    hd.texels = texels.data();
    hd.stride = spr.w * 2;
    hd.scale = 2;
    hd.rle = spr.rle.data();
    hd.orig_width = spr.w;
    hd.orig_height = spr.h;
    hd.parity = true;
    LbSpriteSetScalingData(10, 10, spr.w, spr.h, spr.w * 2, spr.h * 2);
    SetupSpriteRemapShade(10);
    lbSpriteReMapPtr = h.remap; // a caller's own table
    RendererSetDrawFlags(Lb_SPRITE_REMAP);
    CHECK_FALSE(LbSpriteDrawHdUsingScalingData(0, 0, &hd));
    lbSpriteReMapPtr = lbSpriteRemapTable;
    CHECK(LbSpriteDrawHdUsingScalingData(0, 0, &hd));
    RendererSetDrawFlags(0);
}

namespace {

/** The x s upscale of `spr` inside a canvas `spill` original texels larger a side (left, top, right, bottom), the
 *  margins opaque `margin`. */
std::vector<TbPixel> with_spill(const std::vector<TbPixel> &core, const TestSprite &spr, int s, const int64_t spill[4], TbPixel margin)
{
    const int64_t ow = spr.w + spill[0] + spill[2], oh = spr.h + spill[1] + spill[3];
    std::vector<TbPixel> out((size_t)(ow * s * oh * s), margin);
    for (int y = 0; y < spr.h * s; y++)
        for (int x = 0; x < spr.w * s; x++)
            out[(size_t)((y + spill[1] * s) * ow * s + x + spill[0] * s)] = core[(size_t)(y * spr.w * s + x)];
    return out;
}

/** Bound i of an axis the arrays were set for (LbSpriteSetScalingData()'s rule): (f/2 + x*65536 + i*f) >> 16,
 *  clipped to the window's [0, g]. */
int64_t step_bound(int64_t x, int64_t swidth, int64_t dwidth, int64_t i, int64_t g)
{
    const int64_t f = (dwidth << 16) / swidth;
    const int64_t b = ((f >> 1) + x * 65536 + i * f) >> 16;
    return std::max<int64_t>(0, std::min(b, g));
}

/** Where cell i of an axis starts (end: where it ends): the arrays' own cell for the n they were set for (kept as
 *  they are), the rule's past them. */
int64_t cell_edge(const std::vector<int64_t> &steps, int64_t n, int64_t x, int64_t dwidth, int64_t i, bool end, int64_t g)
{
    if ((i >= 0) && (i < n))
        return steps[(size_t)(2 * i)] + (end ? steps[(size_t)(2 * i + 1)] : 0);
    return step_bound(x, n, dwidth, end ? i + 1 : i, g);
}

} // namespace

TEST_CASE("HD art past the original's crop: precision draws it beside the original's pixels, which don't move",
    "[kfx_platform][sprite_golden_hd]")
{
    Harness h;
    const TbPixel margin = TbPixel_RGBA(255, 0, 255, 255);
    const int64_t spill[4] = { 1, 1, 2, 1 };
    int64_t cases = 0, geometry_checked = 0;
    for (const TestSprite &spr : make_sprites())
    {
        if (spr.h <= 2)
            continue;
        for (int s : {2, 4})
        {
            const std::vector<TbPixel> core = upscale(spr, h.palette, s, false);
            const std::vector<TbPixel> wide = with_spill(core, spr, s, spill, margin);
            const int64_t wide_stride = (spr.w + spill[0] + spill[2]) * s;
            for (const auto &bl : kBlends)
                for (const auto &flip : kFlips)
                    for (const auto &sc : kScales)
                    {
                        const int dw = std::max(1, spr.w * sc.nw / sc.dw), dhh = std::max(1, spr.h * sc.nh / sc.dh);
                        const int positions[3][2] = {{20, 15}, {-dw / 2, 30}, {kWinW - dw / 2, kWinH - dhh / 2}};
                        for (const auto &p : positions)
                            for (int offset = 0; offset < 2; offset++)
                                for (int ct : {0, 2})
                                {
                                    const int ox = offset ? 2 : 0, oy = offset ? 1 : 0;
                                    TbHdSpriteSource hd = {};
                                    hd.texels = core.data();
                                    hd.stride = spr.w * s;
                                    hd.scale = s;
                                    hd.rle = spr.rle.data();
                                    hd.orig_width = spr.w;
                                    hd.orig_height = spr.h - ct;
                                    hd.full_height = spr.h;
                                    const std::vector<TbPixel> background = (h.fill(), h.screen);
                                    const std::vector<TbPixel> want = draw(h, bl.blend, flip.flags, spr, p[0], p[1], dw, dhh, ox, oy, ct, &hd);
                                    TbHdSpriteSource sp = hd;
                                    sp.texels = wide.data() + spill[1] * s * wide_stride + spill[0] * s;
                                    sp.stride = wide_stride;
                                    std::memcpy(sp.spill, spill, sizeof(spill));
                                    // The arrays draw() sets (as the game does), to compare with after the spilled draw.
                                    const int frame_w = spr.w + ox + (ox ? 1 : 0), frame_h = spr.h + oy + (oy ? 1 : 0);
                                    const int64_t fdw = dw * frame_w / spr.w, fdh = dhh * frame_h / spr.h;
                                    LbSpriteSetScalingData(p[0], p[1], frame_w, frame_h, fdw, fdh);
                                    const std::vector<int64_t> xs_before(xsteps_array, xsteps_array + 2 * 64), ys_before(ysteps_array, ysteps_array + 2 * 64);
                                    const std::vector<TbPixel> got = draw(h, bl.blend, flip.flags, spr, p[0], p[1], dw, dhh, ox, oy, ct, &sp);
                                    INFO(spr.name << " x" << s << " " << bl.name << "/" << flip.name << "/" << sc.name << " at " << p[0] << ","
                                        << p[1] << " offset " << offset << " cut " << ct);
                                    CHECK(std::equal(xs_before.begin(), xs_before.end(), xsteps_array));
                                    CHECK(std::equal(ys_before.begin(), ys_before.end(), ysteps_array));
                                    // Only pixels the original left alone change.
                                    int64_t moved = 0, wrong = 0;
                                    int64_t mx0 = INT64_MAX, my0 = INT64_MAX, mx1 = INT64_MIN, my1 = INT64_MIN;
                                    for (size_t i = 0; i < got.size(); i++)
                                    {
                                        if (std::memcmp(&got[i], &want[i], sizeof(TbPixel)) == 0)
                                            continue;
                                        if (std::memcmp(&want[i], &background[i], sizeof(TbPixel)) != 0)
                                            moved++;
                                        if ((bl.blend == B_Solid) && (std::memcmp(&got[i], &margin, sizeof(TbPixel)) != 0))
                                            wrong++;
                                        const int64_t x = (int64_t)(i % kScreenW) - kWinX, y = (int64_t)(i / kScreenW) - kWinY;
                                        mx0 = std::min(mx0, x); my0 = std::min(my0, y);
                                        mx1 = std::max(mx1, x + 1); my1 = std::max(my1, y + 1);
                                    }
                                    CHECK(moved == 0);
                                    CHECK(wrong == 0);
                                    cases++;
                                    // Unclipped, solid, magnified: the margins are whole cells by the arrays' rule, a flip
                                    // swapping the sides; the cutoff hides the art below.
                                    if ((p[0] == 20) && (bl.blend == B_Solid) && (sc.nw > sc.dw) && (sc.nh > sc.dh))
                                    {
                                        const bool fh = (flip.flags & Lb_SPRITE_FLIP_HORIZ) != 0, fv = (flip.flags & Lb_SPRITE_FLIP_VERTIC) != 0;
                                        const int64_t below = ct ? 0 : spill[3];
                                        const int64_t lead_x = fh ? spill[2] : spill[0], trail_x = fh ? spill[0] : spill[2];
                                        const int64_t lead_y = fv ? below : spill[1], trail_y = fv ? spill[1] : below;
                                        CHECK(mx0 == cell_edge(xs_before, frame_w, p[0], fdw, ox - lead_x, false, kWinW));
                                        CHECK(mx1 == cell_edge(xs_before, frame_w, p[0], fdw, ox + spr.w + trail_x - 1, true, kWinW));
                                        CHECK(my0 == cell_edge(ys_before, frame_h, p[1], fdh, oy - lead_y, false, kWinH));
                                        CHECK(my1 == cell_edge(ys_before, frame_h, p[1], fdh, oy + (spr.h - ct) + trail_y - 1, true, kWinH));
                                        geometry_checked++;
                                    }
                                    // Parity mode ignores the spill: the original, exactly.
                                    sp.parity = true;
                                    hd.parity = true;
                                    const std::vector<TbPixel> parity_want = draw(h, bl.blend, flip.flags, spr, p[0], p[1], dw, dhh, ox, oy, ct, &hd);
                                    const std::vector<TbPixel> parity_got = draw(h, bl.blend, flip.flags, spr, p[0], p[1], dw, dhh, ox, oy, ct, &sp);
                                    CHECK(std::memcmp(parity_want.data(), parity_got.data(), parity_got.size() * sizeof(TbPixel)) == 0);
                                }
                    }
        }
    }
    RendererSetDrawFlags(0);
    CHECK(cases > 1000);
    CHECK(geometry_checked > 20);
}

namespace {

/** Additive art for a sprite (Phase 9): each texel's render_alpha_blend() ramp delta split into add and sub, upscaled
 *  `s` times; indices 0 and above 64 transparent; with `pattern`, every texel but the middle one adds its complement. */
void additive_art(const TestSprite &spr, int s, bool pattern, std::vector<TbPixel> &add, std::vector<TbPixel> &sub)
{
    static const int ramp_deltas[8][3] = { { 4, 4, 4 }, { 6, 4, 0 }, { 6, 1, 1 }, { 2, 2, 6 }, { 2, 6, 2 }, { 3, 0, 3 },
        { -2, -2, -2 }, { 6, 3, 1 } };
    const std::vector<unsigned char> idx = decode(spr);
    add.assign((size_t)(spr.w * s * spr.h * s), TbPixel_Transparent);
    sub.assign(add.size(), TbPixel_Transparent);
    for (int y = 0; y < spr.h * s; y++)
        for (int x = 0; x < spr.w * s; x++)
        {
            const unsigned char i = idx[(size_t)((y / s) * spr.w + x / s)];
            if (i < 1 || i > 64)
                continue;
            int pa[3], pm[3];
            for (int c = 0; c < 3; c++)
            {
                const int d = ramp_deltas[(i - 1) / 8][c] * ((i - 1) % 8) * 255 / 63;
                pa[c] = d > 0 ? d : 0;
                pm[c] = d < 0 ? -d : 0;
                if (pattern && (((x % s) != (s - 1) / 2) || ((y % s) != (s - 1) / 2)))
                    pa[c] = 255 - pa[c];
            }
            add[(size_t)(y * spr.w * s + x)] = TbPixel_RGBA((uint8_t)pa[0], (uint8_t)pa[1], (uint8_t)pa[2], 255);
            sub[(size_t)(y * spr.w * s + x)] = TbPixel_RGBA((uint8_t)pm[0], (uint8_t)pm[1], (uint8_t)pm[2], 255);
        }
}

/** The harness's sprites with their indices folded into 1..64 (every pixel a ramp step: no "write black"). */
std::vector<TestSprite> ramp_sprites()
{
    std::vector<TestSprite> out;
    for (const TestSprite &spr : make_sprites())
    {
        const std::vector<unsigned char> idx = decode(spr);
        std::vector<std::vector<int>> rows((size_t)spr.h, std::vector<int>((size_t)spr.w, 0));
        for (int y = 0; y < spr.h; y++)
            for (int x = 0; x < spr.w; x++)
            {
                const int v = idx[(size_t)(y * spr.w + x)];
                rows[(size_t)y][(size_t)x] = v ? ((v - 1) % 64) + 1 : 0;
            }
        out.push_back({spr.name + "-ramps", spr.w, spr.h, encode(rows)});
    }
    return out;
}

/** Every case: the original alpha draw, then the additive HD draw; counts the cases whose screens are the same. */
Count compare_additive(const std::vector<TestSprite> &sprites, int s, bool pattern, bool parity)
{
    Harness h;
    Count c;
    for (const TestSprite &spr : sprites)
    {
        std::vector<TbPixel> add, sub;
        additive_art(spr, s, pattern, add, sub);
        for (const auto &flip : kFlips)
            for (const auto &sc : kScales)
            {
                const int dw = std::max(1, spr.w * sc.nw / sc.dw), dhh = std::max(1, spr.h * sc.nh / sc.dh);
                const int positions[3][2] = {{20, 15}, {-dw / 2, 30}, {kWinW - dw / 2, kWinH - dhh / 2}};
                for (const auto &p : positions)
                    for (int offset = 0; offset < 2; offset++)
                        for (int ct : {0, 2})
                        {
                            if (ct && spr.h <= 2)
                                continue;
                            const int ox = offset ? 2 : 0, oy = offset ? 1 : 0;
                            const int frame_w = spr.w + ox + (ox ? 1 : 0), frame_h = spr.h + oy + (oy ? 1 : 0);
                            auto run = [&](bool hd_draw) {
                                h.fill();
                                LbSpriteSetScalingData(p[0], p[1], frame_w, frame_h, dw * frame_w / spr.w, dhh * frame_h / spr.h);
                                RendererSetDrawFlags(flip.flags);
                                if (!hd_draw)
                                {
                                    const struct TbSourceBuffer src = {spr.rle.data(), (uint64_t)spr.w, (uint64_t)(spr.h - ct), 0};
                                    DrawAlphaSpriteUsingScalingData(ox, oy, &src);
                                }
                                else
                                {
                                    TbHdSpriteSource hd = {};
                                    hd.texels = add.data();
                                    hd.sub_texels = sub.data();
                                    hd.additive = true;
                                    hd.stride = spr.w * s;
                                    hd.scale = s;
                                    hd.rle = spr.rle.data();
                                    hd.orig_width = spr.w;
                                    hd.orig_height = spr.h - ct;
                                    hd.full_height = spr.h;
                                    hd.parity = parity;
                                    REQUIRE(LbSpriteDrawHdUsingScalingData(ox, oy, &hd));
                                }
                                return h.screen;
                            };
                            const std::vector<TbPixel> want = run(false), got = run(true);
                            c.cases++;
                            if (std::memcmp(want.data(), got.data(), want.size() * sizeof(TbPixel)) == 0)
                                c.same++;
                            else if (c.first_diff.empty())
                                c.first_diff = spr.name + "/" + flip.name + "/" + sc.name + " at " + std::to_string(p[0]) + "," +
                                    std::to_string(p[1]) + " offset " + std::to_string(offset) + " cut " + std::to_string(ct);
                        }
            }
    }
    RendererSetDrawFlags(0);
    return c;
}

} // namespace

TEST_CASE("Additive HD art (alpha-ramp draws) in parity mode draws exactly what the alpha blitter draws",
    "[kfx_platform][sprite_golden_hd]")
{
    for (int s : {2, 3, 4})
        for (bool pattern : {false, true})
        {
            // The harness's own sprites: indices above 64 inside runs too (the original writes black there).
            const Count c = compare_additive(make_sprites(), s, pattern, true);
            INFO("x" << s << (pattern ? " with the pattern" : "") << ": first difference " << c.first_diff);
            CHECK(c.cases > 500);
            CHECK(c.same == c.cases);
        }
}

TEST_CASE("Additive HD art in precision mode keeps the original's geometry, and samples inside its texels",
    "[kfx_platform][sprite_golden_hd]")
{
    for (int s : {2, 4})
    {
        const Count plain = compare_additive(ramp_sprites(), s, false, false);
        INFO("x" << s << " plain: first difference " << plain.first_diff);
        CHECK(plain.same == plain.cases);
        const Count patterned = compare_additive(ramp_sprites(), s, true, false);
        INFO("x" << s << " patterned: " << patterned.same << " of " << patterned.cases << " the same");
        CHECK(patterned.same < patterned.cases / 2);
    }
}
