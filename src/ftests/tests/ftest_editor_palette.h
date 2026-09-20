#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * docs/refactor/editor/phase6/00-toolbox-icon-grids.md -- checks how the
 * toolbox palette sorts the real config: every slab kind lands in exactly
 * one of Terrain / Rooms / Other with each room's floor slab under Rooms,
 * and objects split into Spells / Specials / Crates / Decor with every
 * spellbook resolving to a power. Also logs the group sizes.
 */
TbBool ftest_editor_palette_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
