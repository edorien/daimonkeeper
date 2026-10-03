/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file room_util.c
 *     Generic utility and maintain functions for rooms.
 * @par Purpose:
 *     Functions to maintain rooms of various kinds.
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
#include "room_util.h"

#include "globals.h"
#include "bflib_basics.h"
#include "ariadne_update.h"
#include "room_data.h"
#include "room_garden.h"
#include "map_utils.h"
#include "map_blocks.h"
#include "player_data.h"
#include "dungeon_data.h"
#include "thing_data.h"
#include "thing_doors.h"
#include "thing_stats.h"
#include "thing_physics.h"
#include "thing_effects.h"
#include "thing_objects.h"
#include "room_list.h"
#include "room_workshop.h"
#include "config_terrain.h"
#include "config_creature.h"
#include "magic_powers.h"
#include <math.h>
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "player_availability.h"
#include "ports/ui_port.h"
#include "ports/audio_port.h"
#include "list_walk.h"
#include "map_columns.h"
#include "post_inc.h"

/******************************************************************************/
struct Thing *create_room_surrounding_flame(struct Room *room, const struct Coord3d *pos,
    int64_t eetype, PlayerNumber owner)
{
    struct Thing* eething = create_effect_element(pos, room_effect_elements[eetype], owner);
    if (!thing_is_invalid(eething))
    {
        eething->mappos.z.val = get_thing_height_at(eething, &eething->mappos);
        eething->mappos.z.val += 10;
        // Size of the flame depends on room efficiency
        eething->sprite_size = ((eething->sprite_size - 80) * ((int64_t)room->efficiency) / 256) + 80;
  }
  return eething;
}

void room_update_surrounding_flames(struct Room *room, const struct Coord3d *pos)
{
    int64_t k;
    int64_t i = room->flames_around_idx;
    MapSubtlCoord x = pos->x.stl.num + (MapSubtlCoord)small_around[i].delta_x;
    MapSubtlCoord y = pos->y.stl.num + (MapSubtlCoord)small_around[i].delta_y;
    struct Room* curoom = subtile_room_get(x, y);
    if (curoom->index != room->index)
    {
        k = (i + 1) % SMALL_AROUND_LENGTH;
        room->flames_around_idx = k;
        return;
    }
    k = (i + 3) % SMALL_AROUND_LENGTH;
    x += (MapSubtlCoord)small_around[k].delta_x;
    y += (MapSubtlCoord)small_around[k].delta_y;
    curoom = subtile_room_get(x,y);
    if (curoom->index != room->index)
    {
        room->flame_slb += kfx_sim_state.small_around_slab[i];
        return;
    }
    room->flame_slb += kfx_sim_state.small_around_slab[i] + kfx_sim_state.small_around_slab[k];
    room->flames_around_idx = k;
}

void process_room_surrounding_flames(struct Room *room)
{
    SYNCDBG(19,"Starting");
    if(player_is_roaming(room->owner))
    {
        return;
    }
    MapSlabCoord x = slb_num_decode_x(room->flame_slb);
    MapSlabCoord y = slb_num_decode_y(room->flame_slb);
    int64_t i = 3 * room->flames_around_idx + room->flame_stl;
    struct Coord3d pos;
    pos.x.val = subtile_coord_center(slab_subtile_center(x)) + room_spark_offset[i].delta_x;
    pos.y.val = subtile_coord_center(slab_subtile_center(y)) + room_spark_offset[i].delta_y;
    pos.z.val = 0;
    // Create new element
    if (room->owner == kfx_config_state.neutral_player_num)
    {
      create_room_surrounding_flame(room,&pos,get_gameturn() & 3,kfx_config_state.neutral_player_num);
    } else
    if (room_effect_elements[get_player_color_idx(room->owner)] != 0)
    {
      create_room_surrounding_flame(room,&pos,get_player_color_idx(room->owner),room->owner);
    }
    // Update coords for next element
    if (room->flame_stl == 2)
    {
      room_update_surrounding_flames(room,&pos);
    }
    room->flame_stl = (room->flame_stl + 1) % 3;
}

