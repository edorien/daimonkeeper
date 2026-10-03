/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file player_computer.h
 *     Header file for the computer player AI (kfx_ai).
 * @par Purpose:
 *     Computer player activities. The data types and the accessors kfx_sim
 *     needs are kfx_sim's player_computer_types.h (refactor pass 2, S14).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     10 Mar 2009 - 20 Mar 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_PLYR_COMPUT_H
#define DK_PLYR_COMPUT_H

#include "bflib_basics.h"
#include "globals.h"

#include "config.h"
#include "config_compp.h"
#include "player_data.h"
#include "player_computer_types.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

/******************************************************************************/
extern const struct ValidRooms valid_rooms_to_build[];

extern Comp_Process_Func const computer_process_func_list[];

extern Comp_Event_Func const computer_event_func_list[];

extern Comp_EvntTest_Func const computer_event_test_func_list[];

extern Comp_Check_Func const computer_check_func_list[];
/******************************************************************************/
int64_t set_autopilot_type(PlayerNumber plridx, int64_t aptype);
/******************************************************************************/
void shut_down_process(struct Computer2 *comp, struct ComputerProcess *cproc);
void reset_process(struct Computer2 *comp, struct ComputerProcess *cproc);
void suspend_process(struct Computer2 *comp, struct ComputerProcess *cproc);
int64_t computer_process_index(const struct Computer2 *comp, const struct ComputerProcess *cproc);
struct ComputerProcess *get_computer_process(struct Computer2 *comp, int64_t cproc_idx);
/******************************************************************************/
TbBool computer_player_in_emergency_state(const struct Computer2 *comp);
TbBool is_there_an_attack_task(const struct Computer2 *comp);
struct ComputerTask *computer_setup_build_room(struct Computer2 *comp, RoomKind rkind, int64_t width_slabs, int64_t height_slabs, int64_t look_randstart);
struct ComputerTask * able_to_build_room(struct Computer2 *comp, struct Coord3d *pos, RoomKind rkind, int64_t width_slabs, int64_t height_slabs, int64_t max_slabs_dist, int64_t perfect);
int64_t computer_finds_nearest_room_to_gold(struct Computer2 *comp, struct Coord3d *pos, struct GoldLookup **gldlookref);
void setup_dig_to(struct ComputerDig *cdig, const struct Coord3d startpos, const struct Coord3d endpos);
int64_t move_imp_to_dig_here(struct Computer2 *comp, struct Coord3d *pos, int64_t max_amount);
int64_t move_imp_to_mine_here(struct Computer2 *comp, struct Coord3d *pos, int64_t max_amount);
void get_opponent(struct Computer2 *comp, struct THate hate[]);
/******************************************************************************/
int64_t set_next_process(struct Computer2 *comp);
void computer_check_events(struct Computer2 *comp);
TbBool process_checks(struct Computer2 *comp);
GoldAmount get_computer_money_less_cost(const struct Computer2 *comp);
void computer_set_dungeon(struct Computer2 *comp, struct Dungeon *dungeon);
TbBool creature_could_be_placed_in_better_room(const struct Computer2 *comp, const struct Thing *thing);
CreatureJob get_job_to_place_creature_in_room(const struct Computer2 *comp, const struct Thing *thing);
int64_t xy_walkable(MapSubtlCoord stl_x, MapSubtlCoord stl_y, int64_t plyr_idx);
/******************************************************************************/
struct ComputerTask *get_computer_task(int64_t idx);
struct ComputerTask *get_task_in_progress(struct Computer2 *comp, ComputerTaskType ttype);
struct ComputerTask *get_task_in_progress_in_list(const struct Computer2 *comp, const ComputerTaskType *ttypes);
TbBool is_task_in_progress(struct Computer2 *comp, ComputerTaskType ttype);
TbBool is_task_in_progress_using_hand(struct Computer2 *comp);
TbBool computer_task_invalid(const struct ComputerTask *ctask);
TbBool remove_task(struct Computer2 *comp, struct ComputerTask *ctask);
void shut_down_task_process(struct Computer2 *comp, struct ComputerTask *ctask);
const char *computer_task_code_name(int64_t ctask_type);

