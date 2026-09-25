// docs/refactor/editor/05-script-and-level-settings.md §4.2 -- Catch2
// coverage for script_setup.cpp's parser/generator: pure text
// logic, no live editor session needed for most of it. The creature-pool
// round-trip needs creature_desc[] (kfx_config's dynamically-populated-at-
// config-load-time name table) to resolve "TROLL"-style names to
// ThingModel ids -- a bare Catch2 binary never loads real creature config,
// so CreatureDescFixture below writes a couple of synthetic entries
// directly (get_rid()'s own scan stops at the first NULL .name, confirmed
// in bflib_basics.c, so a short synthetic prefix is safe) and restores the
// whole array afterward, the same snapshot-and-restore shape editor_
// session_test.cpp's own CampaignLevelsLocationFixture already uses for a
// different global.
#include <catch2/catch_test_macros.hpp>

#include "script_setup.h"
#include "config_creature.h"
#include "config_terrain.h"
#include "config_magic.h"
#include "config_trapdoor.h"

#include <cstring>

namespace {

struct CreatureDescFixture {
    struct NamedCommand saved[CREATURE_TYPES_MAX];
    CreatureDescFixture() {
        std::memcpy(saved, creature_desc, sizeof(saved));
        creature_desc[0].name = "TROLL";
        creature_desc[0].num = 5;
        creature_desc[1].name = "IMP";
        creature_desc[1].num = 1;
        creature_desc[2].name = nullptr;
        creature_desc[2].num = 0;
    }
    ~CreatureDescFixture() {
        std::memcpy(creature_desc, saved, sizeof(saved));
    }
};

} // namespace

TEST_CASE("script_setup_extract_region returns empty when no markers exist", "[kfx_config][script_setup]") {
    CHECK(script_setup_extract_region("REM just a normal script\nSTART_MONEY(PLAYER0,1000)\n").empty());
}

TEST_CASE("script_setup_extract_region returns the body between markers", "[kfx_config][script_setup]") {
    std::string script =
        "REM before\n"
        "REM --- editor-managed setup: do not hand-edit between these markers ---\n"
        "SET_GENERATE_SPEED(300)\n"
        "START_MONEY(PLAYER0,20000)\n"
        "REM --- end editor-managed setup ---\n"
        "REM after\n";
    std::string body = script_setup_extract_region(script);
    CHECK(body == "SET_GENERATE_SPEED(300)\nSTART_MONEY(PLAYER0,20000)\n");
}

TEST_CASE("script_setup_replace_region inserts a fresh block when no markers exist", "[kfx_config][script_setup]") {
    std::string script = "REM Original hand-written script\nIF(PLAYER0,1)\nENDIF\n";
    std::string result = script_setup_replace_region(script, "SET_GENERATE_SPEED(300)\n");

    CHECK(result.find("REM --- editor-managed setup: do not hand-edit between these markers ---") == 0);
    CHECK(result.find("SET_GENERATE_SPEED(300)") != std::string::npos);
    CHECK(result.find("REM --- end editor-managed setup ---") != std::string::npos);
    // Original content is fully preserved, after the new block.
    CHECK(result.find("REM Original hand-written script\nIF(PLAYER0,1)\nENDIF\n") != std::string::npos);
}

TEST_CASE("script_setup_replace_region replaces only the body, preserving surrounding content", "[kfx_config][script_setup]") {
    std::string script =
        "REM before, hand-written\n"
        "REM --- editor-managed setup: do not hand-edit between these markers ---\n"
        "SET_GENERATE_SPEED(300)\n"
        "REM --- end editor-managed setup ---\n"
        "REM after, hand-written\n"
        "IF(PLAYER0,1)\nENDIF\n";
    std::string result = script_setup_replace_region(script, "SET_GENERATE_SPEED(999)\nSTART_MONEY(PLAYER0,5000)\n");

    CHECK(result.find("REM before, hand-written") != std::string::npos);
    CHECK(result.find("REM after, hand-written") != std::string::npos);
    CHECK(result.find("IF(PLAYER0,1)\nENDIF\n") != std::string::npos);
    CHECK(result.find("SET_GENERATE_SPEED(999)") != std::string::npos);
    CHECK(result.find("START_MONEY(PLAYER0,5000)") != std::string::npos);
    // The old body's own value is genuinely gone, not just appended alongside.
    CHECK(result.find("SET_GENERATE_SPEED(300)") == std::string::npos);
}

