/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file player_utils.c
 *     Player data structures definitions.
 * @par Purpose:
 *     Defines functions for player-related structures support.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     10 Nov 2009 - 20 Nov 2012
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "player_utils.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_math.h"
#include "bflib_sound.h"
#include "bflib_netsp.h"
#include "config_sounds.h"
#include "bflib_sndlib.h"
#include "player_data.h"
#include "player_instances.h"
#include "config_players.h"
#include "player_computer_types.h"
#include "dungeon_data.h"
#include "power_hand.h"
#include "thing_objects.h"
#include "thing_effects.h"
#include "room_util.h"
#include "sim_scratch.h"
#include "config_settings.h"
#include "config_keeperfx.h"
#include "config_spritecolors.h"
#include "config_terrain.h"
#include "map_blocks.h"
#include "map_columns.h"
#include "map_utils.h"
#include "map_events.h"
#include "room_entrance.h"
#include "thing_doors.h"
#include "thing_list.h"
#include "slab_data.h"
#include "magic_powers.h"
#include "config.h"
#include "kfx_sim_state.h"
#include "renderer/RendererManager.h"
#include "kfx_config_state.h"
#include "power_process.h"
#include "thing_stats.h"
#include "player_camera.h"
#include "packet_data.h"
#include "light_registry.h"
// initialise_devastate_dungeon_from_heart() (kfx_game's game_loop.h)
// and light_create_light()/light_set_light_never_cache() (kfx_render's
// light_data.h) are reached through ports instead of same-file
// bare-extern forward-declarations. See docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md.
#include "ports/script_port.h"
#include "ports/ui_port.h"
#include "ports/audio_port.h"
#include "ports/game_port.h"
#include "ports/render_port.h"
#include "ports/ai_port.h"
#include "post_inc.h"

/******************************************************************************/

// frontstats_initialise() (kfx_frontend's front_lvlstats.h) is reached
// through UiPort instead of a same-file bare-extern
// forward-declaration. See docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md.

TbBool player_has_lost(PlayerNumber plyr_idx)
{
    struct PlayerInfo* player = get_player(plyr_idx);
    if (player_invalid(player))
        return false;
    return (player->victory_state == VicS_LostLevel);
}

/**
 * Returns whether given player has no longer any chance to win.
 * @param plyr_idx
 * @return
 */
TbBool player_cannot_win(PlayerNumber plyr_idx)
{
    if (plyr_idx == kfx_config_state.neutral_player_num)
        return true;
    struct PlayerInfo* player = get_player(plyr_idx);
    if (!player_exists(player))
        return true;
    if (player->victory_state == VicS_LostLevel)
        return true;
    struct Thing* heartng = get_player_soul_container(player->id_number);
    struct Dungeon* dungeon = get_players_dungeon(player);
    if ((!thing_exists(heartng) || (heartng->active_state == ObSt_BeingDestroyed)) && (dungeon->backup_heart_idx <= 0))
        return true;
    return false;
}

// player dropped and is computer-controlled
static TbBool player_is_ai_standin(const struct PlayerInfo *player)
{
    return flag_is_set(player->allocflags, PlaF_CompCtrl) && flag_is_set(player->allocflags, PlaF_OriginallyHuman);
}

TbBool player_is_victory_candidate(const struct PlayerInfo *player)
{
    return player_exists(player)
        && player->is_active == 1
        && player->id_number != kfx_config_state.neutral_player_num
        && !player_is_ai_standin(player)
        && !player_cannot_win(player->id_number);
}

static TbBool counts_for_alliance_graph(const struct PlayerInfo *player, TbBool humans_only)
{
    return player_is_victory_candidate(player) && (!humans_only || ((player->allocflags & PlaF_CompCtrl) == 0));
}

// check that the graph of alliances among remaining (human/all) players is transitive and reflexive.
TbBool victory_candidates_fully_allied(TbBool humans_only)
{
    for (PlayerNumber i = 0; i < PLAYERS_COUNT; i++) {
        if (!counts_for_alliance_graph(get_player(i), humans_only)) {
            continue;
        }
        for (PlayerNumber j = i + 1; j < PLAYERS_COUNT; j++) {
            if (counts_for_alliance_graph(get_player(j), humans_only) && !players_are_mutual_allies(i, j)) {
                return false;
            }
        }
    }
    return true;
}

void set_player_as_won_level(struct PlayerInfo *player)
{
  if (player->victory_state != VicS_Undecided)
  {
      //WARNLOG("Player fate is already decided to %d",(int)player->victory_state);
      return;
  }
  TbBool my_player = (is_my_player(player));
  struct Dungeon* dungeon = get_dungeon(player->id_number);
  if (my_player)
  {
      script_api_event("WIN_GAME");
      ui_frontstats_initialise();
      if ( ui_timer_enabled() )
      {
        if (kfx_sim_state.TimerGame)
        {
            ui_set_timer_turns(dungeon->lvstats.hopes_dashed);
            ui_update_time();
        }
        else
        {
            ui_show_real_time_taken();
        }
        struct GameTime GT;
        ui_get_game_time(&GT, dungeon->lvstats.hopes_dashed, kfx_sim_state.turns_per_second);
        SYNCMSG("Won level %" PRIu64 ". Total turns taken: %" PRIu64 " (%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64 " at %" PRId64 " fps). Real time elapsed: %02" PRIu64 ":%02" PRIu64 ":%02" PRIu64 ":%03" PRIu64 ".",
            (uint64_t)(get_loaded_level_number()), (uint64_t)(dungeon->lvstats.hopes_dashed),
            (uint64_t)(GT.Hours), (uint64_t)(GT.Minutes), (uint64_t)(GT.Seconds), (int64_t)(kfx_sim_state.turns_per_second),
            (uint64_t)(kfx_sim_state.Timer.Hours), (uint64_t)(kfx_sim_state.Timer.Minutes), (uint64_t)(kfx_sim_state.Timer.Seconds), (uint64_t)(kfx_sim_state.Timer.MSeconds));
      }
  }
  player->victory_state = VicS_WonLevel;
  // Computing player score
  dungeon->lvstats.player_score = compute_player_final_score(player, dungeon->max_gameplay_score);
  dungeon->lvstats.allow_save_score = 1;
  if (!network_is_active())
    player->display_objective_turn = get_gameturn() + 300;
  if (my_player)
  {
    if (lord_of_the_land_in_prison_or_tortured())
    {
        SYNCLOG("Lord Of The Land kept captive. Torture tower unlocked.");
        get_user_state(player->user_id)->additional_flags |= UsrAF_UnlockedLordTorture;
    }
    audio_output_message(SMsg_LevelWon, 0);
  }
}

void set_player_as_lost_level(struct PlayerInfo *player)
{
    if (player->victory_state != VicS_Undecided)
    {
        // Suppress redundant warnings
        if ((kfx_sim_state.system_flags & GSF_RunAfterVictory) == 0)
        {
            WARNLOG("Victory state already set to %" PRId64,(int64_t)player->victory_state);
        }
        return;
    }

    SYNCLOG("%s lost",player_code_name(player->id_number));
    if (is_my_player(player))
    {
        script_api_event("LOSE_GAME");
        ui_frontstats_initialise();
    }
    player->victory_state = VicS_LostLevel;
    struct Dungeon* dungeon = get_dungeon(player->id_number);
    // Computing player score
    dungeon->lvstats.player_score = compute_player_final_score(player, dungeon->max_gameplay_score);
    if (is_my_player(player))
    {
        audio_output_message(SMsg_LevelFailed, 0);
        ui_turn_off_all_menus();
        game_clear_transfered_creatures();
    }
    if ((kfx_config_state.conf.rules[player->id_number].gameplay.classic_bugs_flags & ClscBug_NoHandPurgeOnDefeat) == 0) {
        clear_things_in_hand(player);
        dungeon->num_things_in_hand = 0;
    }
    if (player_uses_power_call_to_arms(player->id_number))
        turn_off_power_call_to_arms(player->id_number);
    if (player_uses_power_sight(player->id_number))
    {
        struct Thing* thing = thing_get(dungeon->sight_casted_thing_idx);
        delete_thing_structure(thing, 0);
        dungeon->sight_casted_thing_idx = 0;
    }
    if (is_my_player(player))
        ui_gui_set_button_flashing(0, 0);
    if (player->view_type == PVT_CreatureContrl)
    {
        struct Thing *thing = thing_get(player->controlled_thing_idx);
        leave_creature_as_controller(player, thing);
    }
    else if (player->view_type == PVT_CreaturePasngr)
    {
        struct Thing *thing = thing_get(player->controlled_thing_idx);
        leave_creature_as_passenger(player, thing);
    }
    else
    {
        if (!flag_is_set(player->allocflags, PlaF_CompCtrl))
        {
            set_player_mode(player, PVT_DungeonTop);
        }
    }
    set_player_state(player, PSt_CtrlDungeon, 0);
    if (!network_is_active())
        player->display_objective_turn = get_gameturn() + 300;
    if (network_is_active())
        reveal_whole_map(player);
    if ((dungeon->computer_enabled & 0x01) != 0)
        ai_toggle_computer_player(player->id_number);
}

