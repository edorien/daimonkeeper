// Catch2 coverage for cfgc_content.cpp (ConfigContent reader) and cfgc_writebatch.cpp
// (docs/refactor/editor/fx-plans/10-config-content-model-and-writers.md, W1).
#include <catch2/catch_test_macros.hpp>

#include "cfgc_content.h"
#include "cfgc_writebatch.h"
#include "kfx_content_test_paths.h" // KFX_CONTENT_TEST_REPO_ROOT

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

ConfigContent read(const std::string &text, bool partial = false)
{
    return read_config_content(ConfigDocument::parse(text), "test", partial);
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
    std::ofstream f(p, std::ios::binary);
    f << s;
}

struct TempDir
{
    fs::path path;
    explicit TempDir(const char *tag)
    {
        // per process: ctest runs each case as its own process, in parallel
        path = fs::temp_directory_path() / (std::string("kfx_wb_") + tag + "_" + std::to_string(getpid()));
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
};

} // namespace

TEST_CASE("section names split into basename and index", "[cfgc_content]")
{
    std::string b;
    int64_t i;
    cfgc_split_section_name("trap12", b, i);
    CHECK(b == "trap"); CHECK(i == 12);
    cfgc_split_section_name("common", b, i);
    CHECK(b == "common"); CHECK(i == -1);
    cfgc_split_section_name("12", b, i);
    CHECK(b == "12"); CHECK(i == -1);
    cfgc_split_section_name("map00019", b, i);
    CHECK(b == "map"); CHECK(i == 19);
    cfgc_split_section_name("", b, i);
    CHECK(b == ""); CHECK(i == -1);
}

TEST_CASE("content reads sections, fields and repeated keys", "[cfgc_content]")
{
    const ConfigContent c = read(
        "; header\r\n"
        "Orphan = 1\r\n"
        "[common]\r\n"
        "TrapsCount = 3\r\n"
        "\r\n"
        "[trap2]\r\n"
        "Name = LAVA\r\n"
        "Health 20   ; a comment\r\n"
        "Crate = A\r\n"
        "crate = B\r\n"
        "Empty =\r\n"
        "[trap2]\r\n"
        "Name = LAVA2\r\n", true);
    CHECK(c.kind == "test");
    CHECK(c.partial);
    REQUIRE(c.sections.size() == 3); // preamble skipped, repeated block kept
    CHECK(c.sections[0].name == "common");
    CHECK(c.sections[0].index == -1);

    const CfgContentSection *t = c.find_section("trap", 2);
    REQUIRE(t != nullptr);
    CHECK(t == &c.sections[1]); // first of the two
    REQUIRE(t->fields.size() == 4);
    CHECK(*t->last_value("name") == "LAVA"); // case-insensitive key
    CHECK(*t->last_value("Health") == "20"); // no "=", inline comment removed
    const CfgField *crate = t->find_field("CRATE");
    REQUIRE(crate != nullptr);
    CHECK(crate->key == "Crate");
    CHECK(crate->values == std::vector<std::string>{"A", "B"});
    CHECK(*t->last_value("Crate") == "B"); // the loader keeps the last
    CHECK(*t->last_value("Empty") == "");
    CHECK(t->last_value("Missing") == nullptr);

    CHECK(c.find_section("Common") == nullptr); // section names are case-sensitive
    CHECK(c.find_section("common") == &c.sections[0]);
    CHECK(c.find_section("trap", 3) == nullptr);
}

TEST_CASE("content of an empty or junk document is empty", "[cfgc_content]")
{
    CHECK(read("").sections.empty());
    CHECK(read("just junk\r\n;x\r\n").sections.empty());
}

TEST_CASE("content equality ignores layout and comments", "[cfgc_content]")
{
    CHECK(read("[a1]\nK=1\n") == read("\n; hi\n[a1]\r\n  K   1 ; c\r\n"));
    CHECK_FALSE(read("[a1]\nK=1\n") == read("[a1]\nK=2\n"));
}

