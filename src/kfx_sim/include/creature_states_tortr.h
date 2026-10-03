/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file creature_states_tortr.h
 *     Header file for creature_states_tortr.c.
 * @par Purpose:
 *     Creature state machine functions for their job in various rooms.
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
#ifndef DK_CRTRSTATETORTR_H
#define DK_CRTRSTATETORTR_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
#pragma pack(1)

struct Thing;
struct Room;

enum CreatureTortureVisualStates {
    CTVS_TortureRandMove,
    CTVS_TortureGoToDevice,
    CTVS_TortureInDevice,
};

#pragma pack()
/******************************************************************************/
int64_t at_kinky_torture_room(struct Thing *thing);
CrStateRet kinky_torturing(struct Thing *thing);
CrCheckRet process_kinky_function(struct Thing *thing);

int64_t at_torture_room(struct Thing *thing);
CrStateRet torturing(struct Thing *thing);
CrCheckRet process_torture_function(struct Thing *thing);
int64_t cleanup_torturing(struct Thing *thing);
/** What a torture victim dying in `room` becomes for the room's owner (its kind's torture_kind, else the room's
 *  creation model); whether it does is ghost_convert_chance. */
ThingModel torture_death_kind(const struct Thing *thing, const struct Room *room);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
