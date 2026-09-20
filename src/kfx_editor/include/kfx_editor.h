/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file kfx_editor.h
 *     Public interface of the in-game level editor library.
 * @par Purpose:
 *     docs/refactor/editor/00-overview.md / 01-entry-and-editor-session.md:
 *     kfx_editor is the highest-ranked internal library (immediately below
 *     app_entry, above kfx_apploop). It owns the editor session lifecycle,
 *     the in-session ImGui toolbox, the map serializer and the blank-map
 *     builder. main.cpp is the only #include edge into this library --
 *     everything below it reaches the editor only via EditorCallbacks
 *     (src/kfx_config/include/editor_callbacks.h).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_KFX_EDITOR_H
#define DK_KFX_EDITOR_H

#include "bflib_basics.h"
#include "globals.h"
#include "editor_journal_callbacks.h"
#include <stddef.h> // size_t -- editor_level_save_dir()

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

// Called once the sim is loaded and running for an editor session
// (kfx_apploop's `case FeSt_START_EDITOR:`, via EditorCallbacks::request_open
// -- kfx_apploop can't call this directly, it's ranked below kfx_editor).
// Marks the session active, reveals the whole map for the editor player and
// puts them in the god/build state (§3 -- no "enable cheats" step needed).
void editor_open(LevelNumber lvnum, TbBool is_new);

// Exit to Main Menu (§6): sends PckA_QuitToMainMenu, clears
// simulation_suspended and marks the session inactive.
void editor_close(void);

TbBool editor_is_active(void);

// ImGui submission + per-frame editor logic -- called every frame from
// main.cpp's ImGui-frame wrapper regardless of what's on screen; no-ops
// unless editor_is_active(). Re-asserts GOF_Paused every frame while
// suspended (§3) and draws the Esc editor menu / toolbox.
void editor_frame(void);

// Playtest (§6) hook: called when a playtest session ends (win/lose/quit),
// so "Return to editor" can reload the pre-playtest scratch-slot state.
void editor_notify_playtest_end(void);
/** Called just before a playtest launches: remembers which level is being edited so the return from the playtest re-opens it. */
void editor_playtest_begin(void);

// docs/refactor/editor/phase3/02-slice3-dialogs-menubar.md -- session state
// tracked since editor_open(), read by File > Save / the Save As dialog
// (editor_dialogs.cpp) and by editor_menubar.cpp's map-name/dirty display.
LevelNumber editor_current_lvnum(void);
const char *editor_current_save_dir(void);
// The level's display name (.lof's NAME_TEXT -- get_level_info.h.name),
// independent of the lvnum that names its files: best-effort read from
// get_level_info() when editor_open() runs (empty if this level has no
// .lof yet), kept up to date by a successful Save As
// (editor_set_current_level_name()) so a later plain File > Save doesn't
// blank it back out.
const char *editor_current_level_name(void);
// docs/refactor/editor/phase3/04-slice5-playtest-settings-overwrite.md --
// same "read-back on open, kept current by whoever last changed it"
// pattern as the name above -- players/is_multiplayer (.lof's PLAYERS/
// KIND) are set from the Level Settings dialog now, not tied to
// New Map/Save As at all.
int editor_current_level_players(void);
TbBool editor_current_level_is_multiplayer(void);
// docs/refactor/editor/05-script-and-level-settings.md §1 -- .lof's
// DESCRIPTION, same pattern as the name field above (the struct field
// already existed on LevelInformation, just wasn't read/written anywhere
// until this slice).
const char *editor_current_level_description(void);
const char *editor_current_level_author(void);
// docs/refactor/editor/05-script-and-level-settings.md §0 -- the level's
// own map%05lu.txt, read verbatim from disk in editor_open() (and editable
// via the §4.1 script text editor's own Apply action, see the setter
// below) so editor_save_map() can write it back rather than truncating it
// to an empty stub.
const char *editor_current_level_script_text(void);
// docs/refactor/editor/05-script-and-level-settings.md §4.1 -- the script
// text editor's own Apply action commits an edit here; the session stays
// dirty (editor_mark_dirty()) until the next real Save actually persists
// it, same as every other content edit (placing things, painting terrain)
// rather than Level Settings' own immediate-write-to-disk shape.
void editor_set_current_level_script_text(const char *script_text);
// docs/refactor/editor/fx-plans/02-lua-scripts.md L1 -- the level's own
// map%05lu.lua, read verbatim in editor_open() and written back by every
// save. has_lua distinguishes "no file" from "empty file"; setting text
// with has_lua true creates the file on the next save.
// New Map's "Lua script" option: the next editor_open(..., is_new) starts with
// the Lua template and no .txt. One-shot.
void editor_set_new_map_lua(TbBool on);
TbBool editor_current_level_has_lua(void);
const char *editor_current_level_lua_text(void);
void editor_set_current_level_lua_text(const char *lua_text, TbBool has_lua);
TbBool editor_is_dirty(void);

