/**
 * @file api_json_out.c
 * @brief Serialising a reply or event for the API client. See api_json_out.h.
 */
#include "pre_inc.h"
#include "api_json_out.h"

#include <stdlib.h>
#include <string.h>
#include <json.h>
#include "post_inc.h"

/** The first buffer; it doubles as the message grows. */
#define API_JSON_OUT_INITIAL 1024

struct GrowingBuffer
{
    char *data;
    size_t len;
    size_t cap;
    size_t max; /**< the message's longest length, the newline included */
};

/** Makes room for `extra` more bytes (plus a terminating NUL). Returns 0 when that would pass the limit or memory runs out. */
static int grow(struct GrowingBuffer *b, size_t extra)
{
    if (b->len + extra > b->max)
        return 0;
    size_t cap = b->cap;
    while (b->len + extra + 1 > cap)
        cap *= 2;
    if (cap != b->cap)
    {
        char *data = (char *)realloc(b->data, cap);
        if (data == NULL)
            return 0;
        b->data = data;
        b->cap = cap;
    }
    return 1;
}

static int dump_writer(const char *str, size_t size, void *user_data)
{
    struct GrowingBuffer *b = (struct GrowingBuffer *)user_data;
    // One byte stays free for the newline that ends the message.
    if ((b->len + size + 1 > b->max) || !grow(b, size))
        return JSON_ERR_OUTOFMEMORY;
    memcpy(b->data + b->len, str, size);
    b->len += size;
    return 0;
}

char *api_json_serialise(const VALUE *root, size_t max_len, size_t *len)
{
    *len = 0;
    struct GrowingBuffer b = {(char *)malloc(API_JSON_OUT_INITIAL), 0, API_JSON_OUT_INITIAL, max_len};
    if (b.data == NULL)
        return NULL;
    if ((json_dom_dump(root, dump_writer, &b, 0, JSON_DOM_DUMP_MINIMIZE) != 0) || !grow(&b, 1))
    {
        free(b.data);
        return NULL;
    }
    b.data[b.len++] = '\n';
    b.data[b.len] = '\0';
    *len = b.len;
    return b.data;
}

struct CloneCtx { VALUE *dst; };

static int clone_member(const VALUE *key, VALUE *val, void *ctx)
{
    struct CloneCtx *c = (struct CloneCtx *)ctx;
    VALUE *d = value_dict_add(c->dst, value_string(key));
    if (d != NULL) {
        api_json_clone(d, val);
    }
    return 0;
}

void api_json_clone(VALUE *dst, const VALUE *src)
{
    const VALUE_TYPE t = value_type(src);
    if ((t == VALUE_INT32) || (t == VALUE_UINT32) || (t == VALUE_INT64) || (t == VALUE_UINT64)) {
        value_init_int64(dst, value_int64(src));
        return;
    }
    switch (t) {
    case VALUE_BOOL: value_init_bool(dst, value_bool(src)); break;
    case VALUE_FLOAT: value_init_float(dst, value_float(src)); break;
    case VALUE_DOUBLE: value_init_double(dst, value_double(src)); break;
    case VALUE_STRING: value_init_string_(dst, value_string(src), value_string_length(src)); break;
    case VALUE_ARRAY: {
        value_init_array(dst);
        for (size_t i = 0; i < value_array_size(src); i++) {
            VALUE *e = value_array_append(dst);
            if (e != NULL) api_json_clone(e, value_array_get(src, i));
        }
        break;
    }
    case VALUE_DICT: {
        struct CloneCtx c = { dst };
        value_init_dict(dst);
        value_dict_walk_sorted(src, clone_member, &c);
        break;
    }
    default: value_init_null(dst); break;
    }
}
