/**
 * @file api_framing.h
 * @brief Finding whole JSON messages in the bytes an API client has sent so far.
 *
 * TCP delivers a stream, not messages: one message may arrive over several reads (an agent's memory can be tens of
 * kilobytes), and one read may hold several messages. Split out from api.c so the scan itself is unit-testable.
 */
#ifndef DK_API_FRAMING_H
#define DK_API_FRAMING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Looks for the first complete top-level JSON object in `buf` (`len` bytes). Returns 1 when there is one, with
 *  [*start, *end) its bytes; 0 when there is none yet, with *start where an unfinished object begins (or `len` when the
 *  buffer holds none), so everything before *start can be dropped. Braces inside strings do not count, and a backslash
 *  escapes the next character inside a string. Bytes outside objects (newlines, stray text) are skipped. */
int api_frame_next(const char *buf, size_t len, size_t *start, size_t *end);

#ifdef __cplusplus
}
#endif

#endif
