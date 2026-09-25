// kfx_sim "creature" cluster, depth increment: creature_states.c's
// can_change_from_state_to() -- the actual state-transition permission
// logic stage-08's fan-in argument was about, not just the accessor
// layer beneath it (creature_control_test.cpp). Driven entirely by
// kfx_config_state.conf.crtr_conf.states[] (pattern A) and a handful of
// struct Thing fields -- no live simulation tick, no CreatureControl
// needed, since this function never dereferences one.
//
// Also covers get_thing_state_info_num()/state_info_invalid(), the same
// "index sentinel" family as every other cluster's accessors
// (docs/refactor/testing/comprehensive/stage-08b-kfx-sim-clusters.md),
// here over kfx_config_state.conf.crtr_conf.states[] instead of a
// kfx_sim_state array.
#include <catch2/catch_test_macros.hpp>

#include "creature_states.h"
#include "creature_control.h"
#include "config_creature.h"
#include "config_keeperfx.h"
#include "config_objects.h"
#include "thing_objects.h"
#include "kfx_config_state.h"
#include "thing_data.h"
#include "globals.h"
#include "kfx_sim_test_fixtures.h"

#include <cstring>

namespace {
struct ResetConfigState {
    // states_count now gates get_thing_state_info_num()/get_thing_active_
    // state_info()/get_thing_continue_state_info()'s bounds check (upstream
    // #5237 replaced the old compile-time CREATURE_STATES_COUNT macro with
    // this runtime, config-driven count) -- a fresh memset leaves it 0,
    // which would make every non-zero state id look "out of range". Set to
    // CrSt_ListEnd, the same bound CREATURE_STATES_COUNT used to be defined
    // as, so existing in-range/out-of-range test expectations still hold.
    ResetConfigState() {
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        kfx_config_state.conf.crtr_conf.states_count = CrSt_ListEnd;
    }
};

struct CreatureStateFixture : ResetConfigState {
    struct Thing thing{};
};
}

TEST_CASE_METHOD(ResetConfigState, "state_info_invalid rejects the reserved index-0 sentinel", "[kfx_sim][creature_states]") {
    CHECK(state_info_invalid(get_thing_state_info_num(0)));
}

TEST_CASE_METHOD(ResetConfigState, "state_info_invalid accepts an in-range state id", "[kfx_sim][creature_states]") {
    CHECK_FALSE(state_info_invalid(get_thing_state_info_num(1)));
}

TEST_CASE_METHOD(ResetConfigState, "get_thing_state_info_num falls back to slot 0 for an out-of-range id", "[kfx_sim][creature_states]") {
    CHECK(get_thing_state_info_num(CrSt_ListEnd) == get_thing_state_info_num(0));
}

TEST_CASE_METHOD(ResetConfigState, "get_thing_state_info_num respects a runtime states_count narrower than CrSt_ListEnd", "[kfx_sim][creature_states]") {
    // Upstream #5237's whole point: the bound is config-driven, not the old
    // fixed CREATURE_STATES_COUNT -- a state id that's in-range for the enum
    // but at/beyond the loaded config's actual states_count still falls back.
    kfx_config_state.conf.crtr_conf.states_count = 2;
    CHECK_FALSE(state_info_invalid(get_thing_state_info_num(1)));
    CHECK(get_thing_state_info_num(2) == get_thing_state_info_num(0));
}

TEST_CASE_METHOD(CreatureStateFixture, "can_change_from_state_to: a controlled creature can only move to an idle-type state", "[kfx_sim][creature_states]") {
    thing.alloc_flags |= TAlF_IsControlled;
    kfx_config_state.conf.crtr_conf.states[2].state_type = CrStTyp_Sleep;
    CHECK_FALSE(can_change_from_state_to(&thing, 1, 2));

    kfx_config_state.conf.crtr_conf.states[2].state_type = CrStTyp_Idle;
    CHECK(can_change_from_state_to(&thing, 1, 2));
}

