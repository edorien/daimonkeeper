/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_mappack.h
 *     The "Editor Maps" mappack: where a map made in the editor is saved by
 *     default (levels/editormaps/, described by levels/editormaps.cfg), so it
 *     shows up in Free Play without editing any campaign.
 */
#ifndef DK_EDITOR_MAPPACK_H
#define DK_EDITOR_MAPPACK_H

#include "globals.h"
#include <string>

/** Folder of the Editor Maps mappack (not created). */
std::string editor_maps_dir(void);

/** Smallest level number >= 1 with no map%05d.slb in `dir`. */
LevelNumber editor_maps_next_free_number(const char *dir);

/** Creates the folder. True if it exists afterwards. */
bool editor_maps_ensure_dir(void);

/** Called after a save into the Editor Maps folder: writes levels/editormaps.cfg if it is missing and
 *  makes the game re-scan the mappack list so the pack can be opened at once. */
void editor_maps_register(void);

/** File name of the Editor Maps mappack ("editormaps.cfg"), as the mappack list knows it. */
const char *editor_maps_pack_fname(void);

/** True if `dir` is the Editor Maps folder. */
bool editor_maps_is_dir(const char *dir);

#endif
