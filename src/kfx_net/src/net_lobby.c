/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file net_lobby.c
 *     Lobby and frontend network support for Dungeon Keeper multiplayer.
 * @par Purpose:
 *     Session lifecycle, login, chat and frontend packet exchange routines.
 * @author   KeeperFX Team
 * @date     09 May 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "net_lobby.h"

#include "bflib_enet.h"
#include "bflib_datetm.h"
#include "bflib_math.h"
#include "net_exchange_common.h"
#include "bflib_netsession.h"
#include "bflib_sound.h"
#include "config_sounds.h"
#include "net_lan.h"
#include "net_matchmaking.h"
#include "packets.h"
#include "config_campaigns.h"
#include "config_strings.h"
#include "player_data.h"
#include <SDL3/SDL.h>
#include "kfx_sim_state.h"
#include "net_game.h"
#include "ports/ui_port.h"
#include "post_inc.h"
/******************************************************************************/

#define SESSION_COUNT 32

static struct TbNetworkSessionNameEntry sessions[SESSION_COUNT];
static int64_t server_port = 0;
static TbClockMSec lobby_ping_last_sample;
uint64_t network_lobby_ping;
int64_t net_lobby_max_players = MAX_NET_USERS;
int8_t net_player_colour[MAX_NET_USERS] = {NET_PLAYER_COLOUR_NONE, NET_PLAYER_COLOUR_NONE, NET_PLAYER_COLOUR_NONE, NET_PLAYER_COLOUR_NONE};
int64_t net_lobby_host_max_users = MAX_NET_USERS;

const uint64_t alliance_grid[4][4] = {
  {0x00, 0x01, 0x02, 0x04,},
  {0x01, 0x00, 0x08, 0x10,},
  {0x02, 0x08, 0x00, 0x20,},
  {0x04, 0x10, 0x20, 0x00,},
};

static char hosted_metadata[SESSION_METADATA_MAX];
static SDL_Mutex *advertisement_mutex;
static SDL_Thread *advertisement_thread;
static SDL_AtomicInt advertisement_running;

// Why a join was refused, for the player (upstream #5373 put this in its legacy lobby menu); NULL
// when there is no such reason.
const char *net_join_error_text(enum NetJoinRejection reason)
{
    switch (reason) {
    case NetJoin_InGame:
        return get_string(GUIStr_NetGameStarted);
    case NetJoin_Locked:
        return get_string(GUIStr_NetJoiningLocked);
    case NetJoin_Full:
        return get_string(GUIStr_NetLobbyFull);
    case NetJoin_Version:
        return get_string(GUIStr_NetDifferentVersion);
    default:
        return NULL;
    }
}

void net_lobby_metadata(char *metadata)
{
    if (!advertisement_mutex) {
        metadata[0] = '\0';
        return;
    }
    SDL_LockMutex(advertisement_mutex);
    memcpy(metadata, hosted_metadata, sizeof(hosted_metadata));
    SDL_UnlockMutex(advertisement_mutex);
}

void net_lobby_refresh_metadata(void)
{
    if (netstate.my_id != SERVER_ID || !advertisement_mutex) {
        return;
    }
    struct TbNetworkSessionNameEntry session = {0};
    session.phase = netstate.phase;
    session.joinable = !netstate.locked && netstate.phase != NetPhase_InGame;
    session.max_players = netstate.max_users;
    snprintf(session.version, sizeof(session.version), "%s", NET_SESSION_VERSION);
    struct LevelInformation *level = get_level_info(get_selected_level_number());
    if (netstate.locked && level && level->players > 0) {
        session.max_players = min(session.max_players, level->players);
    }
    for (int64_t i = 0; i < netstate.max_users && i < SESSION_HUMANS_MAX; i++) {
        if (netstate.users[i].progress == USER_LOGGEDIN) {
            snprintf(session.players[session.player_count++], NETSP_PLAYER_NAME_MAX_LEN, "%s", netstate.users[i].name);
        }
    }
    SDL_LockMutex(advertisement_mutex);
    if (net_session_metadata_json(&session, hosted_metadata, sizeof(hosted_metadata)) < 0) {
        hosted_metadata[0] = '\0';
    }
    SDL_UnlockMutex(advertisement_mutex);
}

