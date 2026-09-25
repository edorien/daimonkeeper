/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file net_checksums.c
 *     Network checksum computation and desync analysis for multiplayer games.
 * @par Purpose:
 *     Computes checksums for multiplayer sync and stores history for debugging.
 * @par Comment:
 *     Uses a circular buffer to store the last N turns of checksum data.
 * @author   KeeperFX Team
 * @date     03 Nov 2025
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "net_checksums.h"
#include "bflib_dernc.h"
#include "config.h"
#include "net_game.h"
#include "packets.h"
#include "player_data.h"
#include "thing_data.h"
#include "room_list.h"
#include "slab_data.h"
#include "creature_control.h"
#include "thing_creature.h"
#include "thing_list.h"
#include "kfx_net_state.h"
#include "kfx_sim_state.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
#define CHECKSUM_ADD(checksum, value) checksum = ((checksum << 5) | (checksum >> 27)) ^ (uint64_t)(value)
#define SNAPSHOT_BUFFER_SIZE 15

struct ChecksumSnapshot {
    GameTurn turn;
    TbBool valid;
    struct DesyncChecksums checksums;
    struct LogDetailedSnapshot log_details;
};

static struct ChecksumSnapshot snapshot_buffer[SNAPSHOT_BUFFER_SIZE];
static int64_t snapshot_head = 0;
static GameTurn desync_turn = 0;

TbBigChecksum get_thing_checksum(const struct Thing* thing) {
    if (!thing_exists(thing) || is_non_synchronized_thing_class(thing->class_id)) {
        return 0;
    }
    TbBigChecksum checksum = 0;
    CHECKSUM_ADD(checksum, thing->index);
    CHECKSUM_ADD(checksum, thing->class_id);
    CHECKSUM_ADD(checksum, thing->model);
    CHECKSUM_ADD(checksum, thing->owner);
    CHECKSUM_ADD(checksum, thing->creation_turn);
    CHECKSUM_ADD(checksum, thing->random_seed);
    CHECKSUM_ADD(checksum, thing->mappos.x.val);
    CHECKSUM_ADD(checksum, thing->mappos.y.val);
    CHECKSUM_ADD(checksum, thing->mappos.z.val);
    CHECKSUM_ADD(checksum, thing->health);
    CHECKSUM_ADD(checksum, thing->anim_sprite);
    CHECKSUM_ADD(checksum, thing->anim_speed);
    CHECKSUM_ADD(checksum, thing->anim_time);
    CHECKSUM_ADD(checksum, thing->current_frame);
    CHECKSUM_ADD(checksum, thing->max_frames);
    CHECKSUM_ADD(checksum, thing->active_state);
    CHECKSUM_ADD(checksum, thing->continue_state);
    CHECKSUM_ADD(checksum, thing->movement_flags);
    CHECKSUM_ADD(checksum, thing->move_angle_xy);
    CHECKSUM_ADD(checksum, thing->move_angle_z);
    CHECKSUM_ADD(checksum, thing->holding_player);
    CHECKSUM_ADD(checksum, thing->parent_idx);
    CHECKSUM_ADD(checksum, thing->fall_acceleration);
    CHECKSUM_ADD(checksum, thing->veloc_base.x.val);
    CHECKSUM_ADD(checksum, thing->veloc_base.y.val);
    CHECKSUM_ADD(checksum, thing->veloc_base.z.val);
    CHECKSUM_ADD(checksum, thing->veloc_push_once.x.val);
    CHECKSUM_ADD(checksum, thing->veloc_push_once.y.val);
    CHECKSUM_ADD(checksum, thing->veloc_push_once.z.val);
    CHECKSUM_ADD(checksum, thing->veloc_push_add.x.val);
    CHECKSUM_ADD(checksum, thing->veloc_push_add.y.val);
    CHECKSUM_ADD(checksum, thing->veloc_push_add.z.val);
    if (thing_is_creature_special_digger(thing)) {
        struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
        CHECKSUM_ADD(checksum, cctrl->moveto_pos.x.val);
        CHECKSUM_ADD(checksum, cctrl->moveto_pos.y.val);
        CHECKSUM_ADD(checksum, cctrl->moveto_pos.z.val);
        CHECKSUM_ADD(checksum, cctrl->dragtng_idx);
        CHECKSUM_ADD(checksum, cctrl->arming_thing_id);
        CHECKSUM_ADD(checksum, cctrl->pickup_object_id);
        CHECKSUM_ADD(checksum, cctrl->pickup_creature_id);
        CHECKSUM_ADD(checksum, cctrl->move_flags);
        CHECKSUM_ADD(checksum, cctrl->digger.stack_update_turn);
        CHECKSUM_ADD(checksum, cctrl->digger.working_stl);
        CHECKSUM_ADD(checksum, cctrl->digger.task_stl);
        CHECKSUM_ADD(checksum, cctrl->digger.consecutive_reinforcements);
        CHECKSUM_ADD(checksum, cctrl->digger.last_did_job);
        CHECKSUM_ADD(checksum, cctrl->digger.task_stack_pos);
        CHECKSUM_ADD(checksum, cctrl->digger.task_repeats);
    }
    return checksum;
}

