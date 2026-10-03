// kfx_sim coverage, per docs/refactor/testing/comprehensive/
// stage-08b-kfx-sim-clusters.md's "still open" list: game_lifecycle.c, a
// previously wholly-untouched file of world-reset routines. All ten
// functions are pattern A on kfx_sim_state (plus reset_creature_max_levels
// reading kfx_config_state.conf.crtr_conf.model_count); the few port
// calls are exercised through the unwired defaults -- confirmed safe to
// call unregistered.
//
// Every fixture here deliberately keeps things/rooms/creatures *absent*
// (zeroed alloc/exists flags), which is real production shape: at the point
// these functions run (level reset, pre-save cleanup), most slots are
// already empty. That keeps delete_all_thing_structures/delete_all_structures/
// delete_all_room_structures on their guard-only branches (thing_exists/
// CCFlg_Exists/RoF_Allocated all false), not the heavier per-instance
// deletion machinery -- a deliberately smaller increment than fixturing a
// live thing/room/creature would be.
#include <catch2/catch_test_macros.hpp>

#include "game_lifecycle.h"
#include "globals.h"
#include "kfx_sim_state.h"
#include "kfx_config_state.h"
#include "dungeon_data.h"
#include "player_data.h"
#include "player_instances.h"
#include "thing_data.h"
#include "thing_list.h"
#include "creature_control.h"
#include "room_data.h"
#include "slab_data.h"
#include "map_columns.h"
#include "map_data.h"
#include "thing_stats.h"

#include <cstring>

namespace {
struct ResetSimState {
    ResetSimState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
    }
};
}

TEST_CASE_METHOD(ResetSimState, "clear_creature_pool zeroes the pool and sets is_empty", "[kfx_sim][game_lifecycle]") {
    kfx_sim_state.pool.is_empty = false;
    kfx_sim_state.pool.crtr_kind[3] = 7;

    clear_creature_pool();

    CHECK(kfx_sim_state.pool.is_empty == true);
    CHECK(kfx_sim_state.pool.crtr_kind[3] == 0);
}

TEST_CASE_METHOD(ResetSimState, "clear_map resets map blocks, slabs to rock, and columns within the configured map size", "[kfx_sim][game_lifecycle]") {
    kfx_sim_state.map_subtiles_x = 2;
    kfx_sim_state.map_subtiles_y = 2;
    kfx_sim_state.map_tiles_x = 1;
    kfx_sim_state.map_tiles_y = 1;

    struct Map *mapblk = get_map_block_at(1, 1);
    mapblk->mapwho = 99;
    struct SlabMap *slb = &kfx_sim_state.slabmap[0];
    slb->kind = SlbT_ROCK_FLOOR;
    struct Column *colmn = &kfx_sim_state.columns_data[0];
    colmn->floor_texture = 42;
    colmn->cubes[0] = 5;

    clear_map();

    CHECK(get_map_block_at(1, 1)->mapwho == 0);
    CHECK(slb->kind == SlbT_ROCK);
    CHECK(colmn->floor_texture == 1);
    CHECK(colmn->solidmask == 0); // make_solidmask() over an all-zero cubes[] after the memset
}

TEST_CASE_METHOD(ResetSimState, "clear_things_and_persons_data rebuilds every thing to the reserved-owner sentinel and the free-index lists", "[kfx_sim][game_lifecycle]") {
    kfx_sim_state.map_subtiles_x = 10;
    kfx_sim_state.map_subtiles_y = 10;
    kfx_sim_state.things_data[5].owner = 2;
    kfx_sim_state.things_data[5].alloc_flags = TAlF_Exists;
    kfx_sim_state.nodungeon_creatr_list_start = 123;
    kfx_sim_state.cctrl_data[7].creature_control_flags = CCFlg_Exists;
    kfx_sim_state.synced_free_things[0] = 999;

    clear_things_and_persons_data();

    CHECK(kfx_sim_state.things_data[5].owner == PLAYERS_COUNT);
    CHECK(kfx_sim_state.things_data[5].alloc_flags == 0);
    CHECK(kfx_sim_state.things_data[5].mappos.x.val == subtile_coord_center(5));
    CHECK(kfx_sim_state.things_data[5].mappos.y.val == subtile_coord_center(5));
    CHECK(kfx_sim_state.nodungeon_creatr_list_start == 0);
    CHECK(kfx_sim_state.cctrl_data[7].creature_control_flags == 0);
    CHECK(kfx_sim_state.synced_free_things_count == SYNCED_THINGS_COUNT - 1);
    CHECK(kfx_sim_state.unsynced_free_things_count == UNSYNCED_THINGS_COUNT - 1);
    // Rebuilt free-list holds index 1 at the far end for the synced range.
    CHECK(kfx_sim_state.synced_free_things[SYNCED_THINGS_COUNT - 2] == 1);
}

