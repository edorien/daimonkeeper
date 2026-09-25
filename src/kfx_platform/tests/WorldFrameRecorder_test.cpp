// kfx_platform: renderer/WorldFrameRecorder.cpp -- the pure op recorder behind
// the gpu-v2 world-frame seam (docs/refactor/renderer/gpu-v2/07-phased-delivery.md).
// Replaces C.0's engine_render_worldframe_test.cpp (flatten_polygon_standard_item),
// whose copy-a-triangle transform is now WorldFrameRecorder::AddPoly().
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "renderer/WorldFrameRecorder.h"

#include <cstring>

TEST_CASE("AddPoly copies all three vertices and the texture pointer losslessly, by value", "[kfx_platform][WorldFrameRecorder]") {
    struct PolyPoint v0 = { 10, 20, 30, 40, 50, 0 };
    struct PolyPoint v1 = { 11, 21, 31, 41, 51, 0 };
    struct PolyPoint v2 = { 12, 22, 32, 42, 52, 0 };
    unsigned char texture_data[4] = { 1, 2, 3, 4 };

    WorldFrameRecorder rec;
    rec.AddPoly(&v0, &v1, &v2, texture_data);
    v0.X = 999; // must not alias the recorded copy

    WorldFrame f = rec.Build();
    REQUIRE(f.op_count == 1);
    REQUIRE(f.ops[0].kind == WF_OP_POLY);
    const WorldFramePolyItem& p = f.ops[0].u.poly;
    CHECK(p.v0.X == 10); CHECK(p.v0.Y == 20); CHECK(p.v0.U == 30); CHECK(p.v0.V == 40); CHECK(p.v0.S == 50);
    CHECK(p.v1.X == 11); CHECK(p.v1.S == 51);
    CHECK(p.v2.Y == 22); CHECK(p.v2.V == 42);
    CHECK(p.texture == texture_data);
}

TEST_CASE("ops keep draw order across kinds", "[kfx_platform][WorldFrameRecorder]") {
    struct PolyPoint pt = {};
    unsigned char rle[2] = { 0, 0 };
    uint16_t map[4] = { 0, 1, 2, 3 };
    WorldFrameRecorder rec;
    rec.AddPoly(&pt, &pt, &pt, nullptr);
    rec.AddSprite(rle, 4, 4, 0, 0, 4, 4, map, map, nullptr, WFS_SOLID, 0);
    rec.AddPoly(&pt, &pt, &pt, nullptr);
    WorldFrame f = rec.Build();
    REQUIRE(f.op_count == 3);
    CHECK(f.ops[0].kind == WF_OP_POLY);
    CHECK(f.ops[1].kind == WF_OP_SPRITE);
    CHECK(f.ops[2].kind == WF_OP_POLY);
}

TEST_CASE("AddSprite records the scaling lookups and de-duplicates colour tables", "[kfx_platform][WorldFrameRecorder]") {
    unsigned char rle[2] = { 0, 0 };
    uint16_t xmap[3] = { 0, 0, 1 };
    uint16_t ymap[2] = { 5, 6 };
    uint32_t table_a[256], table_b[256];
    for (int i = 0; i < 256; i++) { table_a[i] = 0xFF000000u | i; table_b[i] = 0xFF00FF00u | i; }

    WorldFrameRecorder rec;
    rec.AddSprite(rle, 2, 7, 10, 20, 3, 2, xmap, ymap, nullptr, WFS_SOLID, 0);   // palette, row 0
    rec.AddSprite(rle, 2, 7, 0, 0, 3, 2, xmap, ymap, table_a, WFS_GHOST1, 0);      // new table -> row 1
    rec.AddSprite(rle, 2, 7, 0, 0, 3, 2, xmap, ymap, table_b, WFS_SOLID, 0);       // new table -> row 2
    rec.AddSprite(rle, 2, 7, 0, 0, 3, 2, xmap, ymap, table_a, WFS_SOLID, 0);       // same content -> row 1 again

    WorldFrame f = rec.Build();
    REQUIRE(f.op_count == 4);
    CHECK(f.lut_rows == 2);
    CHECK(f.ops[0].u.sprite.lut_row == 0);
    CHECK(f.ops[1].u.sprite.lut_row == 1);
    CHECK(f.ops[2].u.sprite.lut_row == 2);
    CHECK(f.ops[3].u.sprite.lut_row == 1);

    const WorldFrameSpriteOp& s = f.ops[0].u.sprite;
    CHECK(s.dst_x == 10); CHECK(s.dst_y == 20); CHECK(s.dst_w == 3); CHECK(s.dst_h == 2);
    CHECK(f.lookup[s.xmap_off + 2] == 1);
    CHECK(f.lookup[s.ymap_off + 0] == 5);
    CHECK(f.lookup[s.ymap_off + 1] == 6);
    CHECK(f.lut[5] == table_a[5]);
    CHECK(f.lut[256 + 5] == table_b[5]);
}

