// Catch2 golden tests for cfgc_writer.cpp (docs/refactor/editor/fx-plans/10-config-content-model-and-writers.md, W4).
#include <catch2/catch_test_macros.hpp>

#include "cfgc_writer.h"
#include "kfx_content_test_paths.h" // KFX_CONTENT_TEST_REPO_ROOT

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

const ConfigSchema &schema()
{
    static const ConfigSchema s = build_engine_schema();
    return s;
}

std::string patch(const std::string &text, const ChangeSet &cs, const ConfigStack *lower = nullptr)
{
    TrapDoorConfigWriter w(schema());
    ConfigDocument doc = ConfigDocument::parse(text);
    w.apply(doc, cs, lower);
    return doc.serialize();
}

ConfigContent read(const std::string &text, bool partial)
{
    return read_config_content(ConfigDocument::parse(text), "trapdoor", partial);
}

std::string slurp(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void spit(const fs::path &p, const std::string &s)
{
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << s;
}

// True if another block of the file has the same id (the loader reads only the first).
bool find_dup(const ConfigContent &c, const CfgContentSection &s)
{
    for (const CfgContentSection &o : c.sections)
    {
        if (&o == &s)
            return false;
        if (ConfigStack::canonical_id(o) == ConfigStack::canonical_id(s))
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("Set rewrites only the value, keeping spacing, separator and comment", "[cfgc_writer]")
{
    const std::string in =
        "[trap1]\r\n"
        "Name = LAVA\r\n"
        "Health   =   10   ; tough\r\n"
        "Shots 3\r\n";
    ChangeSet cs;
    cs.set("trap1", "health", "25").set("trap1", "Shots", "4");
    CHECK(patch(in, cs) ==
        "[trap1]\r\n"
        "Name = LAVA\r\n"
        "Health   =   25   ; tough\r\n"
        "Shots 4\r\n");
}

TEST_CASE("Set of a repeated key rewrites the last occurrence, the one the loader keeps", "[cfgc_writer]")
{
    const std::string in = "[trap1]\nHealth = 1\nHealth = 2\n";
    ChangeSet cs;
    cs.set("trap1", "Health", "9");
    CHECK(patch(in, cs) == "[trap1]\nHealth = 1\nHealth = 9\n");
}

TEST_CASE("a new key goes after the block's last key, before its trailing comments", "[cfgc_writer]")
{
    const std::string in =
        "[trap1]\n"
        "Name        = A\n"
        "Health      = 5\n"
        "\n"
        "; next item\n"
        "[trap2]\n"
        "Name = B\n";
    ChangeSet cs;
    cs.set("trap1", "Shots", "3");
    CHECK(patch(in, cs) ==
        "[trap1]\n"
        "Name        = A\n"
        "Health      = 5\n"
        "Shots       = 3\n"
        "\n"
        "; next item\n"
        "[trap2]\n"
        "Name = B\n");
}

TEST_CASE("a new key reuses the spelling the file already uses", "[cfgc_writer]")
{
    const std::string in = "[trap1]\nName = A\n[trap2]\nName = B\nTimeBetweenShots = 4\n";
    ChangeSet cs;
    cs.set("trap1", "timebetweenshots", "9");
    CHECK(patch(in, cs) == "[trap1]\nName = A\nTimeBetweenShots = 9\n[trap2]\nName = B\nTimeBetweenShots = 4\n");
}

TEST_CASE("a missing block is appended after a blank line, with Name copied from the layer beneath", "[cfgc_writer]")
{
    ConfigStack lower(schema().find("trapdoor"));
    lower.set_layer(CfgLayer_Base, read("[trap3]\nName = CANNON\nHealth = 7\n", false));
    ChangeSet cs;
    cs.set("trap3", "Health", "20");
    CHECK(patch("[trap1]\nName = A\n", cs, &lower) == "[trap1]\nName = A\n\n[trap3]\nName = CANNON\nHealth = 20\n");
    // An empty file has no separator to add.
    CHECK(patch("", cs, &lower) == "[trap3]\nName = CANNON\nHealth = 20\n");
}

TEST_CASE("a value equal to the layer beneath removes the override", "[cfgc_writer]")
{
    ConfigStack lower(schema().find("trapdoor"));
    lower.set_layer(CfgLayer_Base, read("[trap1]\nName = A\nHealth = 7\nShots = 2\n", false));
    ChangeSet cs;
    cs.set("trap1", "Health", "7");
    // Nothing to override: the key line goes; the block keeps its other line.
    CHECK(patch("[trap1]\nHealth = 20\nShots = 5\n", cs, &lower) == "[trap1]\nShots = 5\n");
    // ... and nothing is written when there was no override.
    CHECK(patch("[trap1]\nShots = 5\n", cs, &lower) == "[trap1]\nShots = 5\n");
}

TEST_CASE("Reset removes every occurrence and drops a block left empty", "[cfgc_writer]")
{
    ChangeSet cs;
    cs.reset("trap1", "Health");
    CHECK(patch("[trap1]\nHealth = 1\nShots = 2\nHealth = 3\n", cs) == "[trap1]\nShots = 2\n");
    CHECK(patch("[trap0]\nName = A\n\n[trap1]\nHealth = 1\n\n[trap2]\nName = B\n", cs) ==
        "[trap0]\nName = A\n\n[trap2]\nName = B\n");
    // A block with authored comments is kept even when it has no keys left.
    CHECK(patch("[trap1]\n; my notes\nHealth = 1\n", cs) == "[trap1]\n; my notes\n");
}

TEST_CASE("ResetSection removes the block's keys", "[cfgc_writer]")
{
    ChangeSet cs;
    cs.reset_section("trap1");
    CHECK(patch("[trap0]\nName = A\n[trap1]\nName = B\nHealth = 1\n", cs) == "[trap0]\nName = A\n");
}

TEST_CASE("changes are idempotent", "[cfgc_writer]")
{
    ChangeSet cs;
    cs.set("trap1", "Health", "5").set("trap1", "Shots", "2").reset("trap1", "Missing");
    TrapDoorConfigWriter w(schema());
    ConfigDocument doc = ConfigDocument::parse("[trap1]\nName = A\nHealth = 1\n");
    const ChangeResult first = w.apply(doc, cs, nullptr);
    CHECK(first.applied == 2);
    CHECK(first.unchanged == 1);
    const std::string once = doc.serialize();
    const ChangeResult second = w.apply(doc, cs, nullptr);
    CHECK(second.applied == 0);
    CHECK(second.unchanged == 3);
    CHECK(doc.serialize() == once);
}

TEST_CASE("list blocks: ReplaceList rewrites in place", "[cfgc_writer]")
{
    RulesConfigWriter w(schema());
    ConfigDocument doc = ConfigDocument::parse(
        "[game]\nMapCreatureLimit = 1000\n\n[research]\n; order\nResearch = MAGIC A 1\nResearch = MAGIC B 2\n\n[health]\nX = 1\n");
    ChangeSet cs;
    cs.replace_list("research", "Research", {"ROOM LAIR 5", "MAGIC C 9", "MAGIC D 12"});
    w.apply(doc, cs, nullptr);
    CHECK(doc.serialize() ==
        "[game]\nMapCreatureLimit = 1000\n\n[research]\n; order\nResearch = ROOM LAIR 5\nResearch = MAGIC C 9\n"
        "Research = MAGIC D 12\n\n[health]\nX = 1\n");
}

TEST_CASE("a list block new to this layer is seeded with the other keys of the layer beneath", "[cfgc_writer]")
{
    ConfigStack lower(schema().find("rules"));
    lower.set_layer(CfgLayer_Base,
        read_config_content(ConfigDocument::parse(
            "[sacrifices]\nMkCreature = IMP\nMkGoodHero = TROLL\n"), "rules", false));
    RulesConfigWriter w(schema());
    ConfigDocument doc = ConfigDocument::parse("[game]\nMapCreatureLimit = 5\n");
    ChangeSet cs;
    cs.replace_list("sacrifices", "MkCreature", {"IMP", "FLY"});
    w.apply(doc, cs, &lower);
    // The block replaces the lower one whole, so it must carry MkGoodHero too.
    const ConfigContent got = read_config_content(doc, "rules", true);
    const CfgContentSection *s = got.find_section("sacrifices");
    REQUIRE(s != nullptr);
    CHECK(s->find_field("MkCreature")->values == std::vector<std::string>{"IMP", "FLY"});
    CHECK(s->find_field("MkGoodHero")->values == std::vector<std::string>{"TROLL"});
}

TEST_CASE("an emptied list block is kept: it clears the layer beneath", "[cfgc_writer]")
{
    RulesConfigWriter w(schema());
    ConfigDocument doc = ConfigDocument::parse("[research]\nResearch = MAGIC A 1\n");
    ChangeSet cs;
    cs.replace_list("research", "Research", {});
    w.apply(doc, cs, nullptr);
    CHECK(doc.serialize() == "[research]\n");
}

TEST_CASE("diagnostics report what the loader would clamp or ignore, without blocking", "[cfgc_writer]")
{
    TrapDoorConfigWriter w(schema());
    ConfigDocument doc = ConfigDocument::parse("[trap1]\nName = A\n");
    ChangeSet cs;
    cs.set("trap1", "Hidden", "5").set("trap1", "NoSuchKey", "1").set("nosuch3", "Health", "1");
    const ChangeResult r = w.apply(doc, cs, nullptr);
    CHECK(r.ok);
    std::vector<std::string> codes;
    for (const CfgDiagnostic &d : r.diagnostics)
        codes.push_back(d.code);
    CHECK(codes == std::vector<std::string>{"number_range", "unknown_key", "unknown_section"});
    // The values were still written.
    CHECK(doc.serialize().find("Hidden = 5") != std::string::npos);

    ChangeSet bad;
    bad.set("", "K", "1");
    ConfigDocument d2 = ConfigDocument::parse("[trap1]\n");
    const ChangeResult rb = w.apply(d2, bad, nullptr);
    CHECK_FALSE(rb.ok);
    CHECK(d2.serialize() == "[trap1]\n");
}

TEST_CASE("create() writes a header line and reads back as the same content", "[cfgc_writer]")
{
    TrapDoorConfigWriter w(schema());
    ChangeSet cs;
    cs.set("trap2", "Name", "CANNON").set("trap2", "Health", "30").set("door1", "Health", "500");
    const std::string text = w.create(cs, nullptr);
    CHECK(text.compare(0, 11, "; KeeperFX ") == 0);
    CHECK(text.find("written by the map editor") != std::string::npos);
    const ConfigContent c = read(text, true);
    REQUIRE(c.sections.size() == 2);
    CHECK(*c.find_section("trap", 2)->last_value("Health") == "30");
    CHECK(*c.find_section("door", 1)->last_value("Health") == "500");
    CHECK(w.create(ChangeSet(), nullptr).empty());
}

TEST_CASE("write() puts, patches and deletes files through the batch", "[cfgc_writer]")
{
    const fs::path root = fs::temp_directory_path() / ("kfx_writer_test_" + std::to_string(getpid()));
    fs::remove_all(root);
    spit(root / "base" / "trapdoor.cfg", "[trap1]\nName = A\nHealth = 7\n");
    ConfigTarget t;
    t.base_dir = (root / "base").string();
    t.campaign_cfg_dir = (root / "camp").string();
    t.level_dir = (root / "lvl").string();
    t.level_number = 5;
    TrapDoorConfigWriter w(schema());

    // Level layer does not exist yet: it is created, with the Name copied and the header on top.
    {
        WriteBatch b;
        ChangeSet cs;
        cs.set("trap1", "Health", "9");
        REQUIRE(w.write(t, CfgLayer_Level, "trapdoor.cfg", cs, b));
        REQUIRE(b.commit());
        const std::string got = slurp(root / "lvl" / "map00005.trapdoor.cfg");
        CHECK(got == "; KeeperFX Partial Traps and Doors Configuration file version 1.0 -- written by the map editor.\n"
                     "\n[trap1]\nName = A\nHealth = 9\n");
    }
    // A value equal to base is not an override: the generated file is left empty and is deleted.
    {
        WriteBatch b;
        ChangeSet cs;
        cs.set("trap1", "Health", "7");
        REQUIRE(w.write(t, CfgLayer_Level, "trapdoor.cfg", cs, b));
        REQUIRE(b.commit());
        CHECK_FALSE(fs::exists(root / "lvl" / "map00005.trapdoor.cfg"));
    }
    // An authored file with the same emptiness is kept.
    {
        spit(root / "camp" / "trapdoor.cfg", "; my campaign\n[trap1]\nHealth = 20\n");
        WriteBatch b;
        ChangeSet cs;
        cs.set("trap1", "Health", "7");
        REQUIRE(w.write(t, CfgLayer_Campaign, "trapdoor.cfg", cs, b));
        REQUIRE(b.commit());
        CHECK(slurp(root / "camp" / "trapdoor.cfg") == "; my campaign\n");
    }
    // A layer the target does not have.
    {
        ConfigTarget none;
        none.base_dir = t.base_dir;
        WriteBatch b;
        ChangeSet cs;
        cs.set("trap1", "Health", "1");
        CHECK_FALSE(w.write(none, CfgLayer_Campaign, "trapdoor.cfg", cs, b));
    }
    fs::remove_all(root);
}

TEST_CASE("value column and blank-separator style are copied from the neighbour", "[cfgc_writer]")
{
    ChangeSet cs;
    cs.set("trap1", "Shots", "4");
    CHECK(patch("[trap1]\nName  A\nHealth 10\n", cs) == "[trap1]\nName  A\nHealth 10\nShots  4\n");
    CHECK(patch("[trap1]\n  Name = A\n", cs) == "[trap1]\n  Name = A\n  Shots = 4\n");
}

// Corpus: setting every key of a shipped file to the value it already has must not touch a byte
// (spacing, CRLF, comments, odd separators all survive).
TEST_CASE("setting every key to its own value leaves the shipped files byte-identical", "[cfgc_writer][corpus]")
{
    const fs::path root(KFX_CONTENT_TEST_REPO_ROOT);
    size_t files = 0, sets = 0;
    for (const char *sub : {"core_files", "config"})
        for (const auto &e : fs::recursive_directory_iterator(root / sub))
        {
            if (!e.is_regular_file() || e.path().extension() != ".cfg")
                continue;
            std::string stem = e.path().stem().string();
            for (char &c : stem)
                c = (char)std::tolower((unsigned char)c);
            if (stem.find('.') != std::string::npos)
                stem = stem.substr(stem.find('.') + 1);
            if (stem != "trapdoor" && stem != "rules")
                continue;
            const std::string text = slurp(e.path());
            TableConfigWriter w(schema().find(stem), "");
            ConfigDocument doc = ConfigDocument::parse(text);
            const ConfigContent c = read_config_content(doc, stem, true);
            ChangeSet cs;
            for (const CfgContentSection &s : c.sections)
            {
                if (cfgc_is_banner_section(s.name) || find_dup(c, s))
                    continue;
                for (const CfgField &f : s.fields)
                    if (f.values.size() == 1)
                    {
                        cs.set(ConfigStack::canonical_id(s), f.key, f.values[0]);
                        sets++;
                    }
            }
            w.apply(doc, cs, nullptr);
            REQUIRE(doc.serialize() == text);
            files++;
        }
    CHECK(files > 50);
    CHECK(sets > 5000);
}
