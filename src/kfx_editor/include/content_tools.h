/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_tools.h
 *     docs/refactor/editor/fx-plans/03-content-editors-foundation.md §4/§5 -- the host of the
 *     content editors: one entry point per tool, opened either from the main menu's Tools modal
 *     (standalone, with a target picker) or from the map editor's Tools menu (level target =
 *     the map being edited). Built so far: Config Files (the raw editor, F3).
 */
#ifndef DK_CONTENT_TOOLS_H
#define DK_CONTENT_TOOLS_H

#include "bflib_basics.h"
#include "editor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** True if `tool` (a ContentTool value) is built. */
TbBool content_tools_is_available(int tool);
/** Opens a tool as a standalone window with its own target picker (main menu). */
void content_tools_open(int tool);
/** Opens a tool on the map being edited: level layer preselected (map editor's Tools menu). */
void content_tools_open_for_map(int tool);
/* Opens the tool; for the Config Files editor, on this file ("rules.cfg", "creatrs/imp.cfg"; null: its default). */
void content_tools_open_file(int tool, const char *file);
/** Draws the open tool windows. Called every frame by the main menu and by the map editor. */
void content_tools_frame(void);
TbBool content_tools_is_open(void);

#ifdef __cplusplus
}
#endif
#endif
