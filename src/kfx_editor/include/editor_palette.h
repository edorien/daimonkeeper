/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_palette.h
 *     Header file for editor_palette.cpp.
 * @par Purpose:
 *     docs/refactor/editor/phase6/00-toolbox-icon-grids.md -- how the toolbox
 *     sorts the game's slab kinds and object models into its palette tabs
 *     (Terrain / Rooms / Other; Spells / Specials / Traps & Doors / Decor).
 *     Read from the live config, so modded kinds are classified by what they
 *     are, not by name. Internal to kfx_editor.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_EDITOR_PALETTE_H
#define DK_EDITOR_PALETTE_H

#include "globals.h"

enum EditorTerrainGroup
{
    ETG_Terrain = 0, // rock, earth, gold, gems, paths, water, lava, claimed walls...
    ETG_Rooms,       // the floor slab of each room (portal, heart, treasure, ...)
    ETG_Walls,       // each room's wall slab (shown with the room's icon)
    ETG_Other,       // doors and other obstacles
    ETG_Count
};

enum EditorObjectGroup
{
    EOG_Spells = 0,  // spellbooks
    EOG_Specials,    // dungeon special boxes
    EOG_Crates,      // workshop boxes (a trap / door in a crate) -- shown with Traps & Doors
    EOG_Decor,       // everything else: torches, furniture, gold, food, statues, hero gates...
    EOG_Count
};

#ifdef __cplusplus
extern "C" {
#endif

// Room whose floor is slab `kind`, or 0 if it is not a room slab.
RoomKind editor_room_of_slab(SlabKind kind);

// Door model whose closed/open slab is `kind`, or 0 (the door slabs' columns
// are empty -- the door is a thing -- so the toolbox shows the door's
// workshop icon for them instead of a texture).
int64_t editor_door_of_slab(SlabKind kind);

// Room a wall slab belongs to (the room whose floor slab shares its SlbID --
// how the config pairs TREASURY_AREA with TREASURY_WALL), or 0.
RoomKind editor_room_of_wall(SlabKind kind);

int64_t editor_terrain_group_of(SlabKind kind);
int64_t editor_object_group_of(ThingModel model);

// The power a spellbook object teaches, or -1.
int64_t editor_spellbook_power(ThingModel model);

#ifdef __cplusplus
}
#endif

#endif
