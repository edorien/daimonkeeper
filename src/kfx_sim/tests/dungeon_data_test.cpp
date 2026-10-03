// kfx_sim coverage, per docs/refactor/testing/comprehensive/
// stage-08b-kfx-sim-clusters.md's "still open" list: dungeon_data.c, a
// previously wholly-untouched file and (aside from thing_creature.c) the
// biggest remaining source of self-contained pattern-A functions in
// kfx_sim -- almost every function here is a bounds-checked field
// read/write on kfx_sim_state.dungeon[], the same broad-coverage-pass
// shape used for config_creature.c elsewhere in this plan. add_heart_health's
// warn_on_damage branch (event_create_event_or_update_nearby_existing_event/
// audio_play_sound_message()/controller rumble) and the room-list-
// walking accessors that need a live struct Room fixture
// (init_dungeon_essential_position's "room found" branch,
// get_player_soul_container's thing_exists path) are covered only on
// their simpler branches; deeper fixturing is left for a later increment.
#include <catch2/catch_test_macros.hpp>

#include "dungeon_data.h"
#include "room_data.h"
#include "player_data.h"
#include "thing_data.h"
#include "globals.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"

#include <cstring>

namespace {
struct ResetState {
    ResetState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        kfx_config_state.neutral_player_num = PLAYER_NEUTRAL;
    }
};
}

TEST_CASE_METHOD(ResetState, "get_dungeon/dungeon_invalid bounds-check plyr_num against 0..DUNGEONS_COUNT-1", "[kfx_sim][dungeon_data]") {
    CHECK(dungeon_invalid(get_dungeon(-1)));
    CHECK(dungeon_invalid(get_dungeon(DUNGEONS_COUNT)));
    CHECK_FALSE(dungeon_invalid(get_dungeon(0)));
}

TEST_CASE_METHOD(ResetState, "clear_dungeons resets every dungeon (and bad_dungeon) to the reserved-owner sentinel", "[kfx_sim][dungeon_data]") {
    get_dungeon(0)->total_area = 123;
    clear_dungeons();
    CHECK(get_dungeon(0)->total_area == 0);
    CHECK(get_dungeon(0)->owner == PLAYERS_COUNT);
}

TEST_CASE_METHOD(ResetState, "increase/decrease_dungeon_area adjust total_area, clamped at 0, skipped for the neutral player", "[kfx_sim][dungeon_data]") {
    struct Dungeon *dungeon = get_dungeon(0);
    increase_dungeon_area(0, 50);
    CHECK(dungeon->total_area == 50);
    decrease_dungeon_area(0, 80); // would go negative -- clamps to 0
    CHECK(dungeon->total_area == 0);

    increase_dungeon_area(PLAYER_NEUTRAL, 50);
    CHECK(get_dungeon(PLAYER_NEUTRAL)->total_area == 0); // no-op for the neutral player
}

TEST_CASE_METHOD(ResetState, "increase/decrease_room_area adjust both room_manage_area and total_area together, clamped at 0", "[kfx_sim][dungeon_data]") {
    struct Dungeon *dungeon = get_dungeon(0);
    increase_room_area(0, 30);
    CHECK(dungeon->room_manage_area == 30);
    CHECK(dungeon->total_area == 30);
    decrease_room_area(0, 100); // both clamp to 0 rather than going negative
    CHECK(dungeon->room_manage_area == 0);
    CHECK(dungeon->total_area == 0);
}

TEST_CASE_METHOD(ResetState, "player_add_offmap_gold adds/removes gold, clamping a removal to what's actually owned", "[kfx_sim][dungeon_data]") {
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->offmap_money_owned = 100;
    dungeon->total_money_owned = 100;

    player_add_offmap_gold(0, -150); // would remove more than owned -- clamps to -100

    CHECK(dungeon->offmap_money_owned == 0);
    CHECK(dungeon->total_money_owned == 0);
}

TEST_CASE_METHOD(ResetState, "player_add_offmap_gold is a no-op for a player with no dungeon", "[kfx_sim][dungeon_data]") {
    player_add_offmap_gold(-1, 100); // doesn't crash, and there's nothing to assert on but the lack of one
    CHECK(true);
}

