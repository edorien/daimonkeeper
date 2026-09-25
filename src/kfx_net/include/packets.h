/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file packets.h
 *     Header file for packets.c.
 * @par Purpose:
 *     Packet processing routines.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 *     struct Packet itself, its flag enums, and its trivial field
 *     accessors moved to kfx_sim's packet_data.h (stage 13.3, docs/
 *     refactor/stage-13-enforce-and-document.md) -- kfx_sim/kfx_render
 *     dereference its fields directly and pervasively, so it has to live
 *     at or below kfx_sim's layer. This header re-includes it, so
 *     existing same-or-higher-ranked consumers of packets.h see no
 *     change; only lower-ranked consumers that needed nothing but the
 *     data half get to include packet_data.h directly instead.
 * @author   Tomasz Lis
 * @date     30 Jan 2009 - 11 Feb 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_PACKETS_H
#define DK_PACKETS_H

#include "bflib_basics.h"
#include "bflib_keybrd.h"
#include "bflib_netsp.h"
#include "globals.h"
#include "player_data.h"
#include "packet_data.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct Camera;
struct PlayerInfo;
struct Thing;
/******************************************************************************/

enum ChecksumKind {
    CKS_Action = 0,
    CKS_Players,
    CKS_Creatures_1,
    CKS_Creatures_2,
    CKS_Creatures_3,
    CKS_Creatures_4,
    CKS_Creatures_5,  // Heroes
    CKS_Creatures_6,  // Neutral
    CKS_Things, //Objects, Traps, Shots etc
    CKS_Effects,
    CKS_Rooms,
    CKS_MAX
};

/******************************************************************************/
#pragma pack(1)

struct PlayerInfo;
struct CatalogueEntry;

extern uint64_t initial_replay_seed;
extern TbBool unpausing_in_progress;

extern double camera_movement_x;
extern double camera_movement_y;

struct PacketEx
{
    struct Packet packet;
    TbBigChecksum sums[CKS_MAX];
};

#pragma pack()
/******************************************************************************/
/******************************************************************************/
void force_application_close(void);
TbBool is_mouse_on_map(struct Packet* pckt);
void remember_cursor_subtile(NetUserId user);
struct Thing *get_thing_under_hand(struct PlayerInfo *player, MapCoord x, MapCoord y);
TbBool process_dungeon_control_packet_clicks(NetUserId user);
TbBool process_user_dungeon_control_packet_action(NetUserId user);
void process_user_creature_control_packet_control(NetUserId user);
void process_user_creature_passenger_packet_action(NetUserId user);
void process_user_creature_control_packet_action(NetUserId user);
void process_map_packet_clicks(NetUserId user);
void process_pause_packet(int64_t a1, int64_t a2);
void process_camera_controls(struct Camera* cam, const struct Packet* pckt, struct PlayerInfo* player, TbBool is_local_camera);
void process_camera_action(struct Camera *cams, const struct Packet *pckt);
void process_first_person_look(struct Thing *thing, const struct Packet *pckt, int64_t current_horizontal, int64_t current_vertical, int64_t *out_horizontal, int64_t *out_vertical, int64_t *out_roll);
TbBool can_process_creature_input(struct Thing *thing);
void exchange_packets(void);
TbBool is_desync_warning_active(void);
void process_packets(void);
void set_local_packet_turn(void);
void clear_packets(void);
TbBigChecksum compute_replay_integrity(void);
void post_init_packets(void);

TbBool open_new_packet_file_for_save(void);
void load_packets_for_turn(GameTurn nturn);
TbBool open_packet_file_for_load(char *fname, struct CatalogueEntry *centry);
int64_t save_packets(void);
void close_packet_file(void);
TbBool reinit_packets_after_load(void);
TbBool packets_process_cheats(NetUserId user, PlayerNumber plyr_idx, MapCoord x, MapCoord y,
    struct Packet* pckt, MapSubtlCoord stl_x, MapSubtlCoord stl_y, MapSlabCoord slb_x, MapSlabCoord slb_y);
void disable_packet_mode(void);
/******************************************************************************/
// The editor's Fill tool (PckA_EditorFloodFill), callable directly so a test
// can drive it: the packet carries the seed only in the ambient cursor field.
void editor_flood_fill_terrain(MapSlabCoord seed_x, MapSlabCoord seed_y, SlabKind target_kind, PlayerNumber owner);

#ifdef __cplusplus
}
#endif
#endif
