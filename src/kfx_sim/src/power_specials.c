/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file power_specials.c
 *     power_specials support functions.
 * @par Purpose:
 *     Functions to power_specials.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     11 Mar 2010 - 12 May 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "power_specials.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_math.h"

#include "thing_creature.h"
#include "thing_navigate.h"
#include "thing_effects.h"
#include "config_terrain.h"
#include "player_data.h"
#include "dungeon_data.h"
#include "creature_control.h"
#include "creature_states_pray.h"
#include "power_hand.h"
#include "map_blocks.h"
#include "spdigger_stack.h"
#include "thing_corpses.h"
#include "thing_objects.h"
#include "sim_scratch.h"
#include "config.h"

#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "thing_stats.h"
#include "player_camera.h"
#include "ports/script_port.h"
#include "ports/ui_port.h"
#include "ports/audio_port.h"
#include "ports/game_port.h"
#include "list_walk.h"
#include "post_inc.h"

// TRANSFER_CREATURE_STORAGE_COUNT (kfx_game's game_merge.h) literal-
// duplicated -- only this file uses it, not worth pulling in game_merge.h
// just for one constant. create_transferred_creatures_on_level() reaches
// kfx_game's intralvl data via game_get_transferred_creature()
// instead of the struct directly.
#define TRANSFER_CREATURE_STORAGE_COUNT 255

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/******************************************************************************/
#ifdef __cplusplus
}
#endif
/******************************************************************************/
/******************************************************************************/

/**
 * Increases creatures' levels for player.
 * @param plyr_idx target player
 * @param count how many levels up (or, negative, down); limited to -9..9, and 0 counts as 1, as for the level
 *     script's USE_SPECIAL_INCREASE_LEVEL (which checks when the script loads; Lua calls come here unchecked)
 */
void script_use_special_increase_level(PlayerNumber plyr_idx, int64_t count)
{
    if (count == 0)
    {
        WARNLOG("Invalid count: %" PRId64 ", setting to 1.", (int64_t)(count));
        count = 1;
    }
    if (count > 9)
    {
        WARNLOG("Count too high: %" PRId64 ", setting to 9.", (int64_t)(count));
        count = 9;
    }
    if (count < -9)
    {
        WARNLOG("Count too low: %" PRId64 ", setting to -9.", (int64_t)(count));
        count = -9;
    }
    increase_level(get_player(plyr_idx), count);
}

/**
 * Multiplies every creature for player.
 * @param plyr_idx target player
 */
void script_use_special_multiply_creatures(PlayerNumber plyr_idx)
{
    multiply_creatures(get_player(plyr_idx));
}

/**
 * Fortifies player's dungeon.
 * @param plyr_idx target player
 */
void script_make_safe(PlayerNumber plyr_idx)
{
    make_safe(get_player(plyr_idx));
}

/**
 * Fortifies player's dungeon.
 * @param plyr_idx target player
 */
void script_make_unsafe(PlayerNumber plyr_idx)
{
    make_unsafe(plyr_idx);
}

/**
 * Enables bonus level for current player.
 */
TbBool script_locate_hidden_world()
{
    return activate_bonus_level(get_player(my_player_number));
}


/**
 * Makes a bonus level for current SP level visible on the land map screen.
 */
TbBool activate_bonus_level(struct PlayerInfo *player)
{
  SYNCDBG(5,"Starting");
  LevelNumber sp_lvnum = get_loaded_level_number();
  TbBool result = game_activate_bonus_level_for_singleplayer(player, sp_lvnum);
  if (!result)
    ERRORLOG("No Bonus level assigned to level %" PRId64,(int64_t)sp_lvnum);
  clear_flag(kfx_sim_state.operation_flags, GOF_SingleLevel);
  return result;
}

