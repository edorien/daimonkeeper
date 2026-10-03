// Catch2 coverage for cfgc_document.cpp -- the lossless line model of a .cfg file
// (docs/refactor/editor/fx-plans/10-config-content-model-and-writers.md, W0/W1).
#include <catch2/catch_test_macros.hpp>

#include "cfgc_document.h"
#include "kfx_content_test_paths.h" // KFX_CONTENT_TEST_REPO_ROOT

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "config.h"
#include "config_creature.h"
#include "config_trapdoor.h"
#include "config_rules.h"
#include "config_objects.h"
#include "config_terrain.h"
#include "config_effects.h"
}

namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::vector<fs::path> shipped_cfg_files()
{
    std::vector<fs::path> out;
    for (const char *root : {"core_files", "config"})
    {
        std::error_code ec;
        for (fs::recursive_directory_iterator it(fs::path(KFX_CONTENT_TEST_REPO_ROOT) / root, ec), end; !ec && it != end;
             it.increment(ec))
        {
            std::string ext = lower(it->path().extension().string());
            if (it->is_regular_file(ec) && ext == ".cfg")
                out.push_back(it->path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::set<std::string> field_names(const struct NamedField *fields)
{
    std::set<std::string> names;
    for (const struct NamedField *f = fields; f != nullptr && f->name != nullptr; f++)
        names.insert(lower(f->name));
    return names;
}

std::set<std::string> field_names(const struct NamedFieldSet &set)
{
    return field_names(set.named_fields);
}

std::set<std::string> command_names(const struct NamedCommand *table)
{
    std::set<std::string> names;
    for (const struct NamedCommand *c = table; c->name != nullptr; c++)
        names.insert(lower(c->name));
    return names;
}

} // namespace

TEST_CASE("document: classifies lines the way the loader reads them", "[kfx_content][cfgc]") {
    const std::string text =
        "; header\r\n\r\n[trap3]\r\nName = BOULDER\r\nHealth 128\r\nShotVector = 1 2 ; note\r\n"
        "[####  BANNER  ####]\r\nAreaDamage 2 0 -40\r\n\x1A";
    ConfigDocument d = ConfigDocument::parse(text);
    REQUIRE(d.lines().size() == 9);
    CHECK(d.lines()[0].kind == CfgLine_Comment);
    CHECK(d.lines()[1].kind == CfgLine_Blank);
    CHECK(d.lines()[2].kind == CfgLine_Section);
    CHECK(d.lines()[2].name == "trap3");
    CHECK(d.lines()[3].kind == CfgLine_Key);
    CHECK(d.lines()[3].name == "Name");
    CHECK(d.lines()[3].value == "BOULDER");
    CHECK(d.lines()[3].has_equals);
    CHECK(d.lines()[4].kind == CfgLine_Key); // no '='
    CHECK_FALSE(d.lines()[4].has_equals);
    CHECK(d.lines()[4].value == "128");
    CHECK(d.lines()[5].value == "1 2 ; note"); // inline comment stays in the value text
    CHECK(d.lines()[6].kind == CfgLine_Section);
    CHECK(d.lines()[7].name == "AreaDamage");
    CHECK(d.lines()[8].kind == CfgLine_Other); // ^Z
    CHECK(d.lines()[0].eol == "\r\n");
    CHECK(d.lines()[8].eol == "");
    CHECK(d.serialize() == text);
    CHECK(d.dominant_eol() == "\r\n");
}

TEST_CASE("document: sections and key lines", "[kfx_content][cfgc]") {
    ConfigDocument d = ConfigDocument::parse("A = 1\n[one]\nX = 1\n; c\nY = 2\n[two]\n[one]\nZ = 3\n");
    REQUIRE(d.sections().size() == 4);
    CHECK(d.sections()[0].header_line == -1); // preamble
    CHECK(d.key_lines(1).size() == 2);
    CHECK(d.key_lines(2).empty());
    CHECK(d.dominant_eol() == "\n");
}

TEST_CASE("document: any byte sequence parses and round-trips", "[kfx_content][cfgc]") {
    for (const std::string &s : {std::string(), std::string("\n"), std::string("\r\n\r\n"), std::string("[open"),
                                 std::string("\xEF\xBB\xBF; bom\nKey=1"), std::string("a\0b\nc", 5), std::string("=\n]\n")})
        CHECK(ConfigDocument::parse(s).serialize() == s);
}

TEST_CASE("document: every shipped .cfg round-trips byte for byte", "[kfx_content][cfgc][corpus]") {
    const std::vector<fs::path> files = shipped_cfg_files();
    REQUIRE(files.size() > 500);
    size_t lines = 0, bad = 0;
    std::string first_bad;
    for (const fs::path &p : files)
    {
        const std::string bytes = slurp(p);
        ConfigDocument d = ConfigDocument::parse(bytes);
        lines += d.lines().size();
        if (d.serialize() != bytes)
        {
            bad++;
            if (first_bad.empty())
                first_bad = p.string();
        }
    }
    INFO("first failing file: " << first_bad);
    CHECK(bad == 0);
    CHECK(lines > 100000);
}

// Schema-coverage baseline for plan 10 (W0). Not a pass/fail gate: it prints, per file
// kind, how many distinct (section, key) pairs used by the shipped files are NOT known
// to the engine's own field tables / exported key tables. W2 turns the residue into
// curated overlays or an explicit allow-list.
TEST_CASE("baseline: schema coverage of the shipped files", "[kfx_content][cfgc][baseline]") {
    struct Kind
    {
        explicit Kind(const char *n) : name(n) {}
        std::string name;
        std::set<std::string> known;
        std::set<std::string> used_keys;
        std::set<std::string> unknown;
        size_t files = 0;
    };
    auto add_all = [](Kind &k, const std::set<std::string> &s) { k.known.insert(s.begin(), s.end()); };

    Kind trapdoor("trapdoor"), rules("rules"), objects("objects"), terrain("terrain"), creature("creature model files");
    add_all(trapdoor, field_names(trapdoor_trap_named_fields_set));
    add_all(trapdoor, field_names(trapdoor_door_named_fields_set));
    for (int64_t i = 0; i < 8; i++) // rules.cfg keeps its blocks in ruleblocks[], not in the set
        add_all(rules, field_names(ruleblocks[i]));
    add_all(objects, field_names(objects_named_fields_set));
    add_all(terrain, field_names(terrain_room_named_fields_set));
    for (const struct NamedCommand *t : {creatmodel_attributes_commands, creatmodel_jobs_commands,
             creatmodel_attraction_commands, creatmodel_sounds_commands, creature_graphics_desc,
             creatmodel_annoyance_commands, creatmodel_experience_commands, creatmodel_senses_commands,
             creatmodel_appearance_commands})
        add_all(creature, command_names(t));

    // Creature file stems, to recognise per-level creature files (map%05d.<creature>.cfg).
    std::set<std::string> creature_stems;
    {
        std::error_code ec;
        for (fs::directory_iterator it(fs::path(KFX_CONTENT_TEST_REPO_ROOT) / "config" / "creatrs", ec), end; !ec && it != end;
             it.increment(ec))
            creature_stems.insert(lower(it->path().stem().string()));
    }

    for (const fs::path &p : shipped_cfg_files())
    {
        const std::string stem = lower(p.stem().string());
        std::string parent = lower(p.parent_path().filename().string());
        Kind *kind = nullptr;
        std::string base = stem;
        if (stem.compare(0, 3, "map") == 0 && stem.find('.') != std::string::npos)
            base = stem.substr(stem.find('.') + 1); // "map00005.rules" -> "rules"
        if (base == "trapdoor") kind = &trapdoor;
        else if (base == "rules") kind = &rules;
        else if (base == "objects") kind = &objects;
        else if (base == "terrain") kind = &terrain;
        else if (creature_stems.count(base) && (parent == "creatrs" || parent == "creatures"
                     || (parent.size() > 5 && parent.compare(parent.size() - 5, 5, "_crtr") == 0)
                     || stem.compare(0, 3, "map") == 0))
            kind = &creature;
        if (kind == nullptr)
            continue;
        kind->files++;
        ConfigDocument d = ConfigDocument::parse(slurp(p));
        for (const CfgLine &l : d.lines())
            if (l.kind == CfgLine_Key)
            {
                const std::string k = lower(l.name);
                kind->used_keys.insert(k);
                if (!kind->known.count(k))
                    kind->unknown.insert(k);
            }
    }

    std::printf("\n== schema coverage baseline (plan 10 W0) ==\n");
    for (Kind *k : {&trapdoor, &rules, &objects, &terrain, &creature})
    {
        std::printf("%-24s files %3zu  table/key-table fields %3zu  distinct keys used %3zu  not in tables %3zu\n", k->name.c_str(),
            k->files, k->known.size(), k->used_keys.size(), k->unknown.size());
        std::string list;
        for (const std::string &u : k->unknown)
            list += u + " ";
        if (list.size() > 700)
            list = list.substr(0, 700) + "...";
        std::printf("    not in tables: %s\n", list.c_str());
    }
    SUCCEED();
}
