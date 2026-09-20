// Catch2 coverage for the pure parts of editor_mappack.cpp.
#include <catch2/catch_test_macros.hpp>

#include "editor_mappack.h"

#include <filesystem>
#include <fstream>

TEST_CASE("next free level number skips maps already in the folder", "[kfx_editor][mappack]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "kfx_mappack_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    CHECK(editor_maps_next_free_number(dir.string().c_str()) == 1);
    std::ofstream(dir / "map00001.slb") << "x";
    std::ofstream(dir / "map00002.slb") << "x";
    CHECK(editor_maps_next_free_number(dir.string().c_str()) == 3);
    std::filesystem::remove(dir / "map00001.slb");
    CHECK(editor_maps_next_free_number(dir.string().c_str()) == 1);
    std::filesystem::remove_all(dir);
}