TbBool create_task_move_creatures_to_defend(struct Computer2 *comp, struct Coord3d *pos, int64_t creatrs_num, uint64_t evflags);
TbBool create_task_move_creatures_to_room(struct Computer2 *comp, int64_t room_idx, int64_t creatrs_num);
TbBool create_task_magic_battle_call_to_arms(struct Computer2 *comp, struct Coord3d *pos, int64_t par2, int64_t creatrs_num);
TbBool create_task_magic_support_call_to_arms(struct Computer2 *comp, struct Coord3d *pos, int64_t cta_duration, int64_t repeat_num);
TbBool create_task_pickup_for_attack(struct Computer2 *comp, struct Coord3d *pos, int64_t creatrs_num);
TbBool create_task_sell_traps_and_doors(struct Computer2 *comp, int64_t num_to_sell, GoldAmount gold_up_to, TbBool allow_deployed);
TbBool create_task_move_gold_to_treasury(struct Computer2 *comp, int64_t num_to_move, int64_t gold_up_to);
TbBool create_task_move_creature_to_subtile(struct Computer2 *comp, const struct Thing *thing, MapSubtlCoord stl_x, MapSubtlCoord stl_y, CrtrStateId dst_state);
TbBool create_task_move_creature_to_pos(struct Computer2 *comp, const struct Thing *thing, const struct Coord3d pos, CrtrStateId dst_state);
TbBool create_task_dig_to_attack(struct Computer2 *comp, const struct Coord3d startpos, const struct Coord3d endpos, PlayerNumber victim_plyr_idx, int64_t parent_cproc_idx);
TbBool create_task_slap_imps(struct Computer2 *comp, int64_t creatrs_num, TbBool skip_speed);
TbBool create_task_dig_to_neutral(struct Computer2 *comp, const struct Coord3d startpos, const struct Coord3d endpos);
TbBool create_task_dig_to_gold(struct Computer2 *comp, const struct Coord3d startpos, const struct Coord3d endpos, int64_t parent_cproc_idx, int64_t count_slabs_to_dig, int64_t gold_lookup_idx);
TbBool create_task_dig_to_entrance(struct Computer2 *comp, const struct Coord3d startpos, const struct Coord3d endpos, int64_t parent_cproc_idx, int64_t entroom_idx);
TbBool create_task_magic_speed_up(struct Computer2 *comp, const struct Thing *creatng, KeepPwrLevel power_level);
TbBool create_task_attack_magic(struct Computer2 *comp, const struct Thing *creatng, PowerKind pwkind, int64_t repeat_num, KeepPwrLevel power_level, int64_t gaction);
TbBool create_task_sacrifice_diggers(struct Computer2 *comp, int64_t max_level, int64_t digger_model_id);
TbResult script_computer_dig_to_location(int64_t plyr_idx, TbMapLocation origin, TbMapLocation destination);

TbBool computer_able_to_use_power(struct Computer2 *comp, PowerKind pwkind, KeepPwrLevel power_level, int64_t amount);
int64_t computer_get_room_role_total_capacity(struct Computer2 *comp, RoomRole rrole);
int64_t computer_get_room_kind_free_capacity(struct Computer2 *comp, RoomKind room_kind);
TbBool computer_finds_nearest_room_to_pos(struct Computer2 *comp, struct Room **retroom, struct Coord3d *nearpos);
int64_t process_tasks(struct Computer2 *comp);
int64_t computer_check_any_room(struct Computer2* comp, struct ComputerProcess* cproc);
TbResult game_action(PlayerNumber plyr_idx, int64_t gaction, KeepPwrLevel power_level,
    MapSubtlCoord stl_x, MapSubtlCoord stl_y, int64_t param1, int64_t param2);
TbResult try_game_action(struct Computer2 *comp, PlayerNumber plyr_idx, int64_t gaction, KeepPwrLevel power_level,
    MapSubtlCoord stl_x, MapSubtlCoord stl_y, int64_t param1, int64_t param2);
