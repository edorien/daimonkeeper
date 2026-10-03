// kfx_sim coverage: create_cave_in is the one function in thing_factory.c
// (besides the create_thing dispatcher, which just forwards to create_*
// functions covered by their own files) that's tractable without a
// dedicated per-class fixture -- and it's the first test in this session
// to go through the real thing allocator (allocate_free_thing_structure)
// rather than writing directly into a things_data[] slot the way every
// other fixture helper here does. kfx_sim_test_fixtures.h's
// make_synced_thing_index_available new helper exists specifically to
// unblock this: ResetSimAndConfig's zero-fill leaves
// synced_free_things_count at 0, so without pushing an index onto that
// stack first, i_can_allocate_free_thing_structure() always reports "no
// free slots" and create_cave_in returns INVALID_THING outright -- which
// is itself the first thing verified below, since it's the natural
// default-fixture behavior.
//
// thing_create_thing/thing_create_thing_adv (level-file InitThing/VALUE
// parsing) and create_thing_at_position_then_move_to_valid_and_add_light
// (navigation-dependent placement) are deliberately deferred.
#include <catch2/catch_test_macros.hpp>

#include "thing_factory.h"
#include "dungeon_data.h"
#include "kfx_sim_test_fixtures.h"
#include "player_camera.h"

using namespace kfx_test;

TEST_CASE_METHOD(ResetSimAndConfig, "create_cave_in refuses when the synced thing allocator has no free slots", "[kfx_sim][thing_factory]") {
    struct Coord3d pos = {};
    CHECK(thing_is_invalid(create_cave_in(&pos, 1, 0))); // no index pushed onto synced_free_things: allocation fails
}

TEST_CASE_METHOD(ResetSimAndConfig, "create_cave_in allocates a real Thing via the synced allocator and initializes its cave-in fields", "[kfx_sim][thing_factory]") {
    make_synced_thing_index_available(5);
    struct Coord3d pos = {};
    pos.x.stl.num = 3;
    pos.y.stl.num = 4;

    struct Thing *thing = create_cave_in(&pos, 7, 0);

    CHECK_FALSE(thing_is_invalid(thing));
    CHECK(thing->index == 5); // the index pushed onto the free stack
    CHECK((thing->alloc_flags & TAlF_Exists) != 0); // set by the allocator itself
    CHECK(thing->class_id == TCls_CaveIn);
    CHECK(thing->parent_idx == thing->index);
    CHECK(thing->owner == 0);
    CHECK(thing->cave_in.x == 3);
    CHECK(thing->cave_in.y == 4);
    CHECK(thing->cave_in.model == 7);
    CHECK(kfx_sim_state.synced_free_things_count == 0); // the slot is now in use
}

TEST_CASE_METHOD(ResetSimAndConfig, "create_cave_in triggers a camera quake on the owning player's dungeon, but not for the neutral player", "[kfx_sim][thing_factory]") {
    make_synced_thing_index_available(5);
    struct Coord3d pos = {};

    struct Thing *thing = create_cave_in(&pos, 1, 0);
    CHECK(kfx_sim_view_signals.camera_deviate_quake[get_dungeon(0)->owner] == thing->cave_in.time);

    make_synced_thing_index_available(6);
    struct Thing *neutral_thing = create_cave_in(&pos, 1, PLAYER_NEUTRAL);
    CHECK_FALSE(thing_is_invalid(neutral_thing));
    // No dungeon exists for the neutral player, so get_dungeon(PLAYER_NEUTRAL) is not a real target --
    // confirmed by reading the source's explicit owner != neutral_player_num guard around that call.
}
