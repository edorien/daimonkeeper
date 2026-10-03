// kfx_sim coverage, per docs/refactor/testing/comprehensive/
// stage-08b-kfx-sim-clusters.md's "still open" list: actionpt.c, a
// previously wholly-untouched file. Covers the accessor/allocation/
// flag-bookkeeping family (pattern A on kfx_sim_state.action_points[]) plus
// process_action_points' cheap "gate closed" branch (default GetGameTurnFunc
// returns 0, so an apt->num not divisible-by-8-when-added-to-turn-0 never
// reaches script_api_event_with_data() -- no fake needed for that
// path). The heavier thing/creature-list traversal functions
// (action_point_is_creature_from_list_within, action_point_get_players_within,
// process_action_points' triggered branch) need a fuller Thing/
// CreatureControl/Dungeon fixture and are left for a later increment.
#include <catch2/catch_test_macros.hpp>

#include "actionpt.h"
#include "globals.h"
#include "kfx_sim_state.h"

#include <cstring>

namespace {
struct ResetSimState {
    ResetSimState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
    }
};
}

TEST_CASE_METHOD(ResetSimState, "action_point_get bounds-checks apt_idx against 1..ACTN_POINTS_COUNT-1, reserving index 0", "[kfx_sim][actionpt]") {
    CHECK(action_point_get(0) == INVALID_ACTION_POINT);
    CHECK(action_point_get(-1) == INVALID_ACTION_POINT);
    CHECK(action_point_get(ACTN_POINTS_COUNT) == INVALID_ACTION_POINT);
    CHECK(action_point_get(1) == &kfx_sim_state.action_points[1]);
}

TEST_CASE_METHOD(ResetSimState, "action_point_is_invalid checks pointer identity against the reserved slot 0, not a range", "[kfx_sim][actionpt]") {
    // Unlike thing_is_invalid's range check against things_data[], this is
    // an exact match against INVALID_ACTION_POINT/NULL only -- confirmed by
    // reading the body, not assumed by family resemblance to other clusters.
    CHECK(action_point_is_invalid(INVALID_ACTION_POINT));
    CHECK(action_point_is_invalid(nullptr));
    CHECK_FALSE(action_point_is_invalid(&kfx_sim_state.action_points[1]));
    CHECK_FALSE(action_point_is_invalid(&kfx_sim_state.action_points[ACTN_POINTS_COUNT - 1]));
}

TEST_CASE_METHOD(ResetSimState, "action_point_exists is false for an invalid pointer, else reads the exists flag", "[kfx_sim][actionpt]") {
    CHECK_FALSE(action_point_exists(INVALID_ACTION_POINT));
    struct ActionPoint *apt = &kfx_sim_state.action_points[1];
    CHECK_FALSE(action_point_exists(apt));
    apt->exists = true;
    CHECK(action_point_exists(apt));
}

TEST_CASE_METHOD(ResetSimState, "action_point_exists_idx resolves through action_point_get", "[kfx_sim][actionpt]") {
    CHECK_FALSE(action_point_exists_idx(0)); // reserved index -> INVALID_ACTION_POINT
    kfx_sim_state.action_points[3].exists = true;
    CHECK(action_point_exists_idx(3));
}

TEST_CASE_METHOD(ResetSimState, "action_point_get_by_number finds the first slot with a matching num, or INVALID_ACTION_POINT", "[kfx_sim][actionpt]") {
    kfx_sim_state.action_points[5].num = 42;
    CHECK(action_point_get_by_number(42) == &kfx_sim_state.action_points[5]);
    CHECK(action_point_get_by_number(999) == INVALID_ACTION_POINT);
}

TEST_CASE_METHOD(ResetSimState, "action_point_number_to_index returns the matching slot's index, or -1", "[kfx_sim][actionpt]") {
    kfx_sim_state.action_points[7].num = 42;
    CHECK(action_point_number_to_index(42) == 7);
    CHECK(action_point_number_to_index(999) == -1);
}

TEST_CASE_METHOD(ResetSimState, "action_point_get_free returns the first non-existing slot from index 1 onward", "[kfx_sim][actionpt]") {
    kfx_sim_state.action_points[1].exists = true;
    kfx_sim_state.action_points[2].exists = true;

    CHECK(action_point_get_free() == &kfx_sim_state.action_points[3]);
}

TEST_CASE_METHOD(ResetSimState, "action_point_get_free returns INVALID_ACTION_POINT once every slot is taken", "[kfx_sim][actionpt]") {
    for (int64_t i = 1; i < ACTN_POINTS_COUNT; i++)
        kfx_sim_state.action_points[i].exists = true;

    CHECK(action_point_get_free() == INVALID_ACTION_POINT);
}

TEST_CASE_METHOD(ResetSimState, "allocate_free_action_point_structure_with_number refuses to allocate over an existing number", "[kfx_sim][actionpt]") {
    kfx_sim_state.action_points[4].exists = true;
    kfx_sim_state.action_points[4].num = 42;

    CHECK(allocate_free_action_point_structure_with_number(42) == INVALID_ACTION_POINT);
}

