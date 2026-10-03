// kfx_ai: the kfx_sim test fixtures plus the computer-player setup the AI
// tests share (make_computer_player() moved here from kfx_sim_test_fixtures.h
// in refactor pass 2, S14, since it calls the AI's computer_set_dungeon()).
#pragma once

#include "kfx_sim/tests/kfx_sim_test_fixtures.h"
#include "player_computer.h"

namespace kfx_test {

// Makes player plyr_idx exist (via make_player_active) and wires up its
// Computer2 slot's ->dungeon pointer to the matching real Dungeon slot
// (also stamping Dungeon::owner, since that's likewise left at its
// zeroed/aliasing-with-0 default otherwise). Computer2 itself lives in
// kfx_sim_state.computer[] and is already zeroed by ResetSimAndConfig.
inline struct Computer2 *make_computer_player(PlayerNumber plyr_idx)
{
    make_player_active(plyr_idx);
    struct Computer2 *comp = get_computer_player(plyr_idx);
    computer_set_dungeon(comp, get_dungeon(plyr_idx));
    computer_dungeon(comp)->owner = plyr_idx;
    return comp;
}

} // namespace kfx_test