// §3's "Preview motion" affordance (editor_session.cpp's own s_preview_motion
// comment) -- moved from the Esc-equivalent hub into the new View menu
// (editor_menubar.cpp) this slice.
TbBool editor_preview_motion(void);

/** Level Settings > Resize. `dropped_*` (each may be NULL) receive how many things, lights and action points
 *  would be lost. editor_resize_map() writes the resized copy to a scratch level and reopens the editor on it,
 *  keeping this level's identity and marking it unsaved. False if the size is unsupported or the write failed. */
TbBool editor_resize_preview(long new_w, long new_h, TbBool centered, int *dropped_things, int *dropped_lights, int *dropped_points);
TbBool editor_resize_map(long new_w, long new_h, TbBool centered);
void editor_set_preview_motion(TbBool on);
/** True while turning Preview Motion off is waiting for a 1st Person view to end. */
TbBool editor_preview_restore_pending(void);

// Internal cross-file use only (same library, not part of this header's
// outside-caller surface) -- declared here so another file in this library
// can reach editor_session.cpp's state. editor_mark_dirty() is called from
// editor_journal.cpp's record_placement/record_rect_terrain on every
// forward edit; editor_clear_dirty() from editor_dialogs.cpp after a
// successful editor_save_map() call.
void editor_mark_dirty(void);
void editor_clear_dirty(void);

// Standard "Save As" semantics: once a save succeeds under a different
// lvnum/dir than the session opened with, that becomes the session's own
// identity (subsequent File > Save writes there too), the same way
// editor_open() itself sets these. Called from editor_dialogs.cpp's Save
// As handler on success.
void editor_set_current_lvnum_and_dir(LevelNumber lvnum, const char *dir);
// Companion setter for the level name -- separate from the lvnum/dir one
// above since Save As can change either independently (the user asked for
// exactly that: level number and level name set independently of each
// other, not tied together).
void editor_set_current_level_name(const char *name);
// Companion setters for players/is_multiplayer -- set from the Level
// Settings dialog (editor_dialogs.cpp), independent of lvnum/dir/name.
void editor_set_current_level_players(int players);
void editor_set_current_level_is_multiplayer(TbBool is_multiplayer);
// docs/refactor/editor/05-script-and-level-settings.md §1 -- same
// read-back-on-open/kept-current-by-whoever-last-changed-it pattern as
// name/players/multiplayer above.
void editor_set_current_level_description(const char *description);
void editor_set_current_level_author(const char *author);

// Pure path logic (no live session needed) -- given lvnum, fills `out` with
// the directory editor_save_map()'s own `dir` parameter expects for that
// level (mirrors get_level_fgroup()+prepare_file_fmtpath()'s own directory,
// stripped of the filename). Exported for a direct Catch2 unit test as well
// as real use from editor_open()/the Save As dialog.
void editor_level_save_dir(LevelNumber lvnum, char *out, size_t out_size);

// docs/refactor/editor/02-editing-toolbox.md §4 -- the one inbound edge for
// the undo/redo journal: wired via EditorJournalCallbacks
// (editor_journal_callbacks.h) in main.cpp's setup_game(), the same way
// EditorCallbacks::request_open above is. Called from packets_cheats.c
// right after any editor placement tool's create_*() call succeeds; no-ops
// unless editor_is_active() (checked internally, not by the caller).
// pcktype/par1-4/pos_x/pos_y are the packet fields that created this thing
// -- see EditorJournalCallbacks::record_placement's own comment for why
// Redo needs all of them, not just the thing index.
void editor_journal_record_placement(long thing_idx, unsigned char pcktype,
    unsigned long par1, unsigned long par2, unsigned short par3, unsigned short par4,
    long pos_x, long pos_y);

