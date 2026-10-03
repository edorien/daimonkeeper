// kfx_config: config_trapdoor.c -- accessor/lookup functions and
// create_manufacture_array_from_trapdoor_data(), all reachable via
// direct kfx_config_state field writes (pattern A) rather than the
// full trapdoor.cfg NamedField loader. door_code_name/trap_code_name
// read trap_desc/door_desc (NamedCommand tables normally auto-populated
// by the real loader's NAME field) -- populated directly here too,
// since they're plain extern arrays.
//
// is_trap_placeable/is_trap_buildable/is_door_placeable/etc. moved to
// kfx_sim's player_availability.c (refactor pass 2, S05) and are tested
// in kfx_sim/tests/player_availability_test.cpp.
#include <catch2/catch_test_macros.hpp>

#include "config_trapdoor.h"
#include "kfx_config_state.h"

#include <cstring>

namespace {
struct ResetConfigState {
    ResetConfigState() {
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        std::memset(trap_desc, 0, sizeof(trap_desc));
        std::memset(door_desc, 0, sizeof(door_desc));
    }
};
}

TEST_CASE_METHOD(ResetConfigState, "get_trap_model_stats/get_door_model_stats fall back to slot 0 for an out-of-range model", "[kfx_config][config_trapdoor]") {
    kfx_config_state.conf.trapdoor_conf.trap_types_count = 2;
    kfx_config_state.conf.trapdoor_conf.door_types_count = 2;
    CHECK(get_trap_model_stats(999) == get_trap_model_stats(0));
    CHECK(get_door_model_stats(999) == get_door_model_stats(0));
}

TEST_CASE_METHOD(ResetConfigState, "door_crate_object_model/trap_crate_object_model fall back to slot 0 for model <= 0 or out of range", "[kfx_config][config_trapdoor]") {
    kfx_config_state.conf.trapdoor_conf.door_to_object[0] = 42;
    kfx_config_state.conf.trapdoor_conf.trap_to_object[0] = 43;
    CHECK(door_crate_object_model(0) == 42);
    CHECK(door_crate_object_model(-1) == 42);
    CHECK(door_crate_object_model(TRAPDOOR_TYPES_MAX + 5) == 42);
    CHECK(trap_crate_object_model(0) == 43);
}

TEST_CASE_METHOD(ResetConfigState, "door_model_id/trap_model_id scan code_name directly, case-insensitively", "[kfx_config][config_trapdoor]") {
    kfx_config_state.conf.trapdoor_conf.door_types_count = 1;
    std::strncpy(kfx_config_state.conf.trapdoor_conf.door_cfgstats[0].code_name, "IRON_DOOR", COMMAND_WORD_LEN - 1);
    kfx_config_state.conf.trapdoor_conf.trap_types_count = 1;
    std::strncpy(kfx_config_state.conf.trapdoor_conf.trap_cfgstats[0].code_name, "ALARM_TRAP", COMMAND_WORD_LEN - 1);

    CHECK(door_model_id("iron_door") == 0);
    CHECK(door_model_id("not_a_door") == -1);
    CHECK(trap_model_id("ALARM_TRAP") == 0);
    CHECK(trap_model_id("not_a_trap") == -1);
}

TEST_CASE_METHOD(ResetConfigState, "door_code_name/trap_code_name resolve a directly-populated NamedCommand table entry", "[kfx_config][config_trapdoor]") {
    door_desc[0] = {"IRON_DOOR", 3};
    trap_desc[0] = {"ALARM_TRAP", 7};

    CHECK(std::strcmp(door_code_name(3), "IRON_DOOR") == 0);
    CHECK(std::strcmp(trap_code_name(7), "ALARM_TRAP") == 0);
    CHECK(std::strcmp(door_code_name(999), "INVALID") == 0);
    CHECK(std::strcmp(trap_code_name(999), "INVALID") == 0);
}