void multiply_creatures_in_dungeon_list(struct Dungeon *dungeon, int64_t list_start)
{
    FOR_EACH_THING(thing, thing_walk_creatures(list_start, CREATURES_COUNT))
    {
        struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
        if (!creature_count_below_map_limit(0))
        {
            WARNLOG("Can't duplicate all creature due to map creature limit.");
            break;
        }
        struct Thing* tncopy = create_creature(&thing->mappos, thing->model, dungeon->owner);
        if (thing_is_invalid(tncopy))
        {
            WARNLOG("Can't create a copy of creature %s", thing_model_name(thing));
            continue;
        }
        struct CreatureControl* newcctrl = creature_control_get_from_thing(tncopy);
        set_creature_level(tncopy, cctrl->exp_level);
        tncopy->health = thing->health;
        newcctrl->exp_points = cctrl->exp_points;
        newcctrl->blood_type = cctrl->blood_type;
        newcctrl->hunger_level = cctrl->hunger_level;
        newcctrl->paydays_owed = cctrl->paydays_owed;
        newcctrl->paydays_advanced = cctrl->paydays_advanced;
        for (unsigned char al = 0; al < AngR_ListEnd; al++)
        {
            newcctrl->annoyance_level[al] = cctrl->annoyance_level[al];
        }
    }
}

void multiply_creatures(struct PlayerInfo *player)
{
    struct Dungeon* dungeon = get_players_dungeon(player);
    // Copy 'normal' creatures
    multiply_creatures_in_dungeon_list(dungeon, dungeon->creatr_list_start);
    // Copy 'special digger' creatures
    multiply_creatures_in_dungeon_list(dungeon, dungeon->digger_list_start);
}

void increase_level(struct PlayerInfo *player, int64_t count)
{
    struct Dungeon* dungeon = get_dungeon(player->id_number);
    // Increase level of normal creatures
    FOR_EACH_THING(thing, thing_walk_creatures(dungeon->creatr_list_start, CREATURES_COUNT))
    {
        if (count != 1) creature_change_multiple_levels(thing, count);
        else creature_increase_level(thing);
    }
    // Increase level of special diggers: the same rule (a count other than 1 used to raise them one level
    // whenever it wasn't above 1, so a level-down raised them; refactor pass 4 finding P4-F1)
    FOR_EACH_THING(thing, thing_walk_creatures(dungeon->digger_list_start, CREATURES_COUNT))
    {
        if (count != 1) creature_change_multiple_levels(thing, count);
        else creature_increase_level(thing);
    }
    for (int64_t i = 0; i < dungeon->num_summon; i++)
    {
        struct Thing* famlrtng = thing_get(dungeon->summon_list[i]);
        if (!thing_exists(famlrtng))
        {
          ERRORLOG("Jump to invalid creature detected");
          continue;
        }
        // They follow their summoner down as well as up (P4-F1)
        if (count < 0) familiar_follow_summoner_level(famlrtng);
        else level_up_familiar(famlrtng);
    }
}