TEST_CASE("script_setup_parse reads generate speed/start money/max creatures", "[kfx_config][script_setup]") {
    std::string body =
        "SET_GENERATE_SPEED(600)\n"
        "START_MONEY(PLAYER0,20000)\n"
        "START_MONEY(PLAYER1,2460000)\n"
        "MAX_CREATURES(PLAYER0,25)\n"
        "MAX_CREATURES(PLAYER1,30)\n";
    ManagedSetupValues values = script_setup_parse(body, 2);

    CHECK(values.generate_speed == 600);
    REQUIRE(values.start_money.size() == 2);
    CHECK(values.start_money[0] == 20000);
    CHECK(values.start_money[1] == 2460000);
    REQUIRE(values.max_creatures.size() == 2);
    CHECK(values.max_creatures[0] == 25);
    CHECK(values.max_creatures[1] == 30);
}

TEST_CASE("script_setup_parse defaults missing per-player lines to 0", "[kfx_config][script_setup]") {
    // Only PLAYER0 has lines -- a level whose Level Settings dialog was
    // Applied with fewer players than it's since been raised to.
    std::string body = "SET_GENERATE_SPEED(300)\nSTART_MONEY(PLAYER0,1000)\n";
    ManagedSetupValues values = script_setup_parse(body, 3);

    REQUIRE(values.start_money.size() == 3);
    CHECK(values.start_money[0] == 1000);
    CHECK(values.start_money[1] == 0);
    CHECK(values.start_money[2] == 0);
    REQUIRE(values.max_creatures.size() == 3);
    CHECK(values.max_creatures[0] == 0);
}

TEST_CASE("script_setup_parse ignores blank lines and comments", "[kfx_config][script_setup]") {
    std::string body = "\nREM a stray comment somehow in here\nSET_GENERATE_SPEED(300)\n\n";
    ManagedSetupValues values = script_setup_parse(body, 1);
    CHECK(values.generate_speed == 300);
}

TEST_CASE("script_setup_generate produces the real shipped-script line format", "[kfx_config][script_setup]") {
    ManagedSetupValues values;
    values.generate_speed = 600;
    values.start_money = {20000, 2460000};
    values.max_creatures = {25, 30};

    std::string body = script_setup_generate(values, 2);

    CHECK(body ==
        "SET_GENERATE_SPEED(600)\n"
        "START_MONEY(PLAYER0,20000)\n"
        "START_MONEY(PLAYER1,2460000)\n"
        "MAX_CREATURES(PLAYER0,25)\n"
        "MAX_CREATURES(PLAYER1,30)\n");
}

TEST_CASE_METHOD(CreatureDescFixture, "script_setup_generate then parse round-trips the creature pool", "[kfx_config][script_setup]") {
    ManagedSetupValues original;
    original.generate_speed = 300;
    original.start_money = {1000};
    original.max_creatures = {10};
    original.creature_pool.push_back(std::make_pair((ThingModel)5, 30)); // TROLL
    original.creature_pool.push_back(std::make_pair((ThingModel)1, 5));  // IMP

    std::string body = script_setup_generate(original, 1);
    CHECK(body.find("ADD_CREATURE_TO_POOL(TROLL,30)") != std::string::npos);
    CHECK(body.find("ADD_CREATURE_TO_POOL(IMP,5)") != std::string::npos);

    ManagedSetupValues parsed = script_setup_parse(body, 1);
    REQUIRE(parsed.creature_pool.size() == 2);
    CHECK(parsed.creature_pool[0].first == 5);
    CHECK(parsed.creature_pool[0].second == 30);
    CHECK(parsed.creature_pool[1].first == 1);
    CHECK(parsed.creature_pool[1].second == 5);
}

