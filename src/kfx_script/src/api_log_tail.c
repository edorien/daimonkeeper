#include "pre_inc.h"
#include <json.h>
#include <json-dom.h>

#include <stdlib.h>

#include "api_log_tail.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

int64_t api_log_tail_lines(const char *text, size_t text_len, TbBool is_whole_buffer, int64_t want, VALUE *out_array)
{
    if ((text == NULL) || (want <= 0)) {
        return 0;
    }
    size_t end = text_len;
    while ((end > 0) && ((text[end - 1] == '\n') || (text[end - 1] == '\r'))) {
        end--; // a trailing newline (or CRLF) is not itself an extra empty line
    }
    if (end == 0) {
        return 0; // nothing but blank lines
    }

    // Every line's [start, end) span, in order, trimming a trailing '\r' left by "\r\n".
    size_t cap = 64;
    size_t *line_start = (size_t *)malloc(sizeof(size_t) * cap);
    size_t *line_end = (size_t *)malloc(sizeof(size_t) * cap);
    if ((line_start == NULL) || (line_end == NULL)) {
        free(line_start);
        free(line_end);
        return 0;
    }
    size_t nlines = 0;
    size_t s = 0;
    for (size_t i = 0; i <= end; i++) {
        if ((i == end) || (text[i] == '\n')) {
            if (nlines == cap) {
                cap *= 2;
                size_t *ns = (size_t *)realloc(line_start, sizeof(size_t) * cap);
                size_t *ne = (size_t *)realloc(line_end, sizeof(size_t) * cap);
                if ((ns == NULL) || (ne == NULL)) {
                    free(ns != NULL ? ns : line_start);
                    free(ne != NULL ? ne : line_end);
                    return 0;
                }
                line_start = ns;
                line_end = ne;
            }
            size_t e = i;
            while ((e > s) && (text[e - 1] == '\r')) {
                e--;
            }
            line_start[nlines] = s;
            line_end[nlines] = e;
            nlines++;
            s = i + 1;
        }
    }

    size_t first = (!is_whole_buffer && (nlines > 0)) ? 1 : 0;
    if ((nlines - first) > (size_t)want) {
        first = nlines - (size_t)want;
    }
    int64_t emitted = 0;
    for (size_t i = first; i < nlines; i++) {
        value_init_string_(value_array_append(out_array), text + line_start[i], line_end[i] - line_start[i]);
        emitted++;
    }
    free(line_start);
    free(line_end);
    return emitted;
}

#ifdef __cplusplus
}
#endif
