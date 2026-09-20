// docs/refactor/skirmish/ (S2b) -- Catch2 coverage for script_setup_prelude.cpp:
// the v1 prelude generator, the "nothing changed" shortcut, lock enforcement
// and the validators. The corpus test proves reader -> generator -> reader is
// stable for every shipped multiplayer script.
#include <catch2/catch_test_macros.hpp>

#include "script_setup_prelude.h"
#include "kfx_config_test_paths.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

SetupAvailKey akey(int kind, int player, const char *item)
{
    SetupAvailKey k;
    k.kind = kind;
    k.player = player;
    k.item = item;
    return k;
}

SetupAvailValue aval(int a, int b)
{
    SetupAvailValue v;
    v.a = a;
    v.b = b;
    return v;
}

bool has_line(const std::string &text, const std::string &line)
{
    return ("\n" + text).find("\n" + line + "\n") != std::string::npos;
}

bool has_issue(const std::vector<SetupIssue> &v, SetupIssueSeverity sev, const std::string &needle)
{
    for (const SetupIssue &i : v)
        if (i.severity == sev && i.message.find(needle) != std::string::npos)
            return true;
    return false;
}

const char *const kBase =
    "LEVEL_VERSION(1)\n"
    "SET_GENERATE_SPEED(400)\n"
    "START_MONEY(ALL_PLAYERS,2000)\n"
    "MAX_CREATURES(PLAYER0,10)\nMAX_CREATURES(PLAYER1,12)\n"
    "ADD_CREATURE_TO_POOL(FLY,5)\n"
    "CREATURE_AVAILABLE(ALL_PLAYERS,TROLL,1,0)\n"
    "ROOM_AVAILABLE(PLAYER0,LAIR,1,1)\n"
    "COMPUTER_PLAYER(PLAYER1,0)\n"
    "IF(PLAYER0,ALL_DUNGEONS_DESTROYED == 1)\n\tWIN_GAME\nENDIF\n"
    "QUICK_MESSAGE(0,\"hi\",PLAYER0)\n";

SetupBuildOptions opts(int players = 2)
{
    SetupBuildOptions o;
    o.players = players;
    return o;
}

} // namespace

TEST_CASE("an untouched tab installs no override", "[kfx_config][script_prelude]") {
    const std::string s = kBase;
    const SetupAnalysis a = script_setup_analyse(s, 2);
    const SetupChoices c = script_setup_default_choices(a);
    CHECK(script_setup_choices_are_default(a, c));
    const SetupOverride o = script_setup_build_override(s, a, c, opts());
    CHECK_FALSE(o.active);
    CHECK(o.prelude.empty());
    CHECK(o.masked.empty());
    CHECK_FALSE(o.has_errors());
    // Keep mode never touches win/lose lines; Replace with identical rules is still a no-op.
    SetupChoices r = c;
    r.replace_win_lose = true;
    CHECK_FALSE(script_setup_build_override(s, a, r, opts()).active);
}

TEST_CASE("the prelude re-emits the whole state, so masking loses nothing", "[kfx_config][script_prelude]") {
    const std::string s = kBase;
    const SetupAnalysis a = script_setup_analyse(s, 2);
    SetupChoices c = script_setup_default_choices(a);
    c.values.generate_speed = 250; // one edit
    const SetupOverride o = script_setup_build_override(s, a, c, opts());
    REQUIRE(o.active);
    CHECK(o.prelude.find("LEVEL_VERSION") == std::string::npos); // the loader forces v1; a version line would leak
    CHECK(has_line(o.prelude, "SET_GENERATE_SPEED(250)"));
    CHECK(has_line(o.prelude, "START_MONEY(ALL_PLAYERS,2000)")); // unchanged, but the original line is masked
    CHECK(has_line(o.prelude, "MAX_CREATURES(PLAYER0,10)"));
    CHECK(has_line(o.prelude, "MAX_CREATURES(PLAYER1,12)"));
    CHECK(has_line(o.prelude, "ADD_CREATURE_TO_POOL(FLY,5)"));
    CHECK(has_line(o.prelude, "CREATURE_AVAILABLE(ALL_PLAYERS,TROLL,1,0)"));
    CHECK(has_line(o.prelude, "ROOM_AVAILABLE(PLAYER0,LAIR,1,1)"));
    CHECK(has_line(o.prelude, "COMPUTER_PLAYER(PLAYER1,0)"));
    CHECK(o.prelude.find("WIN_GAME") == std::string::npos); // Keep mode: rules stay in the file
    CHECK(o.masked.find("SET_GENERATE_SPEED") == std::string::npos);
    CHECK(o.masked.find("WIN_GAME") != std::string::npos);
    CHECK(o.masked.find("QUICK_MESSAGE") != std::string::npos);
}

