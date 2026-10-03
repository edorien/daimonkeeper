// Catch2 coverage for cfgc_stack.cpp (docs/refactor/editor/fx-plans/10-config-content-model-and-writers.md, W3).
#include <catch2/catch_test_macros.hpp>

#include "cfgc_stack.h"
#include "kfx_content_test_paths.h" // KFX_CONTENT_TEST_REPO_ROOT

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

ConfigContent content(const std::string &text, bool partial)
{
    return read_config_content(ConfigDocument::parse(text), "test", partial);
}

void spit(const fs::path &p, const std::string &s)
{
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << s;
}

// A schema with a two-value key and a list block, enough to exercise the rules.
ConfigSchema test_schema()
{
    ConfigSchema s;
    CfgFileSchema f;
    f.kind = "test";
    CfgSectionSpec trap;
    trap.basename = "trap";
    trap.numbered = true;
    CfgFieldSpec sym;
    sym.key = "SymbolSprites";
    sym.parts.resize(2);
    trap.fields.push_back(sym);
    f.sections.push_back(trap);
    CfgSectionSpec research;
    research.basename = "research";
    research.list_replaces = true;
    f.sections.push_back(research);
    s.files.push_back(f);
    return s;
}

} // namespace

TEST_CASE("target paths follow the loader's layout", "[cfgc_stack]")
{
    ConfigTarget t;
    t.base_dir = "/g/fxdata";
    t.base_crtr_dir = "/g/creatrs";
    t.campaign_cfg_dir = "/c/cfgs/";
    t.campaign_crtr_dir = "/c/crtr";
    t.level_dir = "/c/lvls";
    t.level_number = 7;
    CHECK(t.path_for("trapdoor.cfg", CfgLayer_Base) == "/g/fxdata/trapdoor.cfg");
    CHECK(t.path_for("trapdoor.cfg", CfgLayer_Campaign) == "/c/cfgs/trapdoor.cfg");
    CHECK(t.path_for("trapdoor.cfg", CfgLayer_Level) == "/c/lvls/map00007.trapdoor.cfg");
    CHECK(t.path_for("imp.cfg", CfgLayer_Base, true) == "/g/creatrs/imp.cfg");
    CHECK(t.path_for("imp.cfg", CfgLayer_Campaign, true) == "/c/crtr/imp.cfg");
    CHECK(t.path_for("imp.cfg", CfgLayer_Level, true) == "/c/lvls/map00007.imp.cfg");
    t.level_number = -1;
    CHECK(t.path_for("trapdoor.cfg", CfgLayer_Level).empty());
    t.campaign_cfg_dir.clear();
    CHECK(t.path_for("trapdoor.cfg", CfgLayer_Campaign).empty());
}

TEST_CASE("keys are overridden layer by layer, with the value beneath", "[cfgc_stack]")
{
    ConfigStack s;
    s.set_layer(CfgLayer_Base, content("[trap1]\nName = A\nHealth = 10\nShots = 3\n", false));
    s.set_layer(CfgLayer_Campaign, content("[trap1]\nHealth = 20\n", true));
    s.set_layer(CfgLayer_Level, content("[trap1]\nHealth = 30\nhealth = 35\n", true));

    CfgEffective e;
    REQUIRE(s.effective("trap1", "Health", e));
    CHECK(e.values == std::vector<std::string>{"35"}); // the last line of the highest layer
    CHECK(e.source == CfgLayer_Level);
    REQUIRE(e.has_beneath);
    CHECK(e.beneath == std::vector<std::string>{"20"});

    REQUIRE(s.effective("trap1", "Shots", e));
    CHECK(e.values[0] == "3");
    CHECK(e.source == CfgLayer_Base);
    CHECK_FALSE(e.has_beneath);

    CHECK_FALSE(s.effective("trap1", "Missing", e));
    CHECK_FALSE(s.effective("trap9", "Health", e));
}

TEST_CASE("blocks match by number, not by spelling; a repeated name is never read", "[cfgc_stack]")
{
    ConfigStack s;
    s.set_layer(CfgLayer_Base, content("[trap2]\nHealth = 1\n", false));
    s.set_layer(CfgLayer_Level, content("[trap02]\nHealth = 5\n[trap2]\nShots = 9\n[trap2]\nShots = 99\nArmour = 7\n", true));
    CfgEffective e;
    REQUIRE(s.effective("trap2", "Health", e));
    CHECK(e.values[0] == "5"); // "trap02" is trap 2
    REQUIRE(s.effective("trap2", "Shots", e));
    CHECK(e.values[0] == "9"); // the second [trap2] repeats a name: the loader never reads it
    CHECK_FALSE(s.effective("trap2", "Armour", e));
    CHECK(s.layers_defining("trap2") == std::vector<CfgLayer>{CfgLayer_Base, CfgLayer_Level});
}

TEST_CASE("a higher layer can extend the numbered list", "[cfgc_stack]")
{
    ConfigStack s;
    s.set_layer(CfgLayer_Base, content("[trap0]\nName = A\n[trap1]\nName = B\n", false));
    s.set_layer(CfgLayer_Campaign, content("[trap4]\nName = E\n", true));
    CHECK(s.section_count("trap") == 5);
    CHECK(s.numbered_section_ids("trap") == std::vector<std::string>{"trap0", "trap1", "trap4"});
    CfgNameSets names;
    s.collect_names(names);
    REQUIRE(names.find("trap") != nullptr);
    CHECK(names.find("trap")->count("E") == 1);
}

