/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file player_computer_state.c
 *     Accessors for the computer players' state.
 * @par Purpose:
 *     The few functions over kfx_sim_state.computer[] that kfx_sim needs
 *     itself: moved from player_computer.c and player_comptask.c when the
 *     AI became kfx_ai in refactor pass 2 (S14,
 *     docs/refactor-pass2/stage-14-ai-library-spike.md). They only read or
 *     record state; the AI's decisions stay in kfx_ai.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     10 Mar 2009 - 20 Mar 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "player_computer_types.h"

#include "globals.h"
#include "bflib_basics.h"
#include "dungeon_data.h"
#include "thing_data.h"
#include "kfx_sim_state.h"
#include "post_inc.h"

/******************************************************************************/
struct Computer2 *get_computer_player_f(int64_t plyr_idx,const char *func_name)
{
    if ((plyr_idx >= 0) && (plyr_idx < PLAYERS_COUNT))
        return &kfx_sim_state.computer[plyr_idx];
    ERRORMSG("%s: Tried to get non-existing computer player %" PRId64 "!",func_name,(int64_t)plyr_idx);
    return INVALID_COMPUTER_PLAYER;
}

TbBool computer_player_invalid(const struct Computer2 *comp)
{
    if (comp == INVALID_COMPUTER_PLAYER)
        return true;
    return (comp < &kfx_sim_state.computer[0]);
}

struct Dungeon *computer_dungeon(const struct Computer2 *comp)
{
    if ((comp->dungeon_idx < 0) || (comp->dungeon_idx >= DUNGEONS_COUNT))
        return INVALID_DUNGEON;
    return &kfx_sim_state.dungeon[comp->dungeon_idx];
}

/** Checks if given thing is placed in power hand of given player.
 *
 * @param thing
 * @param plyr_idx
 * @return
 */
TbBool thing_is_in_computer_power_hand_list(const struct Thing *thing, PlayerNumber plyr_idx)
{
    struct Computer2 *comp;
    comp = get_computer_player(plyr_idx);
    return (comp->held_thing_idx == thing->index);
}

int64_t find_trap_location_index(const struct Computer2 * comp, const struct Coord3d * coord)
{
    const struct Coord3d * location;
    MapSlabCoord slb_x;
    MapSlabCoord slb_y;
    int64_t i;
    slb_x = subtile_slab(coord->x.stl.num);
    slb_y = subtile_slab(coord->y.stl.num);
    for (i=0; i < COMPUTER_TRAP_LOC_COUNT; i++)
    {
        location = &comp->trap_locations[i];
        if ((subtile_slab(location->x.stl.num) == slb_x) && (subtile_slab(location->y.stl.num) == slb_y)) {
            return i;
        }
    }
    return -1;
}

/** Add a position to the computer player's list of potential trap locations (the trap_locations[] array). */
int64_t add_to_trap_locations(struct Computer2 * comp, struct Coord3d * coord)
{
    SYNCDBG(6,"Starting");
    // Avoid duplicating entries
    if (find_trap_location_index(comp, coord) >= 0) {
        return false;
    }
    struct Coord3d * location;
    int64_t i;
    // Find a free place and add the location
    for (i=0; i < COMPUTER_TRAP_LOC_COUNT; i++)
    {
        location = &comp->trap_locations[i];
        if ((location->x.val <= 0) && (location->y.val <= 0)) {
            location->x.val = coord->x.val;
            location->y.val = coord->y.val;
            location->z.val = coord->z.val;
            return true;
        }
    }
    SYNCDBG(7,"No free location");
    return false;
}
