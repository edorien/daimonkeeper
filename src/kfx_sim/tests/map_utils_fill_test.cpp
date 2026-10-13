// Security pass S08: slabs_fill_iterate_from_slab() is what a script's "change every slab like this one" runs. Its
// stack was allocated a byte per slab for 8-byte coordinates and a slab could be on it up to four times, so a fill
// over a map of open slabs wrote far past it (found by the Lua API fuzzer under AddressSanitizer). These cases fill
// whole maps, so that the sanitizer build (scripts/build-fuzz.sh asan) catches it again.
#include <catch2/catch_test_macros.hpp>

#include "map_utils.h"
#include "kfx_sim_state.h"

#include <cstring>
#include <set>
#include <utility>

namespace {
std::set<std::pair<int64_t, int64_t>> g_seen;
int64_t g_calls;

TbBool take_all(MapSlabCoord x, MapSlabCoord y, MaxCoordFilterParam)
{
    g_calls++;
    g_seen.insert({x, y});
    return true;
}
TbBool take_only_source(MapSlabCoord x, MapSlabCoord y, MaxCoordFilterParam)
{
    g_calls++;
    g_seen.insert({x, y});
    return false;
}
struct MapFixture {
    MapFixture()
    {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        g_seen.clear();
        g_calls = 0;
    }
    void size(int64_t slabs) { kfx_sim_state.map_subtiles_x = kfx_sim_state.map_subtiles_y = slabs * 3; }
};
}

TEST_CASE_METHOD(MapFixture, "a fill over a whole map of slabs visits every slab (some more than once, as it always did)", "[kfx_sim][map_utils][security]") {
    size(85); // the largest map
    slabs_fill_iterate_from_slab(40, 40, take_all, nullptr);
    CHECK(g_seen.size() == 85u * 85u);
    CHECK(g_calls >= 85 * 85);
    CHECK(g_calls <= 4 * 85 * 85);
    g_seen.clear();
    g_calls = 0;
    slabs_fill_iterate_from_slab(0, 0, take_all, nullptr); // from a corner
    CHECK(g_seen.size() == 85u * 85u);
}

TEST_CASE_METHOD(MapFixture, "a fill that refuses the first slab stops there", "[kfx_sim][map_utils][security]") {
    size(10);
    slabs_fill_iterate_from_slab(3, 4, take_only_source, nullptr);
    CHECK(g_calls == 1);
    CHECK(g_seen.count({3, 4}) == 1);
}

TEST_CASE_METHOD(MapFixture, "a fill from a slab that is not on the map does nothing", "[kfx_sim][map_utils][security]") {
    size(10);
    for (auto at : {std::pair<int64_t, int64_t>{-1, 0}, {0, -1}, {10, 0}, {0, 10}, {1 << 20, 3}})
    {
        slabs_fill_iterate_from_slab(at.first, at.second, take_all, nullptr);
        CHECK(g_calls == 0);
    }
}
