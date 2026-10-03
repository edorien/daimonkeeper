// kfx_sim coverage: a first pass over room_workshop.c's manufactured-item
// amount bookkeeping -- Dungeon::mnfct_info's per-model
// stored/placeable/offmap counters and build_flags, manipulated by
// add_workshop_item_to_amounts/readd_workshop_item_to_amount_placeable/
// remove_workshop_item_from_amount_stored/_placeable. These carry real
// arithmetic (the "placeable amount lost sync, clamp it back in range"
// self-healing checks) and multi-way returns (stored vs. offmap vs. none
// for remove_workshop_item_from_amount_stored), not just field reads.
// calculate_manufacture_level's loop is tested via the same
// link_room_into_owner_list fixture helper used for room-capacity
// functions elsewhere this session.
//
// Deliberately deferred: everything else in this file (crate/thing
// creation, get_next_manufacture's doable-manufacture selection,
// process_player_manufacturing, book/crate repositioning) -- thing
// creation and event-dispatch heavy.
#include <catch2/catch_test_macros.hpp>

#include "room_workshop.h"
#include "dungeon_data.h"
#include "room_data.h"
#include "config_terrain.h"
#include "kfx_sim_test_fixtures.h"
#include "config_objects.h"
#include "thing_data.h"

using namespace kfx_test;

TEST_CASE_METHOD(ResetSimAndConfig, "add_workshop_item_to_amounts increments stored/placeable and flags the model as built, for both traps and doors", "[kfx_sim][room_workshop]") {
    struct Dungeon *dungeon = get_dungeon(0);
    CHECK(add_workshop_item_to_amounts(0, TCls_Trap, 3));
    CHECK(dungeon->mnfct_info.trap_amount_stored[3] == 1);
    CHECK(dungeon->mnfct_info.trap_amount_placeable[3] == 1);
    CHECK((dungeon->mnfct_info.trap_build_flags[3] & MnfBldF_Built) != 0);

    CHECK(add_workshop_item_to_amounts(0, TCls_Door, 5));
    CHECK(dungeon->mnfct_info.door_amount_stored[5] == 1);
    CHECK(dungeon->mnfct_info.door_amount_placeable[5] == 1);

    CHECK_FALSE(add_workshop_item_to_amounts(0, TCls_Creature, 1)); // illegal class
}

TEST_CASE_METHOD(ResetSimAndConfig, "add_workshop_item_to_amounts clamps a placeable count that's drifted out of range", "[kfx_sim][room_workshop]") {
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->mnfct_info.trap_amount_stored[3] = 5;
    dungeon->mnfct_info.trap_amount_offmap[3] = 0;
    dungeon->mnfct_info.trap_amount_placeable[3] = 200; // already absurdly larger than stored+offmap

    CHECK(add_workshop_item_to_amounts(0, TCls_Trap, 3));
    CHECK(dungeon->mnfct_info.trap_amount_stored[3] == 6); // incremented normally
    // Clamped back down to stored+offmap, since placeable can never exceed what's actually available.
    CHECK(dungeon->mnfct_info.trap_amount_placeable[3] == 6);
}

TEST_CASE_METHOD(ResetSimAndConfig, "readd_workshop_item_to_amount_placeable only touches the placeable counter", "[kfx_sim][room_workshop]") {
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->mnfct_info.trap_amount_stored[3] = 5;
    CHECK(readd_workshop_item_to_amount_placeable(0, TCls_Trap, 3));
    CHECK(dungeon->mnfct_info.trap_amount_placeable[3] == 1);
    CHECK(dungeon->mnfct_info.trap_amount_stored[3] == 5); // untouched
}

TEST_CASE_METHOD(ResetSimAndConfig, "remove_workshop_item_from_amount_stored prefers a stored item, falls back to offmap, then reports none available", "[kfx_sim][room_workshop]") {
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->mnfct_info.trap_amount_stored[3] = 1;
    dungeon->mnfct_info.trap_amount_offmap[3] = 1;

    CHECK(remove_workshop_item_from_amount_stored(0, TCls_Trap, 3, 0) == WrkCrtS_Stored);
    CHECK(dungeon->mnfct_info.trap_amount_stored[3] == 0);
    CHECK(dungeon->mnfct_info.trap_amount_offmap[3] == 1); // untouched by the stored removal

    CHECK(remove_workshop_item_from_amount_stored(0, TCls_Trap, 3, 0) == WrkCrtS_Offmap);
    CHECK(dungeon->mnfct_info.trap_amount_offmap[3] == 0);

    CHECK(remove_workshop_item_from_amount_stored(0, TCls_Trap, 3, 0) == WrkCrtS_None);
}

