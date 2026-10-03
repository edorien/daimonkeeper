// docs/refactor/skirmish/ (S4): the Skirmish setup tab's state module
// (skirmish_setup.h) -- seeding, edit operations, locks, teams, the built
// override, and Play integration through the real script loader.
#include <catch2/catch_test_macros.hpp>

#include "skirmish_setup.h"
#include "net_game.h"
#include "config_campaigns.h" // SkirmishSetupOption
#include "level_script_override.h"
#include "lvl_script.h"
#include "lvl_filesdk1.h"
#include "kfx_game_state.h"

#include <cstring>

namespace {

const LevelNumber kLevel = 9911;

const char *const kScript =
    "LEVEL_VERSION(1)\n"
    "SET_GENERATE_SPEED(400)\n"
    "START_MONEY(ALL_PLAYERS,2000)\n"
    "MAX_CREATURES(ALL_PLAYERS,20)\n"
    "ADD_CREATURE_TO_POOL(FLY,5)\n"
    "CREATURE_AVAILABLE(ALL_PLAYERS,TROLL,1,0)\n"
    "ROOM_AVAILABLE(ALL_PLAYERS,LAIR,1,1)\n"
    "MAGIC_AVAILABLE(ALL_PLAYERS,POWER_HAND,1,1)\n"
    "TRAP_AVAILABLE(ALL_PLAYERS,LAVA,1,2)\n"
    "IF(PLAYER0,ALL_DUNGEONS_DESTROYED == 1)\n\tWIN_GAME\nENDIF\n"
    "IF(PLAYER1,ALL_DUNGEONS_DESTROYED == 1)\n\tWIN_GAME\nENDIF\n"
    // runtime-controlled: the Reaper toggle
    "MAGIC_AVAILABLE(PLAYER0,POWER_REAPER,1,0)\n"
    "IF(PLAYER0,GAME_TURN > 50)\n\tNEXT_COMMAND_REUSABLE\n\tMAGIC_AVAILABLE(PLAYER0,POWER_REAPER,0,0)\nENDIF\n";

struct Fixture {
    Fixture()
    {
        level_script_override_clear();
        skirmish_setup_load_from_text(kLevel, kScript, 2, false, SkirmishSetup_Auto, 0);
        std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
    }
    ~Fixture()
    {
        level_script_override_clear();
        skirmish_setup_forget();
        std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
    }
};

} // namespace

TEST_CASE_METHOD(Fixture, "seeding: the level's own values are the defaults and nothing counts as changed", "[kfx_frontend][skirmish_setup]") {
    const SkirmishSetup &s = skirmish_setup();
    REQUIRE(s.enabled());
    CHECK(s.players == 2);
    CHECK(s.choices.values.generate_speed == 400);
    CHECK(s.choices.values.start_money.at(1) == 2000);
    CHECK(s.choices.values.pool.at("FLY") == 5);
    CHECK(skirmish_setup_avail_state(AvailKind_Creature, 0, "TROLL") == SkirmishAvail_On);
    CHECK(skirmish_setup_avail_state(AvailKind_Room, -1, "LAIR") == SkirmishAvail_On);
    CHECK(skirmish_setup_avail_state(AvailKind_Trap, 0, "LAVA") == SkirmishAvail_On);
    CHECK(skirmish_setup_avail_amount(0, AvailKind_Trap, "LAVA") == 2);
    CHECK_FALSE(skirmish_setup_is_changed());
    CHECK_FALSE(skirmish_setup_build().active);
}

TEST_CASE("the tab is disabled with a reason when the level cannot be customised", "[kfx_frontend][skirmish_setup]") {
    skirmish_setup_load_from_text(kLevel, kScript, 2, false, SkirmishSetup_Locked, 0);
    CHECK_FALSE(skirmish_setup().enabled());
    CHECK(skirmish_setup().unavailable_reason.find("author") != std::string::npos);
    skirmish_setup_load_from_text(kLevel, "LEVEL_VERSION(7)\nSTART_MONEY(PLAYER0,1)\n", 2, false, SkirmishSetup_Auto, 0);
    CHECK_FALSE(skirmish_setup().enabled());
    skirmish_setup_load_from_text(kLevel, "", 2, false, SkirmishSetup_Auto, 0);
    CHECK_FALSE(skirmish_setup().enabled());
    // Edits do nothing while disabled.
    skirmish_setup_set_money(0, 5);
    CHECK_FALSE(skirmish_setup_is_changed());
    skirmish_setup_forget();
}