static TbBigChecksum compute_player_checksum(struct PlayerInfo *player) {
    struct Camera* camera = get_player_active_camera(player);
    if ((player->allocflags & PlaF_CompCtrl) != 0 || camera == NULL) {
        return 0;
    }
    TbBigChecksum checksum = 0;
    CHECKSUM_ADD(checksum, player->instance_remain_turns);
    CHECKSUM_ADD(checksum, player->instance_num);
    if (player->victory_state == VicS_Undecided) {
        CHECKSUM_ADD(checksum, camera->mappos.x.val);
        CHECKSUM_ADD(checksum, camera->mappos.y.val);
        CHECKSUM_ADD(checksum, camera->mappos.z.val);
    }
    return checksum;
}

static TbBigChecksum get_room_checksum(const struct Room* room) {
    TbBigChecksum checksum = 0;
    CHECKSUM_ADD(checksum, room->slabs_count);
    CHECKSUM_ADD(checksum, room->central_stl_x);
    CHECKSUM_ADD(checksum, room->central_stl_y);
    CHECKSUM_ADD(checksum, room->efficiency);
    CHECKSUM_ADD(checksum, room->used_capacity);
    CHECKSUM_ADD(checksum, room->index);
    return checksum;
}

static TbBigChecksum compute_things_list_checksum(struct StructureList *list) {
    TbBigChecksum sum = 0;
    uint64_t k = 0;
    int64_t i = list->index;
    while (i != 0) {
        struct Thing* thing = thing_get(i);
        if (thing_is_invalid(thing)) {
            ERRORLOG("Jump to invalid thing detected in list");
            break;
        }
        i = thing->next_of_class;
        sum += get_thing_checksum(thing);
        k++;
        if (k > THINGS_COUNT) {
            ERRORLOG("Infinite loop detected in thing list");
            break;
        }
    }
    return sum;
}

static void compute_checksums(struct DesyncChecksums* checksums) {
    checksums->creatures = compute_things_list_checksum(&kfx_sim_state.thing_lists[TngList_Creatures]);
    checksums->traps = compute_things_list_checksum(&kfx_sim_state.thing_lists[TngList_Traps]);
    checksums->shots = compute_things_list_checksum(&kfx_sim_state.thing_lists[TngList_Shots]);
    checksums->objects = compute_things_list_checksum(&kfx_sim_state.thing_lists[TngList_Objects]);
    checksums->effects = compute_things_list_checksum(&kfx_sim_state.thing_lists[TngList_Effects]);
    checksums->dead_creatures = compute_things_list_checksum(&kfx_sim_state.thing_lists[TngList_DeadCreatrs]);
    checksums->effect_gens = compute_things_list_checksum(&kfx_sim_state.thing_lists[TngList_EffectGens]);
    checksums->doors = compute_things_list_checksum(&kfx_sim_state.thing_lists[TngList_Doors]);
    checksums->rooms = 0;
    for (struct Room* room = start_rooms; room < end_rooms; room++) {
        if (room_exists(room)) {
            CHECKSUM_ADD(checksums->rooms, get_room_checksum(room));
        }
    }
    checksums->players = 0;
    for (int64_t i = 0; i < PLAYERS_COUNT; i++) {
        struct PlayerInfo* player = get_player(i);
        if (player_exists(player)) {
            checksums->players += compute_player_checksum(player);
        }
    }
    checksums->dig_tasks = 0;
    for (int64_t i = 0; i < DUNGEONS_COUNT; i++) {
        struct MapTask* task = get_dungeon(i)->task_list;
        for (int64_t t = 0; t < MAPTASKS_COUNT; t++) {
            CHECKSUM_ADD(checksums->dig_tasks, task[t].kind);
            CHECKSUM_ADD(checksums->dig_tasks, task[t].coords);
        }
    }
    checksums->action_seed = kfx_sim_state.action_random_seed;
    checksums->ai_seed = kfx_sim_state.ai_random_seed;
    checksums->player_seed = kfx_sim_state.player_random_seed;
    checksums->game_turn = get_gameturn();
}

