/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_script.h
 *     Header file for editor_script.cpp.
 * @par Purpose:
 *     docs/refactor/editor/05-script-and-level-settings.md §4.1 -- the
 *     in-game level editor's script text editor (Editor menu > Script >
 *     Edit Script...). Internal to kfx_editor (not part of kfx_editor.h's
 *     public surface); its own file for the same reason editor_toolbox.cpp/
 *     editor_overlay.cpp are their own files rather than growing
 *     editor_dialogs.cpp into a catch-all.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_EDITOR_SCRIPT_H
#define DK_EDITOR_SCRIPT_H

#include "bflib_basics.h" // TbBool

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

// Draws the script editor window while it's open. Called every frame from
// editor_frame() while editor_is_active(); does not check that itself.
void editor_script_frame(void);

// Loads the session's current script text (editor_current_level_
// script_text(), kfx_editor.h) into the editor widget and opens the window.
void editor_dialogs_open_script(void);

// Script > Commands window (phase5/08-script-command-browser.md). True while
// the script editor window is open (i.e. there is a live cursor).
TbBool editor_script_is_open(void);

// Puts a command line on the cursor's line if it is blank, else on a new
// line below it (indentation follows the neighbouring IF/ENDIF structure;
// never inside the managed setup region). Open editor: edits the buffer
// (Apply keeps it), returns true. Closed: appends to the session script
// text and marks it dirty, returns false.
TbBool editor_script_insert_command_at_cursor(const char *command);

// Replaces the editor's selection (or inserts at its cursor) with `token`.
// Only meaningful while the editor is open; returns false and does nothing
// otherwise.
TbBool editor_script_insert_token_at_cursor(const char *token);

/******************************************************************************/
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
// docs/refactor/editor/05-script-and-level-settings.md §4.4 -- inserts one
// line into the user part of the script (never inside the managed setup
// region). If the script editor window is open the line goes in at its
// cursor and the caller must tell the user to Apply (returns true); if it
// is closed the session's script text is appended to and marked dirty
// directly (returns false).
bool editor_script_insert_block_at_cursor(const char *block);

// Script > Word Wrap: wraps long lines in every tab of the script editor
// (both script formats, and read-only modules).
// Inserts `text` at the Lua tab's cursor (replacing its selection) and shows
// that tab. False, and nothing happens, unless the script editor is open and
// the level has a Lua script.
bool editor_lua_insert_at_cursor(const char *text);

// One caption line, drawn only when the level has a Lua script: the helpers
// (setup block, availability, messages) write classic commands to the .txt,
// which run before Lua's OnGameStart and so can be overridden by it.
void editor_lua_override_banner();

bool editor_script_word_wrap();
void editor_script_set_word_wrap(bool on);

#endif

#endif
