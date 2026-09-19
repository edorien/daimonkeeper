#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * docs/refactor/editor/09-toolbox-remainder.md's own suggested
 * `editor_paint_terrain` ftest: open an editor session, paint a small
 * rectangle of a chosen slab kind via PckA_EditorPlaceTerrainRect, and
 * assert every slab in that rectangle got the right kind and owner.
 *
 * See the .c file's own comment for why the rectangle's "release corner"
 * is read back from the packet's own ambient pos_x/pos_y rather than
 * chosen directly -- this verb (like every terrain-paint/fill/rect-op
 * verb, unlike the editor's own thing-placement verbs) has no
 * fully-explicit-position packet shape to fall back on.
 */
TbBool ftest_editor_paint_terrain_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
