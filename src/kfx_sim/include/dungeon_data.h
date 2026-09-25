/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file dungeon_data.h
 *     Header file for dungeon_data.c.
 * @par Purpose:
 *     Dungeon data structures definitions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     10 Nov 2009 - 20 Jan 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_DNGN_DATA_H
#define DK_DNGN_DATA_H

#include "bflib_basics.h"
#include "config_magic.h"
#include "config_trapdoor.h"
#include "config_terrain.h"
#include "player_computer.h"
#include "globals.h"
#include "dungeon_stats.h"
#include "thing_creature.h"
#include "thing_doors.h"
#include "room_data.h"
#include "player_data.h"
#include "map_events.h"
#include "tasks_list.h"
#include "thing_traps.h"
#include "roomspace.h"
#include "config_creature.h"
#include "creature_states.h"

#include "power_hand.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define DUNGEONS_COUNT              9
#define DIGGER_TASK_MAX_COUNT       64
#define DUNGEON_RESEARCH_COUNT      2000
#define MAX_THINGS_IN_HAND          64
#define TURN_TIMERS_COUNT           8
#define SCRIPT_FLAGS_COUNT          8
#define MAX_SOE_RADIUS              13
#define CREATURE_GUI_JOBS_COUNT     3
#define CUSTOM_BOX_COUNT            256
#define FX_LINES_COUNT              32
#define MAX_SUMMONS                 255

#define INVALID_DUNGEON (&bad_dungeon)

enum CreatureGUIJob {
    CrGUIJob_Any        =-1,
    CrGUIJob_Wandering  = 0,
    CrGUIJob_Working    = 1,
    CrGUIJob_Fighting   = 2,
};

enum DungeonCreatureTendencies {
    CrTend_None       = 0,
    CrTend_Imprison   = 1,
    CrTend_Flee       = 2,
};

enum DungeonResearchCategory {
    RsCat_None        = 0,
    RsCat_Power       = 1,
    RsCat_Room        = 2,
    RsCat_Creature    = 3,
};

enum DungeonManufactureBuildFlags {
    MnfBldF_Manufacturable = 0x01,
    MnfBldF_Built          = 0x02,
    MnfBldF_Used           = 0x04,
};

/******************************************************************************/
struct DiggerStack {
      SubtlCodedCoords stl_num;
      SpDiggerTaskType task_type;
};

struct ResearchVal {
  unsigned char rtyp;
  int64_t rkind;
  int64_t req_amount;
};

struct TurnTimer {
  uint64_t count;
  unsigned char state;
};

struct TrapInfo
{
    unsigned char trap_amount_offmap[TRAPDOOR_TYPES_MAX];
    unsigned char trap_amount_stored[TRAPDOOR_TYPES_MAX];
    /** Stores flag information about players manufacture of traps of specific kind. */
    unsigned char trap_build_flags[TRAPDOOR_TYPES_MAX];
    /** Amount of traps of every kind for which we can place blueprints. This include both off-map traps and on-map trap boxes.*/
    unsigned char trap_amount_placeable[TRAPDOOR_TYPES_MAX];
    /** Amount of doors of every kind which are stored in workshops. Only on-map door crates which exist in workshop are mentioned here.*/

    unsigned char door_amount_offmap[TRAPDOOR_TYPES_MAX];
    unsigned char door_amount_stored[TRAPDOOR_TYPES_MAX];
    /** Stored flag information about players manufacture of doors of specific kind. */
    unsigned char door_build_flags[TRAPDOOR_TYPES_MAX];
    /** Stored information whether player can place blueprints of doors of specific kind (actually, doors are placed instantly). */
    unsigned char door_amount_placeable[TRAPDOOR_TYPES_MAX];

};

struct BoxInfo
{
    int64_t              activated[CUSTOM_BOX_COUNT];
};


/** Used to set player modifier with script command. */
struct Modifiers
{
    int64_t health;
    int64_t strength;
    int64_t armour;
    int64_t spell_damage;
    int64_t speed;
    int64_t pay;
    int64_t training_cost;
    int64_t scavenging_cost;
    int64_t loyalty;
};

