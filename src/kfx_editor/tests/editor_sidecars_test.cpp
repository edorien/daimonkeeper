// Catch2 coverage for editor_sidecars.cpp -- which per-level files a Save As
// would leave behind (docs/refactor/editor/fx-plans/01-sidecar-files.md).
#include <catch2/catch_test_macros.hpp>

#include "editor_sidecars.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace {
void touch(const std::filesystem::path &p) { std::ofstream(p) << "x"; }
}

TEST_CASE("sidecar classification separates written/derived from KeeperFX-only files", "[kfx_editor][sidecars]") {
    for (const char *e : {"slb", "own", "txt", "lua", "tngfx", "lof", "LIF", "dat", "clm", "wib"})
        CHECK(editor_sidecar_ext_is_handled(e));
    for (const char *e : {"rules.cfg", "sounds.cfg", "zip", "tmapa014.dat"})
        CHECK_FALSE(editor_sidecar_ext_is_handled(e));
}

TEST_CASE("find_sidecars lists only this level's unhandled files", "[kfx_editor][sidecars]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "kfx_sidecar_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    touch(dir / "map07001.slb");
    touch(dir / "map07001.lua"); // saved with the map, not a sidecar
    touch(dir / "map07001.rules.cfg");
    touch(dir / "map07001.sounds.cfg");
    touch(dir / "map07002.rules.cfg"); // another level
    touch(dir / "readme.txt");

    const auto found = editor_find_sidecars(dir.string().c_str(), 7001);
    REQUIRE(found.size() == 2);
    CHECK(found[0] == "map07001.rules.cfg");
    CHECK(found[1] == "map07001.sounds.cfg");
    CHECK(editor_find_sidecars(dir.string().c_str(), 9).empty());
    CHECK(editor_find_sidecars("/nonexistent/kfx", 7001).empty());
    std::filesystem::remove_all(dir);
}

TEST_CASE("relocation detects a changed level number or folder", "[kfx_editor][sidecars]") {
    CHECK_FALSE(editor_save_is_relocation("a/b", 5, "a/b/", 5));
    CHECK(editor_save_is_relocation("a/b", 5, "a/b", 6));
    CHECK(editor_save_is_relocation("a/b", 5, "a/c", 5));
}

TEST_CASE("copy renames sidecars to the target level and remove deletes them", "[kfx_editor][sidecars]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "kfx_sidecar_copy_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    touch(dir / "map00007.sounds.cfg");
    touch(dir / "map00007.rules.cfg");
    touch(dir / "map00007.slb");
    CHECK(editor_copy_sidecars(dir.string().c_str(), 7, dir.string().c_str(), 900002) == 2);
    CHECK(std::filesystem::exists(dir / "map900002.sounds.cfg"));
    CHECK(std::filesystem::exists(dir / "map900002.rules.cfg"));
    CHECK_FALSE(std::filesystem::exists(dir / "map900002.slb"));
    CHECK(editor_remove_sidecars(dir.string().c_str(), 900002) == 2);
    CHECK(editor_find_sidecars(dir.string().c_str(), 900002).empty());
    CHECK(std::filesystem::exists(dir / "map00007.sounds.cfg"));
    std::filesystem::remove_all(dir);
}
