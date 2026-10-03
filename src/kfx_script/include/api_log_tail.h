/**
 * @file api_log_tail.h
 * @brief The last N lines of the running keeperfx's own log (get_log_tail), for an agent debugging a confusing
 * session without a human tailing keeperfx.log by hand.
 *
 * Split out from api.c so the line-splitting itself (the only part with real logic) is unit-testable on an
 * in-memory buffer, without needing a real log file.
 */
#ifndef DK_API_LOG_TAIL_H
#define DK_API_LOG_TAIL_H

#include "globals.h"
#include <json-dom.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Appends the last `want` lines of `text` (`text_len` bytes, not necessarily null-terminated) to `out_array` (an
 *  already-initialised VALUE array), oldest first. `is_whole_buffer` says whether `text` is the entire file: when
 *  it is not (the caller only read a tail window of a larger file), the earliest line found is dropped rather than
 *  shown truncated, since its true start may lie further back, outside the window. Returns how many lines were
 *  appended. */
int64_t api_log_tail_lines(const char *text, size_t text_len, TbBool is_whole_buffer, int64_t want, VALUE *out_array);

#ifdef __cplusplus
}
#endif

#endif
