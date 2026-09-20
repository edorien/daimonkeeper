// docs/refactor/skirmish/ (S2a) -- Catch2 coverage for script_setup_analysis.cpp:
// the version-aware reader, the ownership mask and the suitability verdict.
// Synthetic cases first, then invariants + golden verdicts over every shipped
// multiplayer script (skipped if core_files/ is not next to the build tree).
#include <catch2/catch_test_macros.hpp>

#include "script_setup_analysis.h"
#include "kfx_config_test_paths.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

SetupAvailKey key(int kind, int player, const char *item)
{
    SetupAvailKey k;
    k.kind = kind;
    k.player = player;
    k.item = item;
    return k;
}

size_t owned_count(const SetupAnalysis &a)
{
    size_t n = 0;
    for (bool b : a.owned_setup)
        n += b;
    return n;
}

} // namespace

TEST_CASE("v0 and v1 creature availability normalise to the same model", "[kfx_config][script_analysis]") {
    // Same meaning, different encodings (docs/refactor/skirmish/01 §1): v0 reads the 4th
    // argument as 'available' and can never force; v1 has (available, force).
    const SetupAnalysis v0 = script_setup_analyse("CREATURE_AVAILABLE(ALL_PLAYERS,TROLL,1,1)\n", 2);
    const SetupAnalysis v1 = script_setup_analyse("LEVEL_VERSION(1)\nCREATURE_AVAILABLE(ALL_PLAYERS,TROLL,1,0)\n", 2);
    REQUIRE(v0.level_version == 0);
    REQUIRE(v1.level_version == 1);
    REQUIRE(v0.seed.avail.size() == 2);
    CHECK(v0.seed.avail.at(key(AvailKind_Creature, 1, "TROLL")).a == 1);
    CHECK(v0.seed.avail.at(key(AvailKind_Creature, 1, "TROLL")).b == 0);
    CHECK(v0.seed.avail == v1.seed.avail);
    // v0 "disabled" (4th arg 0) is not available, whatever the 3rd says.
    const SetupAnalysis off = script_setup_analyse("CREATURE_AVAILABLE(PLAYER0,TROLL,1,0)\n", 2);
    CHECK(off.seed.avail.at(key(AvailKind_Creature, 0, "TROLL")).a == 0);
}

TEST_CASE("version is a whole-file property: LEVEL_VERSION after the commands still applies", "[kfx_config][script_analysis]") {
    const SetupAnalysis a = script_setup_analyse("CREATURE_AVAILABLE(PLAYER0,TROLL,1,0)\nLEVEL_VERSION(1)\n", 1);
    CHECK(a.level_version == 1);
    CHECK(a.seed.avail.at(key(AvailKind_Creature, 0, "TROLL")).a == 1); // read as v1 (a=1,b=0)
}

TEST_CASE("static setup is read; runtime setup locks and is not owned", "[kfx_config][script_analysis]") {
    const std::string s =
        "LEVEL_VERSION(1)\n"
        "SET_GENERATE_SPEED(400)\n"
        "START_MONEY(PLAYER0,1000)\n"
        "MAX_CREATURES(ALL_PLAYERS,20)\n"
        "ROOM_AVAILABLE(ALL_PLAYERS,TREASURE,1,1)\n"
        "IF(PLAYER0,GAME_TURN > 100)\n"
        "  MAGIC_AVAILABLE(PLAYER0,POWER_REAPER,1,1)\n"
        "  START_MONEY(PLAYER1,5)\n"
        "ENDIF\n"
        "NEXT_COMMAND_REUSABLE\n"
        "ADD_CREATURE_TO_POOL(FLY,3)\n";
    const SetupAnalysis a = script_setup_analyse(s, 2);
    CHECK(a.seed.generate_speed == 400);
    CHECK(a.seed.start_money.at(0) == 1000);
    CHECK(a.seed.start_money.count(1) == 0); // the runtime one is not a default
    CHECK(a.seed.max_creatures.at(0) == 20);
    CHECK(a.seed.max_creatures.at(1) == 20);
    CHECK(a.seed.pool.empty()); // reusable pool add is runtime
    CHECK(owned_count(a) == 4);
    CHECK(a.is_locked(SetupField_AvailMagic, 0, "POWER_REAPER"));
    CHECK_FALSE(a.is_locked(SetupField_AvailMagic, 1, "POWER_REAPER"));
    CHECK_FALSE(a.is_locked(SetupField_AvailMagic, 0, "POWER_HAND"));
    CHECK(a.is_locked(SetupField_Money, 1));
    CHECK_FALSE(a.is_locked(SetupField_Money, 0));
    CHECK(a.is_locked(SetupField_Pool, -1, "FLY"));
    CHECK(a.verdict == SetupVerdict_Partial);
}

