#include <catch2/catch_test_macros.hpp>

#include "cfgc_campaign_levels.h"
#include "cfgc_content.h"
#include "cfgc_schema.h"

namespace {

ConfigContent content_of(const std::string &text)
{
    return read_config_content(ConfigDocument::parse(text), "campaign", false);
}

std::string apply_changes(const std::string &text, const ChangeSet &cs)
{
    ConfigDocument doc = ConfigDocument::parse(text);
    cfgc_make_writer(build_engine_schema(), "campaign")->apply(doc, cs, nullptr);
    return doc.serialize();
}

} // namespace

TEST_CASE("lists are read with the bonus padded to the single levels", "[cfgc_campaign_levels]")
{
    const CampaignLevels lv = cfgc_read_levels(content_of("[common]\nSINGLE_LEVELS = 1 2 3\nBONUS_LEVELS = 0 7\nEXTRA_LEVELS = 9\n"));
    CHECK(lv.single == std::vector<int64_t>({1, 2, 3}));
    CHECK(lv.bonus == std::vector<int64_t>({0, 7, 0}));
    CHECK(lv.extra == std::vector<int64_t>({9}));
    CHECK(lv.listed(7));
    CHECK_FALSE(lv.listed(0));
    CHECK(lv.all().size() == 5);
}

TEST_CASE("list edits keep the single and bonus lists parallel", "[cfgc_campaign_levels]")
{
    CampaignLevels lv = cfgc_read_levels(content_of("[common]\nSINGLE_LEVELS = 1 2 3\nBONUS_LEVELS = 0 7 0\n"));
    cfgc_levels_add(lv, CampList_Single, 4);
    cfgc_levels_add(lv, CampList_Single, 2); // already listed
    cfgc_levels_add(lv, CampList_Extra, 50);
    cfgc_levels_add(lv, CampList_Extra, 7); // listed as a bonus level
    CHECK(lv.single == std::vector<int64_t>({1, 2, 3, 4}));
    CHECK(lv.bonus == std::vector<int64_t>({0, 7, 0, 0}));
    CHECK(lv.extra == std::vector<int64_t>({50}));

    cfgc_levels_move(lv, CampList_Single, 1, +1); // level 2 and its bonus level move together
    CHECK(lv.single == std::vector<int64_t>({1, 3, 2, 4}));
    CHECK(lv.bonus == std::vector<int64_t>({0, 0, 7, 0}));
    cfgc_levels_move(lv, CampList_Single, 0, -1); // off the top: nothing
    CHECK(lv.single[0] == 1);

    cfgc_levels_set_bonus(lv, 0, 8);
    cfgc_levels_set_bonus(lv, 1, 8); // 8 is listed now
    CHECK(lv.bonus == std::vector<int64_t>({8, 0, 7, 0}));

    cfgc_levels_remove(lv, CampList_Single, 2); // level 2 goes, its bonus level 7 with it
    CHECK(lv.single == std::vector<int64_t>({1, 3, 4}));
    CHECK(lv.bonus == std::vector<int64_t>({8, 0, 0}));
    cfgc_levels_remove(lv, CampList_Extra, 0);
    CHECK(lv.extra.empty());
}

TEST_CASE("new level numbers follow the highest listed", "[cfgc_campaign_levels]")
{
    CampaignLevels lv;
    CHECK(cfgc_next_level_number(lv, CampList_Single) == 1);
    CHECK(cfgc_next_level_number(lv, CampList_Bonus) == 100);
    lv = cfgc_read_levels(content_of("[common]\nSINGLE_LEVELS = 300 301\nBONUS_LEVELS = 0 105\n"));
    CHECK(cfgc_next_level_number(lv, CampList_Single) == 302);
    CHECK(cfgc_next_level_number(lv, CampList_Bonus) == 106);
}

TEST_CASE("a rewritten list keeps the file's column layout", "[cfgc_campaign_levels]")
{
    CHECK(cfgc_list_pitch("  300   301   302") == 6);
    CHECK(cfgc_list_pitch("1") == 0);
    CHECK(cfgc_format_level_list({300, 301}, 6) == "300   301");
    CHECK(cfgc_format_level_list({1, 2, 3}, 0) == "1 2 3");
    CHECK(cfgc_format_level_list({1000, 2}, 3) == "1000 2");

    const std::string text = "[common]\nNAME = X\nSINGLE_LEVELS =   300   301\nBONUS_LEVELS =      0     0\n";
    CampaignLevels lv = cfgc_read_levels(content_of(text));
    // Nothing changed: the file is untouched.
    CHECK(apply_changes(text, cfgc_levels_changes(content_of(text), lv)) == text);
    cfgc_levels_add(lv, CampList_Single, 302);
    const std::string out = apply_changes(text, cfgc_levels_changes(content_of(text), lv));
    CHECK(out.find("SINGLE_LEVELS =   300   301   302") != std::string::npos);
    CHECK(out.find("BONUS_LEVELS =      0     0     0") != std::string::npos);
    CHECK(out.find("EXTRA_LEVELS") == std::string::npos);
}