ToolDigResult tool_dig_to_pos2_f(struct Computer2 * comp, struct ComputerDig * cdig, TbBool simulation, DigFlags digflags, const char *func_name);
TbBool add_trap_location_if_requested(struct Computer2 *comp, struct ComputerTask *ctask, TbBool is_task_dig_to_attack);
#define tool_dig_to_pos2(comp,cdig,simulation,digflags) tool_dig_to_pos2_f(comp,cdig,simulation,digflags,__func__)
#define search_spiral(pos, owner, area_total, cb) search_spiral_f(pos, owner, area_total, cb, __func__)
int64_t search_spiral_f(struct Coord3d *pos, PlayerNumber owner, int64_t area_total, int64_t (*cb)(MapSubtlCoord, MapSubtlCoord, int64_t), const char *func_name);
/******************************************************************************/
ItemAvailability computer_check_room_available(const struct Computer2 * comp, RoomKind rkind);
TbBool computer_find_non_solid_block(const struct Computer2 *comp, struct Coord3d *pos);
TbBool computer_find_safe_non_solid_block(const struct Computer2* comp, struct Coord3d* pos);

int64_t count_creatures_in_dungeon(const struct Dungeon *dungeon);
int64_t count_entrances(const struct Computer2 *comp, PlayerNumber plyr_idx);
int64_t count_diggers_in_dungeon(const struct Dungeon *dungeon);
int64_t check_call_to_arms(struct Computer2 *comp);
int64_t count_creatures_for_defend_pickup(struct Computer2 *comp);
uint64_t count_creatures_availiable_for_fight(struct Computer2 *comp, struct Coord3d *pos);

int64_t setup_computer_attack(struct Computer2 *comp, struct ComputerProcess *cproc, struct Coord3d *pos, int64_t victim_plyr_idx);

TbBool setup_a_computer_player(PlayerNumber plyr_idx, int64_t comp_model);
void process_computer_players2(void);
void setup_computer_players2(void);
void restore_computer_player_after_load(void);
/** Each player's computer points at the player's dungeon; a computer with no player is cleared and points at none.
 *  At a level's start as after a load (refactor pass 5, P5-F21: at the start, a computer with no player, or a
 *  player that isn't an active keeper, pointed at dungeon 0, after a load at none or its own). */
void computer_players_set_dungeons(void);

TbBool computer_force_dump_held_things_on_map(struct Computer2 *comp, const struct Coord3d *pos);
TbBool computer_force_dump_specific_held_thing(struct Computer2 *comp, struct Thing *thing, const struct Coord3d *pos);
struct Thing* find_creature_for_defend_pickup(struct Computer2* comp);

TbBool script_support_setup_player_as_computer_keeper(PlayerNumber plyr_idx, int64_t comp_model);
TbBool script_support_setup_player_as_zombie_keeper(PlayerNumber plyr_idx);
TbBool reactivate_build_process(struct Computer2* comp, RoomKind rkind);
TbBool toggle_computer_player(PlayerNumber plyr_idx);
/* What SET_COMPUTER_GLOBALS / _PROCESS / _CHECKS and their Lua twins do, for players plr_start..plr_end-1
 * (refactor pass 3, S07). The process and check setters return how many they changed; with `report` set they
 * log each change to the script log, as the script commands do. */
void computer_set_globals(PlayerNumber plr_start, PlayerNumber plr_end, int64_t dig_stack_size, int64_t processes_time,
    int64_t click_rate, int64_t max_room_build_tasks, int64_t turn_begin, int64_t sim_before_dig, int64_t task_delay);
int64_t computer_set_process_config(PlayerNumber plr_start, PlayerNumber plr_end, const char *procname, int64_t priority,
    int64_t config_value_2, int64_t config_value_3, int64_t config_value_4, int64_t config_value_5, TbBool report);
int64_t computer_set_check_config(PlayerNumber plr_start, PlayerNumber plr_end, const char *chkname, int64_t turns_interval,
    int64_t primary_parameter, int64_t secondary_parameter, int64_t tertiary_parameter, int64_t last_run_turn,
    TbBool stop_at_unnamed, TbBool report);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
