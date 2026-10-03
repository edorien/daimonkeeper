// kfx_sim "creature" cluster, hard-tail depth increment: the first test
// for creature_states_lair.c. Most of this file is lair-assignment/sleep
// state-machine dispatch needing a full Room+navigation fixture, but its
// classification predicates (creature_can_do_healing_sleep/_is_sleeping/
// _is_doing_toking/_is_doing_lair_activity/_requires_healing) are pure
// pattern A/B, reachable without any of that -- get_creature_state_besides_interruptions
// is a plain field read (thing->active_state/continue_state, with the
// CrSt_CreatureSlapCowers backup-state indirection), and
// creature_stats_get_from_thing resolves through the same default
// model-0 SimPort provider used throughout this plan.
#include <catch2/catch_test_macros.hpp>

#include "creature_states_lair.h"
#include "creature_control.h"
#include "globals.h"
#include "kfx_sim_test_fixtures.h"
#include "thing_stats.h"

using namespace kfx_test;

TEST_CASE_METHOD(ResetSimAndConfig, "creature_can_do_healing_sleep requires a non-neutral creature whose model has both heal_requirement and lair_size", "[kfx_sim][creature_states_lair]") {
    struct Thing *thing = make_creature(1, 1, 0);
    CHECK_FALSE(creature_can_do_healing_sleep(thing)); // both zero by default

    kfx_config_state.conf.crtr_conf.model[0].heal_requirement = 10;
    kfx_config_state.conf.crtr_conf.model[0].lair_size = 1;
    CHECK(creature_can_do_healing_sleep(thing));

    thing->owner = PLAYER_NEUTRAL;
    CHECK_FALSE(creature_can_do_healing_sleep(thing));
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_is_sleeping reads active_state directly", "[kfx_sim][creature_states_lair]") {
    struct Thing *thing = make_creature(1, 1, 0);
    CHECK_FALSE(creature_is_sleeping(thing));

    thing->active_state = CrSt_CreatureSleep;
    CHECK(creature_is_sleeping(thing));
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_is_doing_toking recognizes both toking-related states, seeing through a MoveToPosition wrapper", "[kfx_sim][creature_states_lair]") {
    struct Thing *thing = make_creature(1, 1, 0);
    thing->active_state = CrSt_ImpToking;
    CHECK(creature_is_doing_toking(thing));

    thing->active_state = CrSt_MoveToPosition;
    thing->continue_state = CrSt_CreatureGoingToSafetyForToking;
    CHECK(creature_is_doing_toking(thing));

    thing->continue_state = CrSt_CreatureSleep;
    CHECK_FALSE(creature_is_doing_toking(thing));
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_is_doing_lair_activity recognizes every lair-related state", "[kfx_sim][creature_states_lair]") {
    struct Thing *thing = make_creature(1, 1, 0);

    thing->active_state = CrSt_CreatureSleep;
    CHECK(creature_is_doing_lair_activity(thing));
    thing->active_state = CrSt_CreatureAtChangedLair;
    CHECK(creature_is_doing_lair_activity(thing));
    thing->active_state = CrSt_CreatureEat; // unrelated state
    CHECK_FALSE(creature_is_doing_lair_activity(thing));
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_requires_healing compares current health against heal_requirement-scaled max_health", "[kfx_sim][creature_states_lair]") {
    struct Thing *thing = make_creature(1, 1, 0);
    struct CreatureControl *cctrl = creature_control_get(1);
    cctrl->max_health = 256;
    kfx_config_state.conf.crtr_conf.model[0].heal_requirement = 128; // 50% of max_health -> minhealth 128

    thing->health = 200;
    CHECK_FALSE(creature_requires_healing(thing));

    thing->health = 100;
    CHECK(creature_requires_healing(thing));

    thing->health = 128; // exactly at the threshold -- "<=" includes it
    CHECK(creature_requires_healing(thing));
}