int64_t compute_player_final_score(struct PlayerInfo *player, int64_t gameplay_score)
{
    int64_t i;
    if (network_is_active()
      || !is_singleplayer_level(get_loaded_level_number())) {
        i = 2 * gameplay_score;
    } else {
        i = gameplay_score + 10 * gameplay_score * array_index_for_singleplayer_level(get_loaded_level_number()) / 100;
    }
    if (player_has_lost(player->id_number))
        i /= 2;
    return i;
}

/**
 * Takes money from hoards stored in given room.
 * @param room The room which contains gold hoards.
 * @param amount_take Amount of gold to be taken.
 * @return Gives amount of gold taken from room.
 */
GoldAmount take_money_from_room(struct Room *room, GoldAmount amount_take)
{
    GoldAmount amount = amount_take;
    // Remove gold from room border slabs
    uint64_t k = 0;
    uint64_t slbnum = room->slabs_list;
    while (slbnum > 0)
    {
        struct SlabMap* slb = get_slabmap_direct(slbnum);
        if (slabmap_block_invalid(slb)) {
            ERRORLOG("Jump to invalid room slab detected");
            break;
        }
        // Per-slab code starts
        MapSlabCoord slb_x = slb_num_decode_x(slbnum);
        MapSlabCoord slb_y = slb_num_decode_y(slbnum);
        MapSubtlCoord stl_x = slab_subtile_center(slb_x);
        MapSubtlCoord stl_y = slab_subtile_center(slb_y);
        if (slab_is_area_outer_border(slb_x, slb_y))
        {
            struct Thing* hrdtng = find_gold_hoard_at(stl_x, stl_y);
            if (!thing_is_invalid(hrdtng)) {
                amount -= remove_gold_from_hoarde(hrdtng, room, amount);
            }
        }
        if (amount <= 0)
          break;
        // Per-slab code ends
        slbnum = get_next_slab_number_in_room(slbnum);
        k++;
        if (k > kfx_sim_state.map_tiles_x * kfx_sim_state.map_tiles_y)
        {
            ERRORLOG("Infinite loop detected when sweeping room slabs");
            break;
        }
    }
    if (amount <= 0)
        return amount_take-amount;
    // Remove gold from room center only if borders are clear
    k = 0;
    slbnum = room->slabs_list;
    while (slbnum > 0)
    {
        struct SlabMap* slb = get_slabmap_direct(slbnum);
        if (slabmap_block_invalid(slb)) {
            ERRORLOG("Jump to invalid room slab detected");
            break;
        }
        // Per-slab code starts
        MapSubtlCoord stl_x = slab_subtile_center(slb_num_decode_x(slbnum));
        MapSubtlCoord stl_y = slab_subtile_center(slb_num_decode_y(slbnum));
        {
            struct Thing* hrdtng = find_gold_hoard_at(stl_x, stl_y);
            if (!thing_is_invalid(hrdtng)) {
                amount -= remove_gold_from_hoarde(hrdtng, room, amount);
            }
        }
        if (amount <= 0)
          break;
        // Per-slab code ends
        slbnum = get_next_slab_number_in_room(slbnum);
        k++;
        if (k > kfx_sim_state.map_tiles_x * kfx_sim_state.map_tiles_y)
        {
            ERRORLOG("Infinite loop detected when sweeping room slabs");
            break;
        }
    }
    return amount_take-amount;
}

/**
 * Resets dungeon->total_money_owned by taking the offmap gold and gold from all the treasure rooms it can find
  */
void recalculate_total_gold(struct Dungeon* dungeon, const char* func_name)
{
    GoldAmount gold_before = dungeon->total_money_owned;
    dungeon->offmap_money_owned = max(0, dungeon->offmap_money_owned);
    dungeon->total_money_owned = dungeon->offmap_money_owned;

    for (RoomKind rkind = 0; rkind < kfx_config_state.conf.slab_conf.room_types_count; rkind++)
    {
        if (room_role_matches(rkind, RoRoF_GoldStorage))
        {
            int64_t i = dungeon->room_list_start[rkind];
            uint64_t k = 0;
            while (i != 0)
            {
                struct Room* room = room_get(i);
                if (room_is_invalid(room))
                {
                    ERRORLOG("Jump to invalid room detected");
                    break;
                }
                i = room->next_of_owner;
                dungeon->total_money_owned += room->capacity_used_for_storage;
                // Per-room code ends
                k++;
                if (k > ROOMS_COUNT)
                {
                    ERRORLOG("Infinite loop detected when sweeping rooms list");
                    break;
                }
            }
        }
    }
    if (gold_before == dungeon->total_money_owned)
    {
        SYNCDBG(7, "%s: Dungeon %" PRId64 " did not need gold recalculation. Correct at %" PRId64 ".", func_name, (int64_t)(dungeon->owner), (int64_t)(dungeon->total_money_owned));
    }
    else
    {
        ERRORLOG("%s: Gold recalculation found an error, Dungeon %" PRId64 " correct gold amount %" PRId64 " not %" PRId64 ".", func_name, (int64_t)(dungeon->owner), (int64_t)(dungeon->total_money_owned), (int64_t)(gold_before));
    }
}

int64_t take_money_from_dungeon_f(PlayerNumber plyr_idx, GoldAmount amount_take, TbBool only_whole_sum, const char *func_name)
{
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    if (dungeon_invalid(dungeon)) {
        WARNLOG("%s: Cannot take gold from player %" PRId64 " with no dungeon",func_name,(int64_t)plyr_idx);
        return -1;
    }
    GoldAmount take_remain = amount_take;
    GoldAmount total_money = dungeon->total_money_owned;
    if (take_remain <= 0) {
        SYNCDBG(7, "%s: No gold needed to be taken from player %" PRId64,func_name,(int64_t)plyr_idx);
        return 0;
    }
    if (take_remain > total_money)
    {
        SYNCDBG(7,"%s: Player %" PRId64 " has only %" PRId64 " gold, cannot get %" PRId64 " from him",func_name,(int64_t)plyr_idx,(int64_t)total_money,(int64_t)take_remain);
        if ((only_whole_sum) || (total_money == 0)) {
            return -1;
        }
        take_remain = dungeon->total_money_owned;
        amount_take = dungeon->total_money_owned;
    }
    GoldAmount offmap_money = dungeon->offmap_money_owned;
    if (offmap_money > 0)
    {
        if (take_remain <= offmap_money)
        {
            dungeon->offmap_money_owned -= take_remain;
            dungeon->total_money_owned -= take_remain;
            return amount_take;
        }
        take_remain -= offmap_money;
        dungeon->total_money_owned -= offmap_money;
        dungeon->offmap_money_owned = 0;
    }

    for (RoomKind rkind = 0; rkind < kfx_config_state.conf.slab_conf.room_types_count; rkind++)
    {
        if(room_role_matches(rkind,RoRoF_GoldStorage))
        {
            int64_t i = dungeon->room_list_start[rkind];
            uint64_t k = 0;
            while (i != 0)
            {
                struct Room* room = room_get(i);
                if (room_is_invalid(room))
                {
                    ERRORLOG("Jump to invalid room detected");
                    break;
                }
                i = room->next_of_owner;
                // Per-room code
                if (room->capacity_used_for_storage > 0)
                {
                    take_remain -= take_money_from_room(room, take_remain);
                    if (take_remain <= 0)
                    {
                        if (is_my_player_number(plyr_idx))
                        {
                            if ((total_money >= 1000) && (total_money - amount_take < 1000)) {
                                audio_output_message(SMsg_GoldLow, MESSAGE_DURATION_TREASURY);
                            }
                        }
                        return amount_take;
                    }
                }
                // Per-room code ends
                k++;
                if (k > ROOMS_COUNT)
                {
                    ERRORLOG("Infinite loop detected when sweeping rooms list");
                    break;
                }
            }
        }
    }

    WARNLOG("%s: Player %" PRId64 " could not give %" PRId64 " gold, %" PRId64 " was missing; his total gold was %" PRId64,func_name,(int64_t)plyr_idx,(int64_t)amount_take,(int64_t)take_remain,(int64_t)total_money);
    recalculate_total_gold(dungeon, func_name);
    return -1;
}

