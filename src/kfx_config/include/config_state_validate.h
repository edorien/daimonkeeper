/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_state_validate.h
 *     Whether a configuration state that came from outside can be used without leaving its tables.
 * @par Purpose:
 *     The saved part of kfx_config_state (KFX_CONFIG_STATE_SAVED_*) holds the level's configuration as its
 *     files and script left it: the tables of slabs, rooms, spells, creatures, objects, traps, doors... Their
 *     fields choose what the game does next -- an index into a table of functions, a count of entries, a
 *     texture, a column -- and a saved game or a full resync supplies all of it (security pass S06, S06b).
 *     config_state_validate() checks that every such field names something that exists, so a load can be
 *     refused with the running game untouched; the same check runs on the configuration a level has just
 *     loaded from its files, since a downloaded map pack's configuration files are no more trusted than a save.
 * @par Comment:
 *     The bounds are the sizes of the tables and of the function lists they select from (config_funcnames.h),
 *     not the contents of any campaign.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_CONFIG_STATE_VALIDATE_H
#define DK_CONFIG_STATE_VALIDATE_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

struct KfxConfigState;

/**
 * Checks the saved part of `state` (everything before atmos_sound_frequency; the rest is not read, so a buffer of
 * KFX_CONFIG_STATE_SAVED_LEN bytes laid out like the struct is enough, as long as it is aligned for it).
 * `state` is not changed and need not be the live one. On failure `why` (if given) says which field is wrong.
 * @return true if the state is safe to use.
 */
TbBool config_state_validate(const struct KfxConfigState *state, char *why, size_t why_size);

/**
 * For calibrating the bounds against real configurations: runs every check without stopping at the first, and
 * writes one line per kind of violation into `report`.
 * @return the number of kinds of violation (0 for a state that passes).
 */
int64_t config_state_survey(const struct KfxConfigState *state, char *report, size_t report_size);

/**
 * For a configuration that was just loaded from files or changed by a level's script: every value outside what the
 * tables can take is reset (to 0, "none", where that is a valid value, else to the nearest valid one) and logged, so
 * the game goes on with the level instead of reading beyond a table. A save or a resync with such values is refused
 * by config_state_validate() instead -- nothing legitimate writes them.
 * @return the number of kinds of value that were reset (0 for a configuration that was fine).
 */
int64_t config_state_repair(struct KfxConfigState *state);

#ifdef __cplusplus
}
#endif

#endif
