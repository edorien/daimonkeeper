// kfx_config: config_textures.c's load_textureanim_config_file() -- the
// smallest of the still-untested per-config_*.c TOML loaders (14 lines),
// picked as a worked example of the "per-loader schema" gap
// docs/Architecture/testing-harness.md §10 flags: value_util.c's
// load_toml_file() (the shared mechanism) already has direct coverage
// (value_util_test.cpp), but no individual config_*.c loader's own
// field-mapping had a test until this one. load_textureanim_config_file
// itself is `static` -- reached here through the public
// keeper_textureanim_file_data.load_func field, the same ConfigFileData
// interface every config_*.c loader in this library implements
// (config.h's load_config() is the generic orchestrator that resolves
// real game file paths and calls this same field; not attempted here,
// this test calls load_func directly with a fixture path instead).
//
// Every one of the other 456 texture blocks (TEXTURE_BLOCKS_ANIM_COUNT)
// is deliberately absent from the fixture -- value_dict_get() returns
// NULL for a missing key, and the loop's `if (value_type(section) ==
// VALUE_DICT)` guard just skips those slots, leaving kfx_config_state's
// pre-existing (memset-reset) zero values in place for anything the
// fixture doesn't mention.
#include <catch2/catch_test_macros.hpp>

#include "kfx_config_test_paths.h" // KFX_CONFIG_TEST_FIXTURES_DIR
#include "config_textures.h"
#include "kfx_config_state.h"

#include <cstring>

namespace {
struct ResetConfigState {
    ResetConfigState() { std::memset(&kfx_config_state, 0, sizeof(kfx_config_state)); }
};
}

TEST_CASE_METHOD(ResetConfigState, "load_textureanim_config_file maps a texture block's frames array into kfx_config_state.texture_animation", "[kfx_config][config_textures]") {
    REQUIRE(keeper_textureanim_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/textureanim_minimal.toml", 0));

    // tex_no 0 ("texture544") occupies frame slots [0..7].
    for (int64_t frame = 0; frame < 8; frame++) {
        CHECK(kfx_config_state.texture_animation[frame] == 10 + frame);
    }
}

TEST_CASE_METHOD(ResetConfigState, "load_textureanim_config_file leaves unconfigured texture blocks untouched", "[kfx_config][config_textures]") {
    REQUIRE(keeper_textureanim_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/textureanim_minimal.toml", 0));

    // tex_no 1 ("texture545") isn't in the fixture -- its 8 frame slots
    // (indices 8..15) should stay at their reset value.
    for (int64_t frame = 8; frame < 16; frame++) {
        CHECK(kfx_config_state.texture_animation[frame] == 0);
    }
}

TEST_CASE_METHOD(ResetConfigState, "load_textureanim_config_file returns false for a missing file", "[kfx_config][config_textures]") {
    CHECK_FALSE(keeper_textureanim_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/does_not_exist.toml", CnfLd_IgnoreErrors));
}

TEST_CASE("keeper_textureanim_file_data has no pre/post-load hooks", "[kfx_config][config_textures]") {
    CHECK(keeper_textureanim_file_data.pre_load_func == nullptr);
    CHECK(keeper_textureanim_file_data.post_load_func == nullptr);
    CHECK(std::strcmp(keeper_textureanim_file_data.filename, "textureanim.toml") == 0);
}