TEST_CASE_METHOD(CreatureDescFixture, "script_setup_parse skips an unrecognized creature name", "[kfx_config][script_setup]") {
    std::string body = "ADD_CREATURE_TO_POOL(NOT_A_REAL_CREATURE,10)\nADD_CREATURE_TO_POOL(TROLL,5)\n";
    ManagedSetupValues values = script_setup_parse(body, 1);
    REQUIRE(values.creature_pool.size() == 1);
    CHECK(values.creature_pool[0].first == 5);
    CHECK(values.creature_pool[0].second == 5);
}

// docs/refactor/editor/phase5/05-slice5-availability-grid.md -- the five
// *_AVAILABLE commands. Only the first few entries of each name table are
// touched (and restored), so this doesn't depend on the tables' declared
// sizes; get_rid() stops at the first NULL name.
namespace {
struct AvailDescFixture {
    struct NamedCommand saved[5][4];
    struct NamedCommand *tables[5];
    AvailDescFixture() {
        tables[0] = creature_desc; tables[1] = room_desc; tables[2] = power_desc;
        tables[3] = trap_desc;     tables[4] = door_desc;
        static const char *const names[5][2] = {
            {"TROLL", "IMP"}, {"TREASURE", "LAIR"}, {"POWER_HAND", "POWER_SLAP"},
            {"POISON_GAS", "LAVA"}, {"WOOD", "STEEL"},
        };
        for (int64_t t = 0; t < 5; t++) {
            std::memcpy(saved[t], tables[t], sizeof(saved[t]));
            tables[t][0].name = names[t][0]; tables[t][0].num = 1;
            tables[t][1].name = names[t][1]; tables[t][1].num = 2;
            tables[t][2].name = nullptr;     tables[t][2].num = 0;
        }
    }
    ~AvailDescFixture() {
        for (int64_t t = 0; t < 5; t++)
            std::memcpy(tables[t], saved[t], sizeof(saved[t]));
    }
};
} // namespace

TEST_CASE_METHOD(AvailDescFixture, "availability lines parse for all five kinds", "[kfx_config][script_setup]") {
    std::string body =
        "CREATURE_AVAILABLE(ALL_PLAYERS,TROLL,1,0)\n"
        "ROOM_AVAILABLE(PLAYER0,LAIR,1,1)\n"
        "MAGIC_AVAILABLE(ALL_PLAYERS,POWER_HAND,1,0)\n"
        "TRAP_AVAILABLE(ALL_PLAYERS,LAVA,1,3)\n"
        "DOOR_AVAILABLE(PLAYER1,STEEL,0,0)\n";
    ManagedSetupValues v = script_setup_parse(body, 2);
    REQUIRE(v.availability.size() == 5);
    AvailabilityEntry *e = script_setup_availability_find(v, AvailKind_Room, 0, 2);
    REQUIRE(e != nullptr);
    CHECK(e->a == 1); CHECK(e->b == 1);
    e = script_setup_availability_find(v, AvailKind_Trap, -1, 2);
    REQUIRE(e != nullptr);
    CHECK(e->b == 3); // raw amount preserved, not collapsed to a state
    e = script_setup_availability_find(v, AvailKind_Door, 1, 2);
    REQUIRE(e != nullptr);
    CHECK(e->a == 0);
}

TEST_CASE_METHOD(AvailDescFixture, "availability generate/parse round-trips, ALL_PLAYERS before per-player", "[kfx_config][script_setup]") {
    ManagedSetupValues v;
    v.generate_speed = 0;
    v.availability.push_back({AvailKind_Room, 0, 1, 1, 0});   // PLAYER0 override, added first
    v.availability.push_back({AvailKind_Room, -1, 1, 1, 1});  // ALL_PLAYERS baseline
    v.availability.push_back({AvailKind_Magic, -1, 2, 0, 0});
    std::string body = script_setup_generate(v, 1);
    CHECK(body.find("ROOM_AVAILABLE(ALL_PLAYERS,TREASURE,1,1)") < body.find("ROOM_AVAILABLE(PLAYER0,TREASURE,1,0)"));
    CHECK(body.find("MAGIC_AVAILABLE(ALL_PLAYERS,POWER_SLAP,0,0)") != std::string::npos);
    ManagedSetupValues back = script_setup_parse(body, 1);
    CHECK(back.availability.size() == 3);
}

