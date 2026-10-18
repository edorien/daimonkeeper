/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file rewind_ring.c
 *     The record ring and the XOR page diffs of a replay rewind (rewind_ring.h).
 * @par Comment:
 *     After upstream #5450's ring and diff_page()/apply_diff(), with the record
 *     index kept beside the ring instead of in trailers inside it, and pages
 *     compared with memcmp() before they are looked at line by line.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "rewind_ring.h"

#include "bflib_math.h"
#include <stdlib.h>
#include <string.h>

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define REWIND_LINES_PER_PAGE (REWIND_PAGE_SIZE / REWIND_LINE_SIZE)
#define REWIND_WORDS_PER_LINE (REWIND_LINE_SIZE / 8)
/* the worst a page encodes to: its index and line mask, then every line, word and byte changed */
#define REWIND_PAGE_MAX_ENCODED (sizeof(uint32_t) + sizeof(uint64_t) + REWIND_LINES_PER_PAGE * (1 + REWIND_WORDS_PER_LINE * (1 + 8)))

static size_t ring_align(size_t n)
{
    return (n + REWIND_RING_ALIGN - 1) & ~(size_t)(REWIND_RING_ALIGN - 1);
}

TbBool rewind_ring_init(struct RewindRing *ring, size_t cap)
{
    memset(ring, 0, sizeof(*ring));
    if (cap == 0)
        return false;
    ring->buf = (unsigned char *)malloc(cap);
    if (ring->buf == NULL)
        return false;
    ring->cap = cap;
    return true;
}

void rewind_ring_free(struct RewindRing *ring)
{
    free(ring->buf);
    free(ring->recs);
    memset(ring, 0, sizeof(*ring));
}

void rewind_ring_clear(struct RewindRing *ring)
{
    ring->first = 0;
    ring->count = 0;
    ring->used = 0;
}

static struct RewindRecord *record_at(const struct RewindRing *ring, size_t i)
{
    return &ring->recs[(ring->first + i) % ring->recs_cap];
}

void rewind_ring_drop_oldest(struct RewindRing *ring)
{
    if (ring->count == 0)
        return;
    ring->used -= ring_align(ring->recs[ring->first].len);
    ring->first = (ring->first + 1) % ring->recs_cap;
    ring->count--;
    if (ring->count == 0)
        ring->first = 0;
}

static TbBool reserve_record_slot(struct RewindRing *ring)
{
    if (ring->count < ring->recs_cap)
        return true;
    const size_t ncap = (ring->recs_cap > 0) ? ring->recs_cap * 2 : 64;
    struct RewindRecord *grown = (struct RewindRecord *)malloc(ncap * sizeof(struct RewindRecord));
    if (grown == NULL)
        return false;
    for (size_t i = 0; i < ring->count; i++)
        grown[i] = *record_at(ring, i);
    free(ring->recs);
    ring->recs = grown;
    ring->recs_cap = ncap;
    ring->first = 0;
    return true;
}

unsigned char *rewind_ring_push(struct RewindRing *ring, size_t len)
{
    if ((len == 0) || (len > ring->cap) || !reserve_record_slot(ring))
        return NULL;
    size_t off;
    for (;;)
    {
        if (ring->count == 0)
        {
            off = 0;
            break;
        }
        const size_t tail = ring->recs[ring->first].off;
        const struct RewindRecord *newest = record_at(ring, ring->count - 1);
        const size_t head = ring_align(newest->off + newest->len);
        if (newest->off >= tail)
        {
            // the records run from tail to head: free space after the head, and before the tail
            if ((head <= ring->cap) && (len <= ring->cap - head))
            {
                off = head;
                break;
            }
            if (len <= tail)
            {
                off = 0;
                break;
            }
        } else
        if ((head <= tail) && (len <= tail - head))
        {
            // wrapped: the only free space is between the newest and the oldest
            off = head;
            break;
        }
        rewind_ring_drop_oldest(ring);
    }
    struct RewindRecord *rec = &ring->recs[(ring->first + ring->count) % ring->recs_cap];
    rec->off = off;
    rec->len = len;
    ring->count++;
    ring->used += ring_align(len);
    return ring->buf + off;
}

unsigned char *rewind_ring_record(const struct RewindRing *ring, size_t i, size_t *len)
{
    if (i >= ring->count)
        return NULL;
    const struct RewindRecord *rec = record_at(ring, i);
    if (len != NULL)
        *len = rec->len;
    return ring->buf + rec->off;
}

TbBool rewind_buf_reserve(struct RewindBuf *buf, size_t extra)
{
    if (extra <= buf->cap - buf->len)
        return true;
    size_t ncap = (buf->cap > 0) ? buf->cap : 65536;
    while (ncap - buf->len < extra)
        ncap *= 2;
    unsigned char *grown = (unsigned char *)realloc(buf->data, ncap);
    if (grown == NULL)
        return false;
    buf->data = grown;
    buf->cap = ncap;
    return true;
}

