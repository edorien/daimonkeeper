// kfx_sim: player_camera.c (moved from kfx_render's engine_camera.c in
// refactor pass 2, S07, with this test), per docs/refactor/testing/comprehensive/
// stage-08-comprehensive-library-passes.md's kfx_render row -- camera
// zoom math is a pure-function-over-a-struct-Camera source, no light/
// shadow allocation state involved. Only the PVM_IsoWibbleView and
// PVM_ParchmentView view_mode branches are exercised: PVM_FrontView's
// zoom-out path additionally reads kfx_config_state's
// frontview_zoom_distance_setting, a state dependency deliberately left
// for a future increment rather than folded in here.
#include <catch2/catch_test_macros.hpp>

#include "player_camera.h"
#include "camera_data.h"
#include "player_data.h" // PVM_* view mode enum

#include <cstring>

namespace {
struct ZeroedCamera {
    ZeroedCamera() { std::memset(&cam, 0, sizeof(cam)); }
    struct Camera cam;
};
}

TEST_CASE_METHOD(ZeroedCamera, "get/set_camera_zoom round-trip through cam->zoom in isometric view", "[kfx_sim][player_camera]") {
    cam.view_mode = PVM_IsoWibbleView;
    set_camera_zoom(&cam, 4096);
    CHECK(cam.zoom == 4096);
    CHECK(get_camera_zoom(&cam) == 4096);
}

TEST_CASE_METHOD(ZeroedCamera, "get/set_camera_zoom round-trip through mappos.z.val in parchment view", "[kfx_sim][player_camera]") {
    cam.view_mode = PVM_ParchmentView;
    set_camera_zoom(&cam, 512);
    CHECK(cam.mappos.z.val == 512);
    CHECK(cam.zoom == 0); // confirms it's a genuinely different field, not a union
    CHECK(get_camera_zoom(&cam) == 512);
}

TEST_CASE_METHOD(ZeroedCamera, "get_camera_zoom returns 0 for an unrecognized view_mode", "[kfx_sim][player_camera]") {
    cam.view_mode = PVM_EmptyView;
    cam.zoom = 999; // present, but PVM_EmptyView isn't in get_camera_zoom's switch
    CHECK(get_camera_zoom(&cam) == 0);
}

TEST_CASE("get_camera_zoom returns 0 and set_camera_zoom no-ops for a NULL camera", "[kfx_sim][player_camera]") {
    CHECK(get_camera_zoom(NULL) == 0);
    set_camera_zoom(NULL, 123); // must not crash
}

TEST_CASE_METHOD(ZeroedCamera, "update_camera_zoom_bounds clamps into [zoom_min, zoom_max]", "[kfx_sim][player_camera]") {
    cam.view_mode = PVM_IsoWibbleView;

    cam.zoom = 10;
    update_camera_zoom_bounds(&cam, 12000, 520);
    CHECK(cam.zoom == 520);

    cam.zoom = 99999;
    update_camera_zoom_bounds(&cam, 12000, 520);
    CHECK(cam.zoom == 12000);

    cam.zoom = 3000;
    update_camera_zoom_bounds(&cam, 12000, 520);
    CHECK(cam.zoom == 3000); // already in range, left untouched
}

TEST_CASE_METHOD(ZeroedCamera, "view_zoom_camera_in increases isometric zoom by the fixed ratio and clamps to limit_max", "[kfx_sim][player_camera]") {
    cam.view_mode = PVM_IsoWibbleView;
    cam.zoom = 850;

    view_zoom_camera_in(&cam, 12000, 520);
    CHECK(cam.zoom == 1000); // (100 * 850) / 85

    cam.zoom = 11999;
    view_zoom_camera_in(&cam, 12000, 520);
    CHECK(cam.zoom == 12000); // (100 * 11999) / 85 = 14116, clamped down
}

TEST_CASE_METHOD(ZeroedCamera, "view_zoom_camera_in always changes zoom by at least one step, even at small values", "[kfx_sim][player_camera]") {
    cam.view_mode = PVM_IsoWibbleView;
    cam.zoom = 1; // (100*1)/85 == 1 -- integer division rounds down to itself
    view_zoom_camera_in(&cam, 12000, 520);
    CHECK(cam.zoom == 520); // the +1 "no-op guard" fires (1 -> 2), then clamps up to limit_min
}

TEST_CASE_METHOD(ZeroedCamera, "view_zoom_camera_out decreases isometric zoom by the fixed ratio and clamps to limit_min", "[kfx_sim][player_camera]") {
    cam.view_mode = PVM_IsoWibbleView;
    cam.zoom = 1000;

    view_zoom_camera_out(&cam, 12000, 520);
    CHECK(cam.zoom == 850); // (85 * 1000) / 100

    cam.zoom = 521;
    view_zoom_camera_out(&cam, 12000, 520);
    CHECK(cam.zoom == 520); // (85 * 521) / 100 = 442, clamped up to limit_min
}

TEST_CASE_METHOD(ZeroedCamera, "view_zoom_camera_in/out on parchment view clamp to the fixed [16, 1024] range regardless of limit_max/limit_min", "[kfx_sim][player_camera]") {
    cam.view_mode = PVM_ParchmentView;

    cam.mappos.z.val = 1023;
    view_zoom_camera_in(&cam, 99999, 1); // limit_max/limit_min are ignored in this branch
    CHECK(cam.mappos.z.val == 1024);

    cam.mappos.z.val = 17;
    view_zoom_camera_out(&cam, 99999, 1);
    CHECK(cam.mappos.z.val == 16);
}
