/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file room_garden.c
 *     Hatchery room maintain functions.
 * @par Purpose:
 *     Functions to create and use garden rooms.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     07 Apr 2011 - 19 Nov 2012
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "room_garden.h"

#include "globals.h"
#include "bflib_basics.h"
#include "room_data.h"
#include "player_data.h"
#include "dungeon_data.h"
#include "thing_data.h"
#include "thing_physics.h"

#include "kfx_config_state.h"
#include "map_data.h"
#include "slab_data.h"
#include "kfx_sim_state.h"
#include "thing_objects.h"
#include "list_walk.h"
#include "room_util.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

/******************************************************************************/
#ifdef __cplusplus
}
#endif
/******************************************************************************/
TbBool remove_food_from_food_room_if_possible(struct Thing *thing)
{
    struct Room *room;
    if ( thing->owner == kfx_config_state.neutral_player_num )
    {
        return false;
    }
    if ( thing->food.life_remaining != -1 )
    {
        return false;
    }
    room = get_room_thing_is_on(thing);
    if ( room_is_invalid(room) || !room_role_matches(room->kind,RoRoF_FoodStorage) || room->owner != thing->owner )
    {
        return false;
    }
    if ( room->used_capacity > 0 )
    {
        room->used_capacity--;
    }
    thing->food.life_remaining = kfx_config_state.conf.rules[thing->owner].gameplay.food_life_out_of_hatchery;
    thing->parent_idx = -1;
    return true;
}

int64_t room_grow_food(struct Room *room)
{
    if (room->slabs_count < 1)
    {
        ERRORLOG("Room %s index %" PRId64 " has no slabs",room_code_name(room->kind),(int64_t)room->index);
        return 0;
    }
    if (room->used_capacity > room->total_capacity)
    {
        ERRORLOG("Room %s index %" PRId64 " has too much used capacity: %" PRId64 "/%" PRId64, room_code_name(room->kind), (int64_t)room->index, (int64_t)(room->used_capacity), (int64_t)(room->total_capacity));
        count_food_in_room(room);
    }
    if ((room->used_capacity >= room->total_capacity)
      || get_gameturn() % ((kfx_config_state.conf.rules[room->owner].rooms.food_generation_speed / room->total_capacity) + 1))
    {
        return 0;
    }
    struct RoomConfigStats* roomst = get_room_kind_stats(room->kind);
    uint64_t k;
    int64_t n = PLAYER_RANDOM(room->owner, room->slabs_count);
    SlabCodedCoords slbnum = room->slabs_list;
    for (k = n; k > 0; k--)
    {
        if (slbnum == 0)
            break;
        slbnum = get_next_slab_number_in_room(slbnum);
    }
    if (slbnum == 0) {
        ERRORLOG("Taking random slab (%" PRId64 "/%" PRId64 ") in %s index %" PRId64 " failed - internal inconsistency",(int64_t)n,(int64_t)room->slabs_count,room_code_name(room->kind),(int64_t)room->index);
        slbnum = room->slabs_list;
    }
    for (k = 0; k < room->slabs_count; k++)
    {
        MapSlabCoord slb_x = slb_num_decode_x(slbnum);
        MapSlabCoord slb_y = slb_num_decode_y(slbnum);

        int64_t m = PLAYER_RANDOM(room->owner, STL_PER_SLB * STL_PER_SLB);
        for (int64_t i = 0; i < STL_PER_SLB * STL_PER_SLB; i++)
        {
            MapSubtlCoord stl_x = slab_subtile(slb_x, m % STL_PER_SLB);
            MapSubtlCoord stl_y = slab_subtile(slb_y, m / STL_PER_SLB);
            // Check if there is a food object already
            struct Thing* thing = find_base_thing_on_mapwho(TCls_Object, ObjMdl_ChickenGrowing, stl_x, stl_y);
            if (thing_is_invalid(thing)) {
                thing = find_base_thing_on_mapwho(TCls_Object, ObjMdl_StatueLit, stl_x, stl_y);
            }
            if (thing_is_invalid(thing))
            {
                if ((roomst->storage_height < 0) || (get_floor_filled_subtiles_at(stl_x, stl_y) == roomst->storage_height))
                {
                    return room_create_new_food_at(room, stl_x, stl_y);
                }
            }
            m = (m + 1) % (STL_PER_SLB*STL_PER_SLB);
        }

        slbnum = get_next_slab_number_in_room(slbnum);
        if (slbnum == 0) {
            slbnum = room->slabs_list;
        }
    }
    ERRORLOG("Could not find valid RANDOM point in room %s index %" PRId64,room_code_name(room->kind),(int64_t)room->index);
    return false;
}

static TbBool garden_stores(const struct Room *room, const struct Thing *thing)
{
    return thing_is_object(thing) && (object_is_infant_food(thing) || object_is_growing_food(thing) || object_is_mature_food(thing));
}

static void garden_take_out(struct Room *room, struct Thing *thing, struct RoomReposition *rrepos)
{
    if (!store_reposition_entry(rrepos, thing->model)) {
        WARNLOG("Too many things to reposition in %s.",room_code_name(room->kind));
    }
    destroy_object(thing);
}

static struct Thing *garden_put_back(struct Room *room, ThingModel model, CrtrExpLevel exp_level, MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{
    struct Coord3d pos;
    pos.x.val = subtile_coord_center(stl_x);
    pos.y.val = subtile_coord_center(stl_y);
    pos.z.val = 0;
    return create_object(&pos, model, room->owner, -1);
}

/** Food: nothing sinks into the floor, capacity_used_for_storage follows the count, two sweeps. */
static const struct RoomStorageKind garden_storage = { "food", garden_stores, garden_take_out, garden_put_back, NULL, NULL, 0, 1, 0 };

void count_food_in_room(struct Room *room)
{
    room_storage_recount(&garden_storage, room);
}

TbBool room_create_new_food_at(struct Room *room, MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{
    struct Coord3d pos;
    pos.x.val = subtile_coord_center(stl_x);
    pos.y.val = subtile_coord_center(stl_y);
    pos.z.val = 0;
    struct Thing* foodtng = create_object(&pos, ObjMdl_ChickenGrowing, room->owner, -1);
    if (thing_is_invalid(foodtng))
    {
        ERRORLOG("Cannot Create Food!");
        return false;
    }
    foodtng->mappos.z.val = get_thing_height_at(foodtng, &foodtng->mappos);
    if (thing_in_wall_at(foodtng, &foodtng->mappos)) {
        ERRORLOG("Created chicken in a wall");
    }
    int64_t required_cap = get_required_room_capacity_for_object(RoRoF_FoodStorage, foodtng->model, 0);
    room->used_capacity += required_cap;
    foodtng->food.life_remaining = (foodtng->max_frames << 8) / foodtng->anim_speed - 1;
    return true;
}
/******************************************************************************/
