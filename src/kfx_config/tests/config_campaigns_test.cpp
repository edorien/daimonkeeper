// kfx_config: config_campaigns.c -- previously completely untested (0/804
// lines) despite testing-harness.md's per-library table implying campaign
// coverage existed; that coverage was actually is_bonus_level() and
// friends living in config.c, a different file. This file's own
// struct-management functions (level/campaign array append, grow,
// clear, swap, sort, lookup) are all pattern-A/real-KfxAlloc-family
// pure logic, no config_reload_callbacks fake needed -- unlike
// parse_campaign_*_blocks()/load_campaign()/load_campaigns_list(), which
// need a real .cfg fixture file and the find_and_load_lif/lof_files
// callback pair, not attempted here.
//
// struct GameCampaign is large (multi_levels[1000] + freeplay_levels[5000]
// alone are ~24KB) but that's exactly the production shape -- local
// stack instances are used here the same way production code uses them.
#include <catch2/catch_test_macros.hpp>

#include "config_campaigns.h"
#include "kfx_memory.h" // KfxFree, used directly to release lvinfos/items after each test

#include <cstring>

namespace {
struct ZeroedCampaign {
    ZeroedCampaign() { std::memset(&campgn, 0, sizeof(campgn)); }
    struct GameCampaign campgn;
};

struct ResetGlobalCampaign {
    ResetGlobalCampaign() { std::memset(&campaign, 0, sizeof(campaign)); }
};
}

TEST_CASE_METHOD(ZeroedCampaign, "clear_campaign resets counts, indices, and the human_player sentinel", "[kfx_config][config_campaigns]") {
    campgn.single_levels_count = 5;
    campgn.bonus_levels_index = 3;
    campgn.lvinfos = reinterpret_cast<struct LevelInformation*>(0x1234); // not freed, just overwritten
    campgn.human_player = 7;
    CHECK(clear_campaign(&campgn));
    CHECK(campgn.single_levels_count == 0);
    CHECK(campgn.bonus_levels_index == 0);
    CHECK(campgn.lvinfos == nullptr);
    CHECK(campgn.human_player == -1);
    CHECK(campgn.land_markers == LndMk_ENSIGNS);
}

TEST_CASE_METHOD(ZeroedCampaign, "free_campaign is safe to call on an all-NULL campaign and resets strings_data_count", "[kfx_config][config_campaigns]") {
    campgn.strings_data_count = 0;
    CHECK(free_campaign(&campgn));
    CHECK(campgn.strings_data_count == 0);
}

TEST_CASE("clear_level_info resets a level entry to its documented defaults", "[kfx_config][config_campaigns]") {
    struct LevelInformation lvinfo;
    std::memset(&lvinfo, 0xAB, sizeof(lvinfo)); // start from garbage, not zero
    clear_level_info(&lvinfo);
    CHECK(lvinfo.lvnum == 0);
    CHECK(lvinfo.players == 1);
    CHECK(lvinfo.level_type == LvKind_None);
    CHECK(lvinfo.state == LvSt_Hidden);
    CHECK(lvinfo.mapsize_x == 85); // CAMPAIGNS_DEFAULT_MAP_SIZE, duplicated from kfx_sim's map_data.h
    CHECK(lvinfo.mapsize_y == 85);
    CHECK(lvinfo.skirmish_setup == SkirmishSetup_Auto); // .lof SKIRMISH_SETUP absent = classify automatically
}

TEST_CASE_METHOD(ZeroedCampaign, "add_single_level_to_campaign appends and returns the slot index", "[kfx_config][config_campaigns]") {
    CHECK(add_single_level_to_campaign(&campgn, 10) == 0);
    CHECK(add_single_level_to_campaign(&campgn, 20) == 1);
    CHECK(campgn.single_levels[0] == 10);
    CHECK(campgn.single_levels[1] == 20);
    CHECK(campgn.single_levels_count == 2);
}

TEST_CASE_METHOD(ZeroedCampaign, "add_single_level_to_campaign rejects a non-positive level number", "[kfx_config][config_campaigns]") {
    CHECK(add_single_level_to_campaign(&campgn, 0) == LEVELNUMBER_ERROR);
    CHECK(add_single_level_to_campaign(&campgn, -5) == LEVELNUMBER_ERROR);
    CHECK(campgn.single_levels_count == 0);
}

TEST_CASE_METHOD(ZeroedCampaign, "add_single_level_to_campaign returns LEVELNUMBER_ERROR once CAMPAIGN_LEVELS_COUNT is reached", "[kfx_config][config_campaigns]") {
    campgn.single_levels_count = CAMPAIGN_LEVELS_COUNT;
    CHECK(add_single_level_to_campaign(&campgn, 1) == LEVELNUMBER_ERROR);
}

TEST_CASE_METHOD(ZeroedCampaign, "add_multi_level_to_campaign appends into the multi_levels array", "[kfx_config][config_campaigns]") {
    CHECK(add_multi_level_to_campaign(&campgn, 42) == 0);
    CHECK(campgn.multi_levels[0] == 42);
    CHECK(campgn.multi_levels_count == 1);
}

