/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_script_managed.h
 *     Header file for editor_script_managed.cpp.
 * @par Purpose:
 *     docs/refactor/editor/05-script-and-level-settings.md §4.2 -- the
 *     "managed setup region": a delimited block within a level's own
 *     script text that the Level Settings dialog generates/parses
 *     (generation speed, start gold, max creatures, and the creature
 *     pool), leaving everything else in the script -- the mapmaker's own
 *     hand-written content -- untouched. Internal to kfx_editor (not part
 *     of kfx_editor.h's public surface), same role editor_journal.h/
 *     editor_map_snapshot.h already play for other same-library glue.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_EDITOR_SCRIPT_MANAGED_H
#define DK_EDITOR_SCRIPT_MANAGED_H

#include "globals.h" // ThingModel
#include "bflib_basics.h" // struct NamedCommand

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
    int kind;
    int player;
    int item; // ThingModel / RoomKind / PowerKind / trap / door model number
    int a;
    int b;
};

// The managed-region commands' resolved values. start_money/
// max_creatures are always sized to the level's own player count (index i
// == PLAYERi); creature_pool is a flat list since ADD_CREATURE_TO_POOL has
// no per-player concept at all (docs/refactor/editor/05-script-and-level-
// settings.md's own round-4 findings on this: the pool is one shared
// bucket per creature kind for the whole level).
struct ManagedSetupValues
{
    int generate_speed;
    std::vector<int> start_money;
    std::vector<int> max_creatures;
    std::vector<std::pair<ThingModel, int>> creature_pool;
    std::vector<AvailabilityEntry> availability;
};

// Script command name / NamedCommand table for one availability kind, and
// the item-name lookups the grid UI needs (tables are NULL-terminated,
// dynamically filled from config at load time).
const char *editor_availability_command_name(int kind);
const struct NamedCommand *editor_availability_desc(int kind);
const char *editor_availability_item_name(int kind, int item);

// Finds the entry for exactly (kind, player, item), or nullptr.
AvailabilityEntry *editor_availability_find(ManagedSetupValues &values, int kind, int player, int item);

// Returns the managed region's own body text (between, not including, the
// marker lines) -- empty if `script_text` has no markers at all (a level
// that's never had this dialog's Apply pressed yet).
std::string editor_script_extract_managed_region(const std::string &script_text);

// Replaces the managed region's body with `new_body`, leaving every other
// line of `script_text` untouched -- if no markers exist yet, inserts a
// fresh block (markers + body) at the very start of the script, ahead of
// everything else (including any hand-written REM header a real shipped
// level's script commonly opens with).
std::string editor_script_replace_managed_region(const std::string &script_text, const std::string &new_body);

// Parses SET_GENERATE_SPEED/START_MONEY/MAX_CREATURES/ADD_CREATURE_TO_POOL
// lines out of a managed-region body (as returned by extract, above) --
// any other line (blank, REM comment, something the mapmaker somehow got
// into this block by hand) is silently skipped, not an error. start_money/
// max_creatures come back sized to `players`, 0 for any player with no
// line found (a level that's never set one).
ManagedSetupValues editor_script_parse_managed_setup(const std::string &managed_body, int players);

// The inverse of parse: generates the managed region's own body text
// (without the marker lines -- editor_script_replace_managed_region()
// adds those) for the given values/players, one line per command instance
// -- SET_GENERATE_SPEED once, then one START_MONEY/MAX_CREATURES line per
// player 0..players-1, then one ADD_CREATURE_TO_POOL line per pool entry
// -- matching the ordering real shipped scripts already use.
std::string editor_script_generate_managed_setup(const ManagedSetupValues &values, int players);

#endif