void recompute_rooms_count_in_dungeons(void)
{
    SYNCDBG(17,"Starting");
    for (int64_t i = 0; i < DUNGEONS_COUNT; i++)
    {
        struct Dungeon* dungeon = get_dungeon(i);
        dungeon->total_rooms = 0;
        for (RoomKind rkind = 1; rkind < kfx_config_state.conf.slab_conf.room_types_count; rkind++)
        {
            if (room_is_counted(rkind))
            {
                dungeon->total_rooms += count_player_rooms_of_type(i, rkind);
            }
        }
    }
}

void process_rooms(void)
{
  SYNCDBG(7,"Starting");
  for (struct Room* room = start_rooms; room < end_rooms; room++)
  {
      if (!room_exists(room))
          continue;
      if (room_role_matches(room->kind, RoRoF_FoodSpawn)) {
          room_grow_food(room);
      }
      if (room_has_surrounding_flames(room->kind) && ((kfx_sim_state.view_mode_flags & GNFldD_RoomFlameProcessing) != 0)) {
          process_room_surrounding_flames(room);
      }
  }
  recompute_rooms_count_in_dungeons();
  SYNCDBG(9,"Finished");
}

void kill_all_room_slabs_and_contents(struct Room *room)
{
    FOR_EACH_ROOM_SLAB(slb_num, room_slab_walk_ahead(room))
    {
        int64_t slb_x = slb_num_decode_x(slb_num);
        int64_t slb_y = slb_num_decode_y(slb_num);
        
        struct SlabMap* slb = get_slabmap_block(slb_x, slb_y);
        kill_room_slab_and_contents(room->owner, slb_x, slb_y);
        slb->next_in_room = 0;
        slb->room_index = 0;
    }
    room->slabs_list = 0;
    room->slabs_count = 0;
}

void sell_room_slab_when_no_free_room_structures(struct Room *room, int64_t slb_x, int64_t slb_y, unsigned char gnd_slab)
{
    delete_room_slab_when_no_free_room_structures(slb_x, slb_y, gnd_slab);
    struct RoomConfigStats* roomst = get_room_kind_stats(room->kind);
    int64_t revenue = compute_value_percentage(roomst->cost, kfx_config_state.conf.rules[room->owner].gameplay.room_sale_percent);
    if (revenue != 0)
    {
        struct Coord3d pos;
        set_coords_to_slab_center(&pos, slb_x, slb_y);
        create_price_effect(&pos, room->owner, revenue);
        player_add_offmap_gold(room->owner, revenue);
    }
}

void recreate_rooms_from_room_slabs(struct Room *room, unsigned char gnd_slab)
{
    SYNCDBG(7,"Starting for %s index %" PRId64,room_code_name(room->kind),(int64_t)room->index);
    // Clear room index in all slabs
    // This will make sure that the old room won't be returned by subtile_room_get()
    // and used as one of new rooms.
    FOR_EACH_ROOM_SLAB(slab_num, room_slab_walk_ahead(room))
    {
        struct SlabMap* slb = get_slabmap_direct(slab_num);
        if (slabmap_block_invalid(slb))
        {
          ERRORLOG("Jump to invalid item when sweeping Slabs.");
          break;
        }
        
        slb->room_index = 0;
    }
    // Create a new room for every slab
    struct Room* proom = INVALID_ROOM;
    FOR_EACH_ROOM_SLAB(slb_num, room_slab_walk_ahead(room))
    {
        int64_t slb_x = slb_num_decode_x(slb_num);
        int64_t slb_y = slb_num_decode_y(slb_num);
        
        struct Room* nroom = create_room(room->owner, room->kind, slab_subtile_center(slb_x), slab_subtile_center(slb_y));
        if (room_is_invalid(nroom)) // In case of error, sell the whole thing
        {
            ERRORLOG("Room creation failed; selling slabs");
            sell_room_slab_when_no_free_room_structures(room, slb_x, slb_y, gnd_slab);
        } else
        {
            // We may have created a new room, or just added tile to the old one
            // Integrate the room only if we're not adding tiles to old room
            if ((nroom != proom) && room_exists(proom)) {
                do_room_integration(proom);
            }
        }
        proom = nroom;
    }
    if (room_exists(proom)) {
        do_room_integration(proom);
    }
    // The old room no longer has any slabs
    room->slabs_list = 0;
    room->slabs_count = 0;
}