TbBool steal_hero(struct PlayerInfo *player, struct Coord3d *pos)
{
    struct Thing* herotng = INVALID_THING;
    int64_t heronum;
    ThingIndex tng_idx;
    SYNCDBG(8, "Starting");
    int64_t rand_offset = GAME_RANDOM(PLAYERS_COUNT);
    for (size_t j = 0; j < PLAYERS_COUNT; j++)
    {
        PlayerNumber roam_plr_idx = (j + rand_offset) % PLAYERS_COUNT;
        if ((!player_is_roaming(roam_plr_idx)) || (!players_are_enemies(player->id_number, roam_plr_idx)))
            continue;
        struct Dungeon* herodngn = get_players_num_dungeon(roam_plr_idx);
        uint64_t k = 0;
        if (herodngn->num_active_creatrs > 0) {
            heronum = PLAYER_RANDOM(roam_plr_idx, herodngn->num_active_creatrs);
            tng_idx = herodngn->creatr_list_start;
            SYNCDBG(4, "Selecting random creature %" PRId64 " out of %" PRId64 " heroes", (int64_t)heronum, (int64_t)herodngn->num_active_creatrs);
        } else {
            heronum = 0;
            tng_idx = 0;
            SYNCDBG(4, "No heroes on map, skipping selection");
        }
        while (tng_idx != 0)
        {
            struct Thing* thing = thing_get(tng_idx);
            TRACE_THING(thing);
            struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
            if (thing_is_invalid(thing) || creature_control_invalid(cctrl))
            {
                ERRORLOG("Jump to invalid creature detected");
                break;
            }
            tng_idx = cctrl->players_next_creature_idx;
            // Thing list loop body.
            if (!flag_is_set(get_creature_model_flags(thing), CMF_NoStealHero)) {
                herotng = thing;
            }
            // If we've reached requested hero number, return either current hero on previously selected one.
            if ((heronum <= 0) && thing_is_creature(herotng)) {
                break;
            }
            heronum--;
            if (tng_idx == 0) {
                tng_idx = herodngn->creatr_list_start;
            }
            // Thing list loop body ends.
            k++;
            if (k > CREATURES_COUNT)
            {
                ERRORLOG("Infinite loop detected when sweeping creatures list");
                break;
            }
        }
    }
    if (!thing_is_invalid(herotng))
    {
        move_thing_in_map(herotng, pos);
        reset_interpolation_of_thing(herotng);
        change_creature_owner(herotng, player->id_number);
        SYNCDBG(3, "Converted %s to owner %" PRId64, thing_model_name(herotng), (int64_t)player->id_number);
    }
    else
    {
        if (!creature_count_below_map_limit(0))
        {
            SYNCDBG(7, "Failed to generate a stolen hero due to map creature limit");
            return false;
        }
        ThingModel crkind = get_random_creature_kind_with_model_flags(CMF_PreferSteal);
        if (crkind == -1)
        {
            SYNCDBG(7, "Failed to generate a stolen hero due to lack of model with the property");
            return false;
        }
        struct Thing* creatng = create_creature(pos, crkind, player->id_number);
        if (thing_is_invalid(creatng))
            return false;
        SYNCDBG(3, "Created %s owner %" PRId64, thing_model_name(creatng), (int64_t)player->id_number);
    }
    return true;
}