TEST_CASE_METHOD(AvailDescFixture, "availability: later line for the same kind/player/item wins; unknown names skipped", "[kfx_config][script_setup]") {
    std::string body =
        "ROOM_AVAILABLE(ALL_PLAYERS,TREASURE,1,1)\n"
        "ROOM_AVAILABLE(ALL_PLAYERS,TREASURE,0,0)\n"
        "ROOM_AVAILABLE(ALL_PLAYERS,NOT_A_ROOM,1,1)\n"
        "ROOM_AVAILABLE(PLAYER_GOOD,LAIR,1,1)\n";
    ManagedSetupValues v = script_setup_parse(body, 1);
    REQUIRE(v.availability.size() == 1);
    CHECK(v.availability[0].a == 0);
}

TEST_CASE("generator omits unset (zero) generation speed, gold and max creatures", "[kfx_config][script_setup]") {
    ManagedSetupValues v;
    v.generate_speed = 0;
    v.start_money = {0, 500};
    v.max_creatures = {0, 0};
    std::string body = script_setup_generate(v, 2);
    CHECK(body == "START_MONEY(PLAYER1,500)\n");
}

TEST_CASE("win/lose rules round-trip through generate and parse", "[kfx_config][script_setup]") {
    ManagedSetupValues v;
    v.generate_speed = 0;
    WinLoseRule lose;
    lose.win = false;
    lose.clauses.push_back(WinLoseClause()); // PLAYER0 ALL_DUNGEONS_DESTROYED == 1
    WinLoseRule win;
    win.win = true;
    WinLoseClause a; a.player = 1; a.variable = "DUNGEON_DESTROYED"; a.op = ">="; a.value = 1;
    WinLoseClause b; b.player = 0; b.variable = "MONEY"; b.op = ">"; b.value = 5000;
    win.clauses = {a, b};
    v.rules = {lose, win};
    const std::string body = script_setup_generate(v, 2);
    CHECK(body ==
        "IF(PLAYER0,ALL_DUNGEONS_DESTROYED == 1)\n\tLOSE_GAME\nENDIF\n"
        "IF(PLAYER1,DUNGEON_DESTROYED >= 1)\n\tIF(PLAYER0,MONEY > 5000)\n\t\tWIN_GAME\n\tENDIF\nENDIF\n");
    const ManagedSetupValues p = script_setup_parse(body, 2);
    REQUIRE(p.rules.size() == 2);
    CHECK_FALSE(p.rules[0].win);
    REQUIRE(p.rules[1].clauses.size() == 2);
    CHECK(p.rules[1].clauses[0].op == ">=");
    CHECK(p.rules[1].clauses[1].value == 5000);
    CHECK(script_setup_generate(p, 2) == body);
}

TEST_CASE("rule parsing accepts hand-written spacing and skips blocks that are not plain rules", "[kfx_config][script_setup]") {
    const std::string body =
        "IF(PLAYER1, DUNGEON_DESTROYED>=1)\n  WIN_GAME\nENDIF\n"
        "IF(PLAYER0,GAME_TURN > 100)\n  DISPLAY_OBJECTIVE(1,ALL_PLAYERS)\n  WIN_GAME\nENDIF\n"
        "IF_AVAILABLE(PLAYER0,GUARD_POST == 1)\n  WIN_GAME\nENDIF\n"
        "IF(PLAYER0,FLAG0 >= x)\n  LOSE_GAME\nENDIF\n"
        "START_MONEY(PLAYER0,500)\n";
    const ManagedSetupValues p = script_setup_parse(body, 1);
    REQUIRE(p.rules.size() == 1);
    CHECK(p.rules[0].clauses[0].variable == "DUNGEON_DESTROYED");
    CHECK(p.rules[0].clauses[0].op == ">=");
    CHECK(p.start_money[0] == 500);
}

