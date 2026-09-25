/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file script_setup_prelude.h
 *     Header file for script_setup_prelude.cpp.
 * @par Purpose:
 *     docs/refactor/skirmish/ (S2b) -- turns the Skirmish setup tab's choices
 *     into the two halves of a level-script override (01-...md §2):
 *       * the PRELUDE: a block of v1-syntax commands that the loader scans
 *         first, under a forced level_file_version of 1 (so it contains no
 *         LEVEL_VERSION line -- that would leak into the file's own parse);
 *       * the MASKED script: the original with every tab-owned line blanked
 *         (script_setup_mask()).
 *     Because the mask removes *all* owned lines, the prelude re-emits the whole
 *     effective state (unchanged values included). "Nothing changed" is
 *     detected up front and produces no override at all, so an untouched tab
 *     leaves the level byte-for-byte as shipped.
 *
 *     Also the validators: engine budgets (conditions, win/lose conditions),
 *     value ranges, player numbers, controller model range, unknown item names
 *     (through an optional callback, since the name tables are filled at
 *     config-load time), an empty win set in Replace mode, and edits to fields
 *     the level script controls at runtime (ignored, with a warning).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_SCRIPT_SETUP_PRELUDE_H
#define DK_SCRIPT_SETUP_PRELUDE_H

#include "script_setup_analysis.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

// Engine limits, mirrored from kfx_game (lvl_script.h) because that library
// ranks above kfx_config; a kfx_editor test cross-checks them against the
// real macros.
enum
{
    kSetupConditionsCount = 512,    // CONDITIONS_COUNT (every IF*)
    kSetupWinConditionsCount = 12,  // WIN_CONDITIONS_COUNT, separately for WIN_GAME and LOSE_GAME
    kSetupSensibleGold = 99999999   // SENSIBLE_GOLD: START_MONEY is clamped to this
};

enum SetupIssueSeverity { SetupIssue_Warning = 0, SetupIssue_Error };

struct SetupIssue
{
    SetupIssueSeverity severity = SetupIssue_Error;
    std::string message;
};

// The tab's state. Starts as the level's own defaults
// (script_setup_default_choices) and is edited from there.
struct SetupChoices
{
    SetupSeed values;                 // same shape as the level's static state
    bool replace_win_lose = false;    // false: Keep the level's rules, true: Replace them with `rules`
    std::vector<SetupWinLoseRule> rules; // used only when replace_win_lose
    std::vector<std::pair<int64_t, int64_t>> allies; // extra ALLY_PLAYERS(a,b,1); the file's own are untouched
};

struct SetupBuildOptions
{
    int64_t players = 2;    // keeper slots of the level (.lof PLAYERS)
    bool force = false; // produce an override even when nothing differs (tests/diagnostics)
    // Optional: does this item exist in the loaded config? `field` is a
    // SetupField (availability kinds and SetupField_Pool). Unknown -> error.
    std::function<bool(int64_t field, const std::string &item)> item_exists;
};

struct SetupOverride
{
    bool active = false; // false: install nothing, run the shipped script
    std::string prelude; // v1 commands, scanned first at forced version 1
    std::string masked;  // original script with owned lines blanked
    std::vector<SetupIssue> issues;
    bool has_errors() const;
};

SetupChoices script_setup_default_choices(const SetupAnalysis &analysis);

// True when the choices reproduce the level's own state exactly (an override
// would be a no-op): every value equal to the seed, Keep mode (or Replace with
// identical rules) and no extra alliances.
bool script_setup_choices_are_default(const SetupAnalysis &analysis, const SetupChoices &choices);

// The prelude text alone (also used by tests). Locked fields are emitted with
// the level's own value whatever `choices` says. `issues` may be null.
std::string script_setup_generate_prelude(const SetupAnalysis &analysis, const SetupChoices &choices,
    int64_t players, std::vector<SetupIssue> *issues);

// Validates and builds the whole override. On any Error `active` stays false.
SetupOverride script_setup_build_override(const std::string &text, const SetupAnalysis &analysis,
    const SetupChoices &choices, const SetupBuildOptions &options);

#endif
