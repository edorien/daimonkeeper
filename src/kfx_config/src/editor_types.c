/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_types.c
 *     The content tools' menu labels (editor_types.h).
 */
/******************************************************************************/
#include "pre_inc.h"
#include "editor_types.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
static const char *const tool_labels[ContentTool_Count] = {
    "Config Files",
    "Rules Editor",
    "Creature Editor",
    "Trap and Door Editor",
    "Spell and Ability Editor",
    "Room Editor",
    "Campaign Editor",
    "Text Editor",
};

const char *content_tool_label(int tool)
{
    if (tool < 0 || tool >= ContentTool_Count)
        return "";
    return tool_labels[tool];
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