TEST_CASE_METHOD(CreatureStateFixture, "can_change_from_state_to: a transition-flagged current state blocks the change unless overridden", "[kfx_sim][creature_states]") {
    kfx_config_state.conf.crtr_conf.states[1].transition = true;
    CHECK_FALSE(can_change_from_state_to(&thing, 1, 2));

    kfx_config_state.conf.crtr_conf.states[2].override_transition = true;
    CHECK(can_change_from_state_to(&thing, 1, 2));
}

TEST_CASE_METHOD(CreatureStateFixture, "can_change_from_state_to: a captive current state blocks the change unless overridden", "[kfx_sim][creature_states]") {
    kfx_config_state.conf.crtr_conf.states[1].captive = true;
    CHECK_FALSE(can_change_from_state_to(&thing, 1, 2));

    kfx_config_state.conf.crtr_conf.states[2].override_captive = true;
    CHECK(can_change_from_state_to(&thing, 1, 2));
}

TEST_CASE_METHOD(CreatureStateFixture, "can_change_from_state_to: the current state type's matching override gates the change", "[kfx_sim][creature_states]") {
    kfx_config_state.conf.crtr_conf.states[1].state_type = CrStTyp_Sleep;
    CHECK_FALSE(can_change_from_state_to(&thing, 1, 2)); // override_sleep defaults to false

    kfx_config_state.conf.crtr_conf.states[2].override_sleep = true;
    CHECK(can_change_from_state_to(&thing, 1, 2));
}

TEST_CASE_METHOD(CreatureStateFixture, "can_change_from_state_to: an unmatched current state type defaults to allowing the change", "[kfx_sim][creature_states]") {
    // CrStTyp_Idle (the zero value, unset default) has no case in the
    // switch -- falls through to `default: return true;`.
    CHECK(can_change_from_state_to(&thing, 1, 2));
}

// --- The creature_is_X()/get_creature_state_besides_*() family: a large
// cluster of pure state-comparison predicates, all driven directly by
// struct Thing's active_state/continue_state fields (and, for the
// CrSt_CreatureSlapCowers backup case, CreatureControl's
// active_state_bkp/continue_state_bkp). Unlike can_change_from_state_to()
// above, most of these route through thing_is_invalid()/
// creature_control_get_from_thing() somewhere in the chain, so they need
// a real Thing+CreatureControl slot (kfx_sim_test_fixtures.h's
// make_creature) rather than a stack-local struct Thing.
using namespace kfx_test;

TEST_CASE_METHOD(ResetSimAndConfig, "get_creature_state_besides_interruptions looks through MoveToPosition/MoveBackwardsToPosition, then through a CreatureSlapCowers backup", "[kfx_sim][creature_states]") {
    struct Thing *thing = make_creature(1, 1, 0);
    thing->active_state = CrSt_Training;
    CHECK(get_creature_state_besides_interruptions(thing) == CrSt_Training);

    thing->active_state = CrSt_MoveToPosition;
    thing->continue_state = CrSt_Scavengering;
    CHECK(get_creature_state_besides_interruptions(thing) == CrSt_Scavengering);

    // CrSt_CreatureSlapCowers: looks through to the backed-up state instead.
    thing->active_state = CrSt_CreatureSlapCowers;
    struct CreatureControl *cctrl = creature_control_get(1);
    cctrl->active_state_bkp = CrSt_AtTrainingRoom;
    CHECK(get_creature_state_besides_interruptions(thing) == CrSt_AtTrainingRoom);

    // ...and that backed-up state is itself looked through if it's a Move state.
    cctrl->active_state_bkp = CrSt_MoveBackwardsToPosition;
    cctrl->continue_state_bkp = CrSt_AtScavengerRoom;
    CHECK(get_creature_state_besides_interruptions(thing) == CrSt_AtScavengerRoom);
}

