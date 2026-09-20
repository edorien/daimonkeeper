/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file level_script_override.h
 *     Header file for level_script_override.c.
 * @par Purpose:
 *     docs/refactor/skirmish/ (S3) -- an in-memory, one-shot replacement for a
 *     level's map%05u.txt script, installed by the Skirmish setup tab
 *     (kfx_frontend) and consumed by the script loader (kfx_game, lvl_script.c).
 *     It has two parts, built by kfx_config's script_setup_build_override():
 *       prelude -- v1-syntax setup commands, scanned first at forced version 1;
 *       masked  -- the original script with the tab-owned lines blanked,
 *                  scanned in place of the file, at the file's own version.
 *     Lives in kfx_sim beside load_single_map_file_to_buffer() so both the
 *     frontend (above) and the loader (above) can reach it directly.
 *     The override is keyed on the level number and is ONE-SHOT: the loader
 *     clears it once the level's script has been executed, and a stale one
 *     (different level number) is discarded, so it cannot leak into a later
 *     campaign / free-play / network level.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_LEVEL_SCRIPT_OVERRIDE_H
#define DK_LEVEL_SCRIPT_OVERRIDE_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// Copies both strings. Replaces any previous override.
void level_script_override_set(LevelNumber lvnum, const char *prelude, const char *masked);
void level_script_override_clear(void);
// Any override installed at all (for any level)?
TbBool level_script_override_is_set(void);
// The level number the installed override is for (0 when nothing is installed).
LevelNumber level_script_override_level(void);
// Installed for exactly this level number?
TbBool level_script_override_matches(LevelNumber lvnum);
// NULL when nothing is installed.
const char *level_script_override_prelude(void);
const char *level_script_override_masked(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif

#endif
