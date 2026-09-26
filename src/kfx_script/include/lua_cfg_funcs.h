/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lua_cfg_funcs.h
 *     Header file for lua_cfg_funcs.c.
 * @par Purpose:
 *     Console commands
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef LUACFGFUNCS_H
#define LUACFGFUNCS_H

#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

// LUA_FUNCS_MAX/LUA_FUNCNAME_LENGTH and struct LuaFuncsConf moved to
// game_legacy.h (stage 9) -- struct Configs embeds a LuaFuncsConf by
// value, so game_legacy.h needs the real definition, not just this
// header's function declarations. See docs/refactor/stage-09-kfx-game.md.

FuncIdx get_function_idx(const char *func_name,const struct NamedCommand * Cfuncs);

TbResult luafunc_magic_use_power(FuncIdx func_idx, PlayerNumber plyr_idx, PowerKind pwkind,
    int64_t splevel, MapSubtlCoord stl_x, MapSubtlCoord stl_y, struct Thing *thing, uint64_t allow_flags);

int64_t luafunc_crstate_func(FuncIdx func_idx,struct Thing *thing);
void luafunc_room_capacity_func(FuncIdx func_idx, struct Room *room);
int64_t luafunc_thing_update_func(FuncIdx func_idx,struct Thing *thing);
int64_t luafunc_trap_activation_func(FuncIdx func_idx,struct Thing *trap, struct Thing *creature);
int64_t luafunc_shot_hit_thing_func(FuncIdx func_idx,struct Thing *shot, struct Thing *shooter, struct Thing *target, MapSubtlCoord next_stl_x, MapSubtlCoord next_stl_y);

#ifdef __cplusplus
}
#endif

#endif