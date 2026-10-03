// kfx_sim: player_availability.c -- per-player room/power/trap/door/
// creature availability, moved from kfx_config's config_terrain.c/
// config_magic.c/config_trapdoor.c/config_creature.c in refactor pass 2
// (S05). The kfx_config tests these replace ran against fakes of the
// DungeonAvailabilityCallbacks table; here the functions write and read
// the real struct Dungeon/struct PlayerInfo in kfx_sim_state.
//
// Player 0 with id_number 0 has a valid dungeon for both get_dungeon()
// and get_players_num_dungeon() after the reset; give_heart() adds the
// dungeon heart the *_with_heart checks look for (same setup as
// dungeon_data_test.cpp's validity test).
//
// set_room_available's computer-player notification (only reached when
// the room ends up buildable) and set_power_available's add_power_to_player
// path (only reached when the power isn't already available) call deep
// into player_computer.c/magic_powers.c and are left to their own tests
// and the level-script ftests; the cases below stay on the other branches.
#include <catch2/catch_test_macros.hpp>

#include "player_availability.h"
#include "kfx_sim_test_fixtures.h"
#include "config_creature.h"
#include "config_magic.h"
#include "config_terrain.h"
#include "config_trapdoor.h"

namespace {
struct ResetPlayer : kfx_test::ResetSimAndConfig {
    ResetPlayer() {
        get_player(0)->id_number = 0;
        std::memset(breed_activities, 0, sizeof(breed_activities));
    }
    void give_heart() {
        get_dungeon(0)->dnheart_idx = 5;
        thing_get(5)->alloc_flags = TAlF_Exists;
    }
};
}

TEST_CASE_METHOD(ResetPlayer, "set_room_available refuses a player with no dungeon or an out-of-range room, and otherwise writes the dungeon's room flags", "[kfx_sim][player_availability]") {
    kfx_config_state.conf.slab_conf.room_types_count = 4;
    CHECK_FALSE(set_room_available(-1, 1, 1, 0));
    CHECK_FALSE(set_room_available(0, 4, 1, 0));

    CHECK(set_room_available(0, 2, 1, 0));
    CHECK(get_dungeon(0)->room_resrchable[2] == 1);
    CHECK(get_dungeon(0)->room_buildable[2] == 0);

    get_dungeon(0)->room_buildable[3] = 1;
    CHECK(set_room_available(0, 3, 0, 0)); // not researchable clears buildable
    CHECK(get_dungeon(0)->room_buildable[3] == 0);
}

TEST_CASE_METHOD(ResetPlayer, "is_room_available/find_first_available_roomkind_with_role need a heart and a buildable room", "[kfx_sim][player_availability]") {
    kfx_config_state.conf.slab_conf.room_types_count = 4;
    kfx_config_state.conf.slab_conf.room_cfgstats[2].roles = RoRoF_GoldStorage;
    get_dungeon(0)->room_buildable[2] = 1;

    CHECK_FALSE(is_room_available(0, 2)); // no heart
    CHECK(find_first_available_roomkind_with_role(0, RoRoF_GoldStorage) == RoK_NONE);

    give_heart();
    CHECK(is_room_available(0, 2));
    CHECK_FALSE(is_room_available(0, 1));
    CHECK_FALSE(is_room_available(0, 4)); // out of range
    CHECK(find_first_available_roomkind_with_role(0, RoRoF_GoldStorage) == 2);
    CHECK(is_room_of_role_available(0, RoRoF_GoldStorage));
    CHECK_FALSE(is_room_of_role_available(0, RoRoF_PowersStorage));
}

TEST_CASE_METHOD(ResetPlayer, "is_room_obtainable accepts a buildable or a researchable room", "[kfx_sim][player_availability]") {
    kfx_config_state.conf.slab_conf.room_types_count = 4;
    give_heart();
    get_dungeon(0)->room_buildable[1] = 1;
    get_dungeon(0)->room_resrchable[2] = 1;
    CHECK(is_room_obtainable(0, 1));
    CHECK(is_room_obtainable(0, 2));
    CHECK_FALSE(is_room_obtainable(0, 3));
}

TEST_CASE_METHOD(ResetPlayer, "make_all_rooms_researchable then make_available_all_researchable_rooms makes every room buildable", "[kfx_sim][player_availability]") {
    kfx_config_state.conf.slab_conf.room_types_count = 3;
    CHECK_FALSE(make_all_rooms_researchable(-1));
    CHECK(make_all_rooms_researchable(0));
    CHECK(make_available_all_researchable_rooms(0));
    for (RoomKind rkind = 0; rkind < 3; rkind++) {
        CHECK(get_dungeon(0)->room_resrchable[rkind] == 1);
        CHECK(get_dungeon(0)->room_buildable[rkind] == 1);
    }
    CHECK(get_dungeon(0)->room_buildable[3] == 0);
}

