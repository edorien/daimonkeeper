// kfx_sim: light_registry.c -- the lights themselves, moved from kfx_render's
// light_data.c in refactor pass 2 (S11, docs/refactor-pass2/
// stage-11-lighting-split.md) together with the first half of this file
// (then kfx_render's light_data_test.cpp). Pattern A (docs/refactor/testing/
// stage-02-testability-and-fakes.md §2): the registry lives in
// kfx_sim_state, so the fixture zeroes it, plus direct field manipulation
// on kfx_sim_state.light_registry.lights[] where a test needs a light in a
// state no public function produces on its own. The shading these lights
// feed is kfx_render's, tested in its light_data_test.cpp.
#include <catch2/catch_test_macros.hpp>

#include "light_registry.h"
#include "kfx_sim_state.h"
#include "thing_list.h" // TngList_StaticLights/TngList_DynamLights

#include <cstring>

namespace {
struct ResetLighting {
    ResetLighting() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&light_shading_signals, 0, sizeof(light_shading_signals));
        kfx_sim_state.map_subtiles_x = 255;
        kfx_sim_state.map_subtiles_y = 255;
    }

    static struct InitLight init_light(TbBool dynamic, MapSubtlCoord stl_x, MapSubtlCoord stl_y) {
        struct InitLight ilght;
        std::memset(&ilght, 0, sizeof(ilght));
        ilght.mappos.x.stl.num = stl_x;
        ilght.mappos.y.stl.num = stl_y;
        ilght.radius = 2560;
        ilght.intensity = 40;
        ilght.is_dynamic = dynamic;
        return ilght;
    }

    static struct Light *light(int64_t idx) { return &kfx_sim_state.light_registry.lights[idx]; }
};
}

TEST_CASE_METHOD(ResetLighting, "light_is_invalid rejects null and out-of-range lights", "[kfx_sim][light_registry]") {
    CHECK(light_is_invalid(nullptr));
    CHECK(light_is_invalid(light(0))); // index 0 is the reserved sentinel
    CHECK_FALSE(light_is_invalid(light(1)));
    CHECK_FALSE(light_is_invalid(light(LIGHTS_COUNT - 1)));
}

TEST_CASE_METHOD(ResetLighting, "light_get_light_radius/light_set_light_radius round-trip", "[kfx_sim][light_registry]") {
    light_set_light_radius(1, 500);
    CHECK(light_get_light_radius(1) == 500);
}

TEST_CASE_METHOD(ResetLighting, "light_is_light_allocated reflects the Allocated flag", "[kfx_sim][light_registry]") {
    CHECK_FALSE(light_is_light_allocated(1));
    light(1)->flags |= LgtF_Allocated;
    light(1)->index = 1;
    CHECK(light_is_light_allocated(1));
}

TEST_CASE_METHOD(ResetLighting, "light_is_light_allocated rejects out-of-range indices", "[kfx_sim][light_registry]") {
    CHECK_FALSE(light_is_light_allocated(0));
    CHECK_FALSE(light_is_light_allocated(LIGHTS_COUNT));
}

// light_allocate_light/_free_light/_count_lights: the free-list-free
// linear-scan allocator over the registry's lights[] -- same "allocate,
// dispose, reallocate" shape as kfx_pathfinding's tri_new/tri_dispose, just
// a scan instead of a free list (this array doesn't use one).

TEST_CASE_METHOD(ResetLighting, "light_allocate_light returns the first unallocated slot and marks it allocated", "[kfx_sim][light_registry]") {
    struct Light *lgt = light_allocate_light();
    REQUIRE(lgt != nullptr);
    CHECK(lgt == light(1)); // index 0 is the reserved sentinel
    CHECK(light_is_light_allocated(1));
    CHECK(lgt->index == 1);
}

TEST_CASE_METHOD(ResetLighting, "light_count_lights reflects the number of allocated lights", "[kfx_sim][light_registry]") {
    CHECK(light_count_lights() == 0);
    light_allocate_light();
    light_allocate_light();
    CHECK(light_count_lights() == 2);
}