TEST_CASE("multi-value keys are overlaid position by position", "[cfgc_stack]")
{
    const ConfigSchema schema = test_schema();
    ConfigStack s(schema.find("test"));
    s.set_layer(CfgLayer_Base, content("[trap1]\nSymbolSprites = big med\n", false));
    s.set_layer(CfgLayer_Level, content("[trap1]\nSymbolSprites = other\n", true));
    CfgEffective e;
    REQUIRE(s.effective("trap1", "SymbolSprites", e));
    CHECK(e.values[0] == "other med"); // the short line changed only the first value
    CHECK(e.beneath[0] == "big med");

    ConfigStack plain; // without a schema the whole line replaces
    plain.set_layer(CfgLayer_Base, content("[trap1]\nSymbolSprites = big med\n", false));
    plain.set_layer(CfgLayer_Level, content("[trap1]\nSymbolSprites = other\n", true));
    REQUIRE(plain.effective("trap1", "SymbolSprites", e));
    CHECK(e.values[0] == "other");
}

TEST_CASE("list blocks are replaced whole by the highest layer that has them", "[cfgc_stack]")
{
    const ConfigSchema schema = test_schema();
    ConfigStack s(schema.find("test"));
    s.set_layer(CfgLayer_Base, content("[research]\nResearch = MAGIC A 1\nResearch = MAGIC B 2\n", false));
    CfgEffective e;
    REQUIRE(s.effective("research", "Research", e));
    CHECK(e.values.size() == 2);

    s.set_layer(CfgLayer_Level, content("[research]\nResearch = ROOM X 5\n", true));
    REQUIRE(s.effective("research", "Research", e));
    CHECK(e.values == std::vector<std::string>{"ROOM X 5"});
    CHECK(e.source == CfgLayer_Level);
    REQUIRE(e.has_beneath);
    CHECK(e.beneath.size() == 2);

    // An empty block in a higher layer clears the list: nothing is effective.
    s.set_layer(CfgLayer_Level, content("[research]\n", true));
    CHECK_FALSE(s.effective("research", "Research", e));
}

TEST_CASE("layers load from disk following the target", "[cfgc_stack]")
{
    const fs::path root = fs::temp_directory_path() / "kfx_stack_test";
    fs::remove_all(root);
    spit(root / "base" / "t.cfg", "[trap1]\nHealth = 1\n");
    spit(root / "camp" / "t.cfg", "[trap1]\nHealth = 2\n");
    spit(root / "lvl" / "map00003.t.cfg", "[trap1]\nShots = 4\n");

    ConfigTarget t;
    t.base_dir = (root / "base").string();
    t.campaign_cfg_dir = (root / "camp").string();
    t.level_dir = (root / "lvl").string();
    t.level_number = 3;
    ConfigStack s = ConfigStack::load(t, "test", "t.cfg", nullptr);
    CHECK(s.has_layer(CfgLayer_Base));
    CHECK(s.has_layer(CfgLayer_Campaign));
    CHECK(s.has_layer(CfgLayer_Level));
    CHECK(s.layer(CfgLayer_Level).partial);
    CHECK_FALSE(s.layer(CfgLayer_Base).partial);
    CfgEffective e;
    REQUIRE(s.effective("trap1", "Health", e));
    CHECK(e.values[0] == "2");
    REQUIRE(s.effective("trap1", "Shots", e));
    CHECK(e.source == CfgLayer_Level);

    t.level_number = 4; // no such level file
    ConfigStack s2 = ConfigStack::load(t, "test", "t.cfg", nullptr);
    CHECK_FALSE(s2.has_layer(CfgLayer_Level));
    fs::remove_all(root);
}

TEST_CASE("a real campaign layers over the base files", "[cfgc_stack][corpus]")
{
    const fs::path root(KFX_CONTENT_TEST_REPO_ROOT);
    const fs::path camp = root / "core_files" / "campgns" / "necro_cfgs";
    if (!fs::exists(camp / "trapdoor.cfg"))
        return;
    ConfigTarget t;
    t.base_dir = (root / "config" / "fxdata").string();
    t.campaign_cfg_dir = camp.string();
    const ConfigSchema schema = build_engine_schema();
    ConfigStack s = ConfigStack::load(t, "trapdoor", "trapdoor.cfg", schema.find("trapdoor"));
    REQUIRE(s.has_layer(CfgLayer_Base));
    REQUIRE(s.has_layer(CfgLayer_Campaign));
    CHECK(s.section_count("trap") >= 1);
    // Every block the campaign file has is visible in the merge, and every key of it resolves.
    size_t checked = 0;
    for (const CfgContentSection &sec : s.layer(CfgLayer_Campaign).sections)
        for (const CfgField &f : sec.fields)
        {
            CfgEffective e;
            REQUIRE(s.effective(ConfigStack::canonical_id(sec), f.key, e));
            CHECK(e.source >= CfgLayer_Campaign);
            checked++;
        }
    CHECK(checked > 0);
}
