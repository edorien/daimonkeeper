// kfx_platform: bflib_video.c's DK-value scaling functions, per
// docs/refactor/testing/comprehensive/stage-08-comprehensive-library-
// passes.md's kfx_platform row -- the third and last known
// DisplayHostPort consumer, flagged as still open there
// ("landed for two of the three known callback consumers").
//
// scale_value_for_resolution_with_upp takes units_per_px as a direct
// argument, no callback at all. scale_value_landview reads
// units_per_pixel_landview, a bare kfx_platform extern -- pattern A.
// Everything else (scale_value_by_horizontal_resolution/_vertical_
// resolution, scale_ui_value, scale_fixed_DK_value, scale_value_menu)
// reads its own field off display_get_video_scale_values()
// -- pattern B, kfx_platform's third callback-struct test (after
// GetGameTurnFunc and EmulateIntegerOverflowFunc in bflib_basics_test.cpp).
//
// 16 is "native resolution" for every units_per_pixel_* field (640x400/
// 640x480 at 1:1, per each field's own doc comment) -- scaling by 16
// is documented as the identity case, used throughout as the easy-to-
// verify baseline, with one doubled-upp case per function to confirm the
// field is actually being read, not just defaulting to identity.
#include <catch2/catch_test_macros.hpp>

#include "bflib_video.h"
#include "ports/display_host_port.h"

#include <cstring>

TEST_CASE("scale_value_for_resolution_with_upp is the identity at units_per_px 16", "[kfx_platform][bflib_video]") {
    CHECK(scale_value_for_resolution_with_upp(100, 16) == 100);
}

TEST_CASE("scale_value_for_resolution_with_upp scales linearly with units_per_px", "[kfx_platform][bflib_video]") {
    CHECK(scale_value_for_resolution_with_upp(10, 32) == 20); // double upp -> double output
}

TEST_CASE("scale_value_for_resolution_with_upp floors at 1, never returning 0 or negative", "[kfx_platform][bflib_video]") {
    CHECK(scale_value_for_resolution_with_upp(0, 16) == 1);
    CHECK(scale_value_for_resolution_with_upp(-100, 16) == 1);
}

namespace {
struct ResetLandview {
    ResetLandview() { units_per_pixel_landview = 16; }
};
}

TEST_CASE_METHOD(ResetLandview, "scale_value_landview is the identity at units_per_pixel_landview 16", "[kfx_platform][bflib_video]") {
    CHECK(scale_value_landview(100) == 100);
}

TEST_CASE_METHOD(ResetLandview, "scale_value_landview scales with units_per_pixel_landview", "[kfx_platform][bflib_video]") {
    units_per_pixel_landview = 32;
    CHECK(scale_value_landview(10) == 20);
}

namespace {
struct VideoScaleValues g_fake_scale_values{};
const struct VideoScaleValues *fake_get_video_scale_values(void) { return &g_fake_scale_values; }

struct VideoScaleFixture {
    struct DisplayHostPort port = display_host_port_defaults;
    VideoScaleFixture() {
        std::memset(&g_fake_scale_values, 0, sizeof(g_fake_scale_values));
        g_fake_scale_values.units_per_pixel_width = 16;
        g_fake_scale_values.units_per_pixel_height = 16;
        g_fake_scale_values.units_per_pixel_ui = 16;
        g_fake_scale_values.units_per_pixel_best = 16;
        g_fake_scale_values.units_per_pixel_menu = 16;
        port.get_video_scale_values = fake_get_video_scale_values;
        set_display_host_port(&port);
    }
    ~VideoScaleFixture() { set_display_host_port(nullptr); } // restores the unwired defaults
};
}

TEST_CASE_METHOD(VideoScaleFixture, "scale_value_by_horizontal_resolution reads units_per_pixel_width through the callback", "[kfx_platform][bflib_video]") {
    CHECK(scale_value_by_horizontal_resolution(100) == 100); // identity at upp 16
    g_fake_scale_values.units_per_pixel_width = 32;
    CHECK(scale_value_by_horizontal_resolution(10) == 20);
}

