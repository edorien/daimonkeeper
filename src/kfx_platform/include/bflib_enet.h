/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/**
 * @author   KeeperFX Team
 * @date     18 Oct 2022
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/

#ifndef GIT_BFLIB_ENET_H
#define GIT_BFLIB_ENET_H

#include <stdint.h>
#include <stddef.h>
#include "bflib_basics.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ENET_DEFAULT_PORT 5556
extern int64_t enet_port;

enum {
    ENET_CHANNEL_RELIABLE = 0,
    ENET_CHANNEL_UNSEQUENCED = 1
};

struct NetSP;
struct NetSP* InitEnetSP();
uint64_t GetPing(int64_t id, int64_t local_player_id);
uint64_t GetPacketLoss(int64_t id, int64_t local_player_id);
uint64_t GetClientDataInTransit();
uint64_t GetClientPacketsLost();
uint64_t GetUploadRateBytesPerSecond();
uint64_t GetDownloadRateBytesPerSecond();
int64_t enet_matchmaking_host_update(void);
extern int64_t external_ipv4_port;
extern char external_ipv4_address[64];
extern int64_t skip_holepunch;
int64_t enet_get_bound_ipv6_port(void);

struct _ENetHost;
struct _ENetAddress;

// Mirrors net_matchmaking.h's PunchAddresses (same field sizes/order) so
// this header doesn't need to include it. See EnetConnectivityServices
// below and docs/refactor/stage-02-decouple-bflib.md.
#define ENET_MATCHMAKING_ID_MAX 64
#define ENET_MATCHMAKING_IP_MAX 64

struct EnetPunchAddresses {
    char ipv4[ENET_MATCHMAKING_IP_MAX];
    char ipv6[ENET_MATCHMAKING_IP_MAX];
    int64_t ipv4_port;
    int64_t ipv6_port;
    int64_t direct_ipv4_port;
};

// Injected by the net layer so bflib_enet.cpp (platform layer) doesn't
// reach upward into front_network.h/net_holepunch.h/net_matchmaking.h/
// net_portforward.h directly. See docs/refactor/stage-02-decouple-bflib.md.
struct EnetConnectivityServices {
    void (*display_attempting_to_join_message)(int64_t seconds_remaining);
    TbBool (*attempting_to_join_cancel_requested)(void);
    int64_t (*holepunch_stun_query)(struct _ENetHost *host, char *output_ip, size_t output_ip_buffer_size);
    void (*holepunch_punch_to)(struct _ENetHost *host, const struct _ENetAddress *target);
    int64_t (*holepunch_handle_packet)(struct _ENetHost *host, struct _ENetAddress *expected, size_t expected_count, int64_t *received_mask);
    void (*holepunch_stun_keepalive)(struct _ENetHost *host);
    int64_t (*matchmaking_punch)(const char *lobby_id, const char *udp_ipv4, int64_t udp_ipv4_port, int64_t udp_ipv6_port, struct EnetPunchAddresses *output);
    int64_t (*matchmaking_poll_punch)(struct EnetPunchAddresses *output);
    int64_t (*port_forward_add_mapping)(int64_t port);
    void (*port_forward_remove_mapping)(void);
};

void bf_enet_set_connectivity_services(const struct EnetConnectivityServices *services);

// Sets the pending join target (lobby id, or "LAN:<ip>:<port>", or empty
// for a plain direct-connect session string). Mirrors net_matchmaking.h's
// join_lobby_id global, owned by the net layer.
void bf_enet_set_join_lobby_id(const char *lobby_id);

#ifdef __cplusplus
}
#endif

#endif //GIT_BFLIB_ENET_H
