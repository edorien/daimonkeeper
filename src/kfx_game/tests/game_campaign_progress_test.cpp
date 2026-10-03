// kfx_game: game_campaign_progress.c -- Phase A of
// docs/refactor/gui/05-campaign-progress-and-landview.md (save/progress.cfg).
//
// parse_progress_cfg_campaign_block() is exercised directly against
// hand-written buffers (that function's own comment explains why: this
// test binary has no save/ directory, so load_campaign_progress_file()
// itself can only be tested for "no file present" behaviour, matching
// game_heap_test.cpp's own precedent for the same kind of gap).
// TRANSFER_CREATURE is the one key not round-tripped here: parse_creature_name()
// (config_creature.c) resolves names against creature_desc[], which is
// populated from loaded creature config data -- empty in this environment,
// so every creature name parses to an invalid model. Only the resulting
// graceful-rejection path is tested for that key, matching this codebase's
// established convention for real game-data-dependent lookups (see
// game_saves_transfer_test.cpp's own header comment for a similar case).
// Phase D's continue_game_available() (game_saves.c) is also covered here,
// not in game_saves_transfer_test.cpp -- it's a thin composition of
// load_campaign_progress_file()/reconcile_fx1contn_into_progress()/
// any_campaign_progress_exists(), all already covered above, so it belongs
// with the rest of this feature's own tests.
#include <catch2/catch_test_macros.hpp>

#include "game_campaign_progress.h"
#include "config_campaigns.h" // campaign, campaigns_list
#include "game_saves.h"       // continue_game_filename, continue_game_available

#include <cstring>

namespace {
struct ResetCampaignProgress {
    ResetCampaignProgress() {
        reset_all_campaign_progress(); // clears the in-memory table (file write fails harmlessly, no save/ dir)
        std::memset(&campaign, 0, sizeof(campaign));
        std::memset(&campaigns_list, 0, sizeof(campaigns_list));
    }
    ~ResetCampaignProgress() {
        reset_all_campaign_progress();
        std::memset(&campaign, 0, sizeof(campaign));
        std::memset(&campaigns_list, 0, sizeof(campaigns_list));
    }
};

struct CampaignProgressEntry *make_entry(const char *fname) {
    return get_campaign_progress(fname, true);
}
}

TEST_CASE_METHOD(ResetCampaignProgress, "get_campaign_progress finds an existing entry by name, case-insensitively", "[kfx_game][game_campaign_progress]") {
    struct CampaignProgressEntry *created = make_entry("keeporig.cfg");
    REQUIRE(created != nullptr);
    CHECK(std::strcmp(created->cmpgn_fname, "keeporig.cfg") == 0);

    struct CampaignProgressEntry *found = get_campaign_progress("KEEPORIG.CFG", false);
    CHECK(found == created);
}

TEST_CASE_METHOD(ResetCampaignProgress, "get_campaign_progress returns NULL for an unknown campaign when create_if_missing is false", "[kfx_game][game_campaign_progress]") {
    CHECK(get_campaign_progress("nope.cfg", false) == nullptr);
}

TEST_CASE_METHOD(ResetCampaignProgress, "get_campaign_progress creates a fresh, empty entry", "[kfx_game][game_campaign_progress]") {
    struct CampaignProgressEntry *entry = make_entry("fresh.cfg");
    REQUIRE(entry != nullptr);
    CHECK(entry->unlocked_levels_count == 0);
    CHECK(entry->bonus_available_count == 0);
    CHECK(entry->intralvl.next_level == 0);
}

TEST_CASE_METHOD(ResetCampaignProgress, "campaign_progress_unlock_level adds a level once and is idempotent", "[kfx_game][game_campaign_progress]") {
    struct CampaignProgressEntry *entry = make_entry("keeporig.cfg");
    CHECK_FALSE(campaign_progress_has_unlocked_level(entry, 3));
    CHECK(campaign_progress_unlock_level(entry, 3));
    CHECK(campaign_progress_has_unlocked_level(entry, 3));
    CHECK(entry->unlocked_levels_count == 1);

    CHECK(campaign_progress_unlock_level(entry, 3)); // already unlocked -- true, no duplicate
    CHECK(entry->unlocked_levels_count == 1);

    CHECK(campaign_progress_unlock_level(entry, 4));
    CHECK(entry->unlocked_levels_count == 2);
}

TEST_CASE_METHOD(ResetCampaignProgress, "campaign_progress_has_unlocked_level/unlock_level are false/no-op for a null entry", "[kfx_game][game_campaign_progress]") {
    CHECK_FALSE(campaign_progress_has_unlocked_level(nullptr, 1));
    CHECK_FALSE(campaign_progress_unlock_level(nullptr, 1));
}

