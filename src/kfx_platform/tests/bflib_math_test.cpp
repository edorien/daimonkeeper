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
    CHECK_THAT(LbLerp(0.0, 10.0, 0.5), WithinAbs(5.0, 0.0001));
    CHECK_THAT(LbLerp(2.0, 4.0, 0.0), WithinAbs(2.0, 0.0001));
    CHECK_THAT(LbLerp(2.0, 4.0, 1.0), WithinAbs(4.0, 0.0001));
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

// The simulation's random numbers must be identical on every platform. The state is a 32-bit LCG + rotate
// (32 bits is part of the algorithm), the range and the result are int64_t like every other integer in the game,
// so `RANDOM(11) - 5` is ordinary signed arithmetic. Golden sequence produced by the original 32-bit build.
TEST_CASE("LbRandomSeries keeps the original 32-bit sequence and returns signed 64-bit values", "[kfx_platform][bflib_math]") {
    static_assert(std::is_same<decltype(LbRandomSeries(1, (uint32_t *)nullptr, "", 0)), int64_t>::value,
        "result is int64_t");

    uint32_t seed = 1;
    const int64_t expect_seq[5] = {18, 91, 75, 84, 91};
    for (int i = 0; i < 5; i++)
        CHECK(LbRandomSeries(100, &seed, "t", 0) == expect_seq[i]);
    CHECK(seed == 3921238491u);

    // Arithmetic on the result is signed: (rnd(20) - 10) / 2 is in [-5, 4].
    seed = 3;
    CHECK((LbRandomSeries(20, &seed, "t", 0) - 10) / 2 == -5);

    // A non-positive range draws nothing and leaves the seed alone.
    seed = 7;
    CHECK(LbRandomSeries(0, &seed, "t", 0) == 0);
    CHECK(LbRandomSeries(-5, &seed, "t", 0) == 0);
    CHECK(seed == 7);
}

TEST_CASE("LbSinL covers the whole circle, its last entry included (pass 3 F1)", "[kfx_platform][bflib_math]") {
    // The table had 2047 initialisers for 2048 entries, so sin(-1/2048 turn) read 0.
    CHECK(LbSinL(2047) == -201);
    CHECK(LbSinL(-1) == -201);
    CHECK(LbSinL(0) == 0);
    CHECK(LbSinL(512) == 65536);
    CHECK(LbSinL(1536) == -65536);
    for (int64_t a = 0; a < 2048; a++)
    {
        INFO("angle " << a);
        CHECK(LbSinL(a) == -LbSinL(a + 1024));
    }
}

TEST_CASE("LbCosL is the sine a quarter turn on, and gives the values of the cosine table it replaced", "[kfx_platform][bflib_math]") {
    uint64_t h = 1469598103934665603ULL; // FNV-1a of the 2048 values, as the removed lbCosTable held them
    for (int64_t a = 0; a < 2048; a++)
    {
        CHECK(LbCosL(a) == LbSinL(a + 512));
        const int64_t v = LbCosL(a);
        const unsigned char *b = reinterpret_cast<const unsigned char *>(&v);
        for (size_t i = 0; i < sizeof(v); i++) { h ^= b[i]; h *= 1099511628211ULL; }
    }
    CHECK(h == 0x71677b2072a35db8ULL);
    CHECK(LbCosL(-512) == LbCosL(1536));
}
