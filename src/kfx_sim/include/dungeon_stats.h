/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file dungeon_stats.h
 *     Header file for dungeon_stats.c.
 * @par Purpose:
 *     Dungeon stats structures definitions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     10 Nov 2009 - 20 Jan 2012
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_DNGN_STATS_H
#define DK_DNGN_STATS_H

#include "bflib_basics.h"
#include "globals.h"
#include "player_data.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

struct LevelStats {
  uint64_t things_researched;
  uint64_t creatures_attracted;
  uint64_t gold_mined;
  uint64_t manufactured_doors;
  uint64_t manufactured_traps;
  uint64_t manufactured_items;
  uint64_t start_time;
  uint64_t end_time;
  uint64_t creatures_trained;
  uint64_t creatures_tortured;
  uint64_t creatures_sacrificed;
  uint64_t creatures_converted;
  uint64_t creatures_summoned;
  uint64_t num_slaps;
  uint64_t num_caveins;
  uint64_t bridges_built;
  uint64_t rock_dug_out;
  uint64_t salary_cost;
  uint64_t flies_killed_by_spiders;
  uint64_t territory_destroyed;
  uint64_t territory_lost;
  uint64_t rooms_constructed;
  uint64_t traps_used;
  uint64_t traps_armed;
  uint64_t doors_used;
  uint64_t keepers_destroyed;
  uint64_t area_claimed;
  uint64_t backs_stabbed;
  uint64_t chickens_hatched;
  uint64_t chickens_eaten;
  uint64_t chickens_wasted;
  uint64_t promises_broken;
  uint64_t ghosts_raised;
  uint64_t skeletons_raised;
  uint64_t friendly_kills;
  uint64_t lies_told;
  uint64_t creatures_annoyed;
  uint64_t graveyard_bodys;
  uint64_t vamps_created;
  uint64_t num_creatures;
  uint64_t imps_deployed;
  uint64_t battles_won;
  uint64_t battles_lost;
  uint64_t money;
  uint64_t dngn_breached_count;
  uint64_t doors_destroyed;
  uint64_t rooms_destroyed;
  uint64_t dungeon_area;
  uint64_t ideas_researched;
  uint64_t creatures_scavenged;
  uint64_t creatures_from_sacrifice;
  uint64_t spells_stolen;
  uint64_t gold_pots_stolen;
  uint64_t average_room_efficiency;
  uint64_t player_rating;
  uint64_t player_style;
  uint64_t doors_unused;
  uint64_t traps_unused;
  uint64_t num_rooms;
  uint64_t gameplay_time;
  uint64_t num_entrances;
  uint64_t hopes_dashed;
  uint64_t allow_save_score;
  uint64_t player_score;
  uint64_t keeper_destroyed[PLAYERS_COUNT];
};

#pragma pack()
/******************************************************************************/
int64_t update_dungeons_scores(void);
TbBool update_dungeon_scores_for_player(struct PlayerInfo *player);
TbBool load_stats_files(void);

uint64_t compute_dungeon_rooms_attraction_score(int64_t num_entrance_slbs, int64_t rooms_area, int64_t entrance_gen);
uint64_t compute_dungeon_creature_tactics_score(int64_t battles_won, int64_t battles_lost, int64_t scavenge_gain, int64_t scavenge_lost);
uint64_t compute_dungeon_rooms_variety_score(int64_t room_types, int64_t total_area);
uint64_t compute_dungeon_train_research_manufctr_wealth_score(int64_t total_train, int64_t total_research, int64_t total_manufctr, int64_t total_wealth);
uint64_t compute_dungeon_creature_amount_score(int64_t total_creatrs);
uint64_t compute_dungeon_creature_mood_score(int64_t survived_creatrs, int64_t annoyed_creatrs);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
