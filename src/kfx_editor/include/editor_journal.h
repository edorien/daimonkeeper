/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_journal.h
 *     Header file for editor_journal.cpp.
 * @par Purpose:
 *     docs/refactor/editor/02-editing-toolbox.md §4 -- the editor's undo/
 *     redo journal (placements only -- see that doc's status notes for
 *     scope: terrain paint/fill/rectangle undo stays deferred).
 *     Internal to kfx_editor (not part of kfx_editor.h's public surface,
 *     same split as editor_toolbox.h) except for
 *     editor_journal_record_placement(), which lives in kfx_editor.h
 *     itself since main.cpp needs it as an EditorJournalCallbacks target.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_EDITOR_JOURNAL_H
#define DK_EDITOR_JOURNAL_H

#include "bflib_basics.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

// Clears the journal -- called from editor_open() so a new session never
// sees stale thing indices from a previous one (thing slots get reused).
void editor_journal_reset(void);

// Checks Ctrl+Z/Ctrl+Y and calls editor_journal_do_undo()/do_redo() below.
// Called once per frame from editor_frame(). Keyboard shortcut stays
// hardcoded (not routed through a definable-keybinding system) --
// Ctrl+Z/Ctrl+Y are standard everywhere, unlike the toolbox's other
// implicit shortcuts (D6 backlog item).
void editor_journal_frame(void);

// docs/refactor/editor/09-toolbox-remainder.md backlog item -- the
// toolbox's own Undo/Redo buttons (and a History tab listing the journal)
// need the same trigger/introspection editor_journal_frame()'s keyboard
// path already has, without duplicating it.
//
// do_undo()/do_redo(): apply the top of the undo/redo stack, if any
// (no-op when empty) -- exactly what Ctrl+Z/Ctrl+Y already do, extracted
// so a button click can call the same logic.
void editor_journal_do_undo(void);
void editor_journal_do_redo(void);

// undo_count()/redo_count(): how many entries are on each stack, for
// greying out a button with nothing to do and showing a total in the
// History tab.
int editor_journal_undo_count(void);
int editor_journal_redo_count(void);

// describe_undo()/describe_redo(): a short human-readable label for the
// entry at index_from_top (0 = top of stack, the one Undo/Redo would act
// on next; counts up toward the oldest). Returns NULL if index_from_top is
// out of range. The returned pointer is into a small shared static buffer,
// valid only until the NEXT describe_undo()/describe_redo() call --
// render/consume it immediately, don't hold onto it.
const char *editor_journal_describe_undo(int index_from_top);
const char *editor_journal_describe_redo(int index_from_top);

// phase5/06-slices6-8-points-tool.md -- journals the placement (placed = true)
// or deletion (false) of a light / action point / effect generator so
// Ctrl+Z / Ctrl+Y can reverse it. Applied by editor_points.cpp directly, not
// through a packet.
struct EditorPointSnapshot;
void editor_journal_record_point(TbBool placed, const struct EditorPointSnapshot *snap);

// Test-only seam (docs/refactor/testing/stage-02-testability-and-fakes.md's
// "Pattern A", adapted): record_placement()/record_rect_terrain() both gate
// on editor_is_active(), which reads a real session flag with no exported
// override -- calling the real editor_open() to set it needs a genuinely
// loaded level/player, impractical for an isolated Catch2 unit test. This
// setter forces that gate open/closed independent of any real session, for
// tests only; never call it from production code.
void editor_journal_test_force_active(TbBool force_active);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
