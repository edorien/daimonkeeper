// kfx_config: config_strings.c's pure buffer/lookup functions. gui_string
// had no header declaration anywhere (only reachable from within the
// same file otherwise) -- added to config_strings.h, the usual "add the
// missing declaration" fix. get_string() itself (reaches through
// simport_get_level_strings() plus
// get_translation_file_string()) and the file-loading functions
// (load_gui_strings_data_from_file/load_campaign_strings_data_from_file,
// real file I/O + mod-list iteration) aren't attempted here.
#include <catch2/catch_test_macros.hpp>

#include "config_strings.h"
#include "config_campaigns.h" // struct GameCampaign / the `campaign` global

#include <cstring>

TEST_CASE("reset_strings points every slot (0..max inclusive) at a shared empty string", "[kfx_config][config_strings]") {
    char *strings[4] = {nullptr, nullptr, nullptr, nullptr};
    CHECK(reset_strings(strings, 3)); // max is inclusive: touches indices 0..3
    for (int64_t i = 0; i < 4; i++) {
        REQUIRE(strings[i] != nullptr);
        CHECK(strings[i][0] == '\0');
    }
}

TEST_CASE("fill_strings_list points each slot at the start of its NUL-terminated entry in strings_data", "[kfx_config][config_strings]") {
    char data[] = "first\0second\0third\0";
    char *strings[3] = {nullptr, nullptr, nullptr};
    fill_strings_list(strings, data, data + sizeof(data), 2);
    CHECK(std::strcmp(strings[0], "first") == 0);
    CHECK(std::strcmp(strings[1], "second") == 0);
    CHECK(std::strcmp(strings[2], "third") == 0);
}

TEST_CASE("fill_strings_list does not overwrite a slot with an empty entry (mod-compatible: keeps the prior value)", "[kfx_config][config_strings]") {
    char data[] = "\0second\0";
    char *strings[2] = {const_cast<char*>("preexisting"), nullptr};
    fill_strings_list(strings, data, data + sizeof(data), 1);
    CHECK(std::strcmp(strings[0], "preexisting") == 0); // empty entry skipped
    CHECK(std::strcmp(strings[1], "second") == 0);
}

TEST_CASE("fill_strings_list returns true once it runs out of source data before max", "[kfx_config][config_strings]") {
    char data[] = "only\0";
    char *strings[3] = {nullptr, nullptr, nullptr};
    CHECK(fill_strings_list(strings, data, data + sizeof(data), 2)); // max=2 (3 slots) but only 1 entry available
}

TEST_CASE("count_strings counts the number of NUL terminators in the given range (inclusive of the end byte)", "[kfx_config][config_strings]") {
    // count_strings reads through strings[size] inclusive (the loop
    // condition is `s <= end` where end = strings + size), so the buffer
    // needs one byte of slack past `size` to stay in-bounds -- 'X' here
    // is that slack byte, deliberately not itself a NUL.
    char data[7] = {'a', 'b', '\0', 'c', 'd', '\0', 'X'};
    CHECK(count_strings(data, 6) == 2);
}

namespace {
struct ResetGuiStrings {
    ResetGuiStrings() {
        for (auto &s : gui_strings) s = const_cast<char*>("");
    }
};
}

TEST_CASE_METHOD(ResetGuiStrings, "gui_string returns the registered string for an in-range index", "[kfx_config][config_strings]") {
    gui_strings[5] = const_cast<char*>("Hello");
    CHECK(std::strcmp(gui_string(5), "Hello") == 0);
}

TEST_CASE_METHOD(ResetGuiStrings, "gui_string synthesizes an untranslated placeholder for an out-of-range index", "[kfx_config][config_strings]") {
    uint64_t out_of_range = GUI_STRINGS_COUNT + 5;
    std::string result = gui_string(out_of_range);
    CHECK(result.find("untranslated") != std::string::npos);
}

namespace {
struct ResetCampaignStrings {
    ResetCampaignStrings() {
        std::memset(&campaign, 0, sizeof(campaign));
        for (auto &s : campaign.strings) s = const_cast<char*>("");
        for (auto &s : gui_strings) s = const_cast<char*>("");
    }
};
}

TEST_CASE_METHOD(ResetCampaignStrings, "cmpgn_string prefers the campaign's own string when set", "[kfx_config][config_strings]") {
    campaign.strings[10] = const_cast<char*>("Campaign override");
    CHECK(std::strcmp(cmpgn_string(10), "Campaign override") == 0);
}

TEST_CASE_METHOD(ResetCampaignStrings, "cmpgn_string falls back to gui_string when the campaign's own slot is empty", "[kfx_config][config_strings]") {
    gui_strings[10] = const_cast<char*>("GUI fallback");
    CHECK(std::strcmp(cmpgn_string(10), "GUI fallback") == 0);
}

TEST_CASE_METHOD(ResetCampaignStrings, "cmpgn_string routes an index at or past STRINGS_MAX straight to gui_string", "[kfx_config][config_strings]") {
    gui_strings[3] = const_cast<char*>("Direct GUI string");
    CHECK(std::strcmp(cmpgn_string(STRINGS_MAX + 3), "Direct GUI string") == 0);
}
