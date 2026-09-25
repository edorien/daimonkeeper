/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file script_setup.h
 *     Header file for script_setup.cpp.
 * @par Purpose:
 *     docs/refactor/editor/05-script-and-level-settings.md §4.2 -- the
 *     "managed setup region": a delimited block within a level's own
 *     script text that the Level Settings dialog generates/parses
 *     (generation speed, start gold, max creatures, and the creature
 *     pool), leaving everything else in the script -- the mapmaker's own
 *     hand-written content -- untouched.
 *     Lives in kfx_config (moved from kfx_editor, docs/refactor/skirmish/)
 *     because it is pure text <-> struct logic over kfx_config name tables
 *     and is shared by the editor and the Skirmish setup tab (kfx_frontend,
 *     ranked below kfx_editor). It must not include anything above
 *     kfx_config; version-dependent script tables that live in kfx_game are
 *     mirrored here as constants and cross-checked by tests.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_SCRIPT_SETUP_H
#define DK_SCRIPT_SETUP_H

#include "globals.h" // ThingModel
#include "bflib_basics.h" // struct NamedCommand


#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// docs/refactor/editor/phase5/05-slice5-availability-grid.md -- one
// *_AVAILABLE line's worth of data. `player` is -1 for ALL_PLAYERS, else
// the PLAYERn index. (a, b) are the command's raw trailing integer pair,
// kept verbatim (not collapsed to a 3-state enum) so cells the grid never
// touched round-trip losslessly -- e.g. CREATURE_AVAILABLE's force count
// or TRAP_AVAILABLE's stock amount, which the grid has no UI for.
enum AvailabilityKind
{
    AvailKind_Creature = 0,
    AvailKind_Room,
    AvailKind_Magic,
    AvailKind_Trap,
    AvailKind_Door,
    AvailKind_Count
};

struct AvailabilityEntry
{
    int64_t kind;
    int64_t player;
    int64_t item; // ThingModel / RoomKind / PowerKind / trap / door model number
    int64_t a;
    int64_t b;
};

// One condition of a win/lose rule: IF(PLAYERn, VARIABLE op value).
struct WinLoseClause
{
    int64_t player = 0;
    std::string variable = "ALL_DUNGEONS_DESTROYED";
    std::string op = "==";
    int64_t value = 1;
};

// WIN_GAME or LOSE_GAME once every clause holds. Several clauses are written
// as nested IF blocks (all must hold).
struct WinLoseRule
{
    bool win = true;
    std::vector<WinLoseClause> clauses;
};

// The managed-region commands' resolved values. start_money/
// max_creatures are always sized to the level's own player count (index i
// == PLAYERi); creature_pool is a flat list since ADD_CREATURE_TO_POOL has
// no per-player concept at all (docs/refactor/editor/05-script-and-level-
// settings.md's own round-4 findings on this: the pool is one shared
// bucket per creature kind for the whole level).
struct ManagedSetupValues
{
    int64_t generate_speed;
    std::vector<int64_t> start_money;
    std::vector<int64_t> max_creatures;
    std::vector<std::pair<ThingModel, int64_t>> creature_pool;
    std::vector<AvailabilityEntry> availability;
    // Win/lose rules; parsed and written as plain IF/WIN_GAME/LOSE_GAME/ENDIF
    // blocks after the availability lines.
    std::vector<WinLoseRule> rules;
};

// Script command name / NamedCommand table for one availability kind, and
// the item-name lookups the grid UI needs (tables are NULL-terminated,
// dynamically filled from config at load time).
const char *script_setup_availability_command_name(int64_t kind);
const struct NamedCommand *script_setup_availability_desc(int64_t kind);
const char *script_setup_availability_item_name(int64_t kind, int64_t item);

// Finds the entry for exactly (kind, player, item), or nullptr.
AvailabilityEntry *script_setup_availability_find(ManagedSetupValues &values, int64_t kind, int64_t player, int64_t item);

// Returns the managed region's own body text (between, not including, the
// marker lines) -- empty if `script_text` has no markers at all (a level
// that's never had this dialog's Apply pressed yet).
std::string script_setup_extract_region(const std::string &script_text);

// Replaces the managed region's body with `new_body`, leaving every other
// line of `script_text` untouched -- if no markers exist yet, inserts a
// fresh block (markers + body) at the very start of the script, ahead of
// everything else (including any hand-written REM header a real shipped
// level's script commonly opens with).
std::string script_setup_replace_region(const std::string &script_text, const std::string &new_body);

// Parses SET_GENERATE_SPEED/START_MONEY/MAX_CREATURES/ADD_CREATURE_TO_POOL
// lines out of a managed-region body (as returned by extract, above) --
// any other line (blank, REM comment, something the mapmaker somehow got
// into this block by hand) is silently skipped, not an error. start_money/
// max_creatures come back sized to `players`, 0 for any player with no
// line found (a level that's never set one).
//
// `level_version` is the script's LEVEL_VERSION (script_setup_level_version):
// at 0 (no LEVEL_VERSION line) CREATURE_AVAILABLE's 4th argument is
// "available" and the 3rd is ignored, so it is read into `a` (force is not
// expressible at v0 and comes back 0). Other commands are identical.
int64_t script_setup_level_version(const std::string &script_text);
ManagedSetupValues script_setup_parse(const std::string &managed_body, int64_t players, int64_t level_version = 1);

// The inverse of parse: generates the managed region's own body text
// (without the marker lines -- script_setup_replace_region()
// adds those) for the given values/players, one line per command instance
// -- SET_GENERATE_SPEED once, then one START_MONEY/MAX_CREATURES line per
// player 0..players-1, then one ADD_CREATURE_TO_POOL line per pool entry
// -- matching the ordering real shipped scripts already use.
// Hand-written WIN_GAME / LOSE_GAME lines *outside* the managed block, reported
// only when the block itself already defines a rule with that result (the
// level would then have two). `line` is 0-based in `script_text`. The editor
// wraps these as ScriptIssue warnings (editor_script_check_duplicate_win_lose).
struct DuplicateWinLose
{
    size_t line;
    bool win; // false: LOSE_GAME
};
std::vector<DuplicateWinLose> script_setup_find_duplicate_win_lose(const std::string &script_text);

// Comparison operators the rules use, in the order the UI offers them.
const char *const *script_setup_win_lose_operators(int64_t *count);

// At level_version 0, creatures are written in the v0 form
// CREATURE_AVAILABLE(p,c,a,a); the force flag cannot be expressed there.
std::string script_setup_generate(const ManagedSetupValues &values, int64_t players, int64_t level_version = 1);

// Small text helpers shared with script_setup_analysis.cpp.
std::string script_setup_trim(const std::string &s);
// "COMMAND(a, b)" -> {"a","b"} (trimmed); empty if there is no "(...)" list.
std::vector<std::string> script_setup_split_args(const std::string &line);
// "IF(PLAYERn, VAR op N)" -> clause; false for any other argument shape.
bool script_setup_parse_if_clause(const std::string &line, WinLoseClause &out);

#endif
