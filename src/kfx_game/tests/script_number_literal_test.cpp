// Script number literals must mean the same on 32-bit Windows and 64-bit Linux. strtol() saturates at
// LONG_MAX, which is 2^31-1 on the 32-bit build the script language was written for, so `MONEY > 3000000000`
// compared against 2147483647 there. With a 64-bit `long` the full 3000000000 came through and was then
// stored into 32-bit script slots, wrapping to -1294967296 (see lvl_script.h script_strtol).
#include <catch2/catch_test_macros.hpp>

#include "lvl_script.h"
#include "lvl_filesdk1.h"
#include "level_script_override.h"
#include "kfx_game_state.h"

#include <cstdint>
#include <cstring>

namespace {
const LevelNumber kLevel = 9878;

struct Fixture
{
    Fixture()
    {
        std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
        level_script_override_clear();
    }
    ~Fixture()
    {
        level_script_override_clear();
        std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
    }
};
} // namespace

TEST_CASE("script_strtol saturates to the int32 range like a 32-bit strtol", "[kfx_game][script_number][lp64]") {
    char *end = nullptr;
    CHECK(script_strtol("3000000000", &end, 0) == INT32_MAX);
    CHECK(*end == '\0');
    CHECK(script_strtol("4294967295", nullptr, 0) == INT32_MAX);
    CHECK(script_strtol("0xFFFFFFFF", nullptr, 0) == INT32_MAX);
    CHECK(script_strtol("99999999999999999999", nullptr, 0) == INT32_MAX);
    CHECK(script_strtol("-3000000000", nullptr, 0) == INT32_MIN);
    CHECK(script_strtol("2147483647", nullptr, 0) == INT32_MAX);
    CHECK(script_strtol("-2147483648", nullptr, 0) == INT32_MIN);
    CHECK(script_strtol("12345", nullptr, 0) == 12345);
    CHECK(script_strtol("-7", nullptr, 0) == -7);
    CHECK(script_strtol("0x10", nullptr, 0) == 16);
    CHECK(script_atol("2147483648") == INT32_MAX);
    CHECK(script_atol("42") == 42);
    // text after the number is left for the caller, as with strtol
    CHECK(script_strtol("12abc", &end, 0) == 12);
    CHECK(std::strcmp(end, "abc") == 0);
}

TEST_CASE_METHOD(Fixture, "a condition compared with an out-of-range literal keeps the saturated value", "[kfx_game][script_number][lp64]") {
    level_script_override_set(kLevel, "REM nothing\n",
        "LEVEL_VERSION(1)\nIF(PLAYER0,MONEY > 3000000000)\nENDIF\nIF(PLAYER0,MONEY < -3000000000)\nENDIF\n");
    REQUIRE(preload_script(kLevel));
    REQUIRE(load_script(kLevel));
    REQUIRE(kfx_game_state.script.conditions_num >= 2);
    CHECK((long)kfx_game_state.script.conditions[0].rvalue == INT32_MAX);
    CHECK((long)kfx_game_state.script.conditions[1].rvalue == INT32_MIN);
}
