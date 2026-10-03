// kfx_sim coverage, per docs/refactor/testing/comprehensive/
// stage-08b-kfx-sim-clusters.md's "still open" list (player cluster):
// player_complookup.c's pure array-lookup functions -- get_gold_lookup/
// gold_lookup_index (pointer<->index conversion into kfx_sim_state's
// gold_lookup[] table) and smaller_gold_vein_lookup_idx (the linear scan
// used to evict the "worst" recorded gold vein). check_treasure_map/
// check_map_for_gold need a real map+slab-config fixture and are left for
// a later increment.
#include <catch2/catch_test_macros.hpp>

#include "player_complookup.h"
#include "kfx_sim_state.h"

#include <cstring>

namespace {
struct ResetSimState {
    ResetSimState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
    }
};
}

TEST_CASE_METHOD(ResetSimState, "get_gold_lookup indexes straight into kfx_sim_state.gold_lookup[]", "[kfx_sim][player_complookup]") {
    CHECK(get_gold_lookup(0) == &kfx_sim_state.gold_lookup[0]);
    CHECK(get_gold_lookup(5) == &kfx_sim_state.gold_lookup[5]);
}

TEST_CASE_METHOD(ResetSimState, "gold_lookup_index inverts get_gold_lookup for any in-range entry", "[kfx_sim][player_complookup]") {
    CHECK(gold_lookup_index(get_gold_lookup(0)) == 0);
    CHECK(gold_lookup_index(get_gold_lookup(12)) == 12);
    CHECK(gold_lookup_index(get_gold_lookup(GOLD_LOOKUP_COUNT - 1)) == GOLD_LOOKUP_COUNT - 1);
}

TEST_CASE_METHOD(ResetSimState, "gold_lookup_index falls back to 0 for a pointer outside the table", "[kfx_sim][player_complookup]") {
    // Not a real -1 sentinel -- index 0 doubles as both "the first entry"
    // and "out of range", confirmed by reading the body rather than assumed.
    struct GoldLookup unrelated{};
    CHECK(gold_lookup_index(&unrelated) == 0);
}

namespace {
// A zeroed (never-recorded) slot has num_gem_slabs==num_gold_slabs==0,
// which trivially "beats" any positive reference -- realistic in
// production only once every slot already holds a real vein (the only
// case check_treasure_map() actually calls this in), never in a
// freshly-reset table. Filling every slot with an explicit, inert
// baseline first avoids that empty-slot artifact contaminating these
// targeted assertions -- caught by an empirical run returning 0 (the
// zeroed slot at index 0) instead of the expected result, before adding
// this helper.
void fill_all_lookups(uint64_t gem_slabs, int64_t gold_slabs)
{
    for (int64_t i = 0; i < GOLD_LOOKUP_COUNT; i++)
    {
        get_gold_lookup(i)->num_gem_slabs = gem_slabs;
        get_gold_lookup(i)->num_gold_slabs = gold_slabs;
    }
}
}

TEST_CASE_METHOD(ResetSimState, "smaller_gold_vein_lookup_idx returns -1 when no recorded vein is smaller than the reference", "[kfx_sim][player_complookup]") {
    fill_all_lookups(5, 5); // every slot exactly matches the reference -- neither "<" branch ever fires

    CHECK(smaller_gold_vein_lookup_idx(5, 5) == -1);
}

TEST_CASE_METHOD(ResetSimState, "smaller_gold_vein_lookup_idx prioritizes a strictly smaller gem count over a smaller gold count", "[kfx_sim][player_complookup]") {
    fill_all_lookups(10, 10); // inert baseline: gem count (10) is neither == nor < the reference's 4
    // Index 1: same gem count as the reference, smaller gold count -- would
    // qualify on its own, but index 2's smaller *gem* count outranks it.
    get_gold_lookup(1)->num_gem_slabs = 4;
    get_gold_lookup(1)->num_gold_slabs = 1;
    get_gold_lookup(2)->num_gem_slabs = 3;
    get_gold_lookup(2)->num_gold_slabs = 100; // gold count irrelevant once gem count is smaller

    CHECK(smaller_gold_vein_lookup_idx(10, 4) == 2);
}

TEST_CASE_METHOD(ResetSimState, "smaller_gold_vein_lookup_idx keeps refining to the smallest gold count once gem counts tie", "[kfx_sim][player_complookup]") {
    fill_all_lookups(10, 10); // inert baseline: gem count (10) is neither == nor < the reference's 2
    get_gold_lookup(0)->num_gem_slabs = 2;
    get_gold_lookup(0)->num_gold_slabs = 8;
    get_gold_lookup(1)->num_gem_slabs = 2;
    get_gold_lookup(1)->num_gold_slabs = 3; // smaller than index 0 -- should win

    CHECK(smaller_gold_vein_lookup_idx(10, 2) == 1);
}