void net_lobby_set_phase(enum NetSessionPhase phase)
{
    if (netstate.my_id != SERVER_ID || netstate.phase == NetPhase_InGame) {
        return;
    }
    netstate.phase = phase;
    net_lobby_refresh_metadata();
}

enum NetJoinRejection net_lobby_join_rejection(void)
{
    if (netstate.phase == NetPhase_InGame) {
        return NetJoin_InGame;
    }
    if (netstate.locked) {
        return NetJoin_Locked;
    }
    return NetJoin_Accepted;
}

/******************************************************************************/
// Lobby player colours (upstream #5439)

#define PLAYER_COLOUR_BITS 4
#define PLAYER_COLOUR_MASK 0xF

static NetUserId player_colour_user(const int8_t colours[MAX_NET_USERS], int64_t colour)
{
    for (NetUserId user = 0; user < MAX_NET_USERS; user++) {
        if (colours[user] == colour) {
            return user;
        }
    }
    return -1;
}

void net_player_colours_reset(void)
{
    memset(net_player_colour, NET_PLAYER_COLOUR_NONE, sizeof(net_player_colour));
    net_lobby_host_max_users = MAX_NET_USERS;
}

// 4 bits a user, colour + 1 (0: no user).
int16_t net_pack_player_colours(const int8_t colours[MAX_NET_USERS])
{
    uint16_t packed = 0;
    for (NetUserId user = 0; user < MAX_NET_USERS; user++) {
        int64_t colour = colours[user];
        if (colour < NET_PLAYER_COLOUR_NONE || colour > NET_PLAYER_COLOUR_RANDOM) {
            colour = NET_PLAYER_COLOUR_NONE;
        }
        packed |= (uint16_t)(((colour + 1) & PLAYER_COLOUR_MASK) << (user * PLAYER_COLOUR_BITS));
    }
    return (int16_t)packed;
}

// From the host's packet: a value no colour packs to reads as no user.
void net_unpack_player_colours(int16_t packed, int8_t colours[MAX_NET_USERS])
{
    for (NetUserId user = 0; user < MAX_NET_USERS; user++) {
        int64_t colour = (int64_t)(((uint16_t)packed >> (user * PLAYER_COLOUR_BITS)) & PLAYER_COLOUR_MASK) - 1;
        if (colour > NET_PLAYER_COLOUR_RANDOM) {
            colour = NET_PLAYER_COLOUR_NONE;
        }
        colours[user] = (int8_t)colour;
    }
}

// Two users asked for the same slot: the game can't start until one changes.
TbBool net_player_colours_conflict(const int8_t colours[MAX_NET_USERS])
{
    for (NetUserId user = 0; user < MAX_NET_USERS; user++) {
        int64_t colour = colours[user];
        if (colour >= 0 && colour < NET_PLAYER_COLOUR_RANDOM && player_colour_user(colours, colour) != user) {
            return true;
        }
    }
    return false;
}

// A user's NetAct_SetPlayerColour, on the host: anything but a slot or random is ignored.
TbBool net_host_set_player_colour(NetUserId user, int64_t colour)
{
    if (user < 0 || user >= MAX_NET_USERS || colour < 0 || colour > NET_PLAYER_COLOUR_RANDOM) {
        return false;
    }
    net_player_colour[user] = (int8_t)colour;
    return true;
}

