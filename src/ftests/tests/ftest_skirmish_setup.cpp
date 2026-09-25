#include "ftest_skirmish_setup.h"

#ifdef FUNCTESTING

#include <strings.h>
extern "C" {
#include "pre_inc.h"
#include "../ftest.h"
#include "../ftest_util.h"
#include "game_legacy.h"
#include "config_keeperfx.h"
#include "config_creature.h"
#include "config_magic.h"
#include "config_terrain.h"
#include "dungeon_data.h"
#include "player_data.h"
#include "player_computer.h"
#include "kfx_sim_state.h"
#include "kfx_game_state.h"
#include "lvl_filesdk1.h"
#include "level_script_override.h"
#include "main_game.h"
#include "game_saves.h"
#include "config_campaigns.h"
#include "post_inc.h"
}
#include "skirmish_setup.h"

extern "C" {
extern int64_t fe_computer_players; // frontend.h -- set by Skirmish's Play button (frontend_freeplay_enter_resolve)
}

namespace {

int64_t s_failures = 0;
int64_t s_checks = 0;
bool s_installed = false;
LevelNumber s_level = 0;

#define CHECK_EQ(what, actual, expected) \
    do { \
        s_checks++; \
        const int64_t a_ = (int64_t)(actual); \
        const int64_t e_ = (int64_t)(expected); \
        if (a_ != e_) { s_failures++; FTEST_FAIL_TEST("%s: got %" PRId64 ", expected %" PRId64, what, (int64_t)(a_), (int64_t)(e_)); } \
    } while (0)

ThingModel creature(const char *name) { return (ThingModel)get_rid(creature_desc, name); }
RoomKind room(const char *name) { return (RoomKind)get_rid(room_desc, name); }
PowerKind power(const char *name) { return (PowerKind)get_rid(power_desc, name); }

} // namespace

