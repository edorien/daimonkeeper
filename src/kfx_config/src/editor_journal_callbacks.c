/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_journal_callbacks.c
 *     Callback-registration implementation. See editor_journal_callbacks.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "editor_journal_callbacks.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
static void noop_record_placement(long thing_idx, unsigned char pcktype,
    unsigned long par1, unsigned long par2, unsigned short par3, unsigned short par4,
    long pos_x, long pos_y) {}

static void noop_record_rect_terrain(unsigned char pcktype,
    long box_beg_x, long box_beg_y, long box_end_x, long box_end_y,
    SlabKind new_kind, PlayerNumber new_owner,
    const struct EditorRectSlabSnapshot *before, long count) {}

static void noop_record_door_lock(long thing_idx, TbBool was_locked) {}

static const struct EditorJournalCallbacks default_editor_journal_callbacks = {
    &noop_record_placement,
    &noop_record_rect_terrain,
    &noop_record_door_lock,
};
const struct EditorJournalCallbacks *editor_journal = &default_editor_journal_callbacks;

void set_editor_journal_callbacks(const struct EditorJournalCallbacks *callbacks)
{
    editor_journal = callbacks ? callbacks : &default_editor_journal_callbacks;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
