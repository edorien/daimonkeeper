/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file timer.h
 *     Header file for timer.c.
 * @par Purpose:
 *     Level-load timing: measures how long each phase of loading a level
 *     takes and logs a summary at the end.
 * @par Comment:
 *     The original flat-tree timer.c/timer.h (TimerGame/Timer/
 *     timerstarttime/update_time()/get_game_time()) were split apart by
 *     the src/ -> src/kfx_* refactor: the state and update_time() moved
 *     into kfx_sim_state.h/kfx_frontend's front_input.c, reached from
 *     lower layers via the ports. This file only carries the new
 *     level_load_time_phase() mechanism (added upstream after that
 *     split), which has no lower-layer caller -- every call site
 *     (game_loop.c, main_game.c, main.cpp) is kfx_game or app_entry, so
 *     it lives directly in kfx_game with no callback indirection needed.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/

#ifndef DK_TIMER_H
#define DK_TIMER_H

#include "bflib_basics.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
enum LevelLoadTimeKind {
    LevelLoadTime_Total,
    LevelLoadTime_Sprites,
    LevelLoadTime_Configs,
    LevelLoadTime_Data,
    LevelLoadTime_Navigation,
    LevelLoadTime_GameSetup,
    LevelLoadTime_EngineStartup,
    LevelLoadTime_Count,
};
void level_load_time_phase(enum LevelLoadTimeKind kind);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
