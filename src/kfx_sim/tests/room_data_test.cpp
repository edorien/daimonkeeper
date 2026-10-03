// kfx_sim "room" cluster, per docs/refactor/testing/comprehensive/
// stage-08-comprehensive-library-passes.md §3: room_data.c's own
// accessors follow the same "index 0 reserved sentinel" family as
// thing_data.c/creature_control.c (room_is_invalid/room_exists/
// room_get), plus compute_room_max_health() -- pattern A on
// kfx_config_state (reads conf.rules[0].workers.hits_per_slab) combined
// with saturate_set_unsigned(), the same EmulateIntegerOverflowFunc
// pattern-B target already tested directly in kfx_platform/tests/
// bflib_basics_test.cpp -- here exercised indirectly through its real
// caller instead, with the default (non-emulating) provider in effect.
#include <catch2/catch_test_macros.hpp>

#include "room_data.h"
#include "kfx_sim_state.h"
#include "kfx_config_state.h"
#include "config_creature.h"
#include "config_objects.h"
#include "config_terrain.h"
#include "thing_objects.h"

#include <cstring>

namespace {
struct ResetSimState {
    ResetSimState() { std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state)); }
};
}

TEST_CASE_METHOD(ResetSimState, "room_is_invalid rejects null and the reserved index-0 sentinel", "[kfx_sim][room_data]") {
    CHECK(room_is_invalid(nullptr));
    CHECK(room_is_invalid(room_get(0)));
    CHECK(room_is_invalid(INVALID_ROOM));
}

TEST_CASE_METHOD(ResetSimState, "room_is_invalid accepts an in-range slot", "[kfx_sim][room_data]") {
    CHECK_FALSE(room_is_invalid(room_get(1)));
}

TEST_CASE_METHOD(ResetSimState, "room_get returns the sentinel for an out-of-range index", "[kfx_sim][room_data]") {
    CHECK(room_get(0) == INVALID_ROOM);
    CHECK(room_get(ROOMS_COUNT + 1) == INVALID_ROOM);
}

TEST_CASE_METHOD(ResetSimState, "room_exists is false until RoF_Allocated is set", "[kfx_sim][room_data]") {
    struct Room *room = room_get(1);
    CHECK_FALSE(room_exists(room));

    room->alloc_flags |= RoF_Allocated;
    CHECK(room_exists(room));
}

TEST_CASE_METHOD(ResetSimState, "compute_room_max_health multiplies hits_per_slab by slab count", "[kfx_sim][room_data]") {
    std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
    kfx_config_state.conf.rules[0].workers.hits_per_slab = 200;
    CHECK(compute_room_max_health(100, 0) == 20000); // 200 * 100, well under the 16-bit saturation limit
}

TEST_CASE_METHOD(ResetSimState, "compute_room_max_health saturates at the 16-bit limit", "[kfx_sim][room_data]") {
    std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
    kfx_config_state.conf.rules[0].workers.hits_per_slab = 250;
    CHECK(compute_room_max_health(300, 0) == 65535); // 250 * 300 = 75000, clamped
}

// get_required_room_capacity_for_object moved here from kfx_config's
// config_objects.c (refactor pass 2, S05).
namespace {
struct ResetObjectsConfig {
    ResetObjectsConfig() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        struct ObjectsConfig *objconf = &kfx_config_state.conf.object_conf;
        objconf->object_types_count = 4;
        objconf->object_cfgstats[0].genre = OCtg_GoldHoard;
        objconf->object_cfgstats[1].genre = OCtg_Food;
        objconf->object_cfgstats[2].genre = OCtg_WrkshpBox;
        objconf->object_cfgstats[3].genre = OCtg_Spellbook;
    }
};
}

TEST_CASE_METHOD(ResetObjectsConfig, "get_required_room_capacity_for_object matches gold/food/crate/power genres to their storage room roles", "[kfx_sim][room_data]") {
    CHECK(get_required_room_capacity_for_object(RoRoF_GoldStorage, 0, 0) == get_wealth_size_of_gold_hoard_model(0));
    CHECK(get_required_room_capacity_for_object(RoRoF_FoodStorage, 1, 0) == 1);
    CHECK(get_required_room_capacity_for_object(RoRoF_FoodSpawn, 1, 0) == 1);
    CHECK(get_required_room_capacity_for_object(RoRoF_CratesStorage, 2, 0) == 1);
    CHECK(get_required_room_capacity_for_object(RoRoF_PowersStorage, 3, 0) == 1);
}

TEST_CASE_METHOD(ResetObjectsConfig, "get_required_room_capacity_for_object returns 0 for a genre/role mismatch and for RoRoF_KeeperStorage", "[kfx_sim][room_data]") {
    CHECK(get_required_room_capacity_for_object(RoRoF_GoldStorage, 1, 0) == 0); // object 1 is food
    CHECK(get_required_room_capacity_for_object(RoRoF_PowersStorage, 2, 0) == 0);
    CHECK(get_required_room_capacity_for_object(RoRoF_KeeperStorage, 0, 0) == 0);
}

TEST_CASE_METHOD(ResetObjectsConfig, "get_required_room_capacity_for_object sizes lairs by the related creature's lair_size and counts a valid corpse as 1", "[kfx_sim][room_data]") {
    creature_stats_get(1)->lair_size = 3;
    CHECK(get_required_room_capacity_for_object(RoRoF_LairStorage, 0, 1) == 3);
    CHECK(get_required_room_capacity_for_object(RoRoF_DeadStorage, 0, 1) == 1);
    CHECK(get_required_room_capacity_for_object(RoRoF_DeadStorage, 0, 0) == 0); // model 0 is the invalid slot
}
