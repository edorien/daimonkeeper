// Pilot test for docs/refactor/testing/stage-01-framework-and-scaffold.md:
// proves the KFX_BUILD_TESTS/Catch2 scaffold actually builds and runs,
// against genuinely pure kfx_platform functions (no extern state, no SDL --
// see stage-02-testability-and-fakes.md for the patterns needed once tests
// stop being this simple).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "bflib_math.h"

#include <cstdint>
#include <type_traits>

using Catch::Matchers::WithinAbs;

TEST_CASE("LbSqrL returns the integer square root", "[kfx_platform][bflib_math]") {
    CHECK(LbSqrL(4) == 2);
    CHECK(LbSqrL(16) == 4);
    CHECK(LbSqrL(0) == 0);
}

TEST_CASE("LbSqrL treats non-positive input as zero", "[kfx_platform][bflib_math]") {
    CHECK(LbSqrL(-1) == 0);
    CHECK(LbSqrL(0) == 0);
}

TEST_CASE("LbLerp interpolates linearly between two values", "[kfx_platform][bflib_math]") {
    CHECK_THAT(LbLerp(0.0f, 10.0f, 0.5f), WithinAbs(5.0f, 0.0001f));
    CHECK_THAT(LbLerp(2.0f, 4.0f, 0.0f), WithinAbs(2.0f, 0.0001f));
    CHECK_THAT(LbLerp(2.0f, 4.0f, 1.0f), WithinAbs(4.0f, 0.0001f));
}

// LbMathOperation is kfx_game's lvl_script_conditions.c::get_condition_status's
// entire implementation ("return LbMathOperation(opkind, left, right) != 0"),
// making this the real target behind every level script CONDITION command's
// comparison -- tested here in kfx_platform, where the function actually
// lives, rather than duplicated as a kfx_game test of a one-line wrapper.
TEST_CASE("LbMathOperation evaluates the comparison opkinds", "[kfx_platform][bflib_math]") {
    CHECK(LbMathOperation(MOp_EQUAL, 5, 5) == 1);
    CHECK(LbMathOperation(MOp_EQUAL, 5, 6) == 0);
    CHECK(LbMathOperation(MOp_NOT_EQUAL, 5, 6) == 1);
    CHECK(LbMathOperation(MOp_SMALLER, 5, 6) == 1);
    CHECK(LbMathOperation(MOp_SMALLER, 6, 5) == 0);
    CHECK(LbMathOperation(MOp_GREATER, 6, 5) == 1);
    CHECK(LbMathOperation(MOp_SMALLER_EQ, 5, 5) == 1);
    CHECK(LbMathOperation(MOp_GREATER_EQ, 5, 5) == 1);
}

TEST_CASE("LbMathOperation evaluates the logic opkinds as booleans, not raw values", "[kfx_platform][bflib_math]") {
    CHECK(LbMathOperation(MOp_LOGIC_AND, 2, 3) == 1); // both non-zero -> 1, not 2 or 3
    CHECK(LbMathOperation(MOp_LOGIC_AND, 0, 3) == 0);
    CHECK(LbMathOperation(MOp_LOGIC_OR, 0, 3) == 1);
    CHECK(LbMathOperation(MOp_LOGIC_OR, 0, 0) == 0);
    CHECK(LbMathOperation(MOp_LOGIC_XOR, 2, 0) == 1);
    CHECK(LbMathOperation(MOp_LOGIC_XOR, 2, 3) == 0);
}

TEST_CASE("LbMathOperation evaluates the bitwise and arithmetic opkinds", "[kfx_platform][bflib_math]") {
    CHECK(LbMathOperation(MOp_BITWS_AND, 6, 3) == 2);
    CHECK(LbMathOperation(MOp_BITWS_OR, 6, 1) == 7);
    CHECK(LbMathOperation(MOp_BITWS_XOR, 6, 3) == 5);
    CHECK(LbMathOperation(MOp_SUM, 5, 3) == 8);
    CHECK(LbMathOperation(MOp_SUBTRACT, 5, 3) == 2);
    CHECK(LbMathOperation(MOp_MULTIPLY, 5, 3) == 15);
    CHECK(LbMathOperation(MOp_DIVIDE, 9, 3) == 3);
    CHECK(LbMathOperation(MOp_MODULO, 10, 3) == 1);
}

TEST_CASE("LbMathOperation falls back to first_operand for an unrecognized opkind", "[kfx_platform][bflib_math]") {
    CHECK(LbMathOperation(MOp_UNDEFINED, 42, 7) == 42);
}

// The simulation's random numbers must be identical on every platform (multiplayer peers on 32-bit Windows and
// 64-bit Linux run the same game). Callers do 32-bit unsigned arithmetic on the result -- `RANDOM(11) - 5`,
// `(RANDOM(20) - 10) / 2` -- and pass ints that may be negative as the range, so range/result are uint32_t.
// The golden numbers below were produced by this same code built with `gcc -m32` (unsigned long == 32 bits).
TEST_CASE("LbRandomSeries has 32-bit unsigned semantics on every ABI", "[kfx_platform][bflib_math][lp64]") {
    static_assert(std::is_same<decltype(LbRandomSeries(1, (uint32_t *)nullptr, "", 0)), uint32_t>::value,
        "result must be 32-bit like the Windows build's unsigned long");

    uint32_t seed = 1;
    const uint32_t expect_seq[5] = {18, 91, 75, 84, 91};
    for (int i = 0; i < 5; i++)
        CHECK(LbRandomSeries(100, &seed, "t", 0) == expect_seq[i]);
    CHECK(seed == 3921238491u);

    // A negative range is a huge unsigned range (2^32 - 1e9), not a 2^64-sized one: this seed's next value
    // (3760193542) is above it and wraps.
    seed = 5;
    CHECK(LbRandomSeries((uint32_t)-1000000000L, &seed, "t", 0) == 465226246u);

    // The result wraps in 32 bits when a caller subtracts from it.
    seed = 3;
    long halved = (long)((LbRandomSeries(20, &seed, "t", 0) - 10) / 2);
    CHECK(halved == 2147483643L);

    seed = 7;
    CHECK(LbRandomSeries(0, &seed, "t", 0) == 0); // zero range: no draw, seed untouched
    CHECK(seed == 7);
}
