// kfx_platform: renderer/RendererManager.cpp -- the renderer-backend
// facade. Almost every entry point here guards on its own static
// s_active_renderer, which defaults to nullptr and is never set in this
// test binary (no RendererInit(RENDERER_SOFTWARE) call -- that would
// construct a real RendererSoftware backend and touch actual rendering
// state, the "significant harness change" boundary this library has
// consistently declined). What's tested here is exactly that guarded,
// no-active-renderer surface: every function that safely no-ops/returns
// a failure code rather than falling through to a real Immediate draw
// call (RendererDrawBox/RendererSpriteDraw*/RendererTextDrawResized all
// DO fall through to real screen-buffer-touching bflib_vidraw.c/
// bflib_sprfnt.c code even with no active renderer, so those are left
// alone -- same boundary as bflib_vidraw*.c itself).
#include <catch2/catch_test_macros.hpp>

#include "renderer/RendererManager.h"
#include "bflib_video.h"
#include "ports/display_host_port.h"

#include <cstring>

namespace {
struct RendererManagerFixture {
    RendererManagerFixture() {
        lbScreenInitialised = false;
    }
    ~RendererManagerFixture() {
        set_display_host_port(nullptr); // restores the unwired defaults
    }
};
}

TEST_CASE_METHOD(RendererManagerFixture, "RendererInit fails cleanly for an unknown renderer type, no object created", "[kfx_platform][RendererManager]") {
    CHECK(RendererInit((RendererType)999) == 0);
    CHECK(RendererGetActiveType() == RENDERER_INVALID);
}

TEST_CASE_METHOD(RendererManagerFixture, "RendererGetActiveType reports RENDERER_INVALID with no active backend", "[kfx_platform][RendererManager]") {
    CHECK(RendererGetActiveType() == RENDERER_INVALID);
}

// gpu-v2 Phase C.1: with no active backend there is nothing to draw a world
// frame, so engine_render.c keeps rasterizing on the CPU.
TEST_CASE_METHOD(RendererManagerFixture, "RendererWorldFrameActive is false and RendererSubmitWorldFrame a safe no-op with no active backend", "[kfx_platform][RendererManager]") {
    CHECK(RendererWorldFrameActive() == 0);
    CHECK(RendererWorldFrameBegin() == 0);
    CHECK(RendererWorldFrameCapturing() == 0);
    RendererWorldFrameEnd(); // no capture in progress: must not submit or crash
    WorldFrame frame = {};
    RendererSubmitWorldFrame(&frame);
    RendererSubmitWorldFrame(nullptr);
}

// gpu-v2 Phase C.1: RendererGetDesiredType/RendererSetDesiredType are pure
// state (config_settingschema.c's RENDERER row reads/writes them directly,
// no backend touched) -- unlike RendererInit(), safely testable here. See
// this file's own header comment for why RendererInit(RENDERER_GPU3D)
// itself is deliberately not exercised in this suite.
TEST_CASE_METHOD(RendererManagerFixture, "RendererGetDesiredType defaults to RENDERER_SOFTWARE", "[kfx_platform][RendererManager]") {
    CHECK(RendererGetDesiredType() == RENDERER_SOFTWARE);
}

TEST_CASE_METHOD(RendererManagerFixture, "desired renderer type round-trips through RendererGetDesiredType/RendererSetDesiredType", "[kfx_platform][RendererManager]") {
    RendererSetDesiredType(RENDERER_GPU3D);
    CHECK(RendererGetDesiredType() == RENDERER_GPU3D);
    RendererSetDesiredType(RENDERER_SOFTWARE);
    CHECK(RendererGetDesiredType() == RENDERER_SOFTWARE);
}

TEST_CASE_METHOD(RendererManagerFixture, "RendererShutdown is a safe no-op with no active backend", "[kfx_platform][RendererManager]") {
    RendererShutdown();
    CHECK(RendererGetActiveType() == RENDERER_INVALID);
}

TEST_CASE_METHOD(RendererManagerFixture, "RendererGetActivePalette returns the readonly palette pointer without crashing", "[kfx_platform][RendererManager]") {
    const unsigned char *pal = RendererGetActivePalette();
    (void)pal; // may be null or a real pointer depending on prior tests' LbPaletteStore calls -- just must not crash
    CHECK(true);
}

TEST_CASE_METHOD(RendererManagerFixture, "RendererPaletteSet fails when the screen isn't initialised", "[kfx_platform][RendererManager]") {
    unsigned char palette[768] = {0};
    CHECK(RendererPaletteSet(palette) == Lb_FAIL);
}