void rewind_buf_free(struct RewindBuf *buf)
{
    free(buf->data);
    memset(buf, 0, sizeof(*buf));
}

/* Appends the XOR of one whole page pair to out (the caller knows they differ). */
static void encode_page(struct RewindBuf *out, uint32_t pg, const unsigned char *old_page, const unsigned char *new_page)
{
    unsigned char *base = out->data + out->len;
    size_t n = sizeof(pg) + sizeof(uint64_t);
    uint64_t line_mask = 0;
    for (int ln = 0; ln < REWIND_LINES_PER_PAGE; ln++)
    {
        const unsigned char *a = old_page + ln * REWIND_LINE_SIZE;
        const unsigned char *b = new_page + ln * REWIND_LINE_SIZE;
        if (memcmp(a, b, REWIND_LINE_SIZE) == 0)
            continue;
        line_mask |= (uint64_t)1 << ln;
        unsigned char *word_mask = &base[n++];
        *word_mask = 0;
        for (int w = 0; w < REWIND_WORDS_PER_LINE; w++)
        {
            uint64_t wa;
            uint64_t wb;
            memcpy(&wa, a + w * 8, sizeof(wa));
            memcpy(&wb, b + w * 8, sizeof(wb));
            const uint64_t x = wa ^ wb;
            if (x == 0)
                continue;
            *word_mask |= (unsigned char)(1u << w);
            unsigned char xb[8];
            memcpy(xb, &x, sizeof(xb));
            unsigned char *byte_mask = &base[n++];
            *byte_mask = 0;
            for (int i = 0; i < 8; i++)
            {
                if (xb[i] == 0)
                    continue;
                *byte_mask |= (unsigned char)(1u << i);
                base[n++] = xb[i];
            }
        }
    }
    memcpy(base, &pg, sizeof(pg));
    memcpy(base + sizeof(pg), &line_mask, sizeof(line_mask));
    out->len += n;
}

TbBool rewind_diff_encode(struct RewindBuf *out, unsigned char *snap, const unsigned char *live, size_t len, size_t snap_off)
{
    const uint32_t first_page = (uint32_t)(snap_off / REWIND_PAGE_SIZE);
    for (size_t off = 0; off < len; off += REWIND_PAGE_SIZE)
    {
        const size_t n = (len - off < REWIND_PAGE_SIZE) ? len - off : REWIND_PAGE_SIZE;
        if (memcmp(snap + off, live + off, n) == 0)
            continue;
        if (!rewind_buf_reserve(out, REWIND_PAGE_MAX_ENCODED))
            return false;
        const uint32_t pg = first_page + (uint32_t)(off / REWIND_PAGE_SIZE);
        if (n == REWIND_PAGE_SIZE)
        {
            encode_page(out, pg, snap + off, live + off);
        } else
        {
            // the piece's last page: the snapshot is zero past its end, and live is taken to be
            unsigned char last[REWIND_PAGE_SIZE];
            memset(last, 0, sizeof(last));
            memcpy(last, live + off, n);
            encode_page(out, pg, snap + off, last);
        }
        memcpy(snap + off, live + off, n);
    }
    return true;
}

TbBool rewind_diff_apply(unsigned char *snapshot, size_t snap_len, const unsigned char *diff, size_t diff_len)
{
    const size_t pages = snap_len / REWIND_PAGE_SIZE;
    const unsigned char *p = diff;
    const unsigned char *end = diff + diff_len;
    while (p < end)
    {
        uint32_t pg;
        uint64_t line_mask;
        if ((size_t)(end - p) < sizeof(pg) + sizeof(line_mask))
            return false;
        memcpy(&pg, p, sizeof(pg));
        p += sizeof(pg);
        memcpy(&line_mask, p, sizeof(line_mask));
        p += sizeof(line_mask);
        if (pg >= pages)
            return false;
        unsigned char *page = snapshot + (size_t)pg * REWIND_PAGE_SIZE;
        for (; line_mask != 0; line_mask &= line_mask - 1)
        {
            unsigned char *line = page + LbCtz64(line_mask) * REWIND_LINE_SIZE;
            if (p >= end)
                return false;
            for (unsigned word_mask = *p++; word_mask != 0; word_mask &= word_mask - 1)
            {
                unsigned char *word = line + LbCtz32(word_mask) * 8;
                if (p >= end)
                    return false;
                for (unsigned byte_mask = *p++; byte_mask != 0; byte_mask &= byte_mask - 1)
                {
                    if (p >= end)
                        return false;
                    word[LbCtz32(byte_mask)] ^= *p++;
                }
            }
        }
    }
    return true;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