TEST_CASE_METHOD(ResetSimAndConfig, "remove_workshop_item_from_amount_stored's WrkCrtF_NoStored flag skips straight to offmap", "[kfx_sim][room_workshop]") {
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->mnfct_info.door_amount_stored[2] = 5;
    dungeon->mnfct_info.door_amount_offmap[2] = 1;

    CHECK(remove_workshop_item_from_amount_stored(0, TCls_Door, 2, WrkCrtF_NoStored) == WrkCrtS_Offmap);
    CHECK(dungeon->mnfct_info.door_amount_stored[2] == 5); // stored pool never touched
    CHECK(dungeon->mnfct_info.door_amount_offmap[2] == 0);
}

TEST_CASE_METHOD(ResetSimAndConfig, "remove_workshop_item_from_amount_placeable decrements placeable, flags the model used, and tallies lvstats", "[kfx_sim][room_workshop]") {
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->mnfct_info.trap_amount_placeable[3] = 2;

    CHECK(remove_workshop_item_from_amount_placeable(0, TCls_Trap, 3));
    CHECK(dungeon->mnfct_info.trap_amount_placeable[3] == 1);
    CHECK((dungeon->mnfct_info.trap_build_flags[3] & MnfBldF_Used) != 0);
    CHECK(dungeon->lvstats.traps_used == 1);

    dungeon->mnfct_info.trap_amount_placeable[3] = 0;
    CHECK_FALSE(remove_workshop_item_from_amount_placeable(0, TCls_Trap, 3)); // none left
    CHECK(dungeon->lvstats.traps_used == 1); // unchanged on failure
}

TEST_CASE_METHOD(ResetSimAndConfig, "calculate_manufacture_level scales with the total slab count of RoRoF_CratesManufctr rooms", "[kfx_sim][room_workshop]") {
    struct Dungeon *dungeon = get_dungeon(0);
    configure_room_role(RoK_WORKSHOP, RoRoF_CratesManufctr);
    CHECK(calculate_manufacture_level(dungeon) == 0); // no manufacturing rooms yet

    struct Room *workshop = make_room_at_slab(1, 0, 0, RoK_WORKSHOP, 0);
    workshop->slabs_count = 16;
    link_room_into_owner_list(&dungeon->room_list_start[RoK_WORKSHOP], 1);
    CHECK(calculate_manufacture_level(dungeon) == 1); // 16 > 3^2, not > 4^2

    workshop->slabs_count = 25;
    CHECK(calculate_manufacture_level(dungeon) == 2); // 25 > 3^2 and > 4^2, not > 5^2
}

// crate_thing_to_workshop_item_class/_model moved here from kfx_config's
// config_objects.c (refactor pass 2, S05).
TEST_CASE_METHOD(ResetSimAndConfig, "crate_thing_to_workshop_item_class/_model map a workshop crate to the trap or door it holds", "[kfx_sim][room_workshop]") {
    struct ObjectsConfig *objconf = &kfx_config_state.conf.object_conf;
    objconf->object_types_count = 4;
    objconf->object_cfgstats[2].genre = OCtg_WrkshpBox;
    objconf->workshop_object_class[2] = TCls_Trap;
    objconf->object_to_door_or_trap[2] = 4;
    objconf->object_to_door_or_trap[3] = 7;
    objconf->object_to_door_or_trap[0] = 9;

    struct Thing *crate = thing_get(1);
    crate->index = 1;
    crate->alloc_flags = TAlF_Exists;
    crate->class_id = TCls_Object;
    crate->model = 2;
    CHECK(crate_thing_to_workshop_item_class(crate) == TCls_Trap);
    CHECK(crate_thing_to_workshop_item_model(crate) == 4);

    crate->model = 3; // an object, but not a workshop crate
    CHECK(crate_thing_to_workshop_item_class(crate) == TCls_Object);
    CHECK(crate_thing_to_workshop_item_model(crate) == 7);

    crate->class_id = TCls_Creature; // not an object at all
    CHECK(crate_thing_to_workshop_item_class(crate) == TCls_Creature);
    CHECK(crate_thing_to_workshop_item_model(crate) == 9);
    CHECK(crate_thing_to_workshop_item_model(nullptr) == 9);
}
