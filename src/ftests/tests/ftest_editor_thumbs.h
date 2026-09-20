#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * docs/refactor/editor/phase6/01-thumbnails-owner-icons-classic-hud.md --
 * builds the toolbox's slab and object thumbnails from the real texture and
 * sprite memory of a running level and reports which items have none.
 * Asserts the common cases work (ROCK's wall front, a path's top, several
 * objects) so a change to the block layout or sprite decode is caught.
 */
TbBool ftest_editor_thumbs_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
