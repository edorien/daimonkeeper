#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * docs/refactor/editor/phase5/06-slices6-8-points-tool.md -- proves the
 * Points tool's create/delete/undo/redo paths and its save/reload
 * round-trip through the real engine: creates a static light, an action
 * point and an effect generator, undoes and redoes all three through the
 * journal, saves the level (KFX-native) to a scratch level number, reloads
 * it, and asserts all three come back with their position/size -- and that
 * the number of *level-owned* lights is unchanged across the round trip
 * (a thing's own light saved as a second, level-owned copy would make it
 * grow on every save/reload cycle).
 */
TbBool ftest_editor_points_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
