// level_start_options.h: the Skirmish page's one-shot start options, taken when the level starts and carried out once.
#include <catch2/catch_test_macros.hpp>

#include "level_start_options.h"
#include "map_data.h"
#include "player_data.h"
#include "kfx_config_state.h"
#include "kfx_sim_test_fixtures.h"

using namespace kfx_test;

namespace {
void make_player(PlayerNumber idx, unsigned char type)
{
    kfx_sim_state.players[idx].id_number = idx;
    kfx_sim_state.players[idx].allocflags |= PlaF_Allocated;
    kfx_sim_state.players[idx].player_type = type;
}

struct LevelStartOptions options(TbBool reveal, int32_t sharing, TbBool llm_pause)
{
    struct LevelStartOptions o;
    o.reveal_map = reveal;
    o.sharing = sharing;
    o.llm_pause = llm_pause;
    return o;
}
}

TEST_CASE_METHOD(ResetSimAndConfig, "reveal_map reveals the whole map to every keeper that exists, and only to them", "[kfx_sim][level_start_options]") {
    make_player(0, PT_Keeper);
    make_player(1, PT_Keeper);
    make_player(2, PT_Roaming); // heroes are not keepers
    const struct LevelStartOptions o = options(true, LevelStartSharing_None, false);
    CHECK(level_start_options_apply_world(&o) == 2);
    for (PlayerNumber p = 0; p < 2; p++)
    {
        CHECK(subtile_revealed_directly(1, 1, p));
        CHECK(subtile_revealed_directly(kfx_sim_state.map_subtiles_x - 1, kfx_sim_state.map_subtiles_y - 1, p));
    }
    CHECK_FALSE(subtile_revealed_directly(1, 1, 2));
}

TEST_CASE_METHOD(ResetSimAndConfig, "sharing writes the vision and drop game rules of every player; None leaves them alone", "[kfx_sim][level_start_options]") {
    const size_t n = sizeof(kfx_config_state.conf.rules) / sizeof(kfx_config_state.conf.rules[0]);
    kfx_config_state.conf.rules[3].gameplay.allies_share_drop = true; // the level's own choice
    struct LevelStartOptions o = options(false, LevelStartSharing_None, false);
    level_start_options_apply_world(&o);
    CHECK_FALSE(kfx_config_state.conf.rules[0].gameplay.allies_share_vision);
    CHECK(kfx_config_state.conf.rules[3].gameplay.allies_share_drop);

    o = options(false, LevelStartSharing_Vision, false);
    level_start_options_apply_world(&o);
    for (size_t i = 0; i < n; i++)
    {
        CHECK(kfx_config_state.conf.rules[i].gameplay.allies_share_vision);
        CHECK_FALSE(kfx_config_state.conf.rules[i].gameplay.allies_share_drop);
    }

    o = options(false, LevelStartSharing_VisionAndDrop, false);
    level_start_options_apply_world(&o);
    for (size_t i = 0; i < n; i++)
    {
        CHECK(kfx_config_state.conf.rules[i].gameplay.allies_share_vision);
        CHECK(kfx_config_state.conf.rules[i].gameplay.allies_share_drop);
    }
}

TEST_CASE_METHOD(ResetSimAndConfig, "a request is taken once, and one asked for another level is discarded", "[kfx_sim][level_start_options]") {
    level_start_options_clear();
    const struct LevelStartOptions asked = options(true, LevelStartSharing_Vision, true);
    level_start_options_request(77, &asked);
    CHECK(level_start_options_requested(77).llm_pause);
    CHECK_FALSE(level_start_options_requested(78).llm_pause);
    CHECK(level_start_options_requested(77).sharing == LevelStartSharing_Vision);

    const struct LevelStartOptions taken = level_start_options_take(77);
    CHECK(taken.reveal_map);
    CHECK(taken.llm_pause);
    const struct LevelStartOptions again = level_start_options_take(77);
    CHECK_FALSE(again.reveal_map);
    CHECK_FALSE(again.llm_pause);
    CHECK(again.sharing == LevelStartSharing_None);

    level_start_options_request(77, &asked);
    const struct LevelStartOptions other = level_start_options_take(78); // a different level starts
    CHECK_FALSE(other.reveal_map);
    CHECK_FALSE(level_start_options_requested(77).reveal_map); // and the stale request is gone
}