TEST_CASE("an ALL_PLAYERS runtime write locks the item for every player", "[kfx_config][script_analysis]") {
    const SetupAnalysis a = script_setup_analyse("IF(PLAYER0,GAME_TURN > 1)\nROOM_AVAILABLE(ALL_PLAYERS,LAIR,1,1)\nENDIF\n", 3);
    CHECK(a.is_locked(SetupField_AvailRoom, 0, "LAIR"));
    CHECK(a.is_locked(SetupField_AvailRoom, 2, "LAIR"));
    CHECK_FALSE(a.is_locked(SetupField_AvailRoom, 2, "TREASURE"));
}

TEST_CASE("engine accumulation rules are reproduced", "[kfx_config][script_analysis]") {
    const std::string s =
        "LEVEL_VERSION(1)\n"
        "START_MONEY(PLAYER0,100)\nSTART_MONEY(ALL_PLAYERS,50)\n"      // additive
        "ADD_CREATURE_TO_POOL(FLY,3)\nADD_CREATURE_TO_POOL(fly,4)\n"     // additive, case-insensitive
        "TRAP_AVAILABLE(PLAYER0,LAVA,1,2)\nTRAP_AVAILABLE(PLAYER0,LAVA,1,3)\n" // amount adds
        "ROOM_AVAILABLE(ALL_PLAYERS,LAIR,1,1)\nROOM_AVAILABLE(PLAYER1,LAIR,0,0)\n"; // last writer wins
    const SetupAnalysis a = script_setup_analyse(s, 2);
    CHECK(a.seed.start_money.at(0) == 150);
    CHECK(a.seed.start_money.at(1) == 50);
    CHECK(a.seed.pool.at("FLY") == 7);
    CHECK(a.seed.avail.at(key(AvailKind_Trap, 0, "LAVA")).b == 5);
    CHECK(a.seed.avail.at(key(AvailKind_Room, 0, "LAIR")).a == 1);
    CHECK(a.seed.avail.at(key(AvailKind_Room, 1, "LAIR")).a == 0);
}

TEST_CASE("mask blanks only owned lines and preserves everything else byte-for-byte", "[kfx_config][script_analysis]") {
    const std::string s =
        "REM header\r\nLEVEL_VERSION(1)\r\nSTART_MONEY(PLAYER0,100)\r\n\r\n"
        "IF(PLAYER0,GAME_TURN > 5)\r\n\tNEXT_COMMAND_REUSABLE\r\n\tSTART_MONEY(PLAYER0,1)\r\nENDIF\r\n"
        "QUICK_MESSAGE(0,\"hi\",PLAYER0)\r\nSET_GENERATE_SPEED(200)";
    const SetupAnalysis a = script_setup_analyse(s, 1);
    const std::string m = script_setup_mask(s, a, false);
    CHECK(m ==
        "REM header\r\nLEVEL_VERSION(1)\r\n\r\n\r\n"
        "IF(PLAYER0,GAME_TURN > 5)\r\n\tNEXT_COMMAND_REUSABLE\r\n\tSTART_MONEY(PLAYER0,1)\r\nENDIF\r\n"
        "QUICK_MESSAGE(0,\"hi\",PLAYER0)\r\n"); // last line (no terminator) blanked to ""
    // Idempotent: nothing static left to own.
    const SetupAnalysis again = script_setup_analyse(m, 1);
    CHECK(owned_count(again) == 0);
    CHECK(script_setup_mask(m, again, false) == m);
}

TEST_CASE("unmodelled shapes are left in the script, not masked", "[kfx_config][script_analysis]") {
    const std::string s =
        "PLAYER_GOOD_UNUSED(1)\n"
        "START_MONEY(PLAYER_GOOD,100)\n"        // player the tab does not model
        "START_MONEY(PLAYER0,FLAG0)\n"          // non-numeric value
        "START_MONEY(PLAYER0,10) REM trailing\n" // junk after the argument list
        "ROOM_AVAILABLE(PLAYER0,LAIR,1)\n";     // wrong arity
    const SetupAnalysis a = script_setup_analyse(s, 1);
    CHECK(owned_count(a) == 0);
    CHECK(a.seed.start_money.empty());
}

