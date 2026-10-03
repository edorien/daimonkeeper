// kfx_config: config_objects.c's load_objects_config_file() -- a fourth
// worked example of the NamedField/parse_named_field_blocks pattern.
// crate_thing_to_workshop_item_class/_model and
// get_required_room_capacity_for_object moved to kfx_sim in refactor
// pass 2 (S05); their tests are in kfx_sim's room_workshop_test.cpp and
// room_data_test.cpp.
#include <catch2/catch_test_macros.hpp>

#include "kfx_config_test_paths.h" // KFX_CONFIG_TEST_FIXTURES_DIR
#include "compat_report.h"
#include "config_objects.h"
#include "kfx_config_state.h"

#include <cstdio>
#include <cstring>
#include <string>

#include <unistd.h>

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

// LavaDestroyEffect/WaterDestroyEffect (upstream #4637), whose rows upstream's
// "Compiler portability (#4944)" dropped: an object that doesn't set them keeps
// the effects liquid slabs always used (gas on lava, drips on water).
TEST_CASE_METHOD(ResetConfigState, "LavaDestroyEffect/WaterDestroyEffect load, defaulting to the classic effects", "[kfx_config][config_objects]") {
    const std::string path = "config_objects_test_" + std::to_string(getpid()) + ".cfg";
    std::FILE *f = std::fopen(path.c_str(), "wb");
    REQUIRE(f);
    std::fputs("[object0]\nName = NULL\nLavaDestroyEffect = 0\nWaterDestroyEffect = 0\n"
               "[object1]\nName = BARREL\nDestroyOnLiquid = 1\n"
               "[object2]\nName = CANDLE\nDestroyOnLiquid = 1\nLavaDestroyEffect = 12\nWaterDestroyEffect = -3\n", f);
    std::fclose(f);
    compat_report_clear();
    const bool loaded = keeper_objects_file_data.load_func(path.c_str(), 0);
    std::remove(path.c_str());
    REQUIRE(loaded);
    CHECK(compat_report_count() == 0);

    CHECK(get_object_model_stats(0)->lava_burn_effect == 0);          // explicitly none
    CHECK(get_object_model_stats(0)->water_splash_effect == 0);
    CHECK(get_object_model_stats(1)->lava_burn_effect == TngEff_HarmlessGas2); // unset: default
    CHECK(get_object_model_stats(1)->water_splash_effect == TngEff_Drip3);
    CHECK(get_object_model_stats(2)->lava_burn_effect == 12);         // an effect
    CHECK(get_object_model_stats(2)->water_splash_effect == -3);      // negative: an effect element
}
