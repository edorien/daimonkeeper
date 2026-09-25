/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_script_message.h
 *     Header file for editor_script_message.cpp.
 * @par Purpose:
 *     docs/refactor/editor/05-script-and-level-settings.md §4.4 -- plain-string
 *     helpers behind the Script > Objective / Message window: formatting a
 *     QUICK_OBJECTIVE / QUICK_INFORMATION line, picking a free message
 *     number, and splicing a block into the script at a line without ever
 *     landing inside the editor-managed setup region. No session or ImGui
 *     state, so all of it is unit-testable.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_EDITOR_SCRIPT_MESSAGE_H
#define DK_EDITOR_SCRIPT_MESSAGE_H

#include <stdint.h>
#include <cstddef>
#include <string>

enum ScriptMessageKind
{
    MsgKind_Objective = 0, // QUICK_OBJECTIVE
    MsgKind_Information,   // QUICK_INFORMATION
    MsgKind_Count
};

// Engine limits (kfx_sim_state.h QUICK_MESSAGES_COUNT / MESSAGE_TEXT_LEN,
// mirrored: kfx_editor may include them but the tests don't need config).
const int64_t kScriptMessageCount = 256;
const size_t kScriptMessageMaxChars = 1023;

const char *editor_script_message_command(int64_t kind);

// One script line, e.g. QUICK_OBJECTIVE(12,"Build a lair.",PLAYER0). The
// script tokenizer has no escape for a double quote inside a string, so
// quotes become apostrophes; line breaks become spaces; text is truncated to
// kScriptMessageMaxChars. `location` is optional (omitted when empty or not
// a plain identifier).
std::string editor_script_format_message(int64_t kind, int64_t number, const std::string &text, const std::string &location);

// Whether QUICK_OBJECTIVE/QUICK_INFORMATION (or their _WITH_POS forms)
// already uses `number` somewhere in the script. Both commands share one
// table of kScriptMessageCount slots, so numbers are unique across them.
bool editor_script_message_number_used(const std::string &script_text, int64_t number);

// Smallest unused message number, or -1 if all are taken.
int64_t editor_script_next_message_number(const std::string &script_text);

// 0-based line to insert at, moved past the end of the managed setup region
// if `line` falls inside it (the region is regenerated on Apply, so anything
// put there would be lost).
size_t editor_script_safe_insert_line(const std::string &script_text, size_t line);

// Inserts `block` (without trailing newline) as its own line before 0-based
// line `line` (clamped to the end), after moving it out of the managed
// region. Uses the script's own line ending (CRLF if it has any).
std::string editor_script_insert_block(const std::string &script_text, size_t line, const std::string &block);

#endif
