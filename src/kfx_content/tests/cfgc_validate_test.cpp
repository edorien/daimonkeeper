// Catch2 coverage for cfgc_validate.cpp (plan 03 F3 / plan 10 §4.5).
#include <catch2/catch_test_macros.hpp>

#include "cfgc_content.h"
#include "cfgc_validate.h"
#include "kfx_content_test_paths.h" // KFX_CONTENT_TEST_REPO_ROOT

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

const ConfigSchema &schema()
{
    static const ConfigSchema s = build_engine_schema();
    return s;
}

std::vector<CfgDiagnostic> check(const std::string &text, const char *kind = "trapdoor")
{
    return cfgc_validate_document(ConfigDocument::parse(text), schema().find(kind), nullptr);
}

std::string codes(const std::vector<CfgDiagnostic> &d)
{
    std::string out;
    for (const CfgDiagnostic &x : d)
        out += (out.empty() ? "" : ",") + x.code + "@" + std::to_string(x.line);
    return out;
}

} // namespace

TEST_CASE("validate document: a clean file has no diagnostics", "[cfgc_validate]")
{
    CHECK(check("; header\n[trap1]\nName = A\nHealth = 5 ; ok\n").empty());
}

TEST_CASE("validate document: structure", "[cfgc_validate]")
{
    CHECK(codes(check("Stray = 1\n[trap1]\nName = A\n")) == "key_outside_block@1");
    CHECK(codes(check("[trap1]\nName = A\n[trap1]\nHealth = 9\n")) == "duplicate_block_ignored@3");
    CHECK(codes(check("[nosuch3]\nHealth = 1\n")) == "unknown_section@1");
    CHECK(codes(check("[trap1]\nNoSuchKey = 1\n")) == "unknown_key@2");
    CHECK(codes(check("[trap1]\n!!! not a key\n")) == "unrecognised_line@2");
    // Banners, ^Z and blank junk are not reported.
    CHECK(check("[####### FIRE #######]\nAnything = 1\n[trap1]\n\x1a\n").empty());
    // Both spellings of a number are one block, so the second one is a different name, not a duplicate.
    CHECK(check("[trap2]\nName = A\n[trap02]\nName = B\n").empty());
}

TEST_CASE("validate document: values carry line, block and key", "[cfgc_validate]")
{
    const auto d = check("[trap1]\nName = A\nHidden = 5\n");
    REQUIRE(d.size() == 1);
    CHECK(d[0].code == "number_range");
    CHECK(d[0].line == 3);
    CHECK(d[0].section == "trap1");
    CHECK(d[0].key == "Hidden");
}

TEST_CASE("validate document: names against a registry", "[cfgc_validate]")
{
    CfgNameSets names;
    names.add("object", "CRATE_A");
    const auto d = cfgc_validate_document(ConfigDocument::parse("[trap1]\nCrate = NOPE\n[trap2]\nCrate = crate_a\n"),
        schema().find("trapdoor"), &names);
    REQUIRE(d.size() == 1);
    CHECK(d[0].code == "unknown_name");
    CHECK(d[0].line == 2);
}

TEST_CASE("validate document: no schema means structural checks only", "[cfgc_validate]")
{
    const auto d = cfgc_validate_document(ConfigDocument::parse("[a]\nX = 1\n[a]\nY = 2\n"), nullptr, nullptr);
    REQUIRE(d.size() == 1);
    CHECK(d[0].code == "duplicate_block_ignored");
}

TEST_CASE("validate document: shipped base files are clean", "[cfgc_validate][corpus]")
{
    const fs::path base = fs::path(KFX_CONTENT_TEST_REPO_ROOT) / "config" / "fxdata";
    const char *kinds[][2] = {{"trapdoor", "trapdoor.cfg"}, {"terrain", "terrain.cfg"}, {"objects", "objects.cfg"},
        {"rules", "rules.cfg"}, {"magic", "magic.cfg"}};
    CfgNameSets names;
    std::vector<std::pair<std::string, std::string>> texts;
    for (auto &k : kinds)
    {
        std::ifstream f(base / k[1], std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        texts.emplace_back(k[0], ss.str());
        cfgc_collect_names(read_config_content(ConfigDocument::parse(ss.str()), k[0], false), names);
    }
    for (auto &t : texts)
    {
        const auto d = cfgc_validate_document(ConfigDocument::parse(t.second), schema().find(t.first), &names);
        for (const CfgDiagnostic &x : d)
            if (x.severity >= CfgSev_Warning) // "ignored_key" info is expected: base files carry a few dead keys
                FAIL_CHECK(t.first << ":" << x.line << " " << x.code << ": " << x.message);
    }
}
