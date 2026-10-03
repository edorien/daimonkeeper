// kfx_sim "creature" cluster, per docs/refactor/testing/comprehensive/
// stage-08-comprehensive-library-passes.md §3: the largest cluster by
// file count, flagged as the strongest fan-in/risk argument for going
// first. Starting with creature_control.c's own accessors -- the same
// "index 0 reserved sentinel" pattern as thing_data.c's
// thing_is_invalid/thing_exists/thing_get (tested in thing_data_test.cpp)
// -- before anything in the creature_states_*.c state-machine files,
// which need a fuller Thing+CreatureControl pair set up first.
//
// Worth noting explicitly (found by reading the body, not assumed from
// the name): creature_control_invalid() only checks the *lower* bound
// (`cctrl <= &kfx_sim_state.cctrl_data[0]`) -- unlike thing_is_invalid(),
// it has no upper-bound check against CREATURES_COUNT. Tested as the
// function's actual behavior below, not "fixed" as part of writing a
// test for it -- the same "test what's there, not what you'd expect"
// discipline stage-04-kfx-config.md's `parameter_is_number` quirk
// followed.
#include <catch2/catch_test_macros.hpp>

#include "creature_control.h"
#include "thing_data.h"
#include "kfx_sim_state.h"
#include "config_creature.h"
#include "config_strings.h"
#include "kfx_config_state.h"

#include <cstring>

namespace {
struct ResetSimState {
    ResetSimState() { std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state)); }
};
}

TEST_CASE_METHOD(ResetSimState, "creature_control_invalid rejects null and the reserved index-0 sentinel", "[kfx_sim][creature_control]") {
    CHECK(creature_control_invalid(nullptr));
    CHECK(creature_control_invalid(creature_control_get(0)));
}

TEST_CASE_METHOD(ResetSimState, "creature_control_invalid accepts an in-range slot", "[kfx_sim][creature_control]") {
    CHECK_FALSE(creature_control_invalid(creature_control_get(1)));
}

TEST_CASE_METHOD(ResetSimState, "creature_control_invalid has no upper-bound check", "[kfx_sim][creature_control]") {
    // Documented actual behavior, not "correct" behavior: unlike
    // thing_is_invalid(), an index at or past CREATURES_COUNT is still
    // reported valid, since the function only compares against the
    // lower bound.
    CHECK_FALSE(creature_control_invalid(&kfx_sim_state.cctrl_data[CREATURES_COUNT]));
}

TEST_CASE_METHOD(ResetSimState, "creature_control_exists is false until CCFlg_Exists is set", "[kfx_sim][creature_control]") {
    struct CreatureControl *cctrl = creature_control_get(1);
    CHECK_FALSE(creature_control_exists(cctrl));

    cctrl->creature_control_flags |= CCFlg_Exists;
    CHECK(creature_control_exists(cctrl));
}

TEST_CASE_METHOD(ResetSimState, "creature_control_get_from_thing resolves via the thing's ccontrol_idx", "[kfx_sim][creature_control]") {
    struct Thing *thing = thing_get(1);
    thing->ccontrol_idx = 5;
    CHECK(creature_control_get_from_thing(thing) == creature_control_get(5));
}

TEST_CASE_METHOD(ResetSimState, "creature_control_get_from_thing returns the sentinel for ccontrol_idx 0", "[kfx_sim][creature_control]") {
    struct Thing *thing = thing_get(1);
    thing->ccontrol_idx = 0;
    CHECK(creature_control_invalid(creature_control_get_from_thing(thing)));
}

// creature_own_name moved here from kfx_config's config_creature.c
// (refactor pass 2, S05). Its CMF_OneOfKind case uses namestr_idx
// TRANSLATION_STRINGS_START so get_string() takes the self-contained
// translation-table branch (nothing loaded, so it's out of range).
namespace {
struct ResetSimAndConfigState {
    ResetSimAndConfigState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
    }
    struct Thing *make_creature() {
        struct Thing *thing = thing_get(1);
        thing->index = 1;
        thing->class_id = TCls_Creature;
        thing->ccontrol_idx = 1;
        creature_control_get(1)->index = 1;
        return thing;
    }
};
}

TEST_CASE_METHOD(ResetSimAndConfigState, "creature_own_name returns the creature's already-stored name", "[kfx_sim][creature_control]") {
    struct Thing *thing = make_creature();
    std::strcpy(creature_control_get(1)->creature_name, "Grumbeard");
    CHECK(std::strcmp(creature_own_name(thing), "Grumbeard") == 0);
}

TEST_CASE_METHOD(ResetSimAndConfigState, "creature_own_name resolves a CMF_OneOfKind creature's name via namestr_idx instead of the stored name", "[kfx_sim][creature_control]") {
    struct Thing *thing = make_creature();
    thing->model = 1;
    kfx_config_state.conf.crtr_conf.model_count = 2;
    creature_stats_get(1)->model_flags = CMF_OneOfKind;
    creature_stats_get(1)->namestr_idx = TRANSLATION_STRINGS_START;
    std::strcpy(creature_control_get(1)->creature_name, "Grumbeard");
    CHECK(std::strcmp(creature_own_name(thing), "oh_crap_invalid_string_id") == 0);
}

TEST_CASE_METHOD(ResetSimAndConfigState, "creature_own_name generates a name into the empty buffer once, seeded from the thing", "[kfx_sim][creature_control]") {
    struct Thing *thing = make_creature();
    thing->creation_turn = 1234;
    const char *name = creature_own_name(thing);
    CHECK(name == creature_control_get(1)->creature_name);
    CHECK(std::strlen(name) >= 2);
    std::string first(name);

    CHECK(creature_own_name(thing) == first); // stored now, so returned as-is

    creature_control_get(1)->creature_name[0] = '\0';
    CHECK(creature_own_name(thing) == first); // same seed, same name
}