TbBool delete_room_slab(MapSlabCoord slb_x, MapSlabCoord slb_y, TbBool is_destroyed)
{
    struct Room* room = slab_room_get(slb_x, slb_y);
    if (room_is_invalid(room))
    {
        ERRORLOG("Slab (%" PRId64 ",%" PRId64 ") is not a room",(int64_t)(slb_x), (int64_t)(slb_y));
        return false;
    }
    SYNCDBG(7,"Room on (%" PRId64 ",%" PRId64 ") had %" PRId64 " slabs",(int64_t)slb_x,(int64_t)slb_y,(int64_t)room->slabs_count);
    decrease_room_area(room->owner, 1);
    kill_room_slab_and_contents(room->owner, slb_x, slb_y);
    if (room->slabs_count <= 1)
    {
        delete_room_flag(room);
        replace_room_slab(room, slb_x, slb_y, room->owner, is_destroyed);
        kill_all_room_slabs_and_contents(room);
        free_room_structure(room);
        do_slab_efficiency_alteration(slb_x, slb_y);
    } else
    {
        // Remove the slab from room tiles list
        remove_slab_from_room_tiles_list(room, slb_x, slb_y);
        replace_room_slab(room, slb_x, slb_y, room->owner, is_destroyed);
        // Create a new room from slabs left in old one
        recreate_rooms_from_room_slabs(room, is_destroyed);
        reset_creatures_rooms(room);
        free_room_structure(room);
    }
    return true;
}

TbBool replace_slab_from_script(MapSlabCoord slb_x, MapSlabCoord slb_y, unsigned char slabkind)
{
    struct Room* room = slab_room_get(slb_x, slb_y);
    struct SlabMap* slb = get_slabmap_for_subtile(slab_subtile(slb_x, 0), slab_subtile(slb_y, 0));
    int64_t plyr_idx = slabmap_owner(slb);
    if (slab_kind_has_no_ownership(slabkind))
    {
        plyr_idx = kfx_config_state.neutral_player_num;
    }
    RoomKind rkind = slab_corresponding_room(slabkind);
    //When the slab to be replaced does not have a room yes, simply place the room/slab.
    if (room_is_invalid(room))
    {
        // If we're looking to place a non-room slab, simply place it.
        if (rkind == 0)
        {
            if (slab_kind_is_animated(slabkind))
            {
                place_animating_slab_type_on_map(slabkind, 0, slab_subtile(slb_x, 0), slab_subtile(slb_y, 0), plyr_idx);
            }
            else
            {
                place_slab_type_on_map(slabkind, slab_subtile(slb_x, 0), slab_subtile(slb_y, 0), plyr_idx, 0);
                update_wibble_on_surrounding_slabs(slb_x, slb_y);
            }
            return true;
        }
        else
        {
            // Create the new one-slab room
            if (place_room(plyr_idx, rkind, slab_subtile(slb_x, 0), slab_subtile(slb_y, 0)))
            {
                return true;
            }
        }
        return false;
    }
    else
    {
        if (rkind == 0)
        {
            delete_room_slab(slb_x, slb_y, 0);
            if (slab_kind_is_animated(slabkind))
            {
                place_animating_slab_type_on_map(slabkind, 0, slab_subtile(slb_x, 0), slab_subtile(slb_y, 0), plyr_idx);
            }
            else
            {
                place_slab_type_on_map(slabkind, slab_subtile(slb_x, 0), slab_subtile(slb_y, 0), plyr_idx, 0);
            }
        }
        else
        {
            // Create a new one-slab room
            place_room(plyr_idx, rkind, slab_subtile(slb_x, 0), slab_subtile(slb_y, 0));
        }
    }
    return true;
}

