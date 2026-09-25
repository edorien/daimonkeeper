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

#include <cstring>

namespace {
struct RendererManagerFixture {
    RendererManagerFixture() {
        lbScreenInitialised = false;
    }
    ~RendererManagerFixture() {
        set_renderer_draw_callbacks(nullptr); // restores the default no-op stub
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

TEST_CASE_METHOD(RendererManagerFixture, "RendererSetDisplayPalette/RendererClearScreen/RendererPresentFrame no-op with no active backend", "[kfx_platform][RendererManager]") {
    unsigned char rgb8[768] = {0};
    RendererSetDisplayPalette(rgb8); // must not crash
    RendererClearScreen(0);          // must not crash
    RendererPresentFrame();          // must not crash
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
    // Default renderer_draw_callbacks->draw_slab_background_immediate is
    // itself a no-op stub (RendererManager.cpp's own
    // noop_draw_slab_background_immediate) -- must not crash even
    // though nothing observable happens.
    RendererDrawSlabBackground(0, 0, 32, 32);
    CHECK(true);
}

TEST_CASE_METHOD(RendererManagerFixture, "RendererDrawSlabBackground calls a fake draw_slab_background_immediate with no UI renderer", "[kfx_platform][RendererManager]") {
    static int64_t g_last_x = -1, g_last_y = -1, g_last_w = -1, g_last_h = -1;
    struct RendererDrawCallbacks fake = {
        [](int64_t x, int64_t y, int64_t w, int64_t h) {
            g_last_x = x; g_last_y = y; g_last_w = w; g_last_h = h;
        }
    };
    set_renderer_draw_callbacks(&fake);

    RendererDrawSlabBackground(10, 20, 30, 40);
    CHECK(g_last_x == 10);
    CHECK(g_last_y == 20);
    CHECK(g_last_w == 30);
    CHECK(g_last_h == 40);
}

TEST_CASE_METHOD(RendererManagerFixture, "set_renderer_draw_callbacks(nullptr) restores the default no-op stub", "[kfx_platform][RendererManager]") {
    set_renderer_draw_callbacks(nullptr);
    // Must not crash -- proves renderer_draw_callbacks fell back to
    // &default_renderer_draw_callbacks rather than staying null.
    RendererDrawSlabBackground(0, 0, 1, 1);
    CHECK(true);
}

TEST_CASE_METHOD(RendererManagerFixture, "RendererSetImGuiDemoVisible is safe with no active ImGui context", "[kfx_platform][RendererManager][imgui]") {
    // No SDL window/renderer exists in this test binary, so no ImGui
    // context is active -- must not crash either way.
    RendererSetImGuiDemoVisible(1);
    RendererSetImGuiDemoVisible(0);
    CHECK(true);
}
