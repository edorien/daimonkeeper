/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_script_commands.h
 *     Header file for editor_script_commands.cpp.
 * @par Purpose:
 *     docs/refactor/editor/05-script-and-level-settings.md §4 -- the pure
 *     data/text logic behind the Script > Commands window: which group a
 *     script command belongs to, its one-line summary, its signature and an
 *     insertable template built from the engine's argument string, and how
 *     to splice a command line into a script at the cursor. No engine or
 *     ImGui state, so it is unit-testable.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_EDITOR_SCRIPT_COMMANDS_H
#define DK_EDITOR_SCRIPT_COMMANDS_H

#include <stdint.h>
#include <cstddef>
#include <string>

// Groups follow chapter 5.2 of the original 1993 Editor manual (Setup;
// Creatures/Spells/Traps/Doors; Manipulating Creatures; Research; Adding
// Creatures and Parties; Custom Objectives), with the manual's
// "Miscellaneous" split into flow control vs flags/timers, and KeeperFX's
// additions filed under the theme they belong to.
enum ScriptCommandGroup
{
    ScrGroup_Flow = 0,     // IF/ENDIF, WIN_GAME, ...
    ScrGroup_Flags,        // flags, timers, on-screen variables
    ScrGroup_Setup,        // generate speed, money, max creatures, allies, level/game rules
    ScrGroup_Computer,     // computer player set-up
    ScrGroup_Availability, // creature pool, *_AVAILABLE
    ScrGroup_Research,     // RESEARCH / RESEARCH_ORDER
    ScrGroup_Creatures,    // manipulating creatures
    ScrGroup_Spawn,        // adding creatures, parties, objects, doors, traps, effects
    ScrGroup_Objectives,   // objectives, information, messages
    ScrGroup_MapPowers,    // map reveal/tag, slabs, powers, specials
    ScrGroup_Config,       // KeeperFX configuration commands
    ScrGroup_Other,        // anything not classified
    ScrGroup_Count
};

const char *editor_script_group_title(int64_t group);

// Group of a command by (upper-case) name; ScrGroup_Other if unknown.
int64_t editor_script_command_group(const std::string &name);

// True for commands described in the original Editor manual (the ones a
// classic script uses); false for KeeperFX additions.
bool editor_script_command_is_classic(const std::string &name);

// Commands that steer the script rather than change the level: IF*,
// ENDIF, WIN_GAME, LOSE_GAME, NEXT_COMMAND_REUSABLE (and the manual's
// spelling of it). The script editor colours these as keywords.
bool editor_script_command_is_flow(const std::string &name);

// One-line description for commands the original manual documents; empty
// for the rest (the signature is all there is).
const char *editor_script_command_summary(const std::string &name);

// Human-readable signature from the engine's argument string
// (A string, N number, C creature, P player, R room, L location,
// O comparison, S slab, B 0/1; lower case = optional; '!' = extended
// values allowed; '+' = repeatable), e.g. "START_MONEY(player, number)"
// or "QUICK_OBJECTIVE(number, text, [location], [icon])".
std::string editor_script_command_signature(const std::string &name, const std::string &args);

// Insertable line: required arguments only, each replaced by a
// recognisable placeholder (PLAYER0, 0, CREATURE, ROOM, ...), e.g.
// "START_MONEY(PLAYER0,0)". Commands with no arguments have no parentheses.
std::string editor_script_command_template(const std::string &name, const std::string &args);

struct ScriptInsertResult
{
    std::string text; // the whole script after insertion
    size_t line;      // 0-based line the command now occupies
};

// Puts `command` in the script relative to 0-based `cursor_line`: on that
// line if it is blank, otherwise on a new line below it. The new line takes
// the reference line's indentation (one level deeper under an IF...,
// one level shallower for ENDIF). A cursor inside the managed setup region
// is moved to just after the region. Keeps the script's own line ending.
ScriptInsertResult editor_script_insert_command(const std::string &script_text, size_t cursor_line, const std::string &command);

#endif