TEST_CASE("edits: compression to ALL_PLAYERS, per-player lines, controllers, alliances", "[kfx_config][script_prelude]") {
    const std::string s = kBase;
    const SetupAnalysis a = script_setup_analyse(s, 2);
    SetupChoices c = script_setup_default_choices(a);
    c.values.start_money[0] = 500; // no longer uniform
    c.values.avail[akey(AvailKind_Trap, 0, "LAVA")] = aval(1, 3);
    c.values.avail[akey(AvailKind_Trap, 1, "LAVA")] = aval(1, 3); // both players equal -> ALL_PLAYERS
    c.values.avail[akey(AvailKind_Room, 1, "LAIR")] = aval(0, 0); // now player 1 explicitly off
    c.values.pool["ORC"] = 4;
    c.values.controllers[1].kind = SetupController::Roaming;
    c.values.controllers[0].kind = SetupController::Off;
    c.allies.push_back(std::make_pair(0, 1));
    const std::string p = script_setup_generate_prelude(a, c, 2, nullptr);
    CHECK(has_line(p, "START_MONEY(PLAYER0,500)"));
    CHECK(has_line(p, "START_MONEY(PLAYER1,2000)"));
    CHECK(has_line(p, "TRAP_AVAILABLE(ALL_PLAYERS,LAVA,1,3)"));
    CHECK(has_line(p, "ROOM_AVAILABLE(PLAYER0,LAIR,1,1)"));
    CHECK(has_line(p, "ROOM_AVAILABLE(PLAYER1,LAIR,0,0)"));
    CHECK(has_line(p, "ADD_CREATURE_TO_POOL(ORC,4)"));
    CHECK(has_line(p, "COMPUTER_PLAYER(PLAYER1,ROAMING)"));
    CHECK(has_line(p, "COMPUTER_PLAYER(PLAYER0,OFF)"));
    CHECK(has_line(p, "ALLY_PLAYERS(PLAYER0,PLAYER1,1)"));
}

TEST_CASE("Replace mode writes nested win/lose rules", "[kfx_config][script_prelude]") {
    const std::string s = kBase;
    const SetupAnalysis a = script_setup_analyse(s, 2);
    SetupChoices c = script_setup_default_choices(a);
    c.replace_win_lose = true;
    c.rules.clear();
    SetupWinLoseRule win;
    WinLoseClause c1, c2;
    c1.player = 0; c1.variable = "GAME_TURN"; c1.op = ">="; c1.value = 5000;
    c2.player = 1; c2.variable = "MONEY"; c2.op = "<"; c2.value = 100;
    win.win = true;
    win.clauses = { c1, c2 };
    SetupWinLoseRule lose;
    lose.win = false;
    lose.clauses = { c2 };
    c.rules = { win, lose };
    const SetupOverride o = script_setup_build_override(s, a, c, opts());
    REQUIRE(o.active);
    CHECK_FALSE(o.has_errors());
    CHECK(o.prelude.find("IF(PLAYER0,GAME_TURN >= 5000)\n\tIF(PLAYER1,MONEY < 100)\n\t\tWIN_GAME\n\tENDIF\nENDIF\n") != std::string::npos);
    CHECK(o.prelude.find("IF(PLAYER1,MONEY < 100)\n\tLOSE_GAME\nENDIF\n") != std::string::npos);
    CHECK(o.masked.find("ALL_DUNGEONS_DESTROYED") == std::string::npos); // the original block is masked
    // The emitted rules read back as the same rules.
    const SetupAnalysis back = script_setup_analyse("LEVEL_VERSION(1)\n" + o.prelude, 2);
    REQUIRE(back.seed.rules.size() == 2);
    CHECK(back.seed.rules[0].clauses.size() == 2);
    CHECK(back.seed.rules[0].clauses[1].variable == "MONEY");
    CHECK_FALSE(back.seed.rules[1].win);
}