// Host, every lobby frame: a user who left frees their colour, one who joined gets the first free one.
void net_host_update_player_colours(void)
{
    for (NetUserId user = 0; user < MAX_NET_USERS; user++) {
        if (!network_user_active(user)) {
            net_player_colour[user] = NET_PLAYER_COLOUR_NONE;
        }
    }
    for (NetUserId user = 0; user < MAX_NET_USERS; user++) {
        if (!network_user_active(user) || net_player_colour[user] >= 0) {
            continue;
        }
        net_player_colour[user] = NET_PLAYER_COLOUR_RANDOM;
        for (int64_t colour = 0; colour < NET_PLAYER_COLOUR_RANDOM; colour++) {
            if (player_colour_user(net_player_colour, colour) < 0) {
                net_player_colour[user] = (int8_t)colour;
                break;
            }
        }
    }
}

void net_lobby_write_host_colours(struct ScreenPacket *host_packet)
{
    host_packet->stored_data1 = net_pack_player_colours(net_player_colour);
    host_packet->stored_data2 = (short)netstate.max_users;
    net_lobby_host_max_users = netstate.max_users;
}

void net_lobby_read_host_colours(const struct ScreenPacket *host_packet)
{
    net_unpack_player_colours(host_packet->stored_data1, net_player_colour);
    if (host_packet->stored_data2 >= MIN_NET_USERS && host_packet->stored_data2 <= MAX_NET_USERS) {
        net_lobby_host_max_users = host_packet->stored_data2;
    }
}

/**
 * The player slot each user gets at game start, on the host: the slot they chose, when the map has
 * it and nobody before them chose it too; else (random, or the map is too small for their choice)
 * one of the slots nobody chose, shuffled with the given host-only seed. The lobby's alliances,
 * between users, become the same alliances between their slots.
 * @param colours Each user's colour; NET_PLAYER_COLOUR_NONE for a user not in the game.
 * @param slots_count How many player slots the map has (at most MAX_NET_USERS).
 * @return False when a user in the game is left without a slot (more users than the map has slots),
 *     that user's slot being NET_PLAYER_COLOUR_NONE.
 */
TbBool net_resolve_player_colours(const int8_t colours[MAX_NET_USERS], int64_t slots_count, char lobby_alliances,
    uint32_t *seed, int8_t slots[MAX_NET_USERS], char *slot_alliances)
{
    if (slots_count > MAX_NET_USERS) {
        slots_count = MAX_NET_USERS;
    }
    int8_t free_slots[MAX_NET_USERS];
    int64_t free_count = 0;
    for (int64_t slot = 0; slot < slots_count; slot++) {
        if (player_colour_user(colours, slot) < 0) {
            free_slots[free_count++] = (int8_t)slot;
        }
    }
    for (int64_t i = free_count - 1; i > 0; i--) {
        int64_t k = LB_RANDOM(i + 1, seed);
        int8_t swapped = free_slots[i];
        free_slots[i] = free_slots[k];
        free_slots[k] = swapped;
    }
    TbBool all_placed = true;
    int64_t next_free = 0;
    for (NetUserId user = 0; user < MAX_NET_USERS; user++) {
        int64_t colour = colours[user];
        slots[user] = NET_PLAYER_COLOUR_NONE;
        if (colour < 0) {
            continue;
        }
        if (colour < slots_count && player_colour_user(colours, colour) == user) {
            slots[user] = (int8_t)colour;
        } else if (next_free < free_count) {
            slots[user] = free_slots[next_free++];
        } else {
            all_placed = false;
        }
    }
    char alliances = 0;
    for (NetUserId user1 = 0; user1 < MAX_NET_USERS; user1++) {
        for (NetUserId user2 = user1 + 1; user2 < MAX_NET_USERS; user2++) {
            if (slots[user1] >= 0 && slots[user2] >= 0 && (lobby_alliances & alliance_grid[user1][user2]) != 0) {
                alliances |= alliance_grid[slots[user1]][slots[user2]];
            }
        }
    }
    *slot_alliances = alliances;
    return all_placed;
}

