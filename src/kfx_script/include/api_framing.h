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

/** Whether what a new connection has sent so far can be an API client (security pass S02). A client's first
 *  non-whitespace byte is the '{' of its first message; a web page's request (a browser can be made to send one to
 *  localhost, which is how a page in the player's browser could reach the API) starts with "POST", "GET", ... and
 *  anything else is not ours either. Returns 1 for '{', 0 for another byte (drop the connection, say nothing: an
 *  answer is more than a stranger is owed), -1 while only whitespace (or nothing) has arrived. */
int api_stream_looks_like_api(const char *buf, size_t len);

/** Whether a console command must not be run for an API client: Lua code, which would reach everything the game's
 *  script state can (a client that needs scripting has the script commands and the Lua-free actions). `command` is
 *  the line as `console_command` received it, after the console prefix character. */
int api_console_command_refused(const char *command);

/** Whether `given` is the API token `expected`, compared in time that does not depend on where they differ. An empty
 *  or missing `expected` matches nothing. */
int api_token_matches(const char *expected, const char *given);

#ifdef __cplusplus
}
#endif

#endif
