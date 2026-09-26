#include "pre_inc.h"
#include <json.h>
#include <json-dom.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api_seat_diff.h"
#include "bflib_basics.h"
#include "player_data.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KEY_LEN 96

static TbBool is_int_type(VALUE_TYPE t)
{
    return (t == VALUE_INT32) || (t == VALUE_UINT32) || (t == VALUE_INT64) || (t == VALUE_UINT64);
}

// ---- clone ---------------------------------------------------------------------------------------------------------

struct CloneCtx { VALUE *dst; };

static int clone_member(const VALUE *key, VALUE *val, void *ctx)
{
    struct CloneCtx *c = (struct CloneCtx *)ctx;
    VALUE *d = value_dict_add(c->dst, value_string(key));
    if (d != NULL) {
        api_seat_clone_value(d, val);
    }
    return 0;
}

void api_seat_clone_value(VALUE *dst, const VALUE *src)
{
    const VALUE_TYPE t = value_type(src);
    if (is_int_type(t)) {
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
            if (e != NULL) api_seat_clone_value(e, value_array_get(src, i));
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

// ---- equality and keys -----------------------------------------------------------------------------------------------

static TbBool values_equal(const VALUE *a, const VALUE *b);

static int dict_member_equal(const VALUE *key, VALUE *val, void *ctx)
{
    const VALUE *other = (const VALUE *)ctx;
    const VALUE *o = value_dict_get(other, value_string(key));
    return ((o != NULL) && values_equal(val, o)) ? 0 : 1;
}

static TbBool values_equal(const VALUE *a, const VALUE *b)
{
    const VALUE_TYPE ta = value_type(a), tb = value_type(b);
    if (is_int_type(ta) && is_int_type(tb)) return value_int64(a) == value_int64(b);
    if (ta != tb) return false;
    switch (ta) {
    case VALUE_BOOL: return value_bool(a) == value_bool(b);
    case VALUE_FLOAT: return value_float(a) == value_float(b);
    case VALUE_DOUBLE: return value_double(a) == value_double(b);
    case VALUE_STRING: return (value_string_length(a) == value_string_length(b)) && (strcmp(value_string(a), value_string(b)) == 0);
    case VALUE_ARRAY:
        if (value_array_size(a) != value_array_size(b)) return false;
        for (size_t i = 0; i < value_array_size(a); i++)
            if (!values_equal(value_array_get(a, i), value_array_get(b, i))) return false;
        return true;
    case VALUE_DICT:
        if (value_dict_size(a) != value_dict_size(b)) return false;
        return value_dict_walk_sorted(a, dict_member_equal, (void *)b) == 0;
    default: return true;
    }
}

struct DumpBuf { char *buf; size_t cap, len; };

static int dump_write(const char *s, size_t n, void *ud)
{
    struct DumpBuf *d = (struct DumpBuf *)ud;
    for (size_t i = 0; i < n && d->len + 1 < d->cap; i++) d->buf[d->len++] = s[i];
    d->buf[d->len] = 0;
    return 0;
}

// The identity of an array element: its `id` when it is an object with one, otherwise its own content.
static void elem_key(const VALUE *el, char *buf)
{
    if (value_type(el) == VALUE_DICT) {
        const VALUE *id = value_dict_get(el, "id");
        if ((id != NULL) && is_int_type(value_type(id))) {
            snprintf(buf, KEY_LEN, "i%lld", (long long)value_int64(id));
            return;
        }
    }
    struct DumpBuf d = { buf, KEY_LEN, 0 };
    buf[0] = 'j'; d.len = 1; buf[1] = 0;
    json_dom_dump(el, dump_write, &d, 0, JSON_DOM_DUMP_MINIMIZE);
}

static TbBool elem_has_id(const VALUE *el)
{
    if (value_type(el) != VALUE_DICT) return false;
    const VALUE *id = value_dict_get(el, "id");
    return (id != NULL) && is_int_type(value_type(id));
}

// ---- the diff --------------------------------------------------------------------------------------------------------

static void diff_dict(VALUE *out, const VALUE *prev, const VALUE *cur);

// A short array of numbers is a coordinate ([x, y], a slab rectangle): it changes as a whole, never element by element.
static TbBool is_coordinate(const VALUE *v)
{
    if ((value_type(v) != VALUE_ARRAY) || (value_array_size(v) == 0) || (value_array_size(v) > 4)) return false;
    for (size_t i = 0; i < value_array_size(v); i++)
        if (!is_int_type(value_type(value_array_get(v, i)))) return false;
    return true;
}

static void add_clone(VALUE *out, const char *key, const VALUE *v)
{
    VALUE *d = value_dict_add(out, key);
    if (d != NULL) api_seat_clone_value(d, v);
}

static int cell_changed(const char *a, const char *b, size_t x)
{
    return (a[2 * x] != b[2 * x]) || (a[2 * x + 1] != b[2 * x + 1]);
}

// The map rows as runs. Both arrays hold one string per slab row, two characters per slab.
static void diff_map_rows(VALUE *out, const VALUE *prev, const VALUE *cur)
{
    VALUE *changes = NULL, *revealed = NULL;
    for (size_t y = 0; y < value_array_size(cur); y++) {
        const char *a = value_string(value_array_get(prev, y));
        const char *b = value_string(value_array_get(cur, y));
        const size_t w = value_string_length(value_array_get(cur, y)) / 2;
        for (size_t x = 0; x < w;) {
            if (!cell_changed(a, b, x)) { x++; continue; }
            size_t e = x;
            while (e < w && cell_changed(a, b, e)) e++;
            if (changes == NULL) { changes = value_dict_add(out, "changes"); value_init_array(changes); }
            VALUE *run = value_array_append(changes);
            value_init_dict(run);
            value_init_int32(value_dict_add(run, "y"), (int32_t)y);
            value_init_int32(value_dict_add(run, "x"), (int32_t)x);
            value_init_string_(value_dict_add(run, "cells"), b + 2 * x, 2 * (e - x));
            x = e;
        }
        for (size_t x = 0; x < w;) {
            const TbBool was_hidden = (a[2 * x] == '.') && (a[2 * x + 1] == '.');
            const TbBool now_hidden = (b[2 * x] == '.') && (b[2 * x + 1] == '.');
            if (!(was_hidden && !now_hidden)) { x++; continue; }
            size_t e = x;
            while (e < w && (a[2 * e] == '.' && a[2 * e + 1] == '.') && !(b[2 * e] == '.' && b[2 * e + 1] == '.')) e++;
            if (revealed == NULL) { revealed = value_dict_add(out, "revealed"); value_init_array(revealed); }
            VALUE *run = value_array_append(revealed);
            value_init_dict(run);
            value_init_int32(value_dict_add(run, "y"), (int32_t)y);
            value_init_int32(value_dict_add(run, "x"), (int32_t)x);
            value_init_int32(value_dict_add(run, "len"), (int32_t)(e - x));
            x = e;
        }
    }
}

static TbBool rows_comparable(const VALUE *prev, const VALUE *cur)
{
    if ((value_type(prev) != VALUE_ARRAY) || (value_type(cur) != VALUE_ARRAY) || (value_array_size(prev) != value_array_size(cur))) return false;
    for (size_t y = 0; y < value_array_size(cur); y++) {
        const VALUE *a = value_array_get(prev, y), *b = value_array_get(cur, y);
        if ((value_type(a) != VALUE_STRING) || (value_type(b) != VALUE_STRING) || (value_string_length(a) != value_string_length(b))) return false;
    }
    return true;
}

static void diff_array(VALUE *out_parent, const char *key, const VALUE *prev, const VALUE *cur)
{
    const size_t np = value_array_size(prev), nc = value_array_size(cur);
    char (*kp)[KEY_LEN] = (char (*)[KEY_LEN])malloc((np + 1) * KEY_LEN);
    char (*kc)[KEY_LEN] = (char (*)[KEY_LEN])malloc((nc + 1) * KEY_LEN);
    if ((kp == NULL) || (kc == NULL)) { free(kp); free(kc); add_clone(out_parent, key, cur); return; }
    for (size_t i = 0; i < np; i++) elem_key(value_array_get(prev, i), kp[i]);
    for (size_t i = 0; i < nc; i++) elem_key(value_array_get(cur, i), kc[i]);

    VALUE d;
    value_init_dict(&d);
    VALUE *added = NULL, *removed = NULL, *changed = NULL;
    for (size_t i = 0; i < nc; i++) {
        size_t j = 0;
        while (j < np && strcmp(kc[i], kp[j]) != 0) j++;
        const VALUE *el = value_array_get(cur, i);
        if (j == np) {
            if (added == NULL) { added = value_dict_add(&d, "added"); value_init_array(added); }
            api_seat_clone_value(value_array_append(added), el);
        } else if (elem_has_id(el) && !values_equal(el, value_array_get(prev, j))) {
            VALUE sub;
            value_init_dict(&sub);
            diff_dict(&sub, value_array_get(prev, j), el);
            if (value_dict_size(&sub) > 0) {
                if (changed == NULL) { changed = value_dict_add(&d, "changed"); value_init_array(changed); }
                VALUE *ce = value_array_append(changed);
                value_init_dict(ce);
                add_clone(ce, "id", value_dict_get(el, "id"));
                // move the sub-diff's members in
                api_seat_diff_values(ce, value_array_get(prev, j), el);
            }
            value_fini(&sub);
        }
    }
    for (size_t j = 0; j < np; j++) {
        size_t i = 0;
        while (i < nc && strcmp(kp[j], kc[i]) != 0) i++;
        if (i == nc) {
            if (removed == NULL) { removed = value_dict_add(&d, "removed"); value_init_array(removed); }
            const VALUE *el = value_array_get(prev, j);
            if (elem_has_id(el)) api_seat_clone_value(value_array_append(removed), value_dict_get(el, "id"));
            else api_seat_clone_value(value_array_append(removed), el);
        }
    }
    if (value_dict_size(&d) > 0) {
        VALUE *slot = value_dict_add(out_parent, key);
        *slot = d; // ownership moves into the parent
    } else {
        value_fini(&d);
    }
    free(kp); free(kc);
}

struct DiffCtx { VALUE *out; const VALUE *prev; };

static int diff_member(const VALUE *keyv, VALUE *cur, void *ctx)
{
    struct DiffCtx *c = (struct DiffCtx *)ctx;
    const char *key = value_string(keyv);
    const VALUE *prev = value_dict_get(c->prev, key);
    if (prev == NULL) { add_clone(c->out, key, cur); return 0; }
    const VALUE_TYPE tc = value_type(cur), tp = value_type(prev);
    if ((tc == VALUE_DICT) && (tp == VALUE_DICT)) {
        VALUE sub;
        value_init_dict(&sub);
        diff_dict(&sub, prev, cur);
        if (value_dict_size(&sub) > 0) {
            VALUE *slot = value_dict_add(c->out, key);
            *slot = sub;
        } else {
            value_fini(&sub);
        }
    } else if ((tc == VALUE_ARRAY) && (tp == VALUE_ARRAY) && is_coordinate(cur)) {
        if (!values_equal(prev, cur)) add_clone(c->out, key, cur);
    } else if ((tc == VALUE_ARRAY) && (tp == VALUE_ARRAY)) {
        if ((strcmp(key, "rows") == 0) && rows_comparable(prev, cur)) {
            diff_map_rows(c->out, prev, cur);
        } else {
            diff_array(c->out, key, prev, cur);
        }
    } else if (!values_equal(prev, cur)) {
        add_clone(c->out, key, cur);
    }
    return 0;
}

struct RemovedCtx { VALUE *out; const VALUE *cur; VALUE *list; };

static int removed_member(const VALUE *keyv, VALUE *prev, void *ctx)
{
    struct RemovedCtx *c = (struct RemovedCtx *)ctx;
    (void)prev;
    if (value_dict_get(c->cur, value_string(keyv)) == NULL) {
        if (c->list == NULL) { c->list = value_dict_add(c->out, "_removed"); value_init_array(c->list); }
        value_init_string(value_array_append(c->list), value_string(keyv));
    }
    return 0;
}

static void diff_dict(VALUE *out, const VALUE *prev, const VALUE *cur)
{
    struct DiffCtx c = { out, prev };
    value_dict_walk_sorted(cur, diff_member, &c);
    struct RemovedCtx r = { out, cur, NULL };
    value_dict_walk_sorted(prev, removed_member, &r);
}

void api_seat_diff_values(VALUE *out, const VALUE *prev, const VALUE *cur)
{
    diff_dict(out, prev, cur);
}

// ---- baselines -------------------------------------------------------------------------------------------------------

static VALUE s_base[PLAYERS_COUNT];
static TbBool s_have[PLAYERS_COUNT];
static int64_t s_id[PLAYERS_COUNT];
static int64_t s_next_id = 0;

void api_seat_diff_reset(void)
{
    for (int i = 0; i < PLAYERS_COUNT; i++) {
        if (s_have[i]) value_fini(&s_base[i]);
        s_have[i] = false;
        s_id[i] = 0;
    }
}

void api_seat_finish_view(VALUE *view, PlayerNumber plyr_idx, TbBool want_diff, int64_t since)
{
    if ((plyr_idx < 0) || (plyr_idx >= PLAYERS_COUNT)) return;
    const int64_t id = ++s_next_id;
    const char *mode = "full", *reason = NULL;
    if (want_diff) {
        if (!s_have[plyr_idx]) reason = "NO_BASE";
        else if (s_id[plyr_idx] != since) reason = "BASE_MISMATCH";
        else {
            const VALUE *bt = value_dict_get(&s_base[plyr_idx], "turn");
            const VALUE *ct = value_dict_get(view, "turn");
            if ((bt != NULL) && (ct != NULL) && (value_int64(ct) < value_int64(bt))) reason = "TURN_WENT_BACKWARDS";
        }
    }
    VALUE diff;
    TbBool have_diff = false;
    if (want_diff && (reason == NULL)) {
        value_init_dict(&diff);
        diff_dict(&diff, &s_base[plyr_idx], view);
        have_diff = true;
    }
    // The new baseline is the full view just built.
    if (s_have[plyr_idx]) value_fini(&s_base[plyr_idx]);
    api_seat_clone_value(&s_base[plyr_idx], view);
    s_have[plyr_idx] = true;
    s_id[plyr_idx] = id;

    if (have_diff) {
        value_fini(view);
        *view = diff;
        value_init_string(value_dict_add(view, "mode"), "diff");
        value_init_int64(value_dict_add(view, "base"), since);
        value_init_int64(value_dict_add(view, "view_id"), id);
    } else {
        value_init_int64(value_dict_add(view, "view_id"), id);
        value_init_string(value_dict_add(view, "mode"), mode);
        if (reason != NULL) value_init_string(value_dict_add(view, "reason"), reason);
    }
}

#ifdef __cplusplus
}
#endif
