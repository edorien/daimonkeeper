/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file player_computer_types.h
 *     The computer players' state: types, constants and accessors.
 * @par Purpose:
 *     struct Computer2, struct ComputerTask and the rest of the computer
 *     players' data, which lives in kfx_sim_state (so saves and resyncs
 *     carry it), plus the few accessors kfx_sim itself needs. Split out of
 *     player_computer.h in refactor pass 2 (S14,
 *     docs/refactor-pass2/stage-14-ai-library-spike.md); the AI's behaviour
 *     is kfx_ai's player_computer.h.
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
#ifndef DK_PLYR_COMPUT_TYPES_H
#define DK_PLYR_COMPUT_TYPES_H

#include "bflib_basics.h"
#include "globals.h"

#include "config.h"
#include "config_compp.h"
#include "player_data.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define COMPUTER_TRAP_LOC_COUNT      20

#define COMPUTER_SPARK_POSITIONS_COUNT 64
#define COMPUTER_SOE_GRID_SIZE        8

/** How strong should be the preference to dig gold from treasure room and not other rooms. Originally was 22 subtiles. */
#define TREASURE_ROOM_PREFERENCE_WHILE_DIGGING_GOLD 16

/** How often to check for possible gold veins which could be digged by computer */
#define GOLD_DEMAND_CHECK_INTERVAL 5000
/** How long to wait for diggers to prepare a place for room before dropping the task and assuming it failed */
#define COMPUTER_DIG_ROOM_TIMEOUT 7500
#define COMPUTER_URGENT_BRIDGE_TIMEOUT 1200
#define COMPUTER_TOOL_DIG_LIMIT 356
#define COMPUTER_TOOL_FAILED_DIG_LIMIT 10

/** Holds the return values for the CPU "mark for digging" functions. (see enum ToolDigResults) */
typedef signed char ToolDigResult;
/** Flags to enable actions (e.g. dig gold, build bridge) for the CPU player whilst "marking for digging" (see enum ToolDigFlags). */
typedef signed char DigFlags;

#define COMPUTER_REDROP_DELAY 80

enum ComputerTaskTypes {
    CTT_None = 0,
    CTT_DigRoomPassage,
    CTT_DigRoom,
    CTT_CheckRoomDug,
    CTT_PlaceRoom,
    CTT_DigToEntrance,
    CTT_DigToGold,
    CTT_DigToAttack,
    CTT_MagicCallToArms,
    CTT_PickupForAttack,
    CTT_MoveCreatureToRoom,     // 10
    CTT_MoveCreatureToPos,
    CTT_MoveCreaturesToDefend,
    CTT_SlapDiggers,
    CTT_DigToNeutral,
    CTT_MagicSpeedUp,
    CTT_WaitForBridge,
    CTT_AttackMagic,
    CTT_SellTrapsAndDoors,
    CTT_MoveGoldToTreasury,
    CTT_SacrificeDiggers,
};

enum TrapDoorSellingCategory {
    TDSC_EndList = 0,
    TDSC_DoorCrate,
    TDSC_TrapCrate,
    TDSC_DoorPlaced,
    TDSC_TrapPlaced,
};

enum GameActionTypes {
    GA_None = 0,
    GA_Unk01,
    GA_UsePwrHandPick,
    GA_UsePwrHandDrop,
    GA_UseMkDigger,
    GA_UseSlap,
    GA_UsePwrSight,
    GA_UsePwrObey,
    GA_UsePwrHealCrtr,
    GA_UsePwrCall2Arms,
    GA_UsePwrCaveIn,
    GA_StopPwrCall2Arms,
    GA_StopPwrHoldAudnc,
    GA_Unk13,
    GA_MarkDig,
    GA_Unk15,
    GA_PlaceRoom,
    GA_SetTendencies,
    GA_PlaceTrap,
    GA_PlaceDoor,
    GA_UsePwrLightning,
    GA_UsePwrSpeedUp,
    GA_UsePwrArmour,
    GA_UsePwrRebound,
    GA_UsePwrConceal,
    GA_UsePwrFlight,
    GA_UsePwrVision,
    GA_UsePwrHoldAudnc,
    GA_UsePwrDisease,
    GA_UsePwrChicken,
    GA_UsePwrFreeze,
    GA_UsePwrSlow,
    GA_Unk27,
    GA_UsePwrSlap,
    GA_SellTrap,
    GA_SellDoor,
};

enum ToolDigFlags {
    ToolDig_BasicOnly = 0x00, /**< Allows digging through basic earth slabs (default: always applies). */
    ToolDig_AllowValuable = 0x01, /**< Allows digging through valuable slabs. */
    ToolDig_AllowLiquidWBridge = 0x02, /**< Allows bridging over liquid (bridges must be available to the player for this to have an effect). */
};

