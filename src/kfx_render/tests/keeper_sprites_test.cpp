// kfx_render: keeper_sprites.c, the thing sprites' tables (moved from kfx_sim's creature_graphics.c by the HD
// sprite packs' Phase 1, docs/refactor/sprites/02-design.md section 13). keepersprite_rotable/_array/_index dispatch by
// animation id between creature_table_add[] (ids from SIM_KEEPERSPRITE_ADD_OFFSET) and creature_list[]/
// creature_table[] (ids below CREATURE_FRAMELIST_LENGTH); keeper_sprites_publish_frames() hands the simulation each
// id's frame count, which keepersprite_frames() (kfx_sim) then reads.
#include <catch2/catch_test_macros.hpp>

#include "keeper_sprites.h"
#include "creature_graphics.h"

#include <cstring>
#include <vector>

namespace {
constexpr int64_t kAddOffset = SIM_KEEPERSPRITE_ADD_OFFSET;

struct ResetTables {
    ResetTables() {
        std::memset(creature_table_add, 0, sizeof(creature_table_add));
        creature_table = nullptr;
        creature_table_length = 0; // creature_table[]/creature_list[]'s own data source stays "not loaded"
        keeper_sprites_publish_frames();
    }
    ~ResetTables() {
        std::memset(creature_table_add, 0, sizeof(creature_table_add));
        creature_table = nullptr;
        creature_table_length = 0;
        keeper_sprites_publish_frames();
    }
};
}

TEST_CASE_METHOD(ResetTables, "keepersprite_rotable/_array route an id in the custom range to creature_table_add", "[kfx_render][keeper_sprites]") {
    creature_table_add[0].FramesCount = 12;
    creature_table_add[0].Rotable = 1;
    CHECK(keepersprite_rotable(kAddOffset) == 1);
    CHECK(keepersprite_array(kAddOffset) == &creature_table_add[0]);
}

TEST_CASE("keepersprite_index returns an id in the custom range unchanged", "[kfx_render][keeper_sprites]") {
    CHECK(keepersprite_index(kAddOffset) == kAddOffset);
}

TEST_CASE("keepersprite_rotable/_array/_index report the out-of-range fallback for an id in neither table", "[kfx_render][keeper_sprites]") {
    // Between CREATURE_FRAMELIST_LENGTH (982) and SIM_KEEPERSPRITE_ADD_OFFSET (16384).
    const int64_t n = 5000;
    CHECK(keepersprite_rotable(n) == 0);
    CHECK(keepersprite_array(n) == nullptr);
    CHECK(keepersprite_index(n) == 0);
}

TEST_CASE_METHOD(ResetTables, "an original id whose table isn't loaded is neither drawn nor published", "[kfx_render][keeper_sprites]") {
    CHECK(keepersprite_array(5) == nullptr);
    CHECK(keepersprite_frames(5) == 0);
}

TEST_CASE_METHOD(ResetTables, "keeper_sprites_publish_frames hands the simulation every id's frame count", "[kfx_render][keeper_sprites]") {
    // A table as long as the highest entry creature_list[] names, each entry's FramesCount its index mod 200 + 1.
    std::vector<struct KeeperSprite> table(9200);
    for (size_t i = 0; i < table.size(); i++)
        table[i].FramesCount = (unsigned char)(i % 200 + 1);
    creature_table = table.data();
    creature_table_length = table.size();
    creature_table_add[0].FramesCount = 6;
    creature_table_add[SIM_KEEPERSPRITE_ADD_NUM - 1].FramesCount = 4;
    CHECK(keeper_sprites_frames_mismatches() > 0); // not published yet
    keeper_sprites_publish_frames();
    CHECK(keeper_sprites_frames_mismatches() == 0);
    for (int64_t n = 0; n < CREATURE_FRAMELIST_LENGTH; n++) {
        const struct KeeperSprite *kspr = keepersprite_array(n);
        REQUIRE(kspr != nullptr);
        CHECK(keepersprite_frames(n) == kspr->FramesCount);
    }
    CHECK(keepersprite_frames(kAddOffset) == 6);
    CHECK(keepersprite_frames(kAddOffset + SIM_KEEPERSPRITE_ADD_NUM - 1) == 4);
    // A custom sprite changes: only the custom publish is needed.
    creature_table_add[0].FramesCount = 8;
    CHECK(keeper_sprites_frames_mismatches() == 1);
    keeper_sprites_publish_custom_frames();
    CHECK(keepersprite_frames(kAddOffset) == 8);
    CHECK(keeper_sprites_frames_mismatches() == 0);
}
