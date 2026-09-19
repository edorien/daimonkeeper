/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_map_snapshot.h
 *     docs/refactor/editor/phase3/06-slice7-verify-map.md -- exposes
 *     editor_mapsave.cpp's own snapshot-building logic (private to that
 *     file's anonymous namespace) to editor_dialogs.cpp's "Verify Map"
 *     handler, which needs the same MapContent editor_save_map() would
 *     write without actually writing anything.
 * @par Comment:
 *     Internal to kfx_editor, like editor_toolbox.h/editor_dialogs.h --
 *     not part of kfx_editor.h's outside-caller surface. Plain C++ (not
 *     extern "C"-wrapped): this is same-library glue between two .cpp
 *     files, not a cross-library boundary, and MapContent (std::string/
 *     std::vector members) isn't a type an extern "C" signature should
 *     carry anyway.
 */
/******************************************************************************/
#ifndef DK_EDITOR_MAP_SNAPSHOT_H
#define DK_EDITOR_MAP_SNAPSHOT_H

#include "map_content.h"

// Builds a MapContent from the live session's current state, using the
// session's own current lvnum/name/players/is_multiplayer
// (editor_current_lvnum() etc.) -- exactly what a plain File > Save would
// write, without writing anything.
void editor_snapshot_current_map(MapContent &content);

#endif