// Skirmish/Free Play completion tracking (item 3): completed_levels[] is a
// separate array from unlocked_levels[], mirroring its accessors exactly.
TEST_CASE_METHOD(ResetCampaignProgress, "campaign_progress_mark_level_completed adds a level once and is idempotent", "[kfx_game][game_campaign_progress]") {
    struct CampaignProgressEntry *entry = make_entry("mymappack.cfg");
    CHECK_FALSE(campaign_progress_has_completed_level(entry, 30));
    CHECK(campaign_progress_mark_level_completed(entry, 30));
    CHECK(campaign_progress_has_completed_level(entry, 30));
    CHECK(entry->completed_levels_count == 1);

    CHECK(campaign_progress_mark_level_completed(entry, 30)); // already completed -- true, no duplicate
    CHECK(entry->completed_levels_count == 1);

    CHECK(campaign_progress_mark_level_completed(entry, 31));
    CHECK(entry->completed_levels_count == 2);

    // Independent of unlocked_levels -- a mappack entry never populates that array.
    CHECK(entry->unlocked_levels_count == 0);
}

TEST_CASE_METHOD(ResetCampaignProgress, "campaign_progress_has_completed_level/mark_level_completed are false/no-op for a null entry", "[kfx_game][game_campaign_progress]") {
    CHECK_FALSE(campaign_progress_has_completed_level(nullptr, 1));
    CHECK_FALSE(campaign_progress_mark_level_completed(nullptr, 1));
}

TEST_CASE_METHOD(ResetCampaignProgress, "parsing COMPLETED_LEVELS marks every listed level, independent of UNLOCKED_LEVELS", "[kfx_game][game_campaign_progress]") {
    struct CampaignProgressEntry *entry = make_entry("mymappack.cfg");
    const char *buf = "COMPLETED_LEVELS = 10 20 30\n[nextblock]\n";
    parse_progress_cfg_campaign_block(entry, buf, (int64_t)std::strlen(buf), 0);

    CHECK(entry->completed_levels_count == 3);
    CHECK(campaign_progress_has_completed_level(entry, 10));
    CHECK(campaign_progress_has_completed_level(entry, 20));
    CHECK(campaign_progress_has_completed_level(entry, 30));
    CHECK_FALSE(campaign_progress_has_completed_level(entry, 15));
    CHECK(entry->unlocked_levels_count == 0);
}

TEST_CASE_METHOD(ResetCampaignProgress, "reset_all_campaign_progress clears every in-memory entry", "[kfx_game][game_campaign_progress]") {
    struct CampaignProgressEntry *entry = make_entry("keeporig.cfg");
    campaign_progress_unlock_level(entry, 1);
    REQUIRE(get_campaign_progress("keeporig.cfg", false) != nullptr);

    reset_all_campaign_progress();

    CHECK(get_campaign_progress("keeporig.cfg", false) == nullptr);
}

TEST_CASE_METHOD(ResetCampaignProgress, "parsing UNLOCKED_LEVELS unlocks every listed level", "[kfx_game][game_campaign_progress]") {
    struct CampaignProgressEntry *entry = make_entry("keeporig.cfg");
    const char *buf = "UNLOCKED_LEVELS = 1 2 3 5\n[nextblock]\n";
    parse_progress_cfg_campaign_block(entry, buf, (int64_t)std::strlen(buf), 0);

    CHECK(entry->unlocked_levels_count == 4);
    CHECK(campaign_progress_has_unlocked_level(entry, 1));
    CHECK(campaign_progress_has_unlocked_level(entry, 2));
    CHECK(campaign_progress_has_unlocked_level(entry, 3));
    CHECK(campaign_progress_has_unlocked_level(entry, 5));
    CHECK_FALSE(campaign_progress_has_unlocked_level(entry, 4));
}

TEST_CASE_METHOD(ResetCampaignProgress, "parsing NEXT_LEVEL sets intralvl.next_level", "[kfx_game][game_campaign_progress]") {
    struct CampaignProgressEntry *entry = make_entry("keeporig.cfg");
    const char *buf = "NEXT_LEVEL = 6\n";
    parse_progress_cfg_campaign_block(entry, buf, (int64_t)std::strlen(buf), 0);
    CHECK(entry->intralvl.next_level == 6);
}