void make_safe(struct PlayerInfo *player)
{
    unsigned char* areamap = (unsigned char*)big_scratch;
    MapSlabCoord slb_x;
    MapSlabCoord slb_y;
    // Prepare the array to remember which slabs were already taken care of
    for (slb_y=0; slb_y < kfx_sim_state.map_tiles_y; slb_y++)
    {
        for (slb_x=0; slb_x < kfx_sim_state.map_tiles_x; slb_x++)
        {
            SlabCodedCoords slb_num = get_slab_number(slb_x, slb_y);
            struct SlabMap* slb = get_slabmap_direct(slb_num);
            struct SlabConfigStats* slabst = get_slab_stats(slb);
            if ((slabst->block_flags & (SlbAtFlg_Filled|SlbAtFlg_Digable|SlbAtFlg_Valuable)) != 0)
                areamap[slb_num] = 0x01;
            else
                areamap[slb_num] = 0x00;
        }
    }
    {
        const struct Coord3d* center_pos = dungeon_get_essential_pos(player->id_number);
        slb_x = subtile_slab(center_pos->x.stl.num);
        slb_y = subtile_slab(center_pos->y.stl.num);
        SlabCodedCoords slb_num = get_slab_number(slb_x, slb_y);
        areamap[slb_num] |= 0x02;
    }

    PlayerNumber plyr_idx = player->id_number;
    SlabCodedCoords* slblist = (SlabCodedCoords*)(big_scratch + kfx_sim_state.map_tiles_x * kfx_sim_state.map_tiles_y);
    uint64_t list_len = 0;
    uint64_t list_cur = 0;
    struct Room* room_list[ROOMS_COUNT + 1];
    memset(room_list, 0, sizeof(room_list));

    while (list_cur <= list_len)
    {
        SlabCodedCoords slb_num;

        if (slb_x > 0)
        {
            slb_num = get_slab_number(slb_x-1, slb_y);
            if ((areamap[slb_num] & 0x01) != 0)
            {
                areamap[slb_num] |= 0x02;
                struct SlabMap* slb = get_slabmap_direct(slb_num);
                struct SlabConfigStats* slabst = get_slab_stats(slb);
                if ((slabst->category == SlbAtCtg_FriableDirt) && slab_by_players_land(plyr_idx, slb_x-1, slb_y))
                {
                    unsigned char pretty_type = choose_pretty_type(plyr_idx, slb_x - 1, slb_y);
                    place_slab_type_on_map(pretty_type, slab_subtile(slb_x-1,0), slab_subtile(slb_y,0), plyr_idx, 0);
                    collect_rooms_around_slab(slb_x-1, slb_y, room_list, sizeof(room_list)/sizeof(room_list[0]));
                    fill_in_reinforced_corners(plyr_idx, slb_x-1, slb_y);
                }
            } else
            if ((areamap[slb_num] & 0x02) == 0)
            {
                areamap[slb_num] |= 0x02;
                slblist[list_len] = slb_num;
                list_len++;
            }
        }
        if (slb_x < kfx_sim_state.map_tiles_x-1)
        {
            slb_num = get_slab_number(slb_x+1, slb_y);
            if ((areamap[slb_num] & 0x01) != 0)
            {
                areamap[slb_num] |= 0x02;
                struct SlabMap* slb = get_slabmap_direct(slb_num);
                struct SlabConfigStats* slabst = get_slab_stats(slb);
                if ((slabst->category == SlbAtCtg_FriableDirt) &&  slab_by_players_land(plyr_idx, slb_x+1, slb_y))
                {
                    unsigned char pretty_type = choose_pretty_type(plyr_idx, slb_x + 1, slb_y);
                    place_slab_type_on_map(pretty_type, slab_subtile(slb_x+1,0), slab_subtile(slb_y,0), plyr_idx, 0);
                    collect_rooms_around_slab(slb_x+1, slb_y, room_list, sizeof(room_list)/sizeof(room_list[0]));
                    fill_in_reinforced_corners(plyr_idx, slb_x+1, slb_y);
                }
            } else
            if ((areamap[slb_num] & 0x02) == 0)
            {
                areamap[slb_num] |= 0x02;
                slblist[list_len] = slb_num;
                list_len++;
            }
        }
        if (slb_y > 0)
        {
            slb_num = get_slab_number(slb_x, slb_y-1);
            if ((areamap[slb_num] & 0x01) != 0)
            {
                areamap[slb_num] |= 0x02;
                struct SlabMap* slb = get_slabmap_direct(slb_num);
                struct SlabConfigStats* slabst = get_slab_stats(slb);
                if ((slabst->category == SlbAtCtg_FriableDirt) && slab_by_players_land(plyr_idx, slb_x, slb_y-1))
                {
                    unsigned char pretty_type = choose_pretty_type(plyr_idx, slb_x, slb_y - 1);
                    place_slab_type_on_map(pretty_type, slab_subtile(slb_x,0), slab_subtile(slb_y-1,0), plyr_idx, 0);
                    collect_rooms_around_slab(slb_x, slb_y-1, room_list, sizeof(room_list)/sizeof(room_list[0]));
                    fill_in_reinforced_corners(plyr_idx, slb_x, slb_y-1);
                }
            } else
            if ((areamap[slb_num] & 0x02) == 0)
            {
                areamap[slb_num] |= 0x02;
                slblist[list_len] = slb_num;
                list_len++;
            }
        }
        if (slb_y < kfx_sim_state.map_tiles_y-1)
        {
            slb_num = get_slab_number(slb_x, slb_y+1);
            if ((areamap[slb_num] & 0x01) != 0)
            {
                areamap[slb_num] |= 0x02;
                struct SlabMap* slb = get_slabmap_direct(slb_num);
                struct SlabConfigStats* slabst = get_slab_stats(slb);
                if ((slabst->category == SlbAtCtg_FriableDirt) && slab_by_players_land(plyr_idx, slb_x, slb_y+1))
                {
                    unsigned char pretty_type = choose_pretty_type(plyr_idx, slb_x, slb_y + 1);
                    place_slab_type_on_map(pretty_type, slab_subtile(slb_x,0), slab_subtile(slb_y+1,0), plyr_idx, 0);
                    collect_rooms_around_slab(slb_x, slb_y+1, room_list, sizeof(room_list)/sizeof(room_list[0]));
                    fill_in_reinforced_corners(plyr_idx, slb_x, slb_y+1);
                }
            } else
            if ((areamap[slb_num] & 0x02) == 0)
            {
                areamap[slb_num] |= 0x02;
                slblist[list_len] = slb_num;
                list_len++;
            }
        }

        slb_x = slb_num_decode_x(slblist[list_cur]);
        slb_y = slb_num_decode_y(slblist[list_cur]);
        list_cur++;
    }
    recalculate_rooms_in_list(room_list, sizeof(room_list)/sizeof(room_list[0]));
    ui_panel_map_update(0, 0, kfx_sim_state.map_subtiles_x+1, kfx_sim_state.map_subtiles_y+1);
}