TEST_CASE("a new level's entry is a five-digit block with a working ensign", "[cfgc_campaign_levels]")
{
    const std::string out = apply_changes("[common]\nNAME = X\n", cfgc_default_entry(7, 0, "Level 7"));
    const ConfigContent c = content_of(out);
    const CfgContentSection *e = c.find_section("map", 7);
    REQUIRE(e != nullptr);
    CHECK(e->name == "map00007");
    CHECK(*e->last_value("NAME_TEXT") == "Level 7");
    CHECK(e->last_value("ENSIGN_POS") != nullptr);
    CHECK(*e->last_value("PLAYERS") == "1");
    // different slots give different positions
    CHECK(cfgc_default_entry(8, 1, "x").changes[1].values != cfgc_default_entry(7, 0, "x").changes[1].values);
}

TEST_CASE("adding a saved level to a campaign file", "[cfgc_campaign_levels]")
{
    const std::string text = "; c\n[common]\nNAME = X\nSINGLE_LEVELS = 1\nBONUS_LEVELS = 0\n\n[map00001]\nNAME_TEXT = One\n";
    bool changed = false;
    const std::string out = cfgc_campaign_add_level(text, 2, CampList_Single, "Two", &changed);
    CHECK(changed);
    const ConfigContent c = content_of(out);
    const CampaignLevels lv = cfgc_read_levels(c);
    CHECK(lv.single == std::vector<int64_t>({1, 2}));
    CHECK(lv.bonus == std::vector<int64_t>({0, 0}));
    REQUIRE(c.find_section("map", 2) != nullptr);
    CHECK(*c.find_section("map", 2)->last_value("NAME_TEXT") == "Two");
    CHECK(out.compare(0, 4, "; c\n") == 0);

    // Saving over a listed level with an entry changes nothing; an unlisted extra level goes to EXTRA_LEVELS.
    cfgc_campaign_add_level(out, 2, CampList_Single, "Two", &changed);
    CHECK_FALSE(changed);
    const CampaignLevels ex = cfgc_read_levels(content_of(cfgc_campaign_add_level(out, 100, CampList_Extra, "Bonus", nullptr)));
    CHECK(ex.extra == std::vector<int64_t>({100}));
    // A listed level that lost its entry gets one without a list change.
    const std::string bare = cfgc_campaign_add_level("[common]\nSINGLE_LEVELS = 5\n", 5, CampList_Single, "Five", nullptr);
    CHECK(cfgc_read_levels(content_of(bare)).single == std::vector<int64_t>({5}));
    CHECK(content_of(bare).find_section("map", 5) != nullptr);
}

TEST_CASE("removing a level from a campaign file", "[cfgc_campaign_levels]")
{
    const std::string text = "[common]\nSINGLE_LEVELS = 1 2 3\nBONUS_LEVELS = 0 7 0\nEXTRA_LEVELS = 50\n\n[map00002]\nNAME_TEXT = Two\n\n[map00007]\nNAME_TEXT = B\n";
    bool changed = false;
    const std::string a = cfgc_campaign_remove_level(text, 2, true, &changed);
    CHECK(changed);
    const CampaignLevels lv = cfgc_read_levels(content_of(a));
    CHECK(lv.single == std::vector<int64_t>({1, 3}));
    CHECK(lv.bonus == std::vector<int64_t>({0, 0}));
    CHECK(content_of(a).find_section("map", 2) == nullptr);
    // A bonus level only frees its slot; an extra level leaves the extra list (which is then dropped).
    const CampaignLevels b = cfgc_read_levels(content_of(cfgc_campaign_remove_level(text, 7, false, nullptr)));
    CHECK(b.single == std::vector<int64_t>({1, 2, 3}));
    CHECK(b.bonus == std::vector<int64_t>({0, 0, 0}));
    const std::string c = cfgc_campaign_remove_level(text, 50, false, nullptr);
    CHECK(c.find("EXTRA_LEVELS") == std::string::npos);
    cfgc_campaign_remove_level(text, 99, true, &changed);
    CHECK_FALSE(changed);
}