struct Dungeon {
    ThingIndex dnheart_idx;
    struct Coord3d mappos;
    unsigned char creature_tendencies;
    unsigned char computer_enabled;
    int64_t creatr_list_start;
    int64_t digger_list_start;
    ThingIndex summon_list[MAX_SUMMONS];
    int64_t num_summon;
    ThingIndex things_in_hand[MAX_THINGS_IN_HAND];
    unsigned char num_things_in_hand;
    int64_t crmodel_state_type_count[CREATURE_TYPES_MAX][STATE_TYPES_COUNT];
    int64_t guijob_all_creatrs_count[CREATURE_TYPES_MAX][3];
    int64_t guijob_angry_creatrs_count[CREATURE_TYPES_MAX][3];
    int64_t sight_casted_gameturn;
    int64_t sight_casted_thing_idx;
    KeepPwrLevel sight_casted_power_level;
    MapSubtlCoord sight_casted_stl_x;
    MapSubtlCoord sight_casted_stl_y;
    unsigned char soe_explored_flags[2*MAX_SOE_RADIUS][2*MAX_SOE_RADIUS];
    MapSubtlCoord cta_stl_x;
    MapSubtlCoord cta_stl_y;
    KeepPwrLevel cta_power_level;
    uint64_t cta_start_turn;
    TbBool cta_free;
    uint64_t must_obey_turn;
    int64_t hold_audience_cast_turn;
    int64_t scavenge_counters_turn;
    /** Counter of creatures scavenging of each kind, zeroized and recomputed each game turn. */
    unsigned char creatures_scavenging[CREATURE_TYPES_MAX];
    /** Counter of creatures praying. */
    unsigned char creatures_praying[CREATURE_TYPES_MAX];
    unsigned char chickens_sacrificed;
    unsigned char gold_piles_sacrificed;
    unsigned char creature_sacrifice[CREATURE_TYPES_MAX];
    unsigned char creature_sacrifice_exp[CREATURE_TYPES_MAX];
    int64_t num_active_diggers;
    int64_t num_active_creatrs;
    int64_t owned_creatures_of_model[CREATURE_TYPES_MAX];
    /** Total amount of rooms in possession of a player. Rooms which can never be built are not counted. */
    unsigned char total_rooms;
    int64_t total_doors;
    int64_t total_area;
    int64_t total_creatures_left;
    int64_t doors_destroyed;
    int64_t room_manage_area;
    int64_t creatures_scavenged[CREATURE_TYPES_MAX];
    int64_t creatures_scavenge_gain;
    int64_t creatures_scavenge_lost;
    int64_t scavenge_turn_points[CREATURE_TYPES_MAX];
    int64_t scavenge_targets[CREATURE_TYPES_MAX];
    CrtrExpLevel creature_max_level[CREATURE_TYPES_MAX];
    int64_t creatures_annoyed;
    int64_t battles_lost;
    int64_t battles_won;
    /** Amount of room tiles a player had which were destroyed (vandalized or damaged by enemy). */
    int64_t rooms_destroyed;
    int64_t spells_stolen;
    int64_t times_broken_into;
    int64_t gold_pots_stolen;
    int64_t last_entrance_generation_gameturn;
    int64_t turns_between_entrance_generation;
    int64_t last_research_complete_gameturn;
    int64_t manage_score;
    int64_t total_score;
    uint64_t max_gameplay_score;
    int64_t highest_task_number;
    GoldAmount total_money_owned;
    GoldAmount offmap_money_owned;
    struct MapTask task_list[MAPTASKS_COUNT];
    int64_t task_count;
    unsigned char owner;
    int64_t camera_deviate_quake;
    int64_t camera_deviate_jump;
    int64_t score;
    struct ResearchVal research[DUNGEON_RESEARCH_COUNT];
    int64_t current_research_idx;
    int64_t research_num;
    /** How many creatures are force-enabled for each kind.
     * Force-enabled creature can come from portal without additional conditions,
     * but only until dungeon has up to given amount of their kind. */
    unsigned char creature_force_enabled[CREATURE_TYPES_MAX];
    /** Defines whether a creature of each kind is allowed to come from portal.
     * Allowed creatures can join a dungeon if whether attraction condition is met
     * or force-enabled amount isn't reached. */
    unsigned char creature_allowed[CREATURE_TYPES_MAX];
    int64_t magic_level[POWER_TYPES_MAX];
    int64_t magic_resrchable[POWER_TYPES_MAX];
    struct TurnTimer turn_timers[TURN_TIMERS_COUNT];
    int64_t max_creatures_attracted;
    unsigned char heart_destroy_state;
    int64_t heart_destroy_turn;
    struct Coord3d essential_pos;
    struct DiggerStack digger_stack[DIGGER_TASK_MAX_COUNT];
    uint64_t digger_stack_update_turn;
    uint64_t digger_stack_length;
    /** Array with battle indexes with the battles currently visible in fight event message */
    unsigned char visible_battles[3];
    int64_t zoom_annoyed_creature_idx;
    int64_t total_experience_creatures_gained;
    int64_t total_research_points;
    int64_t total_manufacture_points;
    int64_t manufacture_progress;
    ThingClass manufacture_class;
    ThingModel manufacture_kind;
    int64_t turn_last_manufacture;
    int64_t manufacture_level;
    int64_t research_progress;
    struct LevelStats lvstats;
    struct CreatureStorage dead_creatures[DEAD_CREATURES_MAX_COUNT];
    int64_t dead_creatures_count;
    int64_t dead_creature_idx;
    /** Contains map event index or each even button visible on screen. */
    unsigned char event_button_index[EVENT_BUTTONS_COUNT+1];
    int64_t tortured_creatures[CREATURE_TYPES_MAX];
    unsigned char bodies_rotten_for_vampire;
    int64_t portal_scavenge_boost;
    /** Stores how many creatures of each kind of has joined the dungeon during the level.
     * Values are saturated at 255. */
    unsigned char creature_models_joined[CREATURE_TYPES_MAX];
    uint64_t fights_num;
    unsigned char research_override; // could be easily changed into flags..
    int64_t last_creature_dropped_gameturn;
    MapSubtlCoord devastation_centr_x;
    MapSubtlCoord devastation_centr_y;
    GameTurn devastation_turn;
    int64_t creatures_total_pay;
    ThingIndex gold_hoard_for_pickup;
    uint64_t gold_pickup_amount;
    /** Index of last creature picked up of given model. */
    int64_t selected_creatures_of_model[CREATURE_TYPES_MAX];
    /** Index of last creature picked up of given GUI Job. */
    int64_t selected_creatures_of_gui_job[CREATURE_GUI_JOBS_COUNT];
    unsigned char texture_pack;
    unsigned char color_idx;
    struct Modifiers      modifier;
    struct TrapInfo       mnfct_info;
    struct BoxInfo        box_info;
    struct BoxInfo        trap_info;
    struct Coord3d        last_combat_location;
    struct Coord3d        last_eventful_death_location;
    struct Coord3d        last_trap_event_location;
    int64_t                   creature_awarded[CREATURE_TYPES_MAX];
    CrtrExpLevel          creature_entrance_level;
    uint64_t              evil_creatures_converted;
    uint64_t              good_creatures_converted;
    uint64_t              creatures_transferred;
    uint64_t              traps_sold;
    uint64_t              doors_sold;
    uint64_t              manufacture_gold;
    int64_t               creatures_total_backpay;
    int64_t               cheaper_diggers;
    int64_t               event_last_run_turn[EVENT_KIND_COUNT];
    int64_t               script_flags[SCRIPT_FLAGS_COUNT];
    int64_t        room_list_start[TERRAIN_ITEMS_MAX];
    unsigned char         room_buildable[TERRAIN_ITEMS_MAX];
    unsigned char         room_resrchable[TERRAIN_ITEMS_MAX];
    unsigned char         room_discrete_count[TERRAIN_ITEMS_MAX+1];
    ThingIndex            backup_heart_idx;
    ThingIndex            free_soul_idx;
    struct HandRule       hand_rules[CREATURE_TYPES_MAX][HAND_RULE_SLOTS_COUNT];
};
/******************************************************************************/
extern struct Dungeon bad_dungeon;
/******************************************************************************/
struct Dungeon *get_players_num_dungeon_f(PlayerNumber plyr_idx,const char *func_name);
struct Dungeon *get_players_dungeon_f(const struct PlayerInfo *player,const char *func_name);
struct Dungeon *get_dungeon_f(PlayerNumber plyr_num,const char *func_name);
#define get_players_num_dungeon(plyr_idx) get_players_num_dungeon_f(plyr_idx,__func__)
#define get_players_dungeon(player) get_players_dungeon_f(player,__func__)
#define get_dungeon(plyr_idx) get_dungeon_f(plyr_idx,__func__)
#define get_my_dungeon() get_players_num_dungeon_f(my_player_number,__func__)