static struct ChecksumSnapshot* find_snapshot(GameTurn turn) {
    for (int64_t i = 0; i < SNAPSHOT_BUFFER_SIZE; i++) {
        if (snapshot_buffer[i].valid && snapshot_buffer[i].turn == turn) {
            return &snapshot_buffer[i];
        }
    }
    return NULL;
}

int64_t checksums_different(void)
{
    const NetUserId host_user_id = SERVER_ID;
    struct Packet* host_packet = get_packet(host_user_id);
    TbBigChecksum host_checksum = host_packet->checksum;
    TbBool mismatch = false;
    TbBool already_desynced = (kfx_sim_state.system_flags & GSF_NetGameNoSync) != 0;

    for (NetUserId i = 0; i < MAX_NET_USERS; i++) {
        if (i == host_user_id) {
            continue;
        }
        if (!user_present(i)) {
            continue;
        }
        struct PlayerInfo* player = get_player(get_net_user_player_number(i));
        if (!player_exists(player)) {
            continue;
        }
        if ((player->allocflags & PlaF_CompCtrl) != 0) {
            continue;
        }
        struct Packet* packet = get_packet(i);
        // no need to validate checksm for users who are dropping
        // (and the host sometimes emits checksumless packets for such users)
        if ((packet->action == PckA_QuitToMainMenu) || (packet->action == PckA_ForceApplicationClose)) {
            continue;
        }
        if (is_packet_empty(packet)) {
            if (!already_desynced) {
                ERRORLOG("Missing checksum packet for user %" PRId64 "; host turn: %" PRId64, (int64_t)(i), (int64_t)(host_packet->turn));
            }
            desync_turn = host_packet->turn;
            mismatch = true;
            continue;
        }
        if (packet->checksum != host_checksum) {
            if (!already_desynced) {
                ERRORLOG("Checksums %08" PRIx64 "(Host) != %08" PRIx64 "(Client) turn: %" PRId64 " vs %" PRId64, (uint64_t)(host_checksum), (uint64_t)(packet->checksum), (int64_t)(host_packet->turn), (int64_t)(packet->turn));
            }
            desync_turn = host_packet->turn;
            mismatch = true;
        }
    }
    return mismatch;
}

void calculate_network_startup_map_checksums(TbBigChecksum checksums[NETWORK_STARTUP_MAP_FILE_COUNT])
{
    LevelNumber lvnum = get_loaded_level_number();
    int64_t fgroup = get_level_fgroup(lvnum);
    for (int64_t i = 0; i < NETWORK_STARTUP_MAP_FILE_COUNT; i++) {
        char* fname = prepare_file_fmtpath(fgroup, "map%05" PRIu64 ".%s", (uint64_t)(lvnum), network_startup_compare_files[i]);
        checksums[i] = calculate_file_checksum(fname);
    }
}

