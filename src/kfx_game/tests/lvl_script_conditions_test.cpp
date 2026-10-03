// kfx_game: lvl_script_conditions.c, per docs/refactor/testing/
// stage-08-comprehensive-library-passes.md's kfx_game row.
// condition_inactive() is a bounds-checked bitflag read over
// kfx_game_state.script.conditions[] -- pattern A, same shape as
// game_legacy_test.cpp's kfx_game_state fixture.
//
// get_condition_status() is deliberately NOT retested here: its entire
// body is "return LbMathOperation(opkind, left, right) != 0", so the
// real comparison logic is tested directly in kfx_platform's
// bflib_math_test.cpp, where LbMathOperation actually lives, rather than
// duplicated as a test of a one-line wrapper.
#include <catch2/catch_test_macros.hpp>

#include "lvl_script.h"
#include "lvl_script_conditions.h"
#include "kfx_game_state.h"

#include <cstring>

namespace {
struct ResetGameState {
    ResetGameState() { std::memset(&kfx_game_state, 0, sizeof(kfx_game_state)); }
};
}

TEST_CASE_METHOD(ResetGameState, "condition_inactive is true when the 0x01 status bit is unset", "[kfx_game][lvl_script_conditions]") {
    kfx_game_state.script.conditions[0].status = 0x00;
    CHECK(condition_inactive(0));
}

TEST_CASE_METHOD(ResetGameState, "condition_inactive is true when the 0x04 status bit is set, even alongside 0x01", "[kfx_game][lvl_script_conditions]") {
    kfx_game_state.script.conditions[0].status = 0x01 | 0x04;
    CHECK(condition_inactive(0));
}

TEST_CASE_METHOD(ResetGameState, "condition_inactive is false when 0x01 is set and 0x04 is clear", "[kfx_game][lvl_script_conditions]") {
    kfx_game_state.script.conditions[0].status = 0x01;
    CHECK_FALSE(condition_inactive(0));
}

TEST_CASE("condition_inactive returns false (not inactive) for an out-of-range index", "[kfx_game][lvl_script_conditions]") {
    // Counterintuitive on a first read -- an out-of-bounds index reports
    // "not inactive" rather than "inactive", the opposite of what a
    // fail-safe bounds check would usually return. Tested as the
    // function's actual behavior, not silently corrected, same "test
    // what's there" call stage-04-kfx-config.md made for
    // parameter_is_number's lone-"-" quirk.
    CHECK_FALSE(condition_inactive(-1));
    CHECK_FALSE(condition_inactive(CONDITIONS_COUNT));
}

// get_script_current_condition/set_script_current_condition/pop_condition
// all read/write module-private statics (script_current_condition/
// condition_stack/condition_stack_pos), none exposed through any
// accessor except these functions themselves (and reset_script_conditions()). condition_stack_pos
// in particular has no setter at all -- only command_add_condition()
// (real script-parsing state, not attempted here) ever pushes onto it --
// so the only pop_condition() branch reachable from a test is the
// "stack empty" one, relying on condition_stack_pos being left at its
// zero-initialized default (nothing else in this test binary touches
// it).
TEST_CASE("set_script_current_condition/get_script_current_condition round-trip", "[kfx_game][lvl_script_conditions]") {
    set_script_current_condition(7);
    CHECK(get_script_current_condition() == 7);
}

TEST_CASE("pop_condition on an already-ENDIF'd script logs an error and returns -1 without changing state", "[kfx_game][lvl_script_conditions]") {
    set_script_current_condition(CONDITION_ALWAYS);
    CHECK(pop_condition() == -1);
    CHECK(get_script_current_condition() == CONDITION_ALWAYS);
}

TEST_CASE("pop_condition with an empty condition_stack resets to CONDITION_ALWAYS", "[kfx_game][lvl_script_conditions]") {
    set_script_current_condition(3); // anything other than CONDITION_ALWAYS
    CHECK(pop_condition() == CONDITION_ALWAYS);
    CHECK(get_script_current_condition() == CONDITION_ALWAYS);
}

TEST_CASE("a script left with open IFs doesn't leak them into the next one (P4-F14)", "[kfx_game][lvl_script_conditions]") {
    // The condition stack was never emptied: a script missing ENDIFs left its outer IF on it, and the next
    // script's first ENDIF went back to that IF instead of to the top level.
    std::memset(&kfx_game_state.script, 0, sizeof(kfx_game_state.script));
    reset_script_conditions();
    command_add_condition(0, 0, 0, 0, 0); // IF
    command_add_condition(0, 0, 0, 0, 0); // a nested IF: the first is pushed
    reset_script_conditions();            // the script ends without its ENDIFs
    CHECK(get_script_current_condition() == CONDITION_ALWAYS);
    command_add_condition(0, 0, 0, 0, 0); // the next script's IF ...
    CHECK(pop_condition() == CONDITION_ALWAYS); // ... and its ENDIF
    std::memset(&kfx_game_state.script, 0, sizeof(kfx_game_state.script));
}
