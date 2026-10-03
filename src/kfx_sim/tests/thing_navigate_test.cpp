// kfx_sim "thing" cluster depth increment, per docs/refactor/testing/
// comprehensive/stage-08b-kfx-sim-clusters.md's "still open" list:
// thing_navigate.c's lava/toxicity predicates -- the only self-contained,
// non-pathfinding functions in this file. The creatures keep the zeroed
// thing->model 0, which creature_stats_get_from_thing resolves to
// kfx_config_state.conf.crtr_conf.model[0]. Extends the small
// map+Column fixture (thing_doors_test.cpp's DoorAngleFixture) one level
// further: a "lava" subtile needs its Column's top cube to resolve
// (via cube_is_lava, a direct-by-id config lookup, not routed through
// SimPort) to a cube_cfgstats entry with CPF_IsLava set.
// The pathfinding-heavy functions (creature_move_to_using_gates,
// setup_person_move_*, hug_can_move_on, etc.) need a real Ariadne
// route-planning fixture and are left for a later increment.
#include <catch2/catch_test_macros.hpp>

#include "thing_navigate.h"
#include "thing_data.h"
#include "map_data.h"
#include "map_columns.h"
#include "slab_data.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "thing_stats.h"

#include <cstring>

namespace {
struct NavigateFixture {
    static constexpr MapSubtlCoord kStlX = 4;
    static constexpr MapSubtlCoord kStlY = 4;
    static constexpr int64_t kLavaCubeId = 5;
    static constexpr int64_t kAbyssCubeId = 6;

    struct Thing *creatng;

    NavigateFixture() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        kfx_sim_state.map_subtiles_x = 10;
        kfx_sim_state.map_subtiles_y = 10;
        kfx_sim_state.map_tiles_x = 4;
        kfx_sim_state.map_tiles_y = 4;

        creatng = thing_get(1);
    }

    // Makes (kStlX, kStlY)'s top cube resolve to a lava-flagged cube.
    void make_lava_subtile() {
        set_mapblk_column_index(get_map_block_at(kStlX, kStlY), 1);
        kfx_sim_state.columns_data[1].bitfields = 0x10; // floor_filled_subtiles == 1
        kfx_sim_state.columns_data[1].cubes[0] = kLavaCubeId;
        kfx_config_state.conf.cube_conf.cube_cfgstats[kLavaCubeId].properties_flags = CPF_IsLava;
    }

    // Makes (kStlX, kStlY)'s top cube resolve to an abyss-flagged cube
    // (subtile_has_abyss_on_top -> true), same shape as make_lava_subtile
    // but with CPF_IsAbyss on a distinct cube id.
    void make_abyss_subtile() {
        set_mapblk_column_index(get_map_block_at(kStlX, kStlY), 1);
        kfx_sim_state.columns_data[1].bitfields = 0x10; // floor_filled_subtiles == 1
        kfx_sim_state.columns_data[1].cubes[0] = kAbyssCubeId;
        kfx_config_state.conf.cube_conf.cube_cfgstats[kAbyssCubeId].properties_flags = CPF_IsAbyss;
    }
};
}

TEST_CASE_METHOD(NavigateFixture, "creature_can_travel_over_lava is true when unaffected by lava or currently flying", "[kfx_sim][thing_navigate]") {
    kfx_config_state.conf.crtr_conf.model[0].hurt_by_lava = 0;
    CHECK(creature_can_travel_over_lava(creatng));

    kfx_config_state.conf.crtr_conf.model[0].hurt_by_lava = 5;
    CHECK_FALSE(creature_can_travel_over_lava(creatng));

    creatng->movement_flags |= TMvF_Flying;
    CHECK(creature_can_travel_over_lava(creatng)); // flying overrides being hurt by lava
}

TEST_CASE_METHOD(NavigateFixture, "can_step_on_unsafe_terrain_at_position is false off lava regardless of the creature", "[kfx_sim][thing_navigate]") {
    get_slabmap_block(subtile_slab(kStlX), subtile_slab(kStlY))->kind = SlbT_CLAIMED;
    kfx_config_state.conf.crtr_conf.model[0].hurt_by_lava = 5;

    CHECK_FALSE(can_step_on_unsafe_terrain_at_position(creatng, kStlX, kStlY));
}

