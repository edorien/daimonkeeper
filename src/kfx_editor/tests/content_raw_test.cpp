// Catch2 coverage for content_target.cpp / content_raw.cpp (plan 03 F3).
#include <catch2/catch_test_macros.hpp>

#include "content_raw.h"
#include "content_target.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

void spit(const fs::path &p, const std::string &s)
{
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << s;
}

std::string slurp(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

struct Tree
{
    fs::path root;
    Tree()
    {
        root = fs::temp_directory_path() / "kfx_content_raw_test";
        fs::remove_all(root);
        spit(root / "fxdata" / "trapdoor.cfg", "[trap1]\nName = A\nHealth = 7\nShots = 2\n");
        spit(root / "fxdata" / "notes.toml", "x = 1\n");
        spit(root / "creatrs" / "imp.cfg", "[attributes]\nHealth = 1\n");
        spit(root / "camp" / "trapdoor.cfg", "[trap1]\nHealth = 20\n");
    }
    ~Tree() { fs::remove_all(root); }
    ConfigTarget target(bool with_level) const
    {
        ContentCampaign c;
        c.cfg_dir = (root / "camp").string();
        c.crtr_dir = (root / "campcrtr").string();
        c.levels_dir = (root / "lvls").string();
        return content_target_make((root / "fxdata").string(), (root / "creatrs").string(), &c, with_level ? 3 : -1);
    }
};

RawFileEntry find(const std::vector<RawFileEntry> &files, const std::string &name)
{
    for (const RawFileEntry &f : files)
        if (f.name == name)
            return f;
    FAIL("no file " << name);
    return RawFileEntry();
}

} // namespace

TEST_CASE("target builders join locations and keep layers optional", "[content_raw]")
{
    CHECK(content_resolve_location("/g", "levels/x") == "/g/levels/x");
    CHECK(content_resolve_location("/g/", "levels/x") == "/g/levels/x");
    CHECK(content_resolve_location("/g", "").empty());
    const ConfigTarget none = content_target_make("/g/fxdata", "/g/creatrs", nullptr, 5);
    CHECK(none.campaign_cfg_dir.empty());
    CHECK(none.path_for("a.cfg", CfgLayer_Level).empty());
    const ConfigTarget map = content_target_for_map("/g/fxdata", "/g/creatrs", "/m", 9);
    CHECK(map.path_for("a.cfg", CfgLayer_Level) == "/m/map00009.a.cfg");
    CHECK(map.path_for("a.cfg", CfgLayer_Campaign).empty());
}

TEST_CASE("files list: base configs, plain toml, creature models, with schema kinds", "[content_raw]")
{
    Tree t;
    const auto files = content_raw_list_files(t.target(false));
    CHECK(find(files, "trapdoor.cfg").kind == "trapdoor");
    CHECK(find(files, "notes.toml").kind.empty());
    const RawFileEntry imp = find(files, "creatrs/imp.cfg");
    CHECK(imp.creature_model);
    CHECK(imp.kind == "creaturemodel");
}

