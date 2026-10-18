// level_start_options.h: the Skirmish page's one-shot "reveal map" request, carried out once when the level starts.
#include <catch2/catch_test_macros.hpp>

#include "level_start_options.h"
#include "map_data.h"
#include "player_data.h"
#include "kfx_sim_test_fixtures.h"

using namespace kfx_test;

namespace {
void make_player(PlayerNumber idx, unsigned char type)
{
    kfx_sim_state.players[idx].id_number = idx;
    kfx_sim_state.players[idx].allocflags |= PlaF_Allocated;
    kfx_sim_state.players[idx].player_type = type;
}
}

TEST_CASE_METHOD(ResetSimAndConfig, "level_start_options_apply reveals the whole map to every keeper that exists, once", "[kfx_sim][level_start_options]") {
    make_player(0, PT_Keeper);
    make_player(1, PT_Keeper);
    make_player(2, PT_Roaming); // heroes are not keepers
    level_start_options_clear();
    level_start_reveal_map_request(77);
    CHECK(level_start_reveal_map_requested(77));
    CHECK_FALSE(level_start_reveal_map_requested(78));

    CHECK(level_start_options_apply(77) == 2);
    for (PlayerNumber p = 0; p < 2; p++)
    {
        CHECK(subtile_revealed_directly(1, 1, p));
        CHECK(subtile_revealed_directly(kfx_sim_state.map_subtiles_x - 1, kfx_sim_state.map_subtiles_y - 1, p));
    }
    CHECK_FALSE(subtile_revealed_directly(1, 1, 2));
    // consumed: a later level does not inherit it
    CHECK_FALSE(level_start_reveal_map_requested(77));
    CHECK(level_start_options_apply(77) == 0);
}

TEST_CASE_METHOD(ResetSimAndConfig, "a reveal asked for another level is discarded, not carried over", "[kfx_sim][level_start_options]") {
    make_player(0, PT_Keeper);
    level_start_reveal_map_request(77);
    CHECK(level_start_options_apply(78) == 0);
    CHECK_FALSE(subtile_revealed_directly(1, 1, 0));
    CHECK_FALSE(level_start_reveal_map_requested(77));
}

TEST_CASE_METHOD(ResetSimAndConfig, "level_start_options_clear forgets a request", "[kfx_sim][level_start_options]") {
    level_start_reveal_map_request(5);
    level_start_options_clear();
    CHECK_FALSE(level_start_reveal_map_requested(5));
}
