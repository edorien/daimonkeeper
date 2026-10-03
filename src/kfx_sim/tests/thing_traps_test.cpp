// kfx_sim "thing" cluster depth increment, per docs/refactor/testing/
// comprehensive/stage-08b-kfx-sim-clusters.md's "still open" list:
// thing_traps.c's Thing-based classification predicates
// (trap_is_active/_is_slappable_by_player, thing_is_destructible_trap/
// _is_sellable_trap/_is_deployed_trap, creature_available_for_trap_trigger).
// get_trap_model_stats is a direct by-model-id config lookup, not routed
// through SimPort, so real per-model trap config (slappable/
// destructible/unsellable) is testable directly here -- unlike the
// get_creature_model_flags-based CMF_IsSpectator gate in
// creature_available_for_trap_trigger, which stays at its default (unset)
// for the same reason documented in room_graveyard_test.cpp. The
// map-search/trigger/activation functions (get_trap_for_position,
// update_trap_trigger*, activate_trap*) need a full map+thing-list fixture
// and are left for a later increment.
#include <catch2/catch_test_macros.hpp>

#include "thing_traps.h"
#include "thing_data.h"
#include "creature_states.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "thing_stats.h"

#include <cstring>

namespace {
struct ResetState {
    ResetState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
    }
};
}

TEST_CASE_METHOD(ResetState, "trap_is_active requires shots remaining and the rearm timer to have elapsed", "[kfx_sim][thing_traps]") {
    struct Thing *thing = thing_get(1);
    thing->trap.num_shots = 0;
    thing->trap.rearm_turn = 0;
    CHECK_FALSE(trap_is_active(thing)); // no shots left

    thing->trap.num_shots = 1;
    thing->trap.rearm_turn = 100; // default GetGameTurnFunc returns 0 -- rearm hasn't elapsed
    CHECK_FALSE(trap_is_active(thing));

    thing->trap.rearm_turn = 0;
    CHECK(trap_is_active(thing));
}

TEST_CASE_METHOD(ResetState, "trap_is_slappable_by_player requires the caller to own it, be configured slappable, and be active", "[kfx_sim][thing_traps]") {
    kfx_config_state.conf.trapdoor_conf.trap_types_count = 2;
    kfx_config_state.conf.trapdoor_conf.trap_cfgstats[1].slappable = 1;
    struct Thing *thing = thing_get(1);
    thing->owner = 0;
    thing->model = 1;
    thing->trap.num_shots = 1;
    thing->trap.rearm_turn = 0;

    CHECK(trap_is_slappable_by_player(thing, 0));
    CHECK_FALSE(trap_is_slappable_by_player(thing, 1)); // wrong player

    kfx_config_state.conf.trapdoor_conf.trap_cfgstats[1].slappable = 0;
    CHECK_FALSE(trap_is_slappable_by_player(thing, 0)); // not configured slappable

    kfx_config_state.conf.trapdoor_conf.trap_cfgstats[1].slappable = 1;
    thing->trap.num_shots = 0;
    CHECK_FALSE(trap_is_slappable_by_player(thing, 0)); // not active
}

TEST_CASE_METHOD(ResetState, "thing_is_destructible_trap returns -2 unless the thing is an active TCls_Trap, else the configured destructible value", "[kfx_sim][thing_traps]") {
    struct Thing *thing = thing_get(1);
    CHECK(thing_is_destructible_trap(thing) == -2); // wrong class (default 0)

    thing->class_id = TCls_Trap;
    thing->trap.num_shots = 0;
    CHECK(thing_is_destructible_trap(thing) == -2); // no shots left

    thing->trap.num_shots = 1;
    kfx_config_state.conf.trapdoor_conf.trap_types_count = 2;
    kfx_config_state.conf.trapdoor_conf.trap_cfgstats[1].destructible = 1;
    thing->model = 1;
    CHECK(thing_is_destructible_trap(thing) == 1);
}

TEST_CASE_METHOD(ResetState, "thing_is_sellable_trap checks class and the configured unsellable flag", "[kfx_sim][thing_traps]") {
    kfx_config_state.conf.trapdoor_conf.trap_types_count = 2;
    struct Thing *thing = thing_get(1);
    thing->class_id = TCls_Trap;
    thing->model = 1;

    kfx_config_state.conf.trapdoor_conf.trap_cfgstats[1].unsellable = 0;
    CHECK(thing_is_sellable_trap(thing));

    kfx_config_state.conf.trapdoor_conf.trap_cfgstats[1].unsellable = 1;
    CHECK_FALSE(thing_is_sellable_trap(thing));

    thing->class_id = TCls_Object;
    CHECK_FALSE(thing_is_sellable_trap(thing)); // wrong class, regardless of unsellable
}

TEST_CASE_METHOD(ResetState, "thing_is_deployed_trap only checks pointer validity and class, not existence flags", "[kfx_sim][thing_traps]") {
    struct Thing *thing = thing_get(1); // real slot, but alloc_flags==0 (not "existing")
    thing->class_id = TCls_Trap;

    CHECK(thing_is_deployed_trap(thing)); // thing_is_invalid, not thing_exists -- confirmed by reading the body
}

TEST_CASE_METHOD(ResetState, "creature_available_for_trap_trigger is true for a fresh, unencumbered creature", "[kfx_sim][thing_traps]") {
    struct Thing *creatng = thing_get(1);
    CHECK(creature_available_for_trap_trigger(creatng));
}

TEST_CASE_METHOD(ResetState, "creature_available_for_trap_trigger is false for a dying creature", "[kfx_sim][thing_traps]") {
    struct Thing *creatng = thing_get(1);
    creatng->health = -1;

    CHECK_FALSE(creature_available_for_trap_trigger(creatng));
}

TEST_CASE_METHOD(ResetState, "creature_available_for_trap_trigger is false for a dragged/pulled creature", "[kfx_sim][thing_traps]") {
    struct Thing *creatng = thing_get(1);
    creatng->alloc_flags |= TAlF_IsDragged;

    CHECK_FALSE(creature_available_for_trap_trigger(creatng));
}
