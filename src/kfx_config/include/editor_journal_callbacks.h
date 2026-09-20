/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_journal_callbacks.h
 *     Header file for editor_journal_callbacks.c.
 * @par Purpose:
 *     Callback-registration interface letting kfx_net (packets_cheats.c)
 *     record a just-created thing into kfx_editor's undo journal without
 *     depending on kfx_editor.h directly -- kfx_editor is ranked above
 *     kfx_net (docs/refactor/editor/00-overview.md §5.1), so this is the
 *     one inbound edge needed, mirroring editor_callbacks.h's own pattern.
 *     Always wired to a real, non-NULL pointer (a no-op default until
 *     kfx_editor's setup_game() wiring takes over) -- callers never need a
 *     NULL check; the real implementation itself no-ops when no editor
 *     session is active, exactly like editor_callbacks's own convention.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_EDITOR_JOURNAL_CALLBACKS_H
#define DK_EDITOR_JOURNAL_CALLBACKS_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

// docs/refactor/editor/09-toolbox-remainder.md §1 -- one slab's pre-mutation
// kind+owner, captured by record_rect_terrain()'s caller before applying a
// rect-terrain op. Plain C POD (shared verbatim by kfx_net's capture side
// and kfx_editor's std::vector-backed journal storage).
struct EditorRectSlabSnapshot {
    unsigned char kind;
    unsigned char owner;
};

struct EditorJournalCallbacks {
    /* docs/refactor/editor/02-editing-toolbox.md §4 -- called right after
       any editor placement tool's create_*() call succeeds (creature/hero/
       digger, object, trap, door), so Undo can delete it later and Redo
       can recreate it. No-ops outside an active editor session (checked
       inside the real implementation, not by callers).
       pcktype/par1-4/pos_x/pos_y are the exact packet fields that created
       this thing, so Redo can resend the identical action later -- needed
       because several of these verbs (PckA_CheatMakeCreature/_MakeDigger/
       PckA_EditorPlaceTrap/_PlaceDoor) read position from the packet's own
       *ambient* pos_x/pos_y rather than a param, so replaying them
       correctly means overriding that field too, not just resending
       par1-4 (see editor_journal.cpp's own Redo comment). */
    void (*record_placement)(long thing_idx, unsigned char pcktype,
        unsigned long par1, unsigned long par2, unsigned short par3, unsigned short par4,
        long pos_x, long pos_y);
    /* docs/refactor/editor/09-toolbox-remainder.md §1 -- rect-terrain-op
       undo/redo (PckA_EditorPlaceTerrainRect/_RectClearEarth/_RectSetOwner
       only -- PckA_EditorRectDeleteThings stays unjournaled, see this
       callback's implementation for why). Called by the packet handler
       BEFORE applying the mutation (unlike record_placement, called after),
       since the whole point is capturing the pre-mutation state: `before`
       is one entry per slab in the box, row-major (sy outer, sx inner,
       matching the mutation's own iteration order) -- the implementation
       copies it into its own storage before this call returns, so the
       caller's buffer only needs to survive the call itself. new_kind/
       new_owner are the value the mutation is about to apply (uniform over
       the whole box for PlaceTerrainRect/RectClearEarth; new_owner only for
       RectSetOwner, which leaves each slab's own kind alone) -- recorded so
       Redo can reapply the same op without needing a second server round
       trip. No-ops outside an active editor session, same convention as
       record_placement. */
    void (*record_rect_terrain)(unsigned char pcktype,
        long box_beg_x, long box_beg_y, long box_end_x, long box_end_y,
        SlabKind new_kind, PlayerNumber new_owner,
        const struct EditorRectSlabSnapshot *before, long count);
    /* fx-plans/00 item A7 -- a door's lock is about to be toggled
       (Ctrl+click in the Door tool); `was_locked` is the state before.
       Called by the packet handler before it toggles. No-ops outside an
       active editor session. */
    void (*record_door_lock)(long thing_idx, TbBool was_locked);
};
void set_editor_journal_callbacks(const struct EditorJournalCallbacks *callbacks);
extern const struct EditorJournalCallbacks *editor_journal;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
