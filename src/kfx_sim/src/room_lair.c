/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file room_lair.c
 *     Lair room and creature lairs maintain functions.
 * @par Purpose:
 *     Functions to create and use lairs.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     07 Apr 2011 - 20 Nov 2012
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "room_lair.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_planar.h"
#include "room_data.h"
#include "player_data.h"
#include "dungeon_data.h"
#include "thing_data.h"
#include "thing_navigate.h"
#include "creature_control.h"
#include "config_creature.h"
#include "sim_scratch.h"
#include "thing_effects.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "thing_objects.h"
#include "thing_stats.h"
#include "list_walk.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
#ifdef __cplusplus
}
#endif
/******************************************************************************/
struct Room *start_rooms;
struct Room *end_rooms;
/******************************************************************************/
int64_t calculate_free_lair_space(struct Dungeon * dungeon)
{
    SYNCDBG(9,"Starting");
    int64_t cap_used = 0;
    int64_t cap_total = 0;

    for (RoomKind rkind = 0; rkind < kfx_config_state.conf.slab_conf.room_types_count; rkind++)
    {
        if(room_role_matches(rkind,RoRoF_LairStorage))
        {
            FOR_EACH_ROOM(room, room_walk_owner(dungeon->room_list_start[rkind]))
            {
                cap_total += room->total_capacity;
                cap_used += room->used_capacity;
            }
        }
    }
    int64_t cap_required = 0;
    FOR_EACH_THING(thing, thing_walk_creatures(dungeon->creatr_list_start, CREATURES_COUNT))
    {
        struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
        if (cctrl->lair_room_id == 0)
        {
            struct CreatureModelConfig* crconf = creature_stats_get_from_thing(thing);
            cap_required += crconf->lair_size;
        }
    }
    SYNCDBG(9,"Total lair capacity %" PRId64 ", used %" PRId64 ", will need %" PRId64 " more",(int64_t)cap_total,(int64_t)cap_used,(int64_t)cap_required);
    return cap_total - cap_used - cap_required;
}

static int64_t get_lair_score(TbBool room_has_units_of_same_kind,TbBool room_has_units_of_different_kind,TbBool room_has_lair_enemy)
{
    if ( room_has_units_of_same_kind )
    {
        if ( room_has_units_of_different_kind )
        {
            if ( room_has_lair_enemy )
            {
                return 3;
            }
            else
            {
                return 6;
            }
        }
        else
        {
            if ( room_has_lair_enemy )
            {
                return 2;
            }
            else
            {
                return 7;
            }
        }
    }
    else if ( room_has_units_of_different_kind )
    {
        if ( room_has_lair_enemy )
        {
            return 1;
        }
        else
        {
            return 4;
        }
    }
    else
    {
        return 5;
    }
}

TbBool creature_model_is_lair_enemy(const ThingModel lair_enemy[CREATURE_TYPES_MAX], ThingModel crmodel)
{
    for (int64_t i = 0; i < CREATURE_TYPES_MAX; i++)
    {
        if (lair_enemy[i] == crmodel)
        {
            return true;
        }
    }
    return false;
}

TbBool creature_model_is_hostile_towards(const ThingModel hostile_towards[CREATURE_TYPES_MAX], ThingModel crmodel)
{
    for (int64_t i = 0; i < CREATURE_TYPES_MAX; i++)
    {
        if (hostile_towards[i] == crmodel)
        {
            return true;
        }
    }
    return false;
}

struct Room *get_best_new_lair_for_creature(struct Thing *creatng)
{
    struct Room *room;
    char best_score = 0;

    const struct CreatureModelConfig* crconf = creature_stats_get_from_thing(creatng);
    struct Dungeon* dungeon = get_dungeon(creatng->owner);

    int64_t *room_scores = (int64_t *)big_scratch;
    memset(big_scratch, 0, ROOMS_COUNT);