TEST_CASE("multi-line comments hide commands and never own their line", "[kfx_config][script_analysis]") {
    const std::string s = "/* START_MONEY(PLAYER0,999)\nMAX_CREATURES(PLAYER0,9)\n*/ START_MONEY(PLAYER0,5)\nSTART_MONEY(PLAYER0,7)\n";
    const SetupAnalysis a = script_setup_analyse(s, 1);
    CHECK(a.seed.start_money.at(0) == 7); // the line that closes the comment is not owned (kept as-is)
    CHECK(a.seed.max_creatures.empty());
    CHECK(owned_count(a) == 1);
}

TEST_CASE("computer players: static is a default, runtime locks the slot", "[kfx_config][script_analysis]") {
    const SetupAnalysis a = script_setup_analyse(
        "LEVEL_VERSION(1)\nCOMPUTER_PLAYER(PLAYER1,3)\nCOMPUTER_PLAYER(PLAYER2,ROAMING)\n"
        "IF(PLAYER1,VIEW_TYPE == 0)\n COMPUTER_PLAYER(PLAYER3,0)\nENDIF\n", 4);
    REQUIRE(a.seed.controllers.count(1));
    CHECK(a.seed.controllers.at(1).kind == SetupController::Model);
    CHECK(a.seed.controllers.at(1).model == 3);
    CHECK(a.seed.controllers.at(2).kind == SetupController::Roaming);
    CHECK(a.is_locked(SetupField_Controller, 3));
    CHECK_FALSE(a.is_locked(SetupField_Controller, 1));
    // v0 only has numeric models: ROAMING is not read there.
    const SetupAnalysis v0 = script_setup_analyse("COMPUTER_PLAYER(PLAYER1,ROAMING)\n", 2);
    CHECK(v0.seed.controllers.empty());
    CHECK(owned_count(v0) == 0);
}

TEST_CASE("win/lose: pure blocks are modelled, anything else is kept as custom", "[kfx_config][script_analysis]") {
    const std::string s =
        "LEVEL_VERSION(1)\n"
        "IF(PLAYER0,ALL_DUNGEONS_DESTROYED == 1)\n\tWIN_GAME\nENDIF\n"
        "IF(PLAYER0,GAME_TURN > 10)\n\tIF(PLAYER1,MONEY < 5)\n\t\tLOSE_GAME\n\tENDIF\nENDIF\n"
        "IF(PLAYER0,GAME_TURN > 99)\n\tQUICK_MESSAGE(0,\"x\",PLAYER0)\n\tWIN_GAME\nENDIF\n" // mixed content
        "IF(PLAYER0,FLAG1 == 1)\n\tWIN_GAME\nENDIF\n"                                         // not a whitelisted variable
        "WIN_GAME\n";                                                                       // unconditional
    const SetupAnalysis a = script_setup_analyse(s, 2);
    REQUIRE(a.seed.rules.size() == 2);
    CHECK(a.seed.rules[0].win);
    CHECK(a.seed.rules[0].clauses.size() == 1);
    CHECK_FALSE(a.seed.rules[1].win);
    REQUIRE(a.seed.rules[1].clauses.size() == 2);
    CHECK(a.seed.rules[1].clauses[1].variable == "MONEY");
    CHECK(a.custom_win_lose == 3);
    // keep: nothing masked; replace: only the two modelled blocks (9 lines incl. LEVEL_VERSION kept).
    CHECK(script_setup_mask(s, a, false) == s);
    const std::string replaced = script_setup_mask(s, a, true);
    CHECK(replaced.find("ALL_DUNGEONS_DESTROYED") == std::string::npos);
    CHECK(replaced.find("LOSE_GAME") == std::string::npos);
    CHECK(replaced.find("FLAG1") != std::string::npos);
    CHECK(replaced.find("QUICK_MESSAGE") != std::string::npos);
    size_t nl_a = 0, nl_b = 0;
    for (char c : s) nl_a += (c == '\n');
    for (char c : replaced) nl_b += (c == '\n');
    CHECK(nl_a == nl_b); // line count preserved
}

