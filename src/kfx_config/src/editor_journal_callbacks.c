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
static void noop_record_placement(int64_t thing_idx, unsigned char pcktype,
    uint64_t par1, uint64_t par2, int64_t par3, int64_t par4,
    int64_t pos_x, int64_t pos_y) {}

static void noop_record_rect_terrain(unsigned char pcktype,
    int64_t box_beg_x, int64_t box_beg_y, int64_t box_end_x, int64_t box_end_y,
    SlabKind new_kind, PlayerNumber new_owner,
    const struct EditorRectSlabSnapshot *before, int64_t count) {}

static void noop_record_door_lock(int64_t thing_idx, TbBool was_locked) {}

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
