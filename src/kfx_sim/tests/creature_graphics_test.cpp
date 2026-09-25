// kfx_sim: creature_graphics.c's keepersprite_* dispatch functions --
// originally targeted to focus coverage around
// scripts/check_layering_symbols.py's then-ACCEPTED (architecture.md §8.2)
// kfx_sim -> kfx_render residual: creature_table_add[] was declared in
// kfx_sim's own creature_graphics.h ("a higher-ranked library
// implementing a lower-ranked interface is fine") but really *populated*
// by kfx_render's custom_sprites.c. That residual is gone now
// (docs/refactor/todo/remove-symbol-level-layering-residuals.md):
// creature_table_add[]'s storage itself moved down into
// creature_graphics.c, next to its sibling creature_table -- this test
// now links the real array directly, no stub involved, and uses the real
// SIM_KEEPERSPRITE_ADD_OFFSET/_NUM constants (also moved into
// creature_graphics.h alongside the array they size). Every
// keepersprite_* function dispatches between two data sources by
// frame-number range: creature_table_add[] (for n in
// [SIM_KEEPERSPRITE_ADD_OFFSET, +SIM_KEEPERSPRITE_ADD_NUM)) or
// creature_list[]/creature_table[] (kfx_sim's own, for n <
// CREATURE_FRAMELIST_LENGTH).
#include <catch2/catch_test_macros.hpp>

#include "creature_graphics.h"

#include <cstring>

namespace {
constexpr int64_t kAddOffset = SIM_KEEPERSPRITE_ADD_OFFSET;

struct ResetCreatureTableAdd {
    ResetCreatureTableAdd() {
        std::memset(&creature_table_add[0], 0, sizeof(creature_table_add[0]));
        creature_table_length = 0; // creature_table[]/creature_list[]'s own data source stays "not loaded"
    }
};
}

TEST_CASE_METHOD(ResetCreatureTableAdd, "keepersprite_frames/_rotable/_array route an in-range frame number to creature_table_add", "[kfx_sim][creature_graphics]") {
    creature_table_add[0].FramesCount = 12;
    creature_table_add[0].Rotable = 1;

    CHECK(keepersprite_frames(kAddOffset) == 12);
    CHECK(keepersprite_rotable(kAddOffset) == 1);
    CHECK(keepersprite_array(kAddOffset) == &creature_table_add[0]);
}

TEST_CASE("keepersprite_index returns the frame number unchanged when it's in the creature_table_add range", "[kfx_sim][creature_graphics]") {
    CHECK(keepersprite_index(kAddOffset) == kAddOffset);
}

TEST_CASE("keepersprite_frames/_rotable/_array/_index all report the out-of-range fallback for a frame number in neither data source", "[kfx_sim][creature_graphics]") {
    // Between CREATURE_FRAMELIST_LENGTH (982) and SIM_KEEPERSPRITE_ADD_OFFSET
    // (16384) -- genuinely out of range for both dispatch targets, no
    // array access happens on either side.
    const int64_t n = 5000;
    CHECK(keepersprite_frames(n) == 0);
    CHECK(keepersprite_rotable(n) == 0);
    CHECK(keepersprite_array(n) == nullptr);
    CHECK(keepersprite_index(n) == 0);
}

TEST_CASE("keepersprite_frames falls back to 0 for an in-range creature_list index when the real creature table isn't loaded", "[kfx_sim][creature_graphics]") {
    // creature_table_length defaults to 0 (nothing loaded in a test
    // environment) -- creature_list[]'s own lookup entries are real,
    // baked-in production data, but the "i < creature_table_length"
    // guard rejects every one of them until a real table is populated,
    // so this never dereferences the (also default-null) creature_table
    // pointer.
    CHECK(keepersprite_frames(5) == 0);
}
