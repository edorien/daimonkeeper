// kfx_game: lvl_script.c's script_preflight_text()/_file() -- a level script's
// unknown commands, found without loading it (the level/campaign lists'
// "needs a newer KeeperFX" markers). Same rules as load_script(): REM and
// block comments skipped, LEVEL_VERSION picks the command table.
//
// The last case is the content regression sweep: every level script this game
// ships must preflight clean.
#include <catch2/catch_test_macros.hpp>

#include "lvl_script.h"

#include <filesystem>
#include <regex>
#include <string>

namespace {
struct ScriptPreflight preflight(const std::string &text)
{
    struct ScriptPreflight r;
    script_preflight_text(text.c_str(), (int64_t)text.size(), &r);
    return r;
}
}

TEST_CASE("an unknown command is found, with the line it first appears on", "[kfx_game][script_preflight]") {
    const auto r = preflight("LEVEL_VERSION(1)\nREM FUTURE_COMMAND(1) is only a comment\n"
                             "/* FUTURE_COMMAND(2)\n   still a comment */\nSTART_MONEY(PLAYER0,1000)\n"
                             "future_command(PLAYER0)\nFUTURE_COMMAND(PLAYER1)\n");
    CHECK(r.file_found);
    REQUIRE(r.unknown_count == 1);                  // once, however often it's used
    CHECK(std::string(r.names[0]) == "FUTURE_COMMAND"); // case-insensitive, like the parser
    CHECK(r.lines[0] == 6);
}

TEST_CASE("LEVEL_VERSION decides which commands exist, for every line", "[kfx_game][script_preflight]") {
    // A made-up command is unknown under either version.
    CHECK(preflight("FUTURE_COMMAND(1)\n").unknown_count == 1);
    CHECK(preflight("LEVEL_VERSION(1)\nFUTURE_COMMAND(1)\n").unknown_count == 1);
    // Version 1 is read, and applies to lines before it too, as in load_script():
    // SET_CREATURE_FEAR is original-DK-only, so it's skipped (the other table's) only once version 1 is known.
    CHECK(preflight("SET_CREATURE_FEAR(THIEF,1)\nLEVEL_VERSION(1)\nFUTURE_COMMAND(1)\n").unknown_count == 1);
}

TEST_CASE("a command of the other LEVEL_VERSION's table is the level's mistake, not a newer KeeperFX's", "[kfx_game][script_preflight]") {
    // It fails the same way in KeeperFX, so it isn't something a newer one would have.
    CHECK(preflight("ADD_GOLD_TO_PLAYER(PLAYER0,100)\n").unknown_count == 0);       // KeeperFX command, no LEVEL_VERSION
    CHECK(preflight("LEVEL_VERSION(1)\nSET_CREATURE_FEAR(THIEF,255)\n").unknown_count == 0); // original-DK-only command
}

TEST_CASE("only the first few names are kept, but all are counted", "[kfx_game][script_preflight]") {
    std::string text = "LEVEL_VERSION(1)\n";
    for (int i = 0; i < SCRIPT_PREFLIGHT_NAMES_MAX + 3; i++)
        text += "FUTURE_" + std::to_string(i) + "(1)\n";
    const auto r = preflight(text);
    CHECK(r.unknown_count == SCRIPT_PREFLIGHT_NAMES_MAX + 3);
    CHECK(std::string(r.names[SCRIPT_PREFLIGHT_NAMES_MAX - 1]) == "FUTURE_" + std::to_string(SCRIPT_PREFLIGHT_NAMES_MAX - 1));
}

TEST_CASE("a missing file is reported as not found", "[kfx_game][script_preflight]") {
    struct ScriptPreflight r;
    CHECK_FALSE(script_preflight_file("no/such/map00001.txt", &r));
    CHECK_FALSE(r.file_found);
    CHECK(r.unknown_count == 0);
}

TEST_CASE("every level script this game ships uses only commands it knows", "[kfx_game][script_preflight]") {
    namespace fs = std::filesystem;
    int64_t scripts = 0;
    for (const char *dir : {"levels", "campgns"})
    {
        for (const auto &entry : fs::recursive_directory_iterator(fs::path(KFX_GAME_TEST_REPO_ROOT) / dir))
        {
            const std::string name = entry.path().filename().string();
            // Level scripts only: map00001.txt, not map-pack lists like mappck_order.txt.
            if (!entry.is_regular_file() || !std::regex_match(name, std::regex("map[0-9]{5}\\.txt", std::regex::icase)))
                continue;
            struct ScriptPreflight r;
            REQUIRE(script_preflight_file(entry.path().string().c_str(), &r));
            scripts++;
            INFO(entry.path().string() << ": " << (r.unknown_count ? r.names[0] : "") << " line " << r.lines[0]);
            CHECK(r.unknown_count == 0);
        }
    }
    CHECK(scripts > 100); // the sweep really saw the bundled content
}

#include <cstdlib>

TEST_CASE("optional: level scripts in another install, reported not failed", "[kfx_game][script_preflight]") {
    // Local only (third-party maps aren't in the repo): point KFX_PREFLIGHT_EXTRA_DIR at a
    // game install -- e.g. one with workshop maps -- to see which would get the marker.
    // A map there may genuinely need a newer KeeperFX, so findings are listed, not failures.
    const char *extra = std::getenv("KFX_PREFLIGHT_EXTRA_DIR");
    if ((extra == nullptr) || (extra[0] == '\0'))
        SKIP("KFX_PREFLIGHT_EXTRA_DIR not set");
    namespace fs = std::filesystem;
    int64_t scripts = 0, flagged = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(extra, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec))
    {
        const std::string name = it->path().filename().string();
        if (!it->is_regular_file() || !std::regex_match(name, std::regex("map[0-9]{5}\\.txt", std::regex::icase)))
            continue;
        struct ScriptPreflight r;
        if (!script_preflight_file(it->path().string().c_str(), &r))
            continue;
        scripts++;
        if (r.unknown_count > 0)
        {
            flagged++;
            WARN(it->path().string() << ": " << r.names[0] << " (line " << r.lines[0] << ")"
                 << (r.unknown_count > 1 ? " and " + std::to_string(r.unknown_count - 1) + " more" : ""));
        }
    }
    WARN(flagged << " of " << scripts << " level scripts would be marked");
    CHECK(scripts > 0);
}
