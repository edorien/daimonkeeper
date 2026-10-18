// gpu3d_verify -- manual verification program for RendererGpu3D (gpu-v2 Phase C.1/C.2).
//
// NOT part of the Catch2 suite and not registered with ctest: it needs a real
// Vulkan device and a window (RendererManager_test.cpp's header explains why
// constructing a real backend is a boundary the automated suite declines).
// Build: cmake --build out/linux --target gpu3d_verify   (KFX_BUILD_TESTS=ON)
// Run:   SDL_VIDEODRIVER=offscreen out/linux/src/kfx_platform/tests/gpu3d_verify
//        (prefix with LD_PRELOAD=<system libwayland-client.so.0> on machines
//        where a driver package shadows it -- see scripts/run-keeperfx-vulkan.sh)
//
// It drives the real RendererGpu3D through Init()/SubmitWorldFrame()/PresentFrame(),
// captures each presented frame to a BMP, and checks pixels: terrain textures and
// their palette/in-place invalidation, view-window offset and clipping, CPU overlay
// compositing, underlay persistence, texture-cache eviction beyond capacity, every
// sprite mode (solid/scaled/flipped/remap/ghost1/ghost2/alpha/one-colour), painter's
// order between sprites and terrain, shadow masks, CPU-rasterizer-vs-GPU parity
// image comparisons, and terrain palette index 0 drawn opaque black like the CPU path. Prints VERIFICATION PASSED/FAILED; exit code 0 on pass.

#include "renderer/RendererGpu3D.h"
#include "renderer/RendererManager.h"
#include "ports/display_host_port.h"
#include "renderer/WorldFrameRecorder.h"
#include "renderer/GpuSpriteRenderer.h"
#include "bflib_video.h"
#include "bflib_vidsurface.h"
#include "bflib_vidraw.h"
#include "bflib_render.h"
#include "bflib_colour_transform.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <vector>
#include <cstring>
#include <cstdlib>

static TbBool t_ensure(SDL_Window*, SDL_Renderer*) { return 1; }
static struct DisplayHostPort cbs;

static unsigned char atlas[32 * 256];
// Test helper: record polys through the real recorder and return a WorldFrame view.
struct Rec { WorldFrameRecorder rec; WorldFrame frame; };
static WorldFrame mkframe(Rec& r, const WorldFramePolyItem* items, int n, int64_t vx, int64_t vy, int64_t vw, int64_t vh) {
    r.rec.Reset();
    for (int i = 0; i < n; i++) r.rec.AddPoly(&items[i].v0, &items[i].v1, &items[i].v2, items[i].texture);
    r.frame = r.rec.Build(); r.frame.view_x = vx; r.frame.view_y = vy; r.frame.view_w = vw; r.frame.view_h = vh;
    return r.frame;
}
static unsigned char palette_buf[768];

static bool px_is(SDL_Surface* s, int x, int y, int r, int g, int b, const char* what)
{
    Uint8 R, G, B, A; SDL_ReadSurfacePixel(s, x, y, &R, &G, &B, &A);
    bool ok = (abs(R - r) <= 1 && abs(G - g) <= 1 && abs(B - b) <= 1);
    printf("  %-34s (%d,%d) = (%d,%d,%d) expected (%d,%d,%d) %s\n", what, x, y, R, G, B, r, g, b, ok ? "ok" : "MISMATCH");
    return ok;
}

static SDL_Surface* present_and_capture(RendererGpu3D& r)
{
    r.ScheduleScreenshot("/tmp/gpu3d_verify.bmp", 2);
    r.PresentFrame();
    return SDL_LoadBMP("/tmp/gpu3d_verify.bmp");
}


// RLE-encode w*h texel indices the way sprites are stored: signed-byte runs, 0 ends a row.
static std::vector<unsigned char> rle_encode(int w, int h, const std::vector<unsigned char>& px) {
    std::vector<unsigned char> out;
    for (int y = 0; y < h; y++) {
        int x = 0;
        while (x < w) {
            int start = x; bool transparent = px[y*w+x] == 0;
            while (x < w && (px[y*w+x] == 0) == transparent && x - start < 127) x++;
            int len = x - start;
            if (transparent) out.push_back((unsigned char)(-len));
            else { out.push_back((unsigned char)len); for (int k = 0; k < len; k++) out.push_back(px[y*w+start+k]); }
        }
        out.push_back(0);
    }
    return out;
}

