// kfx_render: engine_sprites_hd.c (docs/refactor/sprites/, HD sprite packs Phase 0): the content key of a sprite
// frame, which scripts/spritepack.py computes too (the test vector below is its `selftest`), and the sprite draw
// audit's binning.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "engine_sprites_hd.h"

namespace {

// 5x3: row 0 has a literal index 0 and a skip, row 1 a run past the row's end, row 2 nothing.
const std::vector<unsigned char> kVector = { 2, 7, 0, 0xFE, 1, 3, 0,   0xFD, 3, 4, 5, 6, 0,   0 };
const uint64_t kVectorKey = 0x7a0d6190d491bb54ULL;

}

TEST_CASE("sprite_hd_frame_key matches spritepack.py's test vector", "[kfx_render][sprite_hd_key]") {
    CHECK(sprite_hd_frame_key(kVector.data(), kVector.size(), 5, 3) == kVectorKey);
}

TEST_CASE("sprite_hd_frame_key hashes a literal index 0 as transparent", "[kfx_render][sprite_hd_key]") {
    // Row 0 as a skip of two (where the vector has the literal 0 and a skip of two, three transparent texels).
    const std::vector<unsigned char> skipped = { 1, 7, 0xFD, 1, 3, 0,   0xFD, 3, 4, 5, 6, 0,   0 };
    CHECK(sprite_hd_frame_key(skipped.data(), skipped.size(), 5, 3) == kVectorKey);
}

TEST_CASE("sprite_hd_frame_key depends on the size and the pixels", "[kfx_render][sprite_hd_key]") {
    CHECK(sprite_hd_frame_key(kVector.data(), kVector.size(), 6, 3) != kVectorKey);
    std::vector<unsigned char> other = kVector;
    other[1] = 8;
    CHECK(sprite_hd_frame_key(other.data(), other.size(), 5, 3) != kVectorKey);
    // An extra empty row at the bottom is a different frame.
    std::vector<unsigned char> taller = kVector;
    taller.push_back(0);
    CHECK(sprite_hd_frame_key(taller.data(), taller.size(), 5, 4) != kVectorKey);
}

TEST_CASE("sprite_hd_frame_key gives 0 for frames it can't hash", "[kfx_render][sprite_hd_key]") {
    CHECK(sprite_hd_frame_key(nullptr, 0, 5, 3) == 0);
    CHECK(sprite_hd_frame_key(kVector.data(), kVector.size(), 0, 3) == 0);
    CHECK(sprite_hd_frame_key(kVector.data(), kVector.size(), 5, 0) == 0);
    CHECK(sprite_hd_frame_key(kVector.data(), kVector.size(), SPRITE_HD_KEY_MAX_WIDTH + 1, 3) == 0);
    // Truncated: the data ends inside a row, or inside a run.
    CHECK(sprite_hd_frame_key(kVector.data(), kVector.size() - 1, 5, 3) == 0);
    CHECK(sprite_hd_frame_key(kVector.data(), 9, 5, 3) == 0);
}

TEST_CASE("sprite_audit_bin puts magnifications in the terrain audit's half-octave bins", "[kfx_render][sprite_audit]") {
    const int one = -TERRAIN_TEXEL_LOG2_MIN * 2; // the bin starting at 1 pixel per texel
    CHECK(sprite_audit_bin(1.0) == one);
    CHECK(sprite_audit_bin(1.4) == one);
    CHECK(sprite_audit_bin(1.5) == one + 1);
    CHECK(sprite_audit_bin(2.0) == one + 2);
    CHECK(sprite_audit_bin(0.5) == one - 2);
    CHECK(sprite_audit_bin(0.0) == 0);
    CHECK(sprite_audit_bin(1e-9) == 0);
    CHECK(sprite_audit_bin(1e9) == TERRAIN_TEXEL_BINS - 1);
}

TEST_CASE("sprite_audit_draw records nothing while the audit is off", "[kfx_render][sprite_audit]") {
    sprite_audit_set_enabled(0);
    sprite_audit_frame_begin(1);
    sprite_audit_draw(0, 0, 10, 10, 20, 20);
    sprite_audit_frame_begin(2);
    CHECK(sprite_audit_last()->draws == 0);
    CHECK(sprite_audit_last()->frame == 0);
}
