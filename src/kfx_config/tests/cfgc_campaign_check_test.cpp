#include <catch2/catch_test_macros.hpp>

#include "cfgc_campaign_check.h"
#include "cfgc_campaign_edit.h"
#include "cfgc_content.h"
#include "kfx_config_test_paths.h" // KFX_CONFIG_TEST_REPO_ROOT

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <cstdlib>
#include <sstream>

namespace fs = std::filesystem;

namespace {

CampaignCheckEnv memory_env(std::set<std::string> files, std::set<std::string> dirs)
{
    CampaignCheckEnv env;
    env.root = "root";
    env.own_fname = "mine.cfg";
    env.file_exists = [files](const std::string &p) { return files.count(p) != 0; };
    env.dir_exists = [dirs](const std::string &p) { return dirs.count(p) != 0; };
    env.file_size = [files](const std::string &p) { return files.count(p) ? (int64_t)100 : (int64_t)-1; };
    return env;
}

bool has(const std::vector<CfgDiagnostic> &d, const char *code)
{
    return std::any_of(d.begin(), d.end(), [&](const CfgDiagnostic &x) { return x.code == code; });
}

} // namespace

TEST_CASE("level lists parse to numbers", "[cfgc_campaign_check]")
{
    CHECK(cfgc_parse_level_list("  300   301 x 302 ") == std::vector<int64_t>({300, 301, 302}));
    CHECK(cfgc_parse_level_list("").empty());
}

TEST_CASE("a clean campaign has no findings", "[cfgc_campaign_check]")
{
    const ConfigSchema schema = build_engine_schema();
    const ConfigDocument doc = ConfigDocument::parse(
        "[common]\nNAME = X\nLEVELS_LOCATION = c/lv\nSINGLE_LEVELS = 1 2\nBONUS_LEVELS = 0 3\nHUMAN_PLAYER = RED\n"
        "[map00001]\nNAME_TEXT = A\n[map00002]\nNAME_TEXT = B\n[map00003]\nNAME_TEXT = C\n");
    const CampaignCheckEnv env = memory_env({"root/c/lv/map00001.slb", "root/c/lv/map00002.slb", "root/c/lv/map00003.slb"}, {"root/c/lv"});
    CHECK(cfgc_check_campaign(doc, schema, env).empty());
}

TEST_CASE("list problems are errors", "[cfgc_campaign_check]")
{
    const ConfigSchema schema = build_engine_schema();
    const ConfigDocument doc = ConfigDocument::parse(
        "[common]\nLEVELS_LOCATION = c/lv\nSINGLE_LEVELS = 1 2 2\nBONUS_LEVELS = 0 3\nEXTRA_LEVELS = 1\nHUMAN_PLAYER = PINK\n");
    const CampaignCheckEnv env = memory_env({"root/c/lv/map00001.slb"}, {"root/c/lv"});
    const auto d = cfgc_check_campaign(doc, schema, env);
    CHECK(has(d, "level_duplicate"));
    CHECK(has(d, "human_player"));
    CHECK(has(d, "level_files_missing"));
}

TEST_CASE("entries, locations, sharing and strings", "[cfgc_campaign_check]")
{
    const ConfigSchema schema = build_engine_schema();
    const ConfigDocument doc = ConfigDocument::parse(
        "[common]\nLEVELS_LOCATION = gone\nCONFIGS_LOCATION = c/cfg\nSINGLE_LEVELS = 1\nLAND_LOCATION = c/lnd\nLAND_VIEW_START = rg v\n"
        "[strings]\nENG = c/t.dat\n[map00007]\nNAME_TEXT = A\n\x1a[map00008]\nNAME_TEXT = B\n");
    CampaignCheckEnv env = memory_env({}, {"root/c/lnd"});
    env.peers.push_back({"Other", "other.cfg", "root/c/cfg", ""});
    env.peers.push_back({"Mine", "mine.cfg", "root/c/cfg", ""});
    const auto d = cfgc_check_campaign(doc, schema, env);
    CHECK(has(d, "location_missing"));
    CHECK(has(d, "location_shared"));
    CHECK(has(d, "entry_unused"));      // [map00007]
    CHECK(has(d, "entry_no_level_list")); // level 1 has no entry
    CHECK(has(d, "not_a_header"));
    CHECK(has(d, "landview_missing"));
    CHECK(has(d, "strings_missing"));
    // the shared note names the other campaign only
    for (const CfgDiagnostic &x : d)
        if (x.code == "location_shared")
        {
            CHECK(x.message.find("Other") != std::string::npos);
            CHECK(x.message.find("Mine") == std::string::npos);
        }
}

