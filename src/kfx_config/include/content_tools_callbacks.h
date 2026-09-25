/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_tools_callbacks.h
 *     Header file for content_tools_callbacks.c.
 * @par Purpose:
 *     docs/refactor/editor/fx-plans/03-content-editors-foundation.md §5 -- the content
 *     editors (raw config editor, rules, creatures, traps, ...) live in kfx_editor, which is ranked
 *     above kfx_frontend; the main menu's Tools modal reaches them through this callback struct
 *     (declared here, implemented in kfx_editor, wired in main.cpp::setup_game()), the same
 *     pattern as editor_callbacks.h.
 * @par Comment:
 *     Until wired, every callback is a no-op and no tool is available.
 */
/******************************************************************************/
#ifndef DK_CONTENT_TOOLS_CALLBACKS_H
#define DK_CONTENT_TOOLS_CALLBACKS_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

/** The tools the Tools menus list, in menu order. The label and availability drive both menus. */
enum ContentTool {
    ContentTool_ConfigFiles = 0, // raw config editor
    ContentTool_Rules,
    ContentTool_Creature,
    ContentTool_TrapDoor,
    ContentTool_SpellAbility,
    ContentTool_Room,
    ContentTool_Campaign,
    ContentTool_Text,
    ContentTool_Count
};

/** Menu label of a tool ("Config Files", "Rules Editor", ...). */
const char *content_tool_label(int tool);

struct ContentToolsCallbacks {
    /* True if the tool is built; the menus grey the others out as "coming soon". */
    TbBool (*is_available)(int tool);
    /* Opens the tool as a window of its own (main menu: no map, target picker inside). */
    void (*open)(int tool);
    /* Draws the open tool windows; called every frame by the host screen (main menu). */
    void (*frame)(void);
    /* True while any tool window is open. */
    TbBool (*is_open)(void);
};
void set_content_tools_callbacks(const struct ContentToolsCallbacks *callbacks);
extern const struct ContentToolsCallbacks *content_tools_callbacks;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