TEST_CASE_METHOD(ResetLighting, "light_free_light clears the slot so it can be allocated again", "[kfx_sim][light_registry]") {
    struct Light *first = light_allocate_light();
    light_free_light(first);
    CHECK(light_count_lights() == 0);

    struct Light *reused = light_allocate_light();
    CHECK(reused == first); // the freed slot is the first unallocated one again
}

// light_get_light_intensity/_set_light_intensity: unlike
// light_get_light_radius (a bare field read, no check at all), the
// intensity getter refuses to read an unallocated light.

TEST_CASE_METHOD(ResetLighting, "light_get_light_intensity refuses to read an unallocated light, unlike light_get_light_radius", "[kfx_sim][light_registry]") {
    CHECK(light_get_light_intensity(1) == 0); // slot 1 not allocated yet
}

TEST_CASE_METHOD(ResetLighting, "light_get_light_intensity/light_set_light_intensity round-trip on an allocated light", "[kfx_sim][light_registry]") {
    struct Light *lgt = light_allocate_light();
    light_set_light_intensity(lgt->index, 200);
    CHECK(light_get_light_intensity(lgt->index) == 200);
}

// Shadow caches: kfx_render holds their contents, but which are handed out
// is registry state -- creating a dynamic light fails when none is free,
// which the sim sees (a thing without its light).

TEST_CASE_METHOD(ResetLighting, "light_allocate_shadow_cache hands out the first free slot, never 0", "[kfx_sim][light_registry]") {
    CHECK(light_allocate_shadow_cache() == 1);
    CHECK(light_allocate_shadow_cache() == 2);
    light_free_shadow_cache(1);
    CHECK(light_allocate_shadow_cache() == 1);
}

TEST_CASE_METHOD(ResetLighting, "light_allocate_shadow_cache returns 0 once every slot is taken", "[kfx_sim][light_registry]") {
    for (int64_t i = 1; i < SHADOW_CACHE_COUNT; i++)
        REQUIRE(light_allocate_shadow_cache() == i);
    CHECK(light_allocate_shadow_cache() == 0);
}

TEST_CASE_METHOD(ResetLighting, "a dynamic light can't be created without a free shadow cache; a static one still can", "[kfx_sim][light_registry]") {
    for (int64_t i = 1; i < SHADOW_CACHE_COUNT; i++)
        light_allocate_shadow_cache();
    struct InitLight dyn = init_light(true, 10, 10);
    CHECK(light_create_light(&dyn) == 0);
    CHECK(light_count_lights() == 0); // the light it grabbed was given back
    struct InitLight stat = init_light(false, 10, 10);
    CHECK(light_create_light(&stat) == 1);
}

TEST_CASE_METHOD(ResetLighting, "deleting a dynamic light gives its shadow cache back", "[kfx_sim][light_registry]") {
    struct InitLight dyn = init_light(true, 10, 10);
    int64_t idx = light_create_light(&dyn);
    REQUIRE(idx == 1);
    CHECK(light(idx)->shadow_index == 1);
    CHECK(kfx_sim_state.light_registry.shadow_cache_used[1] != 0);
    CHECK(kfx_sim_state.light_registry.total_dynamic_lights == 1);
    CHECK(kfx_sim_state.thing_lists[TngList_DynamLights].count == 1);

    light_delete_light(idx);
    CHECK(kfx_sim_state.light_registry.shadow_cache_used[1] == 0);
    CHECK(kfx_sim_state.light_registry.total_dynamic_lights == 0);
    CHECK(kfx_sim_state.thing_lists[TngList_DynamLights].count == 0);
    CHECK(light_allocate_shadow_cache() == 1);
}

// Registry -> shading signals (struct LightShadingSignals): what used to be
// kfx_render clearing its static light map from inside the registry.