/** These are the possible return values for the CPU player's "mark for digging" functions */
enum ToolDigResults {
    TDR_BuildBridgeOnSlab = -5,
    TDR_ToolDigError = -2,
    TDR_ReachedDestination = -1,
    TDR_DigSlab = 0,
};

enum CompProcessFlags {
    ComProc_Unkn0001 = 0x0001,
    ComProc_ListEnd  = 0x0002, /**< Last? */
    ComProc_Unkn0004 = 0x0004, /**< Finished */
    ComProc_Unkn0008 = 0x0008, /**< Done (for subprocesses) */
    ComProc_Unkn0010 = 0x0010,
    ComProc_Unkn0020 = 0x0020, /**< Suspended (Ed: I think this flag is RoomBuildActive...) */
    ComProc_Unkn0040 = 0x0040,
    ComProc_Unkn0080 = 0x0080,
    ComProc_Unkn0100 = 0x0100,
    ComProc_Unkn0200 = 0x0200,
    ComProc_Unkn0400 = 0x0400,
    ComProc_Unkn0800 = 0x0800,
};

enum CompCheckFlags {
    ComChk_Unkn0001 = 0x0001, /**< Disabled */
    ComChk_Unkn0002 = 0x0002, /**< Last */
    ComChk_Unkn0004 = 0x0004,
    ComChk_Unkn0008 = 0x0008,
    ComChk_Unkn0010 = 0x0010,
    ComChk_Unkn0020 = 0x0020,
    ComChk_Unkn0040 = 0x0040,
    ComChk_Unkn0080 = 0x0080,
    ComChk_Unkn0100 = 0x0100,
    ComChk_Unkn0200 = 0x0200,
    ComChk_Unkn0400 = 0x0400,
    ComChk_Unkn0800 = 0x0800,
};

enum CompTaskFlags {
    ComTsk_Unkn0001 = 0x0001, /**< task is disabled */
    ComTsk_Unkn0002 = 0x0002,
    ComTsk_AddTrapLocation = 0x0004, /** if enabled, dug slabs will be added to the computer's list of potential trap positions */
    ComTsk_Unkn0008 = 0x0008,
    ComTsk_Unkn0010 = 0x0010,
    ComTsk_Unkn0020 = 0x0020,
    ComTsk_Unkn0040 = 0x0040,
    ComTsk_Urgent = 0x0080,
};

enum CompTaskStates {
    CTaskSt_None = 0,
    CTaskSt_Wait, /**< Waiting some game turns before starting a new task. */
    CTaskSt_Select, /**< Choosing a task to be performed. */
    CTaskSt_Perform, /**< Performing the task. */
};

/** Return values for computer task functions. */
enum CompTaskRet {
    CTaskRet_Unk0 = 0,
    CTaskRet_Unk1, /**< CONTINUE */
    CTaskRet_Unk2,
    CTaskRet_Unk3,
    CTaskRet_Unk4, /**< FAIL? Wait? */
};

/** Return values for computer process functions. */
enum CompProcRet {
    CProcRet_Fail = 0,
    CProcRet_Continue,
    CProcRet_Finish,
    CProcRet_Unk3,
    CProcRet_Wait,
};

enum ItemAvailabilityRet {
    IAvail_Never         = 0,
    IAvail_Now           = 1,
    IAvail_NeedResearch  = 4,
};

enum CompChatFlags {
    CChat_None          = 0x00,
    CChat_TasksScarce   = 0x01,
    CChat_TasksFrequent = 0x02,
};

enum computer_process_func_list 
{
    cpfl_computer_check_build_all_rooms = 1,
    cpfl_computer_setup_any_room_continue,
    cpfl_computer_check_any_room,
    cpfl_computer_setup_any_room,
    cpfl_computer_check_dig_to_entrance,
    cpfl_computer_setup_dig_to_entrance,
    cpfl_computer_check_dig_to_gold,
    cpfl_computer_setup_dig_to_gold,
    cpfl_computer_check_sight_of_evil,
    cpfl_computer_setup_sight_of_evil,
    cpfl_computer_process_sight_of_evil,
    cpfl_computer_check_attack1,
    cpfl_computer_setup_attack1,
    cpfl_computer_completed_attack1,
    cpfl_computer_check_safe_attack,
    cpfl_computer_process_task,
    cpfl_computer_completed_build_a_room,
    cpfl_computer_paused_task,
    cpfl_computer_completed_task
  };

//TODO COMPUTER This returns NULL, which is unsafe
#define INVALID_COMPUTER_PLAYER NULL
#define INVALID_COMPUTER_PROCESS NULL
#define INVALID_COMPUTER_TASK &kfx_sim_state.computer_task[0]
/******************************************************************************/
#pragma pack(1)

struct Computer2;
struct ComputerProcess;
struct ComputerCheck;
struct ComputerEvent;
struct Event;
struct Thing;
struct Room;
struct ComputerTask;
struct GoldLookup;
struct THate;

