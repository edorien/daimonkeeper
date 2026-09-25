// kfx_sim coverage, per docs/refactor/testing/comprehensive/
// stage-08b-kfx-sim-clusters.md's "still open" list: tasks_list.c's dig/
// task-list bookkeeping, one of the few remaining wholly-untouched files
// that's pure pattern A (kfx_sim_state.dungeon[].task_list[]) with no
// creature/room fixture needed. map_subtiles_x/_y is set to a plausible
// map size (85, matching real DK skirmish maps) because add_task_list_entry
// and the by_slab/by_subtile lookups round-trip coordinates through
// get_subtile_number/stl_slab_center_subtile, both of which read it.
#include <catch2/catch_test_macros.hpp>

#include "tasks_list.h"
#include "dungeon_data.h"
#include "map_data.h"
#include "spdigger_stack.h"
#include "kfx_sim_state.h"

#include <cstring>

namespace {
struct ResetSimState {
    ResetSimState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        kfx_sim_state.map_subtiles_x = 85;
        kfx_sim_state.map_subtiles_y = 85;
    }
};
}

TEST_CASE_METHOD(ResetSimState, "get_dungeon_task_list_entry bounds-checks task_idx against 0..MAPTASKS_COUNT-1", "[kfx_sim][tasks_list]") {
    struct Dungeon *dungeon = get_dungeon(0);
    CHECK(get_dungeon_task_list_entry(dungeon, -1) == INVALID_MAP_TASK);
    CHECK(get_dungeon_task_list_entry(dungeon, MAPTASKS_COUNT) == INVALID_MAP_TASK);
    CHECK(get_dungeon_task_list_entry(dungeon, 0) == &dungeon->task_list[0]);
}

TEST_CASE_METHOD(ResetSimState, "get_task_list_entry returns INVALID_MAP_TASK for an out-of-range player", "[kfx_sim][tasks_list]") {
    CHECK(get_task_list_entry(-1, 0) == INVALID_MAP_TASK);
    CHECK(get_task_list_entry(DUNGEONS_COUNT, 0) == INVALID_MAP_TASK);
}

TEST_CASE_METHOD(ResetSimState, "get_task_list_entry resolves the player's own task_list entry", "[kfx_sim][tasks_list]") {
    struct Dungeon *dungeon = get_dungeon(2);
    CHECK(get_task_list_entry(2, 5) == &dungeon->task_list[5]);
}

TEST_CASE_METHOD(ResetSimState, "add_task_list_entry fills the first free slot and stores an already-centered coord unchanged", "[kfx_sim][tasks_list]") {
    SubtlCodedCoords centered = get_subtile_number(4, 4); // slab (1,1)'s own center subtile
    add_task_list_entry(0, SDDigTask_DigEarth, centered);

    struct Dungeon *dungeon = get_dungeon(0);
    CHECK(dungeon->task_count == 1);
    CHECK(dungeon->highest_task_number == 1);
    CHECK(dungeon->task_list[0].kind == SDDigTask_DigEarth);
    CHECK(dungeon->task_list[0].coords == centered);
}

TEST_CASE_METHOD(ResetSimState, "add_task_list_entry snaps a non-centered subtile to its slab's center", "[kfx_sim][tasks_list]") {
    // stl (5,6): subtile_slab(5)=1, subtile_slab(6)=2 -> slab (1,2)'s center is (4,7).
    add_task_list_entry(0, SDDigTask_DigEarth, get_subtile_number(5, 6));

    struct Dungeon *dungeon = get_dungeon(0);
    CHECK(dungeon->task_list[0].coords == get_subtile_number(4, 7));
}

TEST_CASE_METHOD(ResetSimState, "add_task_list_entry reuses a freed slot before extending highest_task_number", "[kfx_sim][tasks_list]") {
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->highest_task_number = 2;
    dungeon->task_count = 1;
    dungeon->task_list[0].kind = 0; // freed
    dungeon->task_list[1].kind = SDDigTask_DigEarth;

    add_task_list_entry(0, SDDigTask_MineGold, get_subtile_number(4, 4));

    CHECK(dungeon->highest_task_number == 2); // unchanged, slot 0 was reused
    CHECK(dungeon->task_count == 2);
    CHECK(dungeon->task_list[0].kind == SDDigTask_MineGold);
}

TEST_CASE_METHOD(ResetSimState, "add_task_list_entry is a no-op once every slot up to MAPTASKS_COUNT is occupied", "[kfx_sim][tasks_list]") {
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->highest_task_number = MAPTASKS_COUNT;
    dungeon->task_count = MAPTASKS_COUNT;
    for (int64_t i = 0; i < MAPTASKS_COUNT; i++)
        dungeon->task_list[i].kind = SDDigTask_DigEarth;

    add_task_list_entry(0, SDDigTask_MineGold, get_subtile_number(4, 4));

    CHECK(dungeon->highest_task_number == MAPTASKS_COUNT);
    CHECK(dungeon->task_count == MAPTASKS_COUNT);
}

