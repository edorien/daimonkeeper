// kfx_config: config_objects.c's load_objects_config_file() -- a fourth
// worked example of the NamedField/parse_named_field_blocks pattern.
// crate_thing_to_workshop_item_class/_model and
// get_required_room_capacity_for_object moved to kfx_sim in refactor
// pass 2 (S05); their tests are in kfx_sim's room_workshop_test.cpp and
// room_data_test.cpp.
#include <catch2/catch_test_macros.hpp>

#include "kfx_config_test_paths.h" // KFX_CONFIG_TEST_FIXTURES_DIR
#include "config_objects.h"
#include "kfx_config_state.h"

#include <cstring>

namespace {
struct ResetConfigState {
    ResetConfigState() { std::memset(&kfx_config_state, 0, sizeof(kfx_config_state)); }
};
}

TEST_CASE_METHOD(ResetConfigState, "load_objects_config_file maps each object block's Name/Genre", "[kfx_config][config_objects]") {
    REQUIRE(keeper_objects_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/objects_minimal.cfg", 0));

    CHECK(std::strcmp(get_object_model_stats(0)->code_name, "GOLD_PILE") == 0);
    CHECK(std::strcmp(get_object_model_stats(1)->code_name, "CHICKEN") == 0);
    CHECK(std::strcmp(get_object_model_stats(2)->code_name, "WORKSHOP_BOX") == 0);
    CHECK(std::strcmp(get_object_model_stats(3)->code_name, "SPELL_BOOK") == 0);
}

TEST_CASE_METHOD(ResetConfigState, "get_object_model_stats falls back to slot 0 for an out-of-range model", "[kfx_config][config_objects]") {
    REQUIRE(keeper_objects_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/objects_minimal.cfg", 0));
    CHECK(get_object_model_stats(3000) == get_object_model_stats(0)); // ThingModel is short; 3000 > OBJECT_TYPES_MAX(2000) but fits
}

TEST_CASE_METHOD(ResetConfigState, "object_code_name/object_model_id round-trip a loaded object's code name", "[kfx_config][config_objects]") {
    REQUIRE(keeper_objects_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/objects_minimal.cfg", 0));

    CHECK(std::strcmp(object_code_name(1), "CHICKEN") == 0);
    CHECK(object_model_id("CHICKEN") == 1);
    CHECK(object_model_id("NOT_A_REAL_OBJECT") == -1);
}

TEST_CASE_METHOD(ResetConfigState, "load_objects_config_file returns false for a missing file", "[kfx_config][config_objects]") {
    CHECK_FALSE(keeper_objects_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/does_not_exist.cfg", CnfLd_IgnoreErrors));
}

TEST_CASE("keeper_objects_file_data has no pre/post-load hooks", "[kfx_config][config_objects]") {
    CHECK(keeper_objects_file_data.pre_load_func == nullptr);
    CHECK(keeper_objects_file_data.post_load_func == nullptr);
    CHECK(std::strcmp(keeper_objects_file_data.filename, "objects.cfg") == 0);
}
