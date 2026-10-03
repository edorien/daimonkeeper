/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file creature_states.h
 *     Header file for creature_states.c.
 * @par Purpose:
 *     Creature states structure and function definitions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     23 Sep 2009 - 11 Nov 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_CRTRSTATE_H
#define DK_CRTRSTATE_H

#include "bflib_basics.h"
#include "globals.h"
#include "config.h"

#define FIGHT_FEAR_DELAY 160
#define STATE_TYPES_COUNT CrStTyp_ListEnd

#ifdef __cplusplus
extern "C" {
#endif

// enum CreatureStates and enum CreatureStateTypes moved to globals.h
// (stage 13) so config_creature.c/config_crtrmodel.c can reference the
// CrSt_*/CrStTyp_* vocabulary without depending on this simulation
// header -- see docs/refactor/stage-13-enforce-and-document.md.

enum CreatureTrainingModes {
    CrTrMd_Unused = 0,
    CrTrMd_SearchForTrainPost,
    CrTrMd_SelectPositionNearTrainPost,
    CrTrMd_MoveToTrainPost,
    CrTrMd_TurnToTrainPost,
    CrTrMd_DoTrainWithTrainPost,
    CrTrMd_PartnerTraining,
};

enum JobStage {
    JobStage_Unused = 0,          // Initial job stage (unused in current implementation)
    JobStage_SearchingForWork = 1,// Workshop: Finding an available work position in the room
    JobStage_PreparingToWork = 2, // Workshop: Setting up equipment and work timer before starting
    JobStage_MovingToPosition = 3,// Workshop/Research: Moving to or staying at work position
    JobStage_TurningToFace = 4,   // Workshop/Research: Turning to face work direction or random thinking
    JobStage_Manufacturing = 5,   // Workshop: Actively manufacturing items (swinging weapon animation)
    JobStage_JobFailed = 6,       // Unused
    JobStage_ScavengedDisappearing = 7, // Scavenging: This creature was successfully scavenged and is disappearing
    JobStage_BeingScavenged = 8   // Scavenging: This creature is currently being scavenged by an enemy creature
};

/** Defines return values of creature state functions. */
enum CreatureStateReturns {
    CrStRet_Deleted       = -1, /**< Returned if the creature being updated no longer exists. */
    CrStRet_Unchanged     =  0, /**< Returned if no change was made to the creature data. */
    CrStRet_Modified      =  1, /**< Returned if the creature was updated and possibly some variables have changed inside, including state. */
    CrStRet_ResetOk       =  2, /**< Returned if the creature state has been reset because task was completed. */
    CrStRet_ResetFail     =  3, /**< Returned if the creature state has been reset, task was either abandoned or couldn't be completed. */
};

/** Defines return values of creature state check functions. */
enum CreatureCheckReturns {
    CrCkRet_Deleted       = -1, /**< Returned if the creature being updated no longer exists. */
    CrCkRet_Available     =  0, /**< Returned if the creature is available for additional processing, even reset. */
    CrCkRet_Continue      =  1, /**< Returned if the action being performed on the creature shall continue, creature shouldn't be processed. */
};

/******************************************************************************/
#pragma pack(1)

struct Thing;
struct Room;

typedef int64_t (*CreatureStateFunc1)(struct Thing *);
typedef char (*CreatureStateFunc2)(struct Thing *);
typedef CrCheckRet (*CreatureStateCheck)(struct Thing *);


/******************************************************************************/

extern const CreatureStateFunc1 process_func_list[];
extern const CreatureStateFunc2 move_from_slab_func_list[];
extern const CreatureStateCheck move_check_func_list[];

#pragma pack()
/******************************************************************************/

extern int64_t const state_type_to_gui_state[];
/******************************************************************************/
CrtrStateId get_creature_state_besides_move(const struct Thing *thing);
CrtrStateId get_creature_state_besides_interruptions(const struct Thing *thing);
int64_t get_creature_state_type_f(const struct Thing *thing, const char *func_name);
#define get_creature_state_type(thing) get_creature_state_type_f(thing,__func__)

struct CreatureStateConfig *get_thing_active_state_info(struct Thing *thing);
struct CreatureStateConfig *get_thing_continue_state_info(struct Thing *thing);
struct CreatureStateConfig *get_thing_state_info_num(CrtrStateId state_id);
struct CreatureStateConfig *get_creature_state_with_task_completion(struct Thing *thing);

TbBool state_info_invalid(struct CreatureStateConfig *stati);
TbBool can_change_from_state_to(const struct Thing *thing, CrtrStateId curr_state, CrtrStateId next_state);
TbBool internal_set_thing_state(struct Thing *thing, CrtrStateId nState);
TbBool external_set_thing_state_f(struct Thing *thing, CrtrStateId state, const char *func_name);
#define external_set_thing_state(thing,state) external_set_thing_state_f(thing,state,__func__)
TbBool init_creature_state(struct Thing *creatng);
TbBool initialise_thing_state_f(struct Thing *thing, CrtrStateId nState, const char *func_name);
#define initialise_thing_state(thing, nState) initialise_thing_state_f(thing, nState,__func__)
TbBool cleanup_current_thing_state(struct Thing *thing);
TbBool cleanup_creature_state_and_interactions(struct Thing *thing);
int64_t state_cleanup_in_room(struct Thing *creatng);
int64_t set_start_state_f(struct Thing *thing,const char *func_name);
#define set_start_state(thing) set_start_state_f(thing,__func__)
int64_t patrol_here(struct Thing* creatng);
int64_t patrolling(struct Thing* creatng);
/******************************************************************************/
TbBool creature_model_bleeds(uint64_t crmodel);
TbBool creature_can_hear_within_distance(const struct Thing *thing, int64_t dist);
int64_t get_thing_navigation_distance(struct Thing *creatng, struct Coord3d *pos , unsigned char a3);
void create_effect_around_thing(struct Thing *thing, int64_t eff_kind);
int64_t get_creature_gui_job(const struct Thing *thing);
int64_t setup_head_for_empty_treasure_space(struct Thing *thing, struct Room *room);
int64_t process_creature_needs_to_heal_critical(struct Thing *creatng);
int64_t setup_creature_leaves_or_dies(struct Thing *creatng);

void creature_drop_dragged_object(struct Thing *crtng, struct Thing *dragtng);
void creature_drag_object(struct Thing *creatng, struct Thing *dragtng);
TbBool creature_is_dragging_something(const struct Thing *creatng);
TbBool creature_is_dragging_spellbook(const struct Thing *creatng);
void stop_creature_being_dragged_by(struct Thing *dragtng, struct Thing *creatng);

void make_creature_conscious(struct Thing *creatng);
void make_creature_unconscious(struct Thing *creatng);
void make_creature_conscious_without_changing_state(struct Thing *creatng);

TbBool check_experience_upgrade(struct Thing *thing);
void set_creature_size_stuff(struct Thing *creatng);
int64_t process_work_speed_on_work_value(const struct Thing *thing, int64_t base_val);
TbBool find_random_valid_position_for_thing_in_room_avoiding_object(struct Thing *thing, const struct Room *room, struct Coord3d *pos);
SubtlCodedCoords find_position_around_in_room(const struct Coord3d *pos, PlayerNumber owner, RoomKind rkind, struct Thing *thing);
void remove_health_from_thing_and_display_health(struct Thing *thing, HitPoints delta);

TbBool process_creature_hunger(struct Thing *thing);
void process_person_moods_and_needs(struct Thing *thing);
TbBool restore_creature_flight_flag(struct Thing *creatng);
TbBool attempt_to_destroy_enemy_room(struct Thing *thing, MapSubtlCoord stl_x, MapSubtlCoord stl_y);

TbBool room_initially_valid_as_type_for_thing(const struct Room *room, RoomRole rrole, const struct Thing *thing);
TbBool room_still_valid_as_type_for_thing(const struct Room *room, RoomRole rrole, const struct Thing *thing);
TbBool creature_job_in_room_no_longer_possible_f(const struct Room *room, CreatureJob jobpref, const struct Thing *thing, const char *func_name);
#define creature_job_in_room_no_longer_possible(room, jobpref, thing) creature_job_in_room_no_longer_possible_f(room, jobpref, thing, __func__)
TbBool creature_free_for_sleep(const struct Thing *thing,  CrtrStateId state);

// Finding a nearby position to move during a job
TbBool creature_choose_random_destination_on_valid_adjacent_slab(struct Thing *thing);
TbBool person_get_somewhere_adjacent_in_room_f(struct Thing *thing, const struct Room *room, struct Coord3d *pos, const char *func_name);
#define person_get_somewhere_adjacent_in_room(thing, room, pos) person_get_somewhere_adjacent_in_room_f(thing, room, pos, __func__)
TbBool person_get_somewhere_adjacent_in_room_around_borders_f(struct Thing *thing, const struct Room *room, struct Coord3d *pos, const char *func_name);
#define person_get_somewhere_adjacent_in_room_around_borders(thing, room, pos) person_get_somewhere_adjacent_in_room_around_borders_f(thing, room, pos, __func__)

void place_thing_in_creature_controlled_limbo(struct Thing *thing);
void remove_thing_from_creature_controlled_limbo(struct Thing *thing);
TbBool get_random_position_in_dungeon_for_creature(PlayerNumber plyr_idx, unsigned char wandr_select, struct Thing *thing, struct Coord3d *pos);

struct Room* get_room_for_thing_salary(struct Thing* creatng, unsigned char *navtype);
/******************************************************************************/
TbBool creature_is_dying(const struct Thing *thing);
TbBool creature_is_being_dropped(const struct Thing *thing);
TbBool creature_is_being_unconscious(const struct Thing *thing);
TbBool creature_can_be_set_unconscious(const struct Thing *creatng, const struct Thing *killertng, CrDeathFlags flags);
TbBool creature_is_celebrating(const struct Thing *thing);
TbBool creature_is_being_tortured(const struct Thing *thing);
TbBool creature_is_being_tortured_including_kinky(const struct Thing* thing);
TbBool creature_is_being_sacrificed(const struct Thing *thing);
TbBool creature_is_leaving_and_cannot_be_stopped(const struct Thing* thing);
TbBool creature_is_kept_in_prison(const struct Thing *thing);
TbBool creature_is_being_summoned(const struct Thing *thing);
TbBool creature_is_doing_anger_job(const struct Thing *thing);
TbBool creature_is_doing_garden_activity(const struct Thing *thing);
TbBool creature_is_taking_salary_activity(const struct Thing *thing);
TbBool creature_is_doing_temple_pray_activity(const struct Thing *thing);
TbBool creature_is_training(const struct Thing *thing);
TbBool creature_is_being_scavenged(const struct Thing *thing);
TbBool creature_is_scavengering(const struct Thing *thing);
TbBool creature_is_at_alarm(const struct Thing *thing);
TbBool creature_is_escaping_death(const struct Thing *thing);
TbBool creature_is_fleeing_combat(const struct Thing *thing);
TbBool creature_is_called_to_arms(const struct Thing *thing);
TbBool creature_affected_by_call_to_arms(const struct Thing *thing);
TbBool creature_is_kept_in_custody(const struct Thing *thing);
TbBool creature_is_kept_in_custody_by_enemy(const struct Thing *thing);
TbBool creature_is_kept_in_custody_by_player(const struct Thing *thing, PlayerNumber plyr_idx);
int64_t player_keeping_creature_in_custody(const struct Thing* thing);
TbBool creature_state_is_unset(const struct Thing *thing);
TbBool creature_is_hostile_towards(const struct Thing *tng1, const struct Thing *tng2);
/** While a thing-list scan is running (get_nth_thing_of_class_with_filter), hostility lookups may be memoized. */
void creature_hostility_memo_begin_scan(void);
void creature_hostility_memo_end_scan(void);
TbBool creature_is_hostile_to_creature(const struct Thing *tng1, const struct Thing *tng2);
TbBool creature_will_attack_creature(const struct Thing *tng1, const struct Thing *tng2);
TbBool trap_is_valid_combat_target_for_creature(const struct Thing* fightng, const struct Thing* enmtng); //todo move
TbBool creature_will_attack_creature_incl_til_death(const struct Thing *tng1, const struct Thing *tng2);
// Compound checks for specific cases
TbBool creature_is_kept_in_custody_by_enemy_or_dying(const struct Thing *thing);

TbBool creature_state_cannot_be_blocked(const struct Thing *thing);

TbBool setup_move_off_lava(struct Thing* thing);
TbBool setup_move_out_of_cave_in(struct Thing* thing);

struct Room* get_room_xy(MapSubtlCoord stl_x, MapSubtlCoord stl_y);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
