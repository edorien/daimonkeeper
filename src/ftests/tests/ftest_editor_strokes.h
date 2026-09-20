#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * docs/refactor/editor/fx-plans/00-audit-and-index.md items A3, A6, A7 --
 * stroke-level undo (snapshot, change slabs, journal, undo, redo), the
 * reinforce-perimeter tool, and the script validator run over the real
 * script of the level under test (no errors expected).
 */
TbBool ftest_editor_strokes_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
