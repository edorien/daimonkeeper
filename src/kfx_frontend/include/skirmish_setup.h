/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file skirmish_setup.h
 *     Header file for skirmish_setup.cpp.
 * @par Purpose:
 *     docs/refactor/skirmish/ (S4) -- the state and data flow behind the
 *     Skirmish screen's "Setup" tab: reads the highlighted level's script into
 *     kfx_config's setup model (script_setup_analysis.h), holds the player's
 *     edits (SetupChoices) plus the per-slot team choice, offers the
 *     edit operations the UI needs (availability states, pool, win/lose
 *     rules and templates, controllers), and turns the result into a level
 *     script override at Play (script_setup_prelude.h -> level_script_override.h).
 *     No ImGui here (that is frontgui_skirmish_setup.cpp), so it is unit-
 *     testable. The state is a file-static in the .cpp, deliberately NOT in
 *     kfx_frontend_state: that struct is memcpy'd into save games, and this
 *     holds std::string/std::map.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_SKIRMISH_SETUP_H
#define DK_SKIRMISH_SETUP_H

#include "globals.h"

#ifdef __cplusplus
#include "script_setup_prelude.h"

#include <string>
#include <vector>

// How the UI presents one availability cell. Which states exist depends on
// the kind (see skirmish_setup_avail_states()).
enum SkirmishAvailState
{
    SkirmishAvail_Off = 0,
    SkirmishAvail_Research, // rooms / spells: can be researched, not usable yet
    SkirmishAvail_On,       // available (creatures: in the pool; rooms/spells: usable; traps/doors: buildable)
    SkirmishAvail_Forced    // creatures only: force-enabled
};

struct SkirmishSetup
{
    LevelNumber lvnum = 0;
    bool loaded = false;
    int players = 2;              // keeper slots of the level (.lof PLAYERS)
    int human_slot = 0;
    std::string text;             // the level's script as shipped
    SetupAnalysis analysis;
    SetupChoices choices;
    std::vector<int> teams;       // per slot, 0 = no team; equal non-zero = allied
    std::vector<int> hearts;      // per slot: 1 = the map has a Dungeon Heart for it, 0 = none, -1 = unknown
    std::string unavailable_reason; // non-empty: the tab is disabled, with this reason
    bool enabled() const { return loaded && unavailable_reason.empty(); }
};

// The current state (never null; `loaded` false until a level is synced).
SkirmishSetup &skirmish_setup();

// Reads and analyses `lvnum`'s script when it differs from the loaded level
// (cheap no-op otherwise). Called every frame by the screen with the
// highlighted level.
void skirmish_setup_sync(LevelNumber lvnum, int human_slot);

// Seeds from text directly (the disk read in skirmish_setup_sync() calls this;
// tests use it too). `lof_option` is a SkirmishSetupOption.
// `lua_text`: the level's map*.lua, if it has one (scanned for calls that change these settings).
void skirmish_setup_load_from_text(LevelNumber lvnum, const std::string &text, int players, bool has_lua,
    unsigned char lof_option, int human_slot, const std::string *lua_text = nullptr);

// Drops the level and everything edited (leaving the screen).
void skirmish_setup_forget();
// Back to the level's own defaults.
void skirmish_setup_reset_choices();
// Anything edited since the defaults?
bool skirmish_setup_is_changed();

// The override the current choices would install (teams folded into alliances,
// item names validated against the loaded config and the script's own names).
SetupOverride skirmish_setup_build();

// ---- availability ---------------------------------------------------------
// States the UI offers for this kind, in cycle order.
std::vector<SkirmishAvailState> skirmish_setup_avail_states(int kind);
// player -1 = "all players": the value of player 0, with *mixed set when players differ.
SkirmishAvailState skirmish_setup_avail_state(int kind, int player, const std::string &item, bool *mixed = nullptr);
int skirmish_setup_avail_amount(int player, int kind, const std::string &item); // traps/doors: stock
void skirmish_setup_set_avail(int kind, int player, const std::string &item, SkirmishAvailState state, int amount = 0);
// Item not editable for this player (runtime-controlled by the level script)?
bool skirmish_setup_avail_locked(int kind, int player, const std::string &item);
// Names present in the level's own script for this kind (shown even if the base config lacks them).
std::vector<std::string> skirmish_setup_script_items(int kind);

// ---- general ----------------------------------------------------------------
void skirmish_setup_set_pool(const std::string &creature, int amount); // 0 removes
void skirmish_setup_set_money(int player, int gold);                   // player -1 = all
void skirmish_setup_set_max_creatures(int player, int count);          // player -1 = all
void skirmish_setup_set_generate_speed(int speed);                     // < 0 = level default / unset

// ---- slots & AI ---------------------------------------------------------------
enum SkirmishControllerChoice { SkirmishCtl_LevelDefault = 0, SkirmishCtl_Model, SkirmishCtl_Roaming, SkirmishCtl_Off };
SkirmishControllerChoice skirmish_setup_controller_choice(int slot, int *model);
void skirmish_setup_set_controller(int slot, SkirmishControllerChoice choice, int model);
void skirmish_setup_set_team(int slot, int team);

// ---- win / lose ----------------------------------------------------------------
enum SkirmishRuleTemplate { SkirmishRule_LastKeeper, SkirmishRule_SurviveMinutes, SkirmishRule_GoldTarget };
// Rules for the template (win rules for each keeper slot / for the human slot); `value` is the
// minutes or gold amount for the parametrised ones.
std::vector<SetupWinLoseRule> skirmish_setup_template(SkirmishRuleTemplate t, int value);
// True when the rule uses only fields the tab can edit (whitelisted variable, valid player).
bool skirmish_setup_rule_summary(const SetupWinLoseRule &rule, std::string &out);

// ---- Play integration ---------------------------------------------------------
// Errors that must block Play (empty win set, over budget, ...); empty when fine.
std::vector<SetupIssue> skirmish_setup_play_issues();

extern "C" {
#endif

// C entry points for frontmenu_select.c (Play): install the override for `lvnum`, or clear any.
void skirmish_setup_install_for_play(LevelNumber lvnum);
// Non-zero when the tab's choices have blocking errors (Play must not proceed).
int skirmish_setup_play_blocked(LevelNumber lvnum);

#ifdef __cplusplus
}
#endif

#endif