TEST_CASE_METHOD(ResetPlayer, "is_power_available needs a heart except for POWER_POSSESS, and a positive magic level", "[kfx_sim][player_availability]") {
    kfx_config_state.conf.magic_conf.power_types_count = PwrK_POSSESS + 2;
    get_dungeon(0)->magic_level[PwrK_POSSESS] = 1;
    get_dungeon(0)->magic_level[PwrK_SLAP] = 1;

    CHECK(is_power_available(0, PwrK_POSSESS)); // no heart needed
    CHECK_FALSE(is_power_available(0, PwrK_SLAP));

    give_heart();
    CHECK(is_power_available(0, PwrK_SLAP));
    CHECK_FALSE(is_power_available(0, PwrK_HAND));
    CHECK_FALSE(is_power_available(0, PwrK_POSSESS + 2)); // out of range
}

TEST_CASE_METHOD(ResetPlayer, "is_power_obtainable accepts a positive magic level or a researchable power", "[kfx_sim][player_availability]") {
    kfx_config_state.conf.magic_conf.power_types_count = 4;
    give_heart();
    get_dungeon(0)->magic_level[1] = 1;
    get_dungeon(0)->magic_resrchable[2] = 1;
    CHECK(is_power_obtainable(0, 1));
    CHECK(is_power_obtainable(0, 2));
    CHECK_FALSE(is_power_obtainable(0, 3));
}

TEST_CASE_METHOD(ResetPlayer, "set_power_available records researchability and succeeds for an already-available power", "[kfx_sim][player_availability]") {
    kfx_config_state.conf.magic_conf.power_types_count = 4;
    CHECK_FALSE(set_power_available(-1, 1, 1, 1));

    CHECK(set_power_available(0, 1, 1, 0)); // avail 0, not available -> nothing to remove
    CHECK(get_dungeon(0)->magic_resrchable[1] == 1);

    give_heart();
    get_dungeon(0)->magic_level[2] = 1;
    CHECK(set_power_available(0, 2, 0, 1)); // already available -> no grant needed
    CHECK(get_dungeon(0)->magic_resrchable[2] == 0);
}

TEST_CASE_METHOD(ResetPlayer, "make_all_powers_researchable marks every configured power researchable", "[kfx_sim][player_availability]") {
    kfx_config_state.conf.magic_conf.power_types_count = 3;
    CHECK(make_all_powers_researchable(0));
    CHECK(get_dungeon(0)->magic_resrchable[0] == 1);
    CHECK(get_dungeon(0)->magic_resrchable[2] == 1);
    CHECK(get_dungeon(0)->magic_resrchable[3] == 0);
}

TEST_CASE_METHOD(ResetPlayer, "make_available_all_researchable_powers refuses a player with no dungeon and grants nothing when nothing is researchable", "[kfx_sim][player_availability]") {
    kfx_config_state.conf.magic_conf.power_types_count = 3;
    CHECK_FALSE(make_available_all_researchable_powers(-1));
    CHECK(make_available_all_researchable_powers(0));
}

TEST_CASE_METHOD(ResetPlayer, "is_trap_placeable/buildable/built need a heart (placeable/buildable), an in-range model, and the matching mnfct_info state", "[kfx_sim][player_availability]") {
    kfx_config_state.conf.trapdoor_conf.trap_types_count = 4;
    struct TrapInfo *mnfct = &get_dungeon(0)->mnfct_info;
    mnfct->trap_amount_placeable[1] = 1;
    mnfct->trap_build_flags[2] = MnfBldF_Manufacturable;
    mnfct->trap_build_flags[3] = MnfBldF_Built;

    CHECK_FALSE(is_trap_placeable(0, 1)); // no heart
    CHECK_FALSE(is_trap_buildable(0, 2));
    CHECK(is_trap_built(0, 3)); // no heart needed

    give_heart();
    CHECK(is_trap_placeable(0, 1));
    CHECK_FALSE(is_trap_placeable(0, 2));
    CHECK(is_trap_buildable(0, 2));
    CHECK_FALSE(is_trap_buildable(0, 1));
    CHECK_FALSE(is_trap_built(0, 2));
    CHECK_FALSE(is_trap_placeable(0, 0)); // model 0 is out of range
    CHECK_FALSE(is_trap_placeable(0, 4));
}