TEST_CASE("open, edit, validate, apply and delete a level file", "[content_raw]")
{
    Tree t;
    const ConfigTarget target = t.target(true);
    const RawFileEntry file = find(content_raw_list_files(target), "trapdoor.cfg");

    RawConfigSession s;
    REQUIRE(s.open(target, file, CfgLayer_Level));
    CHECK_FALSE(s.exists());
    CHECK(s.writable());
    CHECK_FALSE(s.dirty());

    s.text() = "[trap1]\nShots = 9\nHidden = 5\n";
    CHECK(s.dirty());
    const auto diags = s.validate();
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].code == "number_range");
    CHECK(diags[0].line == 3);

    // Effective view: the text takes the level's place over campaign and base.
    const auto rows = s.effective_rows(false);
    const RawEffectiveRow *health = nullptr, *shots = nullptr;
    for (const RawEffectiveRow &r : rows)
    {
        if (r.section == "trap1" && r.key == "Health")
            health = &r;
        if (r.section == "trap1" && r.key == "Shots")
            shots = &r;
    }
    REQUIRE(health != nullptr);
    CHECK(health->value == "20");
    CHECK(health->source == CfgLayer_Campaign);
    REQUIRE(shots != nullptr);
    CHECK(shots->value == "9");
    CHECK(shots->source == CfgLayer_Level);
    CHECK(shots->beneath == "2");
    CHECK(s.effective_rows(true).size() == 2); // only the two keys the text sets

    std::string err;
    REQUIRE(s.apply(&err));
    CHECK(slurp(t.root / "lvls" / "map00003.trapdoor.cfg") == "[trap1]\nShots = 9\nHidden = 5\n");
    CHECK_FALSE(s.dirty());
    CHECK(s.exists());

    // An empty text removes the file.
    s.text().clear();
    REQUIRE(s.apply(&err));
    CHECK_FALSE(fs::exists(t.root / "lvls" / "map00003.trapdoor.cfg"));

    // Delete keeps the text as an unsaved file.
    s.text() = "[trap1]\nShots = 4\n";
    REQUIRE(s.apply(&err));
    REQUIRE(s.delete_file(&err));
    CHECK_FALSE(fs::exists(t.root / "lvls" / "map00003.trapdoor.cfg"));
    CHECK(s.dirty());
}

TEST_CASE("base files are shown but never written", "[content_raw]")
{
    Tree t;
    const ConfigTarget target = t.target(false);
    RawConfigSession s;
    REQUIRE(s.open(target, find(content_raw_list_files(target), "trapdoor.cfg"), CfgLayer_Base));
    CHECK(s.exists());
    CHECK_FALSE(s.writable());
    s.text() += "[trap2]\nName = X\n";
    std::string err;
    CHECK_FALSE(s.apply(&err));
    CHECK_FALSE(err.empty());
    CHECK(slurp(t.root / "fxdata" / "trapdoor.cfg").find("trap2") == std::string::npos);
}

TEST_CASE("a layer the target does not have cannot be opened", "[content_raw]")
{
    Tree t;
    const ConfigTarget target = t.target(false); // no level number
    RawConfigSession s;
    CHECK_FALSE(s.open(target, find(content_raw_list_files(target), "trapdoor.cfg"), CfgLayer_Level));
    CHECK_FALSE(s.writable());
}

TEST_CASE("plain files without a schema get structural checks only", "[content_raw]")
{
    Tree t;
    const ConfigTarget target = t.target(false);
    RawConfigSession s;
    REQUIRE(s.open(target, find(content_raw_list_files(target), "notes.toml"), CfgLayer_Campaign));
    s.text() = "[a]\nX = 1\n[a]\nY = 2\n";
    const auto d = s.validate();
    REQUIRE(d.size() == 1);
    CHECK(d[0].code == "duplicate_block_ignored");
    CHECK(s.known_keys().empty());
}

TEST_CASE("a map's folder finds the campaign or map pack that owns it", "[content_raw]")
{
    ContentCampaign a, b;
    a.name = "A";
    a.levels_dir = "/g/campgns/a";
    b.name = "B";
    b.levels_dir = "/g/levels/b/";
    const std::vector<ContentCampaign> list = {a, b};
    REQUIRE(content_find_campaign_for_dir(list, "/g/campgns/a") != nullptr);
    CHECK(content_find_campaign_for_dir(list, "/g/campgns/a")->name == "A");
    CHECK(content_find_campaign_for_dir(list, "/g/levels/b")->name == "B");   // trailing separator ignored
    CHECK(content_find_campaign_for_dir(list, "\\g\\levels\\b\\")->name == "B"); // and backslashes
    CHECK(content_find_campaign_for_dir(list, "/g/levels/other") == nullptr);
    CHECK(content_find_campaign_for_dir(list, "") == nullptr);
    ContentCampaign rel;
    rel.name = "R";
    rel.levels_dir = "./levels/r";
    CHECK(content_find_campaign_for_dir({rel}, "levels/r")->name == "R");
}
