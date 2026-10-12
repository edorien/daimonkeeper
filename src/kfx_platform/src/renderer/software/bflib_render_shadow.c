/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_render_shadow.c
 *     trig(): the software renderer's creature shadow triangles.
 * @par Purpose:
 *     Every creature shadow is two of these triangles over whatever is already on screen: where the shadow sprite
 *     (vec_map, 256x256) is set, the pixel is shaded by vec_shade.
 * @par Comment:
 *     Replaces bflib_render_trig.c (refactor pass 5, S10), the decompiled x86 rasteriser of 26 render modes of
 *     which the game draws only this one (VM_SpriteTranslucent). Draws exactly the same pixels
 *     (trig_shadow_golden_test.cpp), including the original's ways:
 *     - a triangle facing the other way draws nothing;
 *     - a row is drawn one row below where it is (a whole shadow sits a pixel low, and the row past the
 *       window can be written);
 *     - the sprite's step along a row is worked out once, at the triangle's widest row, and the start of each
 *       row follows the left edge down;
 *     - a row that starts left of the window is sampled from where it would have started;
 *     - edges and steps are 16.16 fixed point, divided with truncation; only 8 bits of u and v pick the texel.
 *     Every value fits 64 bits (the triangle's corners are within 16.16), so the original's 32-bit register
 *     arithmetic and its `long` (32 bits on Windows) no longer matter.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "bflib_render.h"
#include "renderer/RendererManager.h"

#include "bflib_basics.h"
#include "bflib_vidraw.h"
#include "post_inc.h"

/** A triangle ready to draw, rows counted from its top vertex: at row i the left edge is at xl, the right at xr
 *  (16.16) and the left end of the row samples the sprite at u, v (16.16). The rows from `bend` on use the second
 *  set of starts and steps (one edge turns at the middle vertex there). */
struct ShadowTriangle {
    int64_t top;          // screen row of the top vertex
    int64_t height;       // rows
    int64_t bend;         // first row of the second part
    int64_t xl[2], xl_step[2];
    int64_t xr[2], xr_step[2];
    int64_t u[2], u_step[2];
    int64_t v[2], v_step[2];
    int64_t row_u_step;   // along a row
    int64_t row_v_step;
};

enum ShadowShape {
    Shape_None = 0,
    Shape_LongLeft,   // the middle vertex is on the right: the left edge goes top to bottom
    Shape_LongRight,  // the middle vertex is on the left
    Shape_FlatBottom,
    Shape_FlatTop,
};

/**
 * Orders the points top first and says how the triangle is shaped; Shape_None for one facing the other way
 * (or flat). After it: LongLeft has the middle vertex b on the right, c at the bottom; LongRight has the middle
 * vertex c on the left, b at the bottom; FlatBottom has a on top, b bottom right, c bottom left; FlatTop has a top
 * left, b top right, c at the bottom.
 */
static enum ShadowShape order_points(const struct PolyPoint **pa, const struct PolyPoint **pb, const struct PolyPoint **pc)
{
    const struct PolyPoint *a = *pa, *b = *pb, *c = *pc;
    // rotations of the three points (the winding stays)
    const struct PolyPoint *const abc[3] = {a, b, c}, *const bca[3] = {b, c, a}, *const cab[3] = {c, a, b};
    const struct PolyPoint *const *order;
    enum ShadowShape shape;
    if (a->Y == b->Y) {
        if (a->Y == c->Y)
            return Shape_None;
        if (a->Y >= c->Y) {
            if (a->X <= b->X)
                return Shape_None;
            order = cab; shape = Shape_FlatBottom;
        } else {
            if (b->X <= a->X)
                return Shape_None;
            order = abc; shape = Shape_FlatTop;
        }
    } else if (a->Y > b->Y) {
        if (a->Y == c->Y) {
            if (c->X <= a->X)
                return Shape_None;
            order = bca; shape = Shape_FlatBottom;
        } else if (a->Y < c->Y) {
            order = bca; shape = Shape_LongRight;
        } else if (b->Y == c->Y) {
            if (c->X <= b->X)
                return Shape_None;
            order = bca; shape = Shape_FlatTop;
        } else if (b->Y < c->Y) {
            order = bca; shape = Shape_LongLeft;
        } else {
            order = cab; shape = Shape_LongRight;
        }
    } else {
        if (a->Y == c->Y) {
            if (a->X <= c->X)
                return Shape_None;
            order = cab; shape = Shape_FlatTop;
        } else if (a->Y >= c->Y) {
            order = cab; shape = Shape_LongLeft;
        } else if (b->Y == c->Y) {
            if (b->X <= c->X)
                return Shape_None;
            order = abc; shape = Shape_FlatBottom;
        } else if (b->Y <= c->Y) {
            order = abc; shape = Shape_LongLeft;
        } else {
            order = abc; shape = Shape_LongRight;
        }
    }
    *pa = order[0];
    *pb = order[1];
    *pc = order[2];
    return shape;
}

/** Both parts of an edge on one line: the second part is the first, carried on past the bend. */
static void straight(int64_t start[2], int64_t step[2], int64_t from, int64_t per_row, int64_t bend)
{
    start[0] = from;
    step[0] = per_row;
    start[1] = from + bend * per_row;
    step[1] = per_row;
}

/** Sets up a triangle (its points ordered by order_points()); false when it draws nothing. */
static TbBool shadow_setup(struct ShadowTriangle *t, enum ShadowShape shape,
    const struct PolyPoint *a, const struct PolyPoint *b, const struct PolyPoint *c)
{
    t->top = a->Y;
    switch (shape)
    {
    case Shape_LongLeft: {
        // left a..c, right a..b then b..c
        t->height = c->Y - a->Y;
        t->bend = b->Y - a->Y;
        const int64_t step_ac = ((c->X - a->X) << 16) / t->height;
        const int64_t step_ab = ((b->X - a->X) << 16) / t->bend;
        if (step_ab <= step_ac)
            return false;
        // the row through b, the widest: its width and the sprite across it
        const int64_t ratio = (t->bend << 16) / t->height;
        const int64_t width = ((ratio * (a->X - c->X)) >> 16) + (b->X - a->X);
        if (width < 0)
            return false;
        t->row_u_step = (b->U + ((ratio * (a->U - c->U)) >> 16) - a->U) / (width + 1);
        t->row_v_step = (b->V + ((ratio * (a->V - c->V)) >> 16) - a->V) / (width + 1);
        straight(t->xl, t->xl_step, a->X << 16, step_ac, t->bend);
        t->xr[0] = a->X << 16;
        t->xr_step[0] = step_ab;
        t->xr[1] = b->X << 16;
        t->xr_step[1] = ((c->X - b->X) << 16) / (c->Y - b->Y);
        straight(t->u, t->u_step, a->U, (c->U - a->U) / t->height, t->bend);
        straight(t->v, t->v_step, a->V, (c->V - a->V) / t->height, t->bend);
        return true;
    }
    case Shape_LongRight: {
        // left a..c then c..b, right a..b
        t->height = b->Y - a->Y;
        t->bend = c->Y - a->Y;
        const int64_t step_ac = ((c->X - a->X) << 16) / t->bend;
        const int64_t step_ab = ((b->X - a->X) << 16) / t->height;
        if (step_ab <= step_ac)
            return false;
        const int64_t ratio = (t->bend << 16) / t->height;
        const int64_t width = ((ratio * (b->X - a->X)) >> 16) + (a->X - c->X);
        if (width < 0)
            return false;
        t->row_u_step = (a->U + ((ratio * (b->U - a->U)) >> 16) - c->U) / (width + 1);
        t->row_v_step = (a->V + ((ratio * (b->V - a->V)) >> 16) - c->V) / (width + 1);
        t->xl[0] = a->X << 16;
        t->xl_step[0] = step_ac;
        t->xl[1] = c->X << 16;
        t->xl_step[1] = ((b->X - c->X) << 16) / (b->Y - c->Y);
        straight(t->xr, t->xr_step, a->X << 16, step_ab, t->bend);
        // the sprite carries on from where the first part's steps took it, not from c's own u, v
        t->u[0] = a->U;
        t->u_step[0] = (c->U - a->U) / t->bend;
        t->u[1] = a->U + t->bend * t->u_step[0];
        t->u_step[1] = (b->U - c->U) / (b->Y - c->Y);
        t->v[0] = a->V;
        t->v_step[0] = (c->V - a->V) / t->bend;
        t->v[1] = a->V + t->bend * t->v_step[0];
        t->v_step[1] = (b->V - c->V) / (b->Y - c->Y);
        return true;
    }
    case Shape_FlatBottom:
        // left a..c, right a..b
        t->height = c->Y - a->Y;
        t->bend = t->height;
        t->row_u_step = (b->U - c->U) / (b->X - c->X);
        t->row_v_step = (b->V - c->V) / (b->X - c->X);
        straight(t->xl, t->xl_step, a->X << 16, ((c->X - a->X) << 16) / t->height, t->bend);
        straight(t->xr, t->xr_step, a->X << 16, ((b->X - a->X) << 16) / t->height, t->bend);
        straight(t->u, t->u_step, a->U, (c->U - a->U) / t->height, t->bend);
        straight(t->v, t->v_step, a->V, (c->V - a->V) / t->height, t->bend);
        return true;
    case Shape_FlatTop:
        // left a..c, right b..c
        t->height = c->Y - a->Y;
        t->bend = t->height;
        t->row_u_step = (b->U - a->U) / (b->X - a->X);
        t->row_v_step = (b->V - a->V) / (b->X - a->X);
        straight(t->xl, t->xl_step, a->X << 16, ((c->X - a->X) << 16) / t->height, t->bend);
        straight(t->xr, t->xr_step, b->X << 16, ((c->X - b->X) << 16) / t->height, t->bend);
        straight(t->u, t->u_step, a->U, (c->U - a->U) / t->height, t->bend);
        straight(t->v, t->v_step, a->V, (c->V - a->V) / t->height, t->bend);
        return true;
    case Shape_None:
    default:
        return false;
    }
}

/** Draws the rows inside the window. */
static void shadow_draw(const struct ShadowTriangle *t)
{
    const unsigned char *map = vec_map;
    const int64_t first = max(-t->top, 0);
    const int64_t end = min(t->height, vec_window_height - t->top);
    const uint32_t row_u_step = (uint32_t)t->row_u_step;
    const uint32_t row_v_step = (uint32_t)t->row_v_step;
    for (int64_t i = first; i < end; i++)
    {
        const int part = (i < t->bend) ? 0 : 1;
        const int64_t k = (part == 0) ? i : i - t->bend;
        const int64_t left = (t->xl[part] + k * t->xl_step[part]) >> 16;
        int64_t right = (t->xr[part] + k * t->xr_step[part]) >> 16;
        // the sprite: 8 bits of u and of v, with 16 bits below each
        uint32_t u = (uint32_t)(t->u[part] + k * t->u_step[part]);
        uint32_t v = (uint32_t)(t->v[part] + k * t->v_step[part]);
        int64_t x = left;
        if (left < 0)
        {
            if (right <= 0)
                continue;
            u += row_u_step * (uint32_t)-left;
            v += row_v_step * (uint32_t)-left;
            x = 0;
        }
        right = min(right, vec_window_width);
        TbPixel *o = poly_screen + (t->top + i + 1) * vec_screen_width + x;
        for (; x < right; x++, o++)
        {
            if (map[((v >> 8) & 0xFF00) | ((u >> 16) & 0xFF)])
                *o = render_shade(*o, (uint8_t)vec_shade);
            u += row_u_step;
            v += row_v_step;
        }
    }
}

void trig(struct PolyPoint *point_a, struct PolyPoint *point_b, struct PolyPoint *point_c)
{
    // gpu-v2 Phase C.2: creature shadows record into the GPU world frame
    // instead, while one is being built (the vec window is then the frame's
    // window -- see RendererWorldFrameBegin()).
    if (vec_mode == VM_SpriteTranslucent && RendererWorldFrameCapturing()
        && RendererWorldFrameAddShadowTri(point_a, point_b, point_c, vec_map, vec_shade))
        return;
    if (vec_mode != VM_SpriteTranslucent) {
        ERRORLOG("only creature shadows (VM_SpriteTranslucent) are drawn here, not mode %d", (int)vec_mode);
        return;
    }
    if ((vec_map == NULL) || (poly_screen == NULL)) {
        ERRORLOG("no shadow sprite or screen to draw into");
        return;
    }
    // a triangle too large for 16.16 isn't drawn
    {
        const int64_t max_x = max(max(point_a->X, point_b->X), point_c->X);
        const int64_t min_x = min(min(point_a->X, point_b->X), point_c->X);
        const int64_t max_y = max(max(point_a->Y, point_b->Y), point_c->Y);
        const int64_t min_y = min(min(point_a->Y, point_b->Y), point_c->Y);
        if ((max_x - min_x > 32767) || (max_y - min_y > 32767) || (max_x > 32767) || (min_x < -32767)
            || (max_y > 32767) || (min_y < -32767))
            return;
    }
    const struct PolyPoint *a = point_a, *b = point_b, *c = point_c;
    const enum ShadowShape shape = order_points(&a, &b, &c);
    if ((shape == Shape_None) || (a->Y >= vec_window_height))
        return;
    struct ShadowTriangle t;
    if (shadow_setup(&t, shape, a, b, c))
        shadow_draw(&t);
}
/******************************************************************************/