void make_unsafe(PlayerNumber plyr_idx)
{
    MapSlabCoord slb_x;
    MapSlabCoord slb_y;
    SlabCodedCoords slb_num;
    struct SlabMap* slb;
    struct SlabConfigStats* slabst;
    struct PowerConfigStats* powerst;
    struct Dungeon* dungeon;
    struct Coord3d pos;
    struct Room* room_list[ROOMS_COUNT + 1];
    memset(room_list, 0, sizeof(room_list));

    for (slb_y = 0; slb_y < kfx_sim_state.map_tiles_y; slb_y++)
    {
        for (slb_x = 0; slb_x < kfx_sim_state.map_tiles_x; slb_x++)
        {
            slb_num = get_slab_number(slb_x, slb_y);
            slb = get_slabmap_direct(slb_num);
            if (slabmap_owner(slb) == plyr_idx)
            {
                slabst = get_slab_stats(slb);
                if (slabst->category == SlbAtCtg_FortifiedWall)
                {
                    SlabKind newslab = choose_rock_type(plyr_idx, slb_x, slb_y);
                    dungeon = get_dungeon(plyr_idx);
                    kfx_sim_view_signals.camera_deviate_jump[dungeon->owner] = kfx_sim_view_signals.camera_deviate_jump[dungeon->owner] + 3; //Bigger jump on more slabs changed
                    kfx_sim_view_signals.camera_deviate_quake[dungeon->owner] = 30; //30 frames of camera shaking

                    set_coords_to_slab_center(&pos, slb_x, slb_y);
                    powerst = get_power_model_stats(PwrK_DESTRWALLS);
                    audio_play_sound_if_close_to_receiver(&pos, powerst->select_sound_idx);
                    place_slab_type_on_map(newslab, slab_subtile_center(slb_x), slab_subtile_center(slb_y), kfx_config_state.neutral_player_num, 0);
                    collect_rooms_around_slab(slb_x, slb_y, room_list, sizeof(room_list)/sizeof(room_list[0]));
                }
            }
        }
    }
    recalculate_rooms_in_list(room_list, sizeof(room_list)/sizeof(room_list[0]));
    ui_panel_map_update(0, 0, kfx_sim_state.map_subtiles_x + 1, kfx_sim_state.map_subtiles_y + 1);
}

