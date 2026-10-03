/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file net_main.h
 *     Header file for net_main.c.
 * @par Purpose:
 *     Public declarations for shared multiplayer network support routines.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     09 May 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_NET_MAIN_H
#define DK_NET_MAIN_H

#include "bflib_basics.h"
#include "bflib_netsession.h"
#include "bflib_netsp.h"
#include "ver_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TIMEOUT_CONNECT_HOLEPUNCH 5000
#define TIMEOUT_CONNECT_DIRECT_IPV6 5000
#define TIMEOUT_CONNECT_DIRECT_IPV4 5000
#define TIMEOUT_JOIN_LOBBY 5000
#define TIMEOUT_LOBBY_EXCHANGE 5000
#define TIMEOUT_WAIT_FOR_ALL_PLAYERS 30000

#define MIN_NET_USERS 2
#define MAX_NET_USERS 4
#define MAX_NET_PEERS (MAX_NET_USERS - 1)
#define SERVER_ID 0
#define SOLO_HUMAN_ID 0 /* human player's user id in non-multiplayer, when relevant */
#define NET_MSG_BUFFER_SIZE 5000
#define INVALID_USER_ID 23456

// Moved from front_network.h (stage 8 prep, docs/refactor/
// stage-08-kfx-net.md) -- net_game.h's public setup_network_service()
// signature used this type, forcing every includer of that header to
// pull in frontend just for a 3-value enum. front_network.h includes
// net_main.h and keeps using the same name.
enum FrontendNetService {
    FrontendNetSvc_Skirmish = -1,
    FrontendNetSvc_Online = 0,
    FrontendNetSvc_LAN = 1,
};

enum NetMessageType {
    NETMSG_LOGIN,
    NETMSG_USERUPDATE,
    NETMSG_FRONTEND,
    NETMSG_CLIENT_IS_READY,
    NETMSG_HOST_DECLARES_START,
    NETMSG_STARTUP_SYNC,
    NETMSG_GAMEPLAY_UNSEQUENCED,
    NETMSG_RESYNC_DATA,
    NETMSG_UNPAUSE,
    NETMSG_CHATMESSAGE,
    NETMSG_GAMEPLAY_REPAIR,
    NETMSG_GAMEPLAY_TURN_SYNC,
};

enum NetUserProgress {
    USER_UNUSED = 0,
    USER_CONNECTED,
    USER_LOGGEDIN,
};

struct GameVersionPacket {
    int32_t major;
    int32_t minor;
    int32_t release;
    int32_t build;
};

struct NetUser {
    NetUserId id;
    char name[32];
    enum NetUserProgress progress;
    int64_t ack;
    struct GameVersionPacket version;
};

struct NetFrame {
    struct NetFrame *next;
    char *buffer;
    int64_t seq_nbr;
    size_t size;
};

struct NetState {
    const struct NetSP *sp;
    struct NetUser users[MAX_NET_USERS];
    struct NetFrame *exchg_queue;
    char password[32];
    NetUserId my_id;
    int64_t seq_nbr;
    uint64_t max_users;
    char msg_buffer[NET_MSG_BUFFER_SIZE];
    char msg_buffer_null;
    TbBool locked;
    enum NetSessionPhase phase;
};

struct TbNetworkUserInfo {
    char name[32];
    int64_t network_user_active;
    uint64_t connection_id;
};

struct ServiceInitData {
    int64_t service_flags;
    int64_t max_connections;
    int64_t buffer_size;
    int64_t timeout_value;
};

enum TbNetworkService {
    NS_ENET_UDP,
};

struct TbNetworkPlayerName {
    char name[20];
};

// Screen-share status flags/actions and the packet that carries them,
// exchanged over NETMSG_FRONTEND during the pre-game lobby. Wire format,
// not frontend UI state -- moved here (stage 8.1, docs/refactor/
// stage-08-kfx-net.md) so net_lobby.c/packets_misc.c don't have to
// depend on front_landview.h just to read/write it.
enum NetStatusLayout {
    NetStat_PlayerConnected       = 0x01,
    NetStat_ComputerPlayersMask   = 0x06,
    NetStat_ComputerPlayersShift  = 1,
    NetStat_NonActionMask         = 0x07,
    NetStat_ActionMask            = 0xF8,
};

