#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * The editor's Stamp, right-click delete and Query logic, tested without the
 * UI (editor_brush / editor_things / editor_query): capture an area holding
 * slabs, a creature, a trap and a light; stamp it elsewhere; undo; delete a
 * thing at a position and undo; query a position.
 */
TbBool ftest_editor_brush_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
