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
#ifndef LUAPARAMS_H
#define LUAPARAMS_H

#include "globals.h"
#include "map_locations.h"
#include "room_data.h"
#include <lua.h>

#ifdef __cplusplus
extern "C" {
#endif

struct PlayerRange
{
    PlayerNumber start_idx;
    PlayerNumber end_idx;
};
/****   Checks  *******/
TbBool luaL_isThing(lua_State *L, int64_t index);
TbBool luaL_isPlayer(lua_State *L, int64_t index);

/****   Inputs  *******/
int64_t luaL_optCheckinteger(lua_State* L, int64_t index);
int64_t luaL_optNamedCommand(lua_State *L, int64_t index,const struct NamedCommand * commanddesc);
int64_t luaL_checkNamedCommand(lua_State *L, int64_t index,const struct NamedCommand * commanddesc);
TbMapLocation luaL_checkLocation(lua_State *L, int64_t index);
TbMapLocation luaL_optLocation(lua_State *L, int64_t index);
TbMapLocation luaL_checkHeadingLocation(lua_State *L, int64_t index);
struct PlayerRange luaL_checkPlayerRange(lua_State *L, int64_t index);
PlayerNumber luaL_checkPlayerSingle(lua_State *L, int64_t index);
PlayerNumber luaL_optPlayerSingle(lua_State *L, int64_t index);
PlayerNumber luaL_checkPlayerRangeId(lua_State *L, int64_t index);
MapSubtlCoord luaL_checkstl_x(lua_State *L, int64_t index);
MapSubtlCoord luaL_checkstl_y(lua_State *L, int64_t index);
MapSlabCoord luaL_checkslb_x(lua_State *L, int64_t index);
MapSlabCoord luaL_checkslb_y(lua_State *L, int64_t index);
ActionPointId luaL_checkActionPoint(lua_State *L, int64_t index);
unsigned char luaL_checkCrtLevel(lua_State *L, int64_t index);
unsigned char luaL_checkParty(lua_State *L, int64_t index);
void luaL_checkMessageIcon(lua_State *L, int64_t index, char* type, int64_t* id);
int64_t luaL_checkIntMinMax(lua_State *L, int64_t index,int64_t min, int64_t max);
EffectOrEffElModel luaL_checkEffectOrEffElModel(lua_State *L, int64_t index);
int64_t luaL_checkCreature_or_creature_wildcard(lua_State *L, int64_t index);
int luaL_checkSlab(lua_State *L, int64_t index, MapSlabCoord *slb_x, MapSlabCoord *slb_y);
struct Room* luaL_checkRoom(lua_State *L, int64_t idx);
struct Thing *luaL_checkThing(lua_State *L, int64_t index);
struct Thing *luaL_optCheckThing(lua_State* L, int64_t index);
struct Thing *luaL_checkCreature(lua_State *L, int64_t index);
struct Thing* luaL_checkObject(lua_State* L, int64_t index);
void luaL_checkCoord3d(lua_State *L, int64_t index, struct Coord3d* pos);
int64_t luaL_checkAnimationId(lua_State* L, int64_t index);
void luaL_checkVariable(lua_State* L, int64_t index, int64_t* varib_id, int64_t* varib_type);

/****   Outputs  *******/
void lua_pushThing(lua_State *L, struct Thing* thing);
void lua_pushPlayer(lua_State *L, PlayerNumber plr_idx) ;
void lua_pushPos(lua_State *L, struct Coord3d* pos);
void lua_pushSlab(lua_State *L, MapSlabCoord slb_x, MapSlabCoord slb_y);
void lua_pushPartyTable(lua_State *L, struct Thing* thing);
void lua_pushFamiliarTable(lua_State* L, struct Thing* thing);
void lua_pushParent(lua_State *L, const struct Thing *thing);
void lua_pushRoom(lua_State *L, struct Room* room);
void lua_pushCamera(lua_State *L, PlayerNumber plr_idx);

#ifdef __cplusplus
}
#endif

#endif