TEST_CASE("locked fields keep the level's own value and warn", "[kfx_config][script_prelude]") {
    const std::string s =
        "LEVEL_VERSION(1)\nMAGIC_AVAILABLE(PLAYER0,POWER_REAPER,1,0)\nSTART_MONEY(ALL_PLAYERS,100)\nCOMPUTER_PLAYER(PLAYER1,2)\n"
        "IF(PLAYER0,GAME_TURN > 5)\n NEXT_COMMAND_REUSABLE\n MAGIC_AVAILABLE(PLAYER0,POWER_REAPER,0,0)\n"
        " COMPUTER_PLAYER(PLAYER1,4)\nENDIF\n"
        "IF(PLAYER0,ALL_DUNGEONS_DESTROYED == 1)\n WIN_GAME\nENDIF\n";
    const SetupAnalysis a = script_setup_analyse(s, 2);
    REQUIRE(a.is_locked(SetupField_AvailMagic, 0, "POWER_REAPER"));
    SetupChoices c = script_setup_default_choices(a);
    c.values.avail[akey(AvailKind_Magic, 0, "POWER_REAPER")] = aval(1, 1); // try to change a locked key
    c.values.controllers[1].model = 9;                                     // and a locked controller
    c.values.avail[akey(AvailKind_Magic, 1, "POWER_HAND")] = aval(1, 1);    // free key: honoured
    std::vector<SetupIssue> issues;
    const std::string p = script_setup_generate_prelude(a, c, 2, &issues);
    CHECK(has_line(p, "MAGIC_AVAILABLE(PLAYER0,POWER_REAPER,1,0)")); // seed value, not (1,1)
    CHECK(has_line(p, "MAGIC_AVAILABLE(PLAYER1,POWER_HAND,1,1)"));
    CHECK(has_line(p, "COMPUTER_PLAYER(PLAYER1,2)"));
    CHECK(has_issue(issues, SetupIssue_Warning, "spell availability"));
    CHECK(has_issue(issues, SetupIssue_Warning, "computer player"));
    // Only-locked edits are not a change at all.
    SetupChoices only_locked = script_setup_default_choices(a);
    only_locked.values.avail[akey(AvailKind_Magic, 0, "POWER_REAPER")] = aval(1, 1);
    CHECK_FALSE(script_setup_build_override(s, a, only_locked, opts()).active);
}

TEST_CASE("validators: empty win set, budgets, ranges, players, names", "[kfx_config][script_prelude]") {
    const std::string s = kBase;
    const SetupAnalysis a = script_setup_analyse(s, 2);
    auto build = [&](const SetupChoices &c, SetupBuildOptions o = opts()) { return script_setup_build_override(s, a, c, o); };

    SetupChoices none = script_setup_default_choices(a);
    none.replace_win_lose = true;
    none.rules.clear();
    SetupOverride o = build(none);
    CHECK(o.has_errors());
    CHECK_FALSE(o.active);
    CHECK(has_issue(o.issues, SetupIssue_Error, "No win condition"));

    SetupWinLoseRule lose_only;
    lose_only.win = false;
    lose_only.clauses.push_back(WinLoseClause());
    none.rules = { lose_only };
    CHECK(has_issue(build(none).issues, SetupIssue_Error, "No win condition"));

    SetupChoices many = script_setup_default_choices(a);
    many.replace_win_lose = true;
    SetupWinLoseRule w;
    w.clauses.push_back(WinLoseClause());
    many.rules.assign(13, w); // 12 is the engine's limit per kind
    CHECK(has_issue(build(many).issues, SetupIssue_Error, "Too many win rules"));
    many.rules.assign(12, w);
    CHECK_FALSE(build(many).has_errors());

    SetupChoices deep = script_setup_default_choices(a);
    deep.replace_win_lose = true;
    SetupWinLoseRule big;
    big.clauses.assign(kSetupConditionsCount + 1, WinLoseClause());
    deep.rules = { big };
    CHECK(has_issue(build(deep).issues, SetupIssue_Error, "Too many conditions"));

    SetupChoices bad = script_setup_default_choices(a);
    bad.values.start_money[0] = -1;
    bad.values.max_creatures[5] = 3; // outside 2 slots
    bad.values.pool["OR C"] = 1;
    bad.values.controllers[1].model = 64;
    bad.allies.push_back(std::make_pair(1, 1));
    o = build(bad);
    CHECK(has_issue(o.issues, SetupIssue_Error, "Start gold cannot be negative"));
    CHECK(has_issue(o.issues, SetupIssue_Error, "outside the level's 2 slots"));
    CHECK(has_issue(o.issues, SetupIssue_Error, "not a valid name"));
    CHECK(has_issue(o.issues, SetupIssue_Error, "out of range"));
    CHECK(has_issue(o.issues, SetupIssue_Error, "ally with itself"));
    CHECK_FALSE(o.active);

    SetupChoices gold = script_setup_default_choices(a);
    gold.values.start_money[0] = kSetupSensibleGold + 1;
    CHECK(has_issue(build(gold).issues, SetupIssue_Warning, "clamped"));

    SetupChoices names = script_setup_default_choices(a);
    names.values.avail[akey(AvailKind_Room, 0, "NO_SUCH_ROOM")] = aval(1, 1);
    SetupBuildOptions named = opts();
    named.item_exists = [](int, const std::string &item) { return item != "NO_SUCH_ROOM"; };
    CHECK(has_issue(build(names, named).issues, SetupIssue_Error, "NO_SUCH_ROOM"));
    CHECK_FALSE(has_issue(build(names).issues, SetupIssue_Error, "NO_SUCH_ROOM")); // no callback -> not checked

    SetupChoices unsupported_c = script_setup_default_choices(a);
    const SetupAnalysis unsup = script_setup_analyse("LEVEL_VERSION(9)\nSTART_MONEY(PLAYER0,1)\n", 2);
    CHECK(script_setup_build_override("x", unsup, unsupported_c, opts()).has_errors());
}