TEST_CASE("AddSprite ignores degenerate sprites", "[kfx_platform][WorldFrameRecorder]") {
    unsigned char rle[2] = { 0, 0 };
    uint16_t map[1] = { 0 };
    WorldFrameRecorder rec;
    rec.AddSprite(nullptr, 4, 4, 0, 0, 1, 1, map, map, nullptr, WFS_SOLID, 0);
    rec.AddSprite(rle, 0, 4, 0, 0, 1, 1, map, map, nullptr, WFS_SOLID, 0);
    rec.AddSprite(rle, 4, 4, 0, 0, 0, 1, map, map, nullptr, WFS_SOLID, 0);
    CHECK(rec.OpCount() == 0);
}

TEST_CASE("Reset clears ops, lookups and colour tables", "[kfx_platform][WorldFrameRecorder]") {
    unsigned char rle[2] = { 0, 0 };
    uint16_t map[1] = { 0 };
    uint32_t table[256] = {};
    WorldFrameRecorder rec;
    rec.AddSprite(rle, 1, 1, 0, 0, 1, 1, map, map, table, WFS_SOLID, 0);
    rec.Reset();
    WorldFrame f = rec.Build();
    CHECK(f.op_count == 0);
    CHECK(f.lookup_count == 0);
    CHECK(f.lut_rows == 0);
}

TEST_CASE("AddShadowTri copies the used mask rectangle at record time and keeps local UVs", "[kfx_platform][WorldFrameRecorder]") {
    static unsigned char mask[256 * 256];
    std::memset(mask, 0, sizeof(mask));
    for (int y = 10; y < 14; y++) for (int x = 20; x < 24; x++) mask[y * 256 + x] = 7; // non-zero = inside the shadow
    // Triangle covering mask texels u 20..24, v 10..14 (16.16 fixed point).
    struct PolyPoint a = { 100, 50, (int64_t)20 << 16, (int64_t)10 << 16, 0, 0 };
    struct PolyPoint b = { 110, 50, (int64_t)24 << 16, (int64_t)10 << 16, 0, 0 };
    struct PolyPoint c = { 100, 60, (int64_t)20 << 16, (int64_t)14 << 16, 0, 0 };

    WorldFrameRecorder rec;
    REQUIRE(rec.AddShadowTri(&a, &b, &c, mask, 16));
    std::memset(mask, 0, sizeof(mask)); // the engine reuses its scratch buffer: the copy must survive

    WorldFrame f = rec.Build();
    REQUIRE(f.op_count == 1);
    REQUIRE(f.ops[0].kind == WF_OP_SHADOW);
    const WorldFrameShadowOp& s = f.ops[0].u.shadow;
    CHECK(s.w == 5); CHECK(s.h == 5);       // ceil-inclusive rectangle around 20..24 x 10..14
    CHECK(s.x[1] == 110); CHECK(s.y[2] == 60);
    CHECK(s.u[0] == 0.0f); CHECK(s.u[1] == 4.0f); CHECK(s.v[2] == 4.0f); // relative to the cut-out's corner
    CHECK(s.shade == 16);
    REQUIRE(f.pixels != nullptr);
    CHECK(f.pixels[s.pix_off + 0 * s.w + 0] == 1);  // texel (20,10)
    CHECK(f.pixels[s.pix_off + 3 * s.w + 3] == 1);  // texel (23,13)
    CHECK(f.pixels[s.pix_off + 4 * s.w + 4] == 0);  // texel (24,14): outside the filled 4x4
}

TEST_CASE("AddShadowTri clamps shade and rejects masks that wrap or are missing", "[kfx_platform][WorldFrameRecorder]") {
    static unsigned char mask[256 * 256];
    std::memset(mask, 1, sizeof(mask));
    struct PolyPoint a = { 0, 0, (int64_t)1 << 16, (int64_t)1 << 16, 0, 0 };
    struct PolyPoint b = { 4, 0, (int64_t)5 << 16, (int64_t)1 << 16, 0, 0 };
    struct PolyPoint c = { 0, 4, (int64_t)1 << 16, (int64_t)5 << 16, 0, 0 };
    WorldFrameRecorder rec;
    CHECK(rec.AddShadowTri(&a, &b, &c, mask, 500));
    CHECK(rec.Build().ops[0].u.shadow.shade == 63);
    CHECK_FALSE(rec.AddShadowTri(&a, &b, &c, nullptr, 10));
    struct PolyPoint far = { 4, 0, ((int64_t)255 << 16) + 0x8000, (int64_t)1 << 16, 0, 0 }; // rounds up past the last mask column
    CHECK_FALSE(rec.AddShadowTri(&a, &far, &c, mask, 10));
}

