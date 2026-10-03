// kfx_config: config_crtrstates.c's load_creaturestates_config_file()
// -- a third worked example of the NamedField/parse_named_field_blocks
// pattern. value_overrides() is a genuinely self-contained pure
// space-separated-token parser (OVERRIDES field), independently
// exercisable through the loaded state's override_* bitfields.
// PROCESSFUNCTION/CLEANUPFUNCTION/MOVEFROMSLABFUNCTION/MOVECHECKFUNCTION
// need SimPort's real function-command NamedCommand
// tables (patched in by this file's own pre_load_func,
// resolve_crstates_func_commands_pointers) and SPRITEIDX needs a real
// sprite lookup -- none of the four/one attempted here, the fixture
// omits them entirely.
#include <catch2/catch_test_macros.hpp>

#include "kfx_config_test_paths.h" // KFX_CONFIG_TEST_FIXTURES_DIR
#include "config_crtrstates.h"
#include "config_creature.h"
#include "kfx_config_state.h"

#include <cstring>

namespace {
struct ResetConfigState {
    ResetConfigState() { std::memset(&kfx_config_state, 0, sizeof(kfx_config_state)); }
};
}

TEST_CASE_METHOD(ResetConfigState, "load_creaturestates_config_file maps a state block's Name/Captive/Transition/FollowBehavior/StateType/BlocksAllStateChanges", "[kfx_config][config_crtrstates]") {
    REQUIRE(creature_states_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/crstates_minimal.cfg", 0));

    struct CreatureStateConfig *state = &kfx_config_state.conf.crtr_conf.states[0];
    CHECK(std::strcmp(state->name, "STATE_A") == 0);
    CHECK(state->captive == 1);
    CHECK(state->transition == 1);
    CHECK(state->follow_behavior == 1); // FlwB_FollowLeader -- a #define local to config_crtrstates.c, not exposed via any header
    CHECK(state->state_type == CrStTyp_Work);
    CHECK(state->blocks_all_state_changes == 1);
}

TEST_CASE_METHOD(ResetConfigState, "load_creaturestates_config_file's OVERRIDES field sets exactly the named override bits, ignoring an unknown token", "[kfx_config][config_crtrstates]") {
    REQUIRE(creature_states_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/crstates_minimal.cfg", 0));

    struct CreatureStateConfig *state = &kfx_config_state.conf.crtr_conf.states[0];
    CHECK(state->override_feed == 1);
    CHECK(state->override_sleep == 1);
    // Every other override_* bit must stay clear -- both because they
    // weren't named, and because value_overrides() unconditionally
    // clears all of them before parsing tokens (so a re-load can't leak
    // a previous load's overrides forward).
    CHECK(state->override_own_needs == 0);
    CHECK(state->override_fight_crtr == 0);
    CHECK(state->override_call2arms == 0);
}

TEST_CASE_METHOD(ResetConfigState, "creature_state_code_name falls back to INVALID for an unnamed state", "[kfx_config][config_crtrstates]") {
    CHECK(std::strcmp(creature_state_code_name(5), "INVALID") == 0);
}

TEST_CASE_METHOD(ResetConfigState, "creature_state_code_name resolves a loaded state's real name", "[kfx_config][config_crtrstates]") {
    REQUIRE(creature_states_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/crstates_minimal.cfg", 0));

    // Unlike config_cubes.c's cube_desc (broken because its Name field
    // is spelled "Name", not "NAME"), this file's NAME field really is
    // uppercase, so config.c's set_defaults() case-sensitive match
    // auto-registers creatrstate_desc correctly.
    CHECK(std::strcmp(creature_state_code_name(0), "STATE_A") == 0);
}

TEST_CASE_METHOD(ResetConfigState, "load_creaturestates_config_file returns false for a missing file", "[kfx_config][config_crtrstates]") {
    CHECK_FALSE(creature_states_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/does_not_exist.cfg", CnfLd_IgnoreErrors));
}

TEST_CASE("creature_states_file_data has a pre_load_func but no post_load_func", "[kfx_config][config_crtrstates]") {
    CHECK(creature_states_file_data.pre_load_func != nullptr); // resolve_crstates_func_commands_pointers
    CHECK(creature_states_file_data.post_load_func == nullptr);
    CHECK(std::strcmp(creature_states_file_data.filename, "crstates.cfg") == 0);
}
