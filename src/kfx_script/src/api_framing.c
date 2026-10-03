/**
 * @file api_framing.c
 * @brief Finding whole JSON messages in the bytes an API client has sent so far. See api_framing.h.
 */
#include "pre_inc.h"
#include "api_framing.h"
#include "post_inc.h"

int api_frame_next(const char *buf, size_t len, size_t *start, size_t *end)
{
    size_t begin = len;
    size_t depth = 0;
    int in_string = 0;
    int escaped = 0;
    for (size_t i = 0; i < len; i++) {
        const char c = buf[i];
        if (depth == 0) {
            if (c == '{') {
                begin = i;
                depth = 1;
            }
            continue;
        }
        if (in_string) {
            if (escaped) escaped = 0;
            else if (c == '\\') escaped = 1;
            else if (c == '"') in_string = 0;
            continue;
        }
        if (c == '"') {
            in_string = 1;
        } else if (c == '{') {
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0) {
                *start = begin;
                *end = i + 1;
                return 1;
            }
        }
    }
    *start = begin;
    *end = begin;
    return 0;
}
