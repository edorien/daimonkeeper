// kfx_config: config_terrain.c -- a rich set of pure slab/room-kind
// predicates, most reachable via direct kfx_config_state field writes
// (pattern A) rather than the full terrain.cfg NamedField loader
// (load_terrain_config_file itself is `static`, and its real fixture
// syntax -- three separate blocks: slab%d, [block_health], room%d --
// is a bigger undertaking than this round attempts). Several
// predicates (slab_kind_is_friable_dirt/_liquid/_has_torches) turned
// out to be pure SlabKind-enum comparisons with no config data
// dependency at all.
//
// enemies_may_work_in_room()/get_room_create_creature_model() reach
// into config_creature.c's get_jobs_enemies_may_do_in_room() (a
// different, large file) and aren't attempted here. The per-player room
// availability functions (set_room_available and friends) moved to
// kfx_sim's player_availability.c (refactor pass 2, S05) and are tested
// in kfx_sim/tests/player_availability_test.cpp.
#include <catch2/catch_test_macros.hpp>

#include "config_terrain.h"
#include "kfx_config_state.h"

#include <cstring>

namespace {
struct ResetConfigState {
    ResetConfigState() { std::memset(&kfx_config_state, 0, sizeof(kfx_config_state)); }
};
}

TEST_CASE("slab_kind_is_friable_dirt matches only EARTH/TORCHDIRT", "[kfx_config][config_terrain]") {
    CHECK(slab_kind_is_friable_dirt(SlbT_EARTH));
    CHECK_FALSE(slab_kind_is_friable_dirt(SlbT_WATER));
    CHECK_FALSE(slab_kind_is_friable_dirt(SlbT_WALLDRAPE));
}

TEST_CASE("slab_kind_is_liquid matches only WATER/LAVA", "[kfx_config][config_terrain]") {
    CHECK(slab_kind_is_liquid(SlbT_WATER));
    CHECK_FALSE(slab_kind_is_liquid(SlbT_EARTH));
}

TEST_CASE("slab_kind_has_torches matches only WALLTORCH/TORCHDIRT", "[kfx_config][config_terrain]") {
    CHECK(slab_kind_has_torches(SlbT_WALLTORCH));
    CHECK_FALSE(slab_kind_has_torches(SlbT_WALLDRAPE));
}

TEST_CASE_METHOD(ResetConfigState, "get_slab_kind_stats/get_room_kind_stats fall back to slot 0 for an out-of-range index", "[kfx_config][config_terrain]") {
    kfx_config_state.conf.slab_conf.slab_types_count = 2;
    kfx_config_state.conf.slab_conf.room_types_count = 2;
    CHECK(get_slab_kind_stats(250) == get_slab_kind_stats(0));
    CHECK(get_room_kind_stats(250) == get_room_kind_stats(0));
}

TEST_CASE_METHOD(ResetConfigState, "slab_kind_is_indestructible/_room_wall/_door read straight off the loaded SlabConfigStats", "[kfx_config][config_terrain]") {
    kfx_config_state.conf.slab_conf.slab_types_count = 1;
    struct SlabConfigStats *slabst = get_slab_kind_stats(0);
    slabst->indestructible = 1;
    slabst->category = SlbAtCtg_FortifiedWall;
    slabst->slb_id = 5; // non-zero, required alongside category for _room_wall
    slabst->block_flags = SlbAtFlg_IsDoor;

    CHECK(slab_kind_is_indestructible(0));
    CHECK(slab_kind_is_room_wall(0));
    CHECK(slab_kind_is_door(0));
}

TEST_CASE_METHOD(ResetConfigState, "slab_kind_is_room_wall is false when slb_id is 0, even with the right category", "[kfx_config][config_terrain]") {
    kfx_config_state.conf.slab_conf.slab_types_count = 1;
    struct SlabConfigStats *slabst = get_slab_kind_stats(0);
    slabst->category = SlbAtCtg_FortifiedWall;
    slabst->slb_id = 0;

    CHECK_FALSE(slab_kind_is_room_wall(0));
}

