#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * docs/refactor/editor/phase5/08-script-command-browser.md -- builds the
 * Script > Commands catalogue from the engine's real command/value tables
 * (proving the table walks terminate and the catalogue isn't empty),
 * reports any command that fell into the "Other" group, and inserts a
 * command into the session script text through the same path the window's
 * Insert button uses.
 */
TbBool ftest_editor_script_commands_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