// gpu-v2 Phase C.5: ops are stamped with the current depth; with the default
// monotone mode a later op can never be farther than an earlier one.
TEST_CASE("WorldFrameRecorder stamps depth and clamps it non-increasing by default", "[kfx_platform][WorldFrameRecorder]") {
    WorldFrameRecorder rec;
    unsigned char tex[32 * 256] = {};
    struct PolyPoint a = {0, 0, 0, 0, 0, 0}, b = {8, 0, 0, 0, 0, 0}, c = {0, 8, 0, 0, 0, 0};
    rec.SetDepth(0.75f); rec.AddPoly(&a, &b, &c, tex);
    rec.SetDepth(0.25f); rec.AddPoly(&a, &b, &c, tex);
    rec.SetDepth(0.5f);  rec.AddPoly(&a, &b, &c, tex);   // farther than the previous op: clamped
    WorldFrame f = rec.Build();
    REQUIRE(f.op_count == 3);
    CHECK(f.ops[0].depth == 0.75f);
    CHECK(f.ops[1].depth == 0.25f);
    CHECK(f.ops[2].depth == 0.25f);
    rec.Reset();
    rec.SetDepth(2.0f); rec.AddPoly(&a, &b, &c, tex);     // out-of-range input is clamped to 1
    CHECK(rec.Build().ops[0].depth == 1.0f);
}

TEST_CASE("WorldFrameRecorder keeps out-of-order depth when monotone mode is off", "[kfx_platform][WorldFrameRecorder]") {
    WorldFrameRecorder rec;
    rec.SetMonotoneDepth(false);
    unsigned char tex[32 * 256] = {};
    struct PolyPoint a = {0, 0, 0, 0, 0, 0}, b = {8, 0, 0, 0, 0, 0}, c = {0, 8, 0, 0, 0, 0};
    rec.SetDepth(0.2f); rec.AddPoly(&a, &b, &c, tex);
    rec.SetDepth(0.9f); rec.AddPoly(&a, &b, &c, tex);
    WorldFrame f = rec.Build();
    CHECK(f.ops[1].depth == 0.9f);
}

// gpu-v2 Phase C.5: the per-vertex depth mapping.
#include "renderer/WorldFrame.h"
TEST_CASE("worldframe_depth_from_view_z is monotone, clamped, and linear in 1/z", "[kfx_platform][WorldFrameRecorder]") {
    CHECK(worldframe_depth_from_view_z(0) == 0);
    CHECK(worldframe_depth_from_view_z(WORLDFRAME_DEPTH_NEAR_Z) == 0);
    CHECK(worldframe_depth_from_view_z(WORLDFRAME_DEPTH_FAR_Z) == WORLDFRAME_DEPTH_ONE);
    CHECK(worldframe_depth_from_view_z(WORLDFRAME_DEPTH_FAR_Z * 4) == WORLDFRAME_DEPTH_ONE);
    int64_t prev = -1;
    for (int64_t z = 33; z < WORLDFRAME_DEPTH_FAR_Z; z += 97)
    {
        const int64_t d = worldframe_depth_from_view_z(z);
        CHECK(d >= prev);
        prev = d;
    }
    // 1/z-linear: the depth of the point whose 1/z is midway between two others is their mean.
    const double z1 = 100.0, z2 = 1000.0, zm = 2.0 / (1.0 / z1 + 1.0 / z2);
    CHECK(worldframe_depth_from_view_z_f(zm) == Catch::Approx((worldframe_depth_from_view_z_f(z1) + worldframe_depth_from_view_z_f(z2)) / 2).margin(1e-9));
}

// gpu-v2 Phase C.5 lighting pass: per-frame lighting inputs travel with the frame.
TEST_CASE("WorldFrameRecorder carries per-pixel lighting inputs and clears them on Reset", "[kfx_platform][WorldFrameRecorder]") {
    WorldFrameRecorder rec;
    CHECK(rec.Build().lighting == nullptr);
    WorldFrameLighting l = {};
    l.light_count = WORLDFRAME_MAX_LIGHTS + 10;   // over the cap: clamped
    l.lens = 1000;
    const uint8_t grid[6] = { 255, 0, 255, 0, 255, 0 };
    rec.SetLighting(l, grid, 3, 2);
    WorldFrame f = rec.Build();
    REQUIRE(f.lighting != nullptr);
    CHECK(f.lighting->light_count == WORLDFRAME_MAX_LIGHTS);
    CHECK(f.lighting->lens == 1000);
    CHECK(f.lighting->grid_w == 3);
    CHECK(f.lighting->grid_h == 2);
    REQUIRE(f.lighting->grid != nullptr);
    CHECK(f.lighting->grid[2] == 255);
    rec.SetLighting(l, nullptr, 0, 0);            // no grid: dimensions dropped
    CHECK(rec.Build().lighting->grid == nullptr);
    rec.Reset();
    CHECK(rec.Build().lighting == nullptr);
}

