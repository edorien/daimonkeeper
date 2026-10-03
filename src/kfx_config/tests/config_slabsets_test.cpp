// kfx_config: config_slabsets.c -- two independent loaders in one file.
//
// load_columns_config_file() writes directly into
// kfx_config_state.conf.column_conf, no SimPort entries needed
// -- the easy half.
//
// load_slabset_config_file()/clear_slabsets() are different: they
// reach into simport_get_slabset_array()/
// get_slabobjs_array()/get_slabobjs_idx_array()/get_slabset_num_ptr()/
// get_slabobjs_num_ptr(), all of which default to returning NULL
// (ports/sim_port.def) -- calling either function against
// the default table would dereference a null pointer. A real fake with
// real backing storage is required here, not just "the safe default"
// the way config_objects.c's crate_thing_to_workshop_item_* functions
// were -- built below, the same shape as ariadne_test.cpp's
// PathfindingWorldPort fake but for SimPort.
#include <catch2/catch_test_macros.hpp>

#include "kfx_config_test_paths.h" // KFX_CONFIG_TEST_FIXTURES_DIR
#include "config_slabsets.h"
#include "kfx_config_state.h"
#include "ports/sim_port.h"

#include <cstring>

namespace {
struct ResetConfigState {
    ResetConfigState() { std::memset(&kfx_config_state, 0, sizeof(kfx_config_state)); }
};

struct SlabSet g_fake_slabset_arr[SLABSET_COUNT];
struct SlabObj g_fake_slabobjs_arr[SLABOBJS_COUNT];
int64_t g_fake_slabobjs_idx_arr[SLABSET_COUNT];
int64_t g_fake_slabset_num = 0;
int64_t g_fake_slabobjs_num = 0;

struct SlabSet *fake_get_slabset_array(void) { return g_fake_slabset_arr; }
struct SlabObj *fake_get_slabobjs_array(void) { return g_fake_slabobjs_arr; }
int64_t *fake_get_slabobjs_idx_array(void) { return g_fake_slabobjs_idx_arr; }
int64_t *fake_get_slabset_num_ptr(void) { return &g_fake_slabset_num; }
int64_t *fake_get_slabobjs_num_ptr(void) { return &g_fake_slabobjs_num; }

struct SlabsetFixture : ResetConfigState {
    struct SimPort fake;
    SlabsetFixture() {
        std::memset(g_fake_slabset_arr, 0, sizeof(g_fake_slabset_arr));
        std::memset(g_fake_slabobjs_arr, 0, sizeof(g_fake_slabobjs_arr));
        std::memset(g_fake_slabobjs_idx_arr, 0, sizeof(g_fake_slabobjs_idx_arr));
        g_fake_slabset_num = 0;
        g_fake_slabobjs_num = 0;

        fake = *sim_port;
        fake.get_slabset_array = fake_get_slabset_array;
        fake.get_slabobjs_array = fake_get_slabobjs_array;
        fake.get_slabobjs_idx_array = fake_get_slabobjs_idx_array;
        fake.get_slabset_num_ptr = fake_get_slabset_num_ptr;
        fake.get_slabobjs_num_ptr = fake_get_slabobjs_num_ptr;
        set_sim_port(&fake);

        kfx_config_state.conf.slab_conf.slab_types_count = 1; // normally set by config_terrain.c's own loader
    }
    ~SlabsetFixture() {
        set_sim_port(nullptr);
    }
};
}