void change_slab_owner_from_script(MapSlabCoord slb_x, MapSlabCoord slb_y, PlayerNumber plyr_idx)
{
    struct SlabMap *slb = get_slabmap_block(slb_x, slb_y);
    if (slabmap_owner(slb) == plyr_idx)
        return;
    if (slb->room_index)
    {
        struct Room* room = room_get(slb->room_index);
        if (room_exists(room))
        {
            take_over_room(room, plyr_idx);
        }
    } else
    {
        SlabKind slbkind = (slb->kind == SlbT_PATH) ? SlbT_CLAIMED : slb->kind;
        if (slab_kind_has_no_ownership(slbkind) == false)
        {
            if (slab_kind_is_door(slbkind))
            {
                MapSubtlCoord stl_x = slab_subtile_center(slb_x);
                MapSubtlCoord stl_y = slab_subtile_center(slb_y);
                struct Thing* doortng = get_door_for_position(stl_x, stl_y);
                if (!thing_is_invalid(doortng))
                {
                    kfx_sim_state.dungeon[doortng->owner].total_doors--;
                    remove_key_on_door(doortng);
                    set_slab_owner(slb_x, slb_y, plyr_idx);
                    place_animating_slab_type_on_map(slbkind, doortng->door.closing_counter / 256, stl_x, stl_y, plyr_idx);
                    doortng->owner = plyr_idx;
                    kfx_sim_state.dungeon[doortng->owner].total_doors++;
                    if (doortng->door.is_locked)
                    {
                        add_key_on_door(doortng);
                    }
                    update_navigation_triangulation(stl_x-1,  stl_y-1, stl_x+1,stl_y+1);
                }
            }
            else if (slab_kind_is_animated(slbkind))
            {
                place_animating_slab_type_on_map(slbkind, 0, slab_subtile(slb_x, 0), slab_subtile(slb_y, 0), plyr_idx);
            }
            else
            {
                place_slab_type_on_map(slbkind, slab_subtile(slb_x, 0), slab_subtile(slb_y, 0), plyr_idx, 0);
            }
            do_slab_efficiency_alteration(slb_x, slb_y);
        }
    }
}

/**
 * Updates thing interaction with rooms. Sometimes deletes the given thing.
 * @param thing Thing to be checked, and assimilated or deleted.
 * @note Used capacity of the room don't have to be updated here, as it is re-computed later.
 * @return True if the thing was either assimilated or left intact, false if it was deleted.
 */
