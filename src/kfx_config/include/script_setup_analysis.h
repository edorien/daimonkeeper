/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file script_setup_analysis.h
 *     Header file for script_setup_analysis.cpp.
 * @par Purpose:
 *     docs/refactor/skirmish/ (S2a) -- reads a level's classic (.txt) script
 *     into a version-independent "setup" model, works out which lines the
 *     Skirmish setup tab owns (the mask), and classifies whether the level is
 *     suitable for the tab at all. Pure text logic over plain strings: no
 *     engine state, no name-table lookups (item names stay strings, so
 *     mod-specific names and an unloaded config both work).
 *
 *     Owned lines: depth-0, non-reusable setup commands (SET_GENERATE_SPEED,
 *     START_MONEY, MAX_CREATURES, ADD_CREATURE_TO_POOL, the five *_AVAILABLE),
 *     COMPUTER_PLAYER, and "pure" win/lose blocks
 *     (IF(...) [IF(...)...] WIN_GAME|LOSE_GAME ENDIF with nothing else
 *     inside). Setup commands under an IF or after NEXT_COMMAND_REUSABLE are
 *     runtime game logic: never owned, and they *lock* the matching tab field.
 *
 *     Version handling (docs/refactor/skirmish/01-...md §1, §12.1): the file's
 *     version is the LAST LEVEL_VERSION anywhere in it (whole-file property,
 *     as the engine's preload pass makes it); the reader normalises v0 forms
 *     (CREATURE_AVAILABLE's 4th argument, TOTAL_IMPS) to the v1 model.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_SCRIPT_SETUP_ANALYSIS_H
#define DK_SCRIPT_SETUP_ANALYSIS_H

#include "script_setup.h" // WinLoseClause, AvailabilityKind

#include <cstddef>
#include <map>
#include <string>
#include <vector>

// The tab fields a runtime script command can take over. The first five are
// the AvailabilityKind values, so a kind can be used as a field directly.
enum SetupField
{
    SetupField_AvailCreature = AvailKind_Creature,
    SetupField_AvailRoom = AvailKind_Room,
    SetupField_AvailMagic = AvailKind_Magic,
    SetupField_AvailTrap = AvailKind_Trap,
    SetupField_AvailDoor = AvailKind_Door,
    SetupField_Money,       // START_MONEY, per player
    SetupField_MaxCreatures, // MAX_CREATURES, per player
    SetupField_GenSpeed,    // SET_GENERATE_SPEED, global
    SetupField_Pool,        // ADD_CREATURE_TO_POOL, item = creature name
    SetupField_Controller,  // COMPUTER_PLAYER / SET_COMPUTER_*, per player
    SetupField_Ally,        // ALLY_PLAYERS, per player
    SetupField_Count
};

// player == -1 means "every player" (ALL_PLAYERS). item is upper-case.
struct SetupLockKey
{
    int64_t field = 0;
    int64_t player = -1;
    std::string item;
    bool operator<(const SetupLockKey &o) const;
};

struct SetupAvailKey
{
    int64_t kind = 0;
    int64_t player = 0;
    std::string item; // upper-case
    bool operator<(const SetupAvailKey &o) const;
    bool operator==(const SetupAvailKey &o) const { return kind == o.kind && player == o.player && item == o.item; }
};

// Effective (a, b) after the level's static lines, v1 meaning: creature
// (available, force); room/magic (researchable, buildable/castable);
// trap/door (buildable, amount -- amount accumulates like the engine's).
struct SetupAvailValue
{
    int64_t a = 0;
    int64_t b = 0;
    bool operator==(const SetupAvailValue &o) const { return a == o.a && b == o.b; }
};

struct SetupController
{
    enum Kind { Model, Roaming, Off } kind = Model;
    int64_t model = 0;
    bool operator==(const SetupController &o) const { return kind == o.kind && model == o.model; }
};

struct SetupWinLoseRule
{
    bool win = true;
    std::vector<WinLoseClause> clauses; // outer IF first; variable in v1 spelling
    size_t first_line = 0, last_line = 0; // IF .. ENDIF, 0-based
};

// What the level's own static lines say (the tab's defaults).
struct SetupSeed
{
    int64_t generate_speed = -1;                 // -1: not set
    std::map<int64_t, int64_t> start_money;          // per player, additive like the engine
    std::map<int64_t, int64_t> max_creatures;        // per player, last wins
    std::map<std::string, int64_t> pool;         // per creature name, additive
    std::map<SetupAvailKey, SetupAvailValue> avail; // ALL_PLAYERS expanded per player
    std::map<int64_t, SetupController> controllers; // per player, last wins
    std::vector<SetupWinLoseRule> rules;
};

enum SetupVerdict
{
    SetupVerdict_Supported = 0, // nothing runtime touches the tab's fields
    SetupVerdict_Partial,       // some fields are runtime-controlled (see locks)
    SetupVerdict_Unsupported    // structural: no script, unknown LEVEL_VERSION
};

// Which of the tab's fields a level's Lua script (map%05u.lua) changes through the engine's script API
// (StartMoney, CreatureAvailable, WinGame, ...). Found by a comment- and string-aware token scan, so it
// says "this level's Lua calls that function somewhere", not what it passes -- enough to warn. Lua's
// OnGameStart runs after the classic script (and so after this tab's setup), and top-level Lua runs
// before it, so for these fields the final value can differ from what the tab shows.
struct SetupLuaUse
{
    bool scanned = false;
    bool money = false, max_creatures = false, gen_speed = false, pool = false;
    bool avail[AvailKind_Count] = { false, false, false, false, false };
    bool controller = false, ally = false, win = false, lose = false;
    bool research = false; // informational: research is not editable in the tab

    // Any field the tab edits (research and info-only flags excluded).
    bool any_setup() const;
    // Does the Lua script touch this SetupField?
    bool touches(int64_t field) const;
    // "start gold, the creature pool, room availability, ..." (empty when nothing).
    std::string describe() const;
};

// Pure text; never fails (an unparsable script just finds fewer calls).
SetupLuaUse script_setup_scan_lua(const std::string &lua_text);

struct SetupAnalysis
{
    int64_t level_version = 0;
    SetupVerdict verdict = SetupVerdict_Supported;
    std::string reason; // human-readable, for the Unsupported/Partial banner

    SetupSeed seed;
    std::vector<SetupLockKey> locks; // runtime-controlled fields, deduplicated

    // Per line (0-based, same indexing as the split of the source text):
    std::vector<bool> owned_setup;    // static setup / COMPUTER_PLAYER lines
    std::vector<bool> owned_win_lose; // lines of fully understood win/lose blocks
    size_t line_count = 0;

    // win/lose blocks (or bare WIN_GAME/LOSE_GAME) the model could not
    // represent; they stay in the script and cannot be replaced from the tab.
    int64_t custom_win_lose = 0;

    // ENDIFs with no open IF (the engine ignores them; shipped dk2maps
    // scripts have some). An IF that is never closed makes the verdict
    // Unsupported instead, since everything after it would be conditional.
    int64_t stray_endif = 0;

    // Engine-budget usage of the file as written (each IF* opens one
    // condition; win/lose conditions are counted separately per kind).
    int64_t if_count = 0;
    int64_t win_count = 0;
    int64_t lose_count = 0;

    // Informational tags (no lock): explain why some rows may be locked.
    bool uses_boxes = false;     // SET_BOX_TOOLTIP / BOXn_ACTIVATED (faction choice)
    bool uses_game_rule = false; // SET_GAME_RULE
    bool has_lua_companion = false;
    SetupLuaUse lua; // filled when the Lua text was supplied

    // True if the field is runtime-controlled. `player` -1 asks "for anyone";
    // a lock recorded with player -1 covers every player; an empty `item`
    // matches any item (per-player fields such as money).
    bool is_locked(int64_t field, int64_t player, const std::string &item = std::string()) const;
};

// `players`: the level's keeper count (.lof PLAYERS), used to expand
// ALL_PLAYERS in the seed. `has_lua_companion`: a map*.lua also exists.
// `lua_text`: that Lua script's text, if available -- it is scanned for calls that change the tab's
// fields (analysis.lua); if it does, a Supported verdict becomes Partial and the reason says so.
SetupAnalysis script_setup_analyse(const std::string &text, int64_t players, bool has_lua_companion = false,
    const std::string *lua_text = nullptr);

// The script with every owned line blanked (line count and all other lines,
// including their line endings, are preserved byte-for-byte). Win/lose
// blocks are only blanked when `mask_win_lose` is true (Replace mode).
std::string script_setup_mask(const std::string &text, const SetupAnalysis &analysis, bool mask_win_lose);

// The win/lose variable names the reader/tab model, per docs/refactor/
// skirmish/01-...md §13: [0] names identical in both versions (excluding
// TOTAL_CREATURES, whose meaning differs), [1] names existing only in v1.
// NULL-terminated. Cross-checked against the engine's tables by a
// kfx_editor test (kfx_game is above this library and cannot be included).
const char *const *script_setup_win_variables_identical(void);
const char *const *script_setup_win_variables_v1_only(void);

#endif
