// kfx_sim: map_locations.c's TbMapLocation bit-packing accessors --
// get_map_location_type/_longval/_plyrval and get_coord_encoded_location
// are pure bitfield math (type in bits 0-3, longval = >>4, plyrval =
// >>12), no world/fixture state at all. get_map_location_code_name's
// MLoc_HEROGATE branch is pure too (just an i<=0 guard plus snprintf);
// its MLoc_PLAYERSHEART branch reaches config_players.h's public,
// already-populated-at-compile-time player_desc[] table (no fixture
// needed, unlike MLoc_ACTIONPOINT/MLoc_CREATUREKIND/MLoc_ROOMKIND, which
// read real per-level/per-config state via action_point_get()/
// creature_desc[]/room_desc[] and aren't attempted here).
#include <catch2/catch_test_macros.hpp>

#include "map_locations.h"
#include "config_players.h" // player_desc[], PLAYER0

#include <cstring>
#include <string>

TEST_CASE("get_coord_encoded_location packs subtile x/y (masked to 12 bits) and the MLoc_COORDS type tag", "[kfx_sim][map_locations]") {
    TbMapLocation loc = get_coord_encoded_location(100, 200);
    CHECK((loc & 0x0F) == MLoc_COORDS);
    CHECK(((loc >> 20) & 0x0FFF) == 100);
    CHECK(((loc >> 8) & 0x0FFF) == 200);
}

TEST_CASE("get_coord_encoded_location masks each coordinate to 12 bits", "[kfx_sim][map_locations]") {
    TbMapLocation loc = get_coord_encoded_location(0x1FFF, 0x1FFF); // 13 bits set, only low 12 kept
    CHECK(((loc >> 20) & 0x0FFF) == 0x0FFF);
    CHECK(((loc >> 8) & 0x0FFF) == 0x0FFF);
}

TEST_CASE("get_map_location_type extracts the low 4 bits", "[kfx_sim][map_locations]") {
    CHECK(get_map_location_type(0) == MLoc_NONE);
    CHECK(get_map_location_type(MLoc_PLAYERSHEART) == MLoc_PLAYERSHEART);
    CHECK(get_map_location_type((7 << 4) | MLoc_HEROGATE) == MLoc_HEROGATE);
}

TEST_CASE("get_map_location_longval extracts everything above the type nibble", "[kfx_sim][map_locations]") {
    TbMapLocation loc = (123U << 4) | MLoc_HEROGATE;
    CHECK(get_map_location_longval(loc) == 123);
}

TEST_CASE("get_map_location_plyrval extracts everything above bit 12", "[kfx_sim][map_locations]") {
    TbMapLocation loc = (7U << 12) | (5U << 4) | MLoc_CREATUREKIND;
    CHECK(get_map_location_plyrval(loc) == 7);
}

TEST_CASE("get_map_location_code_name formats a negated gate number for MLoc_HEROGATE", "[kfx_sim][map_locations]") {
    char name[MAX_TEXT_LENGTH];
    TbMapLocation loc = (3U << 4) | MLoc_HEROGATE;
    CHECK(get_map_location_code_name(loc, name));
    CHECK(std::strcmp(name, "-3") == 0);
}

TEST_CASE("get_map_location_code_name reports MLoc_HEROGATE invalid for a non-positive gate number", "[kfx_sim][map_locations]") {
    char name[MAX_TEXT_LENGTH];
    TbMapLocation loc = (0U << 4) | MLoc_HEROGATE;
    CHECK_FALSE(get_map_location_code_name(loc, name));
    CHECK(std::strcmp(name, "INVALID") == 0);
}

TEST_CASE("get_map_location_code_name resolves MLoc_PLAYERSHEART through the real player_desc[] table", "[kfx_sim][map_locations]") {
    char name[MAX_TEXT_LENGTH];
    TbMapLocation loc = ((uint64_t)PLAYER0 << 4) | MLoc_PLAYERSHEART;
    CHECK(get_map_location_code_name(loc, name));
    CHECK(std::strcmp(name, "PLAYER0") == 0);
}

TEST_CASE("get_map_location_code_name falls back to INVALID for an unhandled location type", "[kfx_sim][map_locations]") {
    char name[MAX_TEXT_LENGTH];
    TbMapLocation loc = MLoc_THING; // no case in the switch produces a name for this type
    CHECK_FALSE(get_map_location_code_name(loc, name));
    CHECK(std::strcmp(name, "INVALID") == 0);
}

// COMBAT[...]-style locations name their player in brackets: a player name (PLAYER1) or, since upstream #5444, a
// colour (BLUE), as objective targets already allowed. The bracket text used to be copied into a 64-byte stack
// buffer with its terminator written at the bracket length, past the buffer for a longer name (a level script
// or Lua location string can be up to 4096 bytes).
TEST_CASE("get_map_location_id_f: COMBAT[...] takes a player name or a colour", "[kfx_sim][map_locations]") {
    TbMapLocation by_name = MLoc_NONE;
    TbMapLocation by_colour = MLoc_NONE;
    REQUIRE(get_map_location_id_f("COMBAT[PLAYER1]", &by_name, "test", 0));
    REQUIRE(get_map_location_id_f("COMBAT[BLUE]", &by_colour, "test", 0));
    CHECK(by_colour == by_name);
    CHECK(get_map_location_type(by_name) == MLoc_METALOCATION);
}

TEST_CASE("get_map_location_id_f: a COMBAT[...] name longer than the name buffer is refused, not overflowed", "[kfx_sim][map_locations]") {
    std::string locname = "COMBAT[" + std::string(300, 'X') + "]";
    TbMapLocation location = 1;
    CHECK_FALSE(get_map_location_id_f(locname.c_str(), &location, "test", 0));
    CHECK(location == MLoc_NONE);
    // A long bracket that starts with a real name is still not that name (the copy is cut, not matched)
    locname = "COMBAT[PLAYER1" + std::string(300, ' ') + "]";
    CHECK_FALSE(get_map_location_id_f(locname.c_str(), &location, "test", 0));
}
