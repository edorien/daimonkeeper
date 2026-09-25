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
// order between sprites and terrain, shadow masks, and a CPU-rasterizer-vs-GPU parity
// image comparison. Prints VERIFICATION PASSED/FAILED; exit code 0 on pass.

#include "renderer/RendererGpu3D.h"
#include "renderer/RendererManager.h"
#include "renderer/WorldFrameRecorder.h"
#include "bflib_video.h"
#include "bflib_vidsurface.h"
#include "bflib_vidraw.h"
#include "bflib_render.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <vector>
#include <cstring>
#include <cstdlib>

static TbBool t_ensure(SDL_Window*, SDL_Renderer*) { return 1; }
static void t_void(void) {}
static void t_ev(const SDL_Event*) {}
static TbBool t_false(void) { return 0; }
static void t_demo(TbBool) {}
static const RendererImGuiCallbacks cbs = { t_ensure, t_void, t_void, t_void, t_void, t_ev, t_false, t_false, t_false, t_false, t_demo };

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
    SDL_Init(SDL_INIT_VIDEO);
    const int W = 64, H = 64;
    lbWindow = SDL_CreateWindow("gpu3d-verify", W, H, SDL_WINDOW_HIDDEN);
    lbDrawSurface = SDL_CreateSurface(W, H, SDL_PIXELFORMAT_RGBA32);
    set_renderer_imgui_callbacks(&cbs);
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

    // Frame 2: palette entry 5 changes to blue -> cache must re-resolve.
    memset(&lbDisplay.Palette[3 * 5], 0, 3); lbDisplay.Palette[3 * 5 + 2] = 63;
    cpu_frame(); f = mkframe(R, &item, 1, f.view_x, f.view_y, f.view_w, f.view_h); r.SubmitWorldFrame(f);
    cap = present_and_capture(r);
    printf("frame 2 (palette 5 -> blue):\n");
    ok &= px_is(cap, 30, 10, 0, 0, 255, "palette change re-uploaded");
    SDL_DestroySurface(cap);

    // Frame 3: same pointer, contents rewritten to index 6 (animated texture) -> green.
    memset(atlas, 6, sizeof(atlas));
    cpu_frame(); f = mkframe(R, &item, 1, f.view_x, f.view_y, f.view_w, f.view_h); r.SubmitWorldFrame(f);
    cap = present_and_capture(r);
    printf("frame 3 (block contents rewritten in place):\n");
    ok &= px_is(cap, 30, 10, 0, 255, 0, "in-place texture change re-uploaded");
    SDL_DestroySurface(cap);

    // Frame 4: view window offset+clip: window (32,0,32,32) -> triangle only inside that rect, offset applied.
    memset(atlas, 5, sizeof(atlas)); memset(&lbDisplay.Palette[3 * 5], 0, 3); lbDisplay.Palette[3 * 5] = 63;
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

    // Frame 6: CPU-rasterizer vs GPU parity on a patterned, UV-mapped, gouraud-shaded triangle.
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) atlas[y * 256 + x] = (((x / 4) + (y / 4)) & 1) ? 5 : 6;
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
    }
    r.Shutdown();
    printf(ok ? "VERIFICATION PASSED\n" : "VERIFICATION FAILED\n");
    return ok ? 0 : 1;
}