int main()
{
    SDL_setenv_unsafe("KFX_ALLOW_CPU_VULKAN", "1", 0); // CI and headless runs only have llvmpipe
    SDL_Init(SDL_INIT_VIDEO);
    const int W = 64, H = 64;
    lbWindow = SDL_CreateWindow("gpu3d-verify", W, H, SDL_WINDOW_HIDDEN);
    lbDrawSurface = SDL_CreateSurface(W, H, SDL_PIXELFORMAT_RGBA32);
    cbs = display_host_port_defaults;
    cbs.imgui_ensure = t_ensure;
    set_display_host_port(&cbs);
    lbDisplay.Palette = palette_buf;
    memset(lbDisplay.Palette, 0, 768);
    lbDisplay.Palette[3 * 5 + 0] = 63;                                    // index 5 = red
    lbDisplay.Palette[3 * 6 + 1] = 63;                                    // index 6 = green
    memset(atlas, 5, sizeof(atlas));

    RendererGpu3D r;
    if (!r.Init()) { printf("Init failed: %s\n", SDL_GetError()); return 1; }
    printf("WantsWorldFrame=%d\n", (int)r.WantsWorldFrame());
    bool ok = true;

    // Frame 1: CPU surface transparent with a green 10x10 "sprite" at (5,5); GPU red triangle over the upper-left half.
    auto cpu_frame = [&]() {
        r.ClearScreen(0);
        SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
        SDL_Rect sprite = { 5, 5, 10, 10 };
        SDL_FillSurfaceRect(lbDrawSurface, &sprite, SDL_MapSurfaceRGBA(lbDrawSurface, 0, 255, 0, 255));
    };
    const int64_t sh = (int64_t)32 << 16;
    WorldFramePolyItem item;
    item.v0 = { 0, 0, 0, 0, sh, 0 }; item.v1 = { W, 0, 0, 0, sh, 0 }; item.v2 = { 0, H, 0, 0, sh, 0 };
    item.texture = atlas;
    Rec R; WorldFrame f = mkframe(R, &item, 1, 0, 0, W, H);

    cpu_frame(); f = mkframe(R, &item, 1, f.view_x, f.view_y, f.view_w, f.view_h); r.SubmitWorldFrame(f);
    SDL_Surface* cap = present_and_capture(r);
    if (!cap) { printf("capture failed\n"); return 1; }
    printf("frame 1 (red block texture via palette index 5):\n");
    ok &= px_is(cap, 30, 10, 255, 0, 0, "GPU terrain, outside sprite");
    ok &= px_is(cap, 8, 8, 0, 255, 0, "CPU sprite over GPU terrain");
    ok &= px_is(cap, 55, 55, 0, 0, 0, "outside triangle (nothing drawn)");
    SDL_DestroySurface(cap);

    // Frame 2: palette entry 5 changes to blue -> the colour transform (or a re-expansion) shows it blue.
    memset(&lbDisplay.Palette[3 * 5], 0, 3); lbDisplay.Palette[3 * 5 + 2] = 63;
    cpu_frame(); f = mkframe(R, &item, 1, f.view_x, f.view_y, f.view_w, f.view_h); r.SubmitWorldFrame(f);
    cap = present_and_capture(r);
    printf("frame 2 (palette 5 -> blue):\n");
    ok &= px_is(cap, 30, 10, 0, 0, 255, "palette change applied");
    SDL_DestroySurface(cap);

    // Frame 3: same pointer, contents rewritten to index 6 -> green, once announced as a texture-set load does
    // (RendererWorldFrameTexturesChanged(); the cache no longer compares contents every frame).
    memset(atlas, 6, sizeof(atlas));
    r.WorldTexturesChanged();
    cpu_frame(); f = mkframe(R, &item, 1, f.view_x, f.view_y, f.view_w, f.view_h); r.SubmitWorldFrame(f);
    cap = present_and_capture(r);
    printf("frame 3 (block contents rewritten in place):\n");
    ok &= px_is(cap, 30, 10, 0, 255, 0, "in-place texture change re-uploaded");
    SDL_DestroySurface(cap);

    // Frame 4: view window offset+clip: window (32,0,32,32) -> triangle only inside that rect, offset applied.
    memset(atlas, 5, sizeof(atlas)); memset(&lbDisplay.Palette[3 * 5], 0, 3); lbDisplay.Palette[3 * 5] = 63;
    r.WorldTexturesChanged();
    f.view_x = 32; f.view_y = 0; f.view_w = 32; f.view_h = 32;
    cpu_frame(); f = mkframe(R, &item, 1, 32, 0, 32, 32); r.SubmitWorldFrame(f);
    cap = present_and_capture(r);
    printf("frame 4 (view window 32,0 32x32):\n");
    ok &= px_is(cap, 40, 5, 255, 0, 0, "inside window, inside triangle");
    ok &= px_is(cap, 20, 5, 0, 0, 0, "left of window: clipped");
    ok &= px_is(cap, 40, 40, 0, 0, 0, "below window: clipped");
    SDL_DestroySurface(cap);

    // Frame 5: no world frame submitted -> plain CPU present (no underlay).
    cpu_frame();
    cap = present_and_capture(r);
    printf("frame 5 (no GPU frame):\n");
    ok &= px_is(cap, 30, 10, 0, 0, 0, "CPU-only frame, no stale underlay");
    ok &= px_is(cap, 8, 8, 0, 255, 0, "CPU sprite still shown");
    SDL_DestroySurface(cap);



    // Frame 5b: a present with no ClearScreen and no new world frame (palette-fade step) keeps the underlay.
    cpu_frame(); f = mkframe(R, &item, 1, 0, 0, W, H); r.SubmitWorldFrame(f);
    cap = present_and_capture(r); SDL_DestroySurface(cap);
    cap = present_and_capture(r);
    printf("frame 5b (second present, no new frame):\n");
    ok &= px_is(cap, 30, 10, 255, 0, 0, "underlay persists across presents");
    SDL_DestroySurface(cap);

    // Frame 5c: > layer-count distinct blocks over several frames (fly-through) must never draw white.
    {
        static unsigned char big[256 * 32 * 200];
        const int total = 1600;
        for (int k = 0; k < total; k++) {
            unsigned char* b = big + (k / 8) * 32 * 256 + (k % 8) * 32;
            for (int y = 0; y < 32; y++) memset(b + y * 256, (k & 1) ? 5 : 6, 32);
        }
        bool evict_ok = true;
        for (int frame_i = 0; frame_i < 6; frame_i++) {
            static WorldFramePolyItem items[400];
            int first = frame_i * 301;
            for (int j = 0; j < 300; j++) {
                int k = first + j; if (k >= total) k = total - 1;
                items[j].v0 = { 0, 0, 0, 0, sh, 0 }; items[j].v1 = { W, 0, 0, 0, sh, 0 }; items[j].v2 = { 0, H, 0, 0, sh, 0 };
                items[j].texture = big + (k / 8) * 32 * 256 + (k % 8) * 32;
            }
            Rec RB; WorldFrame bf = mkframe(RB, items, 300, 0, 0, W, H);
            cpu_frame(); r.SubmitWorldFrame(bf);
            cap = present_and_capture(r);
            int lastk = first + 299; if (lastk >= total) lastk = total - 1;
            Uint8 pr, pg, pb, pa; SDL_ReadSurfacePixel(cap, 30, 10, &pr, &pg, &pb, &pa);
            bool red = (lastk & 1) != 0; bool good = red ? (pr > 250 && pg < 5 && pb < 5) : (pg > 250 && pr < 5 && pb < 5);
            evict_ok &= good; SDL_DestroySurface(cap);
        }
        printf("frame 5c (1800 distinct blocks over 6 frames, 1024-layer array): %s\n", evict_ok ? "ok, never white" : "MISMATCH");
        ok &= evict_ok;
    }


    // ---- C.2 sprites -------------------------------------------------------
    // Palette: 5 = red(63,0,0), 6 = green, 7 = blue, 2/1.. left black except alpha ramps below.
    lbDisplay.Palette[3*7+2] = 63;
    auto sprite_frame = [&](Rec& rr, std::initializer_list<int> order_unused){ (void)rr; (void)order_unused; };
    (void)sprite_frame;
    // 8x8 sprite: left 4 columns index 6 (green), right 4 columns index 7 (blue), bottom row transparent (0).
    std::vector<unsigned char> tex(64);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) tex[y*8+x] = (y == 7) ? 0 : (x < 4 ? 6 : 7);
    std::vector<unsigned char> rle = rle_encode(8, 8, tex);
    auto ident = [](int n) { std::vector<uint16_t> m(n); for (int i = 0; i < n; i++) m[i] = (uint16_t)i; return m; };
    auto scaled2 = [](int n) { std::vector<uint16_t> m(n*2); for (int i = 0; i < n*2; i++) m[i] = (uint16_t)(i/2); return m; };
    auto flipped = [](int n) { std::vector<uint16_t> m(n); for (int i = 0; i < n; i++) m[i] = (uint16_t)(n-1-i); return m; };

    auto run_frame = [&](WorldFrameRecorder& rec) {
        r.ClearScreen(0);
        SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0); // transparent, no CPU sprite in the way
        WorldFrame wf = rec.Build(); wf.view_w = W; wf.view_h = H;
        r.SubmitWorldFrame(wf);
        return present_and_capture(r);
    };
    memset(&lbDisplay.Palette[3*5], 0, 3); lbDisplay.Palette[3*5] = 63;
    printf("C.2 sprites:\n");
    {   // solid, 2x scale, at (20,20): 16x16 dst; texel (x<4)=green -> dst x 20..27 green, 28..35 blue; transparent row 7 -> dst y 34..35 shows terrain/black
        WorldFrameRecorder rec; auto xm = scaled2(8), ym = scaled2(8);
        rec.AddSprite(rle.data(), 8, 8, 20, 20, 16, 16, xm.data(), ym.data(), nullptr, WFS_SOLID, 0);
        cap = run_frame(rec);
        ok &= px_is(cap, 22, 22, 0, 255, 0, "solid 2x: left half green");
        ok &= px_is(cap, 30, 22, 0, 0, 255, "solid 2x: right half blue");
        ok &= px_is(cap, 22, 35, 0, 0, 0, "solid: transparent row untouched");
        ok &= px_is(cap, 19, 22, 0, 0, 0, "solid: outside rect untouched");
        SDL_DestroySurface(cap);
    }
    {   // flip: reversed xmap -> left half blue
        WorldFrameRecorder rec; auto xm = flipped(8), ym = ident(8);
        rec.AddSprite(rle.data(), 8, 8, 20, 20, 8, 8, xm.data(), ym.data(), nullptr, WFS_SOLID, 0);
        cap = run_frame(rec);
        ok &= px_is(cap, 22, 22, 0, 0, 255, "flipped: left half now blue");
        ok &= px_is(cap, 26, 22, 0, 255, 0, "flipped: right half now green");
        SDL_DestroySurface(cap);
    }
    {   // painter's order: sprite, then terrain triangle over it, then sprite over the terrain
        WorldFrameRecorder rec; auto xm = ident(8), ym = ident(8);
        struct PolyPoint a = { 10, 10, 0, 0, sh, 0 }, b = { 50, 10, 0, 0, sh, 0 }, c = { 10, 50, 0, 0, sh, 0 };
        rec.AddSprite(rle.data(), 8, 8, 12, 12, 8, 8, xm.data(), ym.data(), nullptr, WFS_SOLID, 0);   // under the wall
        rec.AddPoly(&a, &b, &c, atlas);                                                                 // red terrain covers it
        rec.AddSprite(rle.data(), 8, 8, 30, 12, 8, 8, xm.data(), ym.data(), nullptr, WFS_SOLID, 0);   // in front of the terrain
        cap = run_frame(rec);
        ok &= px_is(cap, 14, 14, 255, 0, 0, "sprite behind terrain is occluded");
        ok &= px_is(cap, 32, 14, 0, 255, 0, "sprite drawn after terrain shows");
        SDL_DestroySurface(cap);
    }
    {   // ghost1 over red terrain: (ref + 2*dest)/3 with ref = blue(0,0,255), dest red(255,0,0) -> (170,0,85)
        WorldFrameRecorder rec; auto xm = ident(8), ym = ident(8);
        struct PolyPoint a = { 0, 0, 0, 0, sh, 0 }, b = { W, 0, 0, 0, sh, 0 }, c = { 0, H, 0, 0, sh, 0 };
        rec.AddPoly(&a, &b, &c, atlas);
        rec.AddSprite(rle.data(), 8, 8, 12, 12, 8, 8, xm.data(), ym.data(), nullptr, WFS_GHOST1, 0);
        cap = run_frame(rec);
        ok &= px_is(cap, 18, 14, 170, 0, 85, "ghost1 blend over red");
        ok &= px_is(cap, 14, 14, 170, 85, 0, "ghost1 blend, green texel");
        SDL_DestroySurface(cap);
    }
    {   // ghost2: (2*ref + dest)/3 -> blue over red = (85,0,170)
        WorldFrameRecorder rec; auto xm = ident(8), ym = ident(8);
        struct PolyPoint a = { 0, 0, 0, 0, sh, 0 }, b = { W, 0, 0, 0, sh, 0 }, c = { 0, H, 0, 0, sh, 0 };
        rec.AddPoly(&a, &b, &c, atlas);
        rec.AddSprite(rle.data(), 8, 8, 12, 12, 8, 8, xm.data(), ym.data(), nullptr, WFS_GHOST2, 0);
        cap = run_frame(rec);
        ok &= px_is(cap, 18, 14, 85, 0, 170, "ghost2 blend over red");
        SDL_DestroySurface(cap);
    }
    {   // remap table: every colour -> (10,200,30)
        uint32_t table[256]; for (int i = 0; i < 256; i++) table[i] = 10u | (200u << 8) | (30u << 16) | (255u << 24);
        WorldFrameRecorder rec; auto xm = ident(8), ym = ident(8);
        rec.AddSprite(rle.data(), 8, 8, 20, 20, 8, 8, xm.data(), ym.data(), table, WFS_SOLID, 0);
        cap = run_frame(rec);
        ok &= px_is(cap, 22, 22, 10, 200, 30, "remap table colours the sprite");
        ok &= px_is(cap, 22, 27, 0, 0, 0, "remap: transparent texel still skipped");
        SDL_DestroySurface(cap);
    }
    {   // one-colour silhouette
        WorldFrameRecorder rec; auto xm = ident(8), ym = ident(8);
        rec.AddSprite(rle.data(), 8, 8, 20, 20, 8, 8, xm.data(), ym.data(), nullptr, WFS_ONECOLOUR, 200u | (100u << 8) | (50u << 16) | (255u << 24));
        cap = run_frame(rec);
        ok &= px_is(cap, 22, 22, 200, 100, 50, "one-colour silhouette");
        ok &= px_is(cap, 22, 27, 0, 0, 0, "one-colour: transparent texel skipped");
        SDL_DestroySurface(cap);
    }
    {   // alpha sprite: texel 2 = ramp 0 (white), step 1 -> +16 per channel over red terrain (255,0,0)->(255,16,16); texel 50 = ramp 6 (black) step 1 -> -2*255/63 = -8
        std::vector<unsigned char> t2(16, 2); for (int i = 8; i < 16; i++) t2[i] = 50;
        std::vector<unsigned char> r2 = rle_encode(8, 2, t2);
        WorldFrameRecorder rec; auto xm = ident(8), ym = ident(2);
        struct PolyPoint a = { 0, 0, 0, 0, sh, 0 }, b = { W, 0, 0, 0, sh, 0 }, c = { 0, H, 0, 0, sh, 0 };
        rec.AddPoly(&a, &b, &c, atlas);
        rec.AddSprite(r2.data(), 8, 2, 12, 12, 8, 2, xm.data(), ym.data(), nullptr, WFS_ALPHA, 0);
        cap = run_frame(rec);
        ok &= px_is(cap, 14, 12, 255, 16, 16, "alpha: white ramp adds");
        ok &= px_is(cap, 14, 13, 247, 0, 0, "alpha: black ramp subtracts");
        SDL_DestroySurface(cap);
    }

    {   // shadow: mask square (10..25) mapped 1:1 onto screen (20..35); shade 16 -> factor 0.5 over red terrain
        static unsigned char mask[256 * 256];
        memset(mask, 0, sizeof(mask));
        for (int y = 10; y < 26; y++) for (int x = 10; x < 26; x++) mask[y*256+x] = 1;
        WorldFrameRecorder rec;
        struct PolyPoint t0 = { 0, 0, 0, 0, sh, 0 }, t1 = { W, 0, 0, 0, sh, 0 }, t2 = { 0, H, 0, 0, sh, 0 };
        rec.AddPoly(&t0, &t1, &t2, atlas);
        // quad (20,20)-(36,36): U/V 10..26 in 16.16
        struct PolyPoint a = { 20, 20, (int64_t)10 << 16, (int64_t)10 << 16, 0, 0 }, b = { 36, 20, (int64_t)26 << 16, (int64_t)10 << 16, 0, 0 };
        struct PolyPoint c = { 36, 36, (int64_t)26 << 16, (int64_t)26 << 16, 0, 0 }, d = { 20, 36, (int64_t)10 << 16, (int64_t)26 << 16, 0, 0 };
        bool r1 = rec.AddShadowTri(&a, &b, &d, mask, 16), r2 = rec.AddShadowTri(&b, &c, &d, mask, 16);
        printf("  shadow tris recorded: %d %d\n", (int)r1, (int)r2);
        cap = run_frame(rec);
        ok &= px_is(cap, 28, 28, 128, 0, 0, "shadow darkens terrain (factor 0.5)");
        ok &= px_is(cap, 22, 22, 128, 0, 0, "shadow near corner");
        ok &= px_is(cap, 10, 10, 255, 0, 0, "outside shadow untouched");
        SDL_DestroySurface(cap);
    }
    {   // soft shadow (Phase 10): the same square as coverage at 2x -- full inside: as the mask; half: half as dark
        for (int half = 0; half < 2; half++) {
            static unsigned char cover[512 * 512];
            memset(cover, 0, sizeof(cover));
            for (int y = 20; y < 52; y++) for (int x = 20; x < 52; x++) cover[y * 512 + x] = half ? 128 : 255;
            WorldFrameRecorder rec;
            struct PolyPoint t0 = { 0, 0, 0, 0, sh, 0 }, t1 = { W, 0, 0, 0, sh, 0 }, t2 = { 0, H, 0, 0, sh, 0 };
            rec.AddPoly(&t0, &t1, &t2, atlas);
            struct PolyPoint a = { 20, 20, (int64_t)10 << 16, (int64_t)10 << 16, 0, 0 }, b = { 36, 20, (int64_t)26 << 16, (int64_t)10 << 16, 0, 0 };
            struct PolyPoint c = { 36, 36, (int64_t)26 << 16, (int64_t)26 << 16, 0, 0 }, d = { 20, 36, (int64_t)10 << 16, (int64_t)26 << 16, 0, 0 };
            bool r1 = rec.AddShadowTri(&a, &b, &d, cover, 16, 2, true), r2 = rec.AddShadowTri(&b, &c, &d, cover, 16, 2, true);
            ok &= r1 && r2;
            cap = run_frame(rec);
            if (!half) {
                ok &= px_is(cap, 28, 28, 128, 0, 0, "soft shadow, full cover: as the mask");
                ok &= px_is(cap, 10, 10, 255, 0, 0, "soft shadow: outside untouched");
            } else {
                ok &= px_is(cap, 28, 28, 191, 0, 0, "soft shadow, half cover: half as dark");
            }
            SDL_DestroySurface(cap);
        }
    }

    {   // Shadows drawn from their frame's texels (WF_OP_SHADOW_SPRITE, docs/plans/sprite-atlas-thrash.md Phase 2). Hard:
        // pixel for pixel what the mask path draws from the 256-wide buffer the engine would fill (the frame's texel
        // (sx, sy) at (base_x + sx, base_y + sy), mirrored at base_x - sx), plain and mirrored. The quad and its two
        // triangles as the engine makes them: P1 (u 0, v max), P2 (0, 0), P3 (max, 0), P4 (max, max).
        const int FW = 16, FH = 16;
        std::vector<unsigned char> fpx((size_t)FW * FH);
        for (int y = 0; y < FH; y++) for (int x = 0; x < FW; x++)
            fpx[(size_t)(y * FW + x)] = (x == 5 || (x > 9 && y < 4) || (x + y == 20)) ? 0 : (unsigned char)(1 + (x * 7 + y) % 200);
        std::vector<unsigned char> frle = rle_encode(FW, FH, fpx);
        struct PolyPoint p1 = { 20, 36, (int64_t)8 << 16, (int64_t)28 << 16, 0, 0 }, p2 = { 20, 20, (int64_t)8 << 16, (int64_t)8 << 16, 0, 0 };
        struct PolyPoint p3 = { 36, 20, (int64_t)28 << 16, (int64_t)8 << 16, 0, 0 }, p4 = { 36, 36, (int64_t)28 << 16, (int64_t)28 << 16, 0, 0 };
        const struct PolyPoint* const corners[4] = { &p1, &p2, &p3, &p4 };
        for (int flip = 0; flip < 2; flip++) {
            const int base_x = flip ? 25 : 10, base_y = 10;
            static unsigned char mask[256 * 256];
            memset(mask, 0, sizeof(mask));
            for (int y = 0; y < FH; y++) for (int x = 0; x < FW; x++)
                if (fpx[(size_t)(y * FW + x)]) mask[(base_y + y) * 256 + (flip ? base_x - x : base_x + x)] = 1;
            struct PolyPoint t0 = { 0, 0, 0, 0, sh, 0 }, t1 = { W, 0, 0, 0, sh, 0 }, t2 = { 0, H, 0, 0, sh, 0 };
            WorldFrameRecorder rm, rf;
            rm.AddPoly(&t0, &t1, &t2, atlas);
            rf.AddPoly(&t0, &t1, &t2, atlas);
            bool rec_ok = rm.AddShadowTri(&p1, &p2, &p3, mask, 16) && rm.AddShadowTri(&p1, &p3, &p4, mask, 16);
            WorldFrameShadowSpriteOp so = {};
            so.rle = frle.data(); so.src_w = FW; so.src_h = FH; so.base_x = base_x; so.base_y = base_y; so.flip = (uint32_t)flip;
            rec_ok &= rf.AddShadowSprite(corners, so, 16);
            SDL_Surface* cm = run_frame(rm);
            SDL_Surface* cf = run_frame(rf);
            int diffs = 0, dark = 0;
            for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
                Uint8 ar, ag, ab, aa, br, bg, bb, ba;
                SDL_ReadSurfacePixel(cm, x, y, &ar, &ag, &ab, &aa);
                SDL_ReadSurfacePixel(cf, x, y, &br, &bg, &bb, &ba);
                diffs += (ar != br || ag != bg || ab != bb);
                dark += (br < 200);
            }
            const bool same_px = rec_ok && diffs == 0 && dark > 100;
            printf("  %-44s %s (%d dark)\n", flip ? "shadow from frame, hard, mirrored" : "shadow from frame, hard", same_px ? "ok, as the mask" : "MISMATCH", dark);
            ok &= same_px;
            SDL_DestroySurface(cm);
            SDL_DestroySurface(cf);
        }
        // Soft, from a solid 16x16 frame's silhouette and from 2x HD texels (opaque inside): full cover in the middle
        // (factor 0.5 over red), part at the edge, none well outside.
        std::vector<unsigned char> solid((size_t)FW * FH, 9);
        std::vector<unsigned char> srle = rle_encode(FW, FH, solid);
        std::vector<TbPixel> shd((size_t)FW * 2 * FH * 2, TbPixel_RGBA(255, 255, 255, 255));
        for (int use_hd = 0; use_hd < 2; use_hd++) {
            WorldFrameRecorder rs;
            struct PolyPoint t0 = { 0, 0, 0, 0, sh, 0 }, t1 = { W, 0, 0, 0, sh, 0 }, t2 = { 0, H, 0, 0, sh, 0 };
            rs.AddPoly(&t0, &t1, &t2, atlas);
            WorldFrameShadowSpriteOp so = {};
            so.rle = srle.data(); so.src_w = FW; so.src_h = FH; so.base_x = 10; so.base_y = 10; so.soft = 1;
            if (use_hd) { so.hd_texels = shd.data(); so.hd_id = 777; so.hd_w = FW * 2; so.hd_h = FH * 2; so.hd_stride = FW * 2; so.hd_scale = 2; }
            const bool rec_ok = rs.AddShadowSprite(corners, so, 16);
            SDL_Surface* cs = run_frame(rs);
            Uint8 mr, mg, mb, ma, er, eg, eb, ea, orr, og, ob, oa;
            SDL_ReadSurfacePixel(cs, 28, 28, &mr, &mg, &mb, &ma); // the frame's middle
            SDL_ReadSurfacePixel(cs, 21, 28, &er, &eg, &eb, &ea); // its left edge (buffer u 10 is at x 21.6)
            SDL_ReadSurfacePixel(cs, 10, 10, &orr, &og, &ob, &oa); // outside
            const bool soft_ok = rec_ok && mr >= 126 && mr <= 130 && er > 130 && er < 250 && orr == 255;
            printf("  %-44s %s (middle %d, edge %d, outside %d)\n", use_hd ? "shadow from frame, soft, HD texels" : "shadow from frame, soft, silhouette",
                soft_ok ? "ok" : "MISMATCH", mr, er, orr);
            ok &= soft_ok;
            SDL_DestroySurface(cs);
        }
    }

    // Frame 6: CPU-rasterizer vs GPU parity on a patterned, UV-mapped, gouraud-shaded triangle.
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) atlas[y * 256 + x] = (((x / 4) + (y / 4)) & 1) ? 5 : 6;
    r.WorldTexturesChanged();
    memset(&lbDisplay.Palette[3 * 5], 0, 3); lbDisplay.Palette[3 * 5] = 63;
    WorldFramePolyItem pat;
    pat.v0 = { 4, 4, 0, 0, (int64_t)24 << 16, 0 };
    pat.v1 = { 60, 4, (int64_t)31 << 16, 0, (int64_t)40 << 16, 0 };
    pat.v2 = { 4, 60, 0, (int64_t)31 << 16, (int64_t)32 << 16, 0 };
    pat.texture = atlas;
    Rec RP; WorldFrame pf = mkframe(RP, &pat, 1, 0, 0, W, H);
    SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
    lbDisplay.WScreen = (TbPixel*)lbDrawSurface->pixels;
    setup_vecs((TbPixel*)lbDrawSurface->pixels, atlas, W, W, H);
    RendererSoftware sw;
    sw.SubmitWorldFrame(pf);   // the CPU reference rasterizer
    SDL_Surface* cpu_ref = SDL_DuplicateSurface(lbDrawSurface);
    SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
    r.SubmitWorldFrame(pf);
    cap = present_and_capture(r);
    long total = 0, close = 0, covered = 0; double sumdiff = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        Uint8 a[4], b[4];
        SDL_ReadSurfacePixel(cpu_ref, x, y, &a[0], &a[1], &a[2], &a[3]);
        SDL_ReadSurfacePixel(cap, x, y, &b[0], &b[1], &b[2], &b[3]);
        if (a[3] == 0 && a[0] == 0 && a[1] == 0 && a[2] == 0 && b[0] == 0 && b[1] == 0 && b[2] == 0) continue;
        covered++;
        int d = abs(a[0]-b[0]) + abs(a[1]-b[1]) + abs(a[2]-b[2]);
        sumdiff += d; if (d <= 24) close++;
        total++;
    }
    printf("frame 6 (CPU rasterizer vs GPU, textured+gouraud): covered=%ld within-tolerance=%ld mean|diff|=%.2f\n", covered, close, total ? sumdiff / total : 0.0);
    ok &= (total > 1000 && close * 100 >= covered * 90);
    SDL_SaveBMP(cpu_ref, "/tmp/gpu3d_verify_cpu.bmp"); SDL_SaveBMP(cap, "/tmp/gpu3d_verify_gpu.bmp");

    // Frame 7 (C.3): read-back of the world layer must equal what was presented,
    // for a sub-rect (dst laid out like the framebuffer), and be refused after ClearScreen().
    {
        std::vector<TbPixel> rb((size_t)W * H);
        memset(rb.data(), 0xAB, rb.size() * sizeof(TbPixel));
        bool got = r.ReadbackWorldLayer(rb.data(), W, 8, 6, 40, 30);
        long bad = 0, checked = 0;
        for (int y = 6; y < 36; y++) for (int x = 8; x < 48; x++) {
            Uint8 b[4]; SDL_ReadSurfacePixel(cap, x, y, &b[0], &b[1], &b[2], &b[3]);
            const TbPixel& p = rb[(size_t)y * W + x];
            checked++; if (abs(p.r - b[0]) > 1 || abs(p.g - b[1]) > 1 || abs(p.b - b[2]) > 1 || p.a != 255) bad++;
        }
        bool untouched = rb[0].r == 0xAB && rb[(size_t)W * H - 1].r == 0xAB;
        printf("frame 7 (read-back): returned=%d mismatching=%ld/%ld outside-rect-untouched=%d\n", (int)got, bad, checked, (int)untouched);
        ok &= got && bad == 0 && untouched;
        r.ClearScreen(0);
        bool refused = !r.ReadbackWorldLayer(rb.data(), W, 0, 0, 8, 8);
        printf("  refused after ClearScreen: %d\n", (int)refused);
        ok &= refused;
    }

    // Frame 8 (C.4): the draw surface changes size mid-session (video-mode change);
    // world target + underlay wrapper must follow it.
    {
        SDL_DestroySurface(lbDrawSurface);
        lbDrawSurface = SDL_CreateSurface(48, 40, SDL_PIXELFORMAT_RGBA32);
        memset(atlas, 5, sizeof(atlas));
        r.WorldTexturesChanged();
        memset(&lbDisplay.Palette[3 * 5], 0, 3); lbDisplay.Palette[3 * 5] = 63;
        r.ClearScreen(0);
        SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
        WorldFramePolyItem it2;
        it2.v0 = { 0, 0, 0, 0, sh, 0 }; it2.v1 = { 48, 0, 0, 0, sh, 0 }; it2.v2 = { 0, 40, 0, 0, sh, 0 };
        it2.texture = atlas;
        Rec R2; WorldFrame f2 = mkframe(R2, &it2, 1, 0, 0, 48, 40);
        r.SubmitWorldFrame(f2);
        SDL_Surface* c2 = present_and_capture(r);
        printf("frame 8 (resized 48x40):\n");
        if (!c2) { printf("  capture failed\n"); ok = false; }
        else {
            printf("  captured %dx%d\n", c2->w, c2->h);
            ok &= px_is(c2, 10, 8, 255, 0, 0, "terrain after resize");
            SDL_DestroySurface(c2);
        }

        // Frame 9 (C.5): a stretched 1x1 one-colour sprite (how SwCaptureRect records selection
        // lines/boxes) drawn before a nearer terrain triangle is hidden by it, visible elsewhere.
        static const unsigned char solid[3] = { 1, 1, 0 };
        static const uint16_t zeros[64] = {};
        WorldFrameRecorder rec9;
        struct PolyPoint b0 = { 0, 0, 0, 0, sh, 0 }, b1 = { 48, 0, 0, 0, sh, 0 }, b2 = { 0, 40, 0, 0, sh, 0 };
        rec9.AddPoly(&b0, &b1, &b2, atlas);
        rec9.AddSprite(solid, 1, 1, 8, 10, 20, 6, zeros, zeros, nullptr, WFS_ONECOLOUR, 0u | (0u << 8) | (255u << 16) | (255u << 24));
        struct PolyPoint n0 = { 0, 0, 0, 0, sh, 0 }, n1 = { 20, 0, 0, 0, sh, 0 }, n2 = { 0, 40, 0, 0, sh, 0 };
        rec9.AddPoly(&n0, &n1, &n2, atlas);
        r.ClearScreen(0);
        SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
        WorldFrame f9 = rec9.Build(); f9.view_x = 0; f9.view_y = 0; f9.view_w = 48; f9.view_h = 40;
        r.SubmitWorldFrame(f9);
        SDL_Surface* c9 = present_and_capture(r);
        printf("frame 9 (rect in painter's order; capture is the 48x40 frame scaled to the 64x64 window):\n");
        if (!c9) ok = false; else {
            ok &= px_is(c9, 32, 19, 0, 0, 255, "rect visible where not covered");
            ok &= px_is(c9, 13, 19, 255, 0, 0, "rect hidden by nearer terrain");
            ok &= px_is(c9, 27, 6, 255, 0, 0, "outside rect");
            SDL_DestroySurface(c9);
        }

        // Frame 10 (C.5 depth buffer): with monotone depth off the GPU depth test, not submission
        // order, decides visibility -- a *farther* red triangle submitted after a nearer green one
        // must not overwrite it, and a translucent-mode op must test against it too.
        static unsigned char atlas_green[32 * 256];
        memset(atlas_green, 6, sizeof(atlas_green));
        WorldFrameRecorder rec10; rec10.SetMonotoneDepth(false);
        rec10.SetDepth(0.2f);   // near: green
        struct PolyPoint g0 = { 0, 0, 0, 0, sh, 0 }, g1 = { 48, 0, 0, 0, sh, 0 }, g2 = { 0, 40, 0, 0, sh, 0 };
        rec10.AddPoly(&g0, &g1, &g2, atlas_green);
        rec10.SetDepth(0.8f);   // far, but recorded (drawn) later: red, would win under painter's order
        rec10.AddPoly(&g0, &g1, &g2, atlas);
        rec10.SetDepth(0.1f);   // nearer than everything: red again, must win
        struct PolyPoint h0 = { 0, 0, 0, 0, sh, 0 }, h1 = { 10, 0, 0, 0, sh, 0 }, h2 = { 0, 40, 0, 0, sh, 0 };
        rec10.AddPoly(&h0, &h1, &h2, atlas);
        r.ClearScreen(0);
        SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
        WorldFrame f10 = rec10.Build(); f10.view_x = 0; f10.view_y = 0; f10.view_w = 48; f10.view_h = 40;
        r.SubmitWorldFrame(f10);
        SDL_Surface* c10 = present_and_capture(r);
        printf("frame 10 (depth test, out-of-order submission):\n");
        if (!c10) ok = false; else {
            ok &= px_is(c10, 30, 6, 0, 255, 0, "farther later op rejected by depth test");
            ok &= px_is(c10, 4, 30, 255, 0, 0, "nearest op wins");
            SDL_DestroySurface(c10);
        }

        // Frame 11 (C.5 per-vertex depth): true_depth on. A green triangle at constant depth 0.5;
        // a red triangle drawn *after* it whose depth ramps 0.1 (x=0) .. 0.9 (x=48), so the two
        // intersect at x=24: red wins on the near (left) side, green on the far (right) side.
        const int64_t D = WORLDFRAME_DEPTH_ONE;
        WorldFrameRecorder rec11; rec11.SetMonotoneDepth(false);
        struct PolyPoint a0 = { 0, 0, 0, 0, sh, D / 2 }, a1 = { 48, 0, 0, 0, sh, D / 2 }, a2 = { 0, 40, 0, 0, sh, D / 2 };
        rec11.AddPoly(&a0, &a1, &a2, atlas_green);
        struct PolyPoint r0 = { 0, 0, 0, 0, sh, D / 10 }, r1 = { 48, 0, 0, 0, sh, D * 9 / 10 }, r2 = { 0, 40, 0, 0, sh, D / 10 };
        rec11.AddPoly(&r0, &r1, &r2, atlas);
        r.ClearScreen(0);
        SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
        WorldFrame f11 = rec11.Build(); f11.view_x = 0; f11.view_y = 0; f11.view_w = 48; f11.view_h = 40; f11.true_depth = 1;
        r.SubmitWorldFrame(f11);
        SDL_Surface* c11 = present_and_capture(r);
        printf("frame 11 (per-vertex depth, intersecting triangles):\n");
        if (!c11) ok = false; else {
            ok &= px_is(c11, 16, 2, 255, 0, 0, "near side of the intersection: red");
            ok &= px_is(c11, 53, 2, 0, 255, 0, "far side of the intersection: green");
            SDL_DestroySurface(c11);
        }

        // Frame 12 (C.5 lighting pass): a dim white terrain triangle (shade 8) with per-pixel lighting.
        // Perspective: lens 1000, centre (24,20); every vertex at view z = 1000 (depth from the
        // hyperbolic mapping), so map x/y = pixel - centre. One light at map (0,0), radius 30,
        // intensity 63, no solid columns: the pixel at the light is bright, one 40 px away is not.
        static unsigned char atlas_white[32 * 256];
        memset(atlas_white, 7, sizeof(atlas_white));
        lbDisplay.Palette[3 * 7 + 0] = lbDisplay.Palette[3 * 7 + 1] = lbDisplay.Palette[3 * 7 + 2] = 63;
        const int64_t zd = worldframe_depth_from_view_z(1000);
        const int64_t shade8 = (int64_t)8 << 16;
        WorldFrameRecorder rec12;
        struct PolyPoint l0 = { 0, 0, 0, 0, shade8, zd }, l1 = { 48, 0, 0, 0, shade8, zd }, l2 = { 0, 40, 0, 0, shade8, zd };
        struct PolyPoint l3 = { 48, 40, 0, 0, shade8, zd };
        rec12.AddPoly(&l0, &l1, &l2, atlas_white);
        rec12.AddPoly(&l1, &l3, &l2, atlas_white);
        WorldFrameLighting lt = {};
        lt.map_x[0] = 1.0; lt.map_x[3] = 0.0;   // map x = view x
        lt.map_y[1] = 1.0; lt.map_y[3] = 0.0;   // map y = view y (pixel y grows down, view y up: sign handled below)
        lt.lens = 1000; lt.centre_x = 24; lt.centre_y = 20;
        lt.fade_min = 1e9; lt.fade_max = 2e9; lt.fade_scaler = 3e9; lt.fade_range = 1e9; // no distance fade at z=1000
        lt.light_count = 1;
        lt.lights[0] = { 0.0f, 0.0f, 0.0f, 30.0f, 63.0f, 1.0f, 1.0f, 1.0f };
        static uint8_t grid[512 * 4];
        memset(grid, 0, sizeof(grid)); // no solid columns
        rec12.SetLighting(lt, grid, 512, 4);
        r.ClearScreen(0);
        SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
        WorldFrame f12 = rec12.Build(); f12.view_x = 0; f12.view_y = 0; f12.view_w = 48; f12.view_h = 40; f12.true_depth = 1;
        r.SubmitWorldFrame(f12);
        SDL_Surface* c12 = present_and_capture(r);
        printf("frame 12 (per-pixel lighting):\n");
        if (!c12) ok = false; else {
            Uint8 rr, gg, bb, aa;
            // window centre (24,20) -> map (0,0) = the light; capture is scaled 64/48 x 64/40
            SDL_ReadSurfacePixel(c12, 32, 32, &rr, &gg, &bb, &aa);
            Uint8 fr, fg, fb;
            SDL_ReadSurfacePixel(c12, 60, 32, &fr, &fg, &fb, &aa); // window (45,20): map (21,0), dist 21 of 30 -> partial light
            Uint8 dr, dg, db;
            SDL_ReadSurfacePixel(c12, 2, 2, &dr, &dg, &db, &aa);   // far corner: unlit
            printf("  at light: %d  near edge of radius: %d  far corner (unlit): %d\n", rr, fr, dr);
            ok &= (rr > 200 && dr < 80 && fr > dr + 30 && fr < rr - 30);
            SDL_DestroySurface(c12);
        }

        // Frame 12b: the same scene inside the engine's distance-fade band. The engine's fade_range is
        // (fade_max - fade_min) >> 8 -- units of 256 -- so the shader must scale it back; using it raw
        // blew the shade out to white (world "loses texturing") whenever terrain sat in the band, which
        // moves as the camera zooms. Band [800,1200] at view z 1000: fade_range = 400 >> 8 = 1.
        WorldFrameLighting ltf = lt;
        ltf.fade_min = 800; ltf.fade_max = 1200; ltf.fade_scaler = 1200; ltf.fade_range = (1200 - 800) >> 8;
        rec12.SetLighting(ltf, grid, 512, 4);
        r.ClearScreen(0);
        SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
        WorldFrame f12b = rec12.Build(); f12b.view_x = 0; f12b.view_y = 0; f12b.view_w = 48; f12b.view_h = 40; f12b.true_depth = 1;
        r.SubmitWorldFrame(f12b);
        SDL_Surface* c12b = present_and_capture(r);
        printf("frame 12b (per-pixel lighting inside the fade band):\n");
        if (!c12b) ok = false; else {
            Uint8 br, bg, bb, ba;
            SDL_ReadSurfacePixel(c12b, 60, 32, &br, &bg, &bb, &ba);
            printf("  partial-light pixel: %d (unfaded ~142; blown-out would be 255)\n", br);
            ok &= (br > 60 && br < 140);
            SDL_DestroySurface(c12b);
        }

        // Frame 13 (C.5 per-light shadow term): scale 64 map units per window pixel (lens 100, view z 6400),
        // window centre = map (1536,1536) = cell (6,6). Light at the centre, z 300, radius 1400. A solid
        // column of height 5 subtiles fills cell x=8 (map x 2048..2303) for every row. Pixels on the far
        // side of it are shadowed (static shade only); pixels at the same distance on the open side are lit.
        const int64_t zd2 = worldframe_depth_from_view_z(6400);
        WorldFrameRecorder rec13;
        struct PolyPoint m0 = { 0, 0, 0, 0, shade8, zd2 }, m1 = { 48, 0, 0, 0, shade8, zd2 }, m2 = { 0, 40, 0, 0, shade8, zd2 };
        struct PolyPoint m3 = { 48, 40, 0, 0, shade8, zd2 };
        rec13.AddPoly(&m0, &m1, &m2, atlas_white);
        rec13.AddPoly(&m1, &m3, &m2, atlas_white);
        WorldFrameLighting lt2 = {};
        lt2.map_x[0] = 1.0; lt2.map_x[3] = 1536.0;
        lt2.map_y[1] = 1.0; lt2.map_y[3] = 1536.0;
        lt2.lens = 100; lt2.centre_x = 24; lt2.centre_y = 20;
        lt2.fade_min = 1e9; lt2.fade_max = 2e9; lt2.fade_scaler = 3e9; lt2.fade_range = 1e9;
        lt2.light_count = 1;
        lt2.lights[0] = { 1536.0f, 1536.0f, 300.0f, 1400.0f, 63.0f, 1.0f, 1.0f, 1.0f };
        static uint8_t heights[512 * 16];
        memset(heights, 0, sizeof(heights));
        for (int y = 0; y < 16; y++) heights[y * 512 + 8] = 5;
        rec13.SetLighting(lt2, heights, 512, 16);
        r.ClearScreen(0);
        SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
        WorldFrame f13 = rec13.Build(); f13.view_x = 0; f13.view_y = 0; f13.view_w = 48; f13.view_h = 40; f13.true_depth = 1;
        r.SubmitWorldFrame(f13);
        SDL_Surface* c13 = present_and_capture(r);
        printf("frame 13 (shadow rays through solid columns):\n");
        if (!c13) ok = false; else {
            Uint8 lr, lg, lb, la, sr, sg, sb, or_, og, ob;
            SDL_ReadSurfacePixel(c13, 37, 27, &lr, &lg, &lb, &la);   // window (28,20): open cell between light and wall, lit
            SDL_ReadSurfacePixel(c13, 53, 27, &sr, &sg, &sb, &la);   // window (40,20): beyond the wall, shadowed
            SDL_ReadSurfacePixel(c13, 11, 27, &or_, &og, &ob, &la);  // window (8,20): same distance, open side, lit
            printf("  lit before wall: %d  behind wall: %d  open side: %d\n", lr, sr, or_);
            ok &= (lr > 200 && sr < 80 && or_ > 80);
            SDL_DestroySurface(c13);
        }

        // Frame 13b (soft shadows): the frame-13 scene with the wall only across cell rows 0..3 (map y < 1024).
        // Map y grows upward on screen, so the shadow sits at the bottom; its edge, seen from x = 40 (map x 2560), lies near window y = 36; with a light of finite width
        // the transition there must pass through intermediate brightness instead of jumping from dark to lit.
        {
            for (int y = 0; y < 16; y++) heights[y * 512 + 8] = (y < 4) ? 5 : 0;
            WorldFrameLighting lt3 = lt2;
            lt3.lights[0].radius = 4000.0f;   // wide enough that the lit side is bright at this distance
            rec13.SetLighting(lt3, heights, 512, 16);
            r.ClearScreen(0);
            SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
            WorldFrame f13b = rec13.Build(); f13b.view_x = 0; f13b.view_y = 0; f13b.view_w = 48; f13b.view_h = 40; f13b.true_depth = 1;
            r.SubmitWorldFrame(f13b);
            SDL_Surface* c13b = present_and_capture(r);
            printf("frame 13b (soft shadow edge):\n  column x=40, window y 28..44:");
            if (!c13b) ok = false; else {
                int v[17]; bool mid = false, mono = true;
                for (int y = 0; y < 17; y++) {
                    Uint8 pr, pg, pb, pa;
                    SDL_ReadSurfacePixel(c13b, 53, 44 + y, &pr, &pg, &pb, &pa);   // window -> the 4/3-scaled capture
                    v[y] = pr;
                    printf(" %d", pr);
                    if (pr > 100 && pr < 200) mid = true;
                    if (y > 0 && v[y] > v[y - 1] + 3) mono = false;
                }
                printf("\n");
                ok &= (v[0] > 200 && v[16] < 90 && mid && mono);
                SDL_DestroySurface(c13b);
            }
        }

        // Frame 14 (overlay pass): a translucent (ghost1) sprite drawn AFTER the world frame is
        // blended against the GPU image, not against the transparent CPU layer. Red terrain, then a
        // green ghost1 rect: (ref + 2*dest)/3 = (85,85,0)... per channel (0+2*255)/3=170, (255+0)/3=85.
        memset(atlas, 5, sizeof(atlas));
        r.WorldTexturesChanged();
        memset(&lbDisplay.Palette[3 * 5], 0, 3); lbDisplay.Palette[3 * 5] = 63;     // index 5 red
        memset(&lbDisplay.Palette[3 * 6], 0, 3); lbDisplay.Palette[3 * 6 + 1] = 63; // index 6 green
        WorldFrameRecorder rec14;
        rec14.AddPoly(&b0, &b1, &b2, atlas);
        rec14.AddPoly(&a1, &a2, &b1, atlas);
        r.ClearScreen(0);
        SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
        WorldFrame f14 = rec14.Build(); f14.view_x = 0; f14.view_y = 0; f14.view_w = 48; f14.view_h = 40;
        r.SubmitWorldFrame(f14);
        static const unsigned char green_px[3] = { 1, 6, 0 };
        WorldFrameRecorder ov; ov.SetDepth(0.0f);
        ov.AddSprite(green_px, 1, 1, 8, 8, 20, 10, zeros, zeros, nullptr, WFS_GHOST1, 0);
        WorldFrame fo = ov.Build(); fo.view_x = 0; fo.view_y = 0; fo.view_w = 48; fo.view_h = 40; fo.overlay = 1;
        r.SubmitWorldFrame(fo);
        SDL_Surface* c14 = present_and_capture(r);
        printf("frame 14 (overlay ghost sprite over the GPU image):\n");
        if (!c14) ok = false; else {
            ok &= px_is(c14, 20, 16, 170, 85, 0, "ghost1 blended against GPU terrain");
            ok &= px_is(c14, 50, 6, 255, 0, 0, "outside the overlay: terrain unchanged");
            SDL_DestroySurface(c14);
        }

        // Frame 15 (lit sprites): the frame-12 lighting scene (lens 1000, view z 1000, one light at the
        // window centre, radius 30, intensity 63, no solid columns), with a dim terrain and a white sprite
        // (palette 7, base shade 8) covering the whole window. The sprite's pixels near the light are
        // brighter than far from it; a WFS_SOLID sprite (no lighting) is uniform.
        WorldFrameRecorder rec15;
        rec15.SetMonotoneDepth(false);
        rec15.SetDepth(static_cast<float>(worldframe_depth_from_view_z_f(1000)));
        static const unsigned char white_px[3] = { 1, 7, 0 };
        rec15.AddSprite(white_px, 1, 1, 0, 0, 48, 40, zeros, zeros, nullptr, WFS_LIT, (uint32_t)(8 * 256));
        rec15.SetLighting(lt, grid, 512, 4);
        r.ClearScreen(0);
        SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
        WorldFrame f15 = rec15.Build(); f15.view_x = 0; f15.view_y = 0; f15.view_w = 48; f15.view_h = 40; f15.true_depth = 1;
        r.SubmitWorldFrame(f15);
        SDL_Surface* c15 = present_and_capture(r);
        printf("frame 15 (lit sprite):\n");
        if (!c15) ok = false; else {
            Uint8 ar, ag, ab, aa, fr2, fg2, fb2, mr, mg, mb;
            SDL_ReadSurfacePixel(c15, 32, 32, &ar, &ag, &ab, &aa);   // at the light
            SDL_ReadSurfacePixel(c15, 60, 32, &mr, &mg, &mb, &aa);   // dist 21 of 30
            SDL_ReadSurfacePixel(c15, 2, 2, &fr2, &fg2, &fb2, &aa);  // far corner
            printf("  sprite at light: %d  partial: %d  far: %d\n", ar, mr, fr2);
            ok &= (ar > 200 && fr2 < 80 && mr > fr2 + 30 && mr < ar - 30);
            SDL_DestroySurface(c15);
        }

        // Frame 16 (coloured lights): the frame-12 scene again, but the light is orange (1, 0.5, 0.1) and the
        // terrain white. Near the light the pixel is tinted (r > g > b); far from it there is no tint.
        Uint8 aa0;
        WorldFrameRecorder rec16;
        struct PolyPoint q0 = { 0, 0, 0, 0, shade8, zd }, q1 = { 48, 0, 0, 0, shade8, zd }, q2 = { 0, 40, 0, 0, shade8, zd }, q3 = { 48, 40, 0, 0, shade8, zd };
        rec16.AddPoly(&q0, &q1, &q2, atlas_white);
        rec16.AddPoly(&q1, &q3, &q2, atlas_white);
        WorldFrameLighting lc = lt;
        lc.lights[0] = { 0.0f, 0.0f, 0.0f, 30.0f, 28.0f, 1.0f, 0.5f, 0.1f };
        rec16.SetLighting(lc, grid, 512, 4);
        r.ClearScreen(0);
        SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
        WorldFrame f16 = rec16.Build(); f16.view_x = 0; f16.view_y = 0; f16.view_w = 48; f16.view_h = 40; f16.true_depth = 1;
        r.SubmitWorldFrame(f16);
        SDL_Surface* c16 = present_and_capture(r);
        printf("frame 16 (coloured light):\n");
        if (!c16) ok = false; else {
            Uint8 nr, ng, nb, xr, xg, xb;
            SDL_ReadSurfacePixel(c16, 34, 32, &nr, &ng, &nb, &aa0);  // near the light
            SDL_ReadSurfacePixel(c16, 2, 2, &xr, &xg, &xb, &aa0);    // far corner: static shade only
            printf("  near light: (%d,%d,%d)  far: (%d,%d,%d)\n", nr, ng, nb, xr, xg, xb);
            ok &= (nr > ng + 40 && ng > nb + 30 && xr == xg && xg == xb);
            SDL_DestroySurface(c16);
        }

        // Frame 17 (lit tinted sprite): a WFS_LIT sprite keeps its colour table (a pale-blue tint: every
        // palette entry -> (100,150,255)) and is lit on top: base shade 8 far from the light = 0.25 x tint,
        // near the light brighter, and still blue-dominant throughout.
        uint32_t tint[256]; for (int i = 0; i < 256; i++) tint[i] = 100u | (150u << 8) | (255u << 16) | (255u << 24);
        WorldFrameRecorder rec17;
        rec17.SetMonotoneDepth(false);
        rec17.SetDepth(static_cast<float>(worldframe_depth_from_view_z_f(1000)));
        rec17.AddSprite(white_px, 1, 1, 0, 0, 48, 40, zeros, zeros, tint, WFS_LIT, (uint32_t)(8 * 256));
        rec17.SetLighting(lt, grid, 512, 4);
        r.ClearScreen(0);
        SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
        WorldFrame f17 = rec17.Build(); f17.view_x = 0; f17.view_y = 0; f17.view_w = 48; f17.view_h = 40; f17.true_depth = 1;
        r.SubmitWorldFrame(f17);
        SDL_Surface* c17 = present_and_capture(r);
        printf("frame 17 (lit tinted sprite):\n");
        if (!c17) ok = false; else {
            Uint8 tr, tg, tb, ur, ug, ub;
            SDL_ReadSurfacePixel(c17, 2, 2, &ur, &ug, &ub, &aa0);    // far: tint * 0.25
            SDL_ReadSurfacePixel(c17, 60, 32, &tr, &tg, &tb, &aa0);  // partial light
            printf("  far: (%d,%d,%d)  lit: (%d,%d,%d)\n", ur, ug, ub, tr, tg, tb);
            ok &= (std::abs(ur - 25) <= 2 && std::abs(ug - 37) <= 2 && std::abs(ub - 64) <= 2 && tb > ub && tb > tr + 40);
            SDL_DestroySurface(c17);
        }

        // Frame 18 (terrain palette index 0): terrain blocks use index 0 as art -- palette entry 0,
        // black (library bookshelves, workshop/temple reliefs) -- and the CPU rasterizer writes it like
        // any other texel, so it shows black. A wall block, left half index 0 and right half red, drawn
        // over a farther green floor (upper-left half of the window) with a blue clear colour: the
        // index-0 half must be black over both the floor and the clear colour, and match the CPU path.
        static unsigned char atlas_zero[32 * 256];
        for (int y = 0; y < 32; y++) for (int x = 0; x < 256; x++) atlas_zero[y * 256 + x] = (x < 16) ? 0 : 5;
        memset(&lbDisplay.Palette[3 * 5], 0, 3); lbDisplay.Palette[3 * 5] = 63;
        WorldFramePolyItem z18[3];
        z18[0].v0 = { 0, 0, 0, 0, sh, 0 }; z18[0].v1 = { 48, 0, 0, 0, sh, 0 }; z18[0].v2 = { 0, 40, 0, 0, sh, 0 };
        z18[0].texture = atlas_green;   // far floor
        const int64_t U32 = (int64_t)32 << 16;
        z18[1].v0 = { 0, 0, 0, 0, sh, 0 }; z18[1].v1 = { 48, 0, U32, 0, sh, 0 }; z18[1].v2 = { 0, 40, 0, U32, sh, 0 };
        z18[2].v0 = { 48, 0, U32, 0, sh, 0 }; z18[2].v1 = { 48, 40, U32, U32, sh, 0 }; z18[2].v2 = { 0, 40, 0, U32, sh, 0 };
        z18[1].texture = z18[2].texture = atlas_zero;   // nearer wall
        Rec R18; WorldFrame f18 = mkframe(R18, z18, 3, 0, 0, 48, 40);
        f18.clear_r = 0; f18.clear_g = 0; f18.clear_b = 255;
        // CPU reference: the same ops through the CPU rasterizer onto the blue-cleared view.
        SDL_FillSurfaceRect(lbDrawSurface, nullptr, SDL_MapSurfaceRGBA(lbDrawSurface, 0, 0, 255, 255));
        lbDisplay.WScreen = (TbPixel*)lbDrawSurface->pixels;
        setup_vecs((TbPixel*)lbDrawSurface->pixels, atlas_zero, 48, 48, 40);
        sw.SubmitWorldFrame(f18);
        SDL_Surface* cpu18 = SDL_DuplicateSurface(lbDrawSurface);
        r.ClearScreen(0);
        SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
        r.SubmitWorldFrame(f18);
        SDL_Surface* c18 = present_and_capture(r);
        printf("frame 18 (terrain index 0 = opaque black; capture is the 48x40 frame scaled to 64x64):\n");
        if (!c18) ok = false; else {
            ok &= px_is(c18, 8, 8, 0, 0, 0, "index 0 over a farther floor");
            ok &= px_is(c18, 20, 50, 0, 0, 0, "index 0 over the clear colour");
            ok &= px_is(c18, 50, 30, 255, 0, 0, "index 5 beside it");
            // Whole-view parity: the CPU pixel as presented (straight alpha over black) vs the GPU.
            long bad = 0;
            for (int y = 0; y < 40; y++) for (int x = 0; x < 48; x++) {
                Uint8 a[4], b[4];
                SDL_ReadSurfacePixel(cpu18, x, y, &a[0], &a[1], &a[2], &a[3]);
                SDL_ReadSurfacePixel(c18, (2 * x + 1) * 64 / 96, (2 * y + 1) * 64 / 80, &b[0], &b[1], &b[2], &b[3]);
                int d = 0; for (int k = 0; k < 3; k++) d += abs(a[k] * a[3] / 255 - b[k]);
                if (d > 24) bad++;
            }
            printf("  CPU-vs-GPU mismatching pixels: %ld/%d\n", bad, 48 * 40);
            ok &= (bad <= 40); // at most the texel-boundary column (x=24), where the CPU's fixed-point U step rounds differently
            SDL_SaveBMP(cpu18, "/tmp/gpu3d_verify_index0_cpu.bmp"); SDL_SaveBMP(c18, "/tmp/gpu3d_verify_index0_gpu.bmp");
            SDL_DestroySurface(c18);
        }
        SDL_DestroySurface(cpu18);

        // Frame 19 (upload ring overflow; texture resolution Phase 1): twelve new blocks in one frame through
        // a 4-layer upload ring -- the renderer submits each full batch and refills the ring mid-frame. Every
        // block must show its own colour, none white.
        static unsigned char atlas_ring[12][32 * 256];
        for (int k = 0; k < 12; k++) {
            memset(atlas_ring[k], 20 + k, sizeof(atlas_ring[k]));
            lbDisplay.Palette[3 * (20 + k) + 0] = (unsigned char)(5 * k);
            lbDisplay.Palette[3 * (20 + k) + 1] = (unsigned char)(63 - 5 * k);
            lbDisplay.Palette[3 * (20 + k) + 2] = (unsigned char)(2 * k + 10);
        }
        WorldFramePolyItem ring[12];
        for (int k = 0; k < 12; k++) {
            const int64_t x0 = (k % 4) * 12, y0 = (k / 4) * 13;
            ring[k].v0 = { x0, y0, 0, 0, sh, 0 }; ring[k].v1 = { x0 + 12, y0, 0, 0, sh, 0 }; ring[k].v2 = { x0, y0 + 13, 0, 0, sh, 0 };
            ring[k].texture = atlas_ring[k];
        }
        r.SetUploadRingLayersForTesting(4);
        Rec R19; WorldFrame f19 = mkframe(R19, ring, 12, 0, 0, 48, 40);
        r.ClearScreen(0);
        SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
        r.SubmitWorldFrame(f19);
        SDL_Surface* c19 = present_and_capture(r);
        printf("frame 19 (12 new blocks through a 4-layer upload ring):\n");
        if (!c19) ok = false; else {
            int good = 0;
            for (int k = 0; k < 12; k++) {
                const int x = (k % 4) * 12 + 2, y = (k / 4) * 13 + 2;
                Uint8 pr, pg, pb, pa;
                SDL_ReadSurfacePixel(c19, (2 * x + 1) * 64 / 96, (2 * y + 1) * 64 / 80, &pr, &pg, &pb, &pa);
                const int er = (5 * k) * 255 / 63, eg = (63 - 5 * k) * 255 / 63, eb = (2 * k + 10) * 255 / 63;
                if (abs(pr - er) <= 2 && abs(pg - eg) <= 2 && abs(pb - eb) <= 2) good++;
                else printf("  block %d: (%d,%d,%d) expected (%d,%d,%d)\n", k, pr, pg, pb, er, eg, eb);
            }
            printf("  %d of 12 blocks in their own colour\n", good);
            ok &= (good == 12);
            SDL_DestroySurface(c19);
        }
        r.SetUploadRingLayersForTesting(0xFFFFFFFFu);

        // Frame 20 (palette effects as a colour transform): the same twelve blocks, already cached, under a
        // per-channel fade (every entry at half brightness: curves) and then the white palette (every entry
        // white). Each block must show the colour the new palette gives it -- with no re-upload -- and the
        // index-0 half of frame 18's wall must stay black under white.
        unsigned char saved_pal[768];
        memcpy(saved_pal, lbDisplay.Palette, sizeof(saved_pal));
        struct WorldFrameTextureStats ts0, ts1;
        r.GetWorldTextureStats(&ts0);
        for (int i = 0; i < 768; i++) lbDisplay.Palette[i] = (unsigned char)(saved_pal[i] / 2);
        r.ClearScreen(0);
        SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
        Rec R20; WorldFrame f20 = mkframe(R20, ring, 12, 0, 0, 48, 40);
        r.SubmitWorldFrame(f20);
        SDL_Surface* c20 = present_and_capture(r);
        r.GetWorldTextureStats(&ts1);
        printf("frame 20 (half-brightness palette: curves, no re-upload; then white with index 0):\n");
        if (!c20) ok = false; else {
            int good = 0;
            for (int k = 0; k < 12; k++) {
                const int x = (k % 4) * 12 + 2, y = (k / 4) * 13 + 2;
                Uint8 pr, pg, pb, pa;
                SDL_ReadSurfacePixel(c20, (2 * x + 1) * 64 / 96, (2 * y + 1) * 64 / 80, &pr, &pg, &pb, &pa);
                const int er = ((5 * k) / 2) * 255 / 63, eg = ((63 - 5 * k) / 2) * 255 / 63, eb = ((2 * k + 10) / 2) * 255 / 63;
                if (abs(pr - er) <= 2 && abs(pg - eg) <= 2 && abs(pb - eb) <= 2) good++;
                else printf("  block %d: (%d,%d,%d) expected (%d,%d,%d)\n", k, pr, pg, pb, er, eg, eb);
            }
            printf("  %d of 12 blocks faded; uploads during the frame: %llu, flushes: %llu\n", good,
                (unsigned long long)(ts1.uploads - ts0.uploads), (unsigned long long)(ts1.palette_flushes - ts0.palette_flushes));
            ok &= (good == 12) && (ts1.uploads == ts0.uploads) && (ts1.palette_flushes == ts0.palette_flushes);
            SDL_DestroySurface(c20);
        }
        memset(lbDisplay.Palette, 63, 768); // white
        r.ClearScreen(0);
        SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
        Rec R20w; WorldFrame f20w = mkframe(R20w, z18, 3, 0, 0, 48, 40);
        r.SubmitWorldFrame(f20w);
        SDL_Surface* c20w = present_and_capture(r);
        if (!c20w) ok = false; else {
            ok &= px_is(c20w, 8, 8, 0, 0, 0, "index 0 stays black under the white palette");
            ok &= px_is(c20w, 50, 30, 255, 255, 255, "index 5 white under the white palette");
            SDL_DestroySurface(c20w);
        }
        memcpy(lbDisplay.Palette, saved_pal, sizeof(saved_pal));

        // Frame 21 (TEXTURE_FILTER, texture resolution Phase 2). A block whose column 0 is red, column 31 blue
        // and the rest green:
        // (a) Bilinear over the whole block: at its right edge the clamp keeps blue, no red from column 0
        //     wrapping in (REPEAT would blend it: the hairline);
        // (b) Bilinear, scrolled to U 16..48 (a liquid): it still wraps, so red shows around U = 32;
        // (c) magnified to 12 pixels a texel (columns alternating red and green): Sharp shows a texel's own colour
        //     3 pixels inside it, Bilinear a blend there.
        static unsigned char atlas_edges[32 * 256];
        static unsigned char atlas_stripes[32 * 256];
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 256; x++) {
                atlas_edges[y * 256 + x] = (x == 0) ? 40 : ((x == 31) ? 41 : 42);
                atlas_stripes[y * 256 + x] = (x & 1) ? 42 : 40;
            }
        r.WorldTexturesChanged();
        memset(&lbDisplay.Palette[3 * 40], 0, 9);
        lbDisplay.Palette[3 * 40 + 0] = 63; // 40 red
        lbDisplay.Palette[3 * 41 + 2] = 63; // 41 blue
        lbDisplay.Palette[3 * 42 + 1] = 63; // 42 green
        auto quad = [&](WorldFramePolyItem* q, unsigned char* block, int64_t u0, int64_t u1) {
            const int64_t v1 = 0x1FFFFF;
            q[0].v0 = { 0, 0, u0, 0, sh, 0 }; q[0].v1 = { 48, 0, u1, 0, sh, 0 }; q[0].v2 = { 0, 40, u0, v1, sh, 0 };
            q[1].v0 = { 48, 0, u1, 0, sh, 0 }; q[1].v1 = { 48, 40, u1, v1, sh, 0 }; q[1].v2 = { 0, 40, u0, v1, sh, 0 };
            q[0].texture = q[1].texture = block;
        };
        auto draw21 = [&](WorldFramePolyItem* q, int filter) -> SDL_Surface* {
            RendererSetTextureFilter(filter);
            Rec R21; WorldFrame f21 = mkframe(R21, q, 2, 0, 0, 48, 40);
            r.ClearScreen(0);
            SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
            r.SubmitWorldFrame(f21);
            return present_and_capture(r);
        };
        auto read = [](SDL_Surface* c, int x, int y, Uint8* px) {
            Uint8 a; SDL_ReadSurfacePixel(c, (2 * x + 1) * 64 / 96, (2 * y + 1) * 64 / 80, &px[0], &px[1], &px[2], &a);
        };
        printf("frame 21 (texture filters: clamp at a block's edge, wrap when scrolled, Sharp vs Bilinear):\n");
        WorldFramePolyItem q21[2];
        Uint8 px[3];
        quad(q21, atlas_edges, 0, 0x1FFFFF);
        SDL_Surface* c21a = draw21(q21, RENDERER_TEXFILTER_BILINEAR);
        if (!c21a) ok = false; else {
            read(c21a, 47, 20, px);
            printf("  (a) right edge, clamped: (%d,%d,%d)\n", px[0], px[1], px[2]);
            ok &= (px[0] < 16) && (px[2] > 100);
            SDL_DestroySurface(c21a);
        }
        quad(q21, atlas_edges, (int64_t)16 << 16, (int64_t)48 << 16);
        SDL_Surface* c21b = draw21(q21, RENDERER_TEXFILTER_BILINEAR);
        if (!c21b) ok = false; else {
            read(c21b, 24, 20, px);
            printf("  (b) scrolled across U = 32, wraps: (%d,%d,%d)\n", px[0], px[1], px[2]);
            ok &= (px[0] > 60);
            SDL_DestroySurface(c21b);
        }
        quad(q21, atlas_stripes, 0, (int64_t)4 << 16);
        SDL_Surface* c21s = draw21(q21, RENDERER_TEXFILTER_SHARP);
        SDL_Surface* c21l = draw21(q21, RENDERER_TEXFILTER_BILINEAR);
        if (!c21s || !c21l) ok = false; else {
            Uint8 ps[3], pl[3];
            read(c21s, 15, 20, ps);
            read(c21l, 15, 20, pl);
            printf("  (c) 3 px inside a texel: Sharp (%d,%d,%d), Bilinear (%d,%d,%d)\n", ps[0], ps[1], ps[2], pl[0], pl[1], pl[2]);
            const bool sharp_pure = ((ps[0] > 250 && ps[1] < 5) || (ps[1] > 250 && ps[0] < 5));
            const bool bilinear_blend = (pl[0] > 20 && pl[1] > 20);
            ok &= sharp_pure && bilinear_blend;
        }
        if (c21s) SDL_DestroySurface(c21s);
        if (c21l) SDL_DestroySurface(c21l);
        RendererSetTextureFilter(RENDERER_TEXFILTER_NEAREST);
        memcpy(lbDisplay.Palette, saved_pal, sizeof(saved_pal));

        // Frame 22 (true-colour HD blocks, texture resolution Phase 3), Nearest, reference palette = the active one:
        // (a) a 128x128 HD block whose texels alternate red/green column by column (finer than a 32 texel), drawn in
        //     precision mode 3 pixels an HD texel, shows its own detail; in the same frame a 32x32 block (columns
        //     alternating blue/green) shares the now 128-texel array, nearest-upscaled, as before;
        // (b) parity: an HD block that is the x4 upscale of that 32x32 block, drawn in parity mode, gives the
        //     32x32 block's image pixel for pixel (a slanted triangle, fractional UVs);
        // (c) a half-brightness palette reaches the HD block through the colour transform: no upload, no flush.
        lbDisplay.Palette[3 * 50 + 0] = 63; lbDisplay.Palette[3 * 50 + 1] = 0;  lbDisplay.Palette[3 * 50 + 2] = 0;  // red
        lbDisplay.Palette[3 * 51 + 0] = 0;  lbDisplay.Palette[3 * 51 + 1] = 63; lbDisplay.Palette[3 * 51 + 2] = 0;  // green
        lbDisplay.Palette[3 * 52 + 0] = 0;  lbDisplay.Palette[3 * 52 + 1] = 0;  lbDisplay.Palette[3 * 52 + 2] = 63; // blue
        unsigned char ref_pal[768];
        memcpy(ref_pal, lbDisplay.Palette, sizeof(ref_pal));
        RendererSetHdReferencePalette(ref_pal);
        static unsigned char atlas_bg[32 * 256];
        static TbPixel hd_fine[128 * 256];
        static TbPixel hd_parity[128 * 256];
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 256; x++)
                atlas_bg[y * 256 + x] = (x & 1) ? 51 : 52;
        for (int y = 0; y < 128; y++)
            for (int x = 0; x < 256; x++) {
                hd_fine[y * 256 + x] = expand_indexed_pixel((x & 1) ? 51 : 50, ref_pal);
                hd_parity[y * 256 + x] = expand_indexed_pixel(atlas_bg[(y >> 2) * 256 + (x >> 2)], ref_pal);
            }
        auto hd_frame = [&](Rec& rec22, const WorldFramePolyItem* q, int n) -> SDL_Surface* {
            rec22.rec.Reset();
            for (int i = 0; i < n; i++)
                rec22.rec.AddPoly(&q[i].v0, &q[i].v1, &q[i].v2, q[i].texture, q[i].block_shift, q[i].parity, q[i].texture_rgba);
            rec22.frame = rec22.rec.Build();
            rec22.frame.view_x = 0; rec22.frame.view_y = 0; rec22.frame.view_w = 48; rec22.frame.view_h = 40;
            r.ClearScreen(0);
            SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
            r.SubmitWorldFrame(rec22.frame);
            return present_and_capture(r);
        };
        printf("frame 22 (HD blocks: own detail, 32x32 beside them, parity, palette fade through the transform):\n");
        // (a) top half: HD, U 0..4 (16 HD texels) across 48 pixels; bottom half: the 32x32 block, U 0..4 likewise.
        WorldFramePolyItem q22[4] = {};
        // V spans 4 texels too: magnified both ways, so the hardware stays at the finest level now that there are mips.
        const int64_t u4 = (int64_t)4 << 16, vb = (int64_t)4 << 16;
        q22[0].v0 = { 0, 0, 0, 0, sh, 0 };   q22[0].v1 = { 48, 0, u4, 0, sh, 0 };  q22[0].v2 = { 0, 20, 0, vb, sh, 0 };
        q22[1].v0 = { 48, 0, u4, 0, sh, 0 }; q22[1].v1 = { 48, 20, u4, vb, sh, 0 }; q22[1].v2 = { 0, 20, 0, vb, sh, 0 };
        for (int i = 0; i < 2; i++) { q22[i].texture = atlas_bg; q22[i].texture_rgba = hd_fine; q22[i].block_shift = 2; q22[i].parity = 0; }
        q22[2].v0 = { 0, 20, 0, 0, sh, 0 };   q22[2].v1 = { 48, 20, u4, 0, sh, 0 };  q22[2].v2 = { 0, 40, 0, vb, sh, 0 };
        q22[3].v0 = { 48, 20, u4, 0, sh, 0 }; q22[3].v1 = { 48, 40, u4, vb, sh, 0 }; q22[3].v2 = { 0, 40, 0, vb, sh, 0 };
        q22[2].texture = q22[3].texture = atlas_bg;
        Rec R22a;
        SDL_Surface* c22a = hd_frame(R22a, q22, 4);
        if (!c22a) ok = false; else {
            Uint8 p0[3], p1[3], s0[3], s1[3];
            read(c22a, 1, 8, p0); read(c22a, 4, 8, p1);    // HD texels 0 and 1
            read(c22a, 5, 30, s0); read(c22a, 17, 30, s1); // 32-res texels 0 and 1
            printf("  (a) HD texels 0,1: (%d,%d,%d) (%d,%d,%d); 32x32 texels 0,1: (%d,%d,%d) (%d,%d,%d)\n",
                p0[0], p0[1], p0[2], p1[0], p1[1], p1[2], s0[0], s0[1], s0[2], s1[0], s1[1], s1[2]);
            ok &= (p0[0] > 250 && p0[1] < 5) && (p1[1] > 250 && p1[0] < 5);
            ok &= (s0[2] > 250 && s0[1] < 5) && (s1[1] > 250 && s1[2] < 5);
            SDL_DestroySurface(c22a);
        }
        // (b) one slanted triangle, fractional UVs: the 32x32 block, then its x4 upscale in parity mode.
        WorldFramePolyItem t22 = {};
        t22.v0 = { 2, 1, 0x18000, 0x6000, sh, 0 }; t22.v1 = { 46, 9, 0x1C4000, 0x3A000, sh, 0 }; t22.v2 = { 7, 39, 0x2A000, 0x1E8000, sh, 0 };
        t22.texture = atlas_bg;
        Rec R22b, R22c;
        SDL_Surface* c22b = hd_frame(R22b, &t22, 1);
        t22.texture_rgba = hd_parity; t22.block_shift = 2; t22.parity = 1;
        SDL_Surface* c22c = hd_frame(R22c, &t22, 1);
        if (!c22b || !c22c) ok = false; else {
            long diff = 0, drawn = 0;
            for (int y = 0; y < c22b->h; y++)
                for (int x = 0; x < c22b->w; x++) {
                    Uint8 ar, ag, ab, aa, br, bg, bb, ba;
                    SDL_ReadSurfacePixel(c22b, x, y, &ar, &ag, &ab, &aa);
                    SDL_ReadSurfacePixel(c22c, x, y, &br, &bg, &bb, &ba);
                    if (ar != br || ag != bg || ab != bb) diff++;
                    if (ag != 0 || ab != 0) drawn++;
                }
            printf("  (b) parity HD vs its 32x32 original: %ld differing pixels of %ld drawn\n", diff, drawn);
            ok &= (diff == 0) && (drawn > 500);
        }
        if (c22b) SDL_DestroySurface(c22b);
        if (c22c) SDL_DestroySurface(c22c);
        // (c) half brightness: the HD block's red and green at half, through the transform alone.
        if (SDL_Surface* warm = hd_frame(R22a, q22, 4)) SDL_DestroySurface(warm); // both blocks cached under the reference palette
        struct WorldFrameTextureStats ts22a, ts22b;
        r.GetWorldTextureStats(&ts22a);
        for (int i = 0; i < 768; i++) lbDisplay.Palette[i] = (unsigned char)(ref_pal[i] / 2);
        SDL_Surface* c22d = hd_frame(R22a, q22, 4);
        r.GetWorldTextureStats(&ts22b);
        if (!c22d) ok = false; else {
            Uint8 p0[3], p1[3];
            read(c22d, 1, 8, p0); read(c22d, 4, 8, p1);
            const int half = 31 * 255 / 63;
            printf("  (c) half brightness, HD texels 0,1: (%d,%d,%d) (%d,%d,%d), expected %d; uploads %llu, flushes %llu\n",
                p0[0], p0[1], p0[2], p1[0], p1[1], p1[2], half,
                (unsigned long long)(ts22b.uploads - ts22a.uploads), (unsigned long long)(ts22b.palette_flushes - ts22a.palette_flushes));
            ok &= (abs(p0[0] - half) <= 2 && p0[1] < 5) && (abs(p1[1] - half) <= 2 && p1[0] < 5);
            ok &= (ts22b.uploads == ts22a.uploads) && (ts22b.palette_flushes == ts22a.palette_flushes);
            SDL_DestroySurface(c22d);
        }
        memcpy(lbDisplay.Palette, ref_pal, sizeof(ref_pal));

        // Frame 23 (mip levels, texture resolution Phase 4), Nearest: a 128 HD block whose texels alternate red and
        // green column by column, its original 32x32 block all blue (use_original_at_32):
        // (a) 96 HD texels over 48 x 40 pixels (2 to 2.4 texels a pixel: level 1): the 64 level, a red/green blend;
        // (b) 128 over 24 x 24 (5.3: level 2): the 32 level, which is the original block: blue;
        // (c) TEXTURE_MIPMAPS on, 128 over 3 x 3: a level below 32, the original's colour still.
        static TbPixel hd_cols[128 * 256];
        static unsigned char atlas_blue[32 * 256];
        for (int y = 0; y < 128; y++)
            for (int x = 0; x < 256; x++)
                hd_cols[y * 256 + x] = expand_indexed_pixel((x & 1) ? 51 : 50, ref_pal);
        memset(atlas_blue, 52, sizeof(atlas_blue));
        // u: the texels (32-res) the quad spans each way, so the minification is the same both ways.
        auto mip_quad = [&](WorldFramePolyItem* q, int64_t w, int64_t h, int64_t texels) {
            const int64_t u = (texels << 16) - 1;
            q[0] = {}; q[1] = {};
            q[0].v0 = { 0, 0, 0, 0, sh, 0 }; q[0].v1 = { w, 0, u, 0, sh, 0 }; q[0].v2 = { 0, h, 0, u, sh, 0 };
            q[1].v0 = { w, 0, u, 0, sh, 0 }; q[1].v1 = { w, h, u, u, sh, 0 }; q[1].v2 = { 0, h, 0, u, sh, 0 };
            for (int i = 0; i < 2; i++) {
                q[i].texture = atlas_blue; q[i].texture_rgba = hd_cols; q[i].block_shift = 2; q[i].parity = 0; q[i].original_at_32 = 1;
            }
        };
        auto mip_frame = [&](Rec& rec23, const WorldFramePolyItem* q) -> SDL_Surface* {
            rec23.rec.Reset();
            for (int i = 0; i < 2; i++)
                rec23.rec.AddPoly(&q[i].v0, &q[i].v1, &q[i].v2, q[i].texture, q[i].block_shift, q[i].parity, q[i].texture_rgba,
                    q[i].original_at_32);
            rec23.frame = rec23.rec.Build();
            rec23.frame.view_x = 0; rec23.frame.view_y = 0; rec23.frame.view_w = 48; rec23.frame.view_h = 40;
            r.ClearScreen(0);
            SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
            r.SubmitWorldFrame(rec23.frame);
            return present_and_capture(r);
        };
        printf("frame 23 (mip levels: 64 level blended, 32 level the original, below 32 with TEXTURE_MIPMAPS):\n");
        WorldFramePolyItem q23[2];
        Rec R23;
        mip_quad(q23, 48, 40, 24);
        SDL_Surface* c23a = mip_frame(R23, q23);
        mip_quad(q23, 24, 24, 32);
        SDL_Surface* c23b = mip_frame(R23, q23);
        RendererSetTextureMipmaps(1);
        mip_quad(q23, 3, 3, 32);
        SDL_Surface* c23c = mip_frame(R23, q23);
        RendererSetTextureMipmaps(0);
        if (!c23a || !c23b || !c23c) ok = false; else {
            Uint8 pa[3], pb[3], pc[3];
            read(c23a, 20, 20, pa);
            read(c23b, 10, 10, pb);
            read(c23c, 1, 1, pc);
            printf("  (a) 64 level: (%d,%d,%d); (b) 32 level: (%d,%d,%d); (c) below 32: (%d,%d,%d)\n",
                pa[0], pa[1], pa[2], pb[0], pb[1], pb[2], pc[0], pc[1], pc[2]);
            ok &= (pa[0] > 60) && (pa[1] > 60) && (pa[2] < 10);
            ok &= (pb[2] > 240) && (pb[0] < 10) && (pb[1] < 10);
            ok &= (pc[2] > 240) && (pc[0] < 10) && (pc[1] < 10);
        }
        if (c23a) SDL_DestroySurface(c23a);
        if (c23b) SDL_DestroySurface(c23b);
        if (c23c) SDL_DestroySurface(c23c);

        RendererSetHdReferencePalette(nullptr);
        memcpy(lbDisplay.Palette, saved_pal, sizeof(saved_pal));
    }

    // Frame 24 (HD sprites, docs/refactor/sprites/ Phase 4): an HD op (an x2 upscale of an 8x8 sprite, its lookups the
    // middle HD texel of each original texel) draws exactly what its indexed twin draws -- solid, both ghosts, a
    // shade remap as a curve row, and under a palette only the transform's table reproduces (channels rotated); and
    // HD ops too big for the HD atlas together draw their originals instead (the fallback), still the same.
    {
        unsigned char saved_pal[768];
        memcpy(saved_pal, lbDisplay.Palette, sizeof(saved_pal));
        unsigned char ref_pal[768];
        {   // pseudo-random (as bflib_colour_transform_test.cpp's): rotating its channels is no per-channel function
            uint32_t seed = 12345;
            for (int i = 0; i < 768; i++) { seed = seed * 1103515245u + 12345u; ref_pal[i] = (unsigned char)((seed >> 16) & 63); }
            ref_pal[0] = ref_pal[1] = ref_pal[2] = 0;
            for (int i = 1; i < 256; i++) if ((ref_pal[3*i] | ref_pal[3*i+1] | ref_pal[3*i+2]) == 0) ref_pal[3*i] = 1;
        }
        memcpy(lbDisplay.Palette, ref_pal, sizeof(ref_pal));
        // 8x8, varied indices, a transparent column and row.
        std::vector<unsigned char> px(64);
        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) px[y*8+x] = (x == 3 || y == 6) ? 0 : (unsigned char)(1 + (x * 29 + y * 53) % 250);
        std::vector<unsigned char> rle8 = rle_encode(8, 8, px);
        const int S = 2;
        std::vector<TbPixel> hd((size_t)64 * S * S);
        for (int y = 0; y < 8 * S; y++) for (int x = 0; x < 8 * S; x++) {
            const unsigned char i = px[(y / S) * 8 + x / S];
            hd[(size_t)(y * 8 * S + x)] = i ? resolve_indexed_pixel(i, ref_pal) : TbPixel_Transparent;
        }
        // 40 x 32 destination: the original's texel per pixel (x5 wide, x4 tall), and the HD texel in its middle.
        std::vector<uint16_t> xm(40), ym(32), hxm(40), hym(32);
        // HD lookups are the texel's centre in sixteenths (HD_MAP_SUBTEXELS).
        for (int i = 0; i < 40; i++) { xm[i] = (uint16_t)(i / 5); hxm[i] = (uint16_t)((xm[i] * S + (S - 1) / 2) * HD_MAP_SUBTEXELS + HD_MAP_SUBTEXELS / 2); }
        for (int i = 0; i < 32; i++) { ym[i] = (uint16_t)(i / 4); hym[i] = (uint16_t)((ym[i] * S + (S - 1) / 2) * HD_MAP_SUBTEXELS + HD_MAP_SUBTEXELS / 2); }
        auto curve_row = [](const SpriteRemapOp* op, const ColourTransform* ct, uint32_t row[256]) {
            uint8_t fc[3][256];
            SpriteRemapCurves(op, fc);
            const bool curves = ct != nullptr && ct->mode == CTM_Curves;
            for (int v = 0; v < 256; v++) {
                const uint8_t rr = curves ? ct->curves[0][v] : (uint8_t)v, gg = curves ? ct->curves[1][v] : (uint8_t)v, bb = curves ? ct->curves[2][v] : (uint8_t)v;
                row[v] = (uint32_t)fc[0][rr] | ((uint32_t)fc[1][gg] << 8) | ((uint32_t)fc[2][bb] << 16) | (0xFFu << 24);
            }
            for (int c = 0; c < 3; c++) row[c] = (row[c] & 0x00FFFFFFu) | ((uint32_t)fc[c][0] << 24);
        };
        auto shot = [&](WorldFrameRecorder& rec) {
            r.ClearScreen(0);
            SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
            WorldFrame wf = rec.Build(); wf.view_w = W; wf.view_h = H;
            r.SubmitWorldFrame(wf);
            return present_and_capture(r);
        };
        auto same = [](SDL_Surface* a, SDL_Surface* b) {
            if (a == nullptr || b == nullptr) return false;
            int diffs = 0;
            for (int y = 0; y < a->h; y++) for (int x = 0; x < a->w; x++) {
                Uint8 ar, ag, ab, aa, br, bg, bb, ba;
                SDL_ReadSurfacePixel(a, x, y, &ar, &ag, &ab, &aa);
                SDL_ReadSurfacePixel(b, x, y, &br, &bg, &bb, &ba);
                if (ar != br || ag != bg || ab != bb) diffs++;
            }
            return diffs == 0;
        };
        printf("frame 24 (HD sprites):\n");
        struct SpriteRemapOp none = {};
        uint64_t next_id = 1;
        auto compare = [&](const char* what, uint32_t mode, const uint32_t* index_cmap, const SpriteRemapOp* op, uint32_t table) {
            WorldFrameRecorder ri, rh;
            ri.AddSprite(rle8.data(), 8, 8, 10, 12, 40, 32, xm.data(), ym.data(), index_cmap, mode, 0);
            uint32_t row[256];
            curve_row(op, RendererHdColourTransform(), row);
            rh.AddSpriteHd(hd.data(), nullptr, next_id++, 8 * S, 8 * S, 8 * S, S, rle8.data(), 8, 8, 10, 12, 40, 32, hxm.data(), hym.data(), row, table,
                index_cmap, mode, 0);
            SDL_Surface* a = shot(ri);
            SDL_Surface* b = shot(rh);
            const bool eq = same(a, b);
            printf("  %-44s %s\n", what, eq ? "ok, identical" : "MISMATCH");
            if (a) SDL_DestroySurface(a);
            if (b) SDL_DestroySurface(b);
            return eq;
        };
        ok &= compare("solid", WFS_SOLID, nullptr, &none, 0);
        ok &= compare("ghost 1", WFS_GHOST1, nullptr, &none, 0);
        ok &= compare("ghost 2", WFS_GHOST2, nullptr, &none, 0);
        SetupSpriteRemapShade(20);
        uint32_t shade_cmap[256];
        for (int i = 0; i < 256; i++) shade_cmap[i] = (uint32_t)lbSpriteRemapTable[i].r | ((uint32_t)lbSpriteRemapTable[i].g << 8) | ((uint32_t)lbSpriteRemapTable[i].b << 16) | ((uint32_t)lbSpriteRemapTable[i].a << 24);
        ok &= compare("shade remap as a curve row", WFS_SOLID, shade_cmap, &lbSpriteRemapOp, 0);
        // Per-pixel lit (WFS_LIT; frame 15's light): a plainly shaded sprite (its colours, the GPU shading them) and a
        // tinted one (the tint kept, lit on top).
        {
            WorldFrameLighting lit = {};
            lit.map_x[0] = 1.0; lit.map_y[1] = 1.0;
            lit.lens = 1000; lit.centre_x = 24; lit.centre_y = 20;
            lit.fade_min = 1e9; lit.fade_max = 2e9; lit.fade_scaler = 3e9; lit.fade_range = 1e9;
            lit.light_count = 1;
            lit.lights[0] = { 0.0f, 0.0f, 0.0f, 30.0f, 63.0f, 1.0f, 1.0f, 1.0f };
            static uint8_t lit_grid[512 * 4];
            memset(lit_grid, 0, sizeof(lit_grid));
            auto compare_lit = [&](const char* what, const uint32_t* index_cmap, const SpriteRemapOp* op) {
                WorldFrameRecorder ri, rh;
                for (WorldFrameRecorder* rr : { &ri, &rh }) {
                    rr->SetMonotoneDepth(false);
                    rr->SetDepth(static_cast<float>(worldframe_depth_from_view_z_f(1000)));
                }
                ri.AddSprite(rle8.data(), 8, 8, 4, 4, 40, 32, xm.data(), ym.data(), index_cmap, WFS_LIT, (uint32_t)(8 * 256));
                uint32_t row[256];
                curve_row(op, RendererHdColourTransform(), row);
                rh.AddSpriteHd(hd.data(), nullptr, next_id++, 8 * S, 8 * S, 8 * S, S, rle8.data(), 8, 8, 4, 4, 40, 32, hxm.data(), hym.data(), row, 0,
                    index_cmap, WFS_LIT, (uint32_t)(8 * 256));
                SDL_Surface* shots[2] = {};
                WorldFrameRecorder* recs[2] = { &ri, &rh };
                for (int k = 0; k < 2; k++) {
                    recs[k]->SetLighting(lit, lit_grid, 512, 4);
                    r.ClearScreen(0);
                    SDL_FillSurfaceRect(lbDrawSurface, nullptr, 0);
                    WorldFrame wf = recs[k]->Build(); wf.view_x = 0; wf.view_y = 0; wf.view_w = 48; wf.view_h = 40; wf.true_depth = 1;
                    r.SubmitWorldFrame(wf);
                    shots[k] = present_and_capture(r);
                }
                const bool eq = same(shots[0], shots[1]);
                printf("  %-44s %s\n", what, eq ? "ok, identical" : "MISMATCH");
                for (SDL_Surface* c : shots) if (c) SDL_DestroySurface(c);
                return eq;
            };
            ok &= compare_lit("lit, plain colours", nullptr, &none);
            SetupSpriteRemapGhost(77, SPRITE_TINT_STRONG);
            uint32_t tint_cmap[256];
            for (int i = 0; i < 256; i++) tint_cmap[i] = (uint32_t)lbSpriteRemapTable[i].r | ((uint32_t)lbSpriteRemapTable[i].g << 8) | ((uint32_t)lbSpriteRemapTable[i].b << 16) | ((uint32_t)lbSpriteRemapTable[i].a << 24);
            ok &= compare_lit("lit, tinted", tint_cmap, &lbSpriteRemapOp);
        }
        // The active palette's channels rotated: the HD texels (reference colours) go through the transform's table.
        RendererSetHdReferencePalette(ref_pal);
        for (int i = 1; i < 256; i++) {
            lbDisplay.Palette[3*i] = ref_pal[3*i+1]; lbDisplay.Palette[3*i+1] = ref_pal[3*i+2]; lbDisplay.Palette[3*i+2] = ref_pal[3*i];
        }
        const ColourTransform* ct = RendererHdColourTransform();
        printf("  (transform mode %d, 2 = table)\n", ct ? (int)ct->mode : -1);
        ok &= (ct != nullptr && ct->mode == CTM_Table);
        ok &= compare("rotated palette through the table", WFS_SOLID, nullptr, &none, 1);
        // Additive art (Phase 9): an alpha-ramp sprite (every ramp, the subtracting black one too) over a grey ground,
        // and its HD version (each texel's ramp delta as add / sub texels): the two passes draw the same.
        {
            static const int ramp_deltas[8][3] = { { 4, 4, 4 }, { 6, 4, 0 }, { 6, 1, 1 }, { 2, 2, 6 }, { 2, 6, 2 }, { 3, 0, 3 },
                { -2, -2, -2 }, { 6, 3, 1 } };
            std::vector<unsigned char> apx(64);
            for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) apx[y*8+x] = (x == 3 || y == 6) ? 0 : (unsigned char)(1 + (x * 29 + y * 53) % 64);
            std::vector<unsigned char> arle = rle_encode(8, 8, apx);
            std::vector<TbPixel> add((size_t)64 * S * S, TbPixel_Transparent), sub((size_t)64 * S * S, TbPixel_Transparent);
            for (int y = 0; y < 8 * S; y++) for (int x = 0; x < 8 * S; x++) {
                const int i = apx[(y / S) * 8 + x / S];
                if (i < 1 || i > 64) continue;
                int pa[3], pm[3];
                for (int c = 0; c < 3; c++) {
                    const int d = ramp_deltas[(i - 1) / 8][c] * ((i - 1) % 8) * 255 / 63;
                    pa[c] = d > 0 ? d : 0; pm[c] = d < 0 ? -d : 0;
                }
                add[(size_t)(y * 8 * S + x)] = TbPixel_RGBA((uint8_t)pa[0], (uint8_t)pa[1], (uint8_t)pa[2], 255);
                sub[(size_t)(y * 8 * S + x)] = TbPixel_RGBA((uint8_t)pm[0], (uint8_t)pm[1], (uint8_t)pm[2], 255);
            }
            std::vector<unsigned char> gpx((size_t)8 * 8, 140);
            std::vector<unsigned char> grle = rle_encode(8, 8, gpx);
            uint32_t row[256];
            curve_row(&none, nullptr, row);
            WorldFrameRecorder ri, rh;
            const uint32_t grey = 120u | (110u << 8) | (100u << 16) | (255u << 24);
            for (WorldFrameRecorder* rr : { &ri, &rh })
                rr->AddSprite(grle.data(), 8, 8, 10, 12, 40, 32, xm.data(), ym.data(), nullptr, WFS_ONECOLOUR, grey);
            ri.AddSprite(arle.data(), 8, 8, 10, 12, 40, 32, xm.data(), ym.data(), nullptr, WFS_ALPHA, 0);
            rh.AddSpriteHd(add.data(), sub.data(), next_id++, 8 * S, 8 * S, 8 * S, S, arle.data(), 8, 8, 10, 12, 40, 32, hxm.data(), hym.data(),
                row, HD_OP_NEAREST, nullptr, WFS_ALPHA, 0);
            SDL_Surface* a = shot(ri);
            SDL_Surface* b = shot(rh);
            const bool eq = same(a, b);
            // and the ramps draw: against the ground alone, some pixels lighter, some darker (the black ramp's subtract pass)
            bool darker = false, lighter = false;
            WorldFrameRecorder rg;
            rg.AddSprite(grle.data(), 8, 8, 10, 12, 40, 32, xm.data(), ym.data(), nullptr, WFS_ONECOLOUR, grey);
            SDL_Surface* g = shot(rg);
            if (a && g) {
                for (int y = 0; y < a->h; y++) for (int x = 0; x < a->w; x++) {
                    Uint8 r1, g1, b1, a1, r2, g2, b2, a2;
                    SDL_ReadSurfacePixel(g, x, y, &r1, &g1, &b1, &a1);
                    SDL_ReadSurfacePixel(a, x, y, &r2, &g2, &b2, &a2);
                    if ((r1 | g1 | b1) == 0)
                        continue;
                    darker |= (r2 + g2 + b2) < (r1 + g1 + b1);
                    lighter |= (r2 + g2 + b2) > (r1 + g1 + b1);
                }
            }
            if (g) SDL_DestroySurface(g);
            printf("  %-44s %s%s\n", "additive art: add and subtract passes", eq ? "ok, identical" : "MISMATCH",
                (darker && lighter) ? "" : " (but no visible add/subtract)");
            ok &= eq && darker && lighter;
            if (a) SDL_DestroySurface(a);
            if (b) SDL_DestroySurface(b);
        }
        RendererSetHdReferencePalette(nullptr);
        memcpy(lbDisplay.Palette, ref_pal, sizeof(ref_pal));
        // TEXTURE_FILTER (Phase 8): the same HD sprite with each pixel's own sub-texel position (precision mode's
        // lookups). Nearest draws the texel under the pixel centre; Bilinear blends across texels (other pixels, the
        // silhouette within 10%); Sharp keeps each texel's colour inside it and blends only across its edges (fewer
        // pixels than Bilinear change); a parity-mode op (HD_OP_NEAREST) ignores the filter.
        {
            std::vector<uint16_t> pxm(40), pym(32);
            for (int i = 0; i < 40; i++) pxm[i] = (uint16_t)(((2 * i + 1) * 8 * S * HD_MAP_SUBTEXELS) / 80);
            for (int i = 0; i < 32; i++) pym[i] = (uint16_t)(((2 * i + 1) * 8 * S * HD_MAP_SUBTEXELS) / 64);
            uint32_t row[256];
            curve_row(&none, nullptr, row);
            auto filtered = [&](int filter_mode, uint32_t flags) {
                RendererSetTextureFilter(filter_mode);
                WorldFrameRecorder rh;
                rh.AddSpriteHd(hd.data(), nullptr, next_id++, 8 * S, 8 * S, 8 * S, S, rle8.data(), 8, 8, 10, 12, 40, 32, pxm.data(), pym.data(), row,
                    flags, nullptr, WFS_SOLID, 0);
                SDL_Surface* captured = shot(rh);
                RendererSetTextureFilter(0);
                return captured;
            };
            auto count = [](SDL_Surface* a, SDL_Surface* b, int* lit_a, int* lit_b) {
                int diffs = 0; *lit_a = *lit_b = 0;
                for (int y = 0; y < a->h; y++) for (int x = 0; x < a->w; x++) {
                    Uint8 ar, ag, ab, aa, br, bg, bb, ba;
                    SDL_ReadSurfacePixel(a, x, y, &ar, &ag, &ab, &aa);
                    SDL_ReadSurfacePixel(b, x, y, &br, &bg, &bb, &ba);
                    diffs += (ar != br || ag != bg || ab != bb);
                    *lit_a += (ar | ag | ab) != 0; *lit_b += (br | bg | bb) != 0;
                }
                return diffs;
            };
            SDL_Surface* nearest = filtered(0, 0);
            SDL_Surface* bilinear = filtered(2, 0);
            SDL_Surface* sharp = filtered(1, 0);
            SDL_Surface* parity = filtered(2, HD_OP_NEAREST);
            if (nearest && bilinear && sharp && parity) {
                int ln, lb, ls, lp;
                const int db = count(nearest, bilinear, &ln, &lb);
                const int ds = count(nearest, sharp, &ln, &ls);
                const int dp = count(nearest, parity, &ln, &lp);
                const bool fb = db > 0 && std::abs(lb - ln) * 10 <= ln, fs = ds > 0 && ds < db, fp = dp == 0;
                printf("  %-44s %s (%d pixels differ; silhouette %d vs %d)\n", "filter: Bilinear blends", fb ? "ok" : "FAIL", db, lb, ln);
                printf("  %-44s %s (%d pixels differ, Bilinear %d)\n", "filter: Sharp blends only at texel edges", fs ? "ok" : "FAIL", ds, db);
                printf("  %-44s %s\n", "filter: parity draws nearest", fp ? "ok, identical" : "FAIL");
                ok &= fb && fs && fp;
            } else {
                printf("  filter: capture failed\n");
                ok = false;
            }
            for (SDL_Surface* c : { nearest, bilinear, sharp, parity }) if (c) SDL_DestroySurface(c);
        }
        // The HD atlas grows: a 300x300 original at x7 is 2100x2100 texels, one to a 4096x4096 layer. (a) Three draw their
        // HD texels (all white here, so the HD path shows), the array growing from 1 layer to 4. (b) With growth capped
        // at those 4 layers, the next three evict what the last frame used (least recently used) and draw HD too.
        // (c) Five in one frame: four fit the four layers (none used this frame yet), the fifth draws its original (the
        // fallback), exactly as its indexed twin does.
        {
            const int O = 300, BS = 7;
            std::vector<unsigned char> big((size_t)O * O);
            for (int y = 0; y < O; y++) for (int x = 0; x < O; x++) big[(size_t)(y * O + x)] = (x % 7 == 3) ? 0 : (unsigned char)(1 + (x * 13 + y * 7) % 250);
            std::vector<unsigned char> rbig = rle_encode(O, O, big);
            std::vector<TbPixel> hbig((size_t)O * BS * O * BS);
            for (int y = 0; y < O * BS; y++) for (int x = 0; x < O * BS; x++)
                hbig[(size_t)y * O * BS + x] = big[(size_t)((y / BS) * O + x / BS)] ? TbPixel_RGBA(255, 255, 255, 255) : TbPixel_Transparent;
            // 12x12 on screen, at x 0, 21, 42 (the frame's window places them; measured below).
            const int SW = 12;
            int SX = 21;
            std::vector<uint16_t> bx(SW), by(SW), hbx(SW), hby(SW);
            for (int i = 0; i < SW; i++) { bx[i] = by[i] = (uint16_t)(i * 3 + 1); hbx[i] = hby[i] = (uint16_t)((bx[i] * BS + (BS - 1) / 2) * HD_MAP_SUBTEXELS + HD_MAP_SUBTEXELS / 2); }
            uint32_t row[256];
            curve_row(&none, nullptr, row);
            auto draw_n = [&](uint64_t first_id, int n, SDL_Surface** idx_shot) {
                WorldFrameRecorder ri, rh;
                for (int k = 0; k < n; k++) {
                    ri.AddSprite(rbig.data(), O, O, SX * k, 30, SW, SW, bx.data(), by.data(), nullptr, WFS_SOLID, 0);
                    rh.AddSpriteHd(hbig.data(), nullptr, first_id + (uint64_t)k, O * BS, O * BS, O * BS, BS, rbig.data(), O, O, SX * k, 30, SW, SW,
                        hbx.data(), hby.data(), row, 0, nullptr, WFS_SOLID, 0);
                }
                *idx_shot = shot(ri);
                return shot(rh);
            };
            auto pixel = [](SDL_Surface* c, int x, int y, Uint8 out[3]) { Uint8 a; SDL_ReadSurfacePixel(c, x, y, &out[0], &out[1], &out[2], &a); };
            // Each sprite's column band, every row: where the window put them is measured on the indexed shot (its
            // leftmost drawn column is sprite 0's first). HD: every pixel the indexed twin draws is white; as indexed:
            // every pixel the same.
            auto first_col = [&](SDL_Surface* cidx) {
                for (int x = 0; x < W; x++) for (int y = 0; y < H; y++) { Uint8 q[3]; pixel(cidx, x, y, q); if (q[0] | q[1] | q[2]) return x; }
                return 0;
            };
            auto white = [&](SDL_Surface* c, SDL_Surface* cidx, int k) {
                int drawn = 0;
                const int x0 = first_col(cidx) + SX * k;
                for (int y = 0; y < H; y++) for (int x = x0; x < x0 + SW && x < W; x++) {
                    Uint8 p[3], q[3]; pixel(c, x, y, p); pixel(cidx, x, y, q);
                    if (q[0] == 0 && q[1] == 0 && q[2] == 0) continue;
                    drawn++;
                    if (p[0] != 255 || p[1] != 255 || p[2] != 255) return false;
                }
                return drawn > 10;
            };
            auto as_indexed = [&](SDL_Surface* c, SDL_Surface* cidx, int k) {
                int drawn = 0;
                const int x0 = first_col(cidx) + SX * k;
                for (int y = 0; y < H; y++) for (int x = x0; x < x0 + SW && x < W; x++) {
                    Uint8 p[3], q[3]; pixel(c, x, y, p); pixel(cidx, x, y, q);
                    if (p[0] != q[0] || p[1] != q[1] || p[2] != q[2]) return false;
                    if (q[0] | q[1] | q[2]) drawn++;
                }
                return drawn > 10;
            };
            GpuSpriteRenderer::hd_layer_limit = 0;
            SDL_Surface* ci = nullptr;
            SDL_Surface* ca = draw_n(1000, 3, &ci);
            const bool grew = ca && ci && white(ca, ci, 0) && white(ca, ci, 1) && white(ca, ci, 2);
            printf("  %-44s %s\n", "(a) three 2100x2100 HD frames, atlas grown", grew ? "ok, all HD" : "MISMATCH");
            ok &= grew;
            if (ca) SDL_DestroySurface(ca);
            if (ci) SDL_DestroySurface(ci);
            GpuSpriteRenderer::hd_layer_limit = 4;
            SDL_Surface* cb = draw_n(2000, 3, &ci);
            const bool evicted = cb && ci && white(cb, ci, 0) && white(cb, ci, 1) && white(cb, ci, 2);
            printf("  %-44s %s\n", "(b) atlas capped: last frame's evicted", evicted ? "ok, all HD" : "MISMATCH");
            ok &= evicted;
            if (cb) SDL_DestroySurface(cb);
            if (ci) SDL_DestroySurface(ci);
            SX = 12;
            SDL_Surface* cc = draw_n(3000, 5, &ci);
            const bool fell_back = cc && ci && white(cc, ci, 0) && white(cc, ci, 1) && white(cc, ci, 2) && white(cc, ci, 3)
                && as_indexed(cc, ci, 4);
            printf("  %-44s %s\n", "(c) capped, 5 in a frame: 4 HD, 1 original", fell_back ? "ok, the original as indexed" : "MISMATCH");
            ok &= fell_back;
            if (cc) SDL_DestroySurface(cc);
            if (ci) SDL_DestroySurface(ci);
            GpuSpriteRenderer::hd_layer_limit = 0;
        }
        memcpy(lbDisplay.Palette, saved_pal, sizeof(saved_pal));
    }
    r.Shutdown();
    printf(ok ? "VERIFICATION PASSED\n" : "VERIFICATION FAILED\n");
    return ok ? 0 : 1;
}
