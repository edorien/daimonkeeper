/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file rewind_ring.h
 *     A ring buffer of variable-length records, and the XOR page diffs a replay rewind stores in it.
 * @par Purpose:
 *     The storage half of replay rewinding (replay_rewind.c, after upstream
 *     #5450), kept free of game state so it can be tested on its own.
 *     The ring holds records oldest to newest, each contiguous (a record never
 *     wraps around the end of the buffer); a new record evicts the oldest ones
 *     until it fits. The diff codec turns two equal-length byte ranges into the
 *     sparse XOR of their 4 KiB pages: applying it to either range gives the
 *     other, so a chain of keyframe diffs can be walked both ways.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_REWIND_RING_H
#define DK_REWIND_RING_H

#include "bflib_basics.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/** Records start on this boundary in the ring. */
#define REWIND_RING_ALIGN 8

struct RewindRecord {
    size_t off;
    size_t len;
};

struct RewindRing {
    unsigned char *buf;
    size_t cap;
    /** the records, a circular array: recs[first] is the oldest */
    struct RewindRecord *recs;
    size_t recs_cap;
    size_t first;
    size_t count;
    /** bytes the records take, alignment included */
    size_t used;
};

/** Allocates a ring of cap bytes. False if there is no memory for it (the ring is then empty, cap 0). */
TbBool rewind_ring_init(struct RewindRing *ring, size_t cap);
void rewind_ring_free(struct RewindRing *ring);
/** Drops every record, keeping the buffer. */
void rewind_ring_clear(struct RewindRing *ring);
/**
 * Reserves len contiguous bytes as the newest record, evicting the oldest records until they fit.
 * NULL (and nothing evicted) when len is 0 or more than the ring holds, or there is no memory to index the record.
 */
unsigned char *rewind_ring_push(struct RewindRing *ring, size_t len);
/** Drops the oldest record. */
void rewind_ring_drop_oldest(struct RewindRing *ring);
/** The i-th record from the oldest (0) to the newest (count - 1); its length in *len. NULL if there is no such record. */
unsigned char *rewind_ring_record(const struct RewindRing *ring, size_t i, size_t *len);

/** A growable byte buffer the diffs are written to. */
struct RewindBuf {
    unsigned char *data;
    size_t len;
    size_t cap;
};
TbBool rewind_buf_reserve(struct RewindBuf *buf, size_t extra);
void rewind_buf_free(struct RewindBuf *buf);

#define REWIND_PAGE_SIZE 4096
#define REWIND_LINE_SIZE 64
/** Rounds n up to whole diff pages. */
#define rewind_page_round(n) (((n) + REWIND_PAGE_SIZE - 1) / REWIND_PAGE_SIZE * REWIND_PAGE_SIZE)

/**
 * Appends the XOR diff of the snapshot range snap (len bytes, at page-aligned offset snap_off of the whole snapshot)
 * against live to out, then copies live over snap. The pages of the snapshot past len (up to the next page boundary)
 * must be zero, as live is taken to be zero there. Format, per changed page: u32 page index in the snapshot, u64 mask
 * of its changed 64-byte lines; per changed line a u8 mask of its changed 8-byte words; per changed word a u8 mask of
 * its changed bytes, then each changed byte's XOR. False if out can't grow.
 */
TbBool rewind_diff_encode(struct RewindBuf *out, unsigned char *snap, const unsigned char *live, size_t len, size_t snap_off);
/** Applies a diff rewind_diff_encode() wrote to the snapshot (snap_len bytes, whole pages). False if it is malformed. */
TbBool rewind_diff_apply(unsigned char *snapshot, size_t snap_len, const unsigned char *diff, size_t diff_len);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