TEST_CASE_METHOD(ResetSimState, "clear_computer zeroes computer tasks, gold lookups, and per-player computer state", "[kfx_sim][game_lifecycle]") {
    kfx_sim_state.computer_task[0].flags = 1;
    kfx_sim_state.gold_lookup[0].flags = 1;
    kfx_sim_state.computer[0].tasks_did = 1;

    clear_computer();

    CHECK(kfx_sim_state.computer_task[0].flags == 0);
    CHECK(kfx_sim_state.gold_lookup[0].flags == 0);
    CHECK(kfx_sim_state.computer[0].tasks_did == 0);
}

TEST_CASE_METHOD(ResetSimState, "init_keepers_map_exploration is a no-op when no player is active, computer-controlled, or roaming", "[kfx_sim][game_lifecycle]") {
    // Every player is fresh-zeroed: allocflags==0 (player_exists false),
    // player_type==PT_Keeper==0 (player_is_roaming false) -- the guard on
    // every loop iteration is false, so nothing beyond the loop itself runs.
    struct PlayerInfo before;
    std::memcpy(&before, get_player(0), sizeof(before));

    init_keepers_map_exploration();

    CHECK(std::memcmp(&before, get_player(0), sizeof(before)) == 0);
}

TEST_CASE_METHOD(ResetSimState, "clear_players_for_save preserves id/active/allocation flags and the first-person camera, clears the rest", "[kfx_sim][game_lifecycle]") {
    struct PlayerInfo *player = get_player(0);
    player->id_number = 3;
    player->is_active = 1;
    player->allocflags = PlaF_Allocated | PlaF_CompCtrl;
    player->cameras[CamIV_FirstPerson].mappos.x.val = 555;
    player->victory_state = 9; // an arbitrary field that must be cleared

    clear_players_for_save();

    CHECK(player->id_number == 3);
    CHECK(player->is_active == 1);
    CHECK((player->allocflags & PlaF_Allocated) != 0);
    CHECK((player->allocflags & PlaF_CompCtrl) != 0);
    CHECK(player->cameras[CamIV_FirstPerson].mappos.x.val == 555);
    CHECK(player->active_camera_idx == CamIV_FirstPerson);
    CHECK(player->victory_state == 0);
}

TEST_CASE_METHOD(ResetSimState, "delete_all_thing_structures rebuilds the free-index lists when every thing is already non-existent", "[kfx_sim][game_lifecycle]") {
    kfx_sim_state.synced_free_things[0] = 999;
    kfx_sim_state.synced_free_things_count = 0;

    delete_all_thing_structures();

    CHECK(kfx_sim_state.synced_free_things_count == SYNCED_THINGS_COUNT - 1);
    CHECK(kfx_sim_state.unsynced_free_things_count == UNSYNCED_THINGS_COUNT - 1);
    // Unlike clear_things_and_persons_data, thing slots themselves aren't
    // memset -- only the free-index bookkeeping is rebuilt (confirmed by
    // reading the body: no per-thing memset outside the thing_exists branch).
}

TEST_CASE_METHOD(ResetSimState, "delete_all_structures runs its guard-only branches cleanly when nothing exists to delete", "[kfx_sim][game_lifecycle]") {
    // No thing/creature/room/action-point slot is allocated/existing, and
    // light_initialise() is safe against the zeroed fixture --
    // this is exercising that the orchestration itself doesn't crash and
    // still rebuilds the free-thing lists via delete_all_thing_structures.
    kfx_sim_state.synced_free_things_count = 0;

    delete_all_structures();

    CHECK(kfx_sim_state.synced_free_things_count == SYNCED_THINGS_COUNT - 1);
}

TEST_CASE_METHOD(ResetSimState, "clear_game_for_save resets entrance/random-seed bookkeeping alongside the full world clear", "[kfx_sim][game_lifecycle]") {
    kfx_sim_state.entrance_room_id = 7;
    kfx_sim_state.action_random_seed = 111;
    kfx_sim_state.ai_random_seed = 222;
    kfx_sim_state.player_random_seed = 333;
    kfx_sim_state.dungeon[0].total_money_owned = 500;

    clear_game_for_save();

    CHECK(kfx_sim_state.entrance_room_id == 0);
    CHECK(kfx_sim_state.action_random_seed == 0);
    CHECK(kfx_sim_state.ai_random_seed == 0);
    CHECK(kfx_sim_state.player_random_seed == 0);
    CHECK(kfx_sim_state.dungeon[0].total_money_owned == 0); // clear_dungeons()
    CHECK(kfx_sim_state.dungeon[0].owner == PLAYERS_COUNT); // clear_dungeons()'s reserved-owner sentinel
}

TEST_CASE_METHOD(ResetSimState, "reset_creature_max_levels raises every configured creature model above CREATURE_MAX_LEVEL, per dungeon", "[kfx_sim][game_lifecycle]") {
    kfx_config_state.conf.crtr_conf.model_count = 3;
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->creature_max_level[0] = 5; // model 0 (the "no creature" slot) must stay untouched

    reset_creature_max_levels();

    CHECK(dungeon->creature_max_level[0] == 5);
    CHECK(dungeon->creature_max_level[1] == CREATURE_MAX_LEVEL + 1);
    CHECK(dungeon->creature_max_level[2] == CREATURE_MAX_LEVEL + 1);
    CHECK(dungeon->creature_max_level[3] == 0); // model_count==3 means k stops before index 3
}