TEST_CASE_METHOD(ResetConfigState, "get_manufacture_data falls back to slot 0 for a negative or out-of-range index", "[kfx_config][config_trapdoor]") {
    kfx_config_state.conf.trapdoor_conf.manufacture_types_count = 2;
    CHECK(get_manufacture_data(-1) == get_manufacture_data(0));
    CHECK(get_manufacture_data(999) == get_manufacture_data(0));
}

TEST_CASE_METHOD(ResetConfigState, "get_manufacture_data_index_for_thing finds a matching class+model, skipping index 0", "[kfx_config][config_trapdoor]") {
    kfx_config_state.conf.trapdoor_conf.manufacture_types_count = 3;
    kfx_config_state.conf.trapdoor_conf.manufacture_data[1].tngclass = TCls_Trap;
    kfx_config_state.conf.trapdoor_conf.manufacture_data[1].tngmodel = 5;
    kfx_config_state.conf.trapdoor_conf.manufacture_data[2].tngclass = TCls_Door;
    kfx_config_state.conf.trapdoor_conf.manufacture_data[2].tngmodel = 9;

    CHECK(get_manufacture_data_index_for_thing(TCls_Trap, 5) == 1);
    CHECK(get_manufacture_data_index_for_thing(TCls_Door, 9) == 2);
    CHECK(get_manufacture_data_index_for_thing(TCls_Trap, 999) == 0); // not found
}

TEST_CASE_METHOD(ResetConfigState, "create_manufacture_array_from_trapdoor_data builds one manufacture entry per real trap/door, skipping index 0", "[kfx_config][config_trapdoor]") {
    kfx_config_state.conf.trapdoor_conf.trap_types_count = 2; // index 0 is the "no trap" sentinel, only index 1 is real
    kfx_config_state.conf.trapdoor_conf.trap_cfgstats[1].tooltip_stridx = 111;
    kfx_config_state.conf.trapdoor_conf.trap_cfgstats[1].bigsym_sprite_idx = 222;

    kfx_config_state.conf.trapdoor_conf.door_types_count = 2;
    kfx_config_state.conf.trapdoor_conf.door_cfgstats[1].tooltip_stridx = 333;
    kfx_config_state.conf.trapdoor_conf.door_cfgstats[1].bigsym_sprite_idx = 444;

    CHECK(create_manufacture_array_from_trapdoor_data());

    // manufacture[0] is always the empty sentinel.
    CHECK(kfx_config_state.conf.trapdoor_conf.manufacture_data[0].tngclass == TCls_Empty);

    // manufacture[1] is the one real trap.
    struct ManufactureData *trap_entry = &kfx_config_state.conf.trapdoor_conf.manufacture_data[1];
    CHECK(trap_entry->tngclass == TCls_Trap);
    CHECK(trap_entry->tngmodel == 1);
    CHECK(trap_entry->tooltip_stridx == 111);
    CHECK(trap_entry->bigsym_sprite_idx == 222);

    // manufacture[2] is the one real door.
    struct ManufactureData *door_entry = &kfx_config_state.conf.trapdoor_conf.manufacture_data[2];
    CHECK(door_entry->tngclass == TCls_Door);
    CHECK(door_entry->tngmodel == 1);
    CHECK(door_entry->tooltip_stridx == 333);
    CHECK(door_entry->bigsym_sprite_idx == 444);

    CHECK(kfx_config_state.conf.trapdoor_conf.manufacture_types_count == 3); // sentinel + 1 trap + 1 door
}

TEST_CASE("keeper_trapdoor_file_data has a post_load_func but no pre_load_func", "[kfx_config][config_trapdoor]") {
    CHECK(keeper_trapdoor_file_data.pre_load_func == nullptr);
    CHECK(keeper_trapdoor_file_data.post_load_func != nullptr); // create_manufacture_array_from_trapdoor_data
    CHECK(std::strcmp(keeper_trapdoor_file_data.filename, "trapdoor.cfg") == 0);
}