    for (RoomKind rkind = 0; rkind < kfx_config_state.conf.slab_conf.room_types_count; rkind++)
    {
        if(room_role_matches(rkind,RoRoF_LairStorage))
        {
            room = room_get(dungeon->room_list_start[rkind]);
            while (!room_is_invalid(room))
            {
                if ( room_has_enough_free_capacity_for_creature_job(room, creatng, Job_TAKE_SLEEP) && creature_can_head_for_room(creatng, room, 0) )
                {
                    TbBool room_has_units_of_same_kind = false;
                    TbBool room_has_units_of_different_kind = false;
                    TbBool room_has_lair_enemy = false;
                    for ( ThingModel model = 0; model < kfx_config_state.conf.crtr_conf.model_count; ++model )
                    {
                        if ( room_has_units_of_same_kind && room_has_units_of_different_kind && room_has_lair_enemy )
                            break;
                        if ( room->content_per_model[model] > 0)
                        {
                            if ( creatng->model == model )
                            {
                                room_has_units_of_same_kind = true;
                            }
                            else
                            {
                                room_has_units_of_different_kind = true;
                            }
                            if (creature_model_is_lair_enemy(crconf->lair_enemy, model)
                            || creature_model_is_hostile_towards(crconf->hostile_towards, model))
                            {
                                room_has_lair_enemy = true;
                            }
                        }
                    }
                    char lair_score = get_lair_score(room_has_units_of_same_kind,room_has_units_of_different_kind,room_has_lair_enemy);
                    room_scores[room->index] = lair_score;
                    if (best_score < lair_score)
                        best_score = lair_score;
                }
                room = room_get(room->next_of_owner);
            }
        }
    }

    if (best_score == 0)
    {
        return INVALID_ROOM;
    }

    struct Room *nearest_room = INVALID_ROOM;
    MapCoordDelta distance;
    MapCoordDelta min_distance = INT_MAX;
    struct Coord3d room_center_pos;

    for (RoomKind rkind = 0; rkind < kfx_config_state.conf.slab_conf.room_types_count; rkind++)
    {
        if(room_role_matches(rkind,RoRoF_LairStorage))
        {
            room = room_get(dungeon->room_list_start[rkind]);
            while (!room_is_invalid(room))
            {
                if ( room_scores[room->index] == best_score )
                {
                    room_center_pos.x.val = subtile_coord_center(room->central_stl_x);
                    room_center_pos.y.val = subtile_coord_center(room->central_stl_y);
                    room_center_pos.z.val = get_floor_height_at(&room_center_pos);
                    distance = get_chessboard_distance(&creatng->mappos, &room_center_pos);

                    if ( min_distance > distance )
                    {
                        min_distance = distance;
                        nearest_room = room;
                    }
                }
                room = room_get(room->next_of_owner);
            }
        }
    }
    return nearest_room;
}

void count_lair_occupants_on_slab(struct Room *room,MapSlabCoord slb_x, MapSlabCoord slb_y)
{
    SYNCDBG(17,"Starting for %s index %" PRId64 " at %" PRId64 ",%" PRId64,room_code_name(room->kind),(int64_t)room->index,(int64_t)slb_x,(int64_t)slb_y);
    for (int64_t n = 0; n < MID_AROUND_LENGTH; n++)
    {
        MapSubtlDelta ssub_x = 1 + start_at_around[n].delta_x;
        MapSubtlDelta ssub_y = 1 + start_at_around[n].delta_y;
        struct Thing* lairtng = find_lair_totem_at(slab_subtile(slb_x, ssub_x), slab_subtile(slb_y, ssub_y));
        if (!thing_is_invalid(lairtng))
        {
            struct Thing* creatng = thing_get(lairtng->lair.belongs_to);
            int64_t required_cap = get_required_room_capacity_for_object(RoRoF_LairStorage, 0, creatng->model);
            if (room->used_capacity + required_cap > room->total_capacity)
            {
                create_effect(&lairtng->mappos, imp_spangle_effects[get_player_color_idx(lairtng->owner)], lairtng->owner);
                delete_lair_totem(lairtng);
            } else
            {
                struct CreatureControl* cctrl = creature_control_get_from_thing(creatng);
                cctrl->lair_room_id = room->index;
                room->content_per_model[creatng->model]++;
                room->used_capacity += required_cap;
            }
        }
    }
}

void count_lair_occupants(struct Room *room)
{
    room->used_capacity = 0;
    memset(room->content_per_model, 0, sizeof(room->content_per_model));
    FOR_EACH_ROOM_SLAB(slb_num, room_slab_walk_ahead_from(room->slabs_list, kfx_sim_state.map_tiles_x * kfx_sim_state.map_tiles_y))
    {
        MapSubtlCoord slb_x = slb_num_decode_x(slb_num);
        MapSubtlCoord slb_y = slb_num_decode_y(slb_num);
        struct SlabMap* slb = get_slabmap_direct(slb_num);
        if (slabmap_block_invalid(slb))
        {
            ERRORLOG("Jump to invalid room slab detected");
            break;
        }
        
        count_lair_occupants_on_slab(room, slb_x, slb_y);
    }
}

struct Thing *find_lair_totem_at(MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{
    struct Map* mapblk = get_map_block_at(stl_x, stl_y);
    FOR_EACH_THING(thing, thing_walk_map_block(mapblk))
    {
        if (thing_is_lair_totem(thing)) {
            return thing;
        }
    }
    return INVALID_THING;
}
/******************************************************************************/