TEST_CASE_METHOD(ResetState, "player_has_room_of_role/count_player_discrete_rooms_with_role check the neutral player and role match", "[kfx_sim][dungeon_data]") {
    kfx_config_state.conf.slab_conf.room_types_count = RoK_LIBRARY + 1;
    kfx_config_state.conf.slab_conf.room_cfgstats[RoK_LIBRARY].roles = RoRoF_Research;
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->room_list_start[RoK_LIBRARY] = 1;
    dungeon->room_discrete_count[RoK_LIBRARY] = 3;

    CHECK(player_has_room_of_role(0, RoRoF_Research));
    CHECK(count_player_discrete_rooms_with_role(0, RoRoF_Research) == 3);
    CHECK_FALSE(player_has_room_of_role(PLAYER_NEUTRAL, RoRoF_Research));
    CHECK(count_player_discrete_rooms_with_role(PLAYER_NEUTRAL, RoRoF_Research) == 0);
}

TEST_CASE_METHOD(ResetState, "get_player_soul_container/player_has_heart resolve through dnheart_idx", "[kfx_sim][dungeon_data]") {
    struct Dungeon *dungeon = get_dungeon(0);
    CHECK(get_player_soul_container(0) == INVALID_THING); // dnheart_idx==0

    dungeon->dnheart_idx = 5;
    struct Thing *heart = thing_get(5);
    heart->alloc_flags = TAlF_Exists;

    CHECK(get_player_soul_container(0) == heart);
    CHECK(player_has_heart(0));
}

TEST_CASE_METHOD(ResetState, "dungeon_has_room/dungeon_has_room_of_role bounds-check rkind and read room_list_start", "[kfx_sim][dungeon_data]") {
    kfx_config_state.conf.slab_conf.room_types_count = RoK_LIBRARY + 1;
    kfx_config_state.conf.slab_conf.room_cfgstats[RoK_LIBRARY].roles = RoRoF_Research;
    struct Dungeon *dungeon = get_dungeon(0);

    CHECK_FALSE(dungeon_has_room(dungeon, RoK_LIBRARY));
    CHECK_FALSE(dungeon_has_room(dungeon, 0)); // rkind 0 is out of the valid [1, count) range
    dungeon->room_list_start[RoK_LIBRARY] = 1;
    CHECK(dungeon_has_room(dungeon, RoK_LIBRARY));
    CHECK(dungeon_has_room_of_role(dungeon, RoRoF_Research));
    CHECK_FALSE(dungeon_has_room_of_role(dungeon, RoRoF_Torture));
}

TEST_CASE_METHOD(ResetState, "player_creature_tends_to/toggle_creature_tendencies/set_creature_tendencies manipulate creature_tendencies", "[kfx_sim][dungeon_data]") {
    struct PlayerInfo *player = get_player(0);
    player->id_number = 0;

    CHECK_FALSE(player_creature_tends_to(0, CrTend_Imprison));
    CHECK(toggle_creature_tendencies(player, CrTend_Imprison));
    CHECK(player_creature_tends_to(0, CrTend_Imprison));
    CHECK(set_creature_tendencies(player, CrTend_Imprison, false));
    CHECK_FALSE(player_creature_tends_to(0, CrTend_Imprison));
    CHECK_FALSE(player_creature_tends_to(PLAYER_NEUTRAL, CrTend_Imprison));
}

TEST_CASE_METHOD(ResetState, "set_trap/door_buildable_and_add_to_amount bounds-check the model and accumulate placeable/offmap amounts", "[kfx_sim][dungeon_data]") {
    kfx_config_state.conf.trapdoor_conf.trap_types_count = 5;
    kfx_config_state.conf.trapdoor_conf.door_types_count = 5;
    CHECK_FALSE(set_trap_buildable_and_add_to_amount(0, 0, true, 1)); // model 0 is reserved/out of range
    CHECK_FALSE(set_trap_buildable_and_add_to_amount(0, 10, true, 1)); // >= trap_types_count

    struct Dungeon *dungeon = get_dungeon(0);
    CHECK(set_trap_buildable_and_add_to_amount(0, 2, true, 3));
    CHECK((dungeon->mnfct_info.trap_build_flags[2] & MnfBldF_Manufacturable) != 0);
    CHECK((dungeon->mnfct_info.trap_build_flags[2] & MnfBldF_Built) != 0); // amount>0
    CHECK(dungeon->mnfct_info.trap_amount_placeable[2] == 3);

    CHECK(set_door_buildable_and_add_to_amount(0, 2, false, 0));
    CHECK((dungeon->mnfct_info.door_build_flags[2] & MnfBldF_Manufacturable) == 0);
}