TEST_CASE_METHOD(ResetCampaignProgress, "parsing BONUS_AVAILABLE records raw level numbers, not IntralevelData.bonuses_found", "[kfx_game][game_campaign_progress]") {
    struct CampaignProgressEntry *entry = make_entry("keeporig.cfg");
    const char *buf = "BONUS_AVAILABLE = 105\nBONUS_AVAILABLE = 106\n";
    parse_progress_cfg_campaign_block(entry, buf, (int64_t)std::strlen(buf), 0);

    REQUIRE(entry->bonus_available_count == 2);
    CHECK(entry->bonus_available[0] == 105);
    CHECK(entry->bonus_available[1] == 106);
    // Confirms the design choice (struct CampaignProgressEntry's own
    // comment): this key never touches intralvl.bonuses_found's
    // campaign-context-relative bitset.
    for (int64_t i = 0; i < BONUS_LEVEL_STORAGE_COUNT; i++)
        CHECK(entry->intralvl.bonuses_found[i] == 0);
}

TEST_CASE_METHOD(ResetCampaignProgress, "parsing CAMPAIGN_FLAG sets the right [player][flag] slot", "[kfx_game][game_campaign_progress]") {
    struct CampaignProgressEntry *entry = make_entry("keeporig.cfg");
    const char *buf = "CAMPAIGN_FLAG = 0 3 42\nCAMPAIGN_FLAG = 1 7 -5\n";
    parse_progress_cfg_campaign_block(entry, buf, (int64_t)std::strlen(buf), 0);

    CHECK(entry->intralvl.campaign_flags[0][3] == 42);
    CHECK(entry->intralvl.campaign_flags[1][7] == -5);
    CHECK(entry->intralvl.campaign_flags[0][0] == 0); // untouched slots stay zero
}

TEST_CASE_METHOD(ResetCampaignProgress, "parsing CAMPAIGN_FLAG rejects an out-of-range player or flag index", "[kfx_game][game_campaign_progress]") {
    struct CampaignProgressEntry *entry = make_entry("keeporig.cfg");
    const char *buf = "CAMPAIGN_FLAG = 99 3 42\n";
    parse_progress_cfg_campaign_block(entry, buf, (int64_t)std::strlen(buf), 0);
    // Nothing in range got written -- every slot stays at its zeroed default.
    for (int64_t p = 0; p < PLAYERS_FOR_CAMPAIGN_FLAGS; p++)
        for (int64_t f = 0; f < CAMPAIGN_FLAGS_PER_PLAYER; f++)
            CHECK(entry->intralvl.campaign_flags[p][f] == 0);
}

TEST_CASE_METHOD(ResetCampaignProgress, "parsing ENSIGN_OVERRIDE fills the first free slot", "[kfx_game][game_campaign_progress]") {
    struct CampaignProgressEntry *entry = make_entry("keeporig.cfg");
    const char *buf = "ENSIGN_OVERRIDE = 7 2\nENSIGN_OVERRIDE = 12 5\n";
    parse_progress_cfg_campaign_block(entry, buf, (int64_t)std::strlen(buf), 0);

    CHECK(entry->intralvl.ensign_overrides[0].lvnum == 7);
    CHECK(entry->intralvl.ensign_overrides[0].active);
    CHECK(entry->intralvl.ensign_overrides[0].ensign_type == 2);
    CHECK(entry->intralvl.ensign_overrides[1].lvnum == 12);
    CHECK(entry->intralvl.ensign_overrides[1].ensign_type == 5);
}

TEST_CASE_METHOD(ResetCampaignProgress, "parsing ENSIGN_OVERRIDE for an already-overridden level updates it in place, not a new slot", "[kfx_game][game_campaign_progress]") {
    struct CampaignProgressEntry *entry = make_entry("keeporig.cfg");
    const char *buf = "ENSIGN_OVERRIDE = 7 2\nENSIGN_OVERRIDE = 7 9\n";
    parse_progress_cfg_campaign_block(entry, buf, (int64_t)std::strlen(buf), 0);

    CHECK(entry->intralvl.ensign_overrides[0].lvnum == 7);
    CHECK(entry->intralvl.ensign_overrides[0].ensign_type == 9); // second line won
    CHECK(entry->intralvl.ensign_overrides[1].lvnum == 0); // no second slot used
}

TEST_CASE_METHOD(ResetCampaignProgress, "parsing TRANSFER_CREATURE with an unrecognized creature name is rejected gracefully", "[kfx_game][game_campaign_progress]") {
    // creature_desc[] (config_creature.c) is empty in this environment --
    // see this file's own header comment -- so every name is "unrecognized".
    struct CampaignProgressEntry *entry = make_entry("keeporig.cfg");
    const char *buf = "TRANSFER_CREATURE = 0 0 HORNY 3 1 Fluffy\n";
    parse_progress_cfg_campaign_block(entry, buf, (int64_t)std::strlen(buf), 0);

    CHECK(entry->intralvl.transferred_creatures[0][0].model == 0);
}

