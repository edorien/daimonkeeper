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

int64_t network_session_join(void);

TbBool network_user_active(NetUserId);
const char *network_user_name(NetUserId);
TbBool network_human_contenders_remain(void);
void process_player_leave_game_packet(struct PlayerInfo *player);
void process_disconnected_network_players(void);
TbBool user_present(NetUserId user);
void host_spoof_dropped_user_packets(void);
void sync_initial_network_seed(void);
TbBool network_is_host(void);
PlayerNumber get_net_user_player_number(NetUserId user);
void set_net_user_player_number(NetUserId user, PlayerNumber plyr_idx);
struct UserStartSettings;
struct PlayerInfo;
void build_local_user_start_settings(struct UserStartSettings *us);
void apply_user_start_settings(struct PlayerInfo *player, const struct UserStartSettings *us, const struct UserStartSettings *host);
void apply_recorded_network_stop(void);
TbBool get_startup_user_settings(NetUserId user, struct UserStartSettings *us);
void remap_user_to_solo(struct PlayerInfo *myplyr);
/** External seats (local games only): human-shaped players driven by packets an outside process
 *  writes into sim_packets[user]. See docs/refactor/AI/LLM/04-seat-and-action-api.md section 1. */
/** Makes plyr_idx an External seat and returns its NetUserId (1..MAX_NET_USERS-1), or -1 if refused:
 *  network game, the local player's slot, no free user id, or a slot held by a human. An unclaimed
 *  slot is set up like a network player; a computer-controlled slot is converted, keeping its
 *  dungeon, and stops running the built-in AI. */
NetUserId net_add_external_seat(PlayerNumber plyr_idx);
/** Hands an External seat back to the built-in AI (docs/refactor/AI/LLM/06 section 2.2 Option B): drops its queued
 * steps, unmaps its user, and re-arms the built-in AI with the model the slot had before it became a seat.
 * Deliberately does NOT reset the creatures' states (no init_creature_states_for_player): whatever they were doing
 * carries on, and the AI picks up from there. Returns false if plyr_idx is not a seat. */
TbBool net_release_external_seat(PlayerNumber plyr_idx);
/** Whether the keeper name (net_player_name: the network screen's name, or -nick) is one the player chose, not empty or
 *  the "No Name" default. */
TbBool keeper_name_is_set(void);
/** A local game starts: the local human's keeper name becomes their player name, as in a network game, so other
 *  players (agents included) can tell who they play against (09-persistent-memory.md section 6.8). */
void net_apply_keeper_name(PlayerNumber plyr_idx);
/** Releases every External seat; returns how many were handed back. */
int64_t net_release_all_external_seats(void);
/** Slots the Skirmish "Slots & AI" page marked External for the next local game (docs/refactor/AI/LLM/01 M5). The list
 * is set when Play is pressed and consumed once by net_claim_pending_external_seats() when the game's players exist. */
void net_pending_external_seats_clear(void);
void net_pending_external_seats_add(PlayerNumber plyr_idx);
int64_t net_pending_external_seats_count(void);
/** Turns each pending slot that has a player with a dungeon heart into an External seat, then empties the list.
 * Returns how many became seats; a slot that cannot (no heart, not computer-controlled) is logged and skipped. */
int64_t net_claim_pending_external_seats(void);
/** Forgets every External seat mapping. Call when a fresh local game starts. */
void net_clear_external_seats(void);
/** Rebuilds the mapping from the players' PlaF_ExternalSeat flag and user_id. Call after loading a
 *  save: the mapping is process-global and never saved, but the flag and user_id are. */
void net_restore_external_seats_after_load(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
