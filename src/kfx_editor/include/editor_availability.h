/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_availability.h
 *     Header file for editor_availability.cpp.
 * @par Purpose:
 *     docs/refactor/editor/05-script-and-level-settings.md §4.3 -- the
 *     Script > Availability window: a creature/room/spell/door/trap x
 *     player grid that edits the *_AVAILABLE lines of the managed setup
 *     region. Internal to kfx_editor, same role as editor_script.h.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_EDITOR_AVAILABILITY_H
#define DK_EDITOR_AVAILABILITY_H

#ifdef __cplusplus
extern "C" {
#endif

// Draws the availability window while open. Called every frame from
// editor_frame() while editor_is_active().
void editor_availability_frame(void);

// Loads the grid from the session's current script's managed region and
// opens the window.
void editor_dialogs_open_availability(void);

#ifdef __cplusplus
}
#endif
#endif