// Engine semantics (kfx_game lvl_script.c): preload_script() pre-scans the
// WHOLE file for LEVEL_VERSION before the real pass, so a managed block
// inserted ahead of a LEVEL_VERSION(1) line is still parsed at v1. What does
// differ is v0 (no LEVEL_VERSION): CREATURE_AVAILABLE(p,c,a,b) ignores `a`
// and uses `b` as "available" (lvl_script_commands_old.c).
TEST_CASE("script_setup_level_version finds LEVEL_VERSION anywhere in the script", "[kfx_config][script_setup]") {
    CHECK(script_setup_level_version("REM x\nSTART_MONEY(PLAYER0,1)\n") == 0);
    CHECK(script_setup_level_version("LEVEL_VERSION(1)\nREM x\n") == 1);
    CHECK(script_setup_level_version("REM x\n\tLEVEL_VERSION(1)\n") == 1);
    CHECK(script_setup_level_version("REM LEVEL_VERSION(1)\n") == 0);
    // Block inserted before the version line does not hide it.
    const std::string s = script_setup_replace_region("LEVEL_VERSION(1)\n", "SET_GENERATE_SPEED(9)\n");
    CHECK(s.find("LEVEL_VERSION") > s.find("SET_GENERATE_SPEED"));
    CHECK(script_setup_level_version(s) == 1);
}

TEST_CASE_METHOD(AvailDescFixture, "v1 script: creature 'available' is written a=1,b=0", "[kfx_config][script_setup]") {
    ManagedSetupValues v;
    v.generate_speed = 0;
    v.availability.push_back({AvailKind_Creature, -1, 1, 1, 0});
    const std::string script = "LEVEL_VERSION(1)\n";
    const std::string body = script_setup_generate(v, 1, script_setup_level_version(script));
    CHECK(body == "CREATURE_AVAILABLE(ALL_PLAYERS,TROLL,1,0)\n");
    const ManagedSetupValues back = script_setup_parse(body, 1, 1);
    REQUIRE(back.availability.size() == 1);
    CHECK(back.availability[0].a == 1);
    CHECK(back.availability[0].b == 0);
}

TEST_CASE_METHOD(AvailDescFixture, "v0 script: creature 'available' must land in the 4th argument", "[kfx_config][script_setup]") {
    ManagedSetupValues v;
    v.generate_speed = 0;
    v.availability.push_back({AvailKind_Creature, -1, 1, 1, 0}); // available
    v.availability.push_back({AvailKind_Creature, -1, 2, 0, 0}); // off
    v.availability.push_back({AvailKind_Room, -1, 1, 1, 1});
    const std::string script = "REM no LEVEL_VERSION here\n";
    const int64_t ver = script_setup_level_version(script);
    REQUIRE(ver == 0);
    const std::string body = script_setup_generate(v, 1, ver);
    // The v1 form CREATURE_AVAILABLE(...,1,0) would read as available=0 at v0.
    CHECK(body.find("CREATURE_AVAILABLE(ALL_PLAYERS,TROLL,1,1)") != std::string::npos);
    CHECK(body.find("CREATURE_AVAILABLE(ALL_PLAYERS,IMP,0,0)") != std::string::npos);
    CHECK(body.find("ROOM_AVAILABLE(ALL_PLAYERS,TREASURE,1,1)") != std::string::npos); // unchanged
    const ManagedSetupValues back = script_setup_parse(body, 1, ver);
    AvailabilityEntry *e = script_setup_availability_find(const_cast<ManagedSetupValues &>(back), AvailKind_Creature, -1, 1);
    REQUIRE(e != nullptr);
    CHECK(e->a == 1);
    CHECK(e->b == 0);
    // A hand-written v0 line reads its 4th argument as "available".
    const ManagedSetupValues hand = script_setup_parse("CREATURE_AVAILABLE(ALL_PLAYERS,TROLL,1,0)\n", 1, 0);
    REQUIRE(hand.availability.size() == 1);
    CHECK(hand.availability[0].a == 0);
}