void update_turn_checksums(void) {
    struct ChecksumSnapshot* snapshot = &snapshot_buffer[snapshot_head];
    struct LogDetailedSnapshot* snapshot_info = &snapshot->log_details;
    snapshot->turn = get_gameturn();
    snapshot->valid = true;
    compute_checksums(&snapshot->checksums);
    snapshot_info->thing_count = 0;
    snapshot_info->player_count = 0;
    snapshot_info->room_count = 0;
    memset(snapshot_info->dig_task_counts, 0, sizeof(snapshot_info->dig_task_counts));
    if (network_is_active()) {
        for (int64_t i = 1; i < SYNCED_THINGS_COUNT; i++) {
            struct Thing* thing = thing_get(i);
            if (!thing_exists(thing) || is_non_synchronized_thing_class(thing->class_id)) {
                continue;
            }
            struct LogThingDesyncInfo* thing_snapshot = &snapshot_info->things[snapshot_info->thing_count++];
            thing_snapshot->index = thing->index;
            thing_snapshot->class_id = thing->class_id;
            thing_snapshot->model = thing->model;
            thing_snapshot->owner = thing->owner;
            thing_snapshot->mappos = thing->mappos;
            thing_snapshot->health = thing->health;
            thing_snapshot->creation_turn = thing->creation_turn;
            thing_snapshot->random_seed = thing->random_seed;
            thing_snapshot->anim_sprite = thing->anim_sprite;
            thing_snapshot->anim_speed = thing->anim_speed;
            thing_snapshot->anim_time = thing->anim_time;
            thing_snapshot->current_frame = thing->current_frame;
            thing_snapshot->max_frames = thing->max_frames;
            thing_snapshot->active_state = thing->active_state;
            thing_snapshot->continue_state = thing->continue_state;
            thing_snapshot->movement_flags = thing->movement_flags;
            thing_snapshot->move_angle_xy = thing->move_angle_xy;
            thing_snapshot->move_angle_z = thing->move_angle_z;
            thing_snapshot->holding_player = thing->holding_player;
            thing_snapshot->parent_idx = thing->parent_idx;
            thing_snapshot->fall_acceleration = thing->fall_acceleration;
            thing_snapshot->veloc_base = thing->veloc_base;
            thing_snapshot->veloc_push_once = thing->veloc_push_once;
            thing_snapshot->veloc_push_add = thing->veloc_push_add;
            thing_snapshot->is_special_digger = thing_is_creature_special_digger(thing);
            if (thing_snapshot->is_special_digger) {
                struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
                thing_snapshot->digger_moveto_pos = cctrl->moveto_pos;
                thing_snapshot->digger_dragtng_idx = cctrl->dragtng_idx;
                thing_snapshot->digger_arming_thing_id = cctrl->arming_thing_id;
                thing_snapshot->digger_pickup_object_id = cctrl->pickup_object_id;
                thing_snapshot->digger_pickup_creature_id = cctrl->pickup_creature_id;
                thing_snapshot->digger_move_flags = cctrl->move_flags;
                thing_snapshot->digger_stack_update_turn = cctrl->digger.stack_update_turn;
                thing_snapshot->digger_working_stl = cctrl->digger.working_stl;
                thing_snapshot->digger_task_stl = cctrl->digger.task_stl;
                thing_snapshot->digger_task_idx = cctrl->digger.task_idx;
                thing_snapshot->digger_consecutive_reinforcements = cctrl->digger.consecutive_reinforcements;
                thing_snapshot->digger_last_did_job = cctrl->digger.last_did_job;
                thing_snapshot->digger_task_stack_pos = cctrl->digger.task_stack_pos;
                thing_snapshot->digger_task_repeats = cctrl->digger.task_repeats;
            }
            thing_snapshot->checksum = get_thing_checksum(thing);
        }
        for (int64_t i = 0; i < PLAYERS_COUNT; i++) {
            struct PlayerInfo* player = get_player(i);
            struct Camera* camera = get_player_active_camera(player);
            if (!player_exists(player) || ((player->allocflags & PlaF_CompCtrl) != 0) || camera == NULL) {
                continue;
            }
            struct LogPlayerDesyncInfo* player_snapshot = &snapshot_info->players[snapshot_info->player_count++];
            player_snapshot->id = i;
            player_snapshot->instance_num = player->instance_num;
            player_snapshot->instance_remain_turns = player->instance_remain_turns;
            if (player->victory_state == VicS_Undecided) {
                player_snapshot->mappos = camera->mappos;
            } else {
                memset(&player_snapshot->mappos, 0, sizeof(player_snapshot->mappos));
            }
            player_snapshot->checksum = compute_player_checksum(player);
        }
        for (struct Room* room = start_rooms; room < end_rooms; room++) {
            if (!room_exists(room)) {
                continue;
            }
            struct LogRoomDesyncInfo* room_snapshot = &snapshot_info->rooms[snapshot_info->room_count++];
            room_snapshot->index = room->index;
            room_snapshot->slabs_count = room->slabs_count;
            room_snapshot->central_stl_x = room->central_stl_x;
            room_snapshot->central_stl_y = room->central_stl_y;
            room_snapshot->efficiency = room->efficiency;
            room_snapshot->used_capacity = room->used_capacity;
            room_snapshot->checksum = get_room_checksum(room);
        }
        for (int64_t i = 0; i < DUNGEONS_COUNT; i++) {
            struct Dungeon* dungeon = get_dungeon(i);
            snapshot_info->dig_task_counts[i] = dungeon->task_count;
            memcpy(snapshot_info->dig_tasks[i], dungeon->task_list, sizeof(dungeon->task_list));
        }
    }
    snapshot_head = (snapshot_head + 1) % SNAPSHOT_BUFFER_SIZE;

    struct DesyncChecksums* checksums = &snapshot->checksums;
    TbBigChecksum things_sum = 0;
    things_sum += checksums->creatures;
    things_sum += checksums->traps;
    things_sum += checksums->shots;
    things_sum += checksums->objects;
    things_sum += checksums->effects;
    things_sum += checksums->dead_creatures;
    things_sum += checksums->effect_gens;
    things_sum += checksums->doors;

    struct Packet* packet = get_local_packet();
    packet->checksum = 0;
    packet->checksum += things_sum;
    packet->checksum += checksums->rooms;
    packet->checksum += checksums->players;
    packet->checksum += checksums->dig_tasks;
    packet->checksum += checksums->action_seed;
    packet->checksum += checksums->player_seed;
    packet->checksum += checksums->ai_seed;

    MULTIPLAYER_LOG("update_turn_checksums: turn=%" PRIu64 " checksum=%08" PRIx64 " things=%08" PRIx64 " rooms=%08" PRIx64 " players=%08" PRIx64, (uint64_t)get_gameturn(), (uint64_t)packet->checksum, (uint64_t)things_sum, (uint64_t)checksums->rooms, (uint64_t)checksums->players);
}