enum NetAction {
    NetAct_None               = 0x00,
    NetAct_Slapping           = 0x08,
    NetAct_Limping            = 0x10,
    NetAct_HostStartLevel     = 0x18,
    NetAct_OpenLandView       = 0x20,
    NetAct_SetAlliance        = 0x28,
    NetAct_SetComputerPlayers = 0x38,
};

#pragma pack(1)
struct ScreenPacket {
  unsigned char networkstatus_flags;
  char frontend_alliances;
  short stored_data1; // Can contain: hand_position_x or other frontend-specific temporary data
  short stored_data2; // Can contain: hand_position_y or other frontend-specific temporary data
  short action_par1;
  unsigned char action_par2;
};
#pragma pack()

static inline unsigned char screen_packet_action(const struct ScreenPacket *nspck) {
    return nspck->networkstatus_flags & NetStat_ActionMask;
}

static inline void screen_packet_set_action(struct ScreenPacket *nspck, unsigned char action) {
    nspck->networkstatus_flags = (nspck->networkstatus_flags & NetStat_NonActionMask) | action;
}

// Session/lobby state read and written by both net_game.c and the
// frontend's network menus -- moved here (stage 8.1) since net_game.c
// (kfx_net) was writing globals declared in front_network.h
// (kfx_frontend), a backwards dependency. front_network.h keeps
// declaring the frontend-only fields that never left it (net_config_info,
// net_number_of_sessions, net_service[], tmp_net_player_name).
extern int64_t fe_network_active;
extern int64_t net_service_index_selected;
extern struct TbNetworkSessionNameEntry *net_session[SESSION_ENTRIES_COUNT];
extern int64_t net_session_index_active;
extern struct TbNetworkPlayerName net_player[MAX_NET_USERS];
extern char net_player_name[20];
extern struct ScreenPacket net_screen_packet[MAX_NET_USERS];

// Sentinel tracking the session-browser row the player is currently
// joining (distinct from net_session_index_active, the row highlighted
// in the list) -- also net-owned, moved here for the same reason
// (stage 8.2). Only net_game.c writes it; front_network.c/frontend.cpp
// read it for UI feedback.
extern int64_t net_session_index_active_id;

// Double-click detection state for packet-driven left-button clicks,
// read and written exclusively by packets.c/packets_input.c -- moved
// here (stage 8.2) since frontend.cpp only ever defined it, never used
// it itself.
extern int64_t packet_left_button_double_clicked[6];
extern int64_t packet_left_button_click_space_count[6];

extern struct NetState netstate;

static const struct GameVersionPacket net_current_version = { VER_MAJOR, VER_MINOR, VER_RELEASE, VER_BUILD };

static inline TbBool net_versions_match(const struct GameVersionPacket *version_a, const struct GameVersionPacket *version_b)
{
    return (version_a->major == version_b->major) &&
        (version_a->minor == version_b->minor) &&
        (version_a->release == version_b->release) &&
        (version_a->build == version_b->build);
}

TbError LbNetwork_Init(uint64_t srvcindex, uint64_t maxplayrs, struct TbNetworkUserInfo *locplayr, struct ServiceInitData *init_data);
enum NetJoinRejection OnNewUser(NetUserId *assigned_id);
void OnDroppedUser(NetUserId id, enum NetDropReason reason);
TbBool IsUserActive(NetUserId id);
int64_t GetRemoteUserCount(void);
void UpdateLocalPlayerInfo(NetUserId id);
char *begin_net_message(enum NetMessageType msg_type);
void send_message_buffer(NetUserId dest, const char *end_ptr);
void send_remote_buffer(const char *end_ptr);
void SendUserUpdate(NetUserId dest, NetUserId updated_user);

#ifdef __cplusplus
}
#endif

#endif
