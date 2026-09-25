/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_dialogs.h
 *     docs/refactor/editor/phase3/02-slice3-dialogs-menubar.md -- New Map/
 *     Open Map/Save As dialogs plus the unsaved-changes confirm, opened
 *     from the menu bar (editor_menubar.cpp). Drawn every frame from
 *     editor_frame() via editor_dialogs_frame(), same as
 *     editor_toolbox_frame()/editor_journal_frame() are already.
 * @par Comment:
 *     Internal to kfx_editor, like editor_toolbox.h -- not part of
 *     kfx_editor.h's outside-caller surface.
 */
/******************************************************************************/
#ifndef DK_EDITOR_DIALOGS_H
#define DK_EDITOR_DIALOGS_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

void editor_dialogs_open_new_map(void);
void editor_dialogs_open_open_map(void);
void editor_dialogs_open_save_as(void);
// File > Save: direct action, no dialog -- saves to the session's own
// current lvnum/dir with EdSaveFmt_Auto.
void editor_dialogs_save_now(void);
// File > Exit to Main Menu: dirty-gated editor_close().
void editor_dialogs_request_exit(void);
// docs/refactor/editor/phase3/04-slice5-playtest-settings-overwrite.md --
// Level Settings (name/players/is_multiplayer, .lof only) and Playtest
// (save to a scratch slot + launch), both independent of New/Save.
void editor_dialogs_open_level_settings(void);
void editor_dialogs_open_playtest_confirm(void);
// docs/refactor/editor/phase3/06-slice7-verify-map.md -- snapshots the
// current session and runs verify_map_content() (kfx_sim) against it,
// then shows the resulting issue list. Purely informational.
void editor_dialogs_open_verify_map(void);
void editor_dialogs_frame(void);
/** The campaign or pack (its .cfg file name; `kind` a ContentKind) the next Save As of an untitled map offers first; "" for Editor Maps. Set by the
 *  Campaign editor's "New map in this campaign"; used once. */
void editor_dialogs_set_default_campaign(const char *campaign_fname, int kind); /* kind: a ContentKind (0 campaign, 1 free-play pack, 2 multiplayer pack) */

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