TEST_CASE("content reads every shipped config file", "[cfgc_content][corpus]")
{
    const fs::path root(KFX_CONTENT_TEST_REPO_ROOT);
    size_t files = 0, sections = 0;
    for (const char *sub : {"core_files", "config"})
        for (const auto &e : fs::recursive_directory_iterator(root / sub))
        {
            if (!e.is_regular_file() || e.path().extension() != ".cfg")
                continue;
            const ConfigContent c = read(slurp(e.path()));
            files++;
            sections += c.sections.size();
        }
    CHECK(files > 800);
    CHECK(sections > 5000);
}

TEST_CASE("WriteBatch writes several files and creates directories", "[cfgc_writebatch]")
{
    TempDir t("ok");
    spit(t.path / "a.cfg", "old a");
    WriteBatch b;
    b.put((t.path / "a.cfg").string(), "new a");
    b.put((t.path / "sub" / "deep" / "b.cfg").string(), "new b");
    std::string err;
    REQUIRE(b.commit(&err));
    CHECK(slurp(t.path / "a.cfg") == "new a");
    CHECK(slurp(t.path / "sub" / "deep" / "b.cfg") == "new b");
    CHECK_FALSE(fs::exists(t.path / "a.cfg.kfxold"));
    CHECK_FALSE(fs::exists(t.path / "a.cfg.kfxnew"));
    CHECK(b.empty());
}

TEST_CASE("WriteBatch removes files and tolerates missing ones", "[cfgc_writebatch]")
{
    TempDir t("rm");
    spit(t.path / "gone.cfg", "x");
    WriteBatch b;
    b.remove((t.path / "gone.cfg").string());
    b.remove((t.path / "never_existed.cfg").string());
    REQUIRE(b.commit());
    CHECK_FALSE(fs::exists(t.path / "gone.cfg"));
    CHECK_FALSE(fs::exists(t.path / "gone.cfg.kfxold"));
}

TEST_CASE("WriteBatch keeps only the last operation on a path", "[cfgc_writebatch]")
{
    TempDir t("dup");
    const std::string p = (t.path / "f.cfg").string();
    WriteBatch b;
    b.put(p, "first");
    b.remove(p);
    b.put(p, "last");
    CHECK(b.ops().size() == 1);
    REQUIRE(b.commit());
    CHECK(slurp(p) == "last");
}

TEST_CASE("WriteBatch that fails while staging changes nothing", "[cfgc_writebatch]")
{
    TempDir t("stage");
    spit(t.path / "a.cfg", "old a");
    spit(t.path / "blocker", "i am a file");
    WriteBatch b;
    b.put((t.path / "a.cfg").string(), "new a");
    b.put((t.path / "blocker" / "b.cfg").string(), "new b"); // parent is a file
    std::string err;
    CHECK_FALSE(b.commit(&err));
    CHECK_FALSE(err.empty());
    CHECK(slurp(t.path / "a.cfg") == "old a");
    CHECK_FALSE(fs::exists(t.path / "a.cfg.kfxnew"));
}

TEST_CASE("WriteBatch that fails while applying restores every file", "[cfgc_writebatch]")
{
    TempDir t("apply");
    spit(t.path / "a.cfg", "old a");
    spit(t.path / "b.cfg", "old b");
    spit(t.path / "c.cfg", "old c");
    // A non-empty directory where c's backup must go makes the third step fail
    // after the first two were already applied.
    fs::create_directories(t.path / "c.cfg.kfxold" / "inner");
    spit(t.path / "c.cfg.kfxold" / "inner" / "x", "x");

    WriteBatch b;
    b.put((t.path / "a.cfg").string(), "new a");
    b.remove((t.path / "b.cfg").string());
    b.put((t.path / "c.cfg").string(), "new c");
    std::string err;
    CHECK_FALSE(b.commit(&err));
    CHECK(err.find("c.cfg") != std::string::npos);
    CHECK(slurp(t.path / "a.cfg") == "old a");
    CHECK(slurp(t.path / "b.cfg") == "old b");
    CHECK(slurp(t.path / "c.cfg") == "old c");
    CHECK_FALSE(fs::exists(t.path / "a.cfg.kfxnew"));
    CHECK_FALSE(fs::exists(t.path / "c.cfg.kfxnew"));
    CHECK_FALSE(fs::exists(t.path / "a.cfg.kfxold"));
}

TEST_CASE("empty WriteBatch succeeds", "[cfgc_writebatch]")
{
    WriteBatch b;
    CHECK(b.commit());
}
