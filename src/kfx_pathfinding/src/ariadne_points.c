/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file ariadne_points.c
 *     ariadne_points support functions.
 * @par Purpose:
 *     Functions to ariadne_points.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     11 Mar 2010 - 22 Jun 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "ariadne_saved_state.h"
#include "ariadne_points.h"

#include "globals.h"
#include "bflib_basics.h"
#include "ports/ui_port.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/******************************************************************************/
static int64_t count_Points;
static int64_t ix_Points;
static int64_t free_Points;

struct Point ari_Points[POINTS_COUNT];
/******************************************************************************/

static AridPointId point_new(void)
{
    AridPointId i;
    if (free_Points == -1)
    {
        i = ix_Points;
        if ((i < 0) || (i >= POINTS_COUNT))
        {
            WARNLOG("ix_Points overflow; %" PRId64 " allocated, id %" PRId64 " outranged",(int64_t)count_Points,(int64_t)ix_Points);
            ui_report_error_stat(ESE_NoFreePathPts);
            return -1;
        }
        ix_Points++;
    } else
    {
        i = free_Points;
        if ((i < 0) || (i >= POINTS_COUNT))
        {
            ERRORDBG(13,"free_Points overflow; %" PRId64 " allocated, id %" PRId64 " outranged",(int64_t)count_Points,(int64_t)free_Points);
            ui_report_error_stat(ESE_NoFreePathPts);
            return -1;
        }
        free_Points = ari_Points[i].x;
    }
    ari_Points[i].y = 0;
    count_Points++;
    return i;
}

void point_dispose(AridPointId pt_id)
{
    AridPointId last_pt_id = free_Points;
    ari_Points[pt_id].y = 0x8000;
    free_Points = pt_id;
    ari_Points[pt_id].x = last_pt_id;
    count_Points--;
}

TbBool point_set(AridPointId pt_id, int64_t x, int64_t y)
{
    if ((pt_id < 0) || (pt_id >= POINTS_COUNT))
    {
        return false;
    }
    ari_Points[pt_id].x = x;
    ari_Points[pt_id].y = y;
    return true;
}

struct Point *point_get(AridPointId pt_id)
{
    if ((pt_id < 0) || (pt_id >= POINTS_COUNT))
    {
        return INVALID_POINT;
    }
    return &ari_Points[pt_id];
}

TbBool point_is_invalid(const struct Point *pt)
{
    return (pt < &ari_Points[0]) || (pt > &ari_Points[POINTS_COUNT-1]) || (pt == INVALID_POINT) || (pt == NULL);
}

TbBool point_equals(AridPointId pt_idx, int64_t pt_x, int64_t pt_y)
{
    if ((pt_idx < 0) || (pt_idx >= POINTS_COUNT))
        return false;
    int64_t tip_x = ari_Points[pt_idx].x;
    int64_t tip_y = ari_Points[pt_idx].y;
    if ((tip_x != pt_x) || (tip_y != pt_y))
        return false;
    return true;
}

AridPointId allocated_point_search(int64_t pt_x, int64_t pt_y)
{
    if (pt_y == 0x8000) {
        return -1;
    }
    for (AridPointId pt_idx = 0; pt_idx < POINTS_COUNT; pt_idx++)
    {
        int64_t tip_x = ari_Points[pt_idx].x;
        int64_t tip_y = ari_Points[pt_idx].y;
        if ((tip_x == pt_x) && (tip_y == pt_y)) {
            return pt_idx;
        }
    }
    return -1;
}

AridPointId point_set_new_or_reuse(int64_t pt_x, int64_t pt_y)
{
    AridPointId pt_idx = allocated_point_search(pt_x, pt_y);
    if (pt_idx >= 0) {
        return pt_idx;
    }
    pt_idx = point_new();
    if (pt_idx < 0) {
        return -1;
    }
    point_set(pt_idx, pt_x, pt_y);
    return pt_idx;
}

void triangulation_initxy_points(int64_t startx, int64_t starty, int64_t endx, int64_t endy)
{
    for (int64_t i = 0; i < POINTS_COUNT; i++)
    {
        struct Point* pt = &ari_Points[i];
        pt->x = 0; // not the previous level's (P5-F22): the mesh is saved and resynced
        pt->y = 0x8000;
    }
    ari_Points[0].x = startx;
    ari_Points[0].y = starty;
    ari_Points[1].x = endx;
    ari_Points[1].y = starty;
    ari_Points[2].x = endx;
    ari_Points[2].y = endy;
    ari_Points[3].x = startx;
    ari_Points[3].y = endy;
    ix_Points = 4;
    count_Points = 4;
    free_Points = -1;
}
/******************************************************************************/
/** The points and their allocator: part of the saved navigation mesh (ariadne_saved_state.h). */
void ariadne_points_visit_saved_state(AriadneStateVisitor visit, void *ctx)
{
    visit(ctx, ari_Points, sizeof(ari_Points));
    visit(ctx, &count_Points, sizeof(count_Points));
    visit(ctx, &ix_Points, sizeof(ix_Points));
    visit(ctx, &free_Points, sizeof(free_Points));
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
