// kfx_sim "room" cluster depth increment, per docs/refactor/testing/
// comprehensive/stage-08b-kfx-sim-clusters.md's "still open" list:
// room_treasure.c's count_gold_slabs_* family, the same shape as
// room_data.c's count_slabs_*_wth_effcncy siblings already covered in
// room_capacity_test.cpp -- a pure function of room->slabs_count/efficiency
// -> room->total_capacity. get_wealth_size_types_count() (thing_objects.c)
// is a hidden dependency, but it's a compile-time constant (5, the length
// of gold_hoard_objects[]) with no state to fake -- confirmed by reading
// its body, not assumed. The rest of room_treasure.c
// (find_gold_hoarde_at/treasure_room_eats_gold_piles/
// count_gold_hoardes_in_room) needs a real map+thing-list fixture and is
// left for a later increment.
#include <catch2/catch_test_macros.hpp>

#include "room_treasure.h"
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

TEST_CASE("count_gold_slabs_wth_effcncy scales by efficiency (5 wealth sizes), flooring at 1", "[kfx_sim][room_treasure]") {
    struct Room full = make_room(10, ROOM_EFFICIENCY_MAX);
    count_gold_slabs_wth_effcncy(&full);
    CHECK(full.total_capacity == 50); // 10 * (5*256/256)

    struct Room zero_eff = make_room(10, 0);
    count_gold_slabs_wth_effcncy(&zero_eff);
    CHECK(zero_eff.total_capacity == 10); // subefficiency floors to 1, not 0

    struct Room empty = make_room(0, 0);
    count_gold_slabs_wth_effcncy(&empty);
    CHECK(empty.total_capacity == 1); // slabs_count*subefficiency==0, clamped up to 1
}

TEST_CASE("count_gold_slabs_full ignores efficiency, always 5 per slab", "[kfx_sim][room_treasure]") {
    struct Room room = make_room(4, 0);
    count_gold_slabs_full(&room);
    CHECK(room.total_capacity == 20);
}

TEST_CASE("count_gold_slabs_div2 halves the full 5-per-slab count", "[kfx_sim][room_treasure]") {
    struct Room room = make_room(4, 0);
    count_gold_slabs_div2(&room);
    CHECK(room.total_capacity == 10); // (4*5)/2

    struct Room odd = make_room(1, 0);
    count_gold_slabs_div2(&odd);
    CHECK(odd.total_capacity == 2); // (1*5)/2 == 2, no minimum-1 floor here
}