TEST_CASE_METHOD(Fixture, "availability edits: states per kind, all-players, mixed, unset items", "[kfx_frontend][skirmish_setup]") {
    // creatures cycle Off / On / Forced
    skirmish_setup_set_avail(AvailKind_Creature, 1, "TROLL", SkirmishAvail_Forced);
    CHECK(skirmish_setup_avail_state(AvailKind_Creature, 1, "TROLL") == SkirmishAvail_Forced);
    CHECK(skirmish_setup_avail_state(AvailKind_Creature, 0, "TROLL") == SkirmishAvail_On);
    bool mixed = false;
    skirmish_setup_avail_state(AvailKind_Creature, -1, "TROLL", &mixed);
    CHECK(mixed);
    skirmish_setup_set_avail(AvailKind_Creature, -1, "TROLL", SkirmishAvail_On); // all players at once
    skirmish_setup_avail_state(AvailKind_Creature, -1, "TROLL", &mixed);
    CHECK_FALSE(mixed);
    CHECK_FALSE(skirmish_setup_is_changed()); // back to the level's own values

    // rooms: Research = (1,0), On = (1,1)
    skirmish_setup_set_avail(AvailKind_Room, 0, "lair", SkirmishAvail_Research); // case-insensitive item
    CHECK(skirmish_setup_avail_state(AvailKind_Room, 0, "LAIR") == SkirmishAvail_Research);
    // traps keep a stock amount
    skirmish_setup_set_avail(AvailKind_Trap, 1, "LAVA", SkirmishAvail_On, 7);
    CHECK(skirmish_setup_avail_amount(1, AvailKind_Trap, "LAVA") == 7);
    // switching an item the level never mentioned off leaves no trace
    skirmish_setup_set_avail(AvailKind_Door, 0, "WOOD", SkirmishAvail_On, 0);
    skirmish_setup_set_avail(AvailKind_Door, 0, "WOOD", SkirmishAvail_Off);
    CHECK(skirmish_setup().choices.values.avail.count([] { SetupAvailKey k; k.kind = AvailKind_Door; k.player = 0; k.item = "WOOD"; return k; }()) == 0);
    CHECK(skirmish_setup_avail_states(AvailKind_Creature).size() == 3);
    CHECK(skirmish_setup_avail_states(AvailKind_Trap).size() == 2);
}

TEST_CASE_METHOD(Fixture, "runtime-controlled rows are locked and ignore edits", "[kfx_frontend][skirmish_setup]") {
    CHECK(skirmish_setup_avail_locked(AvailKind_Magic, 0, "POWER_REAPER"));
    CHECK_FALSE(skirmish_setup_avail_locked(AvailKind_Magic, 1, "POWER_REAPER"));
    CHECK(skirmish_setup_avail_locked(AvailKind_Magic, -1, "POWER_REAPER")); // any player locked
    skirmish_setup_set_avail(AvailKind_Magic, 0, "POWER_REAPER", SkirmishAvail_On);
    CHECK(skirmish_setup_avail_state(AvailKind_Magic, 0, "POWER_REAPER") == SkirmishAvail_Research); // still the level's (1,0)
    CHECK_FALSE(skirmish_setup_is_changed());
    // an all-players edit changes only the unlocked players
    skirmish_setup_set_avail(AvailKind_Magic, -1, "POWER_REAPER", SkirmishAvail_On);
    CHECK(skirmish_setup_avail_state(AvailKind_Magic, 1, "POWER_REAPER") == SkirmishAvail_On);
    CHECK(skirmish_setup_avail_state(AvailKind_Magic, 0, "POWER_REAPER") == SkirmishAvail_Research);
    const std::vector<std::string> items = skirmish_setup_script_items(AvailKind_Magic);
    CHECK(std::find(items.begin(), items.end(), "POWER_REAPER") != items.end());
}

TEST_CASE_METHOD(Fixture, "general settings: gold, max creatures, generation speed, pool", "[kfx_frontend][skirmish_setup]") {
    skirmish_setup_set_money(-1, 5000);
    CHECK(skirmish_setup().choices.values.start_money.at(0) == 5000);
    CHECK(skirmish_setup().choices.values.start_money.at(1) == 5000);
    skirmish_setup_set_money(1, 0); // 0 = none
    CHECK(skirmish_setup().choices.values.start_money.count(1) == 0);
    skirmish_setup_set_max_creatures(0, 9);
    CHECK(skirmish_setup().choices.values.max_creatures.at(0) == 9);
    skirmish_setup_set_generate_speed(250);
    CHECK(skirmish_setup().choices.values.generate_speed == 250);
    skirmish_setup_set_generate_speed(-1); // back to the level's
    CHECK(skirmish_setup().choices.values.generate_speed == 400);
    skirmish_setup_set_pool("orc", 3);
    CHECK(skirmish_setup().choices.values.pool.at("ORC") == 3);
    skirmish_setup_set_pool("FLY", 0);
    CHECK(skirmish_setup().choices.values.pool.count("FLY") == 0);
    CHECK(skirmish_setup_is_changed());
    skirmish_setup_reset_choices();
    CHECK_FALSE(skirmish_setup_is_changed());
}

