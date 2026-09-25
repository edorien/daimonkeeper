/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_tools_callbacks.c
 *     Callback-registration implementation. See content_tools_callbacks.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "content_tools_callbacks.h"
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

static TbBool noop_is_available(int tool) { return false; }
static void noop_open(int tool) {}
static void noop_frame(void) {}
static TbBool noop_is_open(void) { return false; }

static const struct ContentToolsCallbacks default_content_tools_callbacks = {
    &noop_is_available,
    &noop_open,
    &noop_frame,
    &noop_is_open,
};
const struct ContentToolsCallbacks *content_tools_callbacks = &default_content_tools_callbacks;

void set_content_tools_callbacks(const struct ContentToolsCallbacks *callbacks)
{
    content_tools_callbacks = callbacks ? callbacks : &default_content_tools_callbacks;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
