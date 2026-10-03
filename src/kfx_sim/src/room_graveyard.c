/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file room_graveyard.c
 *     Graveyard room maintain functions.
 * @par Purpose:
 *     Functions to create and use graveyards.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     01 Feb 2012 - 01 Jul 2012
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "room_graveyard.h"

#include "globals.h"
#include "bflib_basics.h"
#include "room_data.h"
#include "player_data.h"
#include "dungeon_data.h"
#include "thing_corpses.h"
#include "thing_data.h"
#include "thing_physics.h"
#include "thing_stats.h"
#include "config_terrain.h"

#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "list_walk.h"
#include "room_util.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

/******************************************************************************/
/**
 * Adds a corpse to the graveyard room capacity.
 * @param corpse The dead creature thing to be added.
 * @param room The graveyard room.
 * @return Gives true on success.
 */
TbBool add_body_to_graveyard(struct Thing *deadtng, struct Room *room)
{
    if (room->total_capacity <= room->used_capacity)
    {
        ERRORLOG("The %s has no space for another corpse",room_code_name(room->kind));
        return false;
    }
    if (corpse_laid_to_rest(deadtng))
    {
        ERRORLOG("The %s is already decomposing in %s",thing_model_name(deadtng),room_role_code_name(RoRoF_DeadStorage));
        return false;
    }
    room->used_capacity++;
    deadtng->corpse.laid_to_rest = 1;
    deadtng->health = kfx_config_state.conf.rules[room->owner].rooms.graveyard_convert_time;
    return true;
}

static TbBool graveyard_stores(const struct Room *room, const struct Thing *thing)
{
    return corpse_laid_to_rest(thing);
}

static void graveyard_take_out(struct Room *room, struct Thing *thing, struct RoomReposition *rrepos)
{
    struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
    if (!store_creature_reposition_entry(rrepos, thing->model, cctrl->exp_level)) {
        WARNLOG("Too many things to reposition in %s.",room_code_name(room->kind));
    }
    delete_thing_structure(thing, 0);
}

static struct Thing *graveyard_put_back(struct Room *room, ThingModel model, CrtrExpLevel exp_level, MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{
    struct Coord3d pos;
    pos.x.val = subtile_coord_center(stl_x);
    pos.y.val = subtile_coord_center(stl_y);
    pos.z.val = 0;
    struct Thing* bodytng = create_dead_creature(&pos, model, DCrSt_Dying, room->owner, exp_level);
    if (!thing_is_invalid(bodytng))
    {
        bodytng->corpse.laid_to_rest = 1;
        bodytng->health = kfx_config_state.conf.rules[room->owner].rooms.graveyard_convert_time;
    }
    return bodytng;
}

/** Bodies: capacity_used_for_storage isn't touched, and the second sweep only runs while bodies wait for space. */
static const struct RoomStorageKind graveyard_storage = { "bodies", graveyard_stores, graveyard_take_out, graveyard_put_back, NULL, NULL, 0, 0, 1 };

void count_bodies_in_room(struct Room *room)
{
    room_storage_recount(&graveyard_storage, room);
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
