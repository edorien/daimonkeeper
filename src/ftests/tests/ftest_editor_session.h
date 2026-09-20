#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * docs/refactor/editor/fx-plans/00-audit-and-index.md items B2 and B4 --
 * (1) the frozen-sim invariant: with the editor open the game turn and a
 * checksum of the map and thing counts do not change over several hundred
 * frames; (2) stock-map round trip: each shipped keeporig map is loaded,
 * saved in the classic format through editor_save_map() and reloaded, and
 * its slab kinds and owners must come back identical.
 */
TbBool ftest_editor_session_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
