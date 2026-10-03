/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lvl_script.h
 *     Header file for lvl_script.c.
 * @par Purpose:
 *     Level script commands support.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   KeeperFX Team
 * @date     12 Feb 2009 - 24 Feb 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_LVLSCRIPT_VALUE_H
#define DK_LVLSCRIPT_VALUE_H


#ifdef __cplusplus
extern "C" {
#endif

/** The original game's commands that act on a player's creatures (refactor pass 4, S09: what their processes call). */
void script_kill_creatures(PlayerNumber plyr_idx, int64_t crmodel, int64_t criteria, int64_t copies_num);
TbBool script_level_up_creature(PlayerNumber plyr_idx, int64_t crmodel, int64_t criteria, int64_t count);
TbBool script_change_creature_owner_with_criteria(PlayerNumber origin_plyr_idx, int64_t crmodel, int64_t criteria, PlayerNumber dest_plyr_idx);
/** Runs a script value: its command's process, once for each player in the range. */
void script_process_value(uint64_t var_index, uint64_t plr_range_id, struct ScriptValue *value);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