TEST_CASE_METHOD(ResetPlayer, "is_door_placeable/buildable/built need a heart, an in-range model, and the matching mnfct_info state", "[kfx_sim][player_availability]") {
    kfx_config_state.conf.trapdoor_conf.door_types_count = 4;
    struct TrapInfo *mnfct = &get_dungeon(0)->mnfct_info;
    mnfct->door_amount_placeable[1] = 1;
    mnfct->door_build_flags[2] = MnfBldF_Manufacturable;
    mnfct->door_build_flags[3] = MnfBldF_Built;

    CHECK_FALSE(is_door_built(0, 3)); // unlike traps, needs a heart too

    give_heart();
    CHECK(is_door_placeable(0, 1));
    CHECK(is_door_buildable(0, 2));
    CHECK(is_door_built(0, 3));
    CHECK_FALSE(is_door_built(0, 1));
    CHECK_FALSE(is_door_buildable(0, 4)); // out of range
}

TEST_CASE_METHOD(ResetPlayer, "make_available_all_doors/traps mark every door and trap model manufacturable", "[kfx_sim][player_availability]") {
    kfx_config_state.conf.trapdoor_conf.door_types_count = 3;
    kfx_config_state.conf.trapdoor_conf.trap_types_count = 3;
    CHECK_FALSE(make_available_all_doors(-1));
    CHECK_FALSE(make_available_all_traps(-1));
    CHECK(make_available_all_doors(0));
    CHECK(make_available_all_traps(0));
    struct TrapInfo *mnfct = &get_dungeon(0)->mnfct_info;
    CHECK((mnfct->door_build_flags[1] & MnfBldF_Manufacturable) != 0);
    CHECK((mnfct->door_build_flags[2] & MnfBldF_Manufacturable) != 0);
    CHECK((mnfct->trap_build_flags[2] & MnfBldF_Manufacturable) != 0);
    CHECK(mnfct->trap_build_flags[0] == 0); // model 0 is skipped
}

TEST_CASE_METHOD(ResetPlayer, "set_creature_available fails without a valid dungeon or an in-range model, and clamps force_avail", "[kfx_sim][player_availability]") {
    kfx_config_state.conf.crtr_conf.model_count = 2;
    CHECK_FALSE(set_creature_available(-1, 1, 1, 1));
    CHECK_FALSE(set_creature_available(0, 0, 1, 1)); // models start at 1
    CHECK_FALSE(set_creature_available(0, 5, 1, 1));

    struct Dungeon *dungeon = get_dungeon(0);
    // force_avail is clamped to CREATURES_COUNT-1 (1023), but
    // creature_force_enabled[] is unsigned char, so the stored value wraps
    // (1023 -> 255). Pinned here as found; the move didn't change it.
    CHECK(set_creature_available(0, 1, 1, CREATURES_COUNT + 10));
    CHECK(dungeon->creature_allowed[1] == 1);
    CHECK((int)dungeon->creature_force_enabled[1] == ((CREATURES_COUNT - 1) & 0xFF));

    CHECK(set_creature_available(0, 1, 0, -5));
    CHECK(dungeon->creature_allowed[1] == 0);
    CHECK(dungeon->creature_force_enabled[1] == 0);
}

TEST_CASE_METHOD(ResetPlayer, "get_players_special_digger_model prefers the player's own digger, then falls back by roaming status", "[kfx_sim][player_availability]") {
    kfx_config_state.conf.crtr_conf.special_digger_good = 3;
    kfx_config_state.conf.crtr_conf.special_digger_evil = 4;
    CHECK(get_players_special_digger_model(0) == 4); // keeper -> evil digger

    get_player(0)->player_type = PT_Roaming;
    CHECK(get_players_special_digger_model(0) == 3); // hero -> good digger

    get_player(0)->special_digger = 7;
    CHECK(get_players_special_digger_model(0) == 7);
}

TEST_CASE_METHOD(ResetPlayer, "get_players_spectator_model falls back to special_digger_good when no spectator breed is configured", "[kfx_sim][player_availability]") {
    kfx_config_state.conf.crtr_conf.special_digger_good = 3;
    CHECK(get_players_spectator_model(0) == 3);
    kfx_config_state.conf.crtr_conf.spectator_breed = 9;
    CHECK(get_players_spectator_model(0) == 9);
}

TEST_CASE_METHOD(ResetPlayer, "update_players_special_digger_model stores the new digger and, for the local player, swaps it in breed_activities", "[kfx_sim][player_availability]") {
    get_player(0)->special_digger = 5;
    breed_activities[0] = 5;
    breed_activities[1] = 6;

    update_players_special_digger_model(0, 5); // unchanged: no-op
    CHECK(breed_activities[0] == 5);

    my_player_number = 0;
    update_players_special_digger_model(0, 6);
    CHECK(get_player(0)->special_digger == 6);
    CHECK(breed_activities[0] == 6);
    CHECK(breed_activities[1] == 5);

    my_player_number = 1; // another player's change leaves the local list alone
    update_players_special_digger_model(0, 5);
    CHECK(get_player(0)->special_digger == 5);
    CHECK(breed_activities[0] == 6);
}
