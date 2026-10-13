/**
 * @file api_framing.c
 * @brief Finding whole JSON messages in the bytes an API client has sent so far. See api_framing.h.
 */
#include "pre_inc.h"
#include <string.h>
#include <strings.h>
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

int api_stream_looks_like_api(const char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        const char c = buf[i];
        if ((c == ' ') || (c == '\t') || (c == '\r') || (c == '\n'))
            continue;
        return (c == '{') ? 1 : 0;
    }
    return -1;
}

int api_console_command_refused(const char *command)
{
    if (command == NULL)
        return 0;
    while ((*command != '\0') && ((unsigned char)*command <= ' '))
        command++;
    size_t n = 0;
    while (((unsigned char)command[n] > ' '))
        n++;
    return (n == 3) && (strncasecmp(command, "lua", 3) == 0);
}

int api_token_matches(const char *expected, const char *given)
{
    if ((expected == NULL) || (expected[0] == '\0') || (given == NULL))
        return 0;
    const size_t expected_len = strlen(expected);
    const size_t given_len = strlen(given);
    // Every byte of `expected` is looked at whatever `given` holds; a length mismatch is folded into the result.
    unsigned char diff = (expected_len != given_len) ? 1 : 0;
    for (size_t i = 0; i < expected_len; i++) {
        const unsigned char g = (i < given_len) ? (unsigned char)given[i] : 0;
        diff |= (unsigned char)expected[i] ^ g;
    }
    return diff == 0;
}