int64_t update_dungeon_generation_speeds(void)
{
    int64_t plyr_idx;
    // Get value of generation
    int64_t max_manage_score = 0;
    for (plyr_idx=0; plyr_idx < PLAYERS_COUNT; plyr_idx++)
    {
        struct PlayerInfo* player = get_player(plyr_idx);
        if (player_exists(player) && (player->is_active))
        {
            struct Dungeon* dungeon = get_players_dungeon(player);
            if (dungeon->total_score > max_manage_score)
                max_manage_score = dungeon->manage_score;
        }
    }
    // Update the values
    for (plyr_idx = 0; plyr_idx < PLAYERS_COUNT; plyr_idx++)
    {
        struct PlayerInfo* player = get_player(plyr_idx);
        if (!player_invalid(player))
        {
            struct Dungeon* dungeon = get_players_dungeon(player);
            if (dungeon->manage_score > 0)
                dungeon->turns_between_entrance_generation = max_manage_score * player->generate_speed / dungeon->manage_score;
            else
                dungeon->turns_between_entrance_generation = player->generate_speed;
        }
    }
    return 1;
}

void calculate_dungeon_area_scores(void)
{
    // Zero dungeon areas
    for (PlayerNumber plyr_idx = 0; plyr_idx < PLAYERS_COUNT; plyr_idx++)
    {
        struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
        if (!dungeon_invalid(dungeon))
        {
            dungeon->total_area = 0;
            dungeon->room_manage_area = 0;
        }
    }
    // Compute new values for dungeon areas
    for (MapSlabCoord slb_y = 0; slb_y < kfx_sim_state.map_tiles_y; slb_y++)
    {
        for (MapSlabCoord slb_x = 0; slb_x < kfx_sim_state.map_tiles_x; slb_x++)
        {
            SlabCodedCoords slb_num = get_slab_number(slb_x, slb_y);
            struct SlabMap* slb = get_slabmap_direct(slb_num);
            const struct SlabConfigStats* slabst = get_slab_stats(slb);
            if (slabst->category == SlbAtCtg_RoomInterior)
            {
                struct Dungeon *dungeon;
                if (slabmap_owner(slb) != kfx_config_state.neutral_player_num) {
                    dungeon = get_players_num_dungeon(slabmap_owner(slb));
                } else {
                    dungeon = INVALID_DUNGEON;
                }
                if (!dungeon_invalid(dungeon))
                {
                    dungeon->total_area++;
                    dungeon->room_manage_area++;
                }
            } else
            if (slabst->category == SlbAtCtg_FortifiedGround)
            {
                struct Dungeon *dungeon;
                if (slabmap_owner(slb) != kfx_config_state.neutral_player_num) {
                    dungeon = get_players_num_dungeon(slabmap_owner(slb));
                } else {
                    dungeon = INVALID_DUNGEON;
                }
                if (!dungeon_invalid(dungeon))
                {
                    dungeon->total_area++;
                }
            }
        }
    }
}

TbBool map_position_has_sibling_slab(MapSlabCoord slb_x, MapSlabCoord slb_y, SlabKind slbkind, PlayerNumber plyr_idx)
{
    for (int64_t n = 0; n < SMALL_AROUND_LENGTH; n++)
    {
        int64_t dx = small_around[n].delta_x;
        int64_t dy = small_around[n].delta_y;
        struct SlabMap* slb = get_slabmap_block(slb_x + dx, slb_y + dy);
        if ((slb->kind == slbkind) && (slabmap_owner(slb) == plyr_idx)) {
            return true;
        }
    }
    return false;
}

TbBool map_position_initially_explored_for_player(PlayerNumber plyr_idx, MapSlabCoord slb_x, MapSlabCoord slb_y)
{
    struct SlabMap* slb = get_slabmap_block(slb_x, slb_y);
    struct Map* mapblk = get_map_block_at(slab_subtile_center(slb_x), slab_subtile_center(slb_y));
    // All owned ground is visible
    if (slabmap_owner(slb) == plyr_idx) {
        return true;
    }
    // All Rocks are visible
    if (slb->kind == SlbT_ROCK) {
        return true;
    }
    // Neutral entrances are visible
    struct Room* room = room_get(slb->room_index);
    if (((mapblk->flags & SlbAtFlg_IsRoom) != 0) && (room->kind == RoK_ENTRANCE) && (slabmap_owner(slb) == kfx_config_state.neutral_player_num)) {
        return true;
    }
    // Slabs with specific flag are visible
    if ((mapblk->flags & SlbAtFlg_Valuable) != 0) {
        return true;
    }
    // Area around entrances is visible
    if (map_position_has_sibling_slab(slb_x, slb_y, SlbT_ENTRANCE, kfx_config_state.neutral_player_num)) {
        return true;
    }
    return false;
}