// gpu-v2 (possession testing): translucent CPU draws can land on a not-opaque destination (the transparent
// world window above the GPU image); the ghost blends must then be the standard 'over' operator.
TEST_CASE("ghost blends are unchanged on an opaque destination and alpha-aware on a transparent one", "[kfx_platform][blend]") {
    const TbPixel ref = TbPixel_RGBA(255, 0, 0, 255);
    const TbPixel opaque = TbPixel_RGBA(0, 90, 30, 255);
    const TbPixel a = render_ghost_blend(ref, opaque);
    CHECK((int)a.r == (255 + 0) / 3);   // classic (ref + 2*dest) / 3
    CHECK((int)a.g == (0 + 180) / 3);
    CHECK((int)a.a == 255);
    const TbPixel b = render_ghost_blend_2(ref, opaque);
    CHECK((int)b.r == (510 + 0) / 3);   // classic (2*ref + dest) / 3
    CHECK((int)b.a == 255);

    const TbPixel clear = TbPixel_RGBA(0, 0, 0, 0);
    const TbPixel c = render_ghost_blend(ref, clear);   // weight 1/3 of ref over nothing: ref colour, alpha 85
    CHECK((int)c.r == 255); CHECK((int)c.g == 0); CHECK((int)c.a == 85);
    const TbPixel d = render_ghost_blend_2(ref, clear); // weight 2/3
    CHECK((int)d.r == 255); CHECK((int)d.a == 170);

    // Compositing that result over an opaque backdrop equals the classic blend on that backdrop.
    const int back_g = 90, back_r = 0;
    const int comp_r = (c.r * c.a + back_r * (255 - c.a)) / 255, comp_g = (c.g * c.a + back_g * (255 - c.a)) / 255;
    CHECK(std::abs(comp_r - a.r) <= 1);
    CHECK(std::abs(comp_g - a.g) <= 1);
}

// gpu-v2 lighting pass: visibility depth and lighting view depth are independent; only the former is clamped.
TEST_CASE("WorldFrameRecorder stamps view_depth independently of the clamped visibility depth", "[kfx_platform][WorldFrameRecorder]") {
    WorldFrameRecorder rec;
    unsigned char tex[32 * 256] = {};
    struct PolyPoint a = {0, 0, 0, 0, 0, 0}, b = {8, 0, 0, 0, 0, 0}, c = {0, 8, 0, 0, 0, 0};
    rec.SetDepth(0.25f, 0.9f);  rec.AddPoly(&a, &b, &c, tex);
    rec.SetDepth(0.75f, 0.4f);  rec.AddPoly(&a, &b, &c, tex);   // visibility clamps to 0.25; view depth is kept
    WorldFrame f = rec.Build();
    REQUIRE(f.op_count == 2);
    CHECK(f.ops[1].depth == 0.25f);
    CHECK(f.ops[0].view_depth == 0.9f);
    CHECK(f.ops[1].view_depth == 0.4f);
    rec.SetDepth(0.5f);                                          // single-argument form sets both
    rec.Reset(); rec.AddPoly(&a, &b, &c, tex);
    CHECK(rec.Build().ops[0].view_depth == 0.5f);
}

// gpu-v2 lighting pass: light colour travels with the recorded lights.
TEST_CASE("WorldFrameLighting carries per-light colour through the recorder", "[kfx_platform][WorldFrameRecorder]") {
    WorldFrameRecorder rec;
    WorldFrameLighting l = {};
    l.light_count = 2;
    l.lights[0] = { 1, 2, 3, 400, 20, 1.0f, 0.5f, 0.25f };
    l.lights[1] = { 5, 6, 7, 800, 40, 0.0f, 1.0f, 0.0f };
    rec.SetLighting(l, nullptr, 0, 0);
    WorldFrame f = rec.Build();
    REQUIRE(f.lighting != nullptr);
    CHECK(f.lighting->lights[0].g == 0.5f);
    CHECK(f.lighting->lights[0].b == 0.25f);
    CHECK(f.lighting->lights[1].g == 1.0f);
    CHECK(f.lighting->lights[1].r == 0.0f);
}
