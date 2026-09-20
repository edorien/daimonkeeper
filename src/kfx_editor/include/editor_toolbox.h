/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_toolbox.h
 *     Header file for editor_toolbox.cpp.
 * @par Purpose:
 *     docs/refactor/editor/02-editing-toolbox.md -- the in-game level
 *     editor's tool palette. Internal to kfx_editor (not part of
 *     kfx_editor.h's public surface); split into its own file/header
 *     purely so editor_session.cpp doesn't grow into a catch-all.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_EDITOR_TOOLBOX_H
#define DK_EDITOR_TOOLBOX_H

#include "bflib_basics.h" // TbBool

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

// Submits the tool-strip/picker/bottom-bar panel. Called every frame from
// editor_frame() while editor_is_active(); does not check that itself.
void editor_toolbox_frame(void);

// The toolbox window can be closed (its title-bar X) and re-opened from the
// View menu. Closing only hides the panel; the active tool stays active.
TbBool editor_toolbox_is_open(void);
void editor_toolbox_set_open(TbBool open);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
