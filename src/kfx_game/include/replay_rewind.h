/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file replay_rewind.h
 *     Header file for replay_rewind.c.
 * @par Purpose:
 *     Rewinding a replay while watching it (upstream #5450, #5470): hold the
 *     Undo Pickup key (Backspace) and the replay runs backwards.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_REPLAY_REWIND_H
#define DK_REPLAY_REWIND_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/** A keyframe every this many turns; the turns between are reached by playing on from the one before. */
#define REWIND_KEYFRAME_INTERVAL 20

/** Takes a keyframe if one is due. Called while a replay plays, before a turn's packets are read
 *  (get_packet_load_game_inputs()). */
void replay_rewind_capture(void);
/** A recorded resync was applied: the next turn takes a keyframe, so going back over the turns after it doesn't
 *  apply the resync (the whole state, decompressed and checked) again at every step. */
void replay_rewind_note_resync(void);
/** Whether there is a keyframe to rewind to. */
TbBool replay_rewind_available(void);
/** Whether the replay is at its oldest keyframe: it can't go further back. */
TbBool replay_rewind_at_limit(void);
/**
 * Puts the game back to the newest keyframe at or before target (the oldest, if target is before it): the state
 * blobs, the Lua state and the replay reader, then what this machine derives from them. The caller plays on from
 * there to target (kfx_apploop's replay_rewind_to_turn()). False if there is no keyframe, or it can't be restored
 * (rewinding is then switched off for this replay).
 */
TbBool replay_rewind_restore(GameTurn target);
/** Bytes the keyframes take in the ring. */
size_t replay_rewind_buffer_used(void);
/** What the keyframes cost, for the log and the functional test. */
struct ReplayRewindStats {
    size_t snapshot_bytes;   /**< the snapshot buffer: the state pieces, each rounded up to whole pages */
    size_t ring_bytes;       /**< the ring's size (REPLAY_REWIND_BUFFER_SIZE) */
    size_t ring_used;        /**< what the keyframes in it take */
    size_t largest_record;   /**< the largest keyframe record so far (diff, Lua state, header) */
    int64_t captures;        /**< keyframes taken (the first one a whole copy, not timed below) */
    int64_t diff_ns_total;   /**< time spent on the later ones */
    int64_t diff_ns_max;
};
void replay_rewind_get_stats(struct ReplayRewindStats *stats);
/** The number of keyframes kept, and the turn of the oldest one (0 if none). */
size_t replay_rewind_keyframe_count(GameTurn *oldest_turn);
/** Forgets every keyframe and frees the buffers. Called when a replay starts and when packet mode ends. */
void replay_rewind_reset(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