namespace {

std::string slurp(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string value_of(const ConfigDocument &doc, const char *key)
{
    const ConfigContent c = read_config_content(doc, "campaign", false);
    if (const CfgContentSection *s = c.find_section("common"))
        if (const std::string *v = s->last_value(key))
            return *v;
    return std::string();
}

} // namespace

// The findings over every shipped campaign are committed: a new false positive (or a fixed file) shows in review.
TEST_CASE("the shipped campaigns' findings match the committed snapshot", "[cfgc_campaign_check][corpus]")
{
    const ConfigSchema schema = build_engine_schema();
    const fs::path root = fs::path(KFX_CONFIG_TEST_REPO_ROOT) / "core_files";
    struct Item { std::string name; std::string fname; ConfigDocument doc; };
    std::vector<Item> items;
    CampaignCheckEnv env;
    env.root = root.string();
    env.file_exists = [](const std::string &p) { return cfgc_file_exists_ci(p); };
    env.dir_exists = [](const std::string &p) { return fs::is_directory(p); };
    env.file_size = [](const std::string &p) { std::error_code ec; const auto n = fs::file_size(p, ec); return ec ? (int64_t)-1 : (int64_t)n; };
    std::vector<fs::path> files;
    for (const fs::directory_entry &e : fs::directory_iterator(root / "campgns"))
        if (e.is_regular_file() && e.path().extension() == ".cfg")
            files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const fs::path &p : files)
    {
        Item it{p.stem().string(), p.filename().string(), ConfigDocument::parse(slurp(p))};
        env.peers.push_back({it.name, it.fname, env.root + "/" + value_of(it.doc, "CONFIGS_LOCATION"), env.root + "/" + value_of(it.doc, "CREATURES_LOCATION")});
        items.push_back(std::move(it));
    }
    REQUIRE(items.size() >= 10);

    std::ostringstream report;
    for (Item &it : items)
    {
        env.own_fname = it.fname;
        for (const CfgDiagnostic &d : cfgc_check_campaign(it.doc, schema, env))
            // Checks against files the repository does not ship (the original game's maps, art, text) depend on the data tree.
            if (d.severity >= CfgSev_Warning && d.code != "level_files_missing" && d.code != "strings_missing"
                && d.code != "landview_missing" && d.code != "location_missing" && d.code != "speech_missing")
                report << it.fname << ":" << d.line << " " << d.code << ": " << d.message << "\n";
    }
    const fs::path snap = fs::path(KFX_CONFIG_TEST_REPO_ROOT) / "src" / "kfx_config" / "tests" / "fixtures" / "campaign_check_snapshot.txt";
    if (std::getenv("KFX_UPDATE_SNAPSHOT") != nullptr)
        std::ofstream(snap, std::ios::binary) << report.str();
    CHECK(report.str() == slurp(snap));
}

#include "cfgc_campaign_edit.h"

TEST_CASE("a campaign's own location keeps the parent folder", "[cfgc_campaign_check]")
{
    CHECK(cfgc_own_location("campgns/keeporig_cfg", "mine.cfg", "cfg") == "campgns/mine_cfg");
    CHECK(cfgc_own_location("levels\\classic_crtr/", "Pack.cfg", "crtr") == "levels/Pack_crtr");
    CHECK(cfgc_own_location("shared", "x.cfg", "cfg") == "x_cfg");
}

TEST_CASE("a folder copy is staged whole, and refuses an existing target", "[cfgc_campaign_check]")
{
    const fs::path tmp = fs::temp_directory_path() / "kfx_copy_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp / "src" / "sub");
    std::ofstream(tmp / "src" / "a.cfg", std::ios::binary) << "one";
    std::ofstream(tmp / "src" / "sub" / "b.bin", std::ios::binary) << std::string("x\0y", 3);

    WriteBatch batch;
    std::string err;
    REQUIRE(cfgc_plan_folder_copy((tmp / "src").string(), (tmp / "dst").string(), batch, &err));
    REQUIRE(batch.ops().size() == 2);
    CHECK_FALSE(fs::exists(tmp / "dst")); // staged only
    REQUIRE(batch.commit(&err));
    CHECK(fs::file_size(tmp / "dst" / "sub" / "b.bin") == 3);

    WriteBatch again;
    CHECK_FALSE(cfgc_plan_folder_copy((tmp / "src").string(), (tmp / "dst").string(), again, &err));
    CHECK(again.empty());
    CHECK_FALSE(cfgc_plan_folder_copy((tmp / "nope").string(), (tmp / "d2").string(), again, &err));
    fs::remove_all(tmp);
}

