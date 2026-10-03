// kfx_game: main_game.c's reset_script_timers_and_flags() -- level start clears every
// player's script flags, and in Free Play (a map pack) also the campaign flags.
// Coverage-first for the fix carried in upstream #5360: the player loop runs to
// PLAYERS_COUNT but intralvl.campaign_flags only has PLAYERS_FOR_CAMPAIGN_FLAGS
// rows, so clearing it wrote past the array into the rest of IntralevelData.
#include <catch2/catch_test_macros.hpp>

#include "main_game.h"
#include "game_merge.h"
#include "config_campaigns.h"
#include "kfx_sim_state.h"
#include "kfx_config_state.h"

#include <cstring>

namespace {
struct ResetFlagsFixture {
    int64_t prev_power_types_count;
    ResetFlagsFixture() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&campaign, 0, sizeof(campaign));
        std::memset(&intralvl, 0, sizeof(intralvl));
        // no powers configured: add_power_to_player() refuses them, nothing else to set up
        prev_power_types_count = kfx_config_state.conf.magic_conf.power_types_count;
        kfx_config_state.conf.magic_conf.power_types_count = 0;
        for (int64_t p = 0; p < PLAYERS_FOR_CAMPAIGN_FLAGS; p++)
            for (int64_t k = 0; k < CAMPAIGN_FLAGS_PER_PLAYER; k++)
                intralvl.campaign_flags[p][k] = 5;
        intralvl.next_level = 42;
        intralvl.ensign_overrides[0].lvnum = 7;
        intralvl.ensign_overrides[0].active = true;
    }
    ~ResetFlagsFixture() {
        kfx_config_state.conf.magic_conf.power_types_count = prev_power_types_count;
    }
    void make_map_pack() {
        std::strcpy(campaign.fname, "pack.cfg");
        campaign.freeplay_levels_count = 1;
    }
};
}

TEST_CASE_METHOD(ResetFlagsFixture, "in Free Play the campaign flags are cleared and nothing after them is touched", "[kfx_game][main_game]") {
    make_map_pack();
    REQUIRE(is_map_pack());

    reset_script_timers_and_flags();

    for (int64_t p = 0; p < PLAYERS_FOR_CAMPAIGN_FLAGS; p++)
        for (int64_t k = 0; k < CAMPAIGN_FLAGS_PER_PLAYER; k++)
            CHECK(intralvl.campaign_flags[p][k] == 0);
    CHECK(intralvl.next_level == 42);
    CHECK(intralvl.ensign_overrides[0].lvnum == 7);
    CHECK(intralvl.ensign_overrides[0].active);
}

TEST_CASE_METHOD(ResetFlagsFixture, "in a campaign the campaign flags carry over", "[kfx_game][main_game]") {
    std::strcpy(campaign.fname, "keeporig.cfg");
    campaign.single_levels_count = 1;
    REQUIRE_FALSE(is_map_pack());

    reset_script_timers_and_flags();

    CHECK(intralvl.campaign_flags[0][0] == 5);
    CHECK(intralvl.campaign_flags[PLAYERS_FOR_CAMPAIGN_FLAGS - 1][CAMPAIGN_FLAGS_PER_PLAYER - 1] == 5);
    CHECK(intralvl.next_level == 42);
}
