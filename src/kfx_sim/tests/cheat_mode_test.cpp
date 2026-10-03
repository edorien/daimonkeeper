// kfx_sim: cheat_mode.c (refactor pass 5, P5-F17): cheats are off in a multiplayer game, whatever each machine was
// started with, and on in a one-player game started with cheat mode.
#include <catch2/catch_test_macros.hpp>

#include "cheat_mode.h"
#include "config_players.h"
#include "kfx_sim_state.h"

#include <cstring>

namespace {
struct ResetSimState {
    ResetSimState() { std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state)); }
};
}

TEST_CASE_METHOD(ResetSimState, "cheat mode is on only in a one-player game started with it", "[kfx_sim][cheat_mode]") {
    kfx_sim_state.game_kind = GKind_LocalGame;
    kfx_sim_state.easter_eggs_enabled = false;
    CHECK_FALSE(game_refuses_cheats());
    CHECK_FALSE(cheat_mode_enabled());
    kfx_sim_state.easter_eggs_enabled = true;
    CHECK(cheat_mode_enabled());
    kfx_sim_state.game_kind = GKind_MultiGame;
    CHECK(game_refuses_cheats());
    CHECK_FALSE(cheat_mode_enabled());
}

TEST_CASE("the cheat and editor cursor modes are cheats, the dungeon's own aren't", "[kfx_sim][cheat_mode]") {
    for (int64_t s : {PSt_MkDigger, PSt_KillCreatr, PSt_FreeCtrlDirect, PSt_DestroyThing, PSt_CreatrInfoAll,
                      PSt_EditorFill, PSt_EditorPlacePoint}) {
        INFO(s);
        CHECK(player_state_is_cheat(s));
    }
    for (int64_t s : {PSt_None, PSt_CtrlDungeon, PSt_BuildRoom, PSt_HoldInHand, PSt_Slap, PSt_CtrlDirect,
                      PSt_PlaceTrap, PSt_Sell, PSt_CreateDigger, PST_CastGenericLevelPower}) {
        INFO(s);
        CHECK_FALSE(player_state_is_cheat(s));
    }
}
