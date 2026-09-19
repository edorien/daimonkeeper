/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_callbacks.c
 *     Callback-registration implementation. See editor_callbacks.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "editor_callbacks.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
static void noop_request_open(LevelNumber lvnum, TbBool is_new) {}
static TbBool noop_is_active(void) { return false; }

static const struct EditorCallbacks default_editor_callbacks = {
    &noop_request_open,
    &noop_is_active,
};
const struct EditorCallbacks *editor_callbacks = &default_editor_callbacks;

void set_editor_callbacks(const struct EditorCallbacks *callbacks)
{
    editor_callbacks = callbacks ? callbacks : &default_editor_callbacks;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