void activate_dungeon_special(struct Thing *cratetng, struct PlayerInfo *player)
{
    SYNCDBG(6,"Starting");
    struct Coord3d pos;

    script_lua_on_special_box_activate(player->id_number,cratetng);

    // Gathering data which we'll need if the special is used and disposed.
    struct Dungeon* dungeon = get_dungeon(player->id_number);
    memcpy(&pos,&cratetng->mappos,sizeof(struct Coord3d));
    SpecialKind spkindidx = box_thing_to_special(cratetng);
    struct SpecialConfigStats* specst = get_special_model_stats(spkindidx);
    int64_t used = 0;
    TbBool no_speech = false;
    if (thing_exists(cratetng) && thing_is_special_box(cratetng))
    {
        
    struct ApiEventData event_data[] = {
        { .name = "player", .type = API_EVENT_DATA_INT32, .value.int32_value = player->id_number },
        { .name = "special", .type = API_EVENT_DATA_INT32, .value.int32_value = spkindidx },
        { .name = "special_name", .type = API_EVENT_DATA_STRING, .value.string_value = specst->code_name },
        { .name = "special_value", .type = API_EVENT_DATA_INT32, .value.int32_value = specst->value },
        { .name = "thing", .type = API_EVENT_DATA_INT32, .value.int32_value = cratetng->index },
        { .name = "model", .type = API_EVENT_DATA_INT32, .value.int32_value = cratetng->model },
        { .name = "x", .type = API_EVENT_DATA_INT32, .value.int32_value = cratetng->mappos.x.val >> 8},
        { .name = "y", .type = API_EVENT_DATA_INT32, .value.int32_value = cratetng->mappos.y.val >> 8 },
        { .name = "z", .type = API_EVENT_DATA_INT32, .value.int32_value = cratetng->mappos.z.val >> 8 },
        { .name = "level_number", .type = API_EVENT_DATA_INT32, .value.int32_value = get_loaded_level_number() },
        { .name = "game_turn", .type = API_EVENT_DATA_UINT64, .value.uint64_value = (uint64_t)get_gameturn() },
    };
    script_api_event_with_data("SPECIAL_ACTIVATED", event_data, sizeof(event_data) / sizeof(event_data[0]));

    switch (spkindidx)
    {
        case SpcKind_RevealMap:
            reveal_whole_map(player);
            remove_events_thing_is_attached_to(cratetng);
            used = 1;
            destroy_object(cratetng);
            break;
        case SpcKind_Resurrect:
            start_resurrect_creature(player, cratetng);
            break;
        case SpcKind_TrnsfrCrtr:
            start_transfer_creature(player, cratetng);
            break;
        case SpcKind_StealHero:
            if (steal_hero(player, &cratetng->mappos))
            {
                remove_events_thing_is_attached_to(cratetng);
                used = 1;
                destroy_object(cratetng);
            }
            break;
        case SpcKind_MultplCrtr:
            multiply_creatures(player);
            remove_events_thing_is_attached_to(cratetng);
            used = 1;
            destroy_object(cratetng);
            break;
        case SpcKind_IncrseLvl:
            increase_level(player, 1);
            remove_events_thing_is_attached_to(cratetng);
            used = 1;
            destroy_object(cratetng);
            break;
        case SpcKind_MakeSafe:
            make_safe(player);
            remove_events_thing_is_attached_to(cratetng);
            used = 1;
            delete_thing_structure(cratetng, 0);
            break;
        case SpcKind_MakeUnsafe:
            for (int64_t i = 0; i < PLAYERS_COUNT; i++)
            {
                if (players_are_enemies(player->id_number, i))
                {
                    make_unsafe(i);
                }
            }
            remove_events_thing_is_attached_to(cratetng);
            used = 1;
            destroy_object(cratetng);
            break;
        case SpcKind_HiddnWorld:
            activate_bonus_level(player);
            remove_events_thing_is_attached_to(cratetng);
            used = 1;
            delete_thing_structure(cratetng, 0);
            break;
        case SpcKind_HealAll:
            do_to_players_all_creatures_of_model(player->id_number, CREATURE_ANY, set_creature_health_to_max_with_heal_effect);
            remove_events_thing_is_attached_to(cratetng);
            used = 1;
            destroy_object(cratetng);
            break;
        case SpcKind_GetGold:
            throw_out_gold(cratetng, specst->value);
            remove_events_thing_is_attached_to(cratetng);
            used = 1;
            destroy_object(cratetng);
            break;
        case SpcKind_MakeAngry:
            for (int64_t i = 0; i < PLAYERS_COUNT; i++)
            {
                if (players_are_enemies(player->id_number, i))
                {
                    add_anger_to_all_creatures_of_player(i, specst->value);
                }
            }
            remove_events_thing_is_attached_to(cratetng);
            used = 1;
            destroy_object(cratetng);
            break;
        case SpcKind_Custom:
        default:
            if (thing_is_custom_special_box(cratetng))
            {
                if (kfx_sim_state.current_player_turn == get_gameturn())
                {
                    WARNLOG("box activation rejected turn:%" PRIu64, (uint64_t)(kfx_sim_state.current_player_turn));
                    // If two players suddenly activated box at same turn it is not that we want to
                    return;
                }
                kfx_sim_state.current_player_turn = get_gameturn();
                kfx_sim_state.script_current_player = player->id_number;
                memcpy(&kfx_sim_state.triggered_object_location, &pos, sizeof(struct Coord3d));
                dungeon->box_info.activated[cratetng->custom_box.box_kind]++;
                no_speech = true;
                remove_events_thing_is_attached_to(cratetng);
                used = 1;
                destroy_object(cratetng);
                break;
            }
            else
            {
                ERRORLOG("Invalid dungeon special (Model %" PRId64 ")", (int64_t)cratetng->model);
            }
            break;
        }
        if ( used )
        {
            if (is_my_player(player) && !no_speech)
            {
                audio_play_speech_ref(&specst->speech, 0);
            }
            create_used_effect_or_element(&pos, specst->effect_id, player->id_number, cratetng->index);
        }
    }
}

