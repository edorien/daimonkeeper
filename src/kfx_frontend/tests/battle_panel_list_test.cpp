// kfx_frontend: the battle panel's list of battles (frontmenu_ingame_evnt.c), moved with its tests from kfx_sim's
// creature_battle.c in refactor pass 5 (S05): the list is this machine's view (kfx_local_view), not a Dungeon's.
#include <catch2/catch_test_macros.hpp>

#include "frontmenu_ingame_evnt.h"
#include "kfx_local_view.h"
#include "kfx_sim_state.h"

#include <cstring>

namespace {
struct ResetState {
    ResetState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_local_view, 0, sizeof(kfx_local_view));
    }
};
}

// step_battles_forward rotates kfx_local_view.visible_battles[] (the battle panel's list) by consuming
// slot [2] (the "next queued" battle) and re-deriving [0]/[1] from it.
// With fewer than 4 total battles for the player, visible_battles[2] is
// 0 -- upstream #5231 ("Press F multiple times to cycle battles works
// with <4 battles too") added a fallback branch for exactly this case:
// when [2] is empty but [1] isn't, it still rotates using [1], shifting
// [0]<-[1], [1]<-[2] (0), then recycling the old [0] into the newly
// empty [1] slot -- {3, 7, 0} becomes {7, 3, 0}, so pressing the cycle
// key keeps cycling between the two known battles instead of doing
// nothing.
TEST_CASE_METHOD(ResetState, "step_battles_forward rotates using [1] as a fallback when [2] is empty but [1] isn't", "[kfx_frontend][battle_panel]") {
    kfx_local_view.visible_battles[0] = 3;
    kfx_local_view.visible_battles[1] = 7;
    kfx_local_view.visible_battles[2] = 0; // fewer than 4 total battles for this player

    CHECK(step_battles_forward(0) == true); // active_battle_exists: visible_battles[0] != 0
    CHECK(kfx_local_view.visible_battles[0] == 7);
    CHECK(kfx_local_view.visible_battles[1] == 3);
    CHECK(kfx_local_view.visible_battles[2] == 0);
}

TEST_CASE_METHOD(ResetState, "step_battles_forward returns false and leaves the list untouched when the player has no battles at all", "[kfx_frontend][battle_panel]") {
    // visible_battles all zero after ResetState's memset.

    CHECK(step_battles_forward(0) == false);
    CHECK(kfx_local_view.visible_battles[0] == 0);
}

