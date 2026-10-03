/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file room_library.c
 *     Library room maintain functions.
 * @par Purpose:
 *     Functions to create and use libraries.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     07 Apr 2011 - 05 Jun 2011
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "room_library.h"

#include "globals.h"
#include "bflib_basics.h"
#include "room_data.h"
#include "player_data.h"
#include "dungeon_data.h"
#include "thing_data.h"
#include "thing_objects.h"
#include "thing_effects.h"
#include "thing_physics.h"
#include "thing_stats.h"
#include "thing_navigate.h"
#include "config_terrain.h"
#include "creature_states.h"
#include "creature_states_rsrch.h"
#include "magic_powers.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "ports/script_port.h"
#include "ports/audio_port.h"
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
struct Thing *create_spell_in_library(struct Room *room, ThingModel tngmodel, MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{
    if (!room_role_matches(room->kind,RoRoF_PowersStorage)) {
        SYNCDBG(4,"Cannot add spell to %s owned by player %" PRId64,room_code_name(room->kind),(int64_t)room->owner);
        return INVALID_THING;
    }
    struct Coord3d pos;
    pos.x.val = subtile_coord_center(stl_x);
    pos.y.val = subtile_coord_center(stl_y);
    pos.z.val = 0;
    struct Thing* spelltng = create_object(&pos, tngmodel, room->owner, -1);
    if (thing_is_invalid(spelltng))
    {
        return INVALID_THING;
    }
    // Neutral thing do not need any more processing
    if (is_neutral_thing(spelltng))
    {
        return spelltng;
    }
    if (thing_is_spellbook(spelltng))
    {
        if (!add_item_to_room_capacity(room, true))
        {
            destroy_object(spelltng);
            return INVALID_THING;
        }
    }
    if (!add_power_to_player(book_thing_to_power_kind(spelltng), room->owner))
    {
        remove_item_from_room_capacity(room);
        destroy_object(spelltng);
        return INVALID_THING;
    }
    return spelltng;
}

TbBool remove_spell_from_library(struct Room *room, struct Thing *spelltng, PlayerNumber new_owner)
{
    if ( (!room_role_matches(room->kind,RoRoF_PowersStorage)) || (spelltng->owner != room->owner) ) {
        SYNCDBG(4,"Spell %s owned by player %" PRId64 " found in a %s owned by player %" PRId64 ", instead of proper library",thing_model_name(spelltng),(int64_t)spelltng->owner,room_code_name(room->kind),(int64_t)room->owner);
        return false;
    }
    if (!remove_item_from_room_capacity(room))
        return false;
    if (thing_is_spellbook(spelltng))
    {
        remove_power_from_player(book_thing_to_power_kind(spelltng), room->owner);
    }
    return true;
}

EventIndex update_library_object_pickup_event(struct Thing *creatng, struct Thing *picktng)
{
    EventIndex evidx;
    struct PlayerInfo* player;
    if (thing_is_spellbook(picktng))
    {
        evidx = event_create_event_or_update_nearby_existing_event(
            picktng->mappos.x.val, picktng->mappos.y.val,
            EvKind_SpellPickedUp, creatng->owner, picktng->index);
        // Only play speech message if new event was created
        if (evidx > 0)
        {
            if ( (is_my_player_number(picktng->owner)) && (!is_my_player_number(creatng->owner)) )
            {
                audio_output_message(SMsg_SpellbookStolen, 0);
            }
            else if ( (is_my_player_number(creatng->owner)) && (!is_my_player_number(picktng->owner)) )
            {
                player = get_my_player();
                if (picktng->owner == kfx_config_state.neutral_player_num)
                {
                   if (creatng->index != player->influenced_thing_idx)
                   {
                        audio_output_message(SMsg_DiscoveredSpell, 0);
                   }
                }
                else
                {
                   if (creatng->index != player->influenced_thing_idx)
                   {
                        audio_output_message(SMsg_SpellbookTaken, 0);
                   }
                }
            }
        }
    } else
    if (thing_is_special_box(picktng))
    {
        evidx = event_create_event_or_update_nearby_existing_event(
            picktng->mappos.x.val, picktng->mappos.y.val,
            EvKind_DnSpecialFound, creatng->owner, picktng->index);
        // Only play speech message if new event was created
        if (evidx > 0)
        {
          if (is_my_player_number(creatng->owner) && !is_my_player_number(picktng->owner))
          {
              player = get_my_player();
              if (creatng->index != player->influenced_thing_idx)
              {
                audio_output_message(SMsg_DiscoveredSpecial, 0);
              }
          }
        }
    } else
    {
        WARNLOG("Strange pickup (%s) - no event",thing_class_and_model_name(picktng->class_id, picktng->model));
        evidx = 0;
    }
    return evidx;
}

void init_dungeons_research(void)
{
    for (int64_t i = 0; i < DUNGEONS_COUNT; i++)
    {
        struct Dungeon* dungeon = get_dungeon(i);
        dungeon->current_research_idx = get_next_research_item(dungeon);
    }
}

TbBool remove_all_research_from_player(PlayerNumber plyr_idx)
{
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    dungeon->research_num = 0;
    dungeon->research_override = 1;
    return true;
}

TbBool research_overriden_for_player(PlayerNumber plyr_idx)
{
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    return (dungeon->research_override != 0);
}

TbBool clear_research_for_all_players(void)
{
    for (int64_t plyr_idx = 0; plyr_idx < DUNGEONS_COUNT; plyr_idx++)
    {
        struct Dungeon* dungeon = get_dungeon(plyr_idx);
        dungeon->research_num = 0;
        dungeon->research_override = 0;
    }
    return true;
}

TbBool research_needed(const struct ResearchVal *rsrchval, const struct Dungeon *dungeon)
{
    if (dungeon->research_num == 0)
        return false;
    switch (rsrchval->rtyp)
    {
   case RsCat_Power:
        if ( (dungeon->magic_resrchable[rsrchval->rkind]) && (dungeon->magic_level[rsrchval->rkind] == 0) )
        {
            return true;
        }
        break;
    case RsCat_Room:
        if ((dungeon->room_buildable[rsrchval->rkind] & 1) == 0)
        {
            // Is available for research
            if (dungeon->room_resrchable[rsrchval->rkind] == 1)
                return true;
            // Is available for research and reseach instantly completes when the room is first captured
            else if (dungeon->room_resrchable[rsrchval->rkind] == 2)
                return true;
            // Is not available for research until the room is first captured
            else if ( (dungeon->room_resrchable[rsrchval->rkind] == 4) && (dungeon->room_buildable[rsrchval->rkind] & 2))
                return true;
        }
        break;
    case RsCat_Creature:
        if ((dungeon->creature_allowed[rsrchval->rkind]) && (dungeon->creature_force_enabled[rsrchval->rkind] == 0))
        {
            return true;
        }
        break;
    case RsCat_None:
        break;
    default:
        ERRORLOG("Illegal research type %" PRId64 " while processing player research",(int64_t)rsrchval->rtyp);
        break;
    }
    return false;
}

TbBool add_research_to_player(PlayerNumber plyr_idx, int64_t rtyp, int64_t rkind, int64_t amount)
{
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    int64_t i = dungeon->research_num;
    if (i >= DUNGEON_RESEARCH_COUNT)
    {
      ERRORLOG("Too much research (%" PRId64 " items) for player %" PRId64, (int64_t)(i), (int64_t)(plyr_idx));
      return false;
    }
    struct ResearchVal* resrch = &dungeon->research[i];
    resrch->rtyp = rtyp;
    resrch->rkind = rkind;
    resrch->req_amount = amount;
    dungeon->research_num++;
    return true;
}

TbBool add_research_to_all_players(int64_t rtyp, int64_t rkind, int64_t amount)
{
    TbBool result = true;
    SYNCDBG(17, "Adding type %" PRId64 ", kind %" PRId64 ", amount %" PRId64, (int64_t)(rtyp), (int64_t)(rkind), (int64_t)(amount));
    for (int64_t i = 0; i < PLAYERS_COUNT; i++)
    {
        result &= add_research_to_player(i, rtyp, rkind, amount);
  }
  return result;
}

TbBool update_players_research_amount(PlayerNumber plyr_idx, int64_t rtyp, int64_t rkind, int64_t amount)
{
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    int64_t n = 0;
    for (int64_t i = 0; i < dungeon->research_num; i++)
    {
        struct ResearchVal* resrch = &dungeon->research[i];
        if ((resrch->rtyp == rtyp) && (resrch->rkind == rkind))
        {
            resrch->req_amount = amount;
            n++;
        }
    }
    if (n > 0)
        return true;
    return false;
}

TbBool update_or_add_players_research_amount(PlayerNumber plyr_idx, int64_t rtyp, int64_t rkind, int64_t amount)
{
  if (update_players_research_amount(plyr_idx, rtyp, rkind, amount))
    return true;
  return add_research_to_player(plyr_idx, rtyp, rkind, amount);
}

static void process_player_research(PlayerNumber plyr_idx)
{
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    if (!player_has_room_of_role(plyr_idx, RoRoF_Research)) {
        return;
    }
    struct ResearchVal* rsrchval = get_players_current_research_val(plyr_idx);
    if (rsrchval == NULL)
    {
        // If no current research - try to set one for next time the function is run
        dungeon->current_research_idx = get_next_research_item(dungeon);
        return;
    }
    if (!research_needed(rsrchval, dungeon))
    {
        dungeon->current_research_idx = get_next_research_item(dungeon);
        rsrchval = get_players_current_research_val(plyr_idx);
    }
    if (rsrchval == NULL) {
        // No new research
        return;
    }
    if ((rsrchval->req_amount << 8) > dungeon->research_progress) {
        // Research in progress - not completed
        return;
    }
    struct Room *room;
    struct Coord3d pos;
    TbBool research_completed = false;
    switch (rsrchval->rtyp)
    {
    case RsCat_Power:
    {
        if (dungeon->magic_resrchable[rsrchval->rkind])
        {
            PowerKind pwkind = rsrchval->rkind;
            room = find_room_of_role_with_spare_room_item_capacity(plyr_idx, RoRoF_PowersStorage);
            struct PowerConfigStats* powerst = get_power_model_stats(pwkind);
            if (powerst->artifact_model < 1) {
                ERRORLOG("Tried to research power with no associated artifact");
                break;
            }
            if (room_is_invalid(room)) {
                WARNLOG("Player %" PRId64 " has no %s with capacity for %s artifact, delaying creation",(int64_t)plyr_idx,room_role_code_name(RoRoF_PowersStorage),power_code_name(pwkind));
                return;
            }
            pos.x.val = 0;
            pos.y.val = 0;
            pos.z.val = 0;
            struct Thing* spelltng = create_object(&pos, powerst->artifact_model, plyr_idx, -1);
            if (thing_is_invalid(spelltng))
            {
                ERRORLOG("Could not create %s artifact",power_code_name(pwkind));
                return;
            }
            room = find_random_room_of_role_for_thing_with_spare_room_item_capacity(spelltng, plyr_idx, RoRoF_PowersStorage, 0);
            if (room_is_invalid(room))
            {
                ERRORLOG("There should be %s for %s artifact, but not found",room_role_code_name(RoRoF_PowersStorage),power_code_name(pwkind));
                destroy_object(spelltng);
                return;
            }
            if (!find_random_valid_position_for_thing_in_room_avoiding_object(spelltng, room, &pos))
            {
                ERRORLOG("Could not find position in %s for %s artifact",room_code_name(room->kind),power_code_name(pwkind));
                destroy_object(spelltng);
                return;
            }
            pos.z.val = get_thing_height_at(spelltng, &pos);
            if (add_power_to_player(pwkind, plyr_idx))
            {
                move_thing_in_map(spelltng, &pos);
                if (thing_is_spellbook(spelltng))
                {
                    add_item_to_room_capacity(room, true);
                }
                event_create_event(spelltng->mappos.x.val, spelltng->mappos.y.val, EvKind_NewSpellResrch, spelltng->owner, pwkind);
                create_effect(&pos, TngEff_ResearchComplete, spelltng->owner);
            }
            else
            {
                dungeon->magic_level[pwkind]++;
            }
            if (is_my_player_number(plyr_idx))
            {
                audio_output_message(SMsg_ResearchedSpell, 0);
            }
            research_completed = true;
        }
        break;
    }
    case RsCat_Room:
        if (dungeon->room_resrchable[rsrchval->rkind])
        {
            RoomKind rkind;
            rkind = rsrchval->rkind;
            event_create_event(0, 0, EvKind_NewRoomResrch, plyr_idx, rkind);
            dungeon->room_buildable[rkind] |= 3; // Player may build room and may research it again
            if (is_my_player_number(plyr_idx))
                audio_output_message(SMsg_ResearchedRoom, 0);
            room = find_room_of_role_with_spare_room_item_capacity(plyr_idx, RoRoF_PowersStorage);
            if (!room_is_invalid(room))
            {
                pos.x.val = subtile_coord_center(room->central_stl_x);
                pos.y.val = subtile_coord_center(room->central_stl_y);
                pos.z.val = get_floor_height_at(&pos);
                create_effect(&pos, TngEff_ResearchComplete, room->owner);
            }
            research_completed = true;
        }
        break;
    case RsCat_Creature:
        if (dungeon->creature_allowed[rsrchval->rkind])
        {
            dungeon->creature_force_enabled[rsrchval->rkind]++;
            research_completed = true;
        }
        else 
        {          
            room = find_room_of_role_with_spare_room_item_capacity(plyr_idx, RoRoF_PowersStorage);
            if (!room_is_invalid(room))
            {
                pos.x.val = subtile_coord_center(room->central_stl_x);
                pos.y.val = subtile_coord_center(room->central_stl_y);
                pos.z.val = get_floor_height_at(&pos);
                create_effect(&pos, TngEff_ResearchComplete, room->owner);
            }
            dungeon->creature_allowed[rsrchval->rkind]++;
            research_completed = true;
        }
        break;
    default:
        ERRORLOG("Illegal research type %" PRId64 " while processing player %" PRId64 " research",(int64_t)rsrchval->rtyp,(int64_t)plyr_idx);
        break;
    }

    if(research_completed)
        send_research_complete_event(rsrchval, plyr_idx);
    dungeon->research_progress -= (rsrchval->req_amount << 8);
    dungeon->last_research_complete_gameturn = get_gameturn();

    dungeon->current_research_idx = get_next_research_item(dungeon);
    dungeon->lvstats.things_researched++;
    return;
}

void send_research_complete_event(struct ResearchVal *rsrchval, PlayerNumber plyr_idx)
{
    const char *kind_description;

    switch (rsrchval->rtyp)
    {
        case RsCat_Power:
            kind_description = power_code_name((PowerKind)rsrchval->rkind);
            break;
        case RsCat_Room:
            kind_description = room_code_name((RoomKind)rsrchval->rkind);
            break;
        case RsCat_Creature:
            kind_description = creature_code_name((ThingModel)rsrchval->rkind);
            break;
        default:
            kind_description = "INVALID";
            break;
    }

    struct ApiEventData event_data[] = {
        {"player", API_EVENT_DATA_INT32, {.int32_value = (int64_t)plyr_idx}},
        {"category", API_EVENT_DATA_INT32, {.int32_value = (int64_t)rsrchval->rtyp}},
        {"kind", API_EVENT_DATA_INT32, {.int32_value = (int64_t)rsrchval->rkind}},
        {"kind_description", API_EVENT_DATA_STRING, {.string_value = kind_description}},
        {"level_number", API_EVENT_DATA_INT32, {.int32_value = get_loaded_level_number()}}
    };

    script_api_event_with_data("RESEARCH_COMPLETED",event_data,sizeof(event_data) / sizeof(event_data[0]));
}

void update_research(void)
{
    int64_t i;
    struct PlayerInfo *player;
    SYNCDBG(6,"Starting");
    for (i = 0; i < PLAYERS_COUNT; i++)
    {
        player = get_player(i);
        if (is_active_keeper(player))
        {
            process_player_research(i);
        }
    }
}

void research_found_room(PlayerNumber plyr_idx, RoomKind rkind)
{
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
     // Player got room to build instantly
    if ((dungeon->room_resrchable[rkind] == 2)
        || (dungeon->room_resrchable[rkind] == 3)
        )
    {
        dungeon->room_buildable[rkind] = 3;
    }
    else
    {
        // Player may research room then it is claimed
        dungeon->room_buildable[rkind] |= 2; 
    }
}

static TbBool library_stores(const struct Room *room, const struct Thing *thing)
{
    return thing_is_spellbook(thing) && (book_thing_to_power_kind(thing) > 0) && ((thing->alloc_flags & TAlF_IsDragged) == 0);
}

static void library_take_out(struct Room *room, struct Thing *thing, struct RoomReposition *rrepos)
{
    struct Dungeon* dungeon;
    ThingModel objkind = thing->model;
    PowerKind spl_idx = book_thing_to_power_kind(thing);
    if (get_gameturn() > 10) //Function is used to place books in rooms before dungeons are intialized
    {
        dungeon = get_players_num_dungeon(room->owner);
        if (dungeon->magic_level[spl_idx] < 2)
        {
            if (!store_reposition_entry(rrepos, objkind)) {
                WARNLOG("Too many things to reposition in %s.", room_code_name(room->kind));
            }
        }
        if (!is_neutral_thing(thing))
        {
            remove_power_from_player(spl_idx, room->owner);
            dungeon = get_dungeon(room->owner);
            dungeon->magic_resrchable[spl_idx] = 1;
        }
    }
    else
    {
        if (!store_reposition_entry(rrepos, objkind))
        {
            WARNLOG("Too many things to reposition in %s.", room_code_name(room->kind));
        }
        if (!is_neutral_thing(thing))
        {
            remove_power_from_player(spl_idx, room->owner);
        }
    }
    destroy_object(thing);
}

static struct Thing *library_put_back(struct Room *room, ThingModel model, CrtrExpLevel exp_level, MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{
    return create_spell_in_library(room, model, stl_x, stl_y);
}

int64_t position_books_in_room_with_capacity(PlayerNumber plyr_idx, RoomKind rkind, struct RoomReposition* rrepos)
{
    struct Room* room = find_room_of_role_with_spare_room_item_capacity(plyr_idx, RoRoF_PowersStorage);
    struct Coord3d pos;
    uint64_t k = 0;
    int64_t i = room->index;
    int64_t count = 0;
    while (i != 0)
    {
        if (room_is_invalid(room))
        {
            ERRORLOG("Jump to invalid room detected");
            break;
        }
        // Per-room code
        pos.x.val = subtile_coord_center(room->central_stl_x);
        pos.y.val = subtile_coord_center(room->central_stl_y);
        pos.z.val = get_floor_height_at(&pos);

        for (int64_t ri = 0; ri < ROOM_REPOSITION_COUNT; ri++)
        {
            if (rrepos->models[ri] != 0)
            {
                struct Thing* spelltng = create_spell_in_library(room, rrepos->models[ri], room->central_stl_x, room->central_stl_y);
                if (!thing_is_invalid(spelltng))
                {
                    if (!find_random_valid_position_for_thing_in_room_avoiding_object(spelltng, room, &pos))
                    {
                        SYNCDBG(7, "Could not find position in %s for %s artifact", room_code_name(room->kind), object_code_name(spelltng->model));
                        if (!is_neutral_thing(spelltng))
                        {
                            remove_power_from_player(book_thing_to_power_kind(spelltng), plyr_idx);
                        }
                        destroy_object(spelltng);
                    }
                    else
                    {
                        pos.z.val = get_thing_height_at(spelltng, &pos);
                        if (!thing_exists(spelltng))
                        {
                            ERRORLOG("Attempt to reposition non-existing book.");
                            return false;
                        }
                        move_thing_in_map(spelltng, &pos);
                        create_effect(&pos, TngEff_RoomSparkeLarge, spelltng->owner);
                        rrepos->used--;
                        rrepos->models[ri] = 0;
                        count++;
                    }
                }
            }
        }
        if (rrepos->used <= 0)
        {
            SYNCDBG(7,"Nothing left to reposition");
            break;
        }
        room = find_room_of_role_with_spare_room_item_capacity(plyr_idx, RoRoF_PowersStorage);
        if (room_is_invalid(room))
        {
            SYNCLOG("Could not find any spare %s capacity for %" PRId64 " remaining books", room_role_code_name(RoRoF_PowersStorage), (int64_t)(rrepos->used));
            i = 0;
            break;
        }
        i = room->index;
        // Per-room code ends
        k++;
        if (k > ROOMS_COUNT)
        {
            ERRORLOG("Infinite loop detected when sweeping rooms list");
            break;
        }
    }
    return count;
}

int64_t check_books_on_subtile_for_reposition_in_room(struct Room *room, MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{
    struct Map* mapblk = get_map_block_at(stl_x, stl_y);
    if (map_block_invalid(mapblk))
        return -2; // do nothing
    struct RoomConfigStats* roomst = get_room_kind_stats(room->kind);
    if ((roomst->storage_height >= 0) && (get_floor_filled_subtiles_at(stl_x, stl_y) != roomst->storage_height)) {
        return -1; // re-create all
    }
    int64_t matching_things_at_subtile = 0;
    FOR_EACH_THING(thing, thing_walk_map_block(mapblk))
    {
        if (thing_is_spellbook(thing))
        {
            PowerKind spl_idx = book_thing_to_power_kind(thing);
            if ((spl_idx > 0) && ((thing->alloc_flags & TAlF_IsDragged) == 0) && ((thing->owner == room->owner) || get_gameturn() < 10))//Function is used to integrate preplaced books at map startup too.
            {
                // If the library is full (or already one book over it, with the LIBRARY_EXTRA_BOOK classic bug)
                const TbBool extra_book = flag_is_set(kfx_config_state.conf.rules[room->owner].gameplay.classic_bugs_flags, ClscBug_LibraryExtraBook);
                if (extra_book ? (room->used_capacity > room->total_capacity) : (room->used_capacity >= room->total_capacity))
                {
                    SYNCDBG(7,"Room %" PRId64 " type %s capacity %" PRId64 " exceeded; space used is %" PRId64, (int64_t)(room->index), room_code_name(room->kind), (int64_t)room->total_capacity, (int64_t)room->used_capacity);
                    struct Dungeon* dungeon = get_players_num_dungeon(room->owner);
                    if (dungeon->magic_level[spl_idx] <= 1)
                    { 
                        // We have a single copy, but nowhere to place it. -1 will handle the rest.
                        return -1;
                    }
                    else // We have more than one copy, so we can just delete the book.
                    {
                        if (!is_neutral_thing(thing))
                        {
                            remove_power_from_player(spl_idx, thing->owner);
                        }
                        SYNCLOG("Deleting from %s of player %" PRId64 " duplicate object %s", room_code_name(room->kind), (int64_t)thing->owner, object_code_name(thing->model));
                        destroy_thing(thing);
                    }

                } else // we have capacity to spare, so it can stay unless it's stuck
                if (thing_in_wall_at(thing, &thing->mappos)) 
                {
                    if (position_over_floor_level(thing, &thing->mappos)) //If it's inside the floors, simply move it up and count it.
                    {
                        matching_things_at_subtile++; 
                    }
                    else
                    {
                        return -1; // If it's inside the wall or cannot be moved up, recreate all items.
                    }
                } else
                {
                    matching_things_at_subtile++;
                }
            }
        }
    }
    if (matching_things_at_subtile == 0)
    {
        if (room->used_capacity == room->total_capacity) // When 0 is returned, it would try to place books at this subtile. When at capacity already, return -2 to refuse that.
        {
            return -2;
        }
    }
    return matching_things_at_subtile; // Increase used capacity
}

/** Books that fit nowhere in this library move to the player's other libraries, as far as they have space. */
static void library_overflow(struct Room *room, struct RoomReposition *rrepos)
{
    int64_t move_count = position_books_in_room_with_capacity(room->owner, room->kind, rrepos);
    if (move_count > 0)
    {
        if (rrepos->used > 0)
        {
            SYNCLOG("The %s capacity wasn't enough, %" PRId64 " moved, but %" PRId64 " items belonging to player %" PRId64 " dropped",
                room_code_name(room->kind), (int64_t)(move_count), (int64_t)rrepos->used, (int64_t)room->owner);
        }
        else
        {
            SYNCDBG(7,"Moved %" PRId64 " items belonging to player %" PRId64 " to different %s",
                (int64_t)(move_count), (int64_t)room->owner, room_code_name(room->kind));
        }
    }
    else
    {
        SYNCLOG("No %s capacity available to move, %" PRId64 " items belonging to player %" PRId64 " dropped",
            room_code_name(room->kind), (int64_t)rrepos->used, (int64_t)room->owner);
    }
}

/** Books: the library has its own subtile check; capacity_used_for_storage follows the count, two sweeps. */
static const struct RoomStorageKind library_storage = { "books", library_stores, library_take_out, library_put_back,
    check_books_on_subtile_for_reposition_in_room, library_overflow, 0, 1, 0 };

void count_books_in_room(struct Room *room)
{
    room_storage_recount(&library_storage, room);
}
/******************************************************************************/
