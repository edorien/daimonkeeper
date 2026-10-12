// kfx_sim: creature_graphics.c's animation frame counts -- all the simulation keeps of the thing sprites since the
// sprite tables moved to kfx_render (keeper_sprites.c; docs/refactor/sprites/02-design.md section 13). kfx_render
// publishes them with keepersprite_set_frames(); keepersprite_frames() and get_lifespan_of_animation() read them,
// by animation id: the original ids below CREATURE_FRAMELIST_LENGTH, the custom ones from
// SIM_KEEPERSPRITE_ADD_OFFSET. kfx_render's keeper_sprites_test.cpp checks the publishing.
#include <catch2/catch_test_macros.hpp>

#include "creature_graphics.h"

namespace {
constexpr int64_t kAddOffset = SIM_KEEPERSPRITE_ADD_OFFSET;

void forget_test_ids() {
    keepersprite_forget_frames(5);
    keepersprite_forget_frames(kAddOffset);
    keepersprite_forget_frames(kAddOffset + SIM_KEEPERSPRITE_ADD_NUM - 1);
}

struct ResetFrames {
    ResetFrames() { forget_test_ids(); }
    ~ResetFrames() { forget_test_ids(); }
};
}

TEST_CASE_METHOD(ResetFrames, "keepersprite_frames reads back the frame count published for an original or a custom id", "[kfx_sim][creature_graphics]") {
    keepersprite_set_frames(5, 7);
    keepersprite_set_frames(kAddOffset, 12);
    keepersprite_set_frames(kAddOffset + SIM_KEEPERSPRITE_ADD_NUM - 1, 3);
    CHECK(keepersprite_frames(5) == 7);
    CHECK(keepersprite_frames(kAddOffset) == 12);
    CHECK(keepersprite_frames(kAddOffset + SIM_KEEPERSPRITE_ADD_NUM - 1) == 3);
}

TEST_CASE_METHOD(ResetFrames, "keepersprite_frames gives 0 for an original id nothing published, and for ids out of both ranges", "[kfx_sim][creature_graphics]") {
    CHECK(keepersprite_frames(5) == 0);
    CHECK(keepersprite_frames(-1) == 0);
    // Between CREATURE_FRAMELIST_LENGTH (982) and SIM_KEEPERSPRITE_ADD_OFFSET (16384): neither range.
    CHECK(keepersprite_frames(5000) == 0);
    CHECK(keepersprite_frames(kAddOffset + SIM_KEEPERSPRITE_ADD_NUM) == 0);
    // Out-of-range ids are never stored.
    keepersprite_set_frames(5000, 9);
    CHECK(keepersprite_frames(5000) == 0);
}

TEST_CASE_METHOD(ResetFrames, "keepersprite_forget_frames unpublishes an id", "[kfx_sim][creature_graphics]") {
    keepersprite_set_frames(5, 7);
    keepersprite_forget_frames(5);
    CHECK(keepersprite_frames(5) == 0);
}

TEST_CASE_METHOD(ResetFrames, "get_lifespan_of_animation is the frame count over the speed, in 8.8", "[kfx_sim][creature_graphics]") {
    keepersprite_set_frames(5, 6);
    CHECK(get_lifespan_of_animation(5, 256) == 6);
    CHECK(get_lifespan_of_animation(5, 128) == 12);
    CHECK(get_lifespan_of_animation(5, 0) == 6); // no speed: the frame count itself
}