TEST_CASE_METHOD(ResetState, "dungeon_has_any_buildable_traps/_doors sum stored+offmap amounts across models", "[kfx_sim][dungeon_data]") {
    kfx_config_state.conf.trapdoor_conf.trap_types_count = 3;
    kfx_config_state.conf.trapdoor_conf.door_types_count = 3;
    struct Dungeon *dungeon = get_dungeon(0);

    CHECK_FALSE(dungeon_has_any_buildable_traps(dungeon));
    dungeon->mnfct_info.trap_amount_stored[1] = 1;
    CHECK(dungeon_has_any_buildable_traps(dungeon));

    CHECK_FALSE(dungeon_has_any_buildable_doors(dungeon));
    dungeon->mnfct_info.door_amount_offmap[2] = 1;
    CHECK(dungeon_has_any_buildable_doors(dungeon));
}

TEST_CASE_METHOD(ResetState, "restart_script_timer/add_to_script_timer/set_script_flag bounds-check their ids and write dungeon state", "[kfx_sim][dungeon_data]") {
    CHECK_FALSE(restart_script_timer(0, -1));
    CHECK_FALSE(restart_script_timer(0, TURN_TIMERS_COUNT));
    CHECK(restart_script_timer(0, 2));
    struct Dungeon *dungeon = get_dungeon(0);
    CHECK(dungeon->turn_timers[2].state == 1);
    CHECK(dungeon->turn_timers[2].count == 0); // default GetGameTurnFunc returns 0

    add_to_script_timer(0, 2, 5);
    CHECK(dungeon->turn_timers[2].count == -5);

    CHECK_FALSE(set_script_flag(0, -1, 1));
    CHECK_FALSE(set_script_flag(0, SCRIPT_FLAGS_COUNT, 1));
    CHECK(set_script_flag(0, 3, 42));
    CHECK(dungeon->script_flags[3] == 42);
}

TEST_CASE_METHOD(ResetState, "mark_creature_joined_dungeon refuses the neutral player and otherwise increments creature_models_joined", "[kfx_sim][dungeon_data]") {
    struct Thing *creatng = thing_get(1);
    creatng->owner = PLAYER_NEUTRAL;
    CHECK_FALSE(mark_creature_joined_dungeon(creatng));

    creatng->owner = 0;
    creatng->model = 4;
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->owned_creatures_of_model[4] = 2; // avoids the event_create_event branch (needs <=1)

    CHECK(mark_creature_joined_dungeon(creatng));
    CHECK(dungeon->creature_models_joined[4] == 1);
}

TEST_CASE_METHOD(ResetState, "init_dungeon_essential_position falls back to the map center when the dungeon has no rooms", "[kfx_sim][dungeon_data]") {
    kfx_config_state.conf.slab_conf.room_types_count = 2;
    kfx_sim_state.map_subtiles_x = 10;
    kfx_sim_state.map_subtiles_y = 20;
    struct Dungeon *dungeon = get_dungeon(0); // room_list_start is all zero -> every room_get() is invalid

    init_dungeon_essential_position(dungeon);

    CHECK(dungeon->essential_pos.x.val == subtile_coord_center(5));
    CHECK(dungeon->essential_pos.y.val == subtile_coord_center(10));
}

TEST_CASE_METHOD(ResetState, "init_dungeons sets per-dungeon defaults (owner, modifiers, max_creatures_attracted)", "[kfx_sim][dungeon_data]") {
    kfx_config_state.conf.rules[1].rooms.default_max_crtrs_gen_entrance = 7;

    init_dungeons();

    struct Dungeon *dungeon = get_dungeon(1);
    CHECK(dungeon->owner == 1);
    CHECK(dungeon->color_idx == 1);
    CHECK(dungeon->modifier.health == 100);
    CHECK(dungeon->modifier.loyalty == 100);
    CHECK(dungeon->max_creatures_attracted == 7);
}