void pack_desync_history_for_resync(void) {
    struct ChecksumSnapshot* snapshot = find_snapshot(desync_turn);
    if (snapshot == NULL) {
        compute_checksums(&kfx_net_state.host_checksums);
        kfx_net_state.log_snapshot.thing_count = 0;
        kfx_net_state.log_snapshot.player_count = 0;
        kfx_net_state.log_snapshot.room_count = 0;
        memset(kfx_net_state.log_snapshot.dig_tasks, 0, sizeof(kfx_net_state.log_snapshot.dig_tasks));
        memset(kfx_net_state.log_snapshot.dig_task_counts, 0, sizeof(kfx_net_state.log_snapshot.dig_task_counts));
        return;
    }
    kfx_net_state.host_checksums = snapshot->checksums;
    kfx_net_state.log_snapshot = snapshot->log_details;
}

static TbBool log_checksum_mismatch(const char* name, TbBigChecksum client_sum, TbBigChecksum host_sum)
{
    if (client_sum == host_sum) {
        return false;
    }
    ERRORLOG("  %s MISMATCH - Host: %08" PRIx64 ", Client: %08" PRIx64, name, (uint64_t)host_sum, (uint64_t)client_sum);
    return true;
}

static void log_dig_task_differences(const struct LogDetailedSnapshot* client)
{
    const struct LogDetailedSnapshot* host = &kfx_net_state.log_snapshot;
    int64_t shown = 0;
    for (int64_t plyr_idx = 0; plyr_idx < DUNGEONS_COUNT && shown < 10; plyr_idx++) {
        if (memcmp(host->dig_tasks[plyr_idx], client->dig_tasks[plyr_idx], sizeof(host->dig_tasks[plyr_idx])) == 0) {
            continue;
        }
        ERRORLOG("    Player[%" PRId64 "] dig tags: Host count=%" PRIu64 ", Client count=%" PRIu64, (int64_t)(plyr_idx), (uint64_t)host->dig_task_counts[plyr_idx], (uint64_t)client->dig_task_counts[plyr_idx]);
        shown++;
        for (int64_t i = 0; (i < MAPTASKS_COUNT) && (shown < 10); i++) {
            const struct MapTask* host_task = &host->dig_tasks[plyr_idx][i];
            const struct MapTask* client_task = &client->dig_tasks[plyr_idx][i];
            if ((host_task->kind == client_task->kind) && (host_task->coords == client_task->coords)) {
                continue;
            }
            ERRORLOG("    Player[%" PRId64 "] dig tag[%" PRId64 "]: Host kind=%" PRIu64 " stl=(%" PRIu64 ",%" PRIu64 "), Client kind=%" PRIu64 " stl=(%" PRIu64 ",%" PRIu64 ")", (int64_t)(plyr_idx), (int64_t)(i),
                (uint64_t)host_task->kind, (uint64_t)stl_num_decode_x(host_task->coords), (uint64_t)stl_num_decode_y(host_task->coords),
                (uint64_t)client_task->kind, (uint64_t)stl_num_decode_x(client_task->coords), (uint64_t)stl_num_decode_y(client_task->coords));
            shown++;
        }
    }
}

