// kfx_sim "room"-adjacent depth increment (dungeon-level scoring), per
// docs/refactor/testing/comprehensive/stage-08-comprehensive-library-
// passes.md: dungeon_stats.c's compute_dungeon_*_score family --
// clamp-then-arithmetic point functions for the end-of-game score
// screen, self-contained aside from compute_dungeon_rooms_variety_score
// (pattern A on kfx_config_state.conf.slab_conf.room_types_count).
//
// compute_dungeon_rooms_attraction_score/_creature_tactics_score/
// _rooms_variety_score/_train_research_manufctr_wealth_score/
// _creature_amount_score/_creature_mood_score had no header declaration
// anywhere (only ever called from within dungeon_stats.c itself) -- added
// all six to dungeon_stats.h, the same "add the missing declaration to
// make testable" fix used for lvl_script_conditions.h/value_util.h
// earlier in this plan.
#include <catch2/catch_test_macros.hpp>

#include "dungeon_stats.h"
#include "kfx_config_state.h"

#include <cstring>

namespace {
struct ResetConfigState {
    ResetConfigState() { std::memset(&kfx_config_state, 0, sizeof(kfx_config_state)); }
};
}

TEST_CASE("compute_dungeon_rooms_attraction_score weights entrances, area and generosity", "[kfx_sim][dungeon_stats]") {
    CHECK(compute_dungeon_rooms_attraction_score(10, 20, 5) == 6500); // 100*5 + 100*20 + 400*10
}

TEST_CASE("compute_dungeon_rooms_attraction_score clamps each input independently", "[kfx_sim][dungeon_stats]") {
    CHECK(compute_dungeon_rooms_attraction_score(100, 200, 50) == 24600); // clamped to (27, 128, 10)
}

TEST_CASE("compute_dungeon_creature_tactics_score combines clamped battle and scavenge efficiency", "[kfx_sim][dungeon_stats]") {
    CHECK(compute_dungeon_creature_tactics_score(10, 3, 8, 2) == 351); // 25*6 + 25*7 + 2*13
}

TEST_CASE("compute_dungeon_creature_tactics_score floors negative efficiency at zero rather than going negative", "[kfx_sim][dungeon_stats]") {
    CHECK(compute_dungeon_creature_tactics_score(2, 10, 0, 0) == 24); // battle_efficiency clamped to 0, battle_total=12
}

TEST_CASE("compute_dungeon_creature_tactics_score clamps every component at its own upper bound", "[kfx_sim][dungeon_stats]") {
    CHECK(compute_dungeon_creature_tactics_score(100, 0, 100, 0) == 2160); // 25*40 + 25*40 + 2*80
}

TEST_CASE_METHOD(ResetConfigState, "compute_dungeon_rooms_variety_score weights area and room-type variety", "[kfx_sim][dungeon_stats]") {
    kfx_config_state.conf.slab_conf.room_types_count = 20;
    CHECK(compute_dungeon_rooms_variety_score(5, 100) == 425); // 3*100 + 25*5
}

TEST_CASE_METHOD(ResetConfigState, "compute_dungeon_rooms_variety_score clamps room_types to the configured count and area to 512", "[kfx_sim][dungeon_stats]") {
    kfx_config_state.conf.slab_conf.room_types_count = 20;
    CHECK(compute_dungeon_rooms_variety_score(50, 600) == 2036); // clamped to (20, 512)
}

TEST_CASE_METHOD(ResetConfigState, "compute_dungeon_rooms_variety_score floors negative inputs at zero", "[kfx_sim][dungeon_stats]") {
    kfx_config_state.conf.slab_conf.room_types_count = 20;
    CHECK(compute_dungeon_rooms_variety_score(-5, -10) == 0);
}

TEST_CASE("compute_dungeon_train_research_manufctr_wealth_score scales the first three by /256 and weights wealth 3x", "[kfx_sim][dungeon_stats]") {
    CHECK(compute_dungeon_train_research_manufctr_wealth_score(256 * 10, 256 * 20, 256 * 30, 100) == 330); // 5+10+15+300
}

TEST_CASE("compute_dungeon_train_research_manufctr_wealth_score clamps each component at its own upper bound", "[kfx_sim][dungeon_stats]") {
    CHECK(compute_dungeon_train_research_manufctr_wealth_score(256 * 100000, 0, 0, 50000) == 138000); // 96000/2 + 0 + 0 + 3*30000
}

TEST_CASE("compute_dungeon_train_research_manufctr_wealth_score floors negative components at zero", "[kfx_sim][dungeon_stats]") {
    CHECK(compute_dungeon_train_research_manufctr_wealth_score(-2560, 0, 0, -100) == 0);
}

TEST_CASE("compute_dungeon_creature_amount_score is linear up to a clamp at 160", "[kfx_sim][dungeon_stats]") {
    CHECK(compute_dungeon_creature_amount_score(50) == 25600);  // 512*50
    CHECK(compute_dungeon_creature_amount_score(200) == 81920); // clamped to 160
    CHECK(compute_dungeon_creature_amount_score(-5) == 0);      // floored to 0
}

TEST_CASE("compute_dungeon_creature_mood_score is zero when nothing survived, avoiding a division by zero", "[kfx_sim][dungeon_stats]") {
    CHECK(compute_dungeon_creature_mood_score(0, 10) == 0);
}

TEST_CASE("compute_dungeon_creature_mood_score is the annoyed proportion of survivors, scaled by 256", "[kfx_sim][dungeon_stats]") {
    CHECK(compute_dungeon_creature_mood_score(100, 50) == 128); // (50<<8)/100
}

TEST_CASE("compute_dungeon_creature_mood_score never lets annoyed exceed survived, raising survived to match instead", "[kfx_sim][dungeon_stats]") {
    CHECK(compute_dungeon_creature_mood_score(10, 50) == 256); // survived raised to 50 -> (50<<8)/50
}

TEST_CASE("compute_dungeon_creature_mood_score clamps survived to 160 before comparing against annoyed", "[kfx_sim][dungeon_stats]") {
    CHECK(compute_dungeon_creature_mood_score(200, 50) == 80); // survived clamped to 160 first: (50<<8)/160
}