TEST_CASE_METHOD(ResetSimState, "allocate_free_action_point_structure_with_number claims a free slot and initializes it", "[kfx_sim][actionpt]") {
    struct ActionPoint *apt = allocate_free_action_point_structure_with_number(42);

    CHECK(apt == &kfx_sim_state.action_points[1]); // first free slot from index 1
    CHECK(apt->exists == true);
    CHECK(apt->num == 42);
    CHECK(apt->activated == 0);
}

TEST_CASE_METHOD(ResetSimState, "actnpoint_create_actnpoint allocates by number and copies position/range", "[kfx_sim][actionpt]") {
    struct InitActionPoint iapt{};
    iapt.num = 7;
    iapt.mappos.x.val = 100;
    iapt.mappos.y.val = 200;
    iapt.range = 5;

    struct ActionPoint *apt = actnpoint_create_actnpoint(&iapt);

    CHECK(apt->num == 7);
    CHECK(apt->mappos.x.val == 100);
    CHECK(apt->mappos.y.val == 200);
    CHECK(apt->range == 5);
}

TEST_CASE_METHOD(ResetSimState, "action_point_reset_idx clears one player's flag, or all of them for ALL_PLAYERS", "[kfx_sim][actionpt]") {
    struct ActionPoint *apt = &kfx_sim_state.action_points[1];
    apt->exists = true;
    apt->activated = to_flag(2) | to_flag(3);

    CHECK(action_point_reset_idx(1, 2));
    CHECK(apt->activated == to_flag(3));

    CHECK(action_point_reset_idx(1, ALL_PLAYERS));
    CHECK(apt->activated == 0);
}

TEST_CASE_METHOD(ResetSimState, "action_point_trigger_idx sets one player's flag, or every player's for ALL_PLAYERS", "[kfx_sim][actionpt]") {
    struct ActionPoint *apt = &kfx_sim_state.action_points[1];
    apt->exists = true;

    CHECK(action_point_trigger_idx(1, 2));
    CHECK(apt->activated == to_flag(2));

    CHECK(action_point_trigger_idx(1, ALL_PLAYERS));
    CHECK(apt->activated == 0x1FF);
}

TEST_CASE_METHOD(ResetSimState, "action_point_activated_by_player reads the reserved slot 0 for an out-of-range index rather than reporting false", "[kfx_sim][actionpt]") {
    // Unlike action_point_exists_idx, this function never calls
    // action_point_is_invalid on the result of action_point_get -- an
    // out-of-range apt_idx silently reads INVALID_ACTION_POINT's own
    // (usually all-zero) activated field instead. Confirmed by reading the
    // body: no is_invalid guard before the flag_is_set dereference.
    CHECK_FALSE(action_point_activated_by_player(0, 2)); // slot 0 is zeroed -- reads as "not activated"
    kfx_sim_state.action_points[0].activated = to_flag(2); // the reserved slot itself, not a real action point
    CHECK(action_point_activated_by_player(999, 2)); // still resolves to slot 0
}

TEST_CASE_METHOD(ResetSimState, "clear_action_points zeroes every slot, including the reserved index 0", "[kfx_sim][actionpt]") {
    kfx_sim_state.action_points[0].num = 1;
    kfx_sim_state.action_points[5].exists = true;
    kfx_sim_state.action_points[5].num = 42;

    clear_action_points();

    CHECK(kfx_sim_state.action_points[0].num == 0);
    CHECK(kfx_sim_state.action_points[5].exists == false);
    CHECK(kfx_sim_state.action_points[5].num == 0);
}

TEST_CASE_METHOD(ResetSimState, "delete_action_point_structure clears an existing slot, leaves a non-existing one untouched", "[kfx_sim][actionpt]") {
    struct ActionPoint *apt = &kfx_sim_state.action_points[1];
    apt->exists = true;
    apt->num = 42;
    delete_action_point_structure(apt);
    CHECK(apt->num == 0);
    CHECK(apt->exists == false);

    struct ActionPoint *untouched = &kfx_sim_state.action_points[2];
    untouched->num = 7; // exists is false -- the guard should skip this slot
    delete_action_point_structure(untouched);
    CHECK(untouched->num == 7);
}

TEST_CASE_METHOD(ResetSimState, "delete_all_action_point_structures clears every existing slot from index 1 onward", "[kfx_sim][actionpt]") {
    kfx_sim_state.action_points[0].exists = true;
    kfx_sim_state.action_points[0].num = 1; // reserved index -- must survive untouched
    kfx_sim_state.action_points[3].exists = true;
    kfx_sim_state.action_points[3].num = 42;

    delete_all_action_point_structures();

    CHECK(kfx_sim_state.action_points[0].num == 1);
    CHECK(kfx_sim_state.action_points[3].exists == false);
    CHECK(kfx_sim_state.action_points[3].num == 0);
}

TEST_CASE_METHOD(ResetSimState, "process_action_points leaves activation state untouched on turns its own gate doesn't open", "[kfx_sim][actionpt]") {
    // Default GetGameTurnFunc returns 0, so the gate ((num+turn)&7)==0 opens
    // only when num is itself a multiple of 8 -- num=1 never opens it, so
    // action_point_get_players_within/ScriptPort are never reached.
    struct ActionPoint *apt = &kfx_sim_state.action_points[1];
    apt->exists = true;
    apt->num = 1;
    apt->activated = to_flag(2);

    CHECK(process_action_points());
    CHECK(apt->activated == to_flag(2)); // unchanged
}