extern "C" {

FTestActionResult ftest_skirmish_setup_action001__check_live_state(struct FTestActionArgs* const args);
FTestActionResult ftest_skirmish_setup_action002__save_load_round_trip(struct FTestActionArgs* const args);

static int64_t s_unused;

// Runs after the mappack and level number are selected and before the level loads: edit the setup the way
// the Setup tab does, then install the override the way Play does.
void ftest_skirmish_setup_pre_start()
{
    s_level = get_selected_level_number();
    fe_computer_players = 1; // what Skirmish's Play does, so the second keeper is a computer player

    skirmish_setup_sync(s_level, 0);
    if (!skirmish_setup().enabled())
    {
        FTEST_FAIL_TEST("Setup tab disabled for level %" PRId64 ": %s", (int64_t)s_level, skirmish_setup().unavailable_reason.c_str());
        return;
    }

    // Hearts come straight from the map's thing file (classic .tng here): both keepers have one.
    if (skirmish_setup().hearts != std::vector<int64_t>({ 1, 1 }))
        FTEST_FAIL_TEST("hearts read from the .tng: got %" PRId64 ",%" PRId64 " expected 1,1",
            (int64_t)(skirmish_setup().hearts.size() > 0 ? skirmish_setup().hearts[0] : -9),
            (int64_t)(skirmish_setup().hearts.size() > 1 ? skirmish_setup().hearts[1] : -9));

    skirmish_setup_set_money(0, 20000);           // level: 10000 for everyone
    skirmish_setup_set_money(1, 3000);
    skirmish_setup_set_max_creatures(0, 9);       // level: 17 for everyone
    skirmish_setup_set_generate_speed(250);       // level: 400
    skirmish_setup_set_pool("TROLL", 5);          // level: 20
    skirmish_setup_set_pool("ORC", 0);            // removed from the pool
    skirmish_setup_set_avail(AvailKind_Creature, 0, "TROLL", SkirmishAvail_Off);
    skirmish_setup_set_avail(AvailKind_Creature, 0, "DRAGON", SkirmishAvail_Forced);
    skirmish_setup_set_avail(AvailKind_Room, 0, "LAIR", SkirmishAvail_Off);
    skirmish_setup_set_avail(AvailKind_Room, 0, "TEMPLE", SkirmishAvail_On);   // level: researchable only
    skirmish_setup_set_avail(AvailKind_Magic, 0, "POWER_SPEED", SkirmishAvail_On);
    skirmish_setup_set_controller(1, SkirmishCtl_Model, 13);                    // a skirmish preset, pinned
    skirmish_setup().choices.replace_win_lose = true;
    skirmish_setup().choices.rules = skirmish_setup_template(SkirmishRule_LastKeeper, 0);
    for (const SetupWinLoseRule &r : skirmish_setup_template(SkirmishRule_SurviveMinutes, 1))
        skirmish_setup().choices.rules.push_back(r);

    for (const SetupIssue &i : skirmish_setup_play_issues())
        if (i.severity == SetupIssue_Error)
            FTEST_FAIL_TEST("Setup problem: %s", i.message.c_str());

    skirmish_setup_install_for_play(s_level);
    s_installed = level_script_override_matches(s_level);
}

TbBool ftest_skirmish_setup_init()
{
    s_failures = 0;
    s_checks = 0;
    ftest_append_action(ftest_skirmish_setup_action001__check_live_state, 30, &s_unused);
    ftest_append_action(ftest_skirmish_setup_action002__save_load_round_trip, 5, &s_unused);
    return true;
}

// Everything the edited setup must have produced, checked against the live sim. Used right after level
// load and again after a save/load round trip. `fresh_load` also checks what only a fresh load can show.
static void check_edited_setup(const char *phase, bool fresh_load)
{
    FTESTLOG("Checking the live state (%s)", phase);

    // The override was installed before load and consumed by it (one-shot) -- and must still be gone
    // after a save/load: nothing depends on it once the level is running.
    CHECK_EQ("override consumed", level_script_override_is_set(), 0);
    if (fresh_load)
    {
        CHECK_EQ("override installed before the level loaded", s_installed, 1);
        // The file is v0; the prelude's forced v1 must not have leaked into the file's own version.
        CHECK_EQ("level_file_version is the file's own (0)", level_file_version, 0);
    }

    const struct Dungeon *d0 = get_dungeon(0);
    const struct Dungeon *d1 = get_dungeon(1);

    // General
    CHECK_EQ("player 0 start gold", d0->offmap_money_owned, 20000);
    CHECK_EQ("player 1 start gold", d1->offmap_money_owned, 3000);
    CHECK_EQ("player 0 max creatures", d0->max_creatures_attracted, 9);
    CHECK_EQ("player 1 max creatures (untouched: the level's 17)", d1->max_creatures_attracted, 17);
    CHECK_EQ("player 0 generate speed", get_player(0)->generate_speed, 250);
    CHECK_EQ("player 1 generate speed", get_player(1)->generate_speed, 250);
    CHECK_EQ("pool TROLL", kfx_sim_state.pool.crtr_kind[creature("TROLL")], 5);
    CHECK_EQ("pool ORC (removed)", kfx_sim_state.pool.crtr_kind[creature("ORC")], 0);
    CHECK_EQ("pool DRAGON (untouched: the level's 20)", kfx_sim_state.pool.crtr_kind[creature("DRAGON")], 20);
    CHECK_EQ("pool BUG (untouched: the level's 20)", kfx_sim_state.pool.crtr_kind[creature("BUG")], 20);

    // Availability. The level is v0: its `CREATURE_AVAILABLE(ALL_PLAYERS,TROLL,1,1)` means "available, not
    // forced". The prelude re-emits that in v1 form, so player 1's untouched creatures must still not be forced.
    CHECK_EQ("player 0 TROLL allowed (turned off)", d0->creature_allowed[creature("TROLL")], 0);
    CHECK_EQ("player 1 TROLL allowed", d1->creature_allowed[creature("TROLL")], 1);
    CHECK_EQ("player 1 TROLL forced (v0 translation: not forced)", d1->creature_force_enabled[creature("TROLL")], 0);
    CHECK_EQ("player 0 DRAGON allowed", d0->creature_allowed[creature("DRAGON")], 1);
    CHECK_EQ("player 0 DRAGON forced (edited)", d0->creature_force_enabled[creature("DRAGON")], 1);
    CHECK_EQ("player 1 DRAGON forced (untouched)", d1->creature_force_enabled[creature("DRAGON")], 0);
    CHECK_EQ("player 0 LAIR buildable (turned off)", d0->room_buildable[room("LAIR")] & 1, 0);
    CHECK_EQ("player 1 LAIR buildable", d1->room_buildable[room("LAIR")] & 1, 1);
    CHECK_EQ("player 0 TEMPLE buildable (edited to On)", d0->room_buildable[room("TEMPLE")] & 1, 1);
    CHECK_EQ("player 1 TEMPLE buildable (untouched: researchable only)", d1->room_buildable[room("TEMPLE")] & 1, 0);
    CHECK_EQ("player 0 POWER_SPEED castable (edited to On)", d0->magic_level[power("POWER_SPEED")] != 0, 1);
    CHECK_EQ("player 1 POWER_SPEED castable (untouched: researchable only)", d1->magic_level[power("POWER_SPEED")] != 0, 0);

    // Win rules: the level's own two were replaced by 2 x last-keeper + 1 x survive.
    CHECK_EQ("win conditions", kfx_game_state.script.win_conditions_num, 3);

    // Player 1 is a computer keeper running the pinned skirmish model.
    CHECK_EQ("player 1 is computer controlled", (get_player(1)->allocflags & PlaF_CompCtrl) != 0, 1);
    CHECK_EQ("player 1 computer model", get_computer_player(1)->model, 13);

    if (s_failures == 0)
        FTESTLOG("All %" PRId64 " checks passed", (int64_t)(s_checks));
    else
        FTESTLOG("%" PRId64 " of %" PRId64 " checks failed", (int64_t)(s_failures), (int64_t)(s_checks));
}

FTestActionResult ftest_skirmish_setup_action001__check_live_state(struct FTestActionArgs* const args)
{
    check_edited_setup("fresh level load", true);

    return FTRs_Go_To_Next_Action;
}

// The setup must survive a save and reload with no override around: the script is not re-read on load,
// everything the prelude did lives in the saved sim/game state (docs/refactor/skirmish/ section 14).
FTestActionResult ftest_skirmish_setup_action002__save_load_round_trip(struct FTestActionArgs* const args)
{
    const int64_t slot = 3;
    // As the Save screen / console `save <slot> <name>` do: fill the catalogue entry (level number, campaign)
    // first -- without it the load falls back to the default campaign.
    fill_game_catalogue_slot(slot, "skirmish_setup_ftest");
    set_flag(kfx_sim_state.operation_flags, GOF_Paused); // games are saved paused (as the console `save` does)
    const TbBool saved = save_game(slot);
    clear_flag(kfx_sim_state.operation_flags, GOF_Paused);
    CHECK_EQ("save_game", saved, 1);
    if (!saved)
        return FTRs_Go_To_Next_Action;

    // Break the live state so a load that restored nothing would be caught.
    struct Dungeon *d0 = get_dungeon(0);
    d0->offmap_money_owned = 1;
    d0->max_creatures_attracted = 99;
    d0->room_buildable[room("LAIR")] |= 1;
    d0->creature_allowed[creature("TROLL")] = 1;
    kfx_sim_state.pool.crtr_kind[creature("TROLL")] = 999;
    kfx_game_state.script.win_conditions_num = 0;
    get_computer_player(1)->model = 3;

    CHECK_EQ("save is loadable", is_save_game_loadable(slot), 1);
    const TbBool loaded = load_game(slot);
    CHECK_EQ("load_game", loaded, 1);
    if (loaded)
    {
        // A skirmish level lives in a multiplayer mappack: the load must come back to that pack and level,
        // not fall back to the default campaign.
        CHECK_EQ("level number after load", get_loaded_level_number(), s_level);
        CHECK_EQ("campaign after load is the mappack", strcasecmp(campaign.fname, "original.cfg") == 0, 1);
        check_edited_setup("after save/load", false);
    }

    if (s_failures == 0)
        FTESTLOG("Round trip ok: all %" PRId64 " checks passed", (int64_t)(s_checks));
    else
        FTESTLOG("%" PRId64 " of %" PRId64 " checks failed", (int64_t)(s_failures), (int64_t)(s_checks));
    return FTRs_Go_To_Next_Action;
}

// ---------------------------------------------------------------------------------------------------------
// Second scenario: a LEVEL_VERSION(1) map whose script controls some rows at runtime (dk2maps map 220: the
// Reaper spell toggle). Edits to the locked rows must be ignored -- the level's own value stays -- while
// edits elsewhere apply, and the level's runtime script logic (its own conditions) must still be loaded.
// ---------------------------------------------------------------------------------------------------------
static bool s_locks_installed = false;

void ftest_skirmish_setup_locks_pre_start()
{
    s_level = get_selected_level_number();
    skirmish_setup_sync(s_level, 0);
    if (!skirmish_setup().enabled())
    {
        FTEST_FAIL_TEST("Setup tab disabled for level %" PRId64 ": %s", (int64_t)s_level, skirmish_setup().unavailable_reason.c_str());
        return;
    }
    // Hearts from a native-format map (.tngfx, hand-authored: numbered [thingN] tables and class names).
    if (skirmish_setup().hearts != std::vector<int64_t>({ 1, 1 }))
        FTEST_FAIL_TEST("hearts read from the .tngfx: got %" PRId64 ",%" PRId64 " expected 1,1",
            (int64_t)(skirmish_setup().hearts.size() > 0 ? skirmish_setup().hearts[0] : -9),
            (int64_t)(skirmish_setup().hearts.size() > 1 ? skirmish_setup().hearts[1] : -9));
    if (!skirmish_setup_avail_locked(AvailKind_Magic, 0, "POWER_REAPER"))
        FTEST_FAIL_TEST("POWER_REAPER should be runtime-controlled (locked) for player 0 on this level");

    skirmish_setup_set_money(-1, 12345);                                           // applies
    skirmish_setup_set_avail(AvailKind_Magic, 0, "POWER_REAPER", SkirmishAvail_On); // locked: must be ignored
    skirmish_setup_set_avail(AvailKind_Room, 0, "TEMPLE", SkirmishAvail_On);        // applies
    skirmish_setup_install_for_play(s_level);
    s_locks_installed = level_script_override_matches(s_level);
}

FTestActionResult ftest_skirmish_setup_locks_action001__check(struct FTestActionArgs* const args)
{
    s_failures = 0;
    s_checks = 0;
    CHECK_EQ("override installed before the level loaded", s_locks_installed, 1);
    CHECK_EQ("override consumed", level_script_override_is_set(), 0);
    CHECK_EQ("level_file_version is the file's own (1)", level_file_version, 1);

    const struct Dungeon *d0 = get_dungeon(0);
    const struct Dungeon *d1 = get_dungeon(1);
    CHECK_EQ("player 0 start gold (edited)", d0->offmap_money_owned, 12345);
    CHECK_EQ("player 1 start gold (edited)", d1->offmap_money_owned, 12345);
    CHECK_EQ("player 0 TEMPLE buildable (edited)", d0->room_buildable[room("TEMPLE")] & 1, 1);
    // The level's own `MAGIC_AVAILABLE(PLAYER0,POWER_REAPER,1,0)`: researchable, not castable. The ignored edit
    // (On = castable) must not have changed it.
    CHECK_EQ("player 0 POWER_REAPER not castable (locked edit ignored)", d0->magic_level[power("POWER_REAPER")] != 0, 0);
    // Its runtime script logic is still there: the level's own IF blocks, plus the win conditions untouched.
    CHECK_EQ("win conditions (the level's own two, Keep mode)", kfx_game_state.script.win_conditions_num, 2);
    CHECK_EQ("the level's runtime conditions are loaded", kfx_game_state.script.conditions_num > 10, 1);

    if (s_failures == 0)
        FTESTLOG("All %" PRId64 " checks passed", (int64_t)(s_checks));
    else
        FTESTLOG("%" PRId64 " of %" PRId64 " checks failed", (int64_t)(s_failures), (int64_t)(s_checks));
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_skirmish_setup_locks_init()
{
    ftest_append_action(ftest_skirmish_setup_locks_action001__check, 30, &s_unused);
    return true;
}

} // extern "C"

#endif // FUNCTESTING
