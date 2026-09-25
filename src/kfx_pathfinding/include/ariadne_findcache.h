/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file ariadne_findcache.h
 *     Header file for ariadne_findcache.c.
 * @par Purpose:
 *     FindCache support functions for Ariadne pathfinding.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     11 Mar 2010 - 05 Aug 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_ARIADNE_FINDCACHE_H
#define DK_ARIADNE_FINDCACHE_H

#include "globals.h"
#include "bflib_basics.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
#pragma pack(1)


#pragma pack()
/******************************************************************************/

void triangulation_init_cache(int64_t tri_idx);
int64_t triangle_brute_find8_near(int64_t pos_x, int64_t pos_y);

int64_t triangle_find8(int64_t pt_x, int64_t pt_y);
TbBool point_find(int64_t pt_x, int64_t pt_y, int64_t *out_tri_idx, int64_t *out_cor_idx);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
