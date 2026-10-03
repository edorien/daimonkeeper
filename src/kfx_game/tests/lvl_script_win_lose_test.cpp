// kfx_game: lvl_script.c's process_win_and_lose_conditions() -- which players a
// met WIN_GAME / LOSE_GAME script condition decides the level for.
// Upstream #5365 ("Remove all non-scripted victory conditions") moved it from
// "the local player only" to "every undecided human" (and a dropped human's
// placeholder), so a co-op multiplayer script win reaches every human.
#include <catch2/catch_test_macros.hpp>

#include "lvl_script.h"
#include "player_data.h"
#include "kfx_game_state.h"
#include "kfx_sim_state.h"
#include "kfx_config_state.h"

#include <cstring>

extern "C" void process_win_and_lose_conditions(void);

namespace {
struct WinLoseFixture {
    WinLoseFixture() {
        std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        kfx_config_state.neutral_player_num = PLAYER_NEUTRAL;
        for (PlayerNumber i = 0; i < PLAYERS_COUNT; i++)
            kfx_sim_state.players[i].id_number = i;
        make_player(0, false);
        make_player(1, false);
        make_player(2, true);
        // no local player among them: keeps set_player_as_won_level() off its UI path
        my_player_number = 4;
    }
    void make_player(PlayerNumber i, TbBool computer) {
        struct PlayerInfo *player = &kfx_sim_state.players[i];
        player->allocflags |= PlaF_Allocated;
        if (computer)
            player->allocflags |= PlaF_CompCtrl;
        player->victory_state = VicS_Undecided;
    }
    unsigned char state(PlayerNumber i) { return kfx_sim_state.players[i].victory_state; }
};
}

TEST_CASE_METHOD(WinLoseFixture, "a met win condition decides the level for every undecided human, not computers", "[kfx_game][lvl_script]") {
    kfx_game_state.script.win_conditions_num = 1;
    kfx_game_state.script.win_conditions[0] = CONDITION_ALWAYS;

    process_win_and_lose_conditions();

    CHECK(state(0) == VicS_WonLevel);
    CHECK(state(1) == VicS_WonLevel);
    CHECK(state(2) == VicS_Undecided);
}

TEST_CASE_METHOD(WinLoseFixture, "a met lose condition decides the level for every undecided human, not computers", "[kfx_game][lvl_script]") {
    kfx_game_state.script.lose_conditions_num = 1;
    kfx_game_state.script.lose_conditions[0] = CONDITION_ALWAYS;

    process_win_and_lose_conditions();

    CHECK(state(0) == VicS_LostLevel);
    CHECK(state(1) == VicS_LostLevel);
    CHECK(state(2) == VicS_Undecided);
}

TEST_CASE_METHOD(WinLoseFixture, "a scripted outcome also reaches a dropped human's placeholder, and leaves decided players alone", "[kfx_game][lvl_script]") {
    kfx_game_state.script.win_conditions_num = 1;
    kfx_game_state.script.win_conditions[0] = CONDITION_ALWAYS;
    kfx_sim_state.players[1].allocflags |= PlaF_CompCtrl | PlaF_Placeholder;
    kfx_sim_state.players[0].victory_state = VicS_LostLevel;

    process_win_and_lose_conditions();

    CHECK(state(0) == VicS_LostLevel);
    CHECK(state(1) == VicS_WonLevel);
    CHECK(state(2) == VicS_Undecided);
}

TEST_CASE_METHOD(WinLoseFixture, "an unmet win condition decides nothing", "[kfx_game][lvl_script]") {
    kfx_game_state.script.win_conditions_num = 1;
    kfx_game_state.script.win_conditions[0] = 0; // condition 0, status bit clear

    process_win_and_lose_conditions();

    CHECK(state(0) == VicS_Undecided);
    CHECK(state(1) == VicS_Undecided);
}