TEST_CASE_METHOD(ResetSimAndConfig, "get_creature_state_besides_move looks through MoveToPosition only, not MoveBackwardsToPosition", "[kfx_sim][creature_states]") {
    struct Thing *thing = make_creature(1, 1, 0);
    thing->active_state = CrSt_MoveToPosition;
    thing->continue_state = CrSt_Training;
    CHECK(get_creature_state_besides_move(thing) == CrSt_Training);

    thing->active_state = CrSt_MoveBackwardsToPosition;
    CHECK(get_creature_state_besides_move(thing) == CrSt_MoveBackwardsToPosition); // not looked through
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_is_dying reads thing->health directly", "[kfx_sim][creature_states]") {
    struct Thing *thing = make_creature(1, 1, 0);
    thing->health = 5;
    CHECK_FALSE(creature_is_dying(thing));
    thing->health = -1;
    CHECK(creature_is_dying(thing));
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_is_being_dropped/_celebrating look through the Move states to a single target state", "[kfx_sim][creature_states]") {
    struct Thing *thing = make_creature(1, 1, 0);
    thing->active_state = CrSt_MoveToPosition;
    thing->continue_state = CrSt_CreatureBeingDropped;
    CHECK(creature_is_being_dropped(thing));
    CHECK_FALSE(creature_is_celebrating(thing));

    thing->continue_state = CrSt_CreatureBeHappy;
    CHECK_FALSE(creature_is_being_dropped(thing));
    CHECK(creature_is_celebrating(thing));
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_is_being_tortured(_including_kinky) matches the torture-room states", "[kfx_sim][creature_states]") {
    struct Thing *thing = make_creature(1, 1, 0);
    thing->active_state = CrSt_AtTortureRoom;
    CHECK(creature_is_being_tortured(thing));
    CHECK(creature_is_being_tortured_including_kinky(thing));

    thing->active_state = CrSt_AtKinkyTortureRoom;
    CHECK_FALSE(creature_is_being_tortured(thing)); // kinky variant not included in the plain check
    CHECK(creature_is_being_tortured_including_kinky(thing));
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_is_being_sacrificed/creature_is_kept_in_prison/creature_is_being_summoned/creature_is_training match their respective state pairs", "[kfx_sim][creature_states]") {
    struct Thing *thing = make_creature(1, 1, 0);

    thing->active_state = CrSt_CreatureSacrifice;
    CHECK(creature_is_being_sacrificed(thing));
    thing->active_state = CrSt_CreatureBeingSacrificed;
    CHECK(creature_is_being_sacrificed(thing));

    thing->active_state = CrSt_CreatureInPrison;
    CHECK(creature_is_kept_in_prison(thing));
    thing->active_state = CrSt_CreatureArrivedAtPrison;
    CHECK(creature_is_kept_in_prison(thing));

    thing->active_state = CrSt_CreatureBeingSummoned;
    CHECK(creature_is_being_summoned(thing));

    thing->active_state = CrSt_Training;
    CHECK(creature_is_training(thing));
    thing->active_state = CrSt_AtTrainingRoom;
    CHECK(creature_is_training(thing));

    thing->active_state = CrSt_CreatureDoingNothing;
    CHECK_FALSE(creature_is_being_sacrificed(thing));
    CHECK_FALSE(creature_is_kept_in_prison(thing));
    CHECK_FALSE(creature_is_being_summoned(thing));
    CHECK_FALSE(creature_is_training(thing));
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_is_doing_anger_job reads the resolved state's state_type", "[kfx_sim][creature_states]") {
    struct Thing *thing = make_creature(1, 1, 0);
    thing->active_state = 3;
    kfx_config_state.conf.crtr_conf.states[3].state_type = CrStTyp_AngerJob;
    CHECK(creature_is_doing_anger_job(thing));

    kfx_config_state.conf.crtr_conf.states[3].state_type = CrStTyp_Idle;
    CHECK_FALSE(creature_is_doing_anger_job(thing));
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_is_doing_garden_activity/creature_is_scavengering/creature_is_being_scavenged match their respective states", "[kfx_sim][creature_states]") {
    struct Thing *thing = make_creature(1, 1, 0);

    thing->active_state = CrSt_CreatureEat;
    CHECK(creature_is_doing_garden_activity(thing));
    thing->active_state = CrSt_CreatureArrivedAtGarden;
    CHECK(creature_is_doing_garden_activity(thing));

    thing->active_state = CrSt_Scavengering;
    CHECK(creature_is_scavengering(thing));
    thing->active_state = CrSt_AtScavengerRoom;
    CHECK(creature_is_scavengering(thing));

    thing->active_state = CrSt_CreatureBeingScavenged;
    CHECK(creature_is_being_scavenged(thing));
    CHECK_FALSE(creature_is_scavengering(thing));
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_is_at_alarm/creature_is_escaping_death/creature_is_fleeing_combat match a single state each", "[kfx_sim][creature_states]") {
    struct Thing *thing = make_creature(1, 1, 0);

    thing->active_state = CrSt_ArriveAtAlarm;
    CHECK(creature_is_at_alarm(thing));

    thing->active_state = CrSt_CreatureEscapingDeath;
    CHECK(creature_is_escaping_death(thing));

    thing->active_state = CrSt_CreatureCombatFlee;
    CHECK(creature_is_fleeing_combat(thing));
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_affected_by_call_to_arms reads CreatureControl::called_to_arms directly; creature_is_called_to_arms checks the CTA states", "[kfx_sim][creature_states]") {
    struct Thing *thing = make_creature(1, 1, 0);
    CHECK_FALSE(creature_affected_by_call_to_arms(thing));
    creature_control_get(1)->called_to_arms = 1;
    CHECK(creature_affected_by_call_to_arms(thing));

    thing->active_state = CrSt_AlreadyAtCallToArms;
    CHECK(creature_is_called_to_arms(thing));
    thing->active_state = CrSt_ArriveAtCallToArms;
    CHECK(creature_is_called_to_arms(thing));
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_is_taking_salary_activity looks through MoveToPosition (via get_creature_state_besides_move) to the salary states", "[kfx_sim][creature_states]") {
    struct Thing *thing = make_creature(1, 1, 0);
    thing->active_state = CrSt_MoveToPosition;
    thing->continue_state = CrSt_CreatureWantsSalary;
    CHECK(creature_is_taking_salary_activity(thing));

    thing->continue_state = CrSt_CreatureTakeSalary;
    CHECK(creature_is_taking_salary_activity(thing));
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_is_kept_in_custody is true if any of its four constituent states hold", "[kfx_sim][creature_states]") {
    struct Thing *thing = make_creature(1, 1, 0);
    CHECK_FALSE(creature_is_kept_in_custody(thing));

    thing->active_state = CrSt_CreatureInPrison;
    CHECK(creature_is_kept_in_custody(thing));

    thing->active_state = CrSt_AtTortureRoom;
    CHECK(creature_is_kept_in_custody(thing));

    thing->active_state = CrSt_CreatureBeingSacrificed;
    CHECK(creature_is_kept_in_custody(thing));

    thing->active_state = CrSt_CreatureBeingDropped;
    CHECK(creature_is_kept_in_custody(thing));
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_is_dragging_something/creature_is_dragging_spellbook check CreatureControl::dragtng_idx against a real, existing thing", "[kfx_sim][creature_states]") {
    struct Thing *thing = make_creature(1, 1, 0);
    CHECK_FALSE(creature_is_dragging_something(thing));
    CHECK_FALSE(creature_is_dragging_spellbook(thing));

    struct Thing *dragged = make_creature(2, 2, 0);
    creature_control_get(1)->dragtng_idx = 2;
    CHECK(creature_is_dragging_something(thing));
    CHECK_FALSE(creature_is_dragging_spellbook(thing)); // dragging a creature, not a spellbook

    dragged->class_id = TCls_Object;
    dragged->model = 5;
    kfx_config_state.conf.object_conf.object_types_count = 6;
    kfx_config_state.conf.object_conf.object_cfgstats[5].genre = OCtg_Spellbook;
    CHECK(creature_is_dragging_spellbook(thing));
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_model_bleeds returns the model's bleeds flag directly, or gates it to evil creatures only under censorship", "[kfx_sim][creature_states]") {
    kfx_config_state.conf.crtr_conf.model[1].bleeds = 1;
    CHECK(creature_model_bleeds(1));

    features_enabled |= Ft_Censorship;
    kfx_config_state.conf.crtr_conf.model[1].model_flags = 0; // not evil
    CHECK_FALSE(creature_model_bleeds(1)); // censored: only evil creatures bleed

    kfx_config_state.conf.crtr_conf.model[1].model_flags = CMF_IsEvil;
    CHECK(creature_model_bleeds(1));
    features_enabled &= ~Ft_Censorship; // restore, since this is a process-global, not part of kfx_config_state
}

