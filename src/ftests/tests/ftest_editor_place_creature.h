#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * docs/refactor/editor/02-editing-toolbox.md §6's own suggested
 * `editor_place_creature` ftest: open an editor session on an already-
 * loaded level, place a creature through the editor's real placement path
 * (PckA_EditorRedoCreature -- see the .c file's own comment for why this
 * verb rather than PckA_CheatMakeCreature), assert it exists with the
 * right owner/experience, then undo it (PckA_EditorUndo) and assert it's
 * gone.
 */
TbBool ftest_editor_place_creature_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
