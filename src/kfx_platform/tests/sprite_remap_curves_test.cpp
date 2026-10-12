// kfx_platform: SpriteRemapCurves() (bflib_vidraw.c), the sprite remaps as colour operations for HD sprites
// (docs/refactor/sprites/02-design.md section 5). An HD texel made from palette entry i has entry i's colour in the
// reference palette; with the palette effect applied as a colour transform (bflib_colour_transform.h) and the remap as
// per-channel curves, it must come out exactly as the index path's lbSpriteRemapTable[i] -- for every remap the
// engine sets up (shade, tint at both strengths for every reference colour, the white and the red flash), under every
// palette effect the transform reproduces. Synthetic palettes always; with KFX_SPRITES_COMPAT_DIR=<install> also the
// game's own palette.dat, bluepal.dat, bluepall.dat, lightng.pal and fades of palette.dat.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "bflib_colour_transform.h"
#include "bflib_dernc.h"
#include "bflib_video.h"
#include "bflib_vidraw.h"

namespace {
using Palette = std::array<unsigned char, 768>;

// A palette like the engine's (bflib_colour_transform_test.cpp's): entry 0 black, the rest varied, two duplicates.
Palette synthetic_base()
{
    Palette p{};
    uint32_t s = 4242;
    auto next = [&s]() { s = s * 1103515245u + 12345u; return (s >> 16) & 63; };
    for (int i = 1; i < 256; i++)
    {
        p[3 * i + 0] = (unsigned char)next();
        p[3 * i + 1] = (unsigned char)next();
        p[3 * i + 2] = (unsigned char)next();
        if ((p[3 * i] | p[3 * i + 1] | p[3 * i + 2]) == 0)
            p[3 * i] = 1;
    }
    std::memcpy(&p[3 * 50], &p[3 * 4], 3);
    std::memcpy(&p[3 * 127], &p[3 * 18], 3);
    return p;
}

// Per-channel fades, as the pain, level and possession fades are (vidfade.c): each channel scaled toward 0 or 63.
Palette fade(const Palette &base, int kr, int kg, int kb, bool to_white)
{
    Palette p = base;
    const int k[3] = { kr, kg, kb };
    for (int i = 0; i < 256; i++)
        for (int c = 0; c < 3; c++)
        {
            const int v = base[3 * i + c];
            p[3 * i + c] = (unsigned char)(to_white ? v + (63 - v) * k[c] / 64 : v * k[c] / 64);
        }
    return p;
}

// A swap no per-channel function gives (the freeze and lightning palettes are of this kind): channels rotated.
Palette rotated(const Palette &base)
{
    Palette p = base;
    for (int i = 1; i < 256; i++)
    {
        p[3 * i + 0] = base[3 * i + 1];
        p[3 * i + 1] = base[3 * i + 2];
        p[3 * i + 2] = base[3 * i + 0];
    }
    return p;
}

// As the engine loads them: some are RNC-packed (bluepal.dat, bluepall.dat).
bool load_palette(const std::string &path, Palette &out)
{
    return LbFileLoadAtMax(path.c_str(), out.data(), out.size()) == (int64_t)out.size();
}

struct PaletteCase {
    std::string name;
    Palette active;
};

// For one base palette and one active palette: every remap op, every entry 1..255.
void check_palette(const Palette &base, const PaletteCase &pc, std::vector<uint8_t> &table, const std::vector<uint8_t> &nearest)
{
    ColourTransform ct = {};
    ct.table = table.data();
    const ColourTransformMode mode = colour_transform_build(&ct, base.data(), pc.active.data(), nearest.data());
    INFO("palette " << pc.name << ", transform mode " << (int)mode);
    REQUIRE(mode != CTM_Unsupported);

    unsigned char *saved = lbDisplay.Palette;
    Palette active = pc.active;
    lbDisplay.Palette = active.data();

    uint64_t checked = 0, mismatches = 0;
    std::string first;
    auto check_op = [&](const char *op_name, int64_t param) {
        uint8_t curves[3][256];
        SpriteRemapCurves(&lbSpriteRemapOp, curves);
        for (int i = 1; i < 256; i++)
        {
            const TbPixel base_colour = TbPixel_RGB(chan6_to_8(base[3 * i]), chan6_to_8(base[3 * i + 1]), chan6_to_8(base[3 * i + 2]));
            const TbPixel t = colour_transform_apply(&ct, base_colour);
            const TbPixel want = lbSpriteRemapTable[i];
            checked++;
            if ((curves[0][t.r] != want.r) || (curves[1][t.g] != want.g) || (curves[2][t.b] != want.b))
            {
                if (mismatches++ == 0)
                    first = std::string(op_name) + " " + std::to_string(param) + ", entry " + std::to_string(i);
            }
        }
    };
    for (int64_t shade = 0; shade <= 64; shade++)
    {
        SetupSpriteRemapShade(shade);
        CHECK(lbSpriteRemapOp.kind == SprRemap_Shade);
        check_op("shade", shade);
    }
    for (int strength : { SPRITE_TINT_LEGACY, SPRITE_TINT_STRONG })
        for (int ref = 0; ref < 256; ref++)
        {
            SetupSpriteRemapGhost((uint8_t)ref, (uint8_t)strength);
            check_op(strength == SPRITE_TINT_LEGACY ? "tint legacy, ref" : "tint strong, ref", ref);
        }
    SetupSpriteRemapWhiteFlash();
    CHECK(lbSpriteRemapOp.kind == SprRemap_Flash);
    check_op("white flash", 0);
    SetupSpriteRemapRedFlash();
    check_op("red flash", 0);

    lbDisplay.Palette = saved;
    INFO("first mismatch: " << first);
    CHECK(checked == 255u * (65 + 2 * 256 + 2));
    CHECK(mismatches == 0);
}

void check_all(const Palette &base, const std::vector<PaletteCase> &cases)
{
    std::vector<uint8_t> table(4 * COLOUR_TRANSFORM_TABLE_SIZE);
    std::vector<uint8_t> nearest(COLOUR_TRANSFORM_TABLE_SIZE);
    colour_transform_nearest_entries(base.data(), nearest.data());
    for (const PaletteCase &pc : cases)
        check_palette(base, pc, table, nearest);
}

std::vector<PaletteCase> derived_cases(const Palette &base)
{
    Palette white{};
    for (int i = 0; i < 768; i++)
        white[i] = 0x3F;
    return {
        { "identity", base },
        { "pain (red kept, green and blue faded)", fade(base, 64, 40, 40, false) },
        { "level fade, half", fade(base, 32, 32, 32, false) },
        { "level fade, nearly out", fade(base, 3, 3, 3, false) },
        { "possession fade to white, half", fade(base, 32, 32, 32, true) },
        { "white", white },
        { "channels rotated (non-separable)", rotated(base) },
    };
}
}