TEST_CASE_METHOD(Fixture, "slots & AI: controllers and teams", "[kfx_frontend][skirmish_setup]") {
    int64_t model = -1;
    CHECK(skirmish_setup_controller_choice(1, &model) == SkirmishCtl_LevelDefault);
    skirmish_setup_set_controller(1, SkirmishCtl_Model, 13);
    CHECK(skirmish_setup_controller_choice(1, &model) == SkirmishCtl_Model);
    CHECK(model == 13);
    skirmish_setup_set_controller(1, SkirmishCtl_Roaming, 0);
    CHECK(skirmish_setup_controller_choice(1, &model) == SkirmishCtl_Roaming);
    skirmish_setup_set_controller(1, SkirmishCtl_LevelDefault, 0);
    CHECK(skirmish_setup_controller_choice(1, &model) == SkirmishCtl_LevelDefault);
    CHECK_FALSE(skirmish_setup_is_changed());
    // the human slot is never given a controller
    skirmish_setup_set_controller(0, SkirmishCtl_Off, 0);
    CHECK(skirmish_setup().choices.values.controllers.count(0) == 0);

    skirmish_setup_set_controller(1, SkirmishCtl_Model, 13);
    skirmish_setup_set_team(0, 1);
    skirmish_setup_set_team(1, 1);
    const SetupOverride o = skirmish_setup_build();
    REQUIRE(o.active);
    CHECK(o.prelude.find("COMPUTER_PLAYER(PLAYER1,13)\n") != std::string::npos);
    CHECK(o.prelude.find("ALLY_PLAYERS(PLAYER0,PLAYER1,1)\n") != std::string::npos);
}

TEST_CASE_METHOD(Fixture, "win/lose: templates, summaries, and the empty-win-set block on Play", "[kfx_frontend][skirmish_setup]") {
    const std::vector<SetupWinLoseRule> last = skirmish_setup_template(SkirmishRule_LastKeeper, 0);
    REQUIRE(last.size() == 2);
    CHECK(last[1].clauses[0].player == 1);
    CHECK(last[1].clauses[0].variable == "ALL_DUNGEONS_DESTROYED");
    const std::vector<SetupWinLoseRule> surv = skirmish_setup_template(SkirmishRule_SurviveMinutes, 10);
    REQUIRE(surv.size() == 1);
    CHECK(surv[0].clauses[0].value == 10 * 60 * 20);
    std::string text;
    CHECK(skirmish_setup_rule_summary(last[0], text));
    CHECK(text == "Win when player 1: all dungeons destroyed == 1");

    // Replace with nothing: blocked.
    skirmish_setup().choices.replace_win_lose = true;
    skirmish_setup().choices.rules.clear();
    CHECK(skirmish_setup_play_blocked(kLevel) == 1);
    bool has_error = false;
    for (const SetupIssue &i : skirmish_setup_play_issues())
        has_error |= (i.severity == SetupIssue_Error);
    CHECK(has_error);
    // With a template it is fine.
    skirmish_setup().choices.rules = last;
    skirmish_setup().choices.rules[0].clauses[0].value = 0; // a real difference from the level's rule
    CHECK(skirmish_setup_play_blocked(kLevel) == 0);
}

TEST_CASE_METHOD(Fixture, "Play installs the override for this level only, and only when something changed", "[kfx_frontend][skirmish_setup]") {
    skirmish_setup_install_for_play(kLevel);
    CHECK_FALSE(level_script_override_is_set()); // untouched: the shipped script runs

    skirmish_setup_set_money(-1, 7777);
    skirmish_setup_install_for_play(kLevel + 1); // not the level the tab holds
    CHECK_FALSE(level_script_override_is_set());
    skirmish_setup_install_for_play(kLevel);
    REQUIRE(level_script_override_matches(kLevel));
    CHECK(std::string(level_script_override_prelude()).find("START_MONEY(ALL_PLAYERS,7777)") != std::string::npos);
    CHECK(std::string(level_script_override_masked()).find("START_MONEY") == std::string::npos);
    CHECK(std::string(level_script_override_masked()).find("GAME_TURN > 50") != std::string::npos); // runtime logic kept

    skirmish_setup_reset_choices();
    skirmish_setup_install_for_play(kLevel); // installing again with defaults clears the old one
    CHECK_FALSE(level_script_override_is_set());
}

