/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_texture_paint.h
 *     Per-slab texture-set painting without the UI: one slab, a rectangle, or a
 *     fill (fx-plans/00-audit-and-index.md A14).
 */
#ifndef DK_EDITOR_TEXTURE_PAINT_H
#define DK_EDITOR_TEXTURE_PAINT_H

#include "globals.h"

void editor_texture_paint_slab(MapSlabCoord x, MapSlabCoord y, unsigned char pack);

/** Every slab of the inclusive box. */
void editor_texture_paint_rect(MapSlabCoord x0, MapSlabCoord y0, MapSlabCoord x1, MapSlabCoord y1, unsigned char pack);

/** Repaints the connected area (4-way) of slabs that have the same kind and the same current texture as the
 *  seed. Returns how many slabs changed. */
int editor_texture_paint_fill(MapSlabCoord x, MapSlabCoord y, unsigned char pack);

#endif