void resurrect_creature(struct Thing *boxtng, PlayerNumber owner, ThingModel crmodel, CrtrExpLevel exp_level)
{
    if (!thing_exists(boxtng) || (box_thing_to_special(boxtng) != SpcKind_Resurrect) ) {
        ERRORMSG("Invalid resurrect box object!");
        return;
    }
    if (!creature_count_below_map_limit(0))
    {
        SYNCLOG("Unable to resurrect creature %s due to map creature limit", creature_code_name(crmodel));
        return;
    }
    struct Thing* creatng = create_creature(&boxtng->mappos, crmodel, owner);
    if (!thing_is_invalid(creatng))
    {
        init_creature_level(creatng, exp_level);
        if (is_my_player_number(owner))
          audio_output_message(SMsg_CommonAcknowledge, 0);
    }
    struct SpecialConfigStats* specst = get_special_model_stats(SpcKind_Resurrect);
    create_used_effect_or_element(&boxtng->mappos, specst->effect_id, owner, boxtng->index);
    remove_events_thing_is_attached_to(boxtng);
    force_any_creature_dragging_owned_thing_to_drop_it(boxtng);
    if ((kfx_config_state.conf.rules[owner].gameplay.classic_bugs_flags & ClscBug_ResurrectForever) == 0) {
        remove_item_from_dead_creature_list(get_players_num_dungeon(owner), crmodel, exp_level);
    }
    delete_thing_structure(boxtng, 0);
}

void transfer_creature(struct Thing *boxtng, struct Thing *transftng, unsigned char plyr_idx)
{
    SYNCDBG(7, "Starting");
    TbBool from_script = false;
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    if (dungeon->dnheart_idx == boxtng->index)
    {
        from_script = true;
    }

    if (!from_script)
    {
        if (!thing_exists(boxtng) || (box_thing_to_special(boxtng) != SpcKind_TrnsfrCrtr)) {
            ERRORMSG("Invalid transfer box object!");
            return;
        }
    }
    // Check if 'things' are correct
    if (!thing_exists(transftng) || !thing_is_creature(transftng) || (transftng->owner != plyr_idx)) {
        ERRORMSG("Invalid transfer creature thing!");
        return;
    }

    struct CreatureControl* cctrl = creature_control_get_from_thing(transftng);
    if (game_add_transfered_creature(plyr_idx, transftng->model, cctrl->exp_level, creature_kept_name(transftng)))
    {
        dungeon->creatures_transferred++;
    }
    remove_thing_from_power_hand_list(transftng, plyr_idx);
    kill_creature(transftng, INVALID_THING, -1, CrDed_NoEffects|CrDed_NotReallyDying);
    if (!from_script)
    {
        struct SpecialConfigStats* specst = get_special_model_stats(SpcKind_TrnsfrCrtr);
        create_used_effect_or_element(&boxtng->mappos, specst->effect_id, plyr_idx, boxtng->index);
        remove_events_thing_is_attached_to(boxtng);
        force_any_creature_dragging_owned_thing_to_drop_it(boxtng);
        delete_thing_structure(boxtng, 0);
    }
    if (is_my_player_number(plyr_idx))
      audio_output_message(SMsg_CommonAcknowledge, 0);
}