TEST_CASE("win variables: v0 alias and version-specific meanings", "[kfx_config][script_analysis]") {
    // v0: TOTAL_IMPS is the old spelling of TOTAL_DIGGERS; TOTAL_CREATURES means something else at v0 -> not modelled.
    const SetupAnalysis v0 = script_setup_analyse(
        "IF(PLAYER0,TOTAL_IMPS == 0)\n WIN_GAME\nENDIF\nIF(PLAYER0,TOTAL_CREATURES == 0)\n LOSE_GAME\nENDIF\n", 2);
    REQUIRE(v0.seed.rules.size() == 1);
    CHECK(v0.seed.rules[0].clauses[0].variable == "TOTAL_DIGGERS");
    CHECK(v0.custom_win_lose == 1);
    // v1: TOTAL_CREATURES is a plain total but is still excluded from the whitelist; TOTAL_DIGGERS is fine, TOTAL_IMPS is not.
    const SetupAnalysis v1 = script_setup_analyse(
        "LEVEL_VERSION(1)\nIF(PLAYER0,TOTAL_DIGGERS == 0)\n WIN_GAME\nENDIF\nIF(PLAYER0,TOTAL_IMPS == 0)\n WIN_GAME\nENDIF\n", 2);
    CHECK(v1.seed.rules.size() == 1);
    CHECK(v1.custom_win_lose == 1);
    // A v1-only name cannot be modelled from a v0 file.
    const SetupAnalysis bad = script_setup_analyse("IF(PLAYER0,HEART_HEALTH < 10)\n LOSE_GAME\nENDIF\n", 2);
    CHECK(bad.seed.rules.empty());
    CHECK(bad.custom_win_lose == 1);
}

TEST_CASE("the two win-variable lists are disjoint and NULL-terminated", "[kfx_config][script_analysis]") {
    int n0 = 0, n1 = 0;
    for (const char *const *p = script_setup_win_variables_identical(); *p; p++, n0++)
        for (const char *const *q = script_setup_win_variables_v1_only(); *q; q++)
            CHECK(std::string(*p) != *q);
    for (const char *const *q = script_setup_win_variables_v1_only(); *q; q++) n1++;
    CHECK(n0 == 19);
    CHECK(n1 == 38);
}

TEST_CASE("structural verdicts: stray ENDIF tolerated, unclosed IF / bad version / empty are Unsupported", "[kfx_config][script_analysis]") {
    const SetupAnalysis stray = script_setup_analyse("START_MONEY(PLAYER0,1)\nENDIF\nENDIF\n", 1);
    CHECK(stray.stray_endif == 2);
    CHECK(stray.verdict == SetupVerdict_Supported);
    CHECK(script_setup_analyse("IF(PLAYER0,GAME_TURN > 1)\nSTART_MONEY(PLAYER0,1)\n", 1).verdict == SetupVerdict_Unsupported);
    CHECK(script_setup_analyse("LEVEL_VERSION(2)\nSTART_MONEY(PLAYER0,1)\n", 1).verdict == SetupVerdict_Unsupported);
    CHECK(script_setup_analyse("", 1).verdict == SetupVerdict_Unsupported);
    CHECK(script_setup_analyse("REM only a comment\n", 1).verdict == SetupVerdict_Unsupported);
}

TEST_CASE("information tags", "[kfx_config][script_analysis]") {
    const SetupAnalysis a = script_setup_analyse("SET_BOX_TOOLTIP(1,\"x\")\nSET_GAME_RULE(PayDaySpeed,0)\n", 1, true);
    CHECK(a.uses_boxes);
    CHECK(a.uses_game_rule);
    CHECK(a.has_lua_companion);
    CHECK(a.verdict == SetupVerdict_Supported); // tags never lock
    CHECK(script_setup_analyse("IF(PLAYER0,BOX1_ACTIVATED>0)\nENDIF\n", 1).uses_boxes);
}