typedef int64_t ComputerTaskType;

typedef int64_t (*Comp_Process_Func)(struct Computer2 *, struct ComputerProcess *);
typedef int64_t (*Comp_Check_Func)(struct Computer2 *, struct ComputerCheck *);
typedef int64_t (*Comp_Event_Func)(struct Computer2 *, struct ComputerEvent *,struct Event *);
typedef int64_t (*Comp_EvntTest_Func)(struct Computer2 *, struct ComputerEvent *);
typedef int64_t (*Comp_Task_Func)(struct Computer2 *, struct ComputerTask *);
typedef TbBool (*Comp_HateTest_Func)(const struct Computer2 *, const struct ComputerProcess *, const struct THate *);

struct TaskFunctions {
    const char *name;
    Comp_Task_Func func;
};

struct ValidRooms { // sizeof = 8
    int64_t rkind;
    unsigned char process_idx;
};

struct ComputerDig {
    struct Coord3d pos_E; /**< used by dig to position - set to pos_begin when a dig action fails ?? */
    struct Coord3d pos_dest; /**< used by dig to position - the destination */
    struct Coord3d pos_begin; /**< used by dig to position (the start of the path) and for room dig/place (the centre of the room) */
    struct Coord3d pos_next; /**< used by dig to position - the next position in the path to check */
    int64_t distance; /**< used by dig to position - the distance between a given position and the destination */
    unsigned char hug_side; /**< used by dig to position - the rule to follow when hugging the wall (left-hand rule/side or right-hand rule/side) */
    SmallAroundIndex direction_around; /**< used by dig to position - the forwards direction of the path */
    uint64_t action_success_flag; /**< this is always set to 1... but it's value is used to create a bool test: did action fail */
    int64_t number_of_failed_actions; /**< used by dig to position (incremented when gold is found but digflags is 0, or a mark for digging action failed) */
    MapSubtlCoord last_backwards_step_stl_x; /**< used by dig to position - ?? when a dig action fails, we step backwards, this is this the X coordinate of the slab we stepped back in to */
    MapSubtlCoord last_backwards_step_stl_y; /**< used by dig to position - ?? when a dig action fails, we step backwards, this is this the Y coordinate of the slab we stepped back in to */
    int64_t calls_count; /**< used by dig to position */
    int64_t valuable_slabs_tagged; /**< used by dig to position - Amount of valuable slabs tagged for digging during this dig process. */
    /** Variables for digging (or placing) a room. */
    struct { 
        int64_t area; /**< The number of slabs in the room. */
        int64_t slabs_processed; /**< The number of slabs marked for digging or converted in to a room. */
        /** Variables for the spiral used to dig slabs/place rooms. */
        struct {
            SmallAroundIndex forward_direction; /**< The current direction we are moving through the spiral. */
            int64_t turns_made; /**< The number of turns made in the spiral. */
            int64_t steps_to_take_before_turning; /**< The number of steps to take before the next turn in the spiral. */
            int64_t steps_remaining_before_turn; /**< The number of steps we have left to take before we need to turn in the spiral. */
        } spiral;
    } room;
};

struct ComputerTask {
    unsigned char flags; /**< Values from ComTsk_* enumeration. */
    unsigned char task_state;
    unsigned char ttype;
    unsigned char ottype;
    unsigned char rkind;
    int64_t created_turn;
    struct ComputerDig dig;
    int64_t lastrun_turn;
    int64_t delay;
    struct Coord3d new_room_pos;
    struct Coord3d starting_position;
    union {
    struct {
        /** Amount of items to be sold; task is removed when it reaches zero. */
        int64_t items_amount;
        /** Sum of gold generated by selling. */
        int64_t gold_gain;
        /** Limit of gold generated by selling; task is removed when it is exceeded. */
        int64_t gold_gain_limit;
        /** Limit of total gold owned by player; task is removed when it is exceeded. */
        int64_t total_money_limit;
        /** Whether deployed traps and doors are considered while selling. */
        int64_t allow_deployed; // can be converted to flags
        /** Index of the item currently being checked in list of sellable things. */
        int64_t sell_idx;
    } sell_traps_doors;
    struct {
        /* Amount of gold piles/pots to move */
        int64_t items_amount;
        int64_t room_idx;
        int64_t gold_gain;
        int64_t gold_gain_limit;
        int64_t total_money_limit;
    } move_gold;
    struct {
        struct Coord3d target_pos;
        int64_t repeat_num;
    } magic_cta;
    struct {
        KeepPwrLevel power_level;
        int64_t target_thing_idx;
        int64_t repeat_num;
        int64_t gaction;
        int64_t pwkind;
    } attack_magic;
    struct {
        RoomIndex room_idx1;
        int64_t repeat_num;
        RoomIndex room_idx2;
    } move_to_room;
    struct {
        int64_t evflags;
        struct Coord3d target_pos;
        int64_t repeat_num;
        CrtrStateId target_state;
    } move_to_defend;
    struct {
        int64_t target_thing_idx;
        struct Coord3d target_pos;
        int64_t repeat_num;
        CrtrStateId target_state;
    } move_to_pos;
    struct {
        struct Coord3d target_pos;
        int64_t repeat_num;
        CrtrStateId target_state;
    } pickup_for_attack;
    struct {
        struct Coord3d startpos;
        struct Coord3d endpos;
        /** Target room index. */
        int64_t target_room_idx;
        int64_t target_plyr_idx;
    } dig_to_room;
    struct {
        struct Coord3d startpos;
        struct Coord3d endpos;
        /** Target gold lookup index. */
        int64_t target_lookup_idx;
        int64_t slabs_dig_count;
    } dig_to_gold;
    struct {
        struct Coord3d startpos;
        struct Coord3d endpos;
        int64_t target_plyr_idx;
    } dig_somewhere;
    struct {
        struct Coord3d startpos;
        struct Coord3d endpos;
        int64_t width;
        int64_t height;
        RoomKind kind;
        int64_t area;
    } create_room;
    struct {
        int64_t max_level;
        int64_t digger_model_id;
        CrtrStateId target_state;
    } sacrifice_imp;
    struct {
        TbBool skip_speed;
    } slap_imps;
    };
    int64_t cproc_idx; /**< CProcessId */
    GameTurnDelta cta_duration;
    int64_t next_task;
};

