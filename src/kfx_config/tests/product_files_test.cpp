// kfx_config: this game's own file names (docs/rebadge, local notes) --
// the base config is daimonkeeper.cfg (seeded once from an existing
// keeperfx.cfg, which is never written). Saves, replays and fxdata/ keep
// KeeperFX's folders: a separate install folder (sharing data through
// symlinks if wanted) keeps the two games apart.
#include <catch2/catch_test_macros.hpp>

#include "config.h"
#include "config_keeperfx.h"
#include "version.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <unistd.h>

namespace {
std::string scratch(const char *name)
{
    return "product_files_test_" + std::to_string(getpid()) + "_" + name;
}

std::string read_all(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void write_all(const std::string &path, const std::string &text)
{
    std::ofstream f(path, std::ios::binary);
    f << text;
}

bool ends_with(const std::string &s, const std::string &tail)
{
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}
}

TEST_CASE("the base config is seeded once from KeeperFX's, which is left alone", "[kfx_config][product_files]") {
    const std::string ours = scratch(PRODUCT_SLUG ".cfg");
    const std::string theirs = scratch("keeperfx.cfg");
    const std::string settings = "INSTALL_PATH=./\nLANGUAGE=ENG\n";
    std::remove(ours.c_str());
    write_all(theirs, settings);

    CHECK(import_kfx_base_config(ours.c_str(), theirs.c_str()));
    CHECK(read_all(ours) == settings);
    CHECK(read_all(theirs) == settings);

    // Ours exists now: later runs never overwrite it.
    write_all(ours, "LANGUAGE=GER\n");
    CHECK_FALSE(import_kfx_base_config(ours.c_str(), theirs.c_str()));
    CHECK(read_all(ours) == "LANGUAGE=GER\n");

    std::remove(ours.c_str());
    std::remove(theirs.c_str());
}

TEST_CASE("no KeeperFX config to seed from: nothing is created", "[kfx_config][product_files]") {
    const std::string ours = scratch("fresh.cfg");
    const std::string theirs = scratch("missing_keeperfx.cfg");
    std::remove(ours.c_str());
    std::remove(theirs.c_str());
    CHECK_FALSE(import_kfx_base_config(ours.c_str(), theirs.c_str()));
    CHECK(read_all(ours).empty());
}

TEST_CASE("saves, replays and data use KeeperFX's folder names", "[kfx_config][product_files]") {
    char path[DISKPATH_SIZE];
    REQUIRE(prepare_file_path_buf(path, sizeof(path), FGrp_Save, "settings.toml") != nullptr);
    CHECK(ends_with(path, "/save/settings.toml"));
    REQUIRE(prepare_file_path_buf(path, sizeof(path), FGrp_Replays, "r.pck") != nullptr);
    CHECK(ends_with(path, "/replays/r.pck"));
    REQUIRE(prepare_file_path_buf(path, sizeof(path), FGrp_FxData, "rules.cfg") != nullptr);
    CHECK(ends_with(path, "/fxdata/rules.cfg"));
}
