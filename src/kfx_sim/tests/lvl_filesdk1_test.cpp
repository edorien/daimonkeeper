// kfx_sim coverage: lvl_filesdk1.c's only pure, fixture-free function --
// get_level_number_from_file_name parses "map<N>" file-name prefixes into
// a level number. Everything else in this file is level/column/slab file
// I/O (load_*_file), needing real game data files on disk, so it's
// genuinely untestable at the unit level and left alone entirely --
// except regenerate_derived_map_data(), which needs no file I/O at all
// (it derives columns from already-in-memory slab kinds); see its own
// coverage in lvl_filesdk1_regen_test.cpp.
#include <catch2/catch_test_macros.hpp>

#include "lvl_filesdk1.h"
#include "config.h"

TEST_CASE("get_level_number_from_file_name parses a map<N> prefix, case-insensitively", "[kfx_sim][lvl_filesdk1]") {
    CHECK(get_level_number_from_file_name("map01") == 1);
    CHECK(get_level_number_from_file_name("map123") == 123);
    CHECK(get_level_number_from_file_name("MAP7") == 7);
}

TEST_CASE("get_level_number_from_file_name rejects names without the map prefix or with a non-positive number", "[kfx_sim][lvl_filesdk1]") {
    CHECK(get_level_number_from_file_name("level01") == SINGLEPLAYER_NOTSTARTED);
    CHECK(get_level_number_from_file_name("map") == SINGLEPLAYER_NOTSTARTED); // no digits at all
    CHECK(get_level_number_from_file_name("map0") == SINGLEPLAYER_NOTSTARTED); // parses to 0
    CHECK(get_level_number_from_file_name("map-5") == SINGLEPLAYER_NOTSTARTED); // negative
}
