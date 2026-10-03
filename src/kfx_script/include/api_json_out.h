/**
 * @file api_json_out.h
 * @brief Serialising a reply or event for the API client: minimised JSON plus the newline that ends a message.
 *
 * Split out from api.c (refactor pass 3, S06) so it is unit-testable: every reply and event api.c sends goes
 * through it. The buffer grows as needed, so a reply isn't dropped for being longer than a fixed stack buffer
 * (README finding F4 of pass 3).
 */
#ifndef DK_API_JSON_OUT_H
#define DK_API_JSON_OUT_H

#include <stddef.h>
#include <json-dom.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Serialises `root` (minimised) followed by '\n' into a new heap buffer, sets *len to its length (the newline
 *  included, a terminating NUL not counted) and returns it for the caller to free(). Returns NULL, with *len 0,
 *  when the message would be longer than `max_len` bytes or memory runs out. */
char *api_json_serialise(const VALUE *root, size_t max_len, size_t *len);

/** Initialises `dst` as a deep copy of `src` (every integer as int64). A reply that repeats part of a request
 *  (its ack) must copy it: the request and the reply are freed separately (refactor pass 4 finding P4-F4). */
void api_json_clone(VALUE *dst, const VALUE *src);

#ifdef __cplusplus
}
#endif

#endif