TEST_CASE_METHOD(ResetSimState, "find_from_task_list returns the index of a matching coords entry, or -1", "[kfx_sim][tasks_list]") {
    struct Dungeon *dungeon = get_dungeon(1);
    dungeon->highest_task_number = 2;
    dungeon->task_list[0].kind = SDDigTask_DigEarth;
    dungeon->task_list[0].coords = get_subtile_number(4, 4);
    dungeon->task_list[1].kind = SDDigTask_MineGold;
    dungeon->task_list[1].coords = get_subtile_number(7, 4);

    CHECK(find_from_task_list(1, get_subtile_number(7, 4)) == 1);
    CHECK(find_from_task_list(1, get_subtile_number(10, 10)) == -1);
}

TEST_CASE_METHOD(ResetSimState, "find_from_task_list_by_slab resolves the slab's center subtile before searching", "[kfx_sim][tasks_list]") {
    struct Dungeon *dungeon = get_dungeon(1);
    dungeon->highest_task_number = 1;
    dungeon->task_list[0].kind = SDDigTask_DigEarth;
    dungeon->task_list[0].coords = get_subtile_number_at_slab_center(2, 3);

    CHECK(find_from_task_list_by_slab(1, 2, 3) == 0);
    CHECK(find_from_task_list_by_slab(1, 5, 5) == -1);
}

TEST_CASE_METHOD(ResetSimState, "find_from_task_list_by_subtile snaps to the slab center before searching", "[kfx_sim][tasks_list]") {
    struct Dungeon *dungeon = get_dungeon(1);
    dungeon->highest_task_number = 1;
    dungeon->task_list[0].kind = SDDigTask_DigEarth;
    dungeon->task_list[0].coords = get_subtile_number(4, 7); // slab (1,2)'s center

    // Any raw subtile inside slab (1,2) -- e.g. (5,6) -- must resolve to the
    // same stored center coords.
    CHECK(find_from_task_list_by_subtile(1, 5, 6) == 0);
}

TEST_CASE_METHOD(ResetSimState, "find_dig_from_task_list matches find_from_task_list's coords search exactly", "[kfx_sim][tasks_list]") {
    // Reads the same task_list by the same coords-equality rule -- current
    // behavior confirmed identical to find_from_task_list, not just
    // similarly named.
    struct Dungeon *dungeon = get_dungeon(1);
    dungeon->highest_task_number = 1;
    dungeon->task_list[0].kind = SDDigTask_DigEarth;
    dungeon->task_list[0].coords = get_subtile_number(4, 4);

    CHECK(find_dig_from_task_list(1, get_subtile_number(4, 4)) == 0);
    CHECK(find_dig_from_task_list(1, get_subtile_number(10, 10)) == -1);
}

TEST_CASE_METHOD(ResetSimState, "find_next_dig_in_dungeon_task_list skips SDDigTask_None slots and returns the next real task", "[kfx_sim][tasks_list]") {
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->highest_task_number = 4;
    dungeon->task_list[0].kind = SDDigTask_DigEarth;
    dungeon->task_list[1].kind = SDDigTask_None;
    dungeon->task_list[2].kind = SDDigTask_MineGold;
    dungeon->task_list[3].kind = SDDigTask_None;

    CHECK(find_next_dig_in_dungeon_task_list(dungeon, -1) == 0);
    CHECK(find_next_dig_in_dungeon_task_list(dungeon, 0) == 2);
    CHECK(find_next_dig_in_dungeon_task_list(dungeon, 2) == -1);
}

TEST_CASE_METHOD(ResetSimState, "remove_from_task_list rejects an out-of-range stack_pos", "[kfx_sim][tasks_list]") {
    CHECK(remove_from_task_list(0, -1) == 0);
    CHECK(remove_from_task_list(0, 0) == 0); // highest_task_number is 0 on a fresh dungeon
}

TEST_CASE_METHOD(ResetSimState, "remove_from_task_list clears a non-final entry without shrinking highest_task_number", "[kfx_sim][tasks_list]") {
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->highest_task_number = 3;
    dungeon->task_count = 3;
    dungeon->task_list[0].kind = SDDigTask_DigEarth;
    dungeon->task_list[1].kind = SDDigTask_MineGold;
    dungeon->task_list[2].kind = SDDigTask_MineGems;

    CHECK(remove_from_task_list(0, 0) == 1);
    CHECK(dungeon->task_list[0].kind == 0);
    CHECK(dungeon->task_list[0].coords == 0);
    CHECK(dungeon->task_count == 2);
    CHECK(dungeon->highest_task_number == 3); // slot 0 isn't the top entry
}

TEST_CASE_METHOD(ResetSimState, "remove_from_task_list at the top shrinks highest_task_number past trailing empty slots", "[kfx_sim][tasks_list]") {
    struct Dungeon *dungeon = get_dungeon(0);
    dungeon->highest_task_number = 3;
    dungeon->task_count = 2;
    dungeon->task_list[0].kind = SDDigTask_DigEarth;
    dungeon->task_list[1].kind = 0; // already-freed gap
    dungeon->task_list[2].kind = SDDigTask_MineGold; // the top entry, being removed

    CHECK(remove_from_task_list(0, 2) == 1);
    CHECK(dungeon->task_count == 1);
    // Scans back from the freshly-cleared top slot (2) through slot 1
    // (already empty) and stops at slot 0, the last real entry.
    CHECK(dungeon->highest_task_number == 1);
}
