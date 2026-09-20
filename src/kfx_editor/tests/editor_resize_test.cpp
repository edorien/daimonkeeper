// Catch2 coverage for editor_resize.cpp: resizing a populated map on its snapshot.
#include <catch2/catch_test_macros.hpp>

#include "editor_resize.h"

namespace {
MapContent sample()
{
    MapContent c;
    c.map_tiles_x = 10;
    c.map_tiles_y = 10;
    c.slab_kind.assign(100, SlbT_EARTH);
    c.slab_owner.assign(100, 5);
    c.slab_texture.assign(100, 0);
    c.slab_kind[c.slab_index(1, 1)] = SlbT_GOLD;
    c.slab_texture[c.slab_index(1, 1)] = 7;
    c.slab_kind[c.slab_index(8, 8)] = SlbT_LAVA;
    MapThingRecord near_corner; near_corner.pos_x = 1 * 768 + 100; near_corner.pos_y = 1 * 768 + 100;
    MapThingRecord far_corner; far_corner.pos_x = 8 * 768; far_corner.pos_y = 8 * 768;
    c.things = {near_corner, far_corner};
    MapLightRecord l; l.pos_x = 8 * 768; l.pos_y = 8 * 768;
    c.lights = {l};
    MapActionPointRecord a; a.point_number = 3; a.pos_x = 1 * 768; a.pos_y = 1 * 768;
    c.action_points = {a};
    c.derived_dat.assign(4, 1);
    return c;
}
}

TEST_CASE("growing keeps the top-left corner and pads with rock", "[kfx_editor][resize]") {
    MapContent c = sample();
    EditorResizeReport r;
    REQUIRE(editor_resize_content(c, 12, 14, false, 5, &r));
    CHECK(c.map_tiles_x == 12);
    CHECK(c.map_tiles_y == 14);
    CHECK(c.slab_kind.size() == 168);
    CHECK(c.slab_kind[c.slab_index(1, 1)] == SlbT_GOLD);
    CHECK(c.slab_texture[c.slab_index(1, 1)] == 7);
    CHECK(c.slab_kind[c.slab_index(11, 13)] == SlbT_ROCK);
    CHECK(r.things_dropped == 0);
    CHECK(c.things.size() == 2);
    CHECK(c.derived_dat.empty());
}

TEST_CASE("shrinking removes what falls outside and counts it", "[kfx_editor][resize]") {
    MapContent c = sample();
    EditorResizeReport r;
    REQUIRE(editor_resize_content(c, 8, 8, false, 5, &r));
    CHECK(r.things_dropped == 1);
    CHECK(r.lights_dropped == 1);
    CHECK(r.action_points_dropped == 0);
    CHECK(c.things.size() == 1);
    CHECK(c.action_points[0].point_number == 3);
}

TEST_CASE("centred resize moves everything with the ground", "[kfx_editor][resize]") {
    MapContent c = sample();
    REQUIRE(editor_resize_content(c, 14, 14, true, 5, nullptr)); // 2 slabs of padding each side
    CHECK(c.slab_kind[c.slab_index(3, 3)] == SlbT_GOLD);
    CHECK(c.things[0].pos_x == 3 * 768 + 100);
    CHECK(c.action_points[0].pos_x == 3 * 768);
    CHECK(c.slab_kind[c.slab_index(0, 0)] == SlbT_ROCK);
}

TEST_CASE("sizes outside the supported range are refused", "[kfx_editor][resize]") {
    MapContent c = sample();
    CHECK_FALSE(editor_resize_content(c, 4, 20, false, 5, nullptr));
    CHECK_FALSE(editor_resize_content(c, 200, 20, false, 5, nullptr));
    CHECK(c.map_tiles_x == 10);
}