TbBool dungeon_invalid(const struct Dungeon *dungeon);

void clear_dungeons(void);
void init_dungeons(void);

void decrease_dungeon_area(PlayerNumber plyr_idx, int64_t value);
void increase_room_area(PlayerNumber plyr_idx, int64_t value);
void decrease_room_area(PlayerNumber plyr_idx, int64_t value);
void increase_dungeon_area(PlayerNumber plyr_idx, int64_t value);
TbBool mark_creature_joined_dungeon(struct Thing *creatng);

void player_add_offmap_gold(PlayerNumber plyr_idx, GoldAmount value);

void init_dungeon_essential_position(struct Dungeon *dungeon);
void init_dungeons_essential_position(void);
const struct Coord3d *dungeon_get_essential_pos(PlayerNumber plyr_idx);
TbBool player_has_heart(PlayerNumber plyr_idx);
struct Thing *get_player_soul_container(PlayerNumber plyr_idx);

TbBool player_has_room_of_role(PlayerNumber plyr_idx, RoomRole rrole);
TbBool dungeon_has_room(const struct Dungeon *dungeon, RoomKind rkind);
TbBool dungeon_has_room_of_role(const struct Dungeon *dungeon, RoomRole rrole);
int64_t count_player_discrete_rooms_with_role(PlayerNumber plyr_idx, RoomRole rrole);