TEST_CASE_METHOD(NavigateFixture, "can_step_on_unsafe_terrain_at_position on a lava slab delegates to creature_can_travel_over_lava", "[kfx_sim][thing_navigate]") {
    get_slabmap_block(subtile_slab(kStlX), subtile_slab(kStlY))->kind = SlbT_LAVA;

    kfx_config_state.conf.crtr_conf.model[0].hurt_by_lava = 0;
    CHECK(can_step_on_unsafe_terrain_at_position(creatng, kStlX, kStlY));

    kfx_config_state.conf.crtr_conf.model[0].hurt_by_lava = 5;
    CHECK_FALSE(can_step_on_unsafe_terrain_at_position(creatng, kStlX, kStlY));
}

TEST_CASE_METHOD(NavigateFixture, "terrain_toxic_for_creature_at_position is false when the creature isn't hurt by lava, even standing on it", "[kfx_sim][thing_navigate]") {
    make_lava_subtile();
    kfx_config_state.conf.crtr_conf.model[0].hurt_by_lava = 0;

    CHECK_FALSE(terrain_toxic_for_creature_at_position(creatng, kStlX, kStlY));
}

TEST_CASE_METHOD(NavigateFixture, "terrain_toxic_for_creature_at_position is false off lava even when the creature is vulnerable to it", "[kfx_sim][thing_navigate]") {
    kfx_config_state.conf.crtr_conf.model[0].hurt_by_lava = 5;
    // No make_lava_subtile() call -- the top cube stays the default, non-lava cube 0.

    CHECK_FALSE(terrain_toxic_for_creature_at_position(creatng, kStlX, kStlY));
}

TEST_CASE_METHOD(NavigateFixture, "terrain_toxic_for_creature_at_position is true on lava for a vulnerable, grounded creature", "[kfx_sim][thing_navigate]") {
    make_lava_subtile();
    kfx_config_state.conf.crtr_conf.model[0].hurt_by_lava = 5;

    CHECK(terrain_toxic_for_creature_at_position(creatng, kStlX, kStlY));
}

TEST_CASE_METHOD(NavigateFixture, "terrain_toxic_for_creature_at_position is false on lava while the creature is flying, natural flier or not", "[kfx_sim][thing_navigate]") {
    make_lava_subtile();
    kfx_config_state.conf.crtr_conf.model[0].hurt_by_lava = 5;
    CHECK(terrain_toxic_for_creature_at_position(creatng, kStlX, kStlY)); // grounded
    creatng->movement_flags |= TMvF_Flying;

    // Upstream #5372 ("Fix not being able to fly over lava"): this used to demand
    // flying be the creature's natural ability as well, so a creature flying by a
    // spell refused to cross lava. Now it goes by creature_can_travel_over_lava().
    kfx_config_state.conf.crtr_conf.model[0].flying = false;
    CHECK_FALSE(terrain_toxic_for_creature_at_position(creatng, kStlX, kStlY));

    kfx_config_state.conf.crtr_conf.model[0].flying = true;
    CHECK_FALSE(terrain_toxic_for_creature_at_position(creatng, kStlX, kStlY));
}

// Upstream #5235/#5241 ("Fixed creatures being able to walk on Abyss" /
// "Prevent creatures from voluntarily walking onto lava/abyss subtiles")
// make terrain_toxic_for_creature_at_position also reject abyss subtiles
// (via thing_can_traverse_abyss_at()) for a non-flying creature,
// regardless of hurt_by_lava -- a grounded creature standing on a
// non-lava abyss subtile is now correctly reported as toxic.
TEST_CASE_METHOD(NavigateFixture, "terrain_toxic_for_creature_at_position is true on an abyss subtile for a grounded creature, regardless of hurt_by_lava", "[kfx_sim][thing_navigate]") {
    make_abyss_subtile();
    kfx_config_state.conf.crtr_conf.model[0].hurt_by_lava = 0; // abyss isn't lava-hurt-gated

    CHECK(terrain_toxic_for_creature_at_position(creatng, kStlX, kStlY));
}

TEST_CASE_METHOD(NavigateFixture, "terrain_toxic_for_creature_at_position is false on an abyss subtile for a flying creature", "[kfx_sim][thing_navigate]") {
    make_abyss_subtile();
    kfx_config_state.conf.crtr_conf.model[0].hurt_by_lava = 0;
    creatng->movement_flags |= TMvF_Flying;

    CHECK_FALSE(terrain_toxic_for_creature_at_position(creatng, kStlX, kStlY));
}