// The host's slots, as a peer receives them: every active user has a slot of the map, nobody shares one.
TbBool net_player_slots_valid(const int8_t slots[MAX_NET_USERS], int64_t slots_count)
{
    if (slots_count > MAX_NET_USERS) {
        slots_count = MAX_NET_USERS;
    }
    for (NetUserId user = 0; user < MAX_NET_USERS; user++) {
        if (!network_user_active(user)) {
            continue;
        }
        if (slots[user] < 0 || slots[user] >= slots_count) {
            ERRORLOG("Network user %d has invalid player slot %d", (int)user, (int)slots[user]);
            return false;
        }
        for (NetUserId other = 0; other < user; other++) {
            if (network_user_active(other) && slots[other] == slots[user]) {
                ERRORLOG("Network users %d and %d both have player slot %d", (int)other, (int)user, (int)slots[user]);
                return false;
            }
        }
    }
    return true;
}

static int advertise_host(void *unused)
{
    while (SDL_GetAtomicInt(&advertisement_running)) {
        lan_host_update();
        matchmaking_service();
        SDL_Delay(50);
    }
    return 0;
}

static void AddSessionSegment(const char *start, const char *end)
{
    if (start == end) {
        return;
    }
    for (uint64_t i = 0; i < SESSION_COUNT; i += 1) {
        if (sessions[i].in_use) {
            continue;
        }
        sessions[i].in_use = 1;
        sessions[i].joinable = 1;
        size_t seglen = (size_t)(end - start);
        net_copy_name_string(sessions[i].text, start, min((size_t)SESSION_NAME_MAX_LEN, seglen + 1));
        return;
    }
}

void LbNetwork_SetServerPort(int64_t port)
{
    server_port = port;
}

TbError process_login_message(NetUserId source, char *read_pos, const char *end_pos)
{
    if (source == SERVER_ID) {
        // The host tells us which user we are. Both the number and the length of the message come from the host's
        // machine: a negative or large number indexed netstate.users[] and wrote our version there (found by the
        // network fuzzer), a short message was read past its end.
        if ((end_pos - read_pos) < (ptrdiff_t)(1 + sizeof(struct GameVersionPacket))) {
            WARNLOG("Login message from the host is too short");
            return Lb_OK;
        }
        const NetUserId assigned = (NetUserId)(uint8_t)read_pos[0];
        if (assigned >= (NetUserId)netstate.max_users) {
            WARNLOG("The host assigned user ID %" PRId64 ", which is not a user of this game", (int64_t)assigned);
            return Lb_OK;
        }
        netstate.my_id = assigned;
        LbNetLog("Login: assigned local user ID %d\n", (int)netstate.my_id);
        read_pos += 1;
        netstate.users[netstate.my_id].version = net_current_version;
        struct GameVersionPacket server_version;
        memcpy(&server_version, read_pos, sizeof(server_version));
        netstate.users[SERVER_ID].version = server_version;
        return Lb_OK;
    }
    if ((source < 0) || (source >= (NetUserId)netstate.max_users)) {
        WARNLOG("Login message from a source that is not a user: %" PRId64, (int64_t)source);
        return Lb_OK;
    }
    struct NetUser *user = &netstate.users[source];
    if (user->progress != USER_CONNECTED) {
        NETMSG("Peer was not in connected state");
        return Lb_OK;
    }
    enum NetJoinRejection reason = net_lobby_join_rejection();
    if (reason != NetJoin_Accepted) {
        netstate.sp->drop_user(source, reason);
        return Lb_OK;
    }
    const char *password;
    if (!read_network_message_text(&read_pos, &password, sizeof(netstate.password))) {
        NETDBG(6, "Connected peer sent invalid password");
        netstate.sp->drop_user(source, NetJoin_Accepted);
        return Lb_OK;
    }
    if (netstate.password[0] != 0 && strncmp(password, netstate.password, sizeof(netstate.password)) != 0) {
        NETMSG("Peer chose wrong password");
        return Lb_OK;
    }

    const char *name;
    if (!read_network_message_text(&read_pos, &name, sizeof(user->name) - 1) || name[0] == '\0') {
        NETDBG(6, "Connected peer sent invalid name");
        netstate.sp->drop_user(source, NetJoin_Accepted);
        return Lb_OK;
    }
    strcpy(user->name, name);
    if (!isalnum(user->name[0])) {
        NETDBG(6, "Connected peer had bad name starting with %c", (int)(user->name[0]));
        netstate.sp->drop_user(source, NetJoin_Accepted);
        return Lb_OK;
    }
    if ((end_pos - read_pos) < (ptrdiff_t)sizeof(user->version)) {
        NETDBG(6, "Connected peer sent a login without its version");
        netstate.sp->drop_user(source, NetJoin_Version);
        return Lb_OK;
    }
    memcpy(&user->version, read_pos, sizeof(user->version));
    // Only this game's own version (upstream #5373 also lets KeeperFX's stable release join: not this game's).
    if (!net_versions_match(&user->version, &net_current_version)) {
        netstate.sp->drop_user(source, NetJoin_Version);
        return Lb_OK;
    }
    NETMSG("User %d (%s) successfully logged in", (int)source, user->name);
    user->progress = USER_LOGGEDIN;
    play_non_3d_sample(snd_spell_stars);
    char *reply_pos = begin_net_message(NETMSG_LOGIN);
    *reply_pos = source;
    reply_pos += 1;
    memcpy(reply_pos, &netstate.users[SERVER_ID].version, sizeof(netstate.users[SERVER_ID].version));
    reply_pos += sizeof(netstate.users[SERVER_ID].version);
    send_message_buffer(source, reply_pos);
    for (NetUserId user_id = 0; user_id < netstate.max_users; user_id += 1) {
        if (netstate.users[user_id].progress == USER_UNUSED) {
            continue;
        }
        SendUserUpdate(source, user_id);
        if (user_id != netstate.my_id && user_id != source) {
            SendUserUpdate(user_id, source);
        }
    }
    UpdateLocalPlayerInfo(source);
    return Lb_OK;
}