TEST_CASE_METHOD(ZeroedCampaign, "add_bonus_level_to_campaign always advances bonus_levels_index but only counts nonzero entries", "[kfx_config][config_campaigns]") {
    CHECK(add_bonus_level_to_campaign(&campgn, 0) == 0); // a placeholder "no bonus for this slot" entry
    CHECK(campgn.bonus_levels_index == 1);
    CHECK(campgn.bonus_levels_count == 0); // lvnum 0 doesn't count as a real bonus level

    CHECK(add_bonus_level_to_campaign(&campgn, 99) == 1);
    CHECK(campgn.bonus_levels_index == 2);
    CHECK(campgn.bonus_levels_count == 1);
}

TEST_CASE_METHOD(ZeroedCampaign, "add_bonus_level_to_campaign clamps a negative level number to 0 rather than rejecting it", "[kfx_config][config_campaigns]") {
    CHECK(add_bonus_level_to_campaign(&campgn, -7) == 0);
    CHECK(campgn.bonus_levels[0] == 0);
}

TEST_CASE_METHOD(ZeroedCampaign, "add_extra_level_to_campaign mirrors add_bonus_level_to_campaign's always-advance/nonzero-counts shape", "[kfx_config][config_campaigns]") {
    CHECK(add_extra_level_to_campaign(&campgn, 0) == 0);
    CHECK(campgn.extra_levels_count == 0);
    CHECK(add_extra_level_to_campaign(&campgn, 5) == 1);
    CHECK(campgn.extra_levels_count == 1);
}

TEST_CASE_METHOD(ZeroedCampaign, "add_freeplay_level_to_campaign de-duplicates: adding the same level twice returns the same slot", "[kfx_config][config_campaigns]") {
    int64_t first = add_freeplay_level_to_campaign(&campgn, 30);
    int64_t second = add_freeplay_level_to_campaign(&campgn, 30);
    CHECK(first == second);
    CHECK(campgn.freeplay_levels_count == 1);
}

TEST_CASE_METHOD(ZeroedCampaign, "add_freeplay_level_to_campaign rejects a non-positive level number", "[kfx_config][config_campaigns]") {
    CHECK(add_freeplay_level_to_campaign(&campgn, 0) == LEVELNUMBER_ERROR);
}

TEST_CASE_METHOD(ZeroedCampaign, "init_level_info_entries allocates and default-initializes the requested number of slots", "[kfx_config][config_campaigns]") {
    CHECK(init_level_info_entries(&campgn, 4));
    REQUIRE(campgn.lvinfos != nullptr);
    CHECK(campgn.lvinfos_count == 4);
    CHECK(campgn.lvinfos[0].players == 1); // clear_level_info's default
    KfxFree(campgn.lvinfos);
}

TEST_CASE_METHOD(ZeroedCampaign, "grow_level_info_entries extends an existing allocation, initializing only the new slots", "[kfx_config][config_campaigns]") {
    init_level_info_entries(&campgn, 2);
    campgn.lvinfos[0].lvnum = 99; // mark the original slot so we can tell it wasn't reset
    CHECK(grow_level_info_entries(&campgn, 3));
    CHECK(campgn.lvinfos_count == 5);
    CHECK(campgn.lvinfos[0].lvnum == 99); // untouched
    CHECK(campgn.lvinfos[2].lvnum == 0);  // new slot, cleared
    KfxFree(campgn.lvinfos);
}

TEST_CASE_METHOD(ZeroedCampaign, "get_campaign_level_info finds a level by number and returns NULL when absent", "[kfx_config][config_campaigns]") {
    init_level_info_entries(&campgn, 2);
    campgn.lvinfos[1].lvnum = 55;
    CHECK(get_campaign_level_info(&campgn, 55) == &campgn.lvinfos[1]);
    CHECK(get_campaign_level_info(&campgn, 999) == nullptr);
    CHECK(get_campaign_level_info(&campgn, 0) == nullptr); // lvnum <= 0 guard
    KfxFree(campgn.lvinfos);
}

TEST_CASE_METHOD(ZeroedCampaign, "new_level_info_entry reuses an empty (lvnum<=0) slot before growing the array", "[kfx_config][config_campaigns]") {
    init_level_info_entries(&campgn, 2);
    struct LevelInformation *entry = new_level_info_entry(&campgn, 77);
    REQUIRE(entry != nullptr);
    CHECK(entry == &campgn.lvinfos[0]);
    CHECK(entry->lvnum == 77);
    CHECK(campgn.lvinfos_count == 2); // no growth needed, an empty slot existed
    KfxFree(campgn.lvinfos);
}

