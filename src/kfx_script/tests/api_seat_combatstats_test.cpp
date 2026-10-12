// kfx_script: api_seat_combatstats.c -- the pure parts of the combat statistics (docs/newFeatures/llm/03 P0): a creature's
// low-health episode, the median the summaries use, and the upkeep schedule of a lasting power. The parts that watch a
// running game are tested by ftest combat_stats.
#include <catch2/catch_test_macros.hpp>

#include <vector>
#include <json.h>
#include <json-dom.h>

#include "api_seat_combatstats.h"

namespace {

CombatLowTrack fresh() { CombatLowTrack t{}; return t; }

int64_t median_of(std::vector<int64_t> v) { return combatstats_median(v.data(), (int64_t)v.size()); }

} // namespace

TEST_CASE("a healthy creature has no low episode", "[kfx_script][combatstats]") {
    CombatLowTrack t = fresh();
    combatstats_update_low(&t, 80, 100);
    combatstats_update_low(&t, COMBATSTATS_LOW_PCT, 101); // exactly at the threshold is not under it
    CHECK(t.low_since == 0);
    CHECK(t.critical_since == 0);
}

TEST_CASE("the episode starts the first turn under each threshold and keeps that turn", "[kfx_script][combatstats]") {
    CombatLowTrack t = fresh();
    combatstats_update_low(&t, 34, 100);
    CHECK(t.low_since == 100);
    CHECK(t.critical_since == 0);
    combatstats_update_low(&t, 19, 105);
    combatstats_update_low(&t, 10, 110);
    CHECK(t.low_since == 100);      // not moved by later observations
    CHECK(t.critical_since == 105);
}

TEST_CASE("a creature that drops straight past both thresholds starts both on the same turn", "[kfx_script][combatstats]") {
    CombatLowTrack t = fresh();
    combatstats_update_low(&t, 5, 200);
    CHECK(t.low_since == 200);
    CHECK(t.critical_since == 200);
}

TEST_CASE("climbing between the low and recovered thresholds keeps the episode", "[kfx_script][combatstats]") {
    CombatLowTrack t = fresh();
    combatstats_update_low(&t, 30, 100);
    t.healed_turn = 102;
    combatstats_update_low(&t, COMBATSTATS_RECOVERED_PCT - 1, 103);
    CHECK(t.low_since == 100);
    CHECK(t.healed_turn == 102);
}

TEST_CASE("recovering ends the episode, and the next fall starts a new one", "[kfx_script][combatstats]") {
    CombatLowTrack t = fresh();
    combatstats_update_low(&t, 15, 100);
    t.healed_turn = 104;
    combatstats_update_low(&t, COMBATSTATS_RECOVERED_PCT, 106);
    CHECK(t.low_since == 0);
    CHECK(t.critical_since == 0);
    CHECK(t.healed_turn == 0);
    combatstats_update_low(&t, 25, 300);
    CHECK(t.low_since == 300);
    CHECK(t.critical_since == 0);
}

TEST_CASE("median: empty, odd, even (the lower middle), unsorted input", "[kfx_script][combatstats]") {
    CHECK(median_of({}) == 0);
    CHECK(median_of({7}) == 7);
    CHECK(median_of({9, 1, 5}) == 5);
    CHECK(median_of({40, 10, 30, 20}) == 20);
    CHECK(median_of({-1, 3, 3, 100, 2}) == 3);
}

TEST_CASE("upkeep: every `duration` turns from the cast, every turn below a duration of 1, never on the cast turn", "[kfx_script][combatstats]") {
    // Must Obey: Duration = 18 (magic.cfg)
    CHECK_FALSE(combatstats_upkeep_due(0, 18));
    CHECK_FALSE(combatstats_upkeep_due(1, 18));
    CHECK_FALSE(combatstats_upkeep_due(17, 18));
    CHECK(combatstats_upkeep_due(18, 18));
    CHECK(combatstats_upkeep_due(36, 18));
    // Call to Arms: Duration = 1
    CHECK_FALSE(combatstats_upkeep_due(0, 1));
    for (int64_t t = 1; t < 5; t++) CHECK(combatstats_upkeep_due(t, 1));
    CHECK(combatstats_upkeep_due(3, 0));
    CHECK_FALSE(combatstats_upkeep_due(0, 0));
}
