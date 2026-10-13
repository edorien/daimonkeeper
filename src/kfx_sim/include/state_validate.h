/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file state_validate.h
 *     Whether a simulation state that came from outside can be used without leaving its arrays.
 * @par Purpose:
 *     A saved game, and the full resync a host sends, replace kfx_sim_state wholesale with bytes from a file or a
 *     peer. The game then follows the links in it as it always has: a thing's neighbour on the map, a room's first
 *     slab, a creature's lair, a column's cubes. Every one of those is an index, and one outside its array reads or
 *     writes outside the state; a link that leads back to where it started makes a loop that never ends.
 *     sim_state_validate() checks a state that is not live yet and says which field is wrong, so the load can be
 *     refused with the running game untouched (security pass S06, docs/security-pass).
 * @par Comment:
 *     The bounds are the sizes of the arrays (compile-time), not the loaded configuration: the question is whether
 *     the state stays inside its memory, which does not depend on which campaign is running. A value that is in an
 *     array but names something the level does not have points at an empty entry.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_STATE_VALIDATE_H
#define DK_STATE_VALIDATE_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

struct KfxSimState;

/**
 * Checks every index, link, count and enumerated value of the state that the game uses to reach other memory.
 * `state` is not changed and need not be the live one. On failure `why` (if given) holds the first thing found
 * wrong, e.g. "things_data[132].next_on_mapblk = 99999 is outside 0..12287".
 * @return true if the state is safe to use.
 */
TbBool sim_state_validate(const struct KfxSimState *state, char *why, size_t why_size);

/**
 * For calibrating the bounds against real states: runs every check without stopping at the first, and writes one
 * line per kind of violation ("field xCOUNT (first at [index] = value)") into `report`.
 * @return the number of kinds of violation (0 for a state that passes).
 */
int64_t sim_state_survey(const struct KfxSimState *state, char *report, size_t report_size);

#ifdef __cplusplus
}
#endif

#endif