TbBool check_and_asimilate_thing_by_room(struct Thing *thing)
{
    struct Room *room;
    if (thing_is_dragged_or_pulled(thing))
    {
        ERRORLOG("It shouldn't be possible to drag %s during initial asimilation",thing_model_name(thing));
        thing->owner = kfx_config_state.neutral_player_num;
        return true;
    }
    if (thing_is_gold_hoard(thing))
    {
        room = get_room_thing_is_on(thing);
        GoldAmount wealth_size_holds = kfx_config_state.conf.rules[room->owner].gameplay.gold_per_hoard / get_wealth_size_types_count();
        GoldAmount gold_value = thing->valuable.gold_stored;
        if (gold_value == 0)
        {
            gold_value = wealth_size_holds* max(1, get_wealth_size_of_gold_hoard_object(thing));
        }
        else
        {
            thing->valuable.gold_stored = 0;
        }
        GoldAmount value_left;
        GoldAmount value_added;
        if (room_is_invalid(room) || !room_role_matches(room->kind, RoRoF_GoldStorage))
        {
            // No room - delete it, hoard cannot exist outside treasure room
            ERRORLOG("Found %s outside of %s room; removing",thing_model_name(thing),room_role_code_name(RoRoF_GoldStorage));
            create_gold_pile(&thing->mappos, thing->owner, gold_value);
            destroy_object(thing);
            return false;
        }
        MapSubtlCoord stl_x = thing->mappos.x.stl.num - 1;
        MapSubtlCoord stl_y = thing->mappos.y.stl.num - 1;
        if (!((stl_x % 3) || (stl_y % 3))) // Only accept hoards on a center subtile.
        {
            thing->owner = room->owner;
            value_added = add_gold_to_hoarde(thing, room, gold_value);
            value_left = gold_value - value_added;
            if (value_left > 0)
            {
                create_gold_pile(&thing->mappos, thing->owner, value_left);
            }
            return true;
        }
        create_gold_pile(&thing->mappos, thing->owner, gold_value);
        destroy_object(thing);
        return false;
    }
    if (thing_is_spellbook(thing))
    {
        room = get_room_thing_is_on(thing);
        if (room->owner != kfx_config_state.neutral_player_num)
        {
            if (room_is_invalid(room) || !room_role_matches(room->kind, RoRoF_PowersStorage) || (!player_exists(get_player(room->owner)) && (get_gameturn() >= 10)))
            {
                // No room - oh well, leave it as free spell
                if (((kfx_config_state.conf.rules[room->owner].gameplay.classic_bugs_flags & ClscBug_ClaimRoomAllThings) != 0) && !room_is_invalid(room)) {
                    // Preserve classic bug - object is claimed with the room
                    thing->owner = room->owner;
                }
                else {
                    // Make correct owner so that Imps can pick it up
                    thing->owner = kfx_config_state.neutral_player_num;
                    return false;
                }
                return false;
            }
            if (!add_power_to_player(book_thing_to_power_kind(thing), room->owner))
            {
                thing->owner = kfx_config_state.neutral_player_num;
                return false;
            }
            else
            {
                thing->owner = room->owner;
                return true;
            }
        }
        thing->owner = room->owner;
        return false;
    }
    if (thing_is_workshop_crate(thing))
    {
        room = get_room_thing_is_on(thing);
        if (room_is_invalid(room) || !room_role_matches(room->kind, RoRoF_CratesStorage) || !player_exists(get_player(room->owner)))
        {
            // No room - oh well, leave it as free box
            if (((kfx_config_state.conf.rules[room->owner].gameplay.classic_bugs_flags & ClscBug_ClaimRoomAllThings) != 0) && !room_is_invalid(room)) {
                // Preserve classic bug - object is claimed with the room
                thing->owner = room->owner;
            } else {
                // Make correct owner so that Imps can pick it up
                thing->owner = kfx_config_state.neutral_player_num;
            }
            return true;
        }
        if (!add_workshop_item_to_amounts(room->owner, crate_thing_to_workshop_item_class(thing), crate_thing_to_workshop_item_model(thing)))
        {
            thing->owner = kfx_config_state.neutral_player_num;
            return true;
        }
        thing->owner = room->owner;
        return true;
    }
    return false;
}