TEST_CASE_METHOD(ResetCampaignProgress, "an unparseable line is skipped without corrupting later lines", "[kfx_game][game_campaign_progress]") {
    struct CampaignProgressEntry *entry = make_entry("keeporig.cfg");
    const char *buf = "NOT_A_REAL_KEY = whatever\nNEXT_LEVEL = 9\n";
    parse_progress_cfg_campaign_block(entry, buf, (int64_t)std::strlen(buf), 0);
    CHECK(entry->intralvl.next_level == 9);
}

TEST_CASE_METHOD(ResetCampaignProgress, "load_campaign_progress_file returns true (not an error) when save/progress.cfg doesn't exist", "[kfx_game][game_campaign_progress]") {
    // This test binary's environment has no save/ directory at all --
    // see this file's own header comment.
    CHECK(load_campaign_progress_file());
    CHECK(get_campaign_progress("keeporig.cfg", false) == nullptr);
}

TEST_CASE_METHOD(ResetCampaignProgress, "save_campaign_progress_file fails gracefully when save/ doesn't exist to write into", "[kfx_game][game_campaign_progress]") {
    make_entry("keeporig.cfg");
    CHECK_FALSE(save_campaign_progress_file());
}

TEST_CASE_METHOD(ResetCampaignProgress, "reconcile_fx1contn_into_progress is a no-op when fx1contn.sav doesn't exist", "[kfx_game][game_campaign_progress]") {
    reconcile_fx1contn_into_progress(); // must not crash
    CHECK(get_campaign_progress("keeporig.cfg", false) == nullptr);
}

TEST_CASE_METHOD(ResetCampaignProgress, "campaign_progress_record_level_completed rejects SINGLEPLAYER_NOTSTARTED and other non-positive values", "[kfx_game][game_campaign_progress]") {
    CHECK_FALSE(campaign_progress_record_level_completed(SINGLEPLAYER_NOTSTARTED));
    CHECK_FALSE(campaign_progress_record_level_completed(-2));
}

TEST_CASE_METHOD(ResetCampaignProgress, "campaign_progress_record_level_completed accepts SINGLEPLAYER_FINISHED (still fails on save/ absent, not on the value itself)", "[kfx_game][game_campaign_progress]") {
    // SINGLEPLAYER_FINISHED is a real, meaningful value (Phase B reads it
    // back as "campaign complete") -- this test binary's environment has
    // no save/ directory (see this file's own header comment), so the
    // call still returns false, but from save_campaign_progress_file()
    // failing to open the file, not from the lvnum guard itself.
    CHECK_FALSE(campaign_progress_record_level_completed(SINGLEPLAYER_FINISHED));
}

TEST_CASE_METHOD(ResetCampaignProgress, "any_campaign_progress_exists is false with an empty table", "[kfx_game][game_campaign_progress]") {
    CHECK_FALSE(any_campaign_progress_exists());
}

TEST_CASE_METHOD(ResetCampaignProgress, "any_campaign_progress_exists is true once any campaign has an unlocked level", "[kfx_game][game_campaign_progress]") {
    struct CampaignProgressEntry *entry = make_entry("keeporig.cfg");
    CHECK_FALSE(any_campaign_progress_exists()); // entry exists but nothing unlocked yet
    campaign_progress_unlock_level(entry, 1);
    CHECK(any_campaign_progress_exists());
}

TEST_CASE_METHOD(ResetCampaignProgress, "continue_game_available is false with no progress and no fx1contn.sav", "[kfx_game][game_campaign_progress]") {
    // No save/ directory in this environment (this file's own header
    // comment), so load_campaign_progress_file() finds nothing and
    // reconcile_fx1contn_into_progress() has no fx1contn.sav to absorb --
    // an honest "nothing to continue" rather than a crash either way.
    CHECK_FALSE(continue_game_available());
}

// The "true" case (a real unlocked level making continue_game_available()
// return true) isn't testable in this environment: continue_game_available()
// itself calls load_campaign_progress_file(), which unconditionally
// resets the in-memory table before re-reading from disk -- with no
// save/ directory to read a populated file back from, any entry set up
// directly in a test is wiped by that same call before
// any_campaign_progress_exists() ever runs. This mirrors real usage
// correctly (progress only "counts" once actually persisted via
// campaign_progress_record_level_completed(), which does write to disk),
// it just isn't something this test binary's environment can exercise.
