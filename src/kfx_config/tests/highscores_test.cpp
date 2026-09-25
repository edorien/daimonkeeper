// kfx_config: highscores.c's add_high_score_entry()/get_level_highest_score()
// -- a genuinely nontrivial sorted-array insertion algorithm (find
// insert position by score, separately find a duplicate-level slot to
// overwrite, shift the gap between them) operating purely on the
// module-level `campaign` global's hiscore_table/hiscore_count (already
// exposed for tests via config_campaigns_test.cpp's own
// ResetGlobalCampaign pattern). load_high_score_table()/
// create_empty_high_score_table()/save_high_score_table() all touch
// real file I/O (prepare_file_path/LbFileLengthRnc/LbFileSaveAt) and
// are `static` besides -- not attempted here.
#include <catch2/catch_test_macros.hpp>

#include "highscores.h"
#include "config_campaigns.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {
struct HiscoreFixture {
    HiscoreFixture() {
        std::memset(&campaign, 0, sizeof(campaign));
    }
    ~HiscoreFixture() {
        free(campaign.hiscore_table);
        std::memset(&campaign, 0, sizeof(campaign));
    }
};
}

TEST_CASE_METHOD(HiscoreFixture, "add_high_score_entry inserts into slot 0 of a fully empty table", "[kfx_config][highscores]") {
    campaign.hiscore_count = 15;
    campaign.hiscore_table = (struct HighScore *)calloc(campaign.hiscore_count, sizeof(struct HighScore));

    int64_t idx = add_high_score_entry(500, 3, "Alice");
    CHECK(idx == 0);
    CHECK(campaign.hiscore_table[0].score == 500);
    CHECK(campaign.hiscore_table[0].lvnum == 3);
    CHECK(std::strcmp(campaign.hiscore_table[0].name, "Alice") == 0);
}

TEST_CASE_METHOD(HiscoreFixture, "add_high_score_entry returns -1 for a low score when the table is full of distinct levels", "[kfx_config][highscores]") {
    campaign.hiscore_count = 15;
    campaign.hiscore_table = (struct HighScore *)calloc(campaign.hiscore_count, sizeof(struct HighScore));
    for (uint64_t i = 0; i < campaign.hiscore_count; i++) {
        campaign.hiscore_table[i].score = (int64_t)(15 - i) * 10; // 150, 140, ..., 10 -- distinct, all > 1
        campaign.hiscore_table[i].lvnum = (LevelNumber)(i + 1); // distinct, all positive
        snprintf(campaign.hiscore_table[i].name, HISCORE_NAME_LENGTH, "P%" PRIu64, (uint64_t)(i));
    }

    // Score of 1 is lower than every existing entry, and no two entries
    // share a level number, so there's nothing safe to overwrite.
    CHECK(add_high_score_entry(1, 99, "TooLow") == -1);
}

TEST_CASE_METHOD(HiscoreFixture, "get_level_highest_score finds a real entry for a level, skipping the Bullfrog placeholder for the same level", "[kfx_config][highscores]") {
    campaign.hiscore_count = 4;
    campaign.hiscore_table = (struct HighScore *)calloc(campaign.hiscore_count, sizeof(struct HighScore));
    campaign.hiscore_table[0].lvnum = 7;
    campaign.hiscore_table[0].score = 999;
    snprintf(campaign.hiscore_table[0].name, HISCORE_NAME_LENGTH, "Bullfrog"); // placeholder, must be skipped
    campaign.hiscore_table[1].lvnum = 7;
    campaign.hiscore_table[1].score = 250;
    snprintf(campaign.hiscore_table[1].name, HISCORE_NAME_LENGTH, "RealPlayer");

    CHECK(get_level_highest_score(7) == 250);
}

TEST_CASE_METHOD(HiscoreFixture, "get_level_highest_score returns 0 when no entry matches the level", "[kfx_config][highscores]") {
    campaign.hiscore_count = 2;
    campaign.hiscore_table = (struct HighScore *)calloc(campaign.hiscore_count, sizeof(struct HighScore));
    campaign.hiscore_table[0].lvnum = 1;
    campaign.hiscore_table[0].score = 100;

    CHECK(get_level_highest_score(999) == 0);
}

// The table is written to / read from disk as a raw array of struct HighScore (highscores.c: LbFileSaveAt/
// LbFileLoadAt of hiscore_count * sizeof(...)), and load_high_score_table() rejects a file whose length is not
// exactly that. With explicit 64-bit fields the entry is 8 + 64 + 8 bytes on every platform.
TEST_CASE("struct HighScore has one on-disk layout on every platform", "[kfx_config][highscores]") {
    CHECK(sizeof(struct HighScore) == 80);
    CHECK(offsetof(struct HighScore, score) == 0);
    CHECK(offsetof(struct HighScore, name) == 8);
    CHECK(offsetof(struct HighScore, lvnum) == 8 + HISCORE_NAME_LENGTH);
    CHECK(sizeof(((struct HighScore *)nullptr)->score) == 8);
}
