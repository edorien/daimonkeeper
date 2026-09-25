// kfx_frontend: frontmenu_landpreview.c's pure pan/zoom/hit-test math --
// the parts with no sprite/campaign-data/renderer dependency, so they can
// run without any game data files. land_preview_load/_unload/_maintain/
// _draw and the minimap slab-colour synthesis aren't covered here: all
// touch real campaign state, sprite sheets, or the active renderer
// palette.
#include <catch2/catch_test_macros.hpp>

#include "frontmenu_landpreview.h"
#include "config.h"

#include <cstring>

TEST_CASE("land_preview_compute_units_per_px falls back to 1:1 for a degenerate rect", "[kfx_frontend][frontmenu_landpreview]") {
    CHECK(land_preview_compute_units_per_px(0, 100) == 16);
    CHECK(land_preview_compute_units_per_px(100, 0) == 16);
    CHECK(land_preview_compute_units_per_px(-5, 100) == 16);
}

TEST_CASE("land_preview_compute_units_per_px sizes to show roughly half the map", "[kfx_frontend][frontmenu_landpreview]") {
    // upp_w = 16*2*rect_w/LANDVIEW_MAP_WIDTH; at rect_w == LANDVIEW_MAP_WIDTH/2
    // that's exactly 16 (1:1) on the width axis.
    int64_t rect_w = LANDVIEW_MAP_WIDTH / 2;
    int64_t rect_h = LANDVIEW_MAP_HEIGHT / 2;
    CHECK(land_preview_compute_units_per_px(rect_w, rect_h) == 16);
}

TEST_CASE("land_preview_compute_units_per_px picks the larger of the two axis scales", "[kfx_frontend][frontmenu_landpreview]") {
    // A very wide, short rect: width wants a big scale, height wants a
    // small one -- the wider (more zoomed-in) one wins so nothing is cut
    // off horizontally.
    int64_t rect_w = LANDVIEW_MAP_WIDTH; // upp_w = 32
    int64_t rect_h = 10;                 // upp_h tiny
    CHECK(land_preview_compute_units_per_px(rect_w, rect_h) == 32);
}

TEST_CASE("land_preview_compute_units_per_px floors at the minimum scale", "[kfx_frontend][frontmenu_landpreview]") {
    CHECK(land_preview_compute_units_per_px(1, 1) == 4);
}

namespace {
struct ClampShiftFixture {
    struct LandPreviewPanel panel{};
    ClampShiftFixture() { std::memset(&panel, 0, sizeof(panel)); }
};
}

TEST_CASE_METHOD(ClampShiftFixture, "land_preview_clamp_shift is a no-op when units_per_px is zero", "[kfx_frontend][frontmenu_landpreview]") {
    panel.units_per_px = 0;
    panel.screen_shift_x = -50;
    panel.screen_shift_y = 99999;
    land_preview_clamp_shift(&panel, 100, 100);
    CHECK(panel.screen_shift_x == -50);
    CHECK(panel.screen_shift_y == 99999);
}

TEST_CASE_METHOD(ClampShiftFixture, "land_preview_clamp_shift clamps a negative shift to zero", "[kfx_frontend][frontmenu_landpreview]") {
    panel.units_per_px = 16;
    panel.screen_shift_x = -10;
    panel.screen_shift_y = -10;
    land_preview_clamp_shift(&panel, 100, 100);
    CHECK(panel.screen_shift_x == 0);
    CHECK(panel.screen_shift_y == 0);
}

TEST_CASE_METHOD(ClampShiftFixture, "land_preview_clamp_shift clamps a shift that would scroll past the map edge", "[kfx_frontend][frontmenu_landpreview]") {
    panel.units_per_px = 16; // 1:1, so visible_w/h == rect_w/h
    panel.screen_shift_x = LANDVIEW_MAP_WIDTH;  // way past the edge
    panel.screen_shift_y = LANDVIEW_MAP_HEIGHT;
    land_preview_clamp_shift(&panel, 100, 100);
    CHECK(panel.screen_shift_x == LANDVIEW_MAP_WIDTH - 100);
    CHECK(panel.screen_shift_y == LANDVIEW_MAP_HEIGHT - 100);
}

TEST_CASE("land_preview_point_over_ensign_box is true directly above the ensign anchor", "[kfx_frontend][frontmenu_landpreview]") {
    // Box: x in [ensign_x-w/2, ensign_x+w/2), y in (ensign_y-h, ensign_y-h/3)
    // == (70, 90) for ensign_y=100, h=30 -- pick a y strictly inside that.
    CHECK(land_preview_point_over_ensign_box(100, 80, /*ensign*/100, 100, /*w*/20, /*h*/30));
}

TEST_CASE("land_preview_point_over_ensign_box is false outside the horizontal span", "[kfx_frontend][frontmenu_landpreview]") {
    CHECK_FALSE(land_preview_point_over_ensign_box(150, 90, 100, 100, 20, 30));
}

TEST_CASE("land_preview_point_over_ensign_box is false below the anchor (not the asymmetric flag-banner box)", "[kfx_frontend][frontmenu_landpreview]") {
    // The box only covers the banner above the anchor point, not a
    // symmetric box centered on it.
    CHECK_FALSE(land_preview_point_over_ensign_box(100, 105, 100, 100, 20, 30));
}

TEST_CASE("land_preview_point_over_ensign_box is false too close to the anchor's own pole base", "[kfx_frontend][frontmenu_landpreview]") {
    // y must be strictly less than ensign_y - h/3 == 100-10 == 90.
    CHECK_FALSE(land_preview_point_over_ensign_box(100, 95, 100, 100, 20, 30));
}
