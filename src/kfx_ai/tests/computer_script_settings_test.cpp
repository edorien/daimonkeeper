// kfx_ai: the computer player settings SET_COMPUTER_GLOBALS / _PROCESS / _CHECKS and
// their Lua twins change (refactor pass 3, S07 C: one worker each, player_computer.c).
#include <catch2/catch_test_macros.hpp>

#include "player_computer.h"
#include "kfx_ai_test_fixtures.h"

#include <cstring>

using namespace kfx_test;

namespace {
void name_process(PlayerNumber plyr, int64_t k, const char *name) {
    std::strncpy(kfx_sim_state.computer[plyr].processes[k].name, name, COMMAND_WORD_LEN - 1);
}
void name_check(PlayerNumber plyr, int64_t k, const char *name) {
    std::strncpy(kfx_sim_state.computer[plyr].checks[k].name, name, COMMAND_WORD_LEN - 1);
}
}

TEST_CASE_METHOD(ResetSimAndConfig, "computer_set_globals sets every computer of the range, keeping task_delay for -1", "[kfx_ai][computer_script]") {
    kfx_sim_state.computer[1].task_delay = 9;
    computer_set_globals(0, 2, 1, 2, 3, 4, 5, 6, -1);
    for (PlayerNumber p : {0, 1}) {
        const struct Computer2 *comp = &kfx_sim_state.computer[p];
        CHECK(comp->dig_stack_size == 1);
        CHECK(comp->processes_time == 2);
        CHECK(comp->click_rate == 3);
        CHECK(comp->max_room_build_tasks == 4);
        CHECK(comp->turn_begin == 5);
        CHECK(comp->sim_before_dig == 6);
    }
    CHECK(kfx_sim_state.computer[1].task_delay == 9);
    CHECK(kfx_sim_state.computer[2].dig_stack_size == 0);
    computer_set_globals(1, 2, 0, 0, 0, 0, 0, 0, 12);
    CHECK(kfx_sim_state.computer[1].task_delay == 12);
}

TEST_CASE_METHOD(ResetSimAndConfig, "computer_set_process_config changes the named processes up to the list end", "[kfx_ai][computer_script]") {
    name_process(0, 0, "BUILD_ROOM");
    name_process(0, 1, "DIG_TO_GOLD");
    kfx_sim_state.computer[0].processes[2].flags = ComProc_ListEnd;
    name_process(0, 3, "dig_to_gold"); // past the end: untouched
    CHECK(computer_set_process_config(0, 1, "dig_to_gold", 7, 8, 9, 10, 11, false) == 1);
    const struct ComputerProcess *p = &kfx_sim_state.computer[0].processes[1];
    CHECK(p->priority == 7);
    CHECK(p->process_configuration_value_2 == 8);
    CHECK(p->process_configuration_value_5 == 11);
    CHECK(kfx_sim_state.computer[0].processes[3].priority == 0);
    CHECK(computer_set_process_config(0, 1, "NO_SUCH", 1, 1, 1, 1, 1, false) == 0);
}

TEST_CASE_METHOD(ResetSimAndConfig, "computer_set_check_config: the script stops at an unnamed check, Lua doesn't", "[kfx_ai][computer_script]") {
    name_check(0, 0, "CHECK_MONEY");
    // checks[1] unnamed, checks[2] named after it
    name_check(0, 2, "CHECK_EXPAND");
    kfx_sim_state.computer[0].checks[3].flags = ComChk_Unkn0002;
    CHECK(computer_set_check_config(0, 1, "check_expand", 5, 6, 7, 8, 9, true, false) == 0);
    CHECK(computer_set_check_config(0, 1, "check_expand", 5, 6, 7, 8, 9, false, false) == 1);
    const struct ComputerCheck *c = &kfx_sim_state.computer[0].checks[2];
    CHECK(c->turns_interval == 5);
    CHECK(c->primary_parameter == 6);
    CHECK(c->last_run_turn == 9);
    CHECK(computer_set_check_config(0, 1, "CHECK_MONEY", 1, 2, 3, 4, 5, true, false) == 1);
}