TEST_CASE_METHOD(ResetState, "player_has_valid_dungeon[_with_heart]/players_num_dungeon_valid[_with_heart] wrap dungeon_invalid/player_has_heart", "[kfx_sim][dungeon_data]") {
    CHECK(player_has_valid_dungeon(0));
    CHECK_FALSE(player_has_valid_dungeon(-1));
    CHECK_FALSE(player_has_valid_dungeon_with_heart(0)); // no heart yet

    struct PlayerInfo *player = get_player(0);
    player->id_number = 0;
    CHECK(players_num_dungeon_valid(0));

    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->dnheart_idx = 5;
    thing_get(5)->alloc_flags = TAlF_Exists;
    CHECK(player_has_valid_dungeon_with_heart(0));
    CHECK(players_num_dungeon_valid_with_heart(0));
}

TEST_CASE_METHOD(ResetState, "set_creature_availability/try_set_backup_heart_idx write their dungeon fields directly", "[kfx_sim][dungeon_data]") {
    set_creature_availability(0, 3, 1, 0);
    struct Dungeon *dungeon = get_dungeon(0);
    CHECK(dungeon->creature_allowed[3] == 1);
    CHECK(dungeon->creature_force_enabled[3] == 0);

    try_set_backup_heart_idx(0, 9);
    CHECK(dungeon->backup_heart_idx == 9);
    try_set_backup_heart_idx(0, 42); // already set -- first call wins
    CHECK(dungeon->backup_heart_idx == 9);
}

TEST_CASE_METHOD(ResetState, "room resrchable/buildable accessors round-trip through their dungeon arrays", "[kfx_sim][dungeon_data]") {
    kfx_config_state.conf.slab_conf.room_types_count = RoK_LIBRARY + 1;

    CHECK(set_room_resrchable_and_buildable(0, RoK_LIBRARY, 1, 1));
    CHECK(get_room_resrchable(0, RoK_LIBRARY));
    CHECK(get_room_buildable(0, RoK_LIBRARY));

    CHECK_FALSE(set_room_resrchable_and_buildable(0, RoK_LIBRARY, 0, 1)); // resrch==0 clears buildable too
    CHECK_FALSE(get_room_resrchable(0, RoK_LIBRARY));
    CHECK_FALSE(get_room_buildable(0, RoK_LIBRARY));

    set_all_room_resrchable(0);
    CHECK(get_room_resrchable(0, RoK_LIBRARY));
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->room_buildable[RoK_LIBRARY] = 0;
    set_all_room_buildable_from_resrchable(0);
    CHECK(get_room_buildable(0, RoK_LIBRARY));
}

TEST_CASE_METHOD(ResetState, "magic resrchable/level accessors round-trip through their dungeon arrays", "[kfx_sim][dungeon_data]") {
    kfx_config_state.conf.magic_conf.power_types_count = 3;
    struct PlayerInfo *player = get_player(0);
    player->id_number = 0;

    CHECK_FALSE(get_magic_resrchable(0, 1));
    set_magic_resrchable(0, 1, true);
    CHECK(get_magic_resrchable(0, 1));

    set_all_magic_resrchable_unchecked(0);
    CHECK(get_magic_resrchable(0, 0));
    CHECK(get_magic_resrchable(0, 2));

    CHECK_FALSE(get_magic_level_gt0(0, 1));
    get_dungeon(0)->magic_level[1] = 1;
    CHECK(get_magic_level_gt0(0, 1));
}

TEST_CASE_METHOD(ResetState, "trap/door placeable/manufacturable/built accessors read the dungeon's mnfct_info", "[kfx_sim][dungeon_data]") {
    struct PlayerInfo *player = get_player(0);
    player->id_number = 0;
    struct Dungeon *dungeon = get_dungeon(0);

    dungeon->mnfct_info.trap_amount_placeable[2] = 1;
    dungeon->mnfct_info.trap_build_flags[2] = MnfBldF_Manufacturable | MnfBldF_Built;
    CHECK(get_trap_placeable(0, 2));
    CHECK(get_trap_manufacturable(0, 2));
    CHECK(get_trap_built(0, 2));

    dungeon->mnfct_info.door_amount_placeable[3] = 1;
    dungeon->mnfct_info.door_build_flags[3] = MnfBldF_Manufacturable;
    CHECK(get_door_placeable(0, 3));
    CHECK(get_door_manufacturable(0, 3));
    CHECK_FALSE(get_door_built(0, 3));
}
