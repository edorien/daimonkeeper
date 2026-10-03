// kfx_config: config_compp.c's load_computer_player_config_file() -- a
// fifth worked example of the NamedField/parse_named_field_blocks
// pattern (config_cubes_test.cpp's comment lists 9 files using it).
// Unlike every other file in that list, this one also has a genuine
// pre_load_func (resolve_compp_func_type_pointers, patching the
// FUNCTIONS fields' NamedCommand pointers from
// sim_port->get_computer_*_func_type()) -- called
// explicitly below before load_func, the way the real load_config()
// orchestrator would, though every FUNCTIONS value in the fixture is
// numeric so it resolves the same with or without it (the default
// SimPort stubs return NULL tables anyway).
//
// comp_player_conf is a plain top-level extern global, not part of
// kfx_config_state -- ResetConfigState (used throughout this library's
// other tests) wouldn't touch it, so this file resets it directly.
// The [common] block specifically is parsed with parse_named_field_block
// (singular), which -- unlike parse_named_field_blocks (plural, used for
// the four indexed block families) -- never calls set_defaults(), so
// ComputerAssists/SkirmishFirst/SkirmishLast/DefaultComputerAssist are
// never reset by the loader itself; only this test's own memset resets
// them between cases.
#include <catch2/catch_test_macros.hpp>

#include "kfx_config_test_paths.h" // KFX_CONFIG_TEST_FIXTURES_DIR
#include "config_compp.h"

#include <cstring>

namespace {
struct ResetCompPlayerConf {
    ResetCompPlayerConf() { std::memset(&comp_player_conf, 0, sizeof(comp_player_conf)); }
};
}

