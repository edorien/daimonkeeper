// kfx_render: light_data.c's side of the lighting split (refactor pass 2,
// S11, docs/refactor-pass2/stage-11-lighting-split.md) -- the shading
// applying what kfx_sim's light registry signalled. The registry itself is
// tested in kfx_sim's light_registry_test.cpp (this file's former contents).
// Pattern A (docs/refactor/testing/stage-02-testability-and-fakes.md §2):
// zeroed kfx_sim_state/lish, a small map whose blocks all use column 1 (a
// valid column, so a cleared subtile gets the ambient light rather than 0).
#include <catch2/catch_test_macros.hpp>

#include "light_data.h"
#include "light_registry.h"
#include "kfx_sim_state.h"
#include "map_data.h"

#include <cstring>

namespace {
const MapSubtlCoord kMapSize = 20;

struct ResetShading {
    ResetShading() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&light_shading_signals, 0, sizeof(light_shading_signals));
        std::memset(&lish, 0, sizeof(lish));
        kfx_sim_state.map_subtiles_x = kMapSize;
        kfx_sim_state.map_subtiles_y = kMapSize;
        for (MapSubtlCoord y = 0; y <= kMapSize; y++)
            for (MapSubtlCoord x = 0; x <= kMapSize; x++)
                set_mapblk_column_index(get_map_block_at(x, y), 1);
        kfx_sim_state.light_registry.global_ambient_light = 10;
        for (MapSubtlCoord y = 0; y <= kMapSize; y++)
            for (MapSubtlCoord x = 0; x <= kMapSize; x++)
                stat_light(x, y) = 999;
    }
    static int64_t &stat_light(MapSubtlCoord x, MapSubtlCoord y) {
        return lish.stat_light_map[get_subtile_number(x, y)];
    }
};
}

TEST_CASE_METHOD(ResetShading, "draining clears each signalled area to the ambient light, and only those", "[kfx_render][light_data]") {
    light_shading_signals.areas[0] = {2, 2, 4, 4};
    light_shading_signals.areas_count = 1;

    light_drain_shading_signals();

    CHECK(stat_light(2, 2) == 10 << 8);
    CHECK(stat_light(4, 4) == 10 << 8);
    CHECK(stat_light(5, 5) == 999);
    CHECK(stat_light(1, 2) == 999);
    CHECK(light_shading_signals.areas_count == 0);
}

TEST_CASE_METHOD(ResetShading, "a subtile next to an invalid column is cleared to 0, not the ambient light", "[kfx_render][light_data]") {
    set_mapblk_column_index(get_map_block_at(6, 6), 0);
    light_shading_signals.areas[0] = {6, 6, 7, 7};
    light_shading_signals.areas_count = 1;

    light_drain_shading_signals();

    CHECK(stat_light(6, 6) == 0);
    CHECK(stat_light(7, 7) == 0); // its (x-1, y-1) neighbour is (6, 6)
    CHECK(stat_light(8, 8) == 999);
}

TEST_CASE_METHOD(ResetShading, "a lightness reset request resets every subtile's lightness", "[kfx_render][light_data]") {
    lish.subtile_lightness[get_subtile_number(3, 3)] = 5;
    light_request_lightness_reset();

    light_drain_shading_signals();

    CHECK(lish.subtile_lightness[get_subtile_number(3, 3)] == 8192);
    CHECK(lish.subtile_lightness[get_subtile_number(kMapSize, kMapSize)] == 8192);
    CHECK_FALSE(light_shading_signals.reset_lightness);
}

TEST_CASE_METHOD(ResetShading, "an overflowed queue rebuilds the whole map and flags every static light", "[kfx_render][light_data]") {
    struct InitLight ilght;
    std::memset(&ilght, 0, sizeof(ilght));
    ilght.mappos.x.stl.num = 10;
    ilght.mappos.y.stl.num = 10;
    ilght.radius = 512;
    ilght.intensity = 30;
    int64_t idx = light_create_light(&ilght);
    REQUIRE(idx != 0);
    struct Light *lgt = &kfx_sim_state.light_registry.lights[idx];
    lgt->range = 2;
    lgt->flags &= ~LgtF_NeedUpdate;
    light_shading_signals.areas_overflowed = true;

    light_drain_shading_signals();

    CHECK((lgt->flags & LgtF_NeedUpdate) != 0);
    CHECK(stat_light(1, 1) == 10 << 8);
    CHECK(stat_light(kMapSize, kMapSize) == 10 << 8);
    CHECK_FALSE(light_shading_signals.areas_overflowed);
    CHECK(light_shading_signals.areas_count == 0);
}

TEST_CASE_METHOD(ResetShading, "a replaced registry gets every light shaded again and the map and lightness rebuilt", "[kfx_render][light_data]") {
    struct InitLight ilght;
    std::memset(&ilght, 0, sizeof(ilght));
    ilght.mappos.x.stl.num = 10;
    ilght.mappos.y.stl.num = 10;
    ilght.radius = 512;
    ilght.intensity = 30;
    ilght.is_dynamic = true;
    int64_t idx = light_create_light(&ilght);
    REQUIRE(idx != 0);
    struct Light *lgt = &kfx_sim_state.light_registry.lights[idx];
    lgt->flags &= ~LgtF_NeedUpdate;
    lish.subtile_lightness[get_subtile_number(3, 3)] = 5;

    light_registry_invalidate_shading();
    light_drain_shading_signals();

    CHECK((lgt->flags & LgtF_NeedUpdate) != 0); // its shadow cache is rebuilt too
    CHECK(stat_light(3, 3) == 10 << 8);
    CHECK(lish.subtile_lightness[get_subtile_number(3, 3)] == 8192);
    CHECK_FALSE(light_shading_signals.registry_replaced);
}

TEST_CASE_METHOD(ResetShading, "clear_stat_light_map drops the areas still waiting to be cleared", "[kfx_render][light_data]") {
    light_shading_signals.areas[0] = {2, 2, 4, 4};
    light_shading_signals.areas_count = 1;

    clear_stat_light_map();
    light_drain_shading_signals();

    CHECK(stat_light(3, 3) == 0); // zeroed, not re-cleared to the (now 32) ambient light
    CHECK(kfx_sim_state.light_registry.global_ambient_light == 32);
}

TEST_CASE_METHOD(ResetShading, "light_stat_refresh applies at once", "[kfx_render][light_data]") {
    light_stat_refresh();
    CHECK(stat_light(1, 1) == 10 << 8);
    CHECK(stat_light(kMapSize, kMapSize) == 10 << 8);
    CHECK(light_shading_signals.areas_count == 0);
}