static void log_thing_differences(struct LogDetailedSnapshot* client, const char* name, TbBigChecksum client_sum, TbBigChecksum host_sum, ThingClass filter_class) {
    if (!log_checksum_mismatch(name, client_sum, host_sum)) {
        return;
    }
    struct LogDetailedSnapshot* host = &kfx_net_state.log_snapshot;
    int64_t shown = 0;
    for (int64_t s = 0; s < client->thing_count && shown < 10; s++) {
        struct LogThingDesyncInfo* client_thing = &client->things[s];
        if (client_thing->class_id != filter_class) {
            continue;
        }
        struct LogThingDesyncInfo* host_thing = NULL;
        for (int64_t h = 0; h < host->thing_count; h++) {
            if (host->things[h].index == client_thing->index) {
                host_thing = &host->things[h];
                break;
            }
        }
        if (host_thing == NULL || client_thing->checksum != host_thing->checksum) {
            if (host_thing != NULL) {
                ERRORLOG("    [Host] Thing[%" PRId64 "] class_id=%" PRId64 " model=%" PRId64 " owner=%" PRId64 " mappos=(%" PRId64 ",%" PRId64 ",%" PRId64 ") health=%" PRId64 " creation_turn=%" PRIu64 " random_seed=%08" PRIx64 " anim_sprite=%" PRIu64 " anim_speed=%" PRId64 " anim_time=%" PRId64 " current_frame=%" PRIu64 " max_frames=%" PRIu64 " active_state=%" PRIu64 " continue_state=%" PRIu64 " movement_flags=%04" PRIx64 " move_angle_xy=%" PRId64 " move_angle_z=%" PRId64 " holding_player=%" PRId64 " parent_idx=%" PRId64 " fall_acceleration=%" PRIu64 " veloc_base=(%" PRId64 ",%" PRId64 ",%" PRId64 ") veloc_push_once=(%" PRId64 ",%" PRId64 ",%" PRId64 ") veloc_push_add=(%" PRId64 ",%" PRId64 ",%" PRId64 ")",
                    (int64_t)(host_thing->index), (int64_t)(host_thing->class_id), (int64_t)(host_thing->model), (int64_t)(host_thing->owner),
                    (int64_t)host_thing->mappos.x.val, (int64_t)host_thing->mappos.y.val, (int64_t)host_thing->mappos.z.val,
                    (int64_t)host_thing->health, (uint64_t)host_thing->creation_turn, (uint64_t)(host_thing->random_seed),
                    (uint64_t)host_thing->anim_sprite, (int64_t)host_thing->anim_speed, (int64_t)host_thing->anim_time,
                    (uint64_t)host_thing->current_frame, (uint64_t)host_thing->max_frames,
                    (uint64_t)host_thing->active_state, (uint64_t)host_thing->continue_state,
                    (uint64_t)host_thing->movement_flags,
                    (int64_t)host_thing->move_angle_xy, (int64_t)host_thing->move_angle_z,
                    (int64_t)host_thing->holding_player, (int64_t)host_thing->parent_idx, (uint64_t)host_thing->fall_acceleration,
                    (int64_t)host_thing->veloc_base.x.val, (int64_t)host_thing->veloc_base.y.val, (int64_t)host_thing->veloc_base.z.val,
                    (int64_t)host_thing->veloc_push_once.x.val, (int64_t)host_thing->veloc_push_once.y.val, (int64_t)host_thing->veloc_push_once.z.val,
                    (int64_t)host_thing->veloc_push_add.x.val, (int64_t)host_thing->veloc_push_add.y.val, (int64_t)host_thing->veloc_push_add.z.val);
            } else {
                ERRORLOG("    [Host] Thing[%" PRId64 "] missing", (int64_t)(client_thing->index));
            }
            ERRORLOG("    [Client] Thing[%" PRId64 "] class_id=%" PRId64 " model=%" PRId64 " owner=%" PRId64 " mappos=(%" PRId64 ",%" PRId64 ",%" PRId64 ") health=%" PRId64 " creation_turn=%" PRIu64 " random_seed=%08" PRIx64 " anim_sprite=%" PRIu64 " anim_speed=%" PRId64 " anim_time=%" PRId64 " current_frame=%" PRIu64 " max_frames=%" PRIu64 " active_state=%" PRIu64 " continue_state=%" PRIu64 " movement_flags=%04" PRIx64 " move_angle_xy=%" PRId64 " move_angle_z=%" PRId64 " holding_player=%" PRId64 " parent_idx=%" PRId64 " fall_acceleration=%" PRIu64 " veloc_base=(%" PRId64 ",%" PRId64 ",%" PRId64 ") veloc_push_once=(%" PRId64 ",%" PRId64 ",%" PRId64 ") veloc_push_add=(%" PRId64 ",%" PRId64 ",%" PRId64 ")",
                (int64_t)(client_thing->index), (int64_t)(client_thing->class_id), (int64_t)(client_thing->model), (int64_t)(client_thing->owner),
                (int64_t)client_thing->mappos.x.val, (int64_t)client_thing->mappos.y.val, (int64_t)client_thing->mappos.z.val,
                (int64_t)client_thing->health, (uint64_t)client_thing->creation_turn, (uint64_t)(client_thing->random_seed),
                (uint64_t)client_thing->anim_sprite, (int64_t)client_thing->anim_speed, (int64_t)client_thing->anim_time,
                (uint64_t)client_thing->current_frame, (uint64_t)client_thing->max_frames,
                (uint64_t)client_thing->active_state, (uint64_t)client_thing->continue_state,
                (uint64_t)client_thing->movement_flags,
                (int64_t)client_thing->move_angle_xy, (int64_t)client_thing->move_angle_z,
                (int64_t)client_thing->holding_player, (int64_t)client_thing->parent_idx, (uint64_t)client_thing->fall_acceleration,
                (int64_t)client_thing->veloc_base.x.val, (int64_t)client_thing->veloc_base.y.val, (int64_t)client_thing->veloc_base.z.val,
                (int64_t)client_thing->veloc_push_once.x.val, (int64_t)client_thing->veloc_push_once.y.val, (int64_t)client_thing->veloc_push_once.z.val,
                (int64_t)client_thing->veloc_push_add.x.val, (int64_t)client_thing->veloc_push_add.y.val, (int64_t)client_thing->veloc_push_add.z.val);
            if (client_thing->is_special_digger) {
                if (host_thing != NULL) {
                    ERRORLOG("    [Host]   digger moveto=(%" PRId64 ",%" PRId64 ",%" PRId64 ") dragtng=%" PRId64 " arming=%" PRId64 " pickup_obj=%" PRId64 " pickup_cr=%" PRId64 " move_flags=%" PRIu64 " stack_update_turn=%" PRId64 " working_stl=%" PRIu64 " task_stl=%" PRIu64 " task_idx=%" PRIu64 " consecutive_reinforcements=%" PRIu64 " last_did_job=%" PRIu64 " task_stack_pos=%" PRIu64 " task_repeats=%" PRIu64,
                        (int64_t)host_thing->digger_moveto_pos.x.val, (int64_t)host_thing->digger_moveto_pos.y.val, (int64_t)host_thing->digger_moveto_pos.z.val,
                        (int64_t)host_thing->digger_dragtng_idx, (int64_t)host_thing->digger_arming_thing_id,
                        (int64_t)host_thing->digger_pickup_object_id, (int64_t)host_thing->digger_pickup_creature_id,
                        (uint64_t)host_thing->digger_move_flags, (int64_t)host_thing->digger_stack_update_turn,
                        (uint64_t)host_thing->digger_working_stl, (uint64_t)host_thing->digger_task_stl,
                        (uint64_t)host_thing->digger_task_idx, (uint64_t)host_thing->digger_consecutive_reinforcements,
                        (uint64_t)host_thing->digger_last_did_job,
                        (uint64_t)host_thing->digger_task_stack_pos, (uint64_t)host_thing->digger_task_repeats);
                }
                ERRORLOG("    [Client] digger moveto=(%" PRId64 ",%" PRId64 ",%" PRId64 ") dragtng=%" PRId64 " arming=%" PRId64 " pickup_obj=%" PRId64 " pickup_cr=%" PRId64 " move_flags=%" PRIu64 " stack_update_turn=%" PRId64 " working_stl=%" PRIu64 " task_stl=%" PRIu64 " task_idx=%" PRIu64 " consecutive_reinforcements=%" PRIu64 " last_did_job=%" PRIu64 " task_stack_pos=%" PRIu64 " task_repeats=%" PRIu64,
                    (int64_t)client_thing->digger_moveto_pos.x.val, (int64_t)client_thing->digger_moveto_pos.y.val, (int64_t)client_thing->digger_moveto_pos.z.val,
                    (int64_t)client_thing->digger_dragtng_idx, (int64_t)client_thing->digger_arming_thing_id,
                    (int64_t)client_thing->digger_pickup_object_id, (int64_t)client_thing->digger_pickup_creature_id,
                    (uint64_t)client_thing->digger_move_flags, (int64_t)client_thing->digger_stack_update_turn,
                    (uint64_t)client_thing->digger_working_stl, (uint64_t)client_thing->digger_task_stl,
                    (uint64_t)client_thing->digger_task_idx, (uint64_t)client_thing->digger_consecutive_reinforcements,
                    (uint64_t)client_thing->digger_last_did_job,
                    (uint64_t)client_thing->digger_task_stack_pos, (uint64_t)client_thing->digger_task_repeats);
            }
            shown++;
        }
    }
}