TEST_CASE("lua scan: finds engine setup calls, ignores comments, strings and definitions", "[kfx_config][script_analysis][lua]") {
    const SetupLuaUse u = script_setup_scan_lua(
        "function OnGameStart()\n"
        "  StartMoney(PLAYER0, 5000)\n"
        "  Game.CreatureAvailable( PLAYER0, 'TROLL', true, 0 )\n"
        "  RoomAvailable(PLAYER1,\"TEMPLE\",true,true)\n"
        "  WinGame()\n"
        "end\n");
    CHECK(u.scanned);
    CHECK(u.money);
    CHECK(u.avail[AvailKind_Creature]);
    CHECK(u.avail[AvailKind_Room]);
    CHECK(u.win);
    CHECK_FALSE(u.avail[AvailKind_Magic]);
    CHECK_FALSE(u.max_creatures);
    CHECK_FALSE(u.controller);
    CHECK(u.any_setup());
    CHECK(u.touches(SetupField_Money));
    CHECK(u.touches(SetupField_AvailRoom));
    CHECK_FALSE(u.touches(SetupField_Pool));
    CHECK(u.describe() == "start gold, creature availability, room availability and win/lose rules");

    // Not calls: comments (line and long, incl. levelled brackets), strings, long strings, definitions,
    // and a name that is only a prefix of a longer identifier.
    const SetupLuaUse none = script_setup_scan_lua(
        "-- StartMoney(PLAYER0, 1)\n"
        "--[[ MaxCreatures(PLAYER0, 3)\n AddCreatureToPool(FLY, 2) ]]\n"
        "--[==[ SetGenerateSpeed(5) ]==]\n"
        "print(\"AllyPlayers(PLAYER0, PLAYER1, 1)\")\n"
        "local s = [[ WinGame() ]]\n"
        "local t = 'LoseGame()'\n"
        "function StartMoney(player, gold) end\n"
        "local function ComputerPlayer(p, m) end\n"
        "MyStartMoney(1)\n"
        "StartMoneyLater(2)\n"
        "local x = StartMoney\n"); // a reference without a call
    CHECK(none.scanned);
    CHECK_FALSE(none.any_setup());
    CHECK(none.describe().empty());

    // Information-only and remaining flags.
    const SetupLuaUse more = script_setup_scan_lua("Research(PLAYER0,'ROOM','LAIR',10)\nSetComputerGlobals(PLAYER1,1,2,3,4,5,6)\nAllyPlayers(PLAYER0,PLAYER1,1)\n"
        "TrapAvailable(PLAYER0,'LAVA',1,1)\nDoorAvailable(PLAYER0,'WOOD',1,1)\nMagicAvailable(PLAYER0,'POWER_HAND',1,1)\n"
        "AddCreatureToPool('FLY',3)\nMaxCreatures(PLAYER0,9)\nSetGenerateSpeed(300)\nLoseGame()\n");
    CHECK(more.research);
    CHECK(more.controller);
    CHECK(more.ally);
    CHECK(more.avail[AvailKind_Trap]);
    CHECK(more.avail[AvailKind_Door]);
    CHECK(more.avail[AvailKind_Magic]);
    CHECK(more.pool);
    CHECK(more.max_creatures);
    CHECK(more.gen_speed);
    CHECK(more.lose);
    CHECK_FALSE(more.win);
    CHECK_FALSE(script_setup_scan_lua("Research(PLAYER0,'ROOM','LAIR',10)\n").any_setup()); // research alone is info only
    CHECK(script_setup_scan_lua("").scanned);
    CHECK_FALSE(script_setup_scan_lua("").any_setup());
}