TEST_CASE_METHOD(VideoScaleFixture, "scale_value_by_vertical_resolution reads units_per_pixel_height through the callback", "[kfx_platform][bflib_video]") {
    CHECK(scale_value_by_vertical_resolution(100) == 100);
    g_fake_scale_values.units_per_pixel_height = 32;
    CHECK(scale_value_by_vertical_resolution(10) == 20);
}

TEST_CASE_METHOD(VideoScaleFixture, "scale_ui_value reads units_per_pixel_ui through the callback", "[kfx_platform][bflib_video]") {
    CHECK(scale_ui_value(100) == 100);
    g_fake_scale_values.units_per_pixel_ui = 32;
    CHECK(scale_ui_value(10) == 20);
}

TEST_CASE_METHOD(VideoScaleFixture, "scale_fixed_DK_value reads units_per_pixel_best through the callback", "[kfx_platform][bflib_video]") {
    CHECK(scale_fixed_DK_value(100) == 100);
    g_fake_scale_values.units_per_pixel_best = 32;
    CHECK(scale_fixed_DK_value(10) == 20);
}

TEST_CASE_METHOD(VideoScaleFixture, "scale_value_menu reads units_per_pixel_menu through the callback", "[kfx_platform][bflib_video]") {
    CHECK(scale_value_menu(100) == 100);
    g_fake_scale_values.units_per_pixel_menu = 32;
    CHECK(scale_value_menu(10) == 20);
}

// Regression for a live bug: INGAME_RES never actually applied across a
// restart. Root cause -- Lb_SCREEN_MODE_INVALID is 0, indistinguishable from
// a real mode landing at registry index 0 (LbRegisterVideoMode's own return
// value). setup_game() used to call load_configuration() (which parses
// INGAME_RES via LbRegisterVideoModeString(), config_keeperfx.c case 7)
// before LbScreenInitialize() ever ran -- so on an otherwise-empty registry,
// a user's own custom resolution string became the table's actual first
// entry, landed on index 0, and was silently rejected by the case-7 parser's
// "mode > 0" check, leaving screen_vidmode stuck at its compiled default.
// Fixed by calling LbRegisterDefaultVideoModesIfNeeded() (which reserves
// index 0 with the standard/modern mode table's own "INVALID" placeholder,
// bflib_video.c) early in setup_game(), before load_configuration(). This
// test exercises the fix directly, independent of setup_game()'s call order.
TEST_CASE("LbRegisterDefaultVideoModesIfNeeded reserves index 0 so a fresh custom resolution never collides with Lb_SCREEN_MODE_INVALID", "[kfx_platform][bflib_video]") {
    LbRegisterDefaultVideoModesIfNeeded();

    // Index 0 itself is the reserved placeholder, not a real mode.
    TbScreenModeInfo *invalid_info = LbScreenGetModeInfo(Lb_SCREEN_MODE_INVALID);
    REQUIRE(invalid_info != nullptr);
    CHECK(strcmp(invalid_info->Desc, "INVALID") == 0);

    // A brand-new resolution never seen before in this process must not
    // land on that same index -- this is the exact collision that made
    // INGAME_RES silently fail to apply.
    TbScreenMode mode = LbRegisterVideoModeString("1937x1111x32");
    REQUIRE(mode != Lb_SCREEN_MODE_INVALID);
    TbScreenModeInfo *info = LbScreenGetModeInfo(mode);
    REQUIRE(info != nullptr);
    CHECK(info->Width == 1937);
    CHECK(info->Height == 1111);
    CHECK(info->BitsPerPixel == 32);

    // Idempotent: calling it again (as LbScreenInitialize() itself still
    // does) must not re-reserve or disturb what's already registered.
    LbRegisterDefaultVideoModesIfNeeded();
    TbScreenModeInfo *info_again = LbScreenGetModeInfo(mode);
    REQUIRE(info_again != nullptr);
    CHECK(info_again->Width == 1937);
}