TEST_CASE("a new campaign's file names, reads back and checks clean", "[cfgc_campaign_check]")
{
    CHECK(cfgc_campaign_id_from_name("My  Great Campaign!") == "My_Great_Campaign");
    CHECK(cfgc_campaign_id_from_name("  ") == "campaign");
    CHECK(cfgc_campaign_id_from_name("caf\xC3\xA9") == "caf");

    const std::string text = cfgc_new_campaign_text("My Campaign", "My_Campaign", "RED", true, true);
    const ConfigSchema schema = build_engine_schema();
    const ConfigDocument doc = ConfigDocument::parse(text);
    CampaignCheckEnv env = memory_env({"root/campgns/My_Campaign/map00001.slb", "root/campgns/My_Campaign_lnd/rgmap00.raw", "root/campgns/My_Campaign_lnd/rgmap00.pal", "root/fxdata/gtext_eng.dat"}, {"root/campgns/My_Campaign", "root/campgns/My_Campaign_cfg", "root/campgns/My_Campaign_crtr", "root/campgns/My_Campaign_lnd"});
    env.file_size = [](const std::string &p) { return p.size() > 4 && p.compare(p.size() - 4, 4, ".pal") == 0 ? (int64_t)768 : (int64_t)100; };
    for (const CfgDiagnostic &d : cfgc_check_campaign(doc, schema, env))
        CHECK(d.severity < CfgSev_Warning);
    const ConfigContent c = read_config_content(doc, "campaign", false);
    REQUIRE(c.find_section("common") != nullptr);
    CHECK(*c.find_section("common")->last_value("NAME") == "My Campaign");
    CHECK(c.find_section("common")->last_value("CONFIGS_LOCATION") != nullptr);
    CHECK(c.find_section("strings") != nullptr); // the game does not load a campaign without these two blocks
    CHECK(c.find_section("speech") != nullptr);
    CHECK(c.find_section("map", 1) != nullptr); // level 1 is listed, so the game lists the campaign
    CHECK(read_config_content(ConfigDocument::parse(cfgc_new_campaign_text("X", "X", "BLUE", false, false)), "campaign", false)
              .find_section("common")->last_value("CONFIGS_LOCATION") == nullptr);
}

TEST_CASE("land view images: PNG size, raw size and palette are checked", "[cfgc_campaign_check]")
{
    const ConfigSchema schema = build_engine_schema();
    const ConfigDocument doc = ConfigDocument::parse("[common]\nLAND_LOCATION = lnd\nLAND_VIEW_START = a v\nLAND_VIEW_END = b v\n");
    CampaignCheckEnv env = memory_env({"root/lnd/a.png", "root/lnd/b.raw", "root/lnd/b.pal"}, {"root/lnd"});
    auto png_head = [](uint32_t w, uint32_t h) {
        std::string s = "\x89PNG\r\n\x1a\n\0\0\0\x0d" "IHDR";
        s.resize(8 + 8);
        for (uint32_t v : {w, h})
            for (int sh = 24; sh >= 0; sh -= 8)
                s += (char)((v >> sh) & 0xff);
        return s;
    };
    env.read_prefix = [&](const std::string &, size_t) { return png_head(640, 480); };
    auto d = cfgc_check_campaign(doc, schema, env);
    CHECK(has(d, "landview_size")); // a.png is 640 x 480; b.pal is 100 bytes (memory_env); b.raw's size is not checked
    int sizes = 0;
    for (const CfgDiagnostic &x : d)
        sizes += x.code == "landview_size";
    CHECK(sizes == 2);
    env.read_prefix = [&](const std::string &, size_t) { return png_head(1280, 960); };
    sizes = 0;
    for (const CfgDiagnostic &x : cfgc_check_campaign(doc, schema, env))
        sizes += x.code == "landview_size";
    CHECK(sizes == 1); // only the palette remains
}

