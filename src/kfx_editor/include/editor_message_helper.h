/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_message_helper.h
 *     Header file for editor_message_helper.cpp.
 * @par Purpose:
 *     docs/refactor/editor/05-script-and-level-settings.md §4.4 -- the
 *     Script > Objective / Message window: a small form that writes a
 *     QUICK_OBJECTIVE / QUICK_INFORMATION line into the script. Internal to
 *     kfx_editor, same role as editor_availability.h.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_EDITOR_MESSAGE_HELPER_H
#define DK_EDITOR_MESSAGE_HELPER_H

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

// Draws the window while it's open. Called every frame from editor_frame()
// while editor_is_active(); does not check that itself.
void editor_message_helper_frame(void);

// Opens the window with the next unused message number pre-filled.
void editor_dialogs_open_message_helper(void);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
