/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file creature_states_pray.h
 *     Header file for creature_states_pray.c.
 * @par Purpose:
 *     Creature state machine functions related to temple.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   KeeperFX Team
 * @date     23 Sep 2009 - 05 Jan 2011
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_CRTRSTATEPRAY_H
#define DK_CRTRSTATEPRAY_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
#pragma pack(1)

struct Thing;

#pragma pack()
/******************************************************************************/
int64_t at_temple(struct Thing *thing);
CrStateRet praying_in_temple(struct Thing *thing);
int64_t process_temple_cure(struct Thing *thing);
CrCheckRet process_temple_function(struct Thing *thing);
int64_t state_cleanup_in_temple(struct Thing *thing);

int64_t cleanup_sacrifice(struct Thing *thing);
int64_t creature_being_sacrificed(struct Thing *thing);
int64_t creature_sacrifice(struct Thing *thing);
int64_t creature_being_summoned(struct Thing *thing);

void kill_all_players_chickens(PlayerNumber plyr_idx);
void apply_spell_effect_to_players_creatures(PlayerNumber plyr_idx, ThingModel crmodel, int64_t spl_idx, CrtrExpLevel overchrg);
TbBool add_anger_to_all_creatures_of_player(PlayerNumber plyr_idx, int64_t percentage);

TbBool find_temple_pool(int64_t player_idx, struct Coord3d *pos);
void process_sacrifice_creature(struct Coord3d *pos, ThingModel model, PlayerNumber owner, TbBool partial);

void script_set_sacrifice_recipe(const int64_t action, const int64_t param, ThingModel* victims);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