TbError process_user_update_message(NetUserId source, char *read_pos, const char *end_pos)
{
    if (source != SERVER_ID) {
        WARNLOG("Unexpected USERUPDATE");
        return Lb_OK;
    }
    if ((end_pos - read_pos) < 2) {
        WARNLOG("USERUPDATE from the host is too short");
        return Lb_OK;
    }
    NetUserId user_id = (NetUserId)read_pos[0];
    read_pos += 1;
    if (user_id < 0 || user_id >= netstate.max_users) {
        ERRORLOG("Out of range user ID %" PRId64 " received from the host in USERUPDATE, ignored", (int64_t)(user_id));
        return Lb_OK;
    }
    struct NetUser *user = &netstate.users[user_id];
    // (the stage a user has reached is one of three; the message can say anything)
    if (((uint8_t)read_pos[0] != USER_UNUSED) && ((uint8_t)read_pos[0] != USER_CONNECTED) && ((uint8_t)read_pos[0] != USER_LOGGEDIN)) {
        WARNLOG("USERUPDATE from the host has a stage %d that does not exist", (int)(uint8_t)read_pos[0]);
        return Lb_OK;
    }
    user->progress = (enum NetUserProgress)read_pos[0];
    read_pos += 1;
    const char *name;
    if (!read_network_message_text(&read_pos, &name, sizeof(user->name) - 1)) {
        // (this was an abort(): the host could end the game of everyone who joined it with one short message)
        ERRORLOG("Unterminated name in USERUPDATE, ignored");
        return Lb_OK;
    }
    strcpy(user->name, name);
    if (read_pos + sizeof(user->version) <= end_pos) {
        memcpy(&user->version, read_pos, sizeof(user->version));
    }
    UpdateLocalPlayerInfo(user_id);
    return Lb_OK;
}

void LbNetwork_InitSessionsFromCmdLine(const char *str)
{
    NETMSG("Initializing sessions from command line: %s", str);
    const char *start = str;
    const char *end = str;
    while (*end != '\0') {
        if (start != end && (*end == ',' || *end == ';')) {
            AddSessionSegment(start, end);
            start = end + 1;
        }
        end += 1;
    }
    AddSessionSegment(start, end);
}

