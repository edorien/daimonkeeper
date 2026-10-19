// kfx_config: config_effects.c's load_effects_config_file() -- three
// independent block kinds (effect%d/effectGenerator%d/effectElement%d)
// in one file, each hand-mapped with CONDITIONAL_ASSIGN_* macros rather
// than parse_named_field_blocks (effects_effectgenerator_named_fields_set
// exists but is only used by kfx_game/kfx_script's SET_EFFECTGEN_CONFIG
// script command, not by this loader -- not attempted here, that's a
// higher-layer entry point this library doesn't call itself).
#include <catch2/catch_test_macros.hpp>

#include "kfx_config_test_paths.h" // KFX_CONFIG_TEST_FIXTURES_DIR
#include "config_effects.h"
#include "kfx_config_state.h"

#include "bflib_basics.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
struct ResetConfigState {
    ResetConfigState() { std::memset(&kfx_config_state, 0, sizeof(kfx_config_state)); }
};
}

TEST_CASE_METHOD(ResetConfigState, "load_effects_config_file maps an effect block's fields", "[kfx_config][config_effects]") {
    REQUIRE(keeper_effects_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/effects_minimal.toml", 0));

    struct EffectConfigStats *effcst = get_effect_model_stats(0);
    CHECK(std::strcmp(effcst->code_name, "EFFECT_TEST") == 0);
    CHECK(effcst->start_health == 50);
    CHECK(effcst->generation_type == 2);
    CHECK(effcst->accel_xy_min == 1);
    CHECK(effcst->accel_xy_max == 5);
    CHECK(effcst->accel_z_min == 2);
    CHECK(effcst->accel_z_max == 6);
    CHECK(effcst->kind_min == 10);
    CHECK(effcst->kind_max == 20);
    CHECK(effcst->area_affect_type == 3);
    CHECK(effcst->effect_sound == 7);
    CHECK(effcst->affected_by_wind == 1);
    CHECK(effcst->ilght.radius == 2 * 256); // scaled by CONFIG_COORD_PER_STL
    CHECK(effcst->ilght.intensity == 9);
    CHECK(effcst->ilght.flags == 1);
    CHECK(effcst->elements_count == 4);
    CHECK(effcst->always_generate == 1);
    CHECK(effcst->effect_hit_type == 1);
    CHECK(effcst->spell_effect == 3);
}

TEST_CASE_METHOD(ResetConfigState, "load_effects_config_file maps an effectGenerator block's fields", "[kfx_config][config_effects]") {
    REQUIRE(keeper_effects_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/effects_minimal.toml", 0));

    struct EffectGeneratorConfigStats *effgencst = get_effectgenerator_model_stats(0);
    CHECK(std::strcmp(effgencst->code_name, "EFFECTGEN_TEST") == 0);
    CHECK(effgencst->generation_delay_min == 10);
    CHECK(effgencst->generation_delay_max == 20);
    CHECK(effgencst->generation_amount == 3);
    CHECK(effgencst->effect_model == 0);
    CHECK(effgencst->ignore_terrain == 1);
    CHECK(effgencst->spawn_height == 256);
    CHECK(effgencst->acc_x_min == 1);
    CHECK(effgencst->acc_y_min == 2);
    CHECK(effgencst->acc_z_min == 3);
    CHECK(effgencst->acc_x_max == 4);
    CHECK(effgencst->acc_y_max == 5);
    CHECK(effgencst->acc_z_max == 6);
    CHECK(effgencst->sound_sample_idx == 11);
    CHECK(effgencst->sound_sample_rng == 2);
}

TEST_CASE_METHOD(ResetConfigState, "load_effects_config_file maps an effectElement block's fields", "[kfx_config][config_effects]") {
    REQUIRE(keeper_effects_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/effects_minimal.toml", 0));

    struct EffectElementConfigStats *effelcst = &kfx_config_state.conf.effects_conf.effectelement_cfgstats[0];
    CHECK(std::strcmp(effelcst->code_name, "EFFECTEL_TEST") == 0);
    CHECK(effelcst->draw_class == 1);
    CHECK(effelcst->move_type == 2);
    CHECK(effelcst->unanimated == 0);
    CHECK(effelcst->lifespan == 100);
    CHECK(effelcst->lifespan_random == 10);
    CHECK(effelcst->sprite_idx == 55);
    CHECK(effelcst->sprite_size_min == 1);
    CHECK(effelcst->sprite_size_max == 2);
    CHECK(effelcst->animate_once == 1);
    CHECK(effelcst->sprite_speed_min == 3);
    CHECK(effelcst->sprite_speed_max == 4);
    CHECK(effelcst->animate_on_floor == true);
    CHECK(effelcst->unshaded == true);
    CHECK(effelcst->transparent == 1);
    CHECK(effelcst->through_walls == 1);
    CHECK(effelcst->size_change == 2);
    CHECK(effelcst->fall_acceleration == 1);
    CHECK(effelcst->inertia_floor == 5);
    CHECK(effelcst->inertia_air == 6);
    CHECK(effelcst->subeffect_model == 7);
    CHECK(effelcst->subeffect_delay == 8);
    CHECK(effelcst->movable == true);
    CHECK(effelcst->impacts == false);
    CHECK(effelcst->transform_model == 9);
    CHECK(effelcst->light_radius == 3 * 256);
    CHECK(effelcst->light_intensity == 4);
    CHECK(effelcst->light_flags == 1);
    CHECK(effelcst->affected_by_wind == 1);
}

TEST_CASE_METHOD(ResetConfigState, "load_effects_config_file skips the Impacts-gated fields when Impacts is false", "[kfx_config][config_effects]") {
    REQUIRE(keeper_effects_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/effects_minimal.toml", 0));

    // Impacts=false in the fixture -- solidgnd/water/lava fields are
    // never read from the TOML, staying at ResetConfigState's zero.
    struct EffectElementConfigStats *effelcst = &kfx_config_state.conf.effects_conf.effectelement_cfgstats[0];
    CHECK(effelcst->solidgnd_effmodel == 0);
    CHECK(effelcst->water_effmodel == 0);
    CHECK(effelcst->lava_effmodel == 0);
}

TEST_CASE_METHOD(ResetConfigState, "get_effect_model_stats/get_effectgenerator_model_stats fall back to slot 0 for an out-of-range model", "[kfx_config][config_effects]") {
    REQUIRE(keeper_effects_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/effects_minimal.toml", 0));

    CHECK(get_effect_model_stats(EFFECTS_TYPES_MAX + 1) == get_effect_model_stats(0));
    CHECK(get_effectgenerator_model_stats(EFFECTSGEN_TYPES_MAX + 1) == get_effectgenerator_model_stats(0));
}

TEST_CASE_METHOD(ResetConfigState, "effect_code_name/effect_element_code_name/effectgenerator_code_name round-trip a loaded block's name, falling back to INVALID", "[kfx_config][config_effects]") {
    REQUIRE(keeper_effects_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/effects_minimal.toml", 0));

    CHECK(std::strcmp(effect_code_name(0), "EFFECT_TEST") == 0);
    CHECK(std::strcmp(effectgenerator_code_name(0), "EFFECTGEN_TEST") == 0);
    CHECK(std::strcmp(effect_element_code_name(0), "EFFECTEL_TEST") == 0);

    CHECK(std::strcmp(effect_code_name(EFFECTS_TYPES_MAX - 1), "INVALID") == 0);
    CHECK(std::strcmp(effectgenerator_code_name(EFFECTSGEN_TYPES_MAX - 1), "INVALID") == 0);
    CHECK(std::strcmp(effect_element_code_name(EFFECTSELLEMENTS_TYPES_MAX - 1), "INVALID") == 0);
}

TEST_CASE_METHOD(ResetConfigState, "effect_or_effect_element_id resolves a numeric string directly, without consulting either table", "[kfx_config][config_effects]") {
    CHECK(effect_or_effect_element_id("42") == 42);
    CHECK(effect_or_effect_element_id(nullptr) == 0);
}

TEST_CASE_METHOD(ResetConfigState, "effect_or_effect_element_id returns a positive effect id and a negated effect-element id for names found past slot 0", "[kfx_config][config_effects]") {
    REQUIRE(keeper_effects_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/effects_minimal.toml", 0));

    CHECK(effect_or_effect_element_id("EFFECT_SECOND") == 1);
    CHECK(effect_or_effect_element_id("EFFECTEL_SECOND") == -1);
    CHECK(effect_or_effect_element_id("NOT_A_REAL_EFFECT") == 0);
}

TEST_CASE_METHOD(ResetConfigState, "effect_or_effect_element_id returns 0 for a name that only matches slot 0 -- get_id's 0 return is indistinguishable from not-found here", "[kfx_config][config_effects]") {
    REQUIRE(keeper_effects_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/effects_minimal.toml", 0));

    // EFFECT_TEST is effect_desc[0]; get_id() correctly returns 0, but
    // effect_or_effect_element_id()'s `id > 0` check treats that the
    // same as "not found" and falls through to effectelem_desc, then to
    // the final `return 0` -- a real quirk, not something this test is
    // working around.
    CHECK(effect_or_effect_element_id("EFFECT_TEST") == 0);
}

TEST_CASE_METHOD(ResetConfigState, "load_effects_config_file warns about an unknown effectGenerator EffectModel name (upstream #4706)", "[kfx_config][config_effects]") {
    const char *dir = "kfx_effects_badmodel_test/";
    const std::filesystem::path full_dir = std::filesystem::current_path() / "kfx_effects_badmodel_test";
    std::filesystem::remove_all(full_dir);
    std::filesystem::create_directories(full_dir);
    REQUIRE(LbErrorLogSetup(dir, "test.log", 1) == 1);

    REQUIRE(keeper_effects_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/effects_badmodel.toml", 0));
    LbErrorLogClose();

    std::ifstream in(full_dir / "test.log");
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find("NOT_A_REAL_EFFECT") != std::string::npos);
    CHECK(text.find("EffectModel") != std::string::npos);
    CHECK(text.find("EFFECT_TEST") == std::string::npos);
    CHECK(text.find("EFFECT_SECOND") == std::string::npos);

    CHECK(get_effectgenerator_model_stats(0)->effect_model == 0);
    CHECK(get_effectgenerator_model_stats(1)->effect_model == 0);
    CHECK(get_effectgenerator_model_stats(2)->effect_model == 1);
    std::filesystem::remove_all(full_dir);
}

TEST_CASE_METHOD(ResetConfigState, "load_effects_config_file returns false for a missing file", "[kfx_config][config_effects]") {
    CHECK_FALSE(keeper_effects_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/does_not_exist.toml", CnfLd_IgnoreErrors));
}

TEST_CASE("keeper_effects_file_data has no pre/post-load hooks", "[kfx_config][config_effects]") {
    CHECK(keeper_effects_file_data.pre_load_func == nullptr);
    CHECK(keeper_effects_file_data.post_load_func == nullptr);
    CHECK(std::strcmp(keeper_effects_file_data.filename, "effects.toml") == 0);
}
