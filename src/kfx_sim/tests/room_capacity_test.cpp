// kfx_sim "room" cluster depth increment, per docs/refactor/testing/
// comprehensive/stage-08b-kfx-sim-clusters.md's "still open" list:
// room_data.c's count_slabs_*_wth_effcncy family, the
// terrain_room_total_capacity_func_list dispatch table's entries
// (config_terrain.c-driven, but the functions themselves are kfx_sim's
// per the layering-blind-spot fix). Each is a pure function of
// room->slabs_count/efficiency -> room->total_capacity, no other state
// -- a self-contained sibling group in the compute_room_max_health vein,
// just varying the rounding/clamp rule instead of the config lookup.
//
// None had a header declaration anywhere outside room_data.c's own
// same-file forward declarations -- added all seven to room_data.h, the
// same "add the missing declaration" fix used repeatedly across this
// plan.
#include <catch2/catch_test_macros.hpp>

#include "room_data.h"

namespace {
struct Room make_room(int64_t slabs_count, int64_t efficiency)
{
    struct Room room{};
    room.slabs_count = slabs_count;
    room.efficiency = efficiency;
    return room;
}
}

TEST_CASE("count_slabs_all_only copies slabs_count straight through, ignoring efficiency", "[kfx_sim][room_data]") {
    struct Room room = make_room(10, 0);
    count_slabs_all_only(&room);
    CHECK(room.total_capacity == 10);
}

TEST_CASE("count_slabs_all_wth_effcncy scales by efficiency, flooring at 1", "[kfx_sim][room_data]") {
    struct Room full = make_room(10, ROOM_EFFICIENCY_MAX);
    count_slabs_all_wth_effcncy(&full);
    CHECK(full.total_capacity == 10);

    struct Room zero_eff = make_room(10, 0);
    count_slabs_all_wth_effcncy(&zero_eff);
    CHECK(zero_eff.total_capacity == 1); // would be 0, clamped up to 1
}

TEST_CASE("count_slabs_no_min_wth_effcncy scales by efficiency, but allows a true zero", "[kfx_sim][room_data]") {
    struct Room zero_eff = make_room(10, 0);
    count_slabs_no_min_wth_effcncy(&zero_eff);
    CHECK(zero_eff.total_capacity == 0); // no minimum-1 floor, unlike the sibling above

    struct Room full = make_room(10, ROOM_EFFICIENCY_MAX);
    count_slabs_no_min_wth_effcncy(&full);
    CHECK(full.total_capacity == 10);
}

TEST_CASE("count_slabs_div2_wth_effcncy halves the efficiency-scaled count, flooring at 1", "[kfx_sim][room_data]") {
    struct Room room = make_room(20, ROOM_EFFICIENCY_MAX);
    count_slabs_div2_wth_effcncy(&room);
    CHECK(room.total_capacity == 10); // (20 * 256 / 256) >> 1

    struct Room tiny = make_room(1, ROOM_EFFICIENCY_MAX);
    count_slabs_div2_wth_effcncy(&tiny);
    CHECK(tiny.total_capacity == 1); // (1 >> 1) == 0, clamped up to 1
}

TEST_CASE("count_slabs_div2_nomin_effcncy halves the efficiency-scaled count, allowing zero", "[kfx_sim][room_data]") {
    struct Room tiny = make_room(1, ROOM_EFFICIENCY_MAX);
    count_slabs_div2_nomin_effcncy(&tiny);
    CHECK(tiny.total_capacity == 0); // no minimum-1 floor here either

    struct Room room = make_room(20, ROOM_EFFICIENCY_MAX);
    count_slabs_div2_nomin_effcncy(&room);
    CHECK(room.total_capacity == 10);
}

TEST_CASE("count_slabs_mul2_wth_effcncy doubles the efficiency-scaled count, flooring at 1", "[kfx_sim][room_data]") {
    struct Room room = make_room(10, ROOM_EFFICIENCY_MAX);
    count_slabs_mul2_wth_effcncy(&room);
    CHECK(room.total_capacity == 20); // (10 * 256 / 256) << 1

    struct Room empty = make_room(0, ROOM_EFFICIENCY_MAX);
    count_slabs_mul2_wth_effcncy(&empty);
    CHECK(empty.total_capacity == 1); // 0 << 1 == 0, clamped up to 1
}

TEST_CASE("count_slabs_pow2_wth_effcncy scales by efficiency squared, flooring at 1", "[kfx_sim][room_data]") {
    struct Room full = make_room(10, ROOM_EFFICIENCY_MAX);
    count_slabs_pow2_wth_effcncy(&full);
    CHECK(full.total_capacity == 10); // efficiency^2/max^2 == 1 at full efficiency

    struct Room half = make_room(10, ROOM_EFFICIENCY_MAX / 2);
    count_slabs_pow2_wth_effcncy(&half);
    CHECK(half.total_capacity == 2); // quarter-scaled by the square, not halved

    struct Room zero_eff = make_room(1, 0);
    count_slabs_pow2_wth_effcncy(&zero_eff);
    CHECK(zero_eff.total_capacity == 1); // clamped up to 1
}