TEST_CASE("Replace budget accounts for the blocks it removes", "[kfx_config][script_prelude]") {
    // 12 shipped-style win blocks fill the win budget; replacing them with 12 new ones is fine.
    std::string s = "LEVEL_VERSION(1)\n";
    for (int i = 0; i < 12; i++)
        s += "IF(PLAYER0,ALL_DUNGEONS_DESTROYED == 1)\n WIN_GAME\nENDIF\n";
    const SetupAnalysis a = script_setup_analyse(s, 2);
    REQUIRE(a.win_count == 12);
    REQUIRE(a.if_count == 12);
    SetupChoices c = script_setup_default_choices(a);
    c.replace_win_lose = true;
    c.rules[0].clauses[0].value = 0; // one difference so an override is produced
    CHECK_FALSE(script_setup_build_override(s, a, c, opts()).has_errors());
    SetupWinLoseRule extra = c.rules[0];
    c.rules.push_back(extra); // 13 wins
    CHECK(script_setup_build_override(s, a, c, opts()).has_errors());
    // Keep mode with the same edit elsewhere leaves the 12 in place and adds none.
    SetupChoices k = script_setup_default_choices(a);
    k.values.generate_speed = 100;
    CHECK_FALSE(script_setup_build_override(s, a, k, opts()).has_errors());
}

// ---------------------------------------------------------------------------
// Corpus: reader -> generator -> reader is stable for every shipped script.
// ---------------------------------------------------------------------------
namespace {

SetupSeed normalised(SetupSeed s)
{
    for (auto it = s.start_money.begin(); it != s.start_money.end();)
        it = (it->second == 0) ? s.start_money.erase(it) : std::next(it); // START_MONEY(..,0) is a no-op
    for (auto it = s.pool.begin(); it != s.pool.end();)
        it = (it->second <= 0) ? s.pool.erase(it) : std::next(it);
    return s;
}

} // namespace

TEST_CASE("corpus: the prelude reads back to the level's own state", "[kfx_config][script_prelude][corpus]") {
    const fs::path root = fs::path(KFX_CONFIG_TEST_REPO_ROOT) / "core_files" / "multiplayer";
    if (!fs::exists(root))
        SKIP("core_files/multiplayer not found");
    size_t files = 0;
    for (const auto &d : fs::recursive_directory_iterator(root))
    {
        const std::string fn = d.path().filename().string();
        if (d.path().extension() != ".txt" || fn.rfind("map", 0) != 0)
            continue;
        std::ifstream f(d.path(), std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string text = ss.str();
        INFO(d.path().parent_path().filename().string() << "/" << fn);
        files++;

        const SetupAnalysis a = script_setup_analyse(text, 4);
        for (int replace = 0; replace < 2; replace++)
        {
            SetupChoices c = script_setup_default_choices(a);
            c.replace_win_lose = (replace != 0);
            SetupBuildOptions o = opts(4);
            // Defaults must be recognised as "nothing to do"...
            const SetupOverride none = script_setup_build_override(text, a, c, o);
            CHECK_FALSE(none.has_errors());
            CHECK_FALSE(none.active);
            // ...and a forced override must round-trip.
            o.force = true;
            const SetupOverride forced = script_setup_build_override(text, a, c, o);
            REQUIRE_FALSE(forced.has_errors());
            REQUIRE(forced.active);
            const SetupAnalysis back = script_setup_analyse("LEVEL_VERSION(1)\n" + forced.prelude, 4);
            const SetupSeed want = normalised(a.seed), got = normalised(back.seed);
            CHECK(got.generate_speed == want.generate_speed);
            CHECK(got.start_money == want.start_money);
            CHECK(got.max_creatures == want.max_creatures);
            CHECK(got.pool == want.pool);
            CHECK(got.avail == want.avail);
            CHECK(got.controllers == want.controllers);
            if (replace)
                CHECK(got.rules.size() == want.rules.size());
            else
                CHECK(got.rules.empty());
            // The masked script + prelude never lose a win path.
            CHECK(back.custom_win_lose == 0);
        }
    }
    CHECK(files == 81);
}