TEST_CASE_METHOD(ResetSimAndConfig, "get_creature_state_type resolves through a Move state's continue_state; get_creature_gui_job maps that to a 3-way GUI job", "[kfx_sim][creature_states]") {
    struct Thing *thing = make_creature(1, 1, 0);
    thing->active_state = 1;
    kfx_config_state.conf.crtr_conf.states[1].state_type = CrStTyp_Work;
    CHECK(get_creature_state_type(thing) == CrStTyp_Work);
    CHECK(get_creature_gui_job(thing) == CrGUIJob_Working);

    thing->active_state = 1;
    thing->continue_state = 2;
    kfx_config_state.conf.crtr_conf.states[1].state_type = CrStTyp_Move;
    kfx_config_state.conf.crtr_conf.states[2].state_type = CrStTyp_FightCrtr;
    CHECK(get_creature_state_type(thing) == CrStTyp_FightCrtr); // looked through the Move state
    CHECK(get_creature_gui_job(thing) == CrGUIJob_Fighting);
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_can_hear_within_distance uses the creature's hearing config, or a fixed range for mature food", "[kfx_sim][creature_states]") {
    struct Thing *creature = make_creature(1, 1, 0);
    // creature_stats_get_from_thing() always resolves to model[0] in this test binary.
    kfx_config_state.conf.crtr_conf.model[0].hearing = 10; // 10 subtiles
    CHECK(creature_can_hear_within_distance(creature, subtile_coord(10, 0)));
    CHECK_FALSE(creature_can_hear_within_distance(creature, subtile_coord(11, 0)));

    struct Thing *food = thing_get(2);
    food->index = 2;
    food->alloc_flags = TAlF_Exists;
    food->class_id = TCls_Object;
    food->model = ObjMdl_ChickenMature;
    CHECK(creature_can_hear_within_distance(food, 2560));
    CHECK_FALSE(creature_can_hear_within_distance(food, 2561));

    struct Thing *other_object = thing_get(3);
    other_object->index = 3;
    other_object->alloc_flags = TAlF_Exists;
    other_object->class_id = TCls_Object;
    CHECK_FALSE(creature_can_hear_within_distance(other_object, 0));
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_is_hostile_towards matches by exact model or the CREATURE_ANY wildcard, via model[0]'s hostile_towards table", "[kfx_sim][creature_states]") {
    struct Thing *fightng = make_creature(1, 1, 0);
    struct Thing *enmtng = make_creature(2, 2, 1);
    enmtng->model = 7;

    CHECK_FALSE(creature_is_hostile_towards(fightng, enmtng));

    kfx_config_state.conf.crtr_conf.model[0].hostile_towards[0] = 7;
    CHECK(creature_is_hostile_towards(fightng, enmtng));

    kfx_config_state.conf.crtr_conf.model[0].hostile_towards[0] = CREATURE_ANY;
    CHECK(creature_is_hostile_towards(fightng, enmtng));
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_is_hostile_to_creature is suppressed by Call to Arms or group membership, even when hostility is configured", "[kfx_sim][creature_states]") {
    struct Thing *fightng = make_creature(1, 1, 0);
    struct Thing *enmtng = make_creature(2, 2, 1);
    enmtng->model = 7;
    kfx_config_state.conf.crtr_conf.model[0].hostile_towards[0] = CREATURE_ANY;
    CHECK(creature_is_hostile_to_creature(fightng, enmtng));

    creature_control_get(1)->called_to_arms = 1;
    CHECK_FALSE(creature_is_hostile_to_creature(fightng, enmtng)); // CTA suppresses hostility
    creature_control_get(1)->called_to_arms = 0;

    creature_control_get(1)->group_leader_idx = 9; // any positive value marks group membership
    CHECK_FALSE(creature_is_hostile_to_creature(fightng, enmtng)); // group membership suppresses hostility
}

