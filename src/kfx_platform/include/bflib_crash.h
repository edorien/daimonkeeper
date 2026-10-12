/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_crash.h
 *     Header file for bflib_crash.c.
 * @par Purpose:
 *     Program failure handling system.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     09 Nov 2010 - 11 Nov 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef BFLIB_CRASH_H
#define BFLIB_CRASH_H

#include "bflib_basics.h"

#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// The packet file in use, for the crash log (set by kfx_game's game_replay.c;
// state 0 = none, 1 = recording, 2 = playing back).
extern char replay_crashlog_fname[OS_ABSPATH_SIZE];
extern volatile uint8_t replay_crashlog_state;
extern volatile GameTurn replay_crashlog_turn;

void LbErrorParachuteInstall(void);
void LbErrorParachuteUpdate(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif

#endif