// docs/refactor/editor/09-toolbox-remainder.md §1 -- rect-terrain-op
// undo/redo counterpart to editor_journal_record_placement() above; see
// EditorJournalCallbacks::record_rect_terrain's own comment
// (editor_journal_callbacks.h) for the parameter shapes and why this is
// called BEFORE the mutation rather than after.
void editor_journal_record_rect_terrain(unsigned char pcktype,
    long box_beg_x, long box_beg_y, long box_end_x, long box_end_y,
    SlabKind new_kind, PlayerNumber new_owner,
    const struct EditorRectSlabSnapshot *before, long count);

// fx-plans/00 item A7 -- door-lock toggle counterpart (EditorJournalCallbacks::
// record_door_lock): called before the toggle, journals it for undo.
void editor_journal_record_door_lock(long thing_idx, TbBool was_locked);

// docs/refactor/editor/phase3/01-slice2-classic-save.md -- Auto picks
// ClassicMapContentWriter when map_is_legacy_compatible() (src/kfx_sim/
// include/map_content_compat.h) says so, else KfxNativeMapContentWriter;
// the Force* values bypass that check entirely (an explicit "I accept
// whatever this format can't represent" choice -- ClassicMapContentWriter
// silently drops what it can't carry rather than refusing to write, same
// as slice 1's own "attempt every file, don't stop at the first failure"
// philosophy).
enum EditorSaveFormat
{
    EdSaveFmt_Auto = 0,
    EdSaveFmt_ForceKeeperFX,
    EdSaveFmt_ForceClassic,
};

// docs/refactor/editor/phase3/00-slice1-native-save.md -- slice 1 of
// phase 3 (map serialization). Snapshots the live session (kfx_sim_state's
// slabs/things/action points, kfx_render's lights) into a MapContent
// (src/kfx_sim/include/map_content.h) and writes it into `dir` via the
// chosen format's writer (`format` -- EdSaveFmt_Auto by default). `dir` is
// a plain directory path, not a campaign-relative one -- the caller
// resolves that, this function just writes into wherever it's told.
// `level_name` (docs/refactor/editor/phase3/03-slice4-file-dialogs.md) is
// the level's own display name (written into .lof's NAME_TEXT, both
// formats) -- NULL or "" leaves it unset, same as before this parameter
// existed. `level_players`/`level_is_multiplayer` (docs/refactor/editor/
// phase3/04-slice5-playtest-settings-overwrite.md) are .lof's PLAYERS/KIND
// -- every caller should pass the session's own current values
// (editor_current_level_players()/editor_current_level_is_multiplayer())
// so a plain File > Save doesn't reset them, the same reasoning
// level_name's own callers already follow. No verify_map() yet (still
// deferred).
// `level_description` (docs/refactor/editor/05-script-and-level-settings.md
// §1) is .lof's DESCRIPTION, same "NULL/empty leaves it unset,
// every caller passes the session's own current value so a plain
// File > Save doesn't reset it" convention as level_name/level_players.
TbBool editor_save_map(LevelNumber lvnum, const char *dir, enum EditorSaveFormat format,
    const char *level_name, int level_players, TbBool level_is_multiplayer, const char *level_description);

// docs/refactor/editor/phase3/04-slice5-playtest-settings-overwrite.md --
// writes *only* `.lof` (name/players/is_multiplayer) for `lvnum` in `dir`,
// independent of the rest of the map -- the Level Settings dialog's own
// Apply action, not tied to a full Save. Also re-runs
// find_and_load_lof_files() afterward, same as editor_save_map() does,
// since KIND registers/re-registers the level into
// campaign.single_levels/multi_levels (see this codebase's own
// level_lof_file_parse() -- add_single_level_to_campaign()/
// add_multi_level_to_campaign()).
TbBool editor_save_level_info(LevelNumber lvnum, const char *dir,
    const char *level_name, int level_players, TbBool level_is_multiplayer, const char *level_description);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
