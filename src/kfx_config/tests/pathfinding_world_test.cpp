// kfx_config: pathfinding_world.c -- the callback-registration side of
// the 51-entry PathfindingWorldCallbacks interface docs/Architecture/
// testing-harness.md §10 flags as needing "a dedicated sub-effort" to
// exercise from kfx_pathfinding's "big three" (ariadne.c/_update.c/
// _wallhug.c) -- that sub-effort (a real fake provider driving actual
// pathfinding logic) is NOT what this file is. This is just the
// interface's own default no-op table, the same trivial shape as every
// other *Callbacks registration file in kfx_config: every noop_* stub
// ignores its pointer arguments (verified by reading each one), so
// nullptr is safe throughout, no map/creature fixture needed.
#include <catch2/catch_test_macros.hpp>

#include "pathfinding_world.h"

#include <string>

TEST_CASE("the default pathfinding_world table's every stub is a safe no-op returning its documented default", "[kfx_config][pathfinding_world]") {
    REQUIRE(pathfinding_world != nullptr);

    CHECK(pathfinding_world->get_map_size_x() == 0);
    CHECK(pathfinding_world->get_map_size_y() == 0);
    CHECK(pathfinding_world->get_map_block_at(0, 0) == nullptr);
    CHECK(pathfinding_world->get_map_block_at_pos(0) == nullptr);
    CHECK(pathfinding_world->map_block_flags(nullptr) == 0);
    CHECK(pathfinding_world->map_block_is_invalid(nullptr));
    CHECK(pathfinding_world->get_floor_filled_subtiles_at(0, 0) == 0);
    CHECK_FALSE(pathfinding_world->subtile_is_unsafe(0, 0));
    CHECK(pathfinding_world->get_slabmap_block(0, 0) == nullptr);
    CHECK(pathfinding_world->slabmap_block_kind(nullptr) == 0);
    CHECK(pathfinding_world->slabmap_block_is_invalid(nullptr));
    CHECK(pathfinding_world->slabmap_owner(nullptr) == 0);
    CHECK_FALSE(pathfinding_world->is_valid_hug_subtile(0, 0, 0));
    CHECK_FALSE(pathfinding_world->subtile_is_door(0, 0));
    CHECK_FALSE(pathfinding_world->thing_in_wall_at(nullptr, nullptr));

    CHECK(pathfinding_world->get_door_for_position(0, 0) == nullptr);
    CHECK_FALSE(pathfinding_world->door_is_hidden_to_player(nullptr, 0));
    CHECK_FALSE(pathfinding_world->door_will_open_for_thing(nullptr, nullptr));
    CHECK_FALSE(pathfinding_world->door_is_locked(nullptr));
    CHECK_FALSE(pathfinding_world->players_are_mutual_allies(0, 1));

    CHECK(pathfinding_world->thing_is_invalid(nullptr));
    CHECK(pathfinding_world->thing_get_owner(nullptr) == 0);
    CHECK(pathfinding_world->get_thing_height_at(nullptr, nullptr) == 0);
    CHECK(pathfinding_world->get_floor_height_under_thing_at(nullptr, nullptr) == 0);
    CHECK_FALSE(pathfinding_world->creature_can_travel_over_lava(nullptr));
    CHECK_FALSE(pathfinding_world->creature_steps_into_toxic_terrain(nullptr, nullptr));
    CHECK(std::string(pathfinding_world->thing_model_name(nullptr)).empty());

    struct Coord3d pos = pathfinding_world->thing_get_position(nullptr);
    CHECK(pos.x.val == 0);
    CHECK(pos.y.val == 0);
    CHECK(pos.z.val == 0);
    pathfinding_world->thing_set_position(nullptr, nullptr);
    CHECK(pathfinding_world->thing_get_move_angle(nullptr) == 0);
    pathfinding_world->thing_set_move_angle(nullptr, 0);
    CHECK(pathfinding_world->thing_get_index(nullptr) == 0);
    CHECK(pathfinding_world->thing_get_clipbox_size(nullptr) == 0);

    CHECK(pathfinding_world->creature_get_navigation(nullptr) == nullptr);
    CHECK(pathfinding_world->creature_get_ariadne_state(nullptr) == nullptr);
    CHECK(pathfinding_world->creature_get_max_speed(nullptr) == 0);
    pathfinding_world->creature_clear_state_flags_for_wallhug_override(nullptr);

    CHECK(pathfinding_world->get_subtile_number(0, 0) == 0);
    CHECK(pathfinding_world->stl_num_decode_x(0) == 0);
    CHECK(pathfinding_world->stl_num_decode_y(0) == 0);
    CHECK(pathfinding_world->stl_slab_center_subtile(0) == 0);
    CHECK(pathfinding_world->get_slabmap_for_subtile(0, 0) == nullptr);
    CHECK_FALSE(pathfinding_world->hug_can_move_on(nullptr, 0, 0));
    CHECK_FALSE(pathfinding_world->cross_x_boundary_first(nullptr, nullptr));
    CHECK_FALSE(pathfinding_world->cross_y_boundary_first(nullptr, nullptr));
    struct Around around = pathfinding_world->get_small_around(0);
    CHECK(around.delta_x == 0);
    CHECK(around.delta_y == 0);
    CHECK(pathfinding_world->get_small_around_length() == 0);
    CHECK(pathfinding_world->small_around_index_in_direction(0, 0, 1, 1) == 0);
    CHECK(pathfinding_world->get_map_size_z() == 0);
    CHECK(pathfinding_world->creature_cannot_move_directly_to(nullptr, nullptr));

    CHECK(pathfinding_world->get_owner_player_navigating() == -1);
    pathfinding_world->set_owner_player_navigating(0);
    CHECK(pathfinding_world->get_nav_thing_can_travel_over_lava() == 0);
    pathfinding_world->set_nav_thing_can_travel_over_lava(0);
}

TEST_CASE("set_pathfinding_world_callbacks installs a custom table and falls back to the default once cleared", "[kfx_config][pathfinding_world]") {
    struct PathfindingWorldCallbacks fake = *pathfinding_world;
    static bool called = false;
    called = false;
    fake.get_map_size_x = []() -> MapSubtlCoord { called = true; return 85; };

    set_pathfinding_world_callbacks(&fake);
    CHECK(pathfinding_world->get_map_size_x() == 85);
    CHECK(called);

    set_pathfinding_world_callbacks(nullptr);
    called = false;
    CHECK(pathfinding_world->get_map_size_x() == 0);
    CHECK_FALSE(called);
}