EventIndex update_cannot_find_room_of_role_wth_spare_capacity_event(PlayerNumber plyr_idx, struct Thing *creatng, RoomRole rrole)
{
    EventIndex evidx = 0;
    if (player_has_room_of_role(plyr_idx, rrole))
    {
        // Could not find room to send thing - either no capacity or not navigable
        struct Room *room;
        switch (rrole)
        {
        case RoRoF_LairStorage:
        case RoRoF_CrHealSleep:
            // Find room with lair capacity
            {
                struct CreatureModelConfig* crconf = creature_stats_get_from_thing(creatng);
                room = find_room_of_role_with_spare_capacity(plyr_idx, rrole, crconf->lair_size);
                break;
            }
        // For Treasure rooms, the item capacity is the amount of gold, not the number of gold hoardes.
        case RoRoF_CratesStorage:
        case RoRoF_PowersStorage:
            // Find room with item capacity
            room = find_room_of_role_with_spare_room_item_capacity(plyr_idx, rrole);
            break;
        default:
            // Find room with worker capacity
            room = find_room_of_role_with_spare_capacity(plyr_idx, rrole, 1);
            break;
        }
        if (room_is_invalid(room))
        {
            SYNCDBG(5,"Player %" PRId64 " has %s which cannot find large enough %s",(int64_t)plyr_idx,thing_model_name(creatng),room_role_code_name(rrole));
            switch (rrole)
            {
            case RoRoF_LairStorage:
                evidx = event_create_event_or_update_nearby_existing_event(creatng->mappos.x.val, creatng->mappos.y.val, EvKind_NoMoreLivingSet, plyr_idx, creatng->index);
                break;
            case RoRoF_GoldStorage:
                evidx = event_create_event_or_update_nearby_existing_event(0, 0, EvKind_TreasureRoomFull, plyr_idx, 0);
                break;
            default:
                evidx = 1;
                break;
            }
            if (evidx > 0) {
                audio_output_room_message(plyr_idx, find_first_roomkind_with_role(rrole), OMsg_RoomTooSmall);
            }
        } else
        {
            SYNCDBG(5,"Player %" PRId64 " has %s which cannot reach %s",(int64_t)plyr_idx,thing_model_name(creatng),room_role_code_name(rrole));
            RoomKind rkind = find_first_roomkind_with_role(rrole);
            evidx = event_create_event_or_update_nearby_existing_event(
                creatng->mappos.x.val, creatng->mappos.y.val, EvKind_WorkRoomUnreachable, plyr_idx, rkind);
            if (evidx > 0) {
                audio_output_room_message(plyr_idx, rkind, OMsg_RoomNoRoute);
            }
        }
    } else
    {
        // We simply don't have the room of that kind
        if (rrole == RoRoF_LairStorage || rrole == RoRoF_GoldStorage || is_room_of_role_available(plyr_idx, rrole))
        {
            switch (rrole)
            {
            case RoRoF_LairStorage:
                evidx = event_create_event_or_update_nearby_existing_event(creatng->mappos.x.val, creatng->mappos.y.val, EvKind_NoMoreLivingSet, plyr_idx, creatng->index);
                break;
            case RoRoF_GoldStorage:
                evidx = event_create_event_or_update_nearby_existing_event(0, 0, EvKind_NeedTreasureRoom, plyr_idx, 0);
                break;
            default:
                evidx = 1;
                break;
            }
            if (evidx > 0) {
                audio_output_room_message(plyr_idx, find_first_roomkind_with_role(rrole), OMsg_RoomNeeded);
            }
        }
    }
    return evidx;
}

void query_room(struct Room *room)
{
    char title[26] = "";
    const char* name = room_code_name(room->kind);
    char owner[26] = "";
    char health[26] = "";
    char capacity[26] = "";
    char efficiency[26] = "";
    snprintf(title, sizeof(title), "Room ID: %" PRId64, (int64_t)(room->index));
    snprintf(owner, sizeof(owner), "Owner: %" PRId64, (int64_t)(room->owner));
    snprintf(health, sizeof(health), "Health: %" PRId64, (int64_t)room->health);
    snprintf(capacity, sizeof(capacity), "Capacity: %" PRId64 "/%" PRId64, (int64_t)(room->used_capacity), (int64_t)(room->total_capacity));
    double room_efficiency_percent = ((double)room->efficiency / (double)ROOM_EFFICIENCY_MAX) * 100;
    snprintf(efficiency, sizeof(efficiency), "Efficiency: %" PRId64, (int64_t)((unsigned char)round(room_efficiency_percent)));
    ui_create_message_box((const char*)&title, name, (const char*)&owner, (const char*)&health, (const char*)&capacity, (const char*)&efficiency);
}

