/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file kfx_net_state.h
 *     Header file for kfx_net_state.c.
 * @par Purpose:
 *     Holds struct Game's kfx_net-owned field group, migrated out of
 *     game_legacy.h per docs/refactor/stage-08-kfx-net.md (mirrors
 *     stage 6.7/7.2's kfx_sim_state.h approach). `struct Game` is
 *     raw-serialized wholesale (see src/net_resync.cpp, src/game_saves.c,
 *     src/main_game.c's clear_complete_game()); this struct is synced the
 *     same way, alongside kfx_sim_state, at all three call sites.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/

#ifndef DK_KFX_NET_STATE_H
#define DK_KFX_NET_STATE_H

#include "bflib_basics.h"
#include "globals.h"
#include "thing_list.h"
#include "player_data.h"
#include "room_data.h"
#include "dungeon_data.h" // DUNGEONS_COUNT
#include "tasks_list.h" // struct MapTask, MAPTASKS_COUNT
#include "packets.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

// Per-thing/player/room desync-debug snapshot rows and the checksums
// used to compare host vs. client world state -- only used by
// net_checksums.c, moved here verbatim from game_legacy.h.
struct LogThingDesyncInfo {
    ThingIndex index;
    ThingClass class_id;
    ThingModel model;
    PlayerNumber owner;
    struct Coord3d mappos;
    HitPoints health;
    GameTurn creation_turn;
    uint32_t random_seed;
    int64_t anim_sprite;
    int64_t anim_speed;
    int64_t anim_time;
    unsigned char current_frame;
    unsigned char max_frames;
    unsigned char active_state;
    unsigned char continue_state;
    int64_t movement_flags;
    int64_t move_angle_xy;
    int64_t move_angle_z;
    PlayerNumber holding_player;
    int64_t parent_idx;
    unsigned char fall_acceleration;
    struct CoordDelta3d veloc_base;
    struct CoordDelta3d veloc_push_once;
    struct CoordDelta3d veloc_push_add;
    TbBool is_special_digger;
    struct Coord3d digger_moveto_pos;
    int64_t digger_dragtng_idx;
    ThingIndex digger_arming_thing_id;
    ThingIndex digger_pickup_object_id;
    ThingIndex digger_pickup_creature_id;
    unsigned char digger_move_flags;
    int64_t digger_stack_update_turn;
    SubtlCodedCoords digger_working_stl;
    SubtlCodedCoords digger_task_stl;
    int64_t digger_task_idx;
    unsigned char digger_consecutive_reinforcements;
    unsigned char digger_last_did_job;
    unsigned char digger_task_stack_pos;
    int64_t digger_task_repeats;
    TbBigChecksum checksum;
};

struct LogPlayerDesyncInfo {
    PlayerNumber id;
    unsigned char instance_num;
    uint64_t instance_remain_turns;
    struct Coord3d mappos;
    TbBigChecksum checksum;
};

struct LogRoomDesyncInfo {
    RoomIndex index;
    int64_t slabs_count;
    MapSubtlCoord central_stl_x;
    MapSubtlCoord central_stl_y;
    int64_t efficiency;
    int64_t used_capacity;
    TbBigChecksum checksum;
};

struct DesyncChecksums {
    TbBigChecksum creatures;
    TbBigChecksum traps;
    TbBigChecksum shots;
    TbBigChecksum objects;
    TbBigChecksum effects;
    TbBigChecksum dead_creatures;
    TbBigChecksum effect_gens;
    TbBigChecksum doors;
    TbBigChecksum rooms;
    TbBigChecksum players;
    TbBigChecksum dig_tasks;
    TbBigChecksum action_seed;
    TbBigChecksum ai_seed;
    TbBigChecksum player_seed;
    GameTurn game_turn;
};

struct LogDetailedSnapshot {
    struct LogThingDesyncInfo things[SYNCED_THINGS_COUNT];
    int64_t thing_count;
    struct LogPlayerDesyncInfo players[PLAYERS_COUNT];
    int64_t player_count;
    struct LogRoomDesyncInfo rooms[ROOMS_COUNT];
    int64_t room_count;
    struct MapTask dig_tasks[DUNGEONS_COUNT][MAPTASKS_COUNT];
    int64_t dig_task_counts[DUNGEONS_COUNT];
};

#pragma pack(1)
struct KfxNetState {
    // Packet save/replay file state (packets.c/packets_misc.c).
    unsigned char packet_save_enable;
    char packet_fname[150];
    char packet_fopened;
    uint64_t packet_file_pos;
    struct PacketSaveHead packet_save_head;
    uint64_t turns_stored;
    uint64_t turns_fastforward;
    unsigned char packet_loading_in_progress;
    unsigned char packet_checksum_verify;
    uint64_t log_things_start_turn;
    uint64_t log_things_end_turn;
    uint64_t turns_packetoff;
    // packet_load_enable (now replay_active), local_plyr_idx (now
    // level_human_player) and human_players_count moved to kfx_sim_state
    // (refactor pass 2, S10).

    // Per-turn input packets moved to kfx_sim's sim_packets[] (packet_data.h,
    // docs/refactor/todo/remove-symbol-level-layering-residuals.md) --
    // this file's own packets.c/packets_misc.c/net_exchange_gameplay.c
    // still write into it directly, just no longer as a field here.
    int64_t input_lag_turns;

    // Desync detection (net_checksums.c).
    struct DesyncChecksums host_checksums;
    struct LogDetailedSnapshot log_snapshot;

    /* Moved from struct Game (stage 13, docs/refactor/
       stage-13-enforce-and-document.md) -- both read by kfx_apploop/
       kfx_frontend/kfx_game too, but kfx_net is the lowest-ranked of
       their consumer sets. */
    double process_turn_time;
    int64_t skip_initial_input_turns;

    /* Moved from struct Game (stage 13, docs/refactor/
       stage-13-enforce-and-document.md) -- also read by kfx_platform's
       bflib_sndlib.cpp, which gets pointer access via
       SoundHostPort instead (kfx_platform is the lowest-ranked
       library, can't reach kfx_net_state directly). */
    int64_t frame_skip;

    /* Moved from kfx_frontend_state (stage 13.2, docs/refactor/
       stage-13-enforce-and-document.md) -- read/written by net_resync.cpp
       (full-resync sync), the lowest-ranked of the two real consumers
       (kfx_frontend also reads/writes them for the AI-tendency buttons). */
    char comp_player_aggressive;
    char comp_player_defensive;
    char comp_player_construct;
    char comp_player_creatrsonly;

    /* Moved from kfx_game's kfx_game_state.h (stage 13.4, docs/refactor/
       stage-13-enforce-and-document.md) -- written by kfx_net's
       packets_misc.c, the lowest-ranked of its real consumers (kfx_game's
       main_game.c and kfx_frontend's front_input.c also read it). */
    GameTurn pckt_gameturn;
};
#pragma pack()
/******************************************************************************/
extern struct KfxNetState kfx_net_state;

/** Process-local state, NOT part of the saved/resynced KfxNetState blob: a FILE* is meaningless outside the
 *  process that opened it (a savegame or a multiplayer host would overwrite ours with a dead handle). */
struct KfxNetLocal {
    TbFileHandle packet_save_fp;
};
extern struct KfxNetLocal kfx_net_local;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