TEST_CASE("lua companion: a Lua script that changes the tab's fields makes the level Partial", "[kfx_config][script_analysis][lua]") {
    const std::string txt = "LEVEL_VERSION(1)\nSTART_MONEY(ALL_PLAYERS,1000)\nROOM_AVAILABLE(ALL_PLAYERS,LAIR,1,1)\n";
    const std::string quiet = "function OnGameStart() print('hi') end\n";
    const std::string busy = "function OnGameStart()\n StartMoney(PLAYER0, 50)\n AddCreatureToPool('FLY', 2)\nend\n";

    // Companion present but inert: still Supported, flagged as a companion.
    const SetupAnalysis inert = script_setup_analyse(txt, 2, false, &quiet);
    CHECK(inert.has_lua_companion);
    CHECK(inert.verdict == SetupVerdict_Supported);
    CHECK_FALSE(inert.lua.any_setup());

    // Companion that changes gold and the pool: Partial, and the reason names them.
    const SetupAnalysis a = script_setup_analyse(txt, 2, false, &busy);
    CHECK(a.has_lua_companion);
    CHECK(a.verdict == SetupVerdict_Partial);
    CHECK(a.reason.find("start gold and the creature pool") != std::string::npos);
    CHECK(a.lua.touches(SetupField_Money));
    CHECK(a.locks.empty()); // a warning, not a lock: the tab still edits gold
    CHECK(a.seed.start_money.at(0) == 1000);

    // Existing runtime locks and Lua warnings combine.
    const std::string both_txt = txt + "IF(PLAYER0,GAME_TURN > 5)\n NEXT_COMMAND_REUSABLE\n START_MONEY(PLAYER0,1)\nENDIF\n";
    const SetupAnalysis c = script_setup_analyse(both_txt, 2, false, &busy);
    CHECK(c.verdict == SetupVerdict_Partial);
    CHECK(c.reason.find("controlled by the level script") != std::string::npos);
    CHECK(c.reason.find("Lua") != std::string::npos);

    // Without the Lua text nothing is known: the old behaviour (bool only).
    CHECK(script_setup_analyse(txt, 2, true).verdict == SetupVerdict_Supported);
    // Unsupported stays Unsupported.
    CHECK(script_setup_analyse("LEVEL_VERSION(9)\n", 2, false, &busy).verdict == SetupVerdict_Unsupported);
}

