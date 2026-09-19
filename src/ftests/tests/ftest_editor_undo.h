#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * docs/refactor/editor/09-toolbox-remainder.md's own suggested `editor_undo`
 * ftest -- extends undo/redo coverage beyond ftest_editor_place_creature.c's
 * own creature-only case to a trap: place one (PckA_EditorRedoTrap -- same
 * explicit-position trick that test's own comment describes, not the
 * ambient-position PckA_EditorPlaceTrap a real toolbox click sends), assert
 * it exists, undo it (PckA_EditorUndo), assert it's gone, then redo it
 * (resending PckA_EditorRedoTrap with the same params -- the same action
 * editor_journal.cpp's own Ctrl+Y handler would send) and assert it's back
 * at the same position with the same model/owner.
 */
TbBool ftest_editor_undo_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