TEST_CASE_METHOD(SlabsetFixture, "load_slabset_config_file maps a slab style's Columns array, negated, into the backing SlabSet array", "[kfx_config][config_slabsets]") {
    REQUIRE(keeper_slabset_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/slabset_minimal.toml", 0));

    // slab_kind=0, slabstyle_no=0 ("S") -> slabset_no=0.
    for (int64_t i = 0; i < 9; i++) {
        CHECK(g_fake_slabset_arr[0].col_idx[i] == -(i + 1));
    }
}

TEST_CASE_METHOD(SlabsetFixture, "load_slabset_config_file marks a style with no _objects array as idx -1", "[kfx_config][config_slabsets]") {
    REQUIRE(keeper_slabset_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/slabset_minimal.toml", 0));

    CHECK(g_fake_slabobjs_idx_arr[0] == -1);
    CHECK(g_fake_slabobjs_num == 0); // nothing appended to the shared slabobjs array
}

TEST_CASE_METHOD(SlabsetFixture, "clear_slabsets zeroes every slabset/slabobj slot and resets the counters", "[kfx_config][config_slabsets]") {
    REQUIRE(keeper_slabset_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/slabset_minimal.toml", 0));
    REQUIRE(g_fake_slabset_arr[0].col_idx[0] != 0); // sanity: the load above really wrote something

    clear_slabsets();
    CHECK(g_fake_slabset_arr[0].col_idx[0] == 0);
    CHECK(g_fake_slabobjs_idx_arr[0] == -1); // clear_slabsets' own reset value, not 0
    CHECK(g_fake_slabset_num == SLABSET_COUNT);
    CHECK(g_fake_slabobjs_num == 0);
}

TEST_CASE_METHOD(SlabsetFixture, "load_slabset_config_file returns false for a missing file", "[kfx_config][config_slabsets]") {
    CHECK_FALSE(keeper_slabset_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/does_not_exist.toml", CnfLd_IgnoreErrors));
}

TEST_CASE_METHOD(ResetConfigState, "load_columns_config_file maps a column block's Lintel/Height/SolidMask/FloorTexture/Orientation/Cubes", "[kfx_config][config_slabsets]") {
    REQUIRE(keeper_columns_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/columnset_minimal.toml", 0));

    struct Column *col = &kfx_config_state.conf.column_conf.cols[0];
    // bitfields = permanent(1) | (Lintel=3 << 1 = 6) | (Height=2 << 4 = 32) = 39
    CHECK(col->bitfields == 39);
    CHECK(col->solidmask == 255);
    CHECK(col->floor_texture == 7);
    CHECK(col->orient == 1);
    for (int64_t i = 0; i < COLUMN_STACK_HEIGHT; i++) {
        CHECK(col->cubes[i] == 10 + i);
    }
    CHECK(kfx_config_state.conf.column_conf.columns_count == 1);
}

TEST_CASE_METHOD(ResetConfigState, "load_columns_config_file rejects an out-of-range Height, bumping columns_count but skipping the field writes", "[kfx_config][config_slabsets]") {
    // Height=9 exceeds COLUMN_STACK_HEIGHT(8) -- the ERRORLOG+continue
    // branch skips writing bitfields/solidmask/etc for this column
    // entirely, but columns_count was already bumped before the check
    // runs, so this is a real, observable inconsistency (a column
    // "exists" per columns_count but was never actually populated).
    REQUIRE(keeper_columns_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/columnset_invalid_height.toml", 0));

    CHECK(kfx_config_state.conf.column_conf.columns_count == 1);
    CHECK(kfx_config_state.conf.column_conf.cols[0].solidmask == 0); // SolidMask=255 in the fixture, never written
    CHECK(kfx_config_state.conf.column_conf.cols[0].bitfields == 0);
}

TEST_CASE("keeper_slabset_file_data/keeper_columns_file_data have no pre/post-load hooks", "[kfx_config][config_slabsets]") {
    CHECK(keeper_slabset_file_data.pre_load_func == nullptr);
    CHECK(keeper_slabset_file_data.post_load_func == nullptr);
    CHECK(std::strcmp(keeper_slabset_file_data.filename, "slabset.toml") == 0);
    CHECK(keeper_columns_file_data.pre_load_func == nullptr);
    CHECK(keeper_columns_file_data.post_load_func == nullptr);
    CHECK(std::strcmp(keeper_columns_file_data.filename, "columnset.toml") == 0);
}
