/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_script_names.h
 *     Header file for editor_script_names.cpp.
 * @par Purpose:
 *     The Editor's "variable groups" (QuickCard / manual §5.2.1) read from
 *     the engine's live name tables: players, variables, flags and timers,
 *     comparisons, hero / evil creatures, rooms, doors, traps, spells and
 *     powers, and the action points on the current level. Shared by the
 *     Script > Commands window (click-to-insert values) and the script
 *     editor's syntax colouring (which names count as known values).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_EDITOR_SCRIPT_NAMES_H
#define DK_EDITOR_SCRIPT_NAMES_H

#include <string>
#include <vector>

struct ScriptNameGroup
{
    std::string title;
    std::vector<std::string> names;
    bool per_level = false; // depends on the current level (action point numbers)
};

std::vector<ScriptNameGroup> editor_script_collect_name_groups();

#endif