TEST_CASE_METHOD(Fixture, "end to end: an installed override drives the real script loader", "[kfx_frontend][skirmish_setup]") {
    // Replace the level's two win rules with 'last keeper standing' (still 2 rules) plus a survival rule.
    SkirmishSetup &s = skirmish_setup();
    s.choices.replace_win_lose = true;
    s.choices.rules = skirmish_setup_template(SkirmishRule_LastKeeper, 0);
    for (const SetupWinLoseRule &r : skirmish_setup_template(SkirmishRule_SurviveMinutes, 5))
        s.choices.rules.push_back(r);
    skirmish_setup_set_money(-1, 1234);
    skirmish_setup_install_for_play(kLevel);
    REQUIRE(level_script_override_matches(kLevel));

    REQUIRE(preload_script(kLevel));
    REQUIRE(load_script(kLevel));
    CHECK_FALSE(level_script_override_is_set());                  // one-shot
    CHECK(kfx_game_state.script.win_conditions_num == 3);          // 2 last-keeper + 1 survive; the level's own 2 were replaced
    // conditions: 3 prelude rules + the file's own runtime Reaper IF = 4
    CHECK(kfx_game_state.script.conditions_num == 4);
    CHECK(kfx_game_state.level_file_version == 1); // the file's own version, untouched by the prelude
}

TEST_CASE("a Lua companion that changes the setup is reported, not locked", "[kfx_frontend][skirmish_setup]") {
    const std::string lua = "function OnGameStart()\n StartMoney(PLAYER0, 50)\n CreatureAvailable(PLAYER0, 'FLY', true, 0)\nend\n";
    skirmish_setup_load_from_text(kLevel, kScript, 2, true, SkirmishSetup_Auto, 0, &lua);
    const SkirmishSetup &s = skirmish_setup();
    REQUIRE(s.enabled());
    CHECK(s.analysis.has_lua_companion);
    CHECK(s.analysis.lua.money);
    CHECK(s.analysis.lua.avail[AvailKind_Creature]);
    CHECK(s.analysis.verdict == SetupVerdict_Partial);
    CHECK(s.analysis.reason.find("Lua") != std::string::npos);
    // Still editable: a warning only.
    skirmish_setup_set_money(0, 4321);
    CHECK(skirmish_setup().choices.values.start_money.at(0) == 4321);
    skirmish_setup_forget();
}

TEST_CASE_METHOD(Fixture, "a controller for a slot with no Dungeon Heart gets a note", "[kfx_frontend][skirmish_setup]") {
    skirmish_setup().hearts = { 1, 0 }; // the map has a heart for player 1 only
    skirmish_setup_set_controller(1, SkirmishCtl_Model, 14);
    bool noted = false;
    for (const SetupIssue &i : skirmish_setup_play_issues())
        noted |= (i.severity == SetupIssue_Warning && i.message.find("no Dungeon Heart") != std::string::npos);
    CHECK(noted);
    // Unknown hearts (-1) never warn; and the note is not an error, so Play stays open.
    skirmish_setup().hearts = { -1, -1 };
    for (const SetupIssue &i : skirmish_setup_play_issues())
        CHECK(i.message.find("no Dungeon Heart") == std::string::npos);
    skirmish_setup().hearts = { 1, 0 };
    CHECK(skirmish_setup_play_blocked(kLevel) == 0);
}

TEST_CASE_METHOD(Fixture, "an External slot is remembered, exclusive with the other controllers, and queued for Play", "[kfx_frontend][skirmish_setup][external]") {
    skirmish_setup_install_for_play(kLevel);
    CHECK(net_pending_external_seats_count() == 0);

    skirmish_setup_set_controller(1, SkirmishCtl_External, 0);
    CHECK(skirmish_setup_controller_choice(1, nullptr) == SkirmishCtl_External);
    CHECK_FALSE(skirmish_setup_is_changed()); // the script side is untouched: no override is needed for it
    skirmish_setup_install_for_play(kLevel);
    CHECK(net_pending_external_seats_count() == 1);
    CHECK_FALSE(level_script_override_is_set());

    skirmish_setup_set_controller(0, SkirmishCtl_External, 0); // the human slot is never an agent
    CHECK(skirmish_setup_controller_choice(0, nullptr) != SkirmishCtl_External);

    skirmish_setup_install_for_play(kLevel + 1); // another level: nothing carried over
    CHECK(net_pending_external_seats_count() == 0);

    skirmish_setup_set_controller(1, SkirmishCtl_Model, 13); // choosing something else replaces External
    CHECK(skirmish_setup_controller_choice(1, nullptr) == SkirmishCtl_Model);
    skirmish_setup_install_for_play(kLevel);
    CHECK(net_pending_external_seats_count() == 0);

    skirmish_setup_set_controller(1, SkirmishCtl_External, 0);
    skirmish_setup_reset_choices(); // Reset returns to the level's defaults
    CHECK(skirmish_setup_controller_choice(1, nullptr) == SkirmishCtl_LevelDefault);
    skirmish_setup_install_for_play(kLevel);
    CHECK(net_pending_external_seats_count() == 0);
}
