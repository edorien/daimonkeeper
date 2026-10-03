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

extern TbBool unpausing_in_progress;


struct PacketEx
{
    struct Packet packet;
    TbBigChecksum sums[CKS_MAX];
};

#pragma pack()
/******************************************************************************/
/******************************************************************************/
void force_application_close(void);
void process_pause_packet(int64_t a1, int64_t a2);
void exchange_packets(void);
TbBool is_desync_warning_active(void);
TbBool resync_game_allowed(void);
void set_local_packet_turn(void);
void clear_packets(void);
void set_packet_pause_toggle(void);
/******************************************************************************/
// Applying the packets (process_packets() and the rest) is kfx_game's
// game_commands.h; replay file I/O is game_replay.h (refactor pass 2, S12).

#ifdef __cplusplus
}
#endif
#endif
