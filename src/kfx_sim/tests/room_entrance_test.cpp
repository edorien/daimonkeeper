// kfx_sim "room" cluster depth increment, per docs/refactor/testing/
// comprehensive/stage-08b-kfx-sim-clusters.md's "still open" list:
// room_entrance.c's generation-gating predicates and
// calculate_attractive_room_quantity's per-room-kind formula family.
// Reuses the single-room dungeon->room_list_start[]/rooms[] fixture shape
// established for the lair/room clusters; get_gameturn() stays at its
// default (0), so turns_between_entrance_generation==0 is used to reach
// the "due" branch without a GetGameTurnFunc fake. The generation/pool
// mutation functions (generate_creature_for_dungeon,
// process_entrance_generation, add_creature_to_pool) need a full
// Thing/creature-pool fixture and are left for a later increment.
#include <catch2/catch_test_macros.hpp>

#include "room_entrance.h"
#include "room_data.h"
#include "dungeon_data.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"

#include <cstring>

namespace {
struct ResetState {
    ResetState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        kfx_config_state.conf.rules[0].gameplay.creatures_count = 100; // well above the map limit checks below
    }
};

struct Room *make_single_room(struct Dungeon *dungeon, RoomKind rkind, int64_t slabs_count, uint64_t total_capacity, uint64_t used_capacity)
{
    struct Room *room = room_get(1);
    room->slabs_count = slabs_count;
    room->total_capacity = total_capacity;
    room->used_capacity = used_capacity;
    room->next_of_owner = 0;
    dungeon->room_list_start[rkind] = 1;
    return room;
}
}

TEST_CASE_METHOD(ResetState, "generation_due_for_dungeon is false once the map's creature count limit is reached", "[kfx_sim][room_entrance]") {
    kfx_sim_state.thing_lists[TngList_Creatures].count = CREATURES_COUNT - 1;
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->turns_between_entrance_generation = 0;

    CHECK_FALSE(generation_due_for_dungeon(dungeon));
}

TEST_CASE_METHOD(ResetState, "generation_due_for_dungeon is false before the configured interval has elapsed", "[kfx_sim][room_entrance]") {
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->turns_between_entrance_generation = 5;
    dungeon->last_entrance_generation_gameturn = 0; // default GetGameTurnFunc returns 0 -- elapsed is 0

    CHECK_FALSE(generation_due_for_dungeon(dungeon));
}

TEST_CASE_METHOD(ResetState, "generation_due_for_dungeon is false when turns_between_entrance_generation is disabled (-1)", "[kfx_sim][room_entrance]") {
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->turns_between_entrance_generation = -1;

    CHECK_FALSE(generation_due_for_dungeon(dungeon));
}

TEST_CASE_METHOD(ResetState, "generation_due_for_dungeon is true once below the map limit, outside armageddon, and the interval has elapsed", "[kfx_sim][room_entrance]") {
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->turns_between_entrance_generation = 0; // interval of 0 -- always "elapsed" against turn 0
    dungeon->last_entrance_generation_gameturn = 0;

    CHECK(generation_due_for_dungeon(dungeon));
}

TEST_CASE_METHOD(ResetState, "generation_available_to_dungeon requires a RoRoF_CrPoolSpawn room and spare creature capacity", "[kfx_sim][room_entrance]") {
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->max_creatures_attracted = 5;
    dungeon->num_active_creatrs = 0;

    CHECK_FALSE(generation_available_to_dungeon(dungeon)); // no spawn room yet

    kfx_config_state.conf.slab_conf.room_types_count = RoK_ENTRANCE + 1;
    kfx_config_state.conf.slab_conf.room_cfgstats[RoK_ENTRANCE].roles = RoRoF_CrPoolSpawn;
    dungeon->room_list_start[RoK_ENTRANCE] = 1;
    CHECK(generation_available_to_dungeon(dungeon));

    dungeon->num_active_creatrs = 5; // at capacity now
    CHECK_FALSE(generation_available_to_dungeon(dungeon));
}

TEST_CASE_METHOD(ResetState, "calculate_attractive_room_quantity RoK_LAIR scales by unused-capacity fraction and subtracts owned creatures", "[kfx_sim][room_entrance]") {
    struct Dungeon *dungeon = get_dungeon(0);
    make_single_room(dungeon, RoK_LAIR, 10, 100, 50); // used_fraction = 50*256/100 = 128
    dungeon->owned_creatures_of_model[3] = 1;

    CHECK(calculate_attractive_room_quantity(RoK_LAIR, 0, 3) == 1); // (10*(256-128))/256/2 - 1 == 2-1
}

TEST_CASE_METHOD(ResetState, "calculate_attractive_room_quantity RoK_DUNGHEART/_BRIDGE give one point per 9 slabs, minus owned creatures", "[kfx_sim][room_entrance]") {
    struct Dungeon *dungeon = get_dungeon(0);
    make_single_room(dungeon, RoK_DUNGHEART, 27, 0, 0);
    dungeon->owned_creatures_of_model[3] = 1;

    CHECK(calculate_attractive_room_quantity(RoK_DUNGHEART, 0, 3) == 2); // 27/9 - 1
}

TEST_CASE_METHOD(ResetState, "calculate_attractive_room_quantity's generic room kinds give one point per 3 (or 4) slabs, minus owned creatures", "[kfx_sim][room_entrance]") {
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->owned_creatures_of_model[3] = 1;

    make_single_room(dungeon, RoK_LIBRARY, 9, 0, 0);
    CHECK(calculate_attractive_room_quantity(RoK_LIBRARY, 0, 3) == 2); // 9/3 - 1

    make_single_room(dungeon, RoK_WORKSHOP, 12, 0, 0);
    CHECK(calculate_attractive_room_quantity(RoK_WORKSHOP, 0, 3) == 2); // 12/4 - 1
}

TEST_CASE_METHOD(ResetState, "calculate_attractive_room_quantity RoK_TREASURE scales by used-capacity fraction, no owned-creature subtraction", "[kfx_sim][room_entrance]") {
    struct Dungeon *dungeon = get_dungeon(0);
    make_single_room(dungeon, RoK_TREASURE, 10, 100, 50); // used_fraction = 128
    dungeon->owned_creatures_of_model[3] = 1; // deliberately non-zero -- must not affect the result

    CHECK(calculate_attractive_room_quantity(RoK_TREASURE, 0, 3) == 1); // (10*128)/256/3
}

TEST_CASE_METHOD(ResetState, "calculate_attractive_room_quantity is 0 for RoK_NONE regardless of dungeon state", "[kfx_sim][room_entrance]") {
    CHECK(calculate_attractive_room_quantity(RoK_NONE, 0, 3) == 0);
}