TbError LbNetwork_ExchangeLogin(char *player_name)
{
    NETMSG("Logging in as %s", player_name);
    if (1 + strlen(netstate.password) + 1 + strlen(player_name) + 1 + sizeof(net_current_version) >= sizeof(netstate.msg_buffer)) {
        ERRORLOG("Login credentials too long");
        return Lb_FAIL;
    }
    char *write_pos = begin_net_message(NETMSG_LOGIN);
    strcpy(write_pos, netstate.password);
    write_pos += strlen(netstate.password) + 1;
    strcpy(write_pos, player_name);
    write_pos += strlen(player_name) + 1;
    memcpy(write_pos, &net_current_version, sizeof(net_current_version));
    write_pos += sizeof(net_current_version);
    send_message_buffer(SERVER_ID, write_pos);
    TbClockMSec wait_start_time = LbTimerClock();
    while (net_join_rejection == NetJoin_Accepted && LbTimerClock() - wait_start_time < TIMEOUT_JOIN_LOBBY) {
        if (!netstate.sp->msgready(SERVER_ID, 0)) {
            netstate.sp->update(NULL);
            if (!netstate.sp->msgready(SERVER_ID, 0)) {
                SDL_Delay(1);
                continue;
            }
        }
        if (process_network_message(SERVER_ID, &net_screen_packet, sizeof(struct ScreenPacket), NETMSG_FRONTEND, NULL) == Lb_FAIL
         || netstate.msg_buffer[0] == NETMSG_LOGIN) {
            break;
        }
    }
    if (net_join_rejection != NetJoin_Accepted) {
        return Lb_FAIL;
    }
    if (netstate.msg_buffer[0] != NETMSG_LOGIN || netstate.my_id == INVALID_USER_ID) {
        return Lb_FAIL;
    }
    if (netstate.sp->msgready(SERVER_ID, TIMEOUT_JOIN_LOBBY)) {
        process_network_message(SERVER_ID, &net_screen_packet, sizeof(struct ScreenPacket), NETMSG_FRONTEND, NULL);
    }
    return Lb_OK;
}

TbError LbNetwork_ExchangeFrontend(void *send_buf, void *server_buf, size_t frame_size)
{
    TbError result = exchange_frame_block(NETMSG_FRONTEND, send_buf, server_buf, frame_size);
    TbClockMSec now = LbTimerClock();
    if (network_lobby_ping == 0 || now - lobby_ping_last_sample >= 1000) {
        uint64_t ping = GetPing(my_player_number, my_player_number);
        if (ping > 0) {
            network_lobby_ping = ping;
        }
        lobby_ping_last_sample = now;
    }
    return result;
}

TbError LbNetwork_Create(char *lobby_name, char *plyr_name, uint64_t *plyr_num, void *optns)
{
    if (!netstate.sp) {
        ERRORLOG("No network SP selected");
        return Lb_FAIL;
    }
    char default_port_buf[16];
    snprintf(default_port_buf, sizeof(default_port_buf), ":%" PRIu64, (uint64_t)enet_port);
    const char *port = default_port_buf;
    char port_string[16] = "";
    if (server_port != 0) {
        snprintf(port_string, sizeof(port_string), "%" PRId64, (int64_t)(server_port));
        port = port_string;
    }
    if (netstate.sp->host(port, optns) == Lb_FAIL) {
        return Lb_FAIL;
    }
    if (!advertisement_mutex) {
        advertisement_mutex = SDL_CreateMutex();
    }
    if (!advertisement_mutex) {
        netstate.sp->exit();
        return Lb_FAIL;
    }
    netstate.phase = NetPhase_Lobby;
    netstate.max_users = net_lobby_max_players;
    netstate.my_id = SERVER_ID;
    snprintf(netstate.users[SERVER_ID].name, sizeof(netstate.users[SERVER_ID].name), "%s", plyr_name);
    netstate.users[SERVER_ID].progress = USER_LOGGEDIN;
    netstate.locked = 0;
    net_lobby_refresh_metadata();
    const uint16_t local_port = enet_get_bound_port();
    uint16_t ipv4_port = local_port;
    if (external_ipv4_port != 0) {
        ipv4_port = external_ipv4_port;
    }
    const uint16_t ipv6_port = enet_get_bound_ipv6_port();
    if (ui_frontnet_service_selected(FrontendNetSvc_LAN)) {
        lan_host_start(lobby_name, local_port);
    }
    if (ui_frontnet_service_selected(FrontendNetSvc_Online) && matchmaking_create(lobby_name, external_ipv4_address, ipv4_port, ipv6_port, local_port) != 0) {
        LbNetwork_Stop();
        return Lb_FAIL;
    }
    netstate.users[netstate.my_id].version = net_current_version;
    *plyr_num = netstate.my_id;
    UpdateLocalPlayerInfo(netstate.my_id);
    LbNetwork_EnableNewPlayers(1);
    SDL_SetAtomicInt(&advertisement_running, 1);
    advertisement_thread = SDL_CreateThread(advertise_host, "lobby_advertising", NULL);
    if (!advertisement_thread) {
        LbNetwork_Stop();
        return Lb_FAIL;
    }
    return Lb_OK;
}