TEST_CASE_METHOD(ResetCompPlayerConf, "load_computer_player_config_file maps the [common] block's fields", "[kfx_config][config_compp]") {
    keeper_keepcomp_file_data.pre_load_func();
    REQUIRE(keeper_keepcomp_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/keepcompp_minimal.cfg", 0));

    CHECK(comp_player_conf.computer_assist_types[0] == 1);
    CHECK(comp_player_conf.computer_assist_types[1] == 2);
    CHECK(comp_player_conf.computer_assist_types[2] == 3);
    CHECK(comp_player_conf.computer_assist_types[3] == 4);
    CHECK(comp_player_conf.skirmish_first == 1);
    CHECK(comp_player_conf.skirmish_last == 3);
    CHECK(comp_player_conf.player_assist_default == 2);
}

TEST_CASE_METHOD(ResetCompPlayerConf, "load_computer_player_config_file maps a [process1] block's Name/Mnemonic/Values/Functions/Params", "[kfx_config][config_compp]") {
    keeper_keepcomp_file_data.pre_load_func();
    REQUIRE(keeper_keepcomp_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/keepcompp_minimal.cfg", 0));

    struct ComputerProcess *proc = &comp_player_conf.process_types[1];
    CHECK(std::strcmp(proc->name, "KEEPPWR") == 0);
    CHECK(std::strcmp(proc->mnemonic, "KEEPPWR") == 0);
    CHECK(proc->priority == 5);
    CHECK(proc->process_configuration_value_2 == 10);
    CHECK(proc->process_configuration_value_3 == 20);
    CHECK(proc->process_configuration_value_4 == 30);
    CHECK(proc->process_configuration_value_5 == 40);
    CHECK(proc->func_check == 0);
    CHECK(proc->process_parameter_1 == 1);
    CHECK(proc->process_parameter_2 == 2);
    CHECK(proc->process_parameter_3 == 3);
    CHECK(proc->last_run_turn == 4);
    CHECK(proc->process_parameter_5 == 5);
    CHECK(proc->flags == 6);
}

TEST_CASE_METHOD(ResetCompPlayerConf, "load_computer_player_config_file maps a [check1] block's Name/Mnemonic/Values/Functions/Params", "[kfx_config][config_compp]") {
    keeper_keepcomp_file_data.pre_load_func();
    REQUIRE(keeper_keepcomp_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/keepcompp_minimal.cfg", 0));

    struct ComputerCheck *chk = &comp_player_conf.check_types[1];
    CHECK(std::strcmp(chk->name, "CHECKROOM") == 0);
    CHECK(std::strcmp(chk->mnemonic, "CHECKROOM") == 0);
    CHECK(chk->flags == 1);
    CHECK(chk->turns_interval == 100);
    CHECK(chk->func == 0);
    CHECK(chk->primary_parameter == 1);
    CHECK(chk->secondary_parameter == 2);
    CHECK(chk->tertiary_parameter == 3);
    CHECK(chk->last_run_turn == 4);
}

TEST_CASE_METHOD(ResetCompPlayerConf, "load_computer_player_config_file maps an [event1] block, resolving Process by an already-loaded process mnemonic", "[kfx_config][config_compp]") {
    keeper_keepcomp_file_data.pre_load_func();
    REQUIRE(keeper_keepcomp_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/keepcompp_minimal.cfg", 0));

    struct ComputerEvent *evt = &comp_player_conf.event_types[1];
    CHECK(std::strcmp(evt->name, "EVENTATTACK") == 0);
    CHECK(std::strcmp(evt->mnemonic, "EVENTATTACK") == 0);
    CHECK(evt->cetype == 1);
    CHECK(evt->mevent_kind == 2);
    CHECK(evt->test_interval == 3);
    CHECK(evt->process == 1); // resolved via value_process_mnemonic("KEEPPWR") -> process_types[1]
    CHECK(evt->primary_parameter == 1);
    CHECK(evt->secondary_parameter == 2);
    CHECK(evt->tertiary_parameter == 3);
    CHECK(evt->last_test_gameturn == 4);
}

TEST_CASE_METHOD(ResetCompPlayerConf, "load_computer_player_config_file maps [computer0]'s Name/Processes/Checks/Events by mnemonic", "[kfx_config][config_compp]") {
    keeper_keepcomp_file_data.pre_load_func();
    REQUIRE(keeper_keepcomp_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/keepcompp_minimal.cfg", 0));

    struct ComputerType *cpt = get_computer_type_template(0);
    CHECK(std::strcmp(cpt->name, "COMPUTER_EASY") == 0);
    CHECK(cpt->processes[0] == 1); // KEEPPWR -> process_types[1]
    CHECK(cpt->checks[0] == 1);    // CHECKROOM -> check_types[1]
    CHECK(cpt->events[0] == 1);    // EVENTATTACK -> event_types[1]
}

TEST_CASE_METHOD(ResetCompPlayerConf, "load_computer_player_config_file's [computer0] Values field consumes one token per declared VALUES row, in table order", "[kfx_config][config_compp]") {
    keeper_keepcomp_file_data.pre_load_func();
    REQUIRE(keeper_keepcomp_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/keepcompp_minimal.cfg", 0));

    // compp_computer_named_fields[] declares 7 "VALUES" rows (sim_before_dig
    // and drop_delay both stamped with `pos` 5, seemingly a copy/paste
    // slip), but assign_conf_command_field() doesn't consult `pos` to
    // decide consumption -- it walks the table sequentially, matching by
    // name and consuming one space-separated token per matching row
    // regardless of the declared position. So the fixture needs all 7
    // tokens for drop_delay to be reached at all; it does not receive a
    // copy of sim_before_dig's value.
    struct ComputerType *cpt = get_computer_type_template(0);
    CHECK(cpt->dig_stack_size == 10);
    CHECK(cpt->processes_time == 20);
    CHECK(cpt->click_rate == 30);
    CHECK(cpt->max_room_build_tasks == 40);
    CHECK(cpt->turn_begin == 50);
    CHECK(cpt->sim_before_dig == 60);
    CHECK(cpt->drop_delay == 70);
}

TEST_CASE_METHOD(ResetCompPlayerConf, "get_computer_type_template falls back to slot 0 for an out-of-range index", "[kfx_config][config_compp]") {
    CHECK(get_computer_type_template(-1) == get_computer_type_template(0));
    CHECK(get_computer_type_template(COMPUTER_MODELS_COUNT) == get_computer_type_template(0));
}

TEST_CASE_METHOD(ResetCompPlayerConf, "load_computer_player_config_file returns false for a missing file", "[kfx_config][config_compp]") {
    CHECK_FALSE(keeper_keepcomp_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/does_not_exist.cfg", CnfLd_IgnoreErrors));
}

TEST_CASE("keeper_keepcomp_file_data has a pre_load_func (unlike most other loaders in this library) and no post_load_func", "[kfx_config][config_compp]") {
    CHECK(keeper_keepcomp_file_data.pre_load_func != nullptr);
    CHECK(keeper_keepcomp_file_data.post_load_func == nullptr);
    CHECK(std::strcmp(keeper_keepcomp_file_data.filename, "keepcompp.cfg") == 0);
}