/******************************************************************************/

void place_slab_type_replacing_room(SlabKind slbkind, MapSlabCoord slb_x, MapSlabCoord slb_y, PlayerNumber owner)
{
    const MapSubtlCoord stl_x = slab_subtile(slb_x, 0);
    const MapSubtlCoord stl_y = slab_subtile(slb_y, 0);
    if (subtile_is_room(stl_x, stl_y))
        delete_room_slab(slb_x, slb_y, true);
    if (slab_kind_is_room(slbkind))
    {
        place_slab_type_on_map(SlbT_EARTH, stl_x, stl_y, kfx_config_state.neutral_player_num, 0);
        do_slab_efficiency_alteration(slb_x, slb_y);
    }
    if (slab_kind_is_animated(slbkind))
        place_animating_slab_type_on_map(slbkind, 0, stl_x, stl_y, owner);
    else
        place_slab_type_on_map(slbkind, stl_x, stl_y, owner, 0);
    do_slab_efficiency_alteration(slb_x, slb_y);
}

/******************************************************************************/
static void room_storage_reposition_all_on_subtile(const struct RoomStorageKind *kind, struct Room *room, MapSubtlCoord stl_x, MapSubtlCoord stl_y, struct RoomReposition *rrepos)
{
    struct Map* mapblk = get_map_block_at(stl_x, stl_y);
    if (map_block_invalid(mapblk))
        return;
    FOR_EACH_THING(thing, thing_walk_map_block(mapblk))
    {
        if (kind->is_stored(room, thing))
            kind->take_out(room, thing, rrepos);
    }
}

/**
 * How many stored items a subtile holds, or -1 when all of them have to be taken out
 * and re-created, or -2 when the subtile is to be left alone.
 */