TEST_CASE_METHOD(ResetConfigState, "room predicates read straight off the loaded RoomConfigStats' flags bitmask", "[kfx_config][config_terrain]") {
    kfx_config_state.conf.slab_conf.room_types_count = 1;
    struct RoomConfigStats *roomst = get_room_kind_stats(0);
    roomst->flags = RoCFlg_NoFlames | RoCFlg_CantVandalize | RoCFlg_NotCounted | RoCFlg_NoEnsign;

    CHECK_FALSE(room_has_surrounding_flames(0)); // negated flag
    CHECK(room_cannot_vandalise(0));
    CHECK_FALSE(room_is_counted(0)); // negated flag
    CHECK_FALSE(room_can_have_ensign(0)); // negated flag
}

TEST_CASE_METHOD(ResetConfigState, "room predicates default to the 'permissive' reading when no flags are set", "[kfx_config][config_terrain]") {
    kfx_config_state.conf.slab_conf.room_types_count = 1;
    // get_room_kind_stats(0) is already all-zero from ResetConfigState.
    CHECK(room_has_surrounding_flames(0));
    CHECK_FALSE(room_cannot_vandalise(0));
    CHECK(room_is_counted(0));
    CHECK(room_can_have_ensign(0));
}

TEST_CASE_METHOD(ResetConfigState, "room_role_matches/get_room_roles read the roles bitmask, room_corresponding_slab/slab_corresponding_room round-trip assigned_slab", "[kfx_config][config_terrain]") {
    kfx_config_state.conf.slab_conf.room_types_count = 2;
    struct RoomConfigStats *room0 = get_room_kind_stats(0);
    room0->roles = RoRoF_GoldStorage | RoRoF_FoodStorage;
    room0->assigned_slab = 7;

    CHECK(get_room_roles(0) == (RoRoF_GoldStorage | RoRoF_FoodStorage));
    CHECK(room_role_matches(0, RoRoF_GoldStorage));
    CHECK_FALSE(room_role_matches(0, RoRoF_CratesStorage));
    CHECK(room_corresponding_slab(0) == 7);
    CHECK(slab_corresponding_room(7) == 0);
    CHECK(slab_corresponding_room(250) == 0); // no room has this slab assigned -- falls back to room 0's own default
}

TEST_CASE_METHOD(ResetConfigState, "find_first_roomkind_with_role scans for the first room whose roles match", "[kfx_config][config_terrain]") {
    kfx_config_state.conf.slab_conf.room_types_count = 3;
    get_room_kind_stats(0)->roles = RoRoF_FoodStorage;
    get_room_kind_stats(1)->roles = RoRoF_GoldStorage;
    get_room_kind_stats(2)->roles = RoRoF_GoldStorage;

    CHECK(find_first_roomkind_with_role(RoRoF_GoldStorage) == 1);
    CHECK(find_first_roomkind_with_role(RoRoF_PowersStorage) == 0); // no match -- falls back to 0
}

TEST_CASE_METHOD(ResetConfigState, "slab_code_name/room_code_name/room_role_code_name fall back to INVALID for an unnamed entry", "[kfx_config][config_terrain]") {
    CHECK(std::strcmp(slab_code_name(5), "INVALID") == 0);
    CHECK(std::strcmp(room_code_name(5), "INVALID") == 0);
    CHECK(std::strcmp(room_role_code_name((RoomRole)999999), "INVALID") == 0);
}

TEST_CASE_METHOD(ResetConfigState, "make_all_rooms_free zeroes the cost of every loaded room kind", "[kfx_config][config_terrain]") {
    kfx_config_state.conf.slab_conf.room_types_count = 2;
    get_room_kind_stats(0)->cost = 500;
    get_room_kind_stats(1)->cost = 999;

    CHECK(make_all_rooms_free());
    CHECK(get_room_kind_stats(0)->cost == 0);
    CHECK(get_room_kind_stats(1)->cost == 0);
}