void start_transfer_creature(struct PlayerInfo *player, struct Thing *thing)
{
    struct Dungeon* dungeon = get_dungeon(player->id_number);
    if (dungeon->num_active_creatrs != 0)
    {
        if (is_my_player(player))
        {
            audio_output_message(SMsg_SpecTransfer, MESSAGE_DURATION_SPECIAL);
            ui_open_dungeon_special_menu(GMnu_TRANSFER_CREATURE, thing->index);
        }
  }
}

void start_resurrect_creature(struct PlayerInfo *player, struct Thing *thing)
{
    struct Dungeon* dungeon = get_dungeon(player->id_number);
    if (dungeon->dead_creatures_count != 0)
    {
        if (is_my_player(player))
        {
          audio_output_message(SMsg_SpecResurrect, MESSAGE_DURATION_SPECIAL);
          ui_open_dungeon_special_menu(GMnu_RESURRECT_CREATURE, thing->index);
        }
    }
}

int64_t create_transferred_creatures_on_level(void)
{
    struct Thing* creatng;
    struct Thing* srcetng;
    struct CreatureControl* cctrl;
    int64_t creature_created = 0;
    PlayerNumber plyr_idx;
    for (int64_t p = 0; p < PLAYERS_COUNT; p++)
    {
        plyr_idx = kfx_config_state.neutral_player_num;
        for (int64_t i = 0; i < TRANSFER_CREATURE_STORAGE_COUNT; i++)
        {
            ThingModel model;
            CrtrExpLevel exp_level;
            char creature_name[CREATURE_NAME_MAX];
            if (!game_get_transferred_creature(p, i, &model, &exp_level, creature_name, sizeof(creature_name)))
            {
                continue;
            }
            if (model > 0)
            {
                srcetng = get_player_soul_container(p);
                if (player_is_roaming(p))
                {
                    plyr_idx = p;
                    if (!thing_exists(srcetng))
                    {
                        for (int64_t n = 1; n < HERO_GATES_COUNT; n++)
                        {
                            srcetng = find_hero_gate_of_number(n);
                            if (!thing_is_invalid(srcetng))
                                break;
                        }
                    }
                }

                struct Coord3d* pos = &(srcetng->mappos);
                if (!creature_count_below_map_limit(0))
                {
                    WARNLOG("Can't create transferred creature %s due to map creature limit.", creature_code_name(model));
                    continue;
                }
                creatng = create_creature(pos, model, plyr_idx);
                if (thing_is_invalid(creatng))
                {
                    continue;
                }
                init_creature_level(creatng, exp_level);
                cctrl = creature_control_get_from_thing(creatng);
                strcpy(cctrl->creature_name, creature_name);
                creature_created++;
            }
        }
    }
    game_clear_transfered_creatures();
    return creature_created;
}

SpecialKind box_thing_to_special(const struct Thing *thing)
{
    if (thing_is_invalid(thing))
        return 0;
    if ( (thing->class_id != TCls_Object) || (thing->model >= kfx_config_state.conf.object_conf.object_types_count) )
        return 0;
    return kfx_config_state.conf.object_conf.object_to_special_artifact[thing->model];
}
/******************************************************************************/
