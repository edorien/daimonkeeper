/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_callbacks.h
 *     Header file for editor_callbacks.c.
 * @par Purpose:
 *     Callback-registration interface letting kfx_apploop call into
 *     kfx_editor (editor_open()) without depending on kfx_editor.h
 *     directly -- kfx_editor is ranked above kfx_apploop
 *     (docs/refactor/editor/00-overview.md §5.1), so this is the one
 *     inbound edge the rest of the engine needs, mirroring every other
 *     lower-layer-calls-higher-layer case in this refactor (see
 *     net_callbacks.h/game_callbacks.h for the established pattern).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_EDITOR_CALLBACKS_H
#define DK_EDITOR_CALLBACKS_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

struct EditorCallbacks {
    /* kfx_editor.h -- called from kfx_apploop's `case FeSt_START_EDITOR:`
       (game_session_loop.cpp) once startup_local_game_for_editor()'s
       coroutine has finished loading the sim: the hand-off point from
       "sim is loaded and paused" to "the editor session is active".
       new_map/is_new mirror what the FeSt_EDITOR browser (kfx_frontend)
       stashed before requesting FeSt_START_EDITOR. */
    void (*request_open)(LevelNumber lvnum, TbBool is_new);
    /* docs/refactor/editor/10-definable-keybindings.md -- lets kfx_frontend's
       front_input.c ask "is an editor session currently active" so it can
       read camera/console keys from settings.editor_kbkeys[]/
       EditorGameKeys instead of settings.kbkeys[]/GameKeys, without
       depending on kfx_editor.h directly (kfx_editor is ranked above
       kfx_frontend too). Mirrors kfx_editor.h's own editor_is_active() --
       same no-op-until-wired convention as every other callback here
       (false, not a NULL-deref, until setup_game() wires the real one). */
    TbBool (*is_active)(void);
};
void set_editor_callbacks(const struct EditorCallbacks *callbacks);
extern const struct EditorCallbacks *editor_callbacks;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
