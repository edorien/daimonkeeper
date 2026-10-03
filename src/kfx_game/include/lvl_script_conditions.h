/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lvl_script_conditions.h
 *     Header file for lvl_script_conditions.c.
 * @par Purpose:
 *     should only be used by files under lvl_script_*
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   KeeperFX Team
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/

#ifndef DK_LVLSCRIPTCOND_H
#define DK_LVLSCRIPTCOND_H


#include "globals.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif



extern const struct NamedCommand variable_desc[];
extern const struct NamedCommand dk1_variable_desc[];
extern const struct NamedCommand is_free_desc[];
extern const struct NamedCommand orientation_desc[];

struct ScriptVariableDetails get_condition_details(PlayerNumber plyr_idx, unsigned char valtype, int64_t validx);
int64_t get_condition_value(PlayerNumber plyr_idx, unsigned char valtype, int64_t validx);
const char* get_condition_label(PlayerNumber plyr_idx, unsigned char valtype, int64_t validx);
TbBool condition_inactive(int64_t cond_idx);
TbBool get_condition_status(unsigned char opkind, int64_t left_value, int64_t right_value);
void process_conditions(void);
int64_t pop_condition(void);

int64_t get_script_current_condition();
void set_script_current_condition(int64_t current_condition);
void reset_script_conditions(void);

void command_add_condition(int64_t plr_range_id, int64_t opertr_id, int64_t varib_type, int64_t varib_id, int64_t value);
void script_balance_refused_condition(int64_t conditions_before);
void command_add_condition_2variables(int64_t plr_range_id, int64_t opertr_id, int64_t varib_type, int64_t varib_id,int64_t plr_range_id_right, int64_t varib_type_right, int64_t varib_id_right);

#ifdef __cplusplus
}
#endif
#endif
