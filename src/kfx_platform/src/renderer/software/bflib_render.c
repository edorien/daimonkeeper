/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_render.c
 *     Rendering the 3D view elements functions.
 * @par Purpose:
 *     Functions for rendering 3D elements.
 * @par Comment:
 *     Go away from here, you bad optimizer! Do not compile this with optimizations.
 * @author   Tomasz Lis
 * @date     20 Mar 2009 - 30 Mar 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "bflib_render.h"

#include "globals.h"
#include "bflib_video.h"
#include "post_inc.h"

/******************************************************************************/
/* Was `TbPixel vec_colour = 112;` -- a palette-index literal. 112's actual
 * rendered colour under the shipped palette is not yet resolved (needs
 * checking against data/palette.dat, same "resolve once, hardcode the RGB
 * result" treatment as docs/refactor/renderer/02a-pixel-format-design.md §4's
 * other literal-array sites) -- flagged, not silently reinterpreted. Kept as
 * a placeholder RGB for now so the type compiles; every real call site sets
 * vec_colour explicitly before use (confirmed via grep), so this initial
 * value is very unlikely to ever actually render. */
TbPixel vec_colour = { 112, 112, 112, 255 };
int64_t vec_shade = 0;
unsigned char vec_mode;
struct PolyPoint *polyscans = NULL;
/******************************************************************************/

void setup_bflib_render()
{
    polyscans = malloc(sizeof(struct PolyPoint) * 4096);
    memset(polyscans, 0, sizeof(struct PolyPoint) * 4096);
}

void reset_bflib_render()
{
    memset(polyscans, 0, sizeof(struct PolyPoint) * 4096);
}

void finish_bflib_render()
{
    if (polyscans)
    {
        free(polyscans);
        polyscans = NULL;
    }
}
/******************************************************************************/