void compare_desync_history_from_host(void) {
    struct ChecksumSnapshot* snapshot = find_snapshot(desync_turn);
    if (snapshot == NULL) {
        ERRORLOG("=== DESYNC: No client snapshot for turn %" PRIu64 " ===", (uint64_t)(desync_turn));
        return;
    }
    struct DesyncChecksums* host = &kfx_net_state.host_checksums;
    struct DesyncChecksums* client = &snapshot->checksums;
    struct LogDetailedSnapshot* host_snapshot = &kfx_net_state.log_snapshot;
    struct LogDetailedSnapshot* client_snapshot = &snapshot->log_details;

    ERRORLOG("=== DESYNC ANALYSIS: Host (turn %" PRIu64 ") vs Client (turn %" PRIu64 ") ===", (uint64_t)host->game_turn, (uint64_t)client->game_turn);
    if (log_checksum_mismatch("DigTags", client->dig_tasks, host->dig_tasks)) {
        log_dig_task_differences(client_snapshot);
    }
    log_checksum_mismatch("ACTION_SEED", client->action_seed, host->action_seed);
    log_checksum_mismatch("AI_SEED", client->ai_seed, host->ai_seed);
    log_checksum_mismatch("PLAYER_SEED", client->player_seed, host->player_seed);
    if (log_checksum_mismatch("Players", client->players, host->players)) {
        for (int64_t i = 0; i < client_snapshot->player_count; i++) {
            struct LogPlayerDesyncInfo* client_player = &client_snapshot->players[i];
            struct LogPlayerDesyncInfo* host_player = NULL;
            for (int64_t j = 0; j < host_snapshot->player_count; j++) {
                if (host_snapshot->players[j].id == client_player->id) {
                    host_player = &host_snapshot->players[j];
                    break;
                }
            }
            if (host_player == NULL) {
                ERRORLOG("    Player[%" PRId64 "] missing from host", (int64_t)(client_player->id));
            } else if (client_player->checksum != host_player->checksum) {
                ERRORLOG("    Player[%" PRId64 "] instance_num: Host=%" PRIu64 " Client=%" PRIu64 ", instance_remain_turns: Host=%" PRIu64 " Client=%" PRIu64 ", mappos: Host=(%" PRId64 ",%" PRId64 ",%" PRId64 ") Client=(%" PRId64 ",%" PRId64 ",%" PRId64 ")", (int64_t)(client_player->id),
                    (uint64_t)host_player->instance_num, (uint64_t)client_player->instance_num,
                    (uint64_t)host_player->instance_remain_turns, (uint64_t)client_player->instance_remain_turns,
                    (int64_t)host_player->mappos.x.val, (int64_t)host_player->mappos.y.val, (int64_t)host_player->mappos.z.val,
                    (int64_t)client_player->mappos.x.val, (int64_t)client_player->mappos.y.val, (int64_t)client_player->mappos.z.val);
            }
        }
    }
    if (log_checksum_mismatch("Rooms", client->rooms, host->rooms)) {
        int64_t shown = 0;
        for (int64_t i = 0; i < client_snapshot->room_count && shown < 10; i++) {
            struct LogRoomDesyncInfo* client_room = &client_snapshot->rooms[i];
            struct LogRoomDesyncInfo* host_room = NULL;
            for (int64_t j = 0; j < host_snapshot->room_count; j++) {
                if (host_snapshot->rooms[j].index == client_room->index) {
                    host_room = &host_snapshot->rooms[j];
                    break;
                }
            }
            if (host_room == NULL) {
                ERRORLOG("    Room[%" PRId64 "] missing from host", (int64_t)(client_room->index));
                shown++;
            } else if (client_room->checksum != host_room->checksum) {
                ERRORLOG("    Room[%" PRId64 "] slabs_count: Host=%" PRId64 " Client=%" PRId64 ", efficiency: Host=%" PRId64 " Client=%" PRId64 ", used_capacity: Host=%" PRId64 " Client=%" PRId64 ", central_stl: Host=(%" PRId64 ",%" PRId64 ") Client=(%" PRId64 ",%" PRId64 ")",
                    (int64_t)(client_room->index), (int64_t)(host_room->slabs_count), (int64_t)(client_room->slabs_count), (int64_t)(host_room->efficiency), (int64_t)(client_room->efficiency),
                    (int64_t)host_room->used_capacity, (int64_t)client_room->used_capacity,
                    (int64_t)host_room->central_stl_x, (int64_t)host_room->central_stl_y,
                    (int64_t)client_room->central_stl_x, (int64_t)client_room->central_stl_y);
                shown++;
            }
        }
    }
    log_thing_differences(client_snapshot, "Creatures", client->creatures, host->creatures, TCls_Creature);
    log_thing_differences(client_snapshot, "Traps", client->traps, host->traps, TCls_Trap);
    log_thing_differences(client_snapshot, "Shots", client->shots, host->shots, TCls_Shot);
    log_thing_differences(client_snapshot, "Objects", client->objects, host->objects, TCls_Object);
    log_thing_differences(client_snapshot, "Effects", client->effects, host->effects, TCls_Effect);
    log_thing_differences(client_snapshot, "DeadCreatures", client->dead_creatures, host->dead_creatures, TCls_DeadCreature);
    log_thing_differences(client_snapshot, "EffectGens", client->effect_gens, host->effect_gens, TCls_EffectGen);
    log_thing_differences(client_snapshot, "Doors", client->doors, host->doors, TCls_Door);
    ERRORLOG("=== END ===");
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
