/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file net_game.h
 *     Header file for net_game.c.
 * @par Purpose:
 *     Network game support for Dungeon Keeper.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   KeeperFX Team
 * @date     11 Mar 2010 - 09 Oct 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_NETGAME_H
#define DK_NETGAME_H

#include "globals.h"
#include "bflib_basics.h"
#include "net_main.h"

#ifdef __cplusplus
extern "C" {
#endif

// PACKETS_COUNT moved to kfx_sim's packet_data.h (docs/refactor/todo/
// remove-symbol-level-layering-residuals.md), reached here transitively
// via packets.h's own #include of that header wherever this file's own
// PACKETS_COUNT users (packets.c/packets_misc.c) already need it.

/******************************************************************************/
#pragma pack(1)

struct TbNetworkSessionNameEntry;
struct PlayerInfo;

/******************************************************************************/
extern struct TbNetworkUserInfo net_user_info[MAX_NET_USERS];

#pragma pack()
/******************************************************************************/
int64_t setup_network_service(enum FrontendNetService service);
int64_t setup_old_network_service(void);
TbBool init_players_network_game(void);
// Compacts net_user_info's active slots into net_user_player_number[],
// including host bookkeeping (my_player_number). Exposed (not just called
// from init_players_network_game()) so tests can seed a NetUserId <->
// PlayerNumber mapping via this real production path instead of reaching
// into net_game.c's otherwise-private net_user_player_number[] directly.
void setup_network_player_numbers(void);
void setup_count_players(void);
void are_disconnect_victories_allowed(void);

int64_t network_session_join(void);

TbBool network_user_active(NetUserId);
const char *network_user_name(NetUserId);
TbBool network_human_contenders_remain(void);
TbBool player_has_enemies_to_defeat(const struct PlayerInfo *player);
void process_player_leave_game_packet(struct PlayerInfo *player);
void process_disconnected_network_players(void);
TbBool user_present(NetUserId user);
void host_spoof_dropped_user_packets(void);
void sync_initial_network_seed(void);
TbBool network_is_host(void);
PlayerNumber get_net_user_player_number(NetUserId user);
void set_net_user_player_number(NetUserId user, PlayerNumber plyr_idx);
void remap_local_user_to_solo(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