struct OpponentRelation { // sizeof = 394
    GameTurn last_interaction_turn;
    int64_t next_idx;
    int64_t hate_amount;
    struct Coord3d pos_A[COMPUTER_SPARK_POSITIONS_COUNT];
};

struct Computer2 { // sizeof = 5322
  int64_t task_state;
  uint64_t gameturn_delay;
  uint64_t gameturn_wait;
  uint64_t action_status_flag;
  uint64_t tasks_did;
  uint64_t processes_time;
  uint64_t click_rate;
  int64_t dig_stack_size; // seems to be signed long
  uint64_t sim_before_dig;
  int64_t dungeon_idx; // index into kfx_sim_state.dungeon[], -1 = none. Not a pointer: the struct is saved and network-synced as raw bytes.
  uint64_t model;
  uint64_t turn_begin;
  uint64_t max_room_build_tasks;
  uint64_t task_delay;
  struct ComputerProcess processes[COMPUTER_PROCESSES_COUNT+1];
  struct ComputerCheck checks[COMPUTER_CHECKS_COUNT];
  struct ComputerEvent events[COMPUTER_EVENTS_COUNT];
  struct OpponentRelation opponent_relations[PLAYERS_COUNT];
  // TODO we could use coord2d for trap locations
  struct Coord3d trap_locations[COMPUTER_TRAP_LOC_COUNT];
  /** Stores Sight Of Evil target points data. */
  uint64_t soe_targets[COMPUTER_SOE_GRID_SIZE];
  int64_t ongoing_process;
  int64_t task_idx;
  int64_t held_thing_idx;
};

/**
 * Contains value of hate between players.
 */
struct THate {
    int64_t amount;
    int64_t plyr_idx;
    struct Coord3d *pos_near;
    int64_t distance_near;
};

struct ExpandRooms {
    RoomKind rkind;
    int64_t max_slabs;
};

/******************************************************************************/

#pragma pack()

/******************************************************************************/
/* The gold lookup (player_complookup.c, kfx_ai), embedded in kfx_sim_state. */
#define GOLD_LOOKUP_COUNT      40
#pragma pack(1)

struct GoldLookup { // sizeof = 28
    unsigned char flags;
    /* Informs whether players are interested in that gold vein. */
    unsigned char player_interested[PLAYERS_COUNT];
MapSubtlCoord stl_x;
MapSubtlCoord stl_y;
int64_t num_gold_slabs;
uint64_t num_gem_slabs;
};

#pragma pack()
/******************************************************************************/
/* Accessors kfx_sim needs itself (player_computer_state.c). */
struct Thing;
struct Dungeon;
struct Computer2 *get_computer_player_f(int64_t plyr_idx,const char *func_name);
#define get_computer_player(plyr_idx) get_computer_player_f(plyr_idx,__func__)
TbBool computer_player_invalid(const struct Computer2 *comp);
struct Dungeon *computer_dungeon(const struct Computer2 *comp);
TbBool thing_is_in_computer_power_hand_list(const struct Thing *thing, PlayerNumber plyr_idx);
int64_t find_trap_location_index(const struct Computer2 * comp, const struct Coord3d * coord);
int64_t add_to_trap_locations(struct Computer2 *, struct Coord3d *);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