void fill_in_explored_area(PlayerNumber plyr_idx, MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{

    int64_t block_flags;
    int64_t direction_flags;
    char *fs_par_slab;
    char west_slab_state;
    char east_slab_state;
    char north_slab_state;
    char south_slab_state;
    const char *i;
    char *scratch_slab_ptr;
    MapSlabCoord slb_y;
    MapSlabCoord slb_x;
    uint64_t queue_write_index;
    uint64_t queue_read_index;

    static const char exploration_direction_lookup_table[80] =
    {
    0,0,0,0,0,
    0,0,0,0,0,
    0,0,0,0,0,
    1,0,0,0,-4,
    0,0,0,0,0,
    0,0,0,0,0,
    2,0,0,0,-7,
    1,0,0,0,-2,
    0,0,0,0,0,
    4,0,0,0,-10,
    0,0,0,0,0,
    1,0,0,0,-3,
    3,0,0,0,-13,
    3,0,0,0,-5,
    2,0,0,0,-3,
    1,0,0,0,0
    };

    struct XY {
        MapSlabCoord x;
        MapSlabCoord y;
    };

    static const struct XY exploration_direction_offsets[6] =
    {
        { 0, 0},
        { 1,-1},
        {-1,-1},
        {-1, 1},
        { 1, 1},
        { 0, 0}
    };

    char *first_scratch = (char*) &big_scratch[kfx_sim_state.map_tiles_x];
    struct XY *second_scratch = (struct XY *)big_scratch + kfx_sim_state.map_tiles_x * (kfx_sim_state.map_tiles_y + 1);
    memset((void *)&big_scratch[kfx_sim_state.map_tiles_x], 0, kfx_sim_state.map_tiles_x * kfx_sim_state.map_tiles_y);

    for(MapSlabCoord slb_y_2 = 0;slb_y_2 < kfx_sim_state.map_tiles_y;slb_y_2++)
    {
        for(MapSlabCoord slb_x_2 = 0;slb_x_2 < kfx_sim_state.map_tiles_x;slb_x_2++)
        {
            struct SlabMap *slb = get_slabmap_block(slb_x_2,slb_y_2);
            struct SlabConfigStats *slabst = get_slab_stats(slb);
            block_flags = slabst->block_flags;

            if ((block_flags & (SlbAtFlg_Filled|SlbAtFlg_Digable|SlbAtFlg_Valuable)) != 0 || ((block_flags & SlbAtFlg_IsDoor) != 0 && slabmap_owner(slb) != plyr_idx))
            {
                first_scratch[get_slab_number(slb_x_2,slb_y_2)] = 1;
            }
        }
    }

    for(MapSubtlCoord lpstl_y = 0;lpstl_y < kfx_sim_state.map_subtiles_y;lpstl_y++)
    {
        for(MapSubtlCoord lpstl_x = 0;lpstl_x < kfx_sim_state.map_subtiles_x;lpstl_x++)
        {
            struct Map *mapblk = get_map_block_at(lpstl_x,lpstl_y);
            conceal_map_block(mapblk, plyr_idx);
        }
    }

    queue_read_index = 0;
    queue_write_index = 0;
    slb_x = stl_x / 3;
    slb_y = stl_y / 3;
    first_scratch[get_slab_number(slb_x,slb_y)] |= 2u;
    do
    {
        direction_flags = 0;
        fs_par_slab = &first_scratch[get_slab_number(slb_x,slb_y)];
        west_slab_state = *(fs_par_slab - 1);
        if ((west_slab_state & 1) != 0)
        {
            direction_flags = 8;
            *(fs_par_slab - 1) = west_slab_state | 2;
        }
        else if ((west_slab_state & 2) == 0)
        {
            *(fs_par_slab - 1) = west_slab_state | 2;

            second_scratch[queue_write_index].x = slb_x - 1;
            second_scratch[queue_write_index].y = slb_y;
            queue_write_index++;
        }
        east_slab_state = fs_par_slab[1];
        if ((east_slab_state & 1) != 0)
        {
            direction_flags |= 2u;
            fs_par_slab[1] = east_slab_state | 2;
        }
        else if ((east_slab_state & 2) == 0)
        {
            fs_par_slab[1] = east_slab_state | 2;
            second_scratch[queue_write_index].x = slb_x + 1;
            second_scratch[queue_write_index].y = slb_y;
            queue_write_index++;
        }
        north_slab_state = *(fs_par_slab - kfx_sim_state.map_tiles_x);
        if ((north_slab_state & 1) != 0)
        {
            direction_flags |= 1u;
            *(fs_par_slab - kfx_sim_state.map_tiles_x) = north_slab_state | 2;
        }
        else if ((north_slab_state & 2) == 0)
        {
            *(fs_par_slab - kfx_sim_state.map_tiles_x) = north_slab_state | 2;
            second_scratch[queue_write_index].x = slb_x;
            second_scratch[queue_write_index].y = slb_y - 1;
            queue_write_index++;
        }
        south_slab_state = fs_par_slab[kfx_sim_state.map_tiles_x];
        if ((south_slab_state & 1) != 0)
        {
            direction_flags |= 4u;
            fs_par_slab[kfx_sim_state.map_tiles_x] = south_slab_state | 2;
        }
        else if ((south_slab_state & 2) == 0)
        {
            fs_par_slab[kfx_sim_state.map_tiles_x] = south_slab_state | 2;
            second_scratch[queue_write_index].x = slb_x;
            second_scratch[queue_write_index].y = slb_y + 1;
            queue_write_index++;
        }
        for (i = &exploration_direction_lookup_table[5 * direction_flags]; *i; i = &exploration_direction_lookup_table[5 * direction_flags])
        {
            if (direction_flags == 15)
            {
                direction_flags = 0;
                *(fs_par_slab - kfx_sim_state.map_tiles_x - 1) |= 2u;
                fs_par_slab[kfx_sim_state.map_tiles_x + 1] |= 2u;
                fs_par_slab[kfx_sim_state.map_tiles_x -1] |= 2u;
                *(fs_par_slab - kfx_sim_state.map_tiles_x + 1) |= 2u;
            }
            else
            {
                scratch_slab_ptr = &first_scratch[get_slab_number(exploration_direction_offsets[(int)*i].x,exploration_direction_offsets[(int)*i].y) + kfx_sim_state.map_tiles_x * slb_y];
                scratch_slab_ptr[slb_x] |= 2u;
                direction_flags &= i[4];
            }
        }
        slb_x = second_scratch[queue_read_index].x;
        slb_y = second_scratch[queue_read_index].y;
        queue_read_index++;
    } while (queue_write_index >= queue_read_index);


    for (slb_y = 0; slb_y < kfx_sim_state.map_tiles_y; ++slb_y)
    {
        for (slb_x = 0; slb_x < kfx_sim_state.map_tiles_x; ++slb_x)
        {
            if ((first_scratch[get_slab_number(slb_x,slb_y) ] & 2) != 0)
            {
                clear_slab_dig(slb_x, slb_y, plyr_idx);
                set_slab_explored(plyr_idx, slb_x, slb_y);
            }
        }
    }
    ui_panel_map_update(0, 0, 256, 256);

}

void init_keeper_map_exploration_by_terrain(struct PlayerInfo *player)
{
    struct Thing* heartng = get_player_soul_container(player->id_number);
    if (thing_exists(heartng)) {
        fill_in_explored_area(player->id_number, heartng->mappos.x.stl.num, heartng->mappos.y.stl.num);
    }
    for (MapSlabCoord slb_y = 0; slb_y < kfx_sim_state.map_tiles_y; slb_y++)
    {
        for (MapSlabCoord slb_x = 0; slb_x < kfx_sim_state.map_tiles_x; slb_x++)
        {
            if (map_position_initially_explored_for_player(player->id_number, slb_x, slb_y)) {
                set_slab_explored(player->id_number, slb_x, slb_y);
            }
        }
    }
}

TbBool check_map_explored_at_current_pos(struct Thing *creatng)
{
    check_map_explored(creatng, creatng->mappos.x.stl.num, creatng->mappos.y.stl.num);
    return true;
}

void init_keeper_map_exploration_by_creatures(struct PlayerInfo *player)
{
    do_to_players_all_creatures_of_model(player->id_number, CREATURE_ANY, check_map_explored_at_current_pos);
}

void turn_user_cursor_light(NetUserId user, TbBool turn_on)
{
    const int64_t idx = get_user_state(user)->cursor_light_idx;
    if (idx == 0)
        return;
    if (turn_on)
        light_turn_light_on(idx);
    else
        light_turn_light_off(idx);
}

void init_user_state(NetUserId user)
{
    struct UserState* ustate = get_user_state(user);
    if (user_state_invalid(ustate))
    {
        ERRORLOG("Cannot init state of user %" PRId64, (int64_t)user);
        return;
    }
    memset(ustate, 0, sizeof(*ustate));
    ustate->teleport_destination = 19;
    ustate->battleid = 1;
    struct InitLight ilght;
    memset(&ilght, 0, sizeof(struct InitLight));
    ilght.radius = 2560;
    ilght.intensity = 48;
    ilght.flags = 5;
    ilght.is_dynamic = 1;
    ilght.colour_r = kfx_config_state.conf.rules[0].gameplay.cursor_light_r;
    ilght.colour_g = kfx_config_state.conf.rules[0].gameplay.cursor_light_g;
    ilght.colour_b = kfx_config_state.conf.rules[0].gameplay.cursor_light_b;
    int64_t idx = light_create_light(&ilght);
    ustate->cursor_light_idx = idx;
    if (idx != 0) {
        light_set_light_never_cache(idx);
    } else {
        WARNLOG("Cannot allocate cursor light to user %" PRId64 ".",(int64_t)user);
    }
}

void init_player(struct PlayerInfo *player, int64_t no_explore, const struct PacketSaveHead *replay_head)
{
    SYNCDBG(5,"Starting");
    if (is_my_player(player))
    {
        ui_local_view_transition(player, LVTr_LevelStart);
    }
    player->continue_work_state = PSt_CtrlDungeon;
    player->work_state = PSt_CtrlDungeon;
    player->isometric_view_zoom_level = settings.isometric_view_zoom_level;
    player->frontview_zoom_level = settings.frontview_zoom_level;
    if (is_my_player(player))
    {
        // Read keeperfx_ui_config directly rather than via a kfx_sim_state
        // copy -- clear_complete_game() memsets the whole of kfx_sim_state
        // once at startup, right after setup_game() would have populated
        // such a copy, permanently zeroing it for the rest of the process.
        // keeperfx_ui_config is kfx_config's own struct and isn't touched
        // by that reset.
        if (keeperfx_ui_config.default_tag_mode != 3)
        {
            settings.highlight_mode = keeperfx_ui_config.default_tag_mode - 1;
        }
        player->roomspace_highlight_mode = settings.highlight_mode;
        player->roomspace_mode = settings.highlight_mode;
        set_flag(kfx_sim_state.operation_flags, GOF_ShowPanel);
        ui_set_gui_visible(true);
        ui_init_gui();
        ui_turn_on_menu(GMnu_MAIN);
        ui_turn_on_menu(GMnu_ROOM);
    }
    player->roomspace_width = 1;
    player->roomspace_height = 1;
    player->roomspace_detection_looseness = DEFAULT_USER_ROOMSPACE_DETECTION_LOOSENESS;
    switch (kfx_sim_state.game_kind)
    {
    case GKind_LocalGame:
        init_player_start(player, false);
        reset_player_mode(player, PVT_DungeonTop);
        if ( !no_explore ) {
          init_keeper_map_exploration_by_terrain(player);
          init_keeper_map_exploration_by_creatures(player);
        }
        break;
    case GKind_MultiGame:
        if (replay_head->isometric_view_zoom_level == 0)
        {
            player->isometric_view_zoom_level = CAMERA_ZOOM_MAX;
        }
        if (replay_head->frontview_zoom_level == 0)
        {
            player->frontview_zoom_level = FRONTVIEW_CAMERA_ZOOM_MAX;
        }
        if (player->is_active != 1)
        {
          ERRORLOG("Non Keeper in Keeper game");
          break;
        }
        init_player_start(player, false);
        reset_player_mode(player, PVT_DungeonTop);
        init_keeper_map_exploration_by_terrain(player);
        init_keeper_map_exploration_by_creatures(player);
        break;
    default:
        ERRORLOG("How do I set up this player?");
        break;
    }
    init_player_cameras(player);
    player->mp_message_text[0] = '\0';
    // By default, player is his own ally
    player->allied_players = to_flag(player->id_number);
    player->hand_busy_until_turn = 0;
    if (player->generate_speed == 0)
      player->generate_speed = kfx_config_state.conf.rules[player->id_number].rooms.default_generate_speed;
    if (is_my_player(player)) {
        // new game, play one of the default tracks
        LevelNumber lvnum = get_loaded_level_number();
        int64_t safe_lvnum = (lvnum > 0) ? lvnum : 1; // guard against (lvnum - 1) % 4 going negative
        play_music_track(3 + (int64_t)((safe_lvnum - 1) % 4)); // tracks 3..6
    }
}

void init_players(const struct PacketSaveHead *replay_head)
{
    for (int64_t i = 0; i < PLAYERS_COUNT; i++)
    {
        struct PlayerInfo* player = get_player(i);
        if (flag_is_set(replay_head->players_exist, to_flag(i)))
            player->allocflags |= PlaF_Allocated;
        else
            player->allocflags &= ~PlaF_Allocated;
        if (player_exists(player))
        {
            player->id_number = i;
            if (flag_is_set(replay_head->players_comp, to_flag(i)))
                player->allocflags |= PlaF_CompCtrl;
            else
                player->allocflags &= ~PlaF_CompCtrl;
            if ((player->allocflags & PlaF_CompCtrl) == 0)
            {
              player->allocflags |= PlaF_OriginallyHuman;
              kfx_sim_state.human_players_count++;
              player->is_active = 1;
              kfx_sim_state.game_kind = GKind_MultiGame;
              init_player(player, 0, replay_head);
            }
        }
    }
}

TbBool wp_check_map_pos_valid(struct Wander *wandr, SubtlCodedCoords stl_num)
{
    SYNCDBG(16,"Starting");
    struct Thing* heartng;
    struct Map* mapblk;
    struct Coord3d dstpos;
    struct SlabMap* slb;
    MapSubtlCoord stl_x = stl_num_decode_x(stl_num);
    MapSubtlCoord stl_y = stl_num_decode_y(stl_num);
    if (wandr->wandr_slot == CrWaS_WithinDungeon)
    {
        mapblk = get_map_block_at_pos(stl_num);
        // Add only tiles which are revealed to the wandering player, unless it's heroes - for them, add all
        if ((player_is_roaming(wandr->plyr_idx)) || map_block_revealed(mapblk, wandr->plyr_idx))
        {
            slb = get_slabmap_for_subtile(stl_x, stl_y);
            if (((mapblk->flags & SlbAtFlg_Blocking) == 0) && (!subtile_is_unsafe(stl_x, stl_y))
             && players_creatures_tolerate_each_other(wandr->plyr_idx,slabmap_owner(slb)))
            {
                heartng = get_player_soul_container(wandr->plyr_idx);
                if (thing_exists(heartng))
                {

                    dstpos.x.val = subtile_coord_center(stl_x);
                    dstpos.y.val = subtile_coord_center(stl_y);
                    dstpos.z.val = subtile_coord(1, 0);
                    if (navigation_points_connected(&heartng->mappos, &dstpos))
                    {
                        return true;
                    }
                }
            }
        }
    } else
    {
        mapblk = get_map_block_at_pos(stl_num);
        // Add only tiles which are not revealed to the wandering player, unless it's heroes - for them, do nothing
        if (!player_is_roaming(wandr->plyr_idx) && !map_block_revealed(mapblk, wandr->plyr_idx))
        {
            if (((mapblk->flags & SlbAtFlg_Blocking) == 0) && (!subtile_is_unsafe(stl_x, stl_y)))
            {
                heartng = get_player_soul_container(wandr->plyr_idx);
                if (thing_exists(heartng))
                {
                    dstpos.x.val = subtile_coord_center(stl_x);
                    dstpos.y.val = subtile_coord_center(stl_y);
                    dstpos.z.val = subtile_coord(1,0);
                    if (navigation_points_connected(&heartng->mappos, &dstpos))
                    {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

TbBool wander_point_add(struct Wander *wandr, SubtlCodedCoords stl_num)
{
    uint64_t i = wandr->point_insert_idx;
    wandr->points[i].stl_x = stl_num_decode_x(stl_num);
    wandr->points[i].stl_y = stl_num_decode_y(stl_num);
    wandr->point_insert_idx = (i + 1) % WANDER_POINTS_COUNT;
    if (wandr->points_count < WANDER_POINTS_COUNT)
      wandr->points_count++;
    return true;
}

/**
 * Stores up to given amount of wander points into given wander structure.
 * If required, selects several evenly distributed points from the input array.
 * @param wandr
 * @param stl_num_list
 * @param stl_num_count
 * @param max_to_store
 * @return
 */
TbBool store_wander_points_up_to(struct Wander *wandr, const SubtlCodedCoords stl_num_list[], int64_t stl_num_count, int64_t max_to_store)
{
    int64_t i;
    if (stl_num_count > max_to_store)
    {
        if (wandr->max_found_per_check <= 0)
            return 1;
        wandr->point_insert_idx %= WANDER_POINTS_COUNT;
        double delta = ((double)stl_num_count) / max_to_store;
        double realidx = 0.1; // A little above zero to avoid float rounding errors
        for (i = 0; i < max_to_store; i++)
        {
            wander_point_add(wandr, stl_num_list[(uint64_t)(realidx)]);
            realidx += delta;
        }
    } else
    {
        // Otherwise, add all points to the wander array
        for (i = 0; i < stl_num_count; i++)
        {
            wander_point_add(wandr, stl_num_list[i]);
        }
    }
    return true;
}

int64_t wander_point_initialise(struct Wander *wandr, PlayerNumber plyr_idx, unsigned char wandr_slot)
{
    wandr->wandr_slot = wandr_slot;
    wandr->plyr_idx = plyr_idx;
    wandr->point_insert_idx = 0;
    wandr->last_checked_slb_num = 0;
    wandr->plyr_bit = to_flag(plyr_idx);
    wandr->num_check_per_run = 20;
    wandr->max_found_per_check = 4;
    wandr->search_limiting_enabled = 0;

    int64_t stl_num_list_count = 0;
    SubtlCodedCoords* stl_num_list = (SubtlCodedCoords*)big_scratch;
    SlabCodedCoords slb_num = 0;
    while (1)
    {
        MapSlabCoord slb_x = slb_num_decode_x(slb_num);
        MapSlabCoord slb_y = slb_num_decode_y(slb_num);
        SubtlCodedCoords stl_num = get_subtile_number_at_slab_center(slb_x, slb_y);
        if (wp_check_map_pos_valid(wandr, stl_num))
        {
            if (stl_num_list_count >= 0x10000/sizeof(SubtlCodedCoords)-1)
                break;
            stl_num_list[stl_num_list_count] = stl_num;
            stl_num_list_count++;
        }
        slb_num++;
        if (slb_num >= kfx_sim_state.map_tiles_x*kfx_sim_state.map_tiles_y) {
            break;
        }
    }
    // Check if we have found anything
    if (stl_num_list_count <= 0)
        return 1;
    // If we have too many points, use only some of them
    store_wander_points_up_to(wandr, stl_num_list, stl_num_list_count, WANDER_POINTS_COUNT);
    return 1;
}

#define LOCAL_LIST_SIZE 20
int64_t wander_point_update(struct Wander *wandr)
{
    SubtlCodedCoords stl_num_list[LOCAL_LIST_SIZE];
    SYNCDBG(6,"Starting");
    // Find up to 20 numbers (starting where we ended last time) and store them in local array
    SlabCodedCoords slb_num = wandr->last_checked_slb_num;
    int64_t stl_num_list_count = 0;
    for (int64_t i = 0; i < wandr->num_check_per_run; i++)
    {
        MapSlabCoord slb_x = slb_num_decode_x(slb_num);
        MapSlabCoord slb_y = slb_num_decode_y(slb_num);
        SubtlCodedCoords stl_num = get_subtile_number_at_slab_center(slb_x, slb_y);
        if (wp_check_map_pos_valid(wandr, stl_num))
        {
            if (stl_num_list_count >= LOCAL_LIST_SIZE)
                break;
            stl_num_list[stl_num_list_count] = stl_num;
            stl_num_list_count++;
            if ((wandr->search_limiting_enabled != 0) && (stl_num_list_count == wandr->max_found_per_check))
            {
                slb_num = (wandr->num_check_per_run + wandr->last_checked_slb_num) % (kfx_sim_state.map_tiles_x*kfx_sim_state.map_tiles_y);
                break;
            }
        }
        slb_num++;
        if (slb_num >= kfx_sim_state.map_tiles_x*kfx_sim_state.map_tiles_y) {
            slb_num = 0;
        }
    }
    wandr->last_checked_slb_num = slb_num;
    // Check if we have found anything
    if (stl_num_list_count <= 0)
        return 1;
    // If we have too many points, use only some of them
    store_wander_points_up_to(wandr, stl_num_list, stl_num_list_count, wandr->max_found_per_check);
    return 1;
}
#undef LOCAL_LIST_SIZE

void post_init_player(struct PlayerInfo *player)
{
    switch (kfx_sim_state.game_kind)
    {
    case GKind_LimitedState:
        break;
    case GKind_LocalGame:
    case GKind_MultiGame:
        wander_point_initialise(&player->wandr_within, player->id_number, CrWaS_WithinDungeon);
        wander_point_initialise(&player->wandr_outside, player->id_number, CrWaS_OutsideDungeon);
        break;
    default:
        if ((player->allocflags & PlaF_CompCtrl) == 0) {
            ERRORLOG("Invalid GameMode");
        }
        break;
    }
    ui_panel_map_update(0, 0, kfx_sim_state.map_subtiles_x+1, kfx_sim_state.map_subtiles_y+1);
}

void post_init_players(void)
{
    SYNCDBG(8, "Starting");
    for (PlayerNumber plyr_idx = 0; plyr_idx < PLAYERS_COUNT; plyr_idx++)
    {
        struct PlayerInfo* player = get_player(plyr_idx);
        if ((player->allocflags & PlaF_Allocated) != 0) {
            post_init_player(player);
        }
    }
}

void init_players_local_game(const struct PacketSaveHead *replay_head)
{
    SYNCDBG(4,"Starting");
    struct PlayerInfo* player = get_my_player();
    player->id_number = my_player_number;
    player->user_id = SOLO_HUMAN_ID;
    player->allocflags |= PlaF_Allocated | PlaF_OriginallyHuman;

    if( player->id_number == PLAYER_GOOD)
    {
        player->allocflags &= ~PlaF_CompCtrl;
        player->player_type = PT_Keeper;
    }

    switch (settings.video_rotate_mode) {
        case 0: player->view_mode_restore = PVM_IsoWibbleView; break;
        case 1: player->view_mode_restore = PVM_IsoStraightView; break;
        case 2: player->view_mode_restore = PVM_FrontView; break;
        default: player->view_mode_restore = PVM_IsoWibbleView; break;
    }
    init_player(player, 0, replay_head);
    init_user_state(player->user_id);
    set_creature_tendencies(player, CrTend_Imprison, IMPRISON_BUTTON_DEFAULT);
    set_creature_tendencies(player, CrTend_Flee, FLEE_BUTTON_DEFAULT);
    kfx_sim_state.creatures_tend_imprison = IMPRISON_BUTTON_DEFAULT;
    kfx_sim_state.creatures_tend_flee = FLEE_BUTTON_DEFAULT;
}

void process_player_states(void)
{
    SYNCDBG(6,"Starting");
    for (PlayerNumber plyr_idx = 0; plyr_idx < PLAYERS_COUNT; plyr_idx++)
    {
        struct PlayerInfo* player = get_player(plyr_idx);
        if (player_exists(player) && ((player->allocflags & PlaF_CompCtrl) == 0))
        {
            if ( (player->work_state == PSt_CreatrInfo) || (player->work_state == PSt_CreatrInfoAll) )
            {
                struct Thing* thing = thing_get(player->controlled_thing_idx);
                struct Camera* cam = get_player_active_camera(player);
                if ((cam != NULL) && thing_exists(thing)) {
                    cam->mappos.x.val = thing->mappos.x.val;
                    cam->mappos.y.val = thing->mappos.y.val;
                    signal_local_camera_retarget(player);
                }
            }
        }
    }
}

void process_players(void)
{
    SYNCDBG(5,"Starting");
    update_roomspaces();
    process_player_instances();
    process_player_states();
    for (int64_t i = 0; i < PLAYERS_COUNT; i++)
    {
        struct PlayerInfo* player = get_player(i);
        if (player_exists(player) && (player->is_active == 1))
        {
            SYNCDBG(6,"Doing updates for player %" PRId64,(int64_t)(i));
            wander_point_update(&player->wandr_within);
            wander_point_update(&player->wandr_outside);
            update_power_sight_explored(player);
            ui_update_player_objectives(i);
        }
    }
    SYNCDBG(17,"Finished");
}

TbBool player_sell_trap_at_subtile(PlayerNumber plyr_idx, MapSubtlCoord stl_x, MapSubtlCoord stl_y, TbBool whole_slab)
{
    struct Thing *thing;
    struct Coord3d pos;
    MapSlabCoord slb_x = subtile_slab(stl_x);
    MapSlabCoord slb_y = subtile_slab(stl_y);
    int64_t sell_value = 0;
    uint64_t traps_sold;
    if (!whole_slab)
    {
        thing = get_trap_for_position(stl_x, stl_y);
        if (!thing_is_sellable_trap(thing))
        {
            return false;
        }
        set_coords_to_subtile_center(&pos,stl_x,stl_y,1);
        traps_sold = remove_trap_on_subtile(stl_x, stl_y, &sell_value);
    }
    else
    {
        thing = get_trap_for_slab_position(subtile_slab(stl_x), subtile_slab(stl_y));
        if (!thing_is_sellable_trap(thing))
        {
            return false;
        }
        set_coords_to_slab_center(&pos,slb_x,slb_y);
        traps_sold = remove_traps_around_subtile(slab_subtile_center(slb_x), slab_subtile_center(slb_y), &sell_value);
    }

    struct Dungeon* dungeon = get_dungeon(thing->owner);
    dungeon->traps_sold += traps_sold;
    dungeon->manufacture_gold += sell_value;

    if (is_my_player_number(plyr_idx))
    {
        play_non_3d_sample(snd_tile_sell);
    }
    kfx_sim_view_signals.camera_deviate_jump[dungeon->owner] = 192;
    if (sell_value != 0)
    {
        create_price_effect(&pos, plyr_idx, sell_value);
        player_add_offmap_gold(plyr_idx,sell_value);
    } else
    {
        WARNLOG("Sold traps at (%" PRId64 ",%" PRId64 ") which didn't cost anything",(int64_t)stl_x,(int64_t)stl_y);
    }
    // Add the trap location to related computer player, in case we'll want to place a trap again
    struct Computer2* comp = get_computer_player(plyr_idx);
    if (!computer_player_invalid(comp))
    {
        add_to_trap_locations(comp, &pos);
    }
    return true;
}

TbBool player_sell_door_at_subtile(PlayerNumber plyr_idx, MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{
    MapSubtlCoord cstl_x = stl_slab_center_subtile(stl_x);
    MapSubtlCoord cstl_y = stl_slab_center_subtile(stl_y);
    struct Thing* thing = get_door_for_position(cstl_x, cstl_y);
    if (!thing_is_sellable_door(thing))
    {
        return false;
    }
    struct DoorConfigStats *doorst = get_door_model_stats(thing->model);
    struct Dungeon* dungeon = get_players_num_dungeon(thing->owner);
    kfx_sim_view_signals.camera_deviate_jump[dungeon->owner] = 192;
    GoldAmount sell_value = compute_value_percentage(doorst->selling_value, kfx_config_state.conf.rules[plyr_idx].gameplay.door_sale_percent);
    dungeon->doors_sold++;
    dungeon->manufacture_gold += sell_value;
    destroy_door(thing);
    if (is_my_player_number(plyr_idx))
    {
        play_non_3d_sample(snd_tile_sell);
    }
    struct Coord3d pos;
    set_coords_to_slab_center(&pos,subtile_slab(stl_x),subtile_slab(stl_y));
    if (sell_value != 0)
    {
        create_price_effect(&pos, plyr_idx, sell_value);
        player_add_offmap_gold(plyr_idx, sell_value);
    }
    { // Add the trap location to related computer player, in case we'll want to place a trap again.
        struct Computer2* comp = get_computer_player(plyr_idx);
        if (!computer_player_invalid(comp))
        {
            add_to_trap_locations(comp, &pos);
        }
    }
    return true;
}

// Moved from kfx_net's packets.c (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- pure room-selling domain logic,
// no network-specific concerns, alongside player_sell_trap_at_subtile/
// player_sell_door_at_subtile above; kfx_net's packets.c/packets_input.c
// call it via a normal downward include.
TbBool player_sell_room_at_subtile(int64_t plyr_idx, int64_t stl_x, int64_t stl_y)
{
    struct Room* room = subtile_room_get(stl_x, stl_y);
    if (room_is_invalid(room))
    {
        ERRORLOG("No room to delete at subtile (%" PRId64 ",%" PRId64 ")",(int64_t)stl_x,(int64_t)stl_y);
        return false;
    }
    struct RoomConfigStats* roomst = get_room_kind_stats(room->kind);
    int64_t revenue = compute_value_percentage(roomst->cost, kfx_config_state.conf.rules[plyr_idx].gameplay.room_sale_percent);
    if (room->owner != kfx_config_state.neutral_player_num)
    {
        struct Dungeon* dungeon = get_players_num_dungeon(room->owner);
        dungeon->rooms_destroyed++;
        kfx_sim_view_signals.camera_deviate_jump[dungeon->owner] = 192;
    }
    delete_room_slab(subtile_slab(stl_x), subtile_slab(stl_y), 0);
    if (is_my_player_number(plyr_idx))
        play_non_3d_sample(snd_tile_sell);
    if (revenue != 0)
    {
        struct Coord3d pos;
        set_coords_to_slab_center(&pos, subtile_slab(stl_x), subtile_slab(stl_y));
        create_price_effect(&pos, plyr_idx, revenue);
        player_add_offmap_gold(plyr_idx, revenue);
    }
    return true;
}

void compute_and_update_player_payday_total(PlayerNumber plyr_idx)
{
    SYNCDBG(15,"Starting for player %" PRId64,(int64_t)plyr_idx);
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    dungeon->creatures_total_pay = compute_player_payday_total(dungeon);
}

void compute_and_update_player_backpay_total(PlayerNumber plyr_idx)
{
    SYNCDBG(15, "Starting for player %" PRId64, (int64_t)plyr_idx);
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    dungeon->creatures_total_backpay = compute_player_payday_total(dungeon);
}

void set_player_colour(PlayerNumber plyr_idx, unsigned char colour_idx)
{
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    if (!dungeon_invalid(dungeon))
    {
        if (dungeon->color_idx != colour_idx)
        {
            dungeon->color_idx = colour_idx;
            ui_update_panel_color_player_color(plyr_idx,colour_idx);
            for (MapSlabCoord slb_y=0; slb_y < kfx_sim_state.map_tiles_y; slb_y++)
            {
                for (MapSlabCoord slb_x=0; slb_x < kfx_sim_state.map_tiles_x; slb_x++)
                {
                    struct SlabMap* slb = get_slabmap_block(slb_x,slb_y);
                    if (slabmap_owner(slb) == plyr_idx)
                    {
                        redraw_slab_map_elements(slb_x,slb_y);
                    }

                }
            }
            const struct StructureList *slist = get_list_for_thing_class(TCls_Object);
            int64_t k = 0;
            uint64_t i = slist->index;
            while (i > 0)
            {
                struct Thing *thing = thing_get(i);
                TRACE_THING(thing);
                if (thing_is_invalid(thing)) {
                    ERRORLOG("Jump to invalid thing detected");
                    break;
                }
                i = thing->next_of_class;
                // Per-thing code
                if (thing->owner == plyr_idx)
                {
                    ThingModel base_model = get_coloured_object_base_model(thing->model);
                    if(base_model != 0)
                    {
                        create_coloured_object(&thing->mappos, plyr_idx, thing->parent_idx,base_model);
                        delete_thing_structure(thing, 0);
                    }
                }
                // Per-thing code ends
                k++;
                if (k > slist->count)
                {
                    ERRORLOG("Infinite loop detected when sweeping things list");
                    break;
                }
            }
            // Refresh GUI panel button sprites for local player. Workaround for multiplayer.
            if (plyr_idx == my_player_number) {
                ui_refresh_active_button_sprites_for_player(my_player_number);
            }
        }
    }
}

void check_players_won(void)
{
  SYNCDBG(8,"Starting");

    if (!network_is_active())
        return;

    if (!victory_candidates_fully_allied(false))
        return;

    for (PlayerNumber playerIdx = 0; playerIdx < PLAYERS_COUNT; ++playerIdx)
    {
        struct PlayerInfo* curPlayer = get_player(playerIdx);
        if (player_is_victory_candidate(curPlayer) && (curPlayer->victory_state == VicS_Undecided))
            set_player_as_won_level(curPlayer);
    }
}

void check_players_lost(void)
{
  int64_t i;
  SYNCDBG(8,"Starting");
  struct PlayerInfo* player;
  struct Dungeon* dungeon;
  for (i=0; i < PLAYERS_COUNT; i++)
  {
      player = get_player(i);
      dungeon = get_players_dungeon(player);
      if (player_exists(player) && (player->is_active == 1))
      {
          struct Thing *heartng;
          heartng = get_player_soul_container(i);
          if (heartng->owner != i)
          {
              init_player_start(player, true);
              if (dungeon->dnheart_idx == 0)
              {
                  game_initialise_devastate_dungeon_from_heart(player->id_number);
              }
          }
          if ((!thing_exists(heartng) || ((heartng->active_state == ObSt_BeingDestroyed) && !(dungeon->backup_heart_idx > 0))) && (player->victory_state == VicS_Undecided))
          {
            event_kill_all_players_events(i);
            set_player_as_lost_level(player);
            //this would easily prevent computer player activities on dead player, but it also makes dead player unable to use
            //floating spirit, so it can't be done this way: player->is_active = 0;
            if (is_my_player_number(i)) {
                RendererPaletteSet(engine_palette);
            }
          }
      }
  }
}

void blast_slab(MapSlabCoord slb_x, MapSlabCoord slb_y, PlayerNumber plyr_idx)
{
    struct SlabMap *slb;
    slb = get_slabmap_block(slb_x, slb_y);
    if (slabmap_block_invalid(slb)) {
        return;
    }
    if (slabmap_owner(slb) != plyr_idx) {
        return;
    }
    struct Thing *doortng;
    doortng = get_door_for_position(slab_subtile_center(slb_x), slab_subtile_center(slb_y));
    if (!thing_is_invalid(doortng)) {
        destroy_door(doortng);
    }
    struct SlabConfigStats *slabst;
    slabst = get_slab_stats(slb);
    if (slabst->category == SlbAtCtg_FortifiedGround)
    {
      place_slab_type_on_map(SlbT_PATH, slab_subtile_center(slb_x), slab_subtile_center(slb_y), kfx_config_state.neutral_player_num, 1);
      decrease_dungeon_area(plyr_idx, 1);
      do_unprettying(kfx_config_state.neutral_player_num, slb_x, slb_y);
      do_slab_efficiency_alteration(slb_x, slb_y);
      struct Coord3d pos;
      pos.x.val = subtile_coord_center(slab_subtile_center(slb_x));
      pos.y.val = subtile_coord_center(slab_subtile_center(slb_y));
      pos.z.val = get_floor_height_at(&pos);
      create_effect_element(&pos, TngEffElm_RedFlameBig, plyr_idx);
    }
}

static void process_dungeon_devastation_effects(void)
{
    SYNCDBG(8,"Starting");
    int64_t plyr_idx;
    for (plyr_idx=0; plyr_idx < PLAYERS_COUNT; plyr_idx++)
    {
        struct Dungeon *dungeon;
        dungeon = get_players_num_dungeon(plyr_idx);
        if (dungeon->devastation_turn == 0)
            continue;
        if ((get_gameturn() & 1) != 0)
            continue;
        dungeon->devastation_turn++;
        if (dungeon->devastation_turn >= max(kfx_sim_state.map_tiles_x,kfx_sim_state.map_tiles_y))
            continue;
        MapSlabCoord slb_x;
        MapSlabCoord slb_y;
        int64_t i;
        int64_t range;
        slb_x = subtile_slab(dungeon->devastation_centr_x) - dungeon->devastation_turn;
        slb_y = subtile_slab(dungeon->devastation_centr_y) - dungeon->devastation_turn;
        range = 2*dungeon->devastation_turn;
        for (i = 0; i <= range; i++)
        {
            blast_slab(slb_x + i, slb_y,         dungeon->owner);
            blast_slab(slb_x + i, slb_y + range, dungeon->owner);
        }
        for (i = 0; i <= range; i++)
        {
            blast_slab(slb_x,         slb_y + i, dungeon->owner);
            blast_slab(slb_x + range, slb_y + i, dungeon->owner);
        }
    }
}

/**
 * Increments paydays_owed for all players creatures
 * returns amount of creatures needing payday for player
 */
int64_t set_players_creatures_to_get_paid(PlayerNumber plyr_idx)
{
    uint64_t k;
    int64_t i;
    int64_t count = 0;
    const struct StructureList *slist;
    slist = get_list_for_thing_class(TCls_Creature);
    i = slist->index;
    k = 0;
    while (i != 0)
    {
        struct Thing *thing;
        thing = thing_get(i);
        if (thing_is_invalid(thing))
        {
            ERRORLOG("Jump to invalid thing detected");
            break;
        }
        i = thing->next_of_class;
        // Per-thing code
        if (thing->owner == plyr_idx)
        {
            struct CreatureModelConfig *crconf;
            crconf = creature_stats_get_from_thing(thing);
            if (crconf->pay != 0)
            {
                struct CreatureControl *cctrl;
                cctrl = creature_control_get_from_thing(thing);
                if (cctrl->paydays_advanced > 0)
                {
                    cctrl->paydays_advanced--;
                } else
                {
                    if (!creature_is_kept_in_custody_by_enemy(thing))
                    {
                        cctrl->paydays_owed++;
                        count++;
                    }
                    else
                    {
                        cctrl->paydays_advanced--;
                    }
                }
            }
        }
        // Per-thing code ends
        k++;
        if (k > THINGS_COUNT)
        {
            ERRORLOG("Infinite loop detected when sweeping things list");
            break;
        }
    }
    return count;
}

void process_payday(void)
{
    PlayerNumber plyr_idx;
    for (plyr_idx=0; plyr_idx < PLAYERS_COUNT; plyr_idx++)
    {
        kfx_config_state.pay_day_progress[plyr_idx] = kfx_config_state.pay_day_progress[plyr_idx] + (kfx_config_state.conf.rules[plyr_idx].gameplay.pay_day_speed / 100);
        if (player_is_roaming(plyr_idx) || (plyr_idx == kfx_config_state.neutral_player_num)) {
            continue;
        }
        struct PlayerInfo *player;
        player = get_player(plyr_idx);
        if (player_exists(player) && (player->is_active == 1))
        {
            compute_and_update_player_payday_total(plyr_idx);
            compute_and_update_player_backpay_total(plyr_idx);
        }
    }
    int64_t player_paid_creatures_count;
    for (plyr_idx = 0; plyr_idx < PLAYERS_COUNT; plyr_idx++)
    {
        if (kfx_config_state.conf.rules[plyr_idx].gameplay.pay_day_gap <= kfx_config_state.pay_day_progress[plyr_idx])
        {
            if (is_my_player_number(plyr_idx))
                audio_output_message(SMsg_Payday, 0);
            kfx_config_state.pay_day_progress[plyr_idx] = 0;
            player_paid_creatures_count = set_players_creatures_to_get_paid(plyr_idx);
            if (player_paid_creatures_count > 0)
            {
                struct Dungeon *dungeon = get_players_num_dungeon(plyr_idx);
                event_create_event_or_update_nearby_existing_event(0, 0, EvKind_CreaturePayday, plyr_idx, dungeon->creatures_total_pay);
            }
        }
    }
}

void process_dungeons(void)
{
  SYNCDBG(7,"Starting");
  check_players_won();
  check_players_lost();
  process_dungeon_power_magic();
  process_dungeon_devastation_effects();
  process_entrance_generation();
  process_payday();
  process_things_in_dungeon_hand();
  SYNCDBG(9,"Finished");
}

int64_t clear_active_dungeons_stats(void)
{
  struct Dungeon *dungeon;
  int64_t i;
  for (i=0; i < PLAYERS_COUNT; i++)
  {
      dungeon = get_dungeon(i);
      if (dungeon_invalid(dungeon))
          break;
      memset((char *)dungeon->crmodel_state_type_count, 0, kfx_config_state.conf.crtr_conf.model_count * STATE_TYPES_COUNT * sizeof(int64_t));
      memset((char *)dungeon->guijob_all_creatrs_count, 0, kfx_config_state.conf.crtr_conf.model_count *3*sizeof(int64_t));
      memset((char *)dungeon->guijob_angry_creatrs_count, 0, kfx_config_state.conf.crtr_conf.model_count *3*sizeof(int64_t));
  }
  return i;
}

/** Hands a player's own seat to the built-in AI (full autopilot, not the co-op "computer assistant" that
 *  leaves human input active -- see toggle_computer_player) and reveals the whole map for them, so a human
 *  watching that seat's camera sees everything the AI does instead of only what it has explored. Unlike
 *  script_support_setup_player_as_computer_keeper (lvl_script_commands.c), this never calls init_player_start
 *  or the map-exploration resets: the player already has a live dungeon and those would wipe it. */
TbBool player_enter_spectator_mode(PlayerNumber plyr_idx)
{
    struct PlayerInfo *player = get_player(plyr_idx);
    if (!player_exists(player) || player_is_neutral(plyr_idx))
        return false;
    // Defensive, not reachable via the campaign select screen's own checkbox (fe_spectate_campaign and
    // fe_external_campaign are mutually exclusive there) -- but PlaF_CompCtrl and PlaF_ExternalSeat
    // (net_add_external_seat(), M10) are two different, mutually exclusive bits, and setting CompCtrl on top
    // of an already-claimed External seat here would produce exactly that invalid dual-flag state.
    if (flag_is_set(player->allocflags, PlaF_ExternalSeat))
        return false;
    if (!flag_is_set(player->allocflags, PlaF_CompCtrl))
    {
        if (!ai_setup_a_computer_player(plyr_idx, comp_player_conf.player_assist_default))
            return false;
        set_flag(player->allocflags, PlaF_CompCtrl);
    }
    reveal_whole_map(player);
    return true;
}

/** Hands a spectated seat back to its human: the inverse of player_enter_spectator_mode, but only the CompCtrl
 *  flag -- the revealed map is left alone (there is no "conceal what the AI has seen" to undo, and a human who
 *  just watched the AI play would not expect their vision to shrink back). */
TbBool player_leave_spectator_mode(PlayerNumber plyr_idx)
{
    struct PlayerInfo *player = get_player(plyr_idx);
    if (!player_exists(player) || player_is_neutral(plyr_idx))
        return false;
    clear_flag(player->allocflags, PlaF_CompCtrl);
    return true;
}

/******************************************************************************/
