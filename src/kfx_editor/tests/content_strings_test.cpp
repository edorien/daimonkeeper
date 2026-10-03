// Catch2 coverage for content_strings.cpp (plan 09 X2).
#include <catch2/catch_test_macros.hpp>

#include "content_strings.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

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

std::string nuls(std::initializer_list<const char *> entries)
{
    std::string out;
    for (const char *e : entries)
    {
        out += e;
        out += '\0';
    }
    return out;
}

struct Tree
{
    fs::path root;
    Tree()
    {
        // per process: ctest runs each case as its own process, in parallel
        root = fs::temp_directory_path() / ("kfx_content_strings_test_" + std::to_string(getpid()));
        fs::remove_all(root);
        spit(root / "fxdata" / "gtext_eng.dat", nuls({"", "base one", "base two", "base three caf\x82"}));
        spit(root / "camp" / "text_eng.dat", nuls({"", "camp one"}));
    }
    ~Tree() { fs::remove_all(root); }
    StringsPaths paths() const
    {
        ContentCampaign c;
        c.strings["eng"] = (root / "camp" / "text_eng.dat").string();
        return content_strings_paths(root.string(), &c, (root / "lvls").string(), 7, "eng");
    }
};

} // namespace

TEST_CASE("string paths and languages", "[content_strings]")
{
    Tree t;
    const StringsPaths p = t.paths();
    CHECK(p.base == t.root.string() + "/fxdata/gtext_eng.dat");
    CHECK(p.campaign == (t.root / "camp" / "text_eng.dat").string());
    CHECK(p.level == t.root.string() + "/lvls/map00007.eng.dat");
    CHECK(content_strings_paths(t.root.string(), nullptr, "", -1, "eng").level.empty());
    spit(t.root / "fxdata" / "gtext_fre.dat", nuls({"", "un"}));
    ContentCampaign c;
    c.strings["ger"] = "x";
    const auto langs = content_strings_languages(t.root.string(), &c);
    CHECK(langs == std::vector<std::string>{"eng", "fre", "ger"});
}

TEST_CASE("views: the highest layer with text wins and reports what is beneath", "[content_strings]")
{
    Tree t;
    StringsSession s;
    REQUIRE(s.open(t.paths(), "eng", CfgLayer_Level));
    CHECK(s.writable());
    auto v = s.view(1);
    CHECK(v.text == "camp one");
    CHECK(v.source == CfgLayer_Campaign);
    CHECK_FALSE(v.overridden_here);
    v = s.view(3);
    CHECK(v.text == "base three caf\xC3\xA9"); // shown as UTF-8
    CHECK(v.source == CfgLayer_Base);
    CHECK(s.id_count() == 4);
}

TEST_CASE("editing writes the level file; untouched entries stay byte for byte", "[content_strings]")
{
    Tree t;
    // The campaign file has bytes the editor must not disturb.
    spit(t.root / "camp" / "text_eng.dat", std::string("\0camp one\0\x82\xA0\0", 13));
    StringsSession s;
    REQUIRE(s.open(t.paths(), "eng", CfgLayer_Campaign));
    s.set_text(1, "camp one edited\nsecond line");
    std::string err;
    REQUIRE(s.apply(&err));
    const std::string bytes = slurp(t.root / "camp" / "text_eng.dat");
    CHECK(bytes == std::string("\0camp one edited\r\nsecond line\0", 30) + std::string("\x82\xA0\0", 3)); // line break stored as CR LF, entry 2 untouched
    CHECK(s.view(1).text == "camp one edited\nsecond line");

    // A level string.
    StringsSession lv;
    REQUIRE(lv.open(t.paths(), "eng", CfgLayer_Level));
    lv.set_text(5, "level five caf\xC3\xA9");
    CHECK(lv.id_count() == 6);
    REQUIRE(lv.apply(&err));
    const std::string lb = slurp(t.root / "lvls" / "map00007.eng.dat");
    CHECK(lb.size() >= 16);
    CHECK(StringsFile::parse(lb).raw(5) == "level five caf\x82"); // the code page byte
    CHECK(lv.view(5).source == CfgLayer_Level);

    // Reset blanks it; a level file with nothing left is removed.
    lv.reset(5);
    REQUIRE(lv.apply(&err));
    CHECK_FALSE(fs::exists(t.root / "lvls" / "map00007.eng.dat"));
    CHECK_FALSE(lv.view(5).found);
}

