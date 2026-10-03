// kfx_sim "creature" cluster: creature_states_mood.c's anger/mood
// bookkeeping cluster -- the second individual creature_states_*.c file
// to get direct coverage (after creature_states_tresr.c), and the first
// to combine both established fixture pieces at once: the model set on
// the thing for creature_stats_get_from_thing() (same as
// creature_states_tresr_test.cpp) plus creature_control_test.cpp's
// thing_get(1)/ccontrol_idx wiring for creature_control_get_from_thing().
// Picked because its anger_* accessors/mutators are pure Thing+
// CreatureControl+CreatureModelConfig computation -- no Room/dungeon
// spatial lookups involved -- unlike the at_*_room()/*ing() state-machine
// entry points in this and every other creature_states_*.c file, which
// all need a fuller Room+job-system fixture not attempted here (see
// docs/Architecture/testing-harness.md §10).
//
// anger_set_creature_anger_f() and everything that calls into it
// (anger_increase/reduce_creature_anger_f, anger_apply_anger_to_creature_
// all_types_f, anger_make_creature_angry, anger_give_creatures_annoyance_
// percentage) additionally touch get_players_num_dungeon()/dungeon state
// and the event system on the angry/no-longer-angry transition edges --
// out of scope here for the same reason. What's covered below is the
// anger_free_for_anger_increase/decrease guards, the angry/livid
// classification (anger_calculate_creature_is_angry, anger_is_creature_
// livid/angry, anger_get_creature_anger_type), and creature_can_get_angry
// itself.
#include <catch2/catch_test_macros.hpp>

#include "creature_states_mood.h"
#include "creature_control.h"
#include "thing_data.h"
#include "config_creature.h"
#include "config_magic.h" // CSAfF_MadKilling
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "thing_stats.h"

#include <cstring>

namespace {
struct MoodFixture {
    struct Thing* thing;
    struct CreatureControl* cctrl;

    MoodFixture() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        kfx_config_state.conf.crtr_conf.model_count = CREATURE_TYPES_MAX;

        thing = thing_get(1);
        thing->model = 1;
        thing->ccontrol_idx = 1;
        cctrl = creature_control_get(1);
    }
};
}

TEST_CASE_METHOD(MoodFixture, "creature_can_get_angry is true for a non-neutral creature whose model has a nonzero annoy_level", "[kfx_sim][creature_states_mood]") {
    thing->owner = 0;
    kfx_config_state.neutral_player_num = 5;
    kfx_config_state.conf.crtr_conf.model[1].annoy_level = 10;
    CHECK(creature_can_get_angry(thing));
}

TEST_CASE_METHOD(MoodFixture, "creature_can_get_angry is false for a neutral-owned thing regardless of annoy_level", "[kfx_sim][creature_states_mood]") {
    kfx_config_state.neutral_player_num = 5;
    thing->owner = 5;
    kfx_config_state.conf.crtr_conf.model[1].annoy_level = 10;
    CHECK_FALSE(creature_can_get_angry(thing));
}

TEST_CASE_METHOD(MoodFixture, "creature_can_get_angry is false when the model's annoy_level is zero", "[kfx_sim][creature_states_mood]") {
    thing->owner = 0;
    kfx_config_state.neutral_player_num = 5;
    kfx_config_state.conf.crtr_conf.model[1].annoy_level = 0;
    CHECK_FALSE(creature_can_get_angry(thing));
}

TEST_CASE_METHOD(MoodFixture, "anger_free_for_anger_increase is true with no combat flags and no active call-to-arms", "[kfx_sim][creature_states_mood]") {
    cctrl->combat_flags = 0;
    cctrl->called_to_arms = 0;
    CHECK(anger_free_for_anger_increase(thing));
}

TEST_CASE_METHOD(MoodFixture, "anger_free_for_anger_increase is false while in combat", "[kfx_sim][creature_states_mood]") {
    cctrl->combat_flags = 1;
    CHECK_FALSE(anger_free_for_anger_increase(thing));
}

TEST_CASE_METHOD(MoodFixture, "anger_free_for_anger_increase is false while following a call to arms", "[kfx_sim][creature_states_mood]") {
    cctrl->combat_flags = 0;
    cctrl->called_to_arms = 1;
    CHECK_FALSE(anger_free_for_anger_increase(thing));
}

TEST_CASE_METHOD(MoodFixture, "anger_free_for_anger_decrease is false while mad-killing, true otherwise", "[kfx_sim][creature_states_mood]") {
    cctrl->spell_flags = 0;
    CHECK(anger_free_for_anger_decrease(thing));
    cctrl->spell_flags = CSAfF_MadKilling;
    CHECK_FALSE(anger_free_for_anger_decrease(thing));
}

