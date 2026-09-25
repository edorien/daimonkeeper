// kfx_platform: moonphase.c -- calculate_moon_phase()'s own static
// phase_of_moon has no accessor/setter, so the only branch reachable
// without a real astronomy-library call is do_calculate=0 read against
// this static's zero-initialised default (0.0), which the "new moon"
// branch (phase_of_moon < 0.025) catches deterministically. That
// assumption only holds for the very first call to this function
// anywhere in the process, so both assertions below live in one
// TEST_CASE with the do_calculate=0 read first -- splitting them into
// two separate TEST_CASEs was tried first and found order-dependent
// under --order rand (confirmed by an actual failure, not assumed):
// whichever TEST_CASE happened to run the real do_calculate=1 path
// first would leave phase_of_moon at the real current moon phase for
// the other, breaking its "still zero" assumption. A single TEST_CASE
// fixes this the same way the kfx_apploop round's timing-function tests
// were fixed -- statements within one TEST_CASE run in a fixed order
// regardless of which TEST_CASE Catch2 picks to run next.
#include <catch2/catch_test_macros.hpp>

#include "moonphase.h"

TEST_CASE("calculate_moon_phase reads the zero-initialised static as a new moon, then runs the real astronomy calculation", "[kfx_platform][moonphase]") {
    int64_t result = calculate_moon_phase(0, 0);
    CHECK(result == 0); // returns is_full_moon, which the new-moon branch clears
    CHECK(is_new_moon == 1);
    CHECK(is_full_moon == 0);
    CHECK(is_near_full_moon == 0);
    CHECK(is_near_new_moon == 0);

    // The real moon phase depends on the real current date, so the exact
    // flag can't be asserted -- this exercises the real
    // Astronomy_CurrentTime()/Astronomy_MoonPhase() call path and checks
    // its result is a well-formed single-branch selection.
    calculate_moon_phase(1, 0);
    int64_t flags_set = is_full_moon + is_near_full_moon + is_new_moon + is_near_new_moon;
    CHECK(flags_set <= 1); // the if/else-if chain guarantees at most one flag
}
