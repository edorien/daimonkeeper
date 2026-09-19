/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_command_browser.h
 *     Header file for editor_command_browser.cpp.
 * @par Purpose:
 *     docs/refactor/editor/phase5/08-script-command-browser.md -- the
 *     Script > Commands window: browse every script command grouped by
 *     purpose and insert one at the script editor's cursor, plus the
 *     Editor's "variable groups" (players, variables, comparisons, creature/
 *     room/trap/door/spell names) as click-to-insert values. Internal to
 *     kfx_editor, same role as editor_availability.h.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_EDITOR_COMMAND_BROWSER_H
#define DK_EDITOR_COMMAND_BROWSER_H

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

// Draws the window while it's open. Called every frame from editor_frame()
// while editor_is_active(); does not check that itself.
void editor_command_browser_frame(void);

// Rebuilds the catalogue from the engine's current tables and opens the window.
void editor_dialogs_open_command_browser(void);

// Test seams: how many commands the catalogue holds, and the names of those
// that fell into the "Other" group (copied into `out`, comma-separated,
// truncated to `out_size`). Both rebuild the catalogue.
int editor_command_browser_command_count(void);
int editor_command_browser_unclassified(char *out, int out_size);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