// ---------------------------------------------------------------------------
// Corpus: every shipped multiplayer script. Golden verdicts come from
// docs/refactor/skirmish/01-...md §11/§15 (re-derived by this reader; the
// classic ALLY_PLAYERS locks were first found here).
// ---------------------------------------------------------------------------
namespace {

struct CorpusFile
{
    std::string pack, name, text;
};

std::vector<CorpusFile> load_corpus()
{
    std::vector<CorpusFile> out;
    const fs::path root = fs::path(KFX_CONFIG_TEST_REPO_ROOT) / "core_files" / "multiplayer";
    if (!fs::exists(root))
        return out;
    for (const auto &d : fs::recursive_directory_iterator(root))
    {
        const std::string fn = d.path().filename().string();
        if (d.path().extension() != ".txt" || fn.rfind("map", 0) != 0)
            continue;
        std::ifstream f(d.path(), std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        out.push_back({ d.path().parent_path().filename().string(), fn, ss.str() });
    }
    return out;
}

} // namespace

TEST_CASE("corpus: mask invariants hold for every shipped multiplayer script", "[kfx_config][script_analysis][corpus]") {
    const std::vector<CorpusFile> corpus = load_corpus();
    if (corpus.empty())
        SKIP("core_files/multiplayer not found");
    REQUIRE(corpus.size() == 81);
    for (const CorpusFile &c : corpus)
    {
        INFO(c.pack << "/" << c.name);
        const SetupAnalysis a = script_setup_analyse(c.text, 4);
        REQUIRE(a.verdict != SetupVerdict_Unsupported);
        for (int replace = 0; replace < 2; replace++)
        {
            const std::string m = script_setup_mask(c.text, a, replace != 0);
            // Same number of lines, every non-owned line byte-identical, owned lines empty.
            std::vector<std::string> ol, ml;
            for (const std::string *src : { &c.text, &m })
            {
                std::vector<std::string> &dst = (src == &c.text) ? ol : ml;
                size_t pos = 0;
                while (true)
                {
                    const size_t nl = src->find('\n', pos);
                    dst.push_back(src->substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
                    if (nl == std::string::npos)
                        break;
                    pos = nl + 1;
                }
            }
            REQUIRE(ol.size() == ml.size());
            REQUIRE(ol.size() == a.line_count);
            for (size_t i = 0; i < ol.size(); i++)
            {
                const bool owned = a.owned_setup[i] || (replace && a.owned_win_lose[i]);
                if (owned)
                    CHECK((ml[i].empty() || ml[i] == "\r"));
                else
                    CHECK(ml[i] == ol[i]);
            }
            // Idempotent: a masked script has no static setup (and, in replace mode, no modelled rules) left.
            const SetupAnalysis again = script_setup_analyse(m, 4);
            CHECK(owned_count(again) == 0);
            CHECK(again.seed.avail.empty());
            CHECK(again.seed.controllers.empty());
            if (replace)
                CHECK(again.seed.rules.empty());
        }
        CHECK(a.custom_win_lose == 0); // all shipped win rules are pure
    }
}

TEST_CASE("corpus: every shipped win rule is the modelled 'ALL_DUNGEONS_DESTROYED' form", "[kfx_config][script_analysis][corpus]") {
    const std::vector<CorpusFile> corpus = load_corpus();
    if (corpus.empty())
        SKIP("core_files/multiplayer not found");
    size_t rules = 0;
    for (const CorpusFile &c : corpus)
    {
        const SetupAnalysis a = script_setup_analyse(c.text, 4);
        for (const SetupWinLoseRule &r : a.seed.rules)
        {
            rules++;
            CHECK(r.win);
            REQUIRE(r.clauses.size() == 1);
            CHECK(r.clauses[0].variable == "ALL_DUNGEONS_DESTROYED");
        }
    }
    CHECK(rules == 207);
}

TEST_CASE("corpus: golden suitability verdicts per mappack", "[kfx_config][script_analysis][corpus]") {
    const std::vector<CorpusFile> corpus = load_corpus();
    if (corpus.empty())
        SKIP("core_files/multiplayer not found");
    int original_partial = 0, classic_partial = 0, classic_supported = 0;
    for (const CorpusFile &c : corpus)
    {
        INFO(c.pack << "/" << c.name);
        const SetupAnalysis a = script_setup_analyse(c.text, 4);
        if (c.pack == "original")
        {
            if (c.name == "map00150.txt")
            {
                CHECK(a.verdict == SetupVerdict_Partial);
                CHECK(a.is_locked(SetupField_AvailRoom, 0, "PRISON"));
            }
            else
                CHECK(a.verdict == SetupVerdict_Supported);
            original_partial += (a.verdict == SetupVerdict_Partial);
        }
        else if (c.pack == "classic")
        {
            classic_partial += (a.verdict == SetupVerdict_Partial);
            classic_supported += (a.verdict == SetupVerdict_Supported);
            if (c.name == "map00055.txt")
                CHECK(a.is_locked(SetupField_MaxCreatures, 0));
        }
        else if (c.pack == "dk2maps")
        {
            // Only the Reaper spell toggle, per player.
            CHECK(a.verdict == SetupVerdict_Partial);
            REQUIRE(a.locks.size() == 2);
            CHECK(a.is_locked(SetupField_AvailMagic, 0, "POWER_REAPER"));
            CHECK(a.is_locked(SetupField_AvailMagic, 1, "POWER_REAPER"));
            CHECK_FALSE(a.is_locked(SetupField_AvailMagic, 0, "POWER_HAND"));
            CHECK(a.uses_game_rule);
        }
        else if (c.pack == "biervampir")
        {
            // Faction-choice maps: creature rows are chosen in-game, PLAYER1's AI is decided by the script.
            CHECK(a.verdict == SetupVerdict_Partial);
            CHECK(a.uses_boxes);
            CHECK(a.is_locked(SetupField_Controller, 1));
            CHECK_FALSE(a.is_locked(SetupField_Controller, 0));
            CHECK_FALSE(a.is_locked(SetupField_Money, 0)); // start gold stays editable
            CHECK(a.locks.size() == 55);
        }
    }
    CHECK(original_partial == 1);
    CHECK(classic_partial == 4);   // map00055 (MAX_CREATURES) + 3 with runtime ALLY_PLAYERS
    CHECK(classic_supported == 11);
}

TEST_CASE("corpus: the Lua-only classic skirmish maps define the whole setup in Lua", "[kfx_config][script_analysis][lua][corpus]") {
    const fs::path dir = fs::path(KFX_CONFIG_TEST_REPO_ROOT) / "core_files" / "multiplayer" / "classic";
    if (!fs::exists(dir))
        SKIP("core_files/multiplayer not found");
    int seen = 0;
    for (const auto &d : fs::directory_iterator(dir))
    {
        if (d.path().extension() != ".lua")
            continue;
        std::ifstream f(d.path(), std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        const SetupLuaUse u = script_setup_scan_lua(ss.str());
        INFO(d.path().filename().string());
        CHECK(u.any_setup());
        CHECK(u.money);
        CHECK(u.max_creatures);
        CHECK(u.pool);
        CHECK(u.avail[AvailKind_Creature]);
        CHECK(u.avail[AvailKind_Room]);
        CHECK(u.controller);
        CHECK(u.win);
        seen++;
    }
    CHECK(seen == 4);
}
