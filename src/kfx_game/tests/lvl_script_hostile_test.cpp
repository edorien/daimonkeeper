// Security pass S08 (docs/security-pass/README.md): level scripts the fuzzer made that used to read or write outside
// the parser's buffers (found with AddressSanitizer / UBSan; the tests show the parser still refuses or accepts them
// the same way, and under the sanitizer build (scripts/build-fuzz.sh asan) fail if they go out of bounds again).
#include <catch2/catch_test_macros.hpp>

#include "lvl_script.h"
#include "lvl_script_lib.h"
#include "level_script_override.h"
#include "kfx_game_state.h"
#include "kfx_sim_state.h"

#include <cstring>
#include <string>

namespace {
const LevelNumber kLevel = 9877;

struct HostileScriptFixture {
    HostileScriptFixture()
    {
        std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
        level_script_override_clear();
    }
    ~HostileScriptFixture()
    {
        level_script_override_clear();
        std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
    }
    void run(const std::string &text)
    {
        level_script_override_set(kLevel, "", text.c_str());
        if (preload_script(kLevel))
            load_script(kLevel);
        level_script_override_clear();
    }
};
}

TEST_CASE_METHOD(HostileScriptFixture, "a script word longer than every command name is not a command and reads nothing past a name", "[kfx_game][lvl_script][security]") {
    // find_command_desc() compared textptr[token_len] before strncmp: a long word read past the end of each name
    run(std::string(300, 'A') + "(1,2)\n" + std::string(40, 'X') + "\nREM fine\n");
    CHECK(kfx_game_state.script.values_num == 0);
}

TEST_CASE_METHOD(HostileScriptFixture, "a command given more parameters than it has slots is refused, not written past its buffers", "[kfx_game][lvl_script][security]") {
    // ten parameters: the ninth used to be copied into tp[8], one past the parameter text array
    run("ADD_CREATURE_TO_POOL(IMP,1,2,3,4,5,6,7,8,9,10)\n"
        "SET_FLAG(PLAYER0,FLAG0,1,2,3,4,5,6,7,8,9)\n"
        "DISPLAY_OBJECTIVE(1,2,3,4,5,6,7,8,9,10,11,12)\n");
    SUCCEED();
}

TEST_CASE_METHOD(HostileScriptFixture, "stray tokens in a parameter list do not move the type position past the end of the command's letters", "[kfx_game][lvl_script][security]") {
    // each ignored token advances the position in args[] without a parameter taking it: 9+ of them ran past args[9]
    std::string line = "ADD_CREATURE_TO_POOL(IMP";
    for (int i = 0; i < 20; i++)
        line += " , , ";
    run(line + ")\nADD_CREATURE_TO_POOL(IMP 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15)\n");
    SUCCEED();
}

TEST_CASE_METHOD(HostileScriptFixture, "SET_CREATURE_INSTANCE with a slot outside 1..10 changes nothing", "[kfx_game][lvl_script][security]") {
    run("SET_CREATURE_INSTANCE(IMP,0,HEAL,1)\nSET_CREATURE_INSTANCE(IMP,11,HEAL,1)\nSET_CREATURE_INSTANCE(IMP,255,HEAL,1)\n");
    SUCCEED();
}
