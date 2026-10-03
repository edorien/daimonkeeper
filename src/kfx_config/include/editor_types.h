/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_types.h
 *     Plain types and labels shared by kfx_editor and the lower libraries
 *     that talk to it through ports/editor_port.h.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_EDITOR_TYPES_H
#define DK_EDITOR_TYPES_H

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

// docs/refactor/editor/09-toolbox-remainder.md §1 -- one slab's pre-mutation
// kind+owner, captured by record_rect_terrain()'s caller before applying a
// rect-terrain op. Plain C POD (shared verbatim by kfx_net's capture side
// and kfx_editor's std::vector-backed journal storage).
struct EditorRectSlabSnapshot {
    unsigned char kind;
    unsigned char owner;
};
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