TbError LbNetwork_Join(struct TbNetworkSessionNameEntry *nsname, char *plyr_name, int64_t *plyr_num, void *optns)
{
    if (!netstate.sp) {
        ERRORLOG("No network SP selected");
        return Lb_FAIL;
    }
    if (netstate.sp->join(nsname->text, optns) == Lb_FAIL) {
        return Lb_FAIL;
    }
    netstate.my_id = INVALID_USER_ID;
    if (LbNetwork_ExchangeLogin(plyr_name) == Lb_FAIL) {
        return Lb_FAIL;
    }
    *plyr_num = netstate.my_id;
    return Lb_OK;
}

TbError LbNetwork_EnableNewPlayers(TbBool allow)
{
    int was_locked = netstate.locked;
    netstate.locked = !allow;
    net_lobby_refresh_metadata();
    if (!was_locked && !allow) {
        for (NetUserId i = 0; i < netstate.max_users; i += 1) {
            if (netstate.users[i].progress == USER_CONNECTED) {
                netstate.sp->drop_user(i, net_lobby_join_rejection());
            }
        }
    }
    if (netstate.locked) {
        NETMSG("New players are NOT allowed to join");
    } else {
        NETMSG("New players ARE allowed to join");
    }
    return Lb_OK;
}

TbError LbNetwork_Stop(void)
{
    SDL_SetAtomicInt(&advertisement_running, 0);
    if (advertisement_thread) {
        SDL_WaitThread(advertisement_thread, NULL);
        advertisement_thread = NULL;
    }
    lan_shutdown();
    matchmaking_disconnect(netstate.phase);
    if (netstate.sp) {
        netstate.sp->exit();
    }
    clear_flag(local_system_flags, GSF_NetworkActive);
    memset(&netstate, 0, sizeof(netstate));
    netstate.my_id = INVALID_USER_ID;
    return Lb_OK;
}

TbError LbNetwork_EnumeratePlayers(struct TbNetworkSessionNameEntry *, TbNetworkCallbackFunc callback, void *buf)
{
    struct TbNetworkCallbackData data;
    for (NetUserId id = 0; id < netstate.max_users; id += 1) {
        if (!IsUserActive(id)) {
            continue;
        }
        memset(&data, 0, sizeof(data));
        snprintf(data.plyr_name, sizeof(data.plyr_name), "%s", netstate.users[id].name);
        callback(&data, buf);
    }
    return Lb_OK;
}

TbError LbNetwork_EnumerateSessions(TbNetworkCallbackFunc callback, void *ptr)
{
    for (uint64_t i = 0; i < SESSION_COUNT; i += 1) {
        if (!sessions[i].in_use) {
            continue;
        }
        callback((struct TbNetworkCallbackData *)&sessions[i], ptr);
    }
    return Lb_OK;
}
