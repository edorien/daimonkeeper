// kfx_sim coverage: regenerate_derived_map_data() (lvl_filesdk1.c) --
// see its own declaration (lvl_filesdk1.h) and
// docs/refactor/editor/phase3/00-slice1-native-save.md for why it exists.
// Unlike the rest of lvl_filesdk1.c (see lvl_filesdk1_test.cpp's own
// header comment), this function needs no file I/O at all -- it derives
// every slab's columns purely from in-memory slab kinds already set by
// ResetSimAndConfig/direct writes, so it's genuinely unit-testable.
#include <catch2/catch_test_macros.hpp>

#include "lvl_filesdk1.h"
#include "map_blocks.h"
#include "map_data.h"
#include "slab_data.h"
#include "config_terrain.h"
#include "kfx_sim_test_fixtures.h"

using namespace kfx_test;

TEST_CASE_METHOD(ResetSimAndConfig, "regenerate_derived_map_data derives every slab's health from its own kind's config", "[kfx_sim][lvl_filesdk1]") {
    // slab_types_count must be set explicitly -- ResetSimAndConfig zeroes
    // it, and get_slab_kind_stats()/regenerate_derived_map_data()'s own
    // bounds check both treat "kind >= slab_types_count" as invalid, which
    // would otherwise reject even SlbT_ROCK (kind 0).
    kfx_config_state.conf.slab_conf.slab_types_count = 10;
    // A distinctive, non-zero block_health value at index 0 (slab_cfgstats[0]'s
    // own default block_health_index), so a slab picking it up proves
    // place_single_slab_type_on_map() genuinely ran and read config, rather
    // than everything just staying at ResetSimAndConfig's zeroed default --
    // col_idx/collide-flag derivation isn't independently observable here
    // since kfx_sim_state.slabset[]/slab_cfgstats[] are otherwise still
    // entirely zeroed (no real slabset.toml load happens in this fixture).
    kfx_sim_state.block_health[0] = 42;

    for (MapSlabCoord y = 0; y < kfx_sim_state.map_tiles_y; y++) {
        for (MapSlabCoord x = 0; x < kfx_sim_state.map_tiles_x; x++) {
            get_slabmap_block(x, y)->kind = SlbT_ROCK;
        }
    }

    CHECK(regenerate_derived_map_data());

    for (MapSlabCoord y = 0; y < kfx_sim_state.map_tiles_y; y++) {
        for (MapSlabCoord x = 0; x < kfx_sim_state.map_tiles_x; x++) {
            CHECK(get_slabmap_block(x, y)->health == 42);
        }
    }
}

TEST_CASE_METHOD(ResetSimAndConfig, "regenerate_derived_map_data fails loudly on a slab kind outside the configured range, rather than clamping and continuing", "[kfx_sim][lvl_filesdk1]") {
    kfx_config_state.conf.slab_conf.slab_types_count = 10;

    for (MapSlabCoord y = 0; y < kfx_sim_state.map_tiles_y; y++) {
        for (MapSlabCoord x = 0; x < kfx_sim_state.map_tiles_x; x++) {
            get_slabmap_block(x, y)->kind = SlbT_ROCK;
        }
    }
    // One corrupt slab, out of the configured 0-9 range.
    get_slabmap_block(2, 2)->kind = 255;

    CHECK_FALSE(regenerate_derived_map_data());
}