TEST_CASE_METHOD(MoodFixture, "anger_is_creature_angry/livid read mood_flags directly", "[kfx_sim][creature_states_mood]") {
    cctrl->mood_flags = 0;
    CHECK_FALSE(anger_is_creature_angry(thing));
    CHECK_FALSE(anger_is_creature_livid(thing));

    cctrl->mood_flags = CCMoo_Angry;
    CHECK(anger_is_creature_angry(thing));
    CHECK_FALSE(anger_is_creature_livid(thing));

    cctrl->mood_flags = CCMoo_Angry | CCMoo_Livid;
    CHECK(anger_is_creature_angry(thing));
    CHECK(anger_is_creature_livid(thing));
}

TEST_CASE_METHOD(MoodFixture, "anger_is_creature_angry/livid are false for an invalid creature control", "[kfx_sim][creature_states_mood]") {
    thing->ccontrol_idx = 0; // resolves to the reserved index-0 sentinel
    CHECK_FALSE(anger_is_creature_angry(thing));
    CHECK_FALSE(anger_is_creature_livid(thing));
}

TEST_CASE_METHOD(MoodFixture, "anger_calculate_creature_is_angry sets Angry once any reason's level reaches the model's annoy_level", "[kfx_sim][creature_states_mood]") {
    kfx_config_state.conf.crtr_conf.model[1].annoy_level = 100;
    std::memset(cctrl->annoyance_level, 0, sizeof(cctrl->annoyance_level));
    cctrl->annoyance_level[AngR_Hungry] = 100;
    anger_calculate_creature_is_angry(thing);
    CHECK((cctrl->mood_flags & CCMoo_Angry) != 0);
    CHECK((cctrl->mood_flags & CCMoo_Livid) == 0);
}

TEST_CASE_METHOD(MoodFixture, "anger_calculate_creature_is_angry sets Livid once a reason's level reaches double the model's annoy_level", "[kfx_sim][creature_states_mood]") {
    kfx_config_state.conf.crtr_conf.model[1].annoy_level = 100;
    std::memset(cctrl->annoyance_level, 0, sizeof(cctrl->annoyance_level));
    cctrl->annoyance_level[AngR_Hungry] = 200;
    anger_calculate_creature_is_angry(thing);
    CHECK((cctrl->mood_flags & CCMoo_Angry) != 0);
    CHECK((cctrl->mood_flags & CCMoo_Livid) != 0);
}

TEST_CASE_METHOD(MoodFixture, "anger_calculate_creature_is_angry clears both flags when no reason reaches the threshold", "[kfx_sim][creature_states_mood]") {
    kfx_config_state.conf.crtr_conf.model[1].annoy_level = 100;
    cctrl->mood_flags = CCMoo_Angry | CCMoo_Livid; // stale from a prior turn
    std::memset(cctrl->annoyance_level, 0, sizeof(cctrl->annoyance_level));
    cctrl->annoyance_level[AngR_Hungry] = 50;
    anger_calculate_creature_is_angry(thing);
    CHECK((cctrl->mood_flags & CCMoo_Angry) == 0);
    CHECK((cctrl->mood_flags & CCMoo_Livid) == 0);
}

TEST_CASE_METHOD(MoodFixture, "anger_get_creature_anger_type is AngR_None when the model can't get angry at all", "[kfx_sim][creature_states_mood]") {
    kfx_config_state.conf.crtr_conf.model[1].annoy_level = 0;
    cctrl->mood_flags = CCMoo_Angry;
    cctrl->annoyance_level[AngR_Hungry] = 999;
    CHECK(anger_get_creature_anger_type(thing) == AngR_None);
}

TEST_CASE_METHOD(MoodFixture, "anger_get_creature_anger_type is AngR_None when the Angry flag isn't set", "[kfx_sim][creature_states_mood]") {
    kfx_config_state.conf.crtr_conf.model[1].annoy_level = 100;
    cctrl->mood_flags = 0;
    cctrl->annoyance_level[AngR_Hungry] = 999;
    CHECK(anger_get_creature_anger_type(thing) == AngR_None);
}

TEST_CASE_METHOD(MoodFixture, "anger_get_creature_anger_type returns the reason with the highest annoyance_level once Angry is set", "[kfx_sim][creature_states_mood]") {
    kfx_config_state.conf.crtr_conf.model[1].annoy_level = 100;
    cctrl->mood_flags = CCMoo_Angry;
    std::memset(cctrl->annoyance_level, 0, sizeof(cctrl->annoyance_level));
    cctrl->annoyance_level[AngR_NotPaid] = 150;
    cctrl->annoyance_level[AngR_Hungry] = 300; // the highest
    cctrl->annoyance_level[AngR_NoLair] = 200;
    CHECK(anger_get_creature_anger_type(thing) == AngR_Hungry);
}