TEST_CASE("SpriteRemapCurves of kind 0 is the identity", "[sprite_remap_curves]")
{
    struct SpriteRemapOp none = {};
    uint8_t curves[3][256];
    SpriteRemapCurves(&none, curves);
    for (int v = 0; v < 256; v++)
    {
        CHECK(curves[0][v] == v);
        CHECK(curves[1][v] == v);
        CHECK(curves[2][v] == v);
    }
}

TEST_CASE("Sprite remaps as curves after the palette transform equal the index path's tables (synthetic palettes)", "[sprite_remap_curves]")
{
    const Palette base = synthetic_base();
    check_all(base, derived_cases(base));
}

TEST_CASE("Sprite remaps as curves after the palette transform equal the index path's tables (the game's palettes)", "[sprite_remap_curves_compat]")
{
    const char *dir = std::getenv("KFX_SPRITES_COMPAT_DIR");
    if (dir == nullptr)
        SKIP("KFX_SPRITES_COMPAT_DIR not set");
    const std::string data = std::string(dir) + "/data/";
    Palette base{};
    REQUIRE(load_palette(data + "palette.dat", base));
    std::vector<PaletteCase> cases = derived_cases(base);
    for (const char *name : { "bluepal.dat", "bluepall.dat", "lightng.pal" })
    {
        PaletteCase pc{ name, {} };
        REQUIRE(load_palette(data + name, pc.active));
        cases.push_back(pc);
    }
    check_all(base, cases);
}

TEST_CASE("The curves check catches a wrong op (negative control)", "[sprite_remap_curves]")
{
    Palette base = synthetic_base();
    unsigned char *saved = lbDisplay.Palette;
    lbDisplay.Palette = base.data();
    SetupSpriteRemapShade(20);
    struct SpriteRemapOp wrong = lbSpriteRemapOp;
    wrong.shade = 21;
    uint8_t curves[3][256];
    SpriteRemapCurves(&wrong, curves);
    int mismatches = 0;
    for (int i = 1; i < 256; i++)
    {
        const TbPixel c = TbPixel_RGB(chan6_to_8(base[3 * i]), chan6_to_8(base[3 * i + 1]), chan6_to_8(base[3 * i + 2]));
        if ((curves[0][c.r] != lbSpriteRemapTable[i].r) || (curves[1][c.g] != lbSpriteRemapTable[i].g) || (curves[2][c.b] != lbSpriteRemapTable[i].b))
            mismatches++;
    }
    lbDisplay.Palette = saved;
    CHECK(mismatches > 200);
}
