// os.time, os.date and os.clock as the scripts see them (lua_base.c, synchronise_lua_clock()). While the game has to
// play the same everywhere -- a multiplayer game, a replay being recorded or played back -- they follow the game's
// synchronised clock (kfx_sim_state.timestamp*, the clock GetGameTimestamp reads, plus the game turns since), so a
// level script that uses them can't make a replay diverge. Otherwise they are the host's real clock.
#include "lua_script_fixture.h"
#include "globals.h"     // set_gameturn_source
#include "packet_data.h" // replay

#include <ctime>
#include <string>

namespace {

// Evaluates `expr` in the scripts' state; a Lua error comes back as "ERROR: <message>".
std::string lua_eval(const std::string &expr) {
    const std::string chunk = "return tostring(" + expr + ")";
    REQUIRE(luaL_loadstring(Lvl_script, chunk.c_str()) == LUA_OK);
    if (lua_pcall(Lvl_script, 0, 1, 0) != LUA_OK) {
        std::string message = std::string("ERROR: ") + (lua_isstring(Lvl_script, -1) ? lua_tostring(Lvl_script, -1) : "?");
        lua_pop(Lvl_script, 1);
        return message;
    }
    std::string out = lua_tostring(Lvl_script, -1);
    lua_pop(Lvl_script, 1);
    return out;
}

// The scripts' state, with the game's clock set: 1000000000 is 2001-09-09 01:46:40 UTC (a Sunday); the host's zone is
// UTC+1; the stamp was taken on turn 100 and it is now turn 207, 5.35 seconds of game time later.
struct GameClockFixture : LuaScriptFixture {
    GameTurn turn = 207;
    GameClockFixture() {
        set_gameturn_source(&turn);
        kfx_sim_state.timestamp = 1000000000;
        kfx_sim_state.timestamp_tz = 3600;
        kfx_sim_state.timestamp_turn = 100;
        replay.save_enable = 0;
        replay.load_enable = 0;
    }
    ~GameClockFixture() {
        replay.save_enable = 0;
        replay.load_enable = 0;
        set_gameturn_source(nullptr);
    }
};

const char *const kClockReport =
    "table.concat({os.time(), os.clock(), os.date('%Y-%m-%d %H:%M:%S'), os.date('!%Y-%m-%d %H:%M:%S'),"
    " os.date(), os.date('*t').hour, os.date('*t').wday, os.date('*t').yday}, '|')";

std::string clock_report_while_recording() {
    GameClockFixture fx;
    replay.save_enable = 1;
    return lua_eval(kClockReport);
}

} // namespace

TEST_CASE("while a replay is recorded os.time, os.date and os.clock follow the game's clock", "[kfx_script][lua_game_clock]") {
    const std::string first = clock_report_while_recording();
    CHECK(first == "1000000005|10.35|2001-09-09 02:46:45|2001-09-09 01:46:45|Sun Sep  9 02:46:45 2001|2|1|252");
    // a second game recorded from the same synchronised state sees exactly the same
    CHECK(clock_report_while_recording() == first);
}

TEST_CASE_METHOD(GameClockFixture, "while a replay is played back the scripts see the recorded clock", "[kfx_script][lua_game_clock]") {
    replay.load_enable = 1;
    CHECK(lua_eval("os.time()") == "1000000005");
    turn = 100 + 20 * 60; // a minute of game time later
    CHECK(lua_eval("os.time()") == "1000000060");
    CHECK(lua_eval("os.date('%H:%M:%S')") == "02:47:40");
    CHECK(lua_eval("os.clock()") == "65");
    // a turn before the stamp's (a stamp taken later, then a rewind) holds at the stamp
    turn = 50;
    CHECK(lua_eval("os.time()") == "1000000000");
}

TEST_CASE_METHOD(GameClockFixture, "in a multiplayer game the scripts get the game's clock, not an error", "[kfx_script][lua_game_clock]") {
    kfx_sim_state.game_kind = GKind_MultiGame;
    CHECK(lua_eval("os.time()") == "1000000005");
    CHECK(lua_eval("os.date('%H:%M')") == "02:46");
    CHECK(lua_eval("os.clock()") == "10.35");
}

TEST_CASE_METHOD(GameClockFixture, "the game's calendar: os.time of a table, os.date of a time", "[kfx_script][lua_game_clock]") {
    replay.save_enable = 1;
    // a date table is read in the game's zone (UTC+1 here), whatever the zone of the machine
    CHECK(lua_eval("os.time{year=2001, month=9, day=9, hour=2, min=46, sec=40}") == "1000000000");
    CHECK(lua_eval("os.time{year=2001, month=9, day=9}") == "1000033200");                  // hour defaults to 12
    CHECK(lua_eval("os.time{year=2000, month=14, day=0, hour=0}") == "980895600");          // normalised: 2001-01-31
    CHECK(lua_eval("os.time{year=1970, month=1, day=1, hour=1}") == "0");
    CHECK(lua_eval("os.time{year=2001, month=9}").rfind("ERROR:", 0) == 0);               // day is required
    CHECK(lua_eval("os.time{year=1e300, month=1, day=1}") == "nil");                        // out of range
    CHECK(lua_eval("os.date('%c', 0)") == "Thu Jan  1 01:00:00 1970");
    CHECK(lua_eval("os.date('!%c', 0)") == "Thu Jan  1 00:00:00 1970");
    CHECK(lua_eval("os.date('!%a %A %b %B %d %e %j %m %y %C %p %I %u %w %U %W %z %Z %%', 1000000000)") ==
          "Sun Sunday Sep September 09  9 252 09 01 20 AM 01 7 0 36 36 +0000 UTC %");
    CHECK(lua_eval("os.date('%z %Z %D %F %T %R %r %x %X', 1000000000)") ==
          "+0100 +0100 09/09/01 2001-09-09 02:46:40 02:46 02:46:40 AM 09/09/01 02:46:40");
    CHECK(lua_eval("os.date('!%G-W%V-%u %g', 1609675200)") == "2020-W53-7 20");             // 2021-01-03, ISO week
    CHECK(lua_eval("os.date('!%Y-%m-%d', -86400)") == "1969-12-31");
    CHECK(lua_eval("os.date('!*t', 951782400).day") == "29");                               // 2000-02-29
    CHECK(lua_eval("os.date('*t').isdst") == "false");
    CHECK(lua_eval("os.date('%Q')").rfind("ERROR:", 0) == 0);                               // no such conversion
    CHECK(lua_eval("os.date('%')").rfind("ERROR:", 0) == 0);
    CHECK(lua_eval("os.difftime(os.time(), os.time{year=2001, month=9, day=9, hour=2, min=46, sec=40})") == "5");
}

TEST_CASE_METHOD(GameClockFixture, "otherwise the scripts see the host's clock", "[kfx_script][lua_game_clock]") {
    const double now = (double)std::time(nullptr);
    const double script_now = std::stod(lua_eval("os.time()"));
    CHECK(script_now >= now - 2);
    CHECK(script_now <= now + 2);
    CHECK(lua_eval("os.clock() >= 0") == "true");
}