TEST_CASE_METHOD(RendererManagerFixture, "RendererPaletteGet fails when the screen isn't initialised", "[kfx_platform][RendererManager]") {
    unsigned char palette[768] = {0};
    CHECK(RendererPaletteGet(palette) == Lb_FAIL);
}

TEST_CASE_METHOD(RendererManagerFixture, "RendererSetDisplayPalette/RendererClearScreen/RendererPresentGameFrame/RendererPresentStepFrame no-op with no active backend", "[kfx_platform][RendererManager]") {
    unsigned char rgb8[768] = {0};
    RendererSetDisplayPalette(rgb8); // must not crash
    RendererClearScreen(0);          // must not crash
    RendererPresentGameFrame();      // must not crash
    RendererPresentStepFrame();      // must not crash
    CHECK(true);
}

TEST_CASE_METHOD(RendererManagerFixture, "RendererLockFramebuffer fails when the screen isn't initialised", "[kfx_platform][RendererManager]") {
    CHECK(RendererLockFramebuffer() == Lb_FAIL);
}

TEST_CASE_METHOD(RendererManagerFixture, "RendererUnlockFramebuffer is safe with no active backend, clears the framebuffer pointers", "[kfx_platform][RendererManager]") {
    lbDisplay.WScreen = (TbPixel*)1; // any non-null sentinel
    lbDisplay.GraphicsWindowPtr = (TbPixel*)1;
    CHECK(RendererUnlockFramebuffer() == Lb_SUCCESS);
    CHECK(lbDisplay.WScreen == nullptr);
    CHECK(lbDisplay.GraphicsWindowPtr == nullptr);
}

TEST_CASE_METHOD(RendererManagerFixture, "RendererScheduleScreenshot fails with no active backend", "[kfx_platform][RendererManager]") {
    CHECK_FALSE(RendererScheduleScreenshot("shot.png", 1));
}

TEST_CASE_METHOD(RendererManagerFixture, "draw colour round-trips through RendererGetDrawColour/RendererSetDrawColour", "[kfx_platform][RendererManager]") {
    RendererSetDrawColour(42);
    CHECK(RendererGetDrawColour() == 42);
}

TEST_CASE_METHOD(RendererManagerFixture, "draw flags round-trip through RendererGetDrawFlags/RendererSetDrawFlags/Add/Clear/Toggle", "[kfx_platform][RendererManager]") {
    RendererSetDrawFlags(0);
    CHECK(RendererGetDrawFlags() == 0);

    RendererAddDrawFlags(0x05);
    CHECK(RendererGetDrawFlags() == 0x05);

    RendererAddDrawFlags(0x02);
    CHECK(RendererGetDrawFlags() == 0x07);

    RendererClearDrawFlags(0x02);
    CHECK(RendererGetDrawFlags() == 0x05);

    RendererToggleDrawFlags(0x05);
    CHECK(RendererGetDrawFlags() == 0);

    RendererToggleDrawFlags(0x03);
    CHECK(RendererGetDrawFlags() == 0x03);
}

TEST_CASE_METHOD(RendererManagerFixture, "RendererDrawSlabBackground falls through to the default no-op callback with no UI renderer", "[kfx_platform][RendererManager]") {
    // DisplayHostPort's unwired draw_slab_background_immediate is a no-op
    // -- must not crash even though nothing observable happens.
    RendererDrawSlabBackground(0, 0, 32, 32);
    CHECK(true);
}

TEST_CASE_METHOD(RendererManagerFixture, "RendererDrawSlabBackground calls a fake draw_slab_background_immediate with no UI renderer", "[kfx_platform][RendererManager]") {
    static int64_t g_last_x = -1, g_last_y = -1, g_last_w = -1, g_last_h = -1;
    static struct DisplayHostPort fake;
    fake = display_host_port_defaults;
    fake.draw_slab_background_immediate = [](int64_t x, int64_t y, int64_t w, int64_t h) {
        g_last_x = x; g_last_y = y; g_last_w = w; g_last_h = h;
    };
    set_display_host_port(&fake);

    RendererDrawSlabBackground(10, 20, 30, 40);
    CHECK(g_last_x == 10);
    CHECK(g_last_y == 20);
    CHECK(g_last_w == 30);
    CHECK(g_last_h == 40);
}