TEST_CASE_METHOD(ResetLighting, "a change near a static light flags it and records the area for kfx_render", "[kfx_sim][light_registry]") {
    struct InitLight stat = init_light(false, 10, 10);
    int64_t idx = light_create_light(&stat);
    REQUIRE(idx != 0);
    light(idx)->range = 3; // kfx_render sets it when it shades the light
    light(idx)->flags &= ~LgtF_NeedUpdate;

    light_signal_stat_light_update_in_area(12, 12, 14, 14);

    CHECK((light(idx)->flags & LgtF_NeedUpdate) != 0);
    REQUIRE(light_shading_signals.areas_count == 1);
    CHECK(light_shading_signals.areas[0].x1 == 12);
    CHECK(light_shading_signals.areas[0].y2 == 14);
}

TEST_CASE_METHOD(ResetLighting, "a change away from every static light records nothing", "[kfx_sim][light_registry]") {
    struct InitLight stat = init_light(false, 10, 10);
    int64_t idx = light_create_light(&stat);
    light(idx)->range = 3;
    light(idx)->flags &= ~LgtF_NeedUpdate;

    light_signal_stat_light_update_in_area(50, 50, 60, 60);

    CHECK((light(idx)->flags & LgtF_NeedUpdate) == 0);
    CHECK(light_shading_signals.areas_count == 0);
}

TEST_CASE_METHOD(ResetLighting, "moving a static light records the area it lit", "[kfx_sim][light_registry]") {
    struct InitLight stat = init_light(false, 10, 10);
    int64_t idx = light_create_light(&stat);
    light(idx)->range = 2;
    struct Coord3d pos = light(idx)->mappos;
    pos.x.stl.num = 20;
    light_set_light_position(idx, &pos);

    REQUIRE(light_shading_signals.areas_count == 1);
    CHECK(light_shading_signals.areas[0].x1 == 8);
    CHECK(light_shading_signals.areas[0].x2 == 12);
}

TEST_CASE_METHOD(ResetLighting, "a repeat of the last recorded area isn't recorded twice", "[kfx_sim][light_registry]") {
    struct InitLight stat = init_light(false, 10, 10);
    light(light_create_light(&stat))->range = 3;
    light_signal_stat_light_update_in_area(9, 9, 11, 11);
    light_signal_stat_light_update_in_area(9, 9, 11, 11);
    CHECK(light_shading_signals.areas_count == 1);
}

TEST_CASE_METHOD(ResetLighting, "too many areas collapse into a full rebuild", "[kfx_sim][light_registry]") {
    struct InitLight stat = init_light(false, 100, 100);
    light(light_create_light(&stat))->range = 100;
    for (int64_t i = 0; i <= LIGHT_SHADING_AREAS_MAX; i++)
        light_signal_stat_light_update_in_area(i, i, i + 1, i + 1);
    CHECK(light_shading_signals.areas_overflowed);
    CHECK(light_shading_signals.areas_count == 0);
}

TEST_CASE_METHOD(ResetLighting, "light_set_lights_on(false) goes fullbright and asks for the whole map", "[kfx_sim][light_registry]") {
    light_set_lights_on(false);
    CHECK(kfx_sim_state.light_registry.global_ambient_light == 32);
    CHECK_FALSE(kfx_sim_state.light_registry.light_enabled);
    CHECK_FALSE(kfx_sim_state.light_registry.light_auto_sync);
    REQUIRE(light_shading_signals.areas_count >= 1);
    CHECK(light_shading_signals.areas[0].x1 == 0);
    CHECK(light_shading_signals.areas[0].x2 == 255);
}

TEST_CASE_METHOD(ResetLighting, "light_registry_invalidate_shading asks for a full rebuild without touching the registry", "[kfx_sim][light_registry]") {
    struct InitLight stat = init_light(false, 10, 10);
    int64_t s = light_create_light(&stat);
    light(s)->flags &= ~LgtF_NeedUpdate;
    light_shading_signals.areas_count = 5; // stale areas are dropped
    // A resync compares the received sim state byte for byte (ftest
    // net_resync_fake_multiplayer): the flags must be as they arrived.
    const unsigned char flags_before = light(s)->flags;

    light_registry_invalidate_shading();

    CHECK(light(s)->flags == flags_before);
    CHECK(light_shading_signals.registry_replaced);
    CHECK(light_shading_signals.areas_count == 0);
}
