// kfx_config: config_cubes.c's load_cubes_config_file() -- the first
// worked example of the NamedField/parse_named_field_blocks pattern
// (config.h), a second, older-style config-block family alongside
// value_util.c's TOML load_toml_file() -- 9 files use it
// (config_crtrstates.c/config_objects.c/config_compp.c/config_cubes.c/
// config_terrain.c/config.c/config_lenses.c/config_trapdoor.c/
// config_magic.c), and parse_named_field_blocks itself had no direct
// test coverage before this. Fixture modelled on the real
// config/fxdata/cubes.cfg's own syntax ("[cube0]", space-separated
// array values, named flag tokens).
//
// Found while reading this file, not fixed: config_cubes.h declares
// `extern struct NamedCommand cubes_desc[...]` but config_cubes.c
// defines `cube_desc` (no trailing "s") -- two different names, so
// `cubes_desc` is a dead, never-defined, never-referenced-elsewhere
// declaration. Not touched here, same restraint as other
// found-not-fixed quirks this pass. clear_cubes() had real external
// linkage but no header declaration at all; added.
//
// A second, more consequential quirk, confirmed by an actual test
// failure (not assumed from reading): config.c's set_defaults() only
// auto-populates a NamedFieldSet's `names[]` table (cube_desc here) for
// a field literally named "NAME" (uppercase, matched with strcmp) --
// config_lenses.c's Name field IS "NAME" and gets this for free, but
// config_cubes.c's is "Name" (mixed case), so cube_desc[] is NEVER
// populated by load_cubes_config_file(), and cube_code_name() (which
// reads cube_desc via get_conf_parameter_text()) always returns
// "INVALID" regardless of what was actually loaded. Currently dormant:
// cube_code_name()/cube_model_id() aren't called from anywhere outside
// this file. cube_model_id() itself is unaffected -- it scans
// cube_cfgstats[].code_name directly, not cube_desc.
#include <catch2/catch_test_macros.hpp>

#include "kfx_config_test_paths.h" // KFX_CONFIG_TEST_FIXTURES_DIR
#include "config_cubes.h"
#include "kfx_config_state.h"

#include <cstring>

namespace {
struct ResetConfigState {
    ResetConfigState() { std::memset(&kfx_config_state, 0, sizeof(kfx_config_state)); }
};
}

TEST_CASE_METHOD(ResetConfigState, "load_cubes_config_file maps a cube block's Name/Textures/Properties into kfx_config_state", "[kfx_config][config_cubes]") {
    REQUIRE(keeper_cubes_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/cubes_minimal.cfg", 0));

    struct CubeConfigStats *stat = get_cube_model_stats(0);
    CHECK(std::strcmp(stat->code_name, "CUBE_A") == 0);
    for (int64_t i = 0; i < CUBE_TEXTURES; i++) {
        CHECK(stat->texture_id[i] == i + 1);
    }
    CHECK(stat->properties_flags == (CPF_IsLava | CPF_IsWater));
}

TEST_CASE_METHOD(ResetConfigState, "get_cube_model_stats returns the slot-0 sentinel for an out-of-range model", "[kfx_config][config_cubes]") {
    CHECK(get_cube_model_stats(-1) == get_cube_model_stats(0));
    CHECK(get_cube_model_stats(CUBE_ITEMS_MAX) == get_cube_model_stats(0));
}

TEST_CASE_METHOD(ResetConfigState, "cube_model_id finds a loaded cube by code name, scanning cube_cfgstats directly", "[kfx_config][config_cubes]") {
    REQUIRE(keeper_cubes_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/cubes_minimal.cfg", 0));
    kfx_config_state.conf.cube_conf.cube_types_count = 1; // cube_model_id's own loop bound, not set by the loader itself

    CHECK(cube_model_id("CUBE_A") == 0);
    CHECK(cube_model_id("NOT_A_REAL_CUBE") == -1);
}

TEST_CASE_METHOD(ResetConfigState, "cube_code_name always returns INVALID, even for a just-loaded cube -- cube_desc is never populated (see file comment)", "[kfx_config][config_cubes]") {
    REQUIRE(keeper_cubes_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/cubes_minimal.cfg", 0));

    CHECK(std::strcmp(cube_code_name(0), "INVALID") == 0);
}

TEST_CASE_METHOD(ResetConfigState, "cube_code_name falls back to INVALID for an unnamed model", "[kfx_config][config_cubes]") {
    CHECK(std::strcmp(cube_code_name(5), "INVALID") == 0);
}

TEST_CASE_METHOD(ResetConfigState, "clear_cubes zeroes the whole cube config, including a previously loaded entry", "[kfx_config][config_cubes]") {
    REQUIRE(keeper_cubes_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/cubes_minimal.cfg", 0));
    clear_cubes();

    struct CubeConfigStats *stat = get_cube_model_stats(0);
    CHECK(stat->code_name[0] == '\0');
    CHECK(stat->texture_id[0] == 0);
}

TEST_CASE_METHOD(ResetConfigState, "load_cubes_config_file returns false for a missing file", "[kfx_config][config_cubes]") {
    CHECK_FALSE(keeper_cubes_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/does_not_exist.cfg", CnfLd_IgnoreErrors));
}

TEST_CASE("keeper_cubes_file_data has no pre/post-load hooks", "[kfx_config][config_cubes]") {
    CHECK(keeper_cubes_file_data.pre_load_func == nullptr);
    CHECK(keeper_cubes_file_data.post_load_func == nullptr);
    CHECK(std::strcmp(keeper_cubes_file_data.filename, "cubes.cfg") == 0);
}