TEST_CASE("the campaign menu order file keeps its comment and its line endings", "[cfgc_campaign_check]")
{
    const std::vector<std::string> order = {"a.cfg", "b.cfg", "c.cfg"};
    CHECK(cfgc_move_in_order(order, 1, -1) == std::vector<std::string>({"b.cfg", "a.cfg", "c.cfg"}));
    CHECK(cfgc_move_in_order(order, 2, +1) == order);
    CHECK(cfgc_move_in_order(order, 0, -1) == order);
    CHECK(cfgc_order_file_text("#order\r\na.cfg\r\nb.cfg\r\n", {"b.cfg", "a.cfg"}) == "#order\r\nb.cfg\r\na.cfg\r\n");
    CHECK(cfgc_order_file_text("", {"x.cfg"}) == "x.cfg\n");
}

TEST_CASE("per-level land view and speech entries are checked", "[cfgc_campaign_check]")
{
    const ConfigSchema schema = build_engine_schema();
    const ConfigDocument doc = ConfigDocument::parse(
        "[common]\nLAND_LOCATION = lnd\nSINGLE_LEVELS = 1\n[speech]\nENG = sp\n"
        "[map00001]\nLAND_VIEW = gone\nSPEECH = a.mp3\n[map00002]\nLAND_VIEW = here viframe00\nSPEECH = ok.mp3 ok2.mp3\n");
    const CampaignCheckEnv env = memory_env({"root/lnd/here.raw", "root/sp/ok.mp3"}, {"root/lnd", "root/sp"});
    const auto d = cfgc_check_campaign(doc, schema, env);
    CHECK(has(d, "landview_missing"));
    CHECK(has(d, "landview_frame"));
    CHECK(has(d, "speech_pair"));
    int missing = 0;
    for (const CfgDiagnostic &x : d)
        missing += x.code == "speech_missing";
    CHECK(missing == 2); // a.mp3 and ok2.mp3
}

TEST_CASE("level files are found and copied under a new number, in any letter case", "[cfgc_campaign_check]")
{
    const fs::path tmp = fs::temp_directory_path() / "kfx_level_copy_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp / "src");
    fs::create_directories(tmp / "dst");
    for (const char *n : {"MAP00007.SLB", "MAP00007.TNG", "map00007.rules.cfg", "map00070.slb", "map00007x.slb"})
        std::ofstream(tmp / "src" / n, std::ios::binary) << n;
    std::ofstream(tmp / "dst" / "map00009.slb") << "x";

    CHECK(cfgc_level_files((tmp / "src").string(), 7).size() == 3);
    CHECK(cfgc_level_numbers_in_dir((tmp / "src").string()) == std::vector<int64_t>({7, 70}));
    CHECK(cfgc_file_exists_ci((tmp / "src" / "map00007.slb").string()));
    CHECK_FALSE(cfgc_file_exists_ci((tmp / "src" / "map00008.slb").string()));
    CHECK(cfgc_level_file_rename("MAP00007.SLB", 7, 12) == "MAP00012.SLB");
    CHECK(cfgc_level_file_rename("map00007.rules.cfg", 7, 12) == "map00012.rules.cfg");
    CHECK(cfgc_level_file_rename("other.cfg", 7, 12) == "other.cfg");

    WriteBatch batch;
    std::string err;
    REQUIRE(cfgc_plan_level_copy((tmp / "src").string(), 7, (tmp / "dst").string(), 12, batch, &err));
    CHECK(batch.ops().size() == 3);
    CHECK_FALSE(fs::exists(tmp / "dst" / "MAP00012.SLB")); // staged only
    REQUIRE(batch.commit(&err));
    CHECK(fs::exists(tmp / "dst" / "MAP00012.SLB"));
    CHECK(fs::exists(tmp / "dst" / "map00012.rules.cfg"));

    WriteBatch again;
    CHECK_FALSE(cfgc_plan_level_copy((tmp / "src").string(), 7, (tmp / "dst").string(), 12, again, &err)); // taken
    CHECK_FALSE(cfgc_plan_level_copy((tmp / "src").string(), 8, (tmp / "dst").string(), 13, again, &err)); // no source
    CHECK(again.empty());
    fs::remove_all(tmp);
}