TEST_CASE_METHOD(ZeroedCampaign, "new_level_info_entry grows the array once every existing slot is occupied", "[kfx_config][config_campaigns]") {
    init_level_info_entries(&campgn, 1);
    campgn.lvinfos[0].lvnum = 1; // occupy the only slot
    struct LevelInformation *entry = new_level_info_entry(&campgn, 88);
    REQUIRE(entry != nullptr);
    CHECK(entry->lvnum == 88);
    CHECK(campgn.lvinfos_count == 1 + LEVEL_INFO_GROW_DELTA);
    KfxFree(campgn.lvinfos);
}

TEST_CASE_METHOD(ZeroedCampaign, "new_level_info_entry returns NULL when lvinfos hasn't been allocated yet", "[kfx_config][config_campaigns]") {
    CHECK(new_level_info_entry(&campgn, 1) == nullptr);
}

TEST_CASE_METHOD(ResetGlobalCampaign, "is_campaign_loaded is false when no campaign filename is set", "[kfx_config][config_campaigns]") {
    CHECK_FALSE(is_campaign_loaded());
}

TEST_CASE_METHOD(ResetGlobalCampaign, "is_campaign_loaded is true once a campaign is named and has at least one level", "[kfx_config][config_campaigns]") {
    std::strcpy(campaign.fname, "mycampaign.cfg");
    campaign.single_levels_count = 1;
    CHECK(is_campaign_loaded());
}

TEST_CASE_METHOD(ResetGlobalCampaign, "is_map_pack is true only when freeplay levels exist and single/multi levels don't", "[kfx_config][config_campaigns]") {
    std::strcpy(campaign.fname, "mypack.cfg");
    campaign.freeplay_levels_count = 3;
    CHECK(is_map_pack());
    campaign.single_levels_count = 1;
    CHECK_FALSE(is_map_pack()); // no longer a "pure" map pack once a single level exists
}

TEST_CASE("init_campaigns_list_entries allocates the requested number of cleared GameCampaign slots", "[kfx_config][config_campaigns]") {
    struct CampaignsList clist{};
    CHECK(init_campaigns_list_entries(&clist, 3));
    REQUIRE(clist.items != nullptr);
    CHECK(clist.items_count == 3);
    CHECK(clist.items_num == 0);
    CHECK(clist.items[0].human_player == -1); // clear_campaign's default
    KfxFree(clist.items);
}

TEST_CASE("grow_campaigns_list_entries extends the list without disturbing existing entries' names", "[kfx_config][config_campaigns]") {
    struct CampaignsList clist{};
    init_campaigns_list_entries(&clist, 1);
    std::strcpy(clist.items[0].name, "Original");
    CHECK(grow_campaigns_list_entries(&clist, 2));
    CHECK(clist.items_count == 3);
    CHECK(std::strcmp(clist.items[0].name, "Original") == 0);
    KfxFree(clist.items);
}

TEST_CASE("swap_campaigns_in_list exchanges two entries and rejects out-of-range indices", "[kfx_config][config_campaigns]") {
    struct CampaignsList clist{};
    init_campaigns_list_entries(&clist, 2);
    clist.items_num = 2;
    std::strcpy(clist.items[0].name, "First");
    std::strcpy(clist.items[1].name, "Second");
    CHECK(swap_campaigns_in_list(&clist, 0, 1));
    CHECK(std::strcmp(clist.items[0].name, "Second") == 0);
    CHECK(std::strcmp(clist.items[1].name, "First") == 0);
    CHECK_FALSE(swap_campaigns_in_list(&clist, 0, 5)); // idx2 out of range
    CHECK_FALSE(swap_campaigns_in_list(&clist, -1, 0)); // idx1 negative
    KfxFree(clist.items);
}

TEST_CASE("sort_campaigns_quicksort orders entries case-insensitively by name", "[kfx_config][config_campaigns]") {
    struct CampaignsList clist{};
    init_campaigns_list_entries(&clist, 3);
    clist.items_num = 3;
    std::strcpy(clist.items[0].name, "Charlie");
    std::strcpy(clist.items[1].name, "alpha");
    std::strcpy(clist.items[2].name, "Bravo");
    sort_campaigns_quicksort(&clist, 0, 3);
    CHECK(std::strcmp(clist.items[0].name, "alpha") == 0);
    CHECK(std::strcmp(clist.items[1].name, "Bravo") == 0);
    CHECK(std::strcmp(clist.items[2].name, "Charlie") == 0);
    KfxFree(clist.items);
}

TEST_CASE("is_campaign_in_list matches by fname case-insensitively", "[kfx_config][config_campaigns]") {
    struct CampaignsList clist{};
    init_campaigns_list_entries(&clist, 2);
    clist.items_num = 2;
    std::strcpy(clist.items[0].fname, "keeporig.cfg");
    CHECK(is_campaign_in_list("KEEPORIG.CFG", &clist));
    CHECK_FALSE(is_campaign_in_list("nope.cfg", &clist));
    KfxFree(clist.items);
}

TEST_CASE("is_campaign_in_list is false for an empty or unallocated list", "[kfx_config][config_campaigns]") {
    struct CampaignsList clist{};
    CHECK_FALSE(is_campaign_in_list("anything.cfg", &clist));
}