// The per-scan hostility memo (creature_hostility_memo_begin/end_scan) must never change an
// answer: inside a scan creature_is_hostile_towards() has to equal the plain loop for every
// hostile_towards table shape and enemy model.
TEST_CASE_METHOD(ResetSimAndConfig, "creature_is_hostile_towards gives identical answers inside and outside a memoized scan", "[kfx_sim][creature_states]") {
    struct Thing *fightng = make_creature(1, 1, 0);
    struct Thing *enmtng = make_creature(2, 2, 1);
    struct CreatureModelConfig *crconf = &kfx_config_state.conf.crtr_conf.model[0];

    for (int64_t variant = 0; variant < 4; variant++) {
        std::memset(crconf->hostile_towards, 0, sizeof(crconf->hostile_towards));
        if (variant == 1) crconf->hostile_towards[0] = 7;
        if (variant == 2) crconf->hostile_towards[CREATURE_TYPES_MAX - 1] = CREATURE_ANY;
        if (variant == 3) crconf->hostile_towards[3] = 5;
        for (int64_t model = 0; model < CREATURE_TYPES_MAX; model++) {
            enmtng->model = model;
            const TbBool plain = creature_is_hostile_towards(fightng, enmtng);
            creature_hostility_memo_begin_scan();
            const TbBool first = creature_is_hostile_towards(fightng, enmtng);
            const TbBool memoized = creature_is_hostile_towards(fightng, enmtng);
            creature_hostility_memo_end_scan();
            CHECK(first == plain);
            CHECK(memoized == plain);
        }
    }
    // an all-empty table is only "hostile" to model 0 (the loop's zero == zero match)
    std::memset(crconf->hostile_towards, 0, sizeof(crconf->hostile_towards));
    enmtng->model = 0;
    CHECK(creature_is_hostile_towards(fightng, enmtng));
    enmtng->model = 9;
    CHECK_FALSE(creature_is_hostile_towards(fightng, enmtng));
}

TEST_CASE_METHOD(ResetSimAndConfig, "the hostility memo is dropped between scans so config changes are seen", "[kfx_sim][creature_states]") {
    struct Thing *fightng = make_creature(1, 1, 0);
    struct Thing *enmtng = make_creature(2, 2, 1);
    enmtng->model = 7;
    struct CreatureModelConfig *crconf = &kfx_config_state.conf.crtr_conf.model[0];
    std::memset(crconf->hostile_towards, 0, sizeof(crconf->hostile_towards));

    creature_hostility_memo_begin_scan();
    CHECK_FALSE(creature_is_hostile_towards(fightng, enmtng)); // memo: "all empty"
    creature_hostility_memo_end_scan();

    crconf->hostile_towards[0] = 7;
    creature_hostility_memo_begin_scan(); // new scan -> memo cleared
    CHECK(creature_is_hostile_towards(fightng, enmtng));
    creature_hostility_memo_end_scan();
}
