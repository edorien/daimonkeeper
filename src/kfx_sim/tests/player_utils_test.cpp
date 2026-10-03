// kfx_sim "player" cluster depth increment, per docs/refactor/testing/
// comprehensive/stage-08b-kfx-sim-clusters.md's "still open" list:
// player_utils.c's scoring/status functions. player_has_lost and
// player_cannot_win are both pure reads over player/dungeon/thing state
// already set up (no orchestration, no mutation) -- pattern A on
// kfx_sim_state, same fixture player_data_test.cpp/thing_data_test.cpp
// use. compute_player_final_score/take_money_from_dungeon_f and friends
// are NOT attempted here: they additionally reach through the ports and/or dungeon gold bookkeeping across many slabs,
// a bigger increment than this pass's two functions.
#include <catch2/catch_test_macros.hpp>

#include "player_utils.h"
#include "player_data.h"
#include "dungeon_data.h"
#include "thing_data.h"
#include "thing_objects.h" // ObSt_BeingDestroyed
#include "kfx_sim_state.h"
#include "kfx_config_state.h"

#include <cstring>

namespace {
struct ResetSimState {
    ResetSimState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        kfx_config_state.neutral_player_num = 5; // out of the plyr_idx=0 tests' way
    }
};

// player_data_test.cpp's convention: index 0 is a real, valid player
// slot here, no reservation -- get_player(0)/id_number must be set to 0
// to match get_players_num_dungeon_f's own player->id_number lookup.
struct PlayerInfo *make_existing_player(PlayerNumber plyr_idx)
{
    struct PlayerInfo *player = get_player(plyr_idx);
    player->id_number = plyr_idx;
    player->allocflags |= PlaF_Allocated;
    return player;
}
}

TEST_CASE_METHOD(ResetSimState, "player_has_lost is false for an invalid player", "[kfx_sim][player_utils]") {
    CHECK_FALSE(player_has_lost(-1)); // INVALID_PLAYER sentinel path
}

TEST_CASE_METHOD(ResetSimState, "player_has_lost reads victory_state", "[kfx_sim][player_utils]") {
    struct PlayerInfo *player = make_existing_player(0);

    player->victory_state = VicS_Undecided;
    CHECK_FALSE(player_has_lost(0));

    player->victory_state = VicS_LostLevel;
    CHECK(player_has_lost(0));
}

TEST_CASE_METHOD(ResetSimState, "player_cannot_win is true for the neutral player", "[kfx_sim][player_utils]") {
    CHECK(player_cannot_win(kfx_config_state.neutral_player_num));
}

TEST_CASE_METHOD(ResetSimState, "player_cannot_win is true for a player that doesn't exist", "[kfx_sim][player_utils]") {
    CHECK(player_cannot_win(0)); // PlaF_Allocated never set
}

TEST_CASE_METHOD(ResetSimState, "player_cannot_win is true once the player has lost", "[kfx_sim][player_utils]") {
    struct PlayerInfo *player = make_existing_player(0);
    player->victory_state = VicS_LostLevel;
    CHECK(player_cannot_win(0));
}

TEST_CASE_METHOD(ResetSimState, "player_cannot_win is true when the player has no dungeon heart", "[kfx_sim][player_utils]") {
    struct PlayerInfo *player = make_existing_player(0);
    player->victory_state = VicS_Undecided;
    kfx_sim_state.dungeon[0].dnheart_idx = 0; // no heart -> get_player_soul_container returns INVALID_THING
    CHECK(player_cannot_win(0));
}

TEST_CASE_METHOD(ResetSimState, "player_cannot_win is true when the dungeon heart is being destroyed", "[kfx_sim][player_utils]") {
    struct PlayerInfo *player = make_existing_player(0);
    player->victory_state = VicS_Undecided;
    struct Thing *heart = thing_get(1);
    heart->alloc_flags |= TAlF_Exists;
    heart->active_state = ObSt_BeingDestroyed;
    kfx_sim_state.dungeon[0].dnheart_idx = 1;
    CHECK(player_cannot_win(0));
}

TEST_CASE_METHOD(ResetSimState, "player_cannot_win is false for a player with an intact dungeon heart", "[kfx_sim][player_utils]") {
    struct PlayerInfo *player = make_existing_player(0);
    player->victory_state = VicS_Undecided;
    struct Thing *heart = thing_get(1);
    heart->alloc_flags |= TAlF_Exists;
    heart->active_state = ObSt_FoodMoves; // anything other than ObSt_BeingDestroyed
    kfx_sim_state.dungeon[0].dnheart_idx = 1;
    CHECK_FALSE(player_cannot_win(0));
}
