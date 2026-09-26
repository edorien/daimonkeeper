/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/**
 * @file net_matchmaking.h
 *     Matchmaking client for the KeeperFX lobby server.
 * @par Purpose:
 *     Connects to the WebSocket-based matchmaking server to list, host, and
 *     join game sessions.  Provides the hole-punch relay flow required for
 *     NAT traversal when direct port-forwarding is unavailable.
 * @author   KeeperFX Team
 * @date     06 Mar 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef NET_MATCHMAKING_H
#define NET_MATCHMAKING_H

#include "bflib_basics.h"
#include "bflib_netsession.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MATCHMAKING_HOST_MAX 128 /* max string length*/
#define MATCHMAKING_URL_MAX (MATCHMAKING_HOST_MAX + 16)
#define MATCHMAKING_ID_MAX 64
#define MATCHMAKING_IP_MAX 64
#define MATCHMAKING_NAME_MAX SESSION_NAME_MAX_LEN
#define MATCHMAKING_SESSIONS_MAX 32

enum MatchmakingLobbyResult {
    MMLobbyResult_Closed,
    MMLobbyResult_Started,
};

typedef struct {
    char ipv4[MATCHMAKING_IP_MAX];
    char ipv6[MATCHMAKING_IP_MAX];
    int64_t ipv4_port;
    int64_t ipv6_port;
    int64_t direct_ipv4_port;
} PunchAddresses;

extern struct TbNetworkSessionNameEntry matchmaking_sessions[MATCHMAKING_SESSIONS_MAX];
extern TbBool matchmaking_enabled;
extern char matchmaking_ws_url[MATCHMAKING_URL_MAX];
extern char matchmaking_ip_url[MATCHMAKING_URL_MAX];
extern int64_t matchmaking_session_count;
extern char join_lobby_id[MATCHMAKING_ID_MAX];

void matchmaking_set_server(const char* host);
void matchmaking_connect_async(void);
int64_t matchmaking_connect(void);
int64_t matchmaking_request_list(void);
void matchmaking_disconnect(void);
void matchmaking_close_lobby(enum MatchmakingLobbyResult result, int64_t map_number, const char *map_name);
void matchmaking_refresh_sessions(void);
int64_t matchmaking_create(const char *name, const char *udp_ipv4, int64_t udp_ipv4_port, int64_t udp_ipv6_port, int64_t direct_ipv4_port);
int64_t matchmaking_punch(const char *lobby_id, const char *udp_ipv4, int64_t udp_ipv4_port, int64_t udp_ipv6_port, PunchAddresses *output);
int64_t matchmaking_poll_punch(PunchAddresses *output);

#ifdef __cplusplus
}
#endif

#endif