static int64_t room_storage_check_subtile(const struct RoomStorageKind *kind, struct Room *room, MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{
    if (kind->check_subtile != NULL)
        return kind->check_subtile(room, stl_x, stl_y);
    struct Map* mapblk = get_map_block_at(stl_x, stl_y);
    if (map_block_invalid(mapblk))
        return -2; // do nothing
    struct RoomConfigStats* roomst = get_room_kind_stats(room->kind);
    if ((roomst->storage_height >= 0) && (get_map_floor_filled_subtiles(mapblk) != roomst->storage_height)) {
        return -1; // re-create all
    }
    int64_t matching_things_at_subtile = 0;
    FOR_EACH_THING(thing, thing_walk_map_block(mapblk))
    {
        if (!kind->is_stored(room, thing))
            continue;
        // If exceeded capacity of the room
        if (room->used_capacity >= room->total_capacity)
        {
            WARNLOG("The %s capacity %" PRId64 " exceeded; space used is %" PRId64,room_code_name(room->kind),(int64_t)room->total_capacity,(int64_t)room->used_capacity);
            return -1; // re-create all (this could save the object if there are duplicates)
        } else
        // If the thing is in wall, remove it but store to re-create later
        if (thing_in_wall_at(thing, &thing->mappos))
        {
            // If it's inside the floor, it may be moved up and counted
            if (kind->lift_out_of_floor && position_over_floor_level(thing, &thing->mappos))
                matching_things_at_subtile++;
            else
                return -1; // re-create all
        } else
        {
            matching_things_at_subtile++;
        }
    }
    return matching_things_at_subtile; // Increase used capacity
}

static TbBool room_storage_recreate_on_subtile(const struct RoomStorageKind *kind, struct Room *room, MapSubtlCoord stl_x, MapSubtlCoord stl_y, struct RoomReposition *rrepos)
{
    if ((rrepos->used < 0) || (room->used_capacity >= room->total_capacity)) {
        return false;
    }
    for (int64_t ri = 0; ri < ROOM_REPOSITION_COUNT; ri++)
    {
        if (rrepos->models[ri] != 0)
        {
            struct Thing* tng = kind->put_back(room, rrepos->models[ri], rrepos->exp_level[ri], stl_x, stl_y);
            if (!thing_is_invalid(tng))
            {
                rrepos->used--;
                rrepos->models[ri] = 0;
                rrepos->exp_level[ri] = 0;
                return true;
            }
        }
    }
    return false;
}

static void room_storage_count_and_reposition_on_subtile(const struct RoomStorageKind *kind, struct Room *room, MapSubtlCoord stl_x, MapSubtlCoord stl_y, struct RoomReposition *rrepos)
{
    int64_t matching_things_at_subtile = room_storage_check_subtile(kind, room, stl_x, stl_y);
    if (matching_things_at_subtile > 0) {
        // This subtile contains stored items
        SYNCDBG(19,"Got %" PRId64 " matching things at (%" PRId64 ",%" PRId64 ")",(int64_t)matching_things_at_subtile,(int64_t)stl_x,(int64_t)stl_y);
        room->used_capacity += matching_things_at_subtile;
    } else
    {
        switch (matching_things_at_subtile)
        {
        case -2:
            // No matching things, but also cannot recreate anything on this subtile
            break;
        case -1:
            // All matching things are to be removed from the subtile and stored for re-creation
            room_storage_reposition_all_on_subtile(kind, room, stl_x, stl_y, rrepos);
            break;
        case 0:
            // There are no matching things there, something can be re-created
            room_storage_recreate_on_subtile(kind, room, stl_x, stl_y, rrepos);
            break;
        default:
            WARNLOG("Invalid value returned by reposition check");
            break;
        }
    }
}

/** Recounts a storage room's contents, putting back what is out of place. */
void room_storage_recount(const struct RoomStorageKind *kind, struct Room *room)
{
    SYNCDBG(17,"Starting for %s index %" PRId64,room_code_name(room->kind),(int64_t)room->index);
    struct RoomReposition rrepos;
    init_reposition_struct(&rrepos);
    // Making two loops guarantees that no rrepos things will be lost
    for (int64_t n = 0; n < 2; n++)
    {
        // The correct count should be taken from last sweep
        room->used_capacity = 0;
        if (kind->tracks_storage_capacity)
            room->capacity_used_for_storage = 0;
        FOR_EACH_ROOM_SLAB(slb_num, room_slab_walk(room))
        {
            MapSlabCoord slb_x = slb_num_decode_x(slb_num);
            MapSlabCoord slb_y = slb_num_decode_y(slb_num);
            for (int64_t dy = 0; dy < STL_PER_SLB; dy++)
            {
                for (int64_t dx = 0; dx < STL_PER_SLB; dx++)
                {
                    room_storage_count_and_reposition_on_subtile(kind, room, slab_subtile(slb_x,dx), slab_subtile(slb_y,dy), &rrepos);
                }
            }
        }
        if (kind->stops_when_settled && ((rrepos.used <= 0) || (room->used_capacity >= room->total_capacity)))
            break;
    }
    SYNCDBG(7,"The %s index %" PRId64 " contains %" PRId64 " %s",room_code_name(room->kind),(int64_t)room->index,(int64_t)room->used_capacity,kind->what);
    if (rrepos.used > 0)
    {
        if (kind->overflow != NULL)
        {
            kind->overflow(room, &rrepos);
        } else
        {
            ERRORLOG("The %s index %" PRId64 " capacity %" PRId64 " wasn't enough; %" PRId64 " items belonging to player %" PRId64 " dropped",
              room_code_name(room->kind),(int64_t)room->index,(int64_t)room->total_capacity,(int64_t)rrepos.used,(int64_t)room->owner);
        }
    }
    if (kind->tracks_storage_capacity)
        room->capacity_used_for_storage = room->used_capacity;
}