TbBool set_creature_tendencies(struct PlayerInfo *player, int64_t tend_type, TbBool val);
TbBool toggle_creature_tendencies(struct PlayerInfo *player, int64_t tend_type);
TbBool player_creature_tends_to(PlayerNumber plyr_idx, int64_t tend_type);

TbBool set_trap_buildable_and_add_to_amount(PlayerNumber plyr_idx, ThingModel trap_kind, int64_t buildable, int64_t amount);
TbBool set_door_buildable_and_add_to_amount(PlayerNumber plyr_idx, ThingModel door_kind, int64_t buildable, int64_t amount);
TbBool dungeon_has_any_buildable_traps(struct Dungeon *dungeon);
TbBool dungeon_has_any_buildable_doors(struct Dungeon *dungeon);

TbBool restart_script_timer(PlayerNumber plyr_idx, int64_t timer_id);
TbBool set_script_flag(PlayerNumber plyr_idx, int64_t flag_id, int64_t value);
void add_to_script_timer(PlayerNumber plyr_idx, unsigned char timer_id, int64_t value);

void add_heart_health(PlayerNumber plyr_idx,HitPoints healthdelta,TbBool warn_on_damage);

// Registered on DungeonAvailabilityCallbacks (src/kfx_config/include/
// dungeon_availability.h); see dungeon_data.c and docs/refactor/
// stage-13-enforce-and-document.md.
TbBool player_has_valid_dungeon(PlayerNumber plyr_idx);
TbBool player_has_valid_dungeon_with_heart(PlayerNumber plyr_idx);
TbBool players_num_dungeon_valid(PlayerNumber plyr_idx);
TbBool players_num_dungeon_valid_with_heart(PlayerNumber plyr_idx);
void set_creature_availability(PlayerNumber plyr_idx, ThingModel crtr_model, int64_t can_be_avail, int64_t force_avail);
void try_set_backup_heart_idx(PlayerNumber owner, ThingIndex thing_idx);
TbBool set_room_resrchable_and_buildable(PlayerNumber plyr_idx, RoomKind rkind, int64_t resrch, int64_t avail);
TbBool get_room_resrchable(PlayerNumber plyr_idx, RoomKind rkind);
void set_all_room_resrchable(PlayerNumber plyr_idx);
TbBool get_room_buildable(PlayerNumber plyr_idx, RoomKind rkind);
void set_all_room_buildable_from_resrchable(PlayerNumber plyr_idx);
TbBool get_magic_resrchable(PlayerNumber plyr_idx, PowerKind pwkind);
void set_magic_resrchable(PlayerNumber plyr_idx, PowerKind pwkind, TbBool resrch);
void set_all_magic_resrchable_unchecked(PlayerNumber plyr_idx);
TbBool get_magic_level_gt0(PlayerNumber plyr_idx, PowerKind pwkind);
TbBool get_trap_placeable(PlayerNumber plyr_idx, int64_t tngmodel);
TbBool get_trap_manufacturable(PlayerNumber plyr_idx, int64_t tngmodel);
TbBool get_trap_built(PlayerNumber plyr_idx, int64_t tngmodel);
TbBool get_door_placeable(PlayerNumber plyr_idx, int64_t door_idx);
TbBool get_door_manufacturable(PlayerNumber plyr_idx, int64_t door_idx);
TbBool get_door_built(PlayerNumber plyr_idx, int64_t door_idx);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