TEST_CASE_METHOD(RendererManagerFixture, "set_display_host_port(nullptr) restores the unwired defaults", "[kfx_platform][RendererManager]") {
    set_display_host_port(nullptr);
    // Must not crash -- proves display_host_port fell back to
    // &display_host_port_defaults rather than staying null.
    RendererDrawSlabBackground(0, 0, 1, 1);
    CHECK(true);
}

TEST_CASE_METHOD(RendererManagerFixture, "RendererSetImGuiDemoVisible/RendererScreenOwned are safe with DisplayHostPort unwired", "[kfx_platform][RendererManager][imgui]") {
    // The fixture leaves DisplayHostPort at its unwired defaults -- must not
    // crash, and the bool-returning ones must report "nothing active"
    // rather than garbage.
    RendererSetImGuiDemoVisible(1);
    RendererSetImGuiDemoVisible(0);
    CHECK_FALSE(RendererScreenOwned());
}

TEST_CASE_METHOD(RendererManagerFixture, "RendererScreenOwned/RendererSetImGuiDemoVisible reach DisplayHostPort's imgui entries", "[kfx_platform][RendererManager][imgui]") {
    static int64_t g_demo_visible = -1;
    static struct DisplayHostPort fake;
    fake = display_host_port_defaults;
    fake.imgui_is_active = []() -> TbBool { return 1; };
    fake.imgui_screen_owned = []() -> TbBool { return 1; };
    fake.imgui_set_demo_visible = [](TbBool visible) { g_demo_visible = visible; };
    set_display_host_port(&fake);

    CHECK(RendererScreenOwned());
    RendererSetImGuiDemoVisible(1);
    CHECK(g_demo_visible == 1);
}

// gpu-v2 Phase C.3: with no GPU layer (no active backend) RendererCopyFrameRect
// is a plain, clipped copy of the CPU framebuffer rect into a same-layout buffer.
TEST_CASE_METHOD(RendererManagerFixture, "RendererCopyFrameRect copies a clipped framebuffer rect with no GPU layer", "[kfx_platform][RendererManager]") {
    TbPixel fb[8 * 4];
    TbPixel dst[8 * 4];
    for (int i = 0; i < 8 * 4; i++) { fb[i] = TbPixel_RGBA((uint8_t)i, 1, 2, 255); dst[i] = TbPixel_RGBA(0, 0, 0, 0); }
    TbPixel *saved_ws = lbDisplay.WScreen;
    auto saved_w = lbDisplay.GraphicsScreenWidth, saved_h = lbDisplay.GraphicsScreenHeight;
    lbDisplay.WScreen = fb; lbDisplay.GraphicsScreenWidth = 8; lbDisplay.GraphicsScreenHeight = 4;

    RendererCopyFrameRect(dst, 8, 2, 1, 100, 100); // clipped to x 2..7, y 1..3
    CHECK(dst[0].a == 0);                          // outside the rect: untouched
    CHECK(dst[1 * 8 + 1].a == 0);
    CHECK(dst[1 * 8 + 2].r == 10);                 // (2,1) -> index 10
    CHECK(dst[3 * 8 + 7].r == 31);
    RendererCopyFrameRect(nullptr, 8, 0, 0, 1, 1); // must not crash
    RendererCopyFrameRect(dst, 8, 20, 20, 4, 4);   // fully outside: no-op

    lbDisplay.WScreen = saved_ws; lbDisplay.GraphicsScreenWidth = saved_w; lbDisplay.GraphicsScreenHeight = saved_h;
}

// gpu-v2 Phase C.5: lighting mode is pure state; per-pixel lighting is only "active" with a GPU backend.
TEST_CASE_METHOD(RendererManagerFixture, "lighting mode round-trips and per-pixel lighting is inactive without a GPU backend", "[kfx_platform][RendererManager]") {
    CHECK(RendererGetLightingMode() == RENDERER_LIGHTING_CLASSIC);
    RendererSetLightingMode(RENDERER_LIGHTING_PERPIXEL);
    CHECK(RendererGetLightingMode() == RENDERER_LIGHTING_PERPIXEL);
    CHECK_FALSE(RendererPerPixelLightingActive());   // no active world-frame backend in this binary
    RendererSetLightingMode(42);                       // anything else is classic
    CHECK(RendererGetLightingMode() == RENDERER_LIGHTING_CLASSIC);
    const double m[4] = {}, fade[4] = {};
    RendererWorldFrameSetLighting(m, m, m, 1, 0, 0, fade, nullptr, 0, nullptr, 0, 0); // not capturing: safe no-op
}