TEST_CASE("an edit equal to what the layer has is not an edit", "[content_strings]")
{
    Tree t;
    StringsSession s;
    REQUIRE(s.open(t.paths(), "eng", CfgLayer_Campaign));
    s.set_text(1, "camp one");
    CHECK_FALSE(s.dirty());
    s.set_text(1, "different");
    CHECK(s.dirty());
    s.set_text(1, "camp one");
    CHECK_FALSE(s.dirty());
}

TEST_CASE("characters the code page cannot hold stop the write", "[content_strings]")
{
    Tree t;
    StringsSession s;
    REQUIRE(s.open(t.paths(), "eng", CfgLayer_Level));
    s.set_text(2, "chinese \xE4\xB8\xAD here");
    const auto d = s.diagnostics();
    REQUIRE(d.size() == 1);
    CHECK(d[0].message.find("U+4E2D") != std::string::npos);
    std::string err;
    CHECK_FALSE(s.apply(&err));
    CHECK_FALSE(fs::exists(t.root / "lvls" / "map00007.eng.dat"));
    s.set_text(2, "fixed");
    CHECK(s.diagnostics().empty());
    CHECK(s.apply(&err));
}

TEST_CASE("read-only cases say why", "[content_strings]")
{
    Tree t;
    StringsSession base;
    REQUIRE(base.open(t.paths(), "eng", CfgLayer_Base));
    CHECK_FALSE(base.writable());
    base.set_text(1, "x");
    CHECK_FALSE(base.dirty());
    StringsSession cjk;
    StringsPaths p = t.paths();
    REQUIRE(cjk.open(p, "jpn", CfgLayer_Level));
    CHECK_FALSE(cjk.writable());
    CHECK(cjk.why_read_only().find("multi-byte") != std::string::npos);
    // A campaign that names the base file as its own has nothing of its own to edit.
    StringsPaths same = t.paths();
    same.campaign = same.base;
    StringsSession sc;
    REQUIRE(sc.open(same, "eng", CfgLayer_Campaign));
    CHECK_FALSE(sc.writable());
    // No campaign file at all.
    same.campaign.clear();
    StringsSession none;
    REQUIRE(none.open(same, "eng", CfgLayer_Campaign));
    CHECK_FALSE(none.writable());
}

TEST_CASE("next free id skips every id any layer defines", "[content_strings]")
{
    Tree t;
    StringsSession s;
    REQUIRE(s.open(t.paths(), "eng", CfgLayer_Level));
    CHECK(s.next_free_id() == 4); // ids 1-3 are taken by base and campaign
    s.set_text(4, "new");
    CHECK(s.next_free_id() == 5);
}

TEST_CASE("usage: campaign name ids and script display commands", "[content_strings]")
{
    Tree t;
    spit(t.root / "lvls" / "map00007.txt", "REM x\nDISPLAY_OBJECTIVE(12,ALL_PLAYERS)\nDisplay_Information ( 13 , 5)\nQUICK_OBJECTIVE(3,\"inline\",ALL_PLAYERS)\n");
    ContentCampaign c;
    c.name_ids[202] = "map00001";
    const auto used = content_strings_usage(&c, (t.root / "lvls").string(), {7, 8});
    REQUIRE(used.count(202) == 1);
    CHECK(used.at(202)[0] == "map00001 name");
    REQUIRE(used.count(12) == 1);
    CHECK(used.at(12)[0] == "map00007 script");
    CHECK(used.count(13) == 1);
    CHECK(used.count(3) == 0); // QUICK_OBJECTIVE carries its text inline
}
