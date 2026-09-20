/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_script_validate.h
 *     Header file for editor_script_validate.cpp.
 * @par Purpose:
 *     The script editor's Validate button: a lexical check of a classic
 *     (.txt) script -- unknown commands, argument counts, IF/ENDIF balance.
 *     Pure text logic: the command table is passed in as a lookup, so it is
 *     unit-testable and always agrees with whatever the engine defines.
 *     docs/refactor/editor/fx-plans/00-audit-and-index.md item A3.
 */
#ifndef DK_EDITOR_SCRIPT_VALIDATE_H
#define DK_EDITOR_SCRIPT_VALIDATE_H

#include <functional>
#include <string>
#include <vector>

enum ScriptIssueSeverity { ScrIssue_Warning = 0, ScrIssue_Error };

struct ScriptIssue
{
    size_t line = 0; // 0-based
    ScriptIssueSeverity severity = ScrIssue_Error;
    std::string message;
};

// Returns the engine's argument string for an (upper-case) command name, or
// nullptr when there is no such command.
typedef std::function<const char *(const std::string &)> ScriptCommandLookup;

// True if the command opens an IF block (IF, IF_AVAILABLE, IF_ACTION_POINT, ...).
bool editor_script_command_opens_block(const std::string &name);

// Issues sorted by line.
std::vector<ScriptIssue> editor_script_validate(const std::string &text, const ScriptCommandLookup &lookup);

// The same check against the engine's own command table (command_desc[]).
std::vector<ScriptIssue> editor_script_validate_engine(const std::string &text);

// Warnings for WIN_GAME / LOSE_GAME written by hand outside the managed setup
// block when the block itself already defines a rule with that result.
std::vector<ScriptIssue> editor_script_check_duplicate_win_lose(const std::string &script_text);

#endif
