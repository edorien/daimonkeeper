/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file net_game.c
 *     Network game support for Dungeon Keeper.
 * @par Purpose:
 *     Functions to exchange packets through network.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     11 Mar 2010 - 09 Oct 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "net_matchmaking.h"
#include "net_game.h"
#include "external_seat.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_coroutine.h"
#include "bflib_datetm.h"
#include "bflib_enet.h"
#include "net_exchange_common.h"
#include "net_holepunch.h"
#include "net_lobby.h"
#include "net_main.h"
#include "net_portforward.h"
#include "net_resync.h"

#include "player_data.h"
#include "player_utils.h"
#include "player_computer.h"
#include "light_data.h"
#include "packets.h"
#include "sim_feedback.h"
#include "net_callbacks.h"
#include "config.h"
#include "config_campaigns.h"
#include "config_settings.h"
#include "config_keeperfx.h"
#include "config_strings.h"
#include "custom_sprites.h"
#include "dungeon_data.h"
#include "thing_list.h"
#include "engine_camera.h"
#include "net_exchange_gameplay.h"
#include "net_input_lag.h"
#include "net_checksums.h"
#include "kfx_config_state.h"
#include "kfx_net_state.h"
#include "kfx_sim_state.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct TbNetworkUserInfo net_user_info[MAX_NET_USERS];
extern int64_t multiplayer_speed_adjustment_ns;
/******************************************************************************/

#pragma pack(1)
struct StartupSyncPacket {
    uint8_t startup_sync_packet_valid;
    int64_t video_rotate_mode;
    TbBigChecksum map_checksums[NETWORK_STARTUP_MAP_FILE_COUNT];
    TbBigChecksum required_sprite_zip_checksums[REQUIRED_SPRITE_ZIP_COUNT];
    int64_t initial_tendencies;
    uint64_t isometric_view_zoom_level;
    uint64_t frontview_zoom_level;
    uint64_t zoom_distance_setting;
    uint64_t frontview_zoom_distance_setting;
    uint8_t initial_input_lag_turns;
    // TODO: also record alliance matrix.
};
#pragma pack()

// Adapters for bflib_enet.h's EnetConnectivityServices: bridge
// net_matchmaking.h's real PunchAddresses to bflib's mirror struct, since
// bflib_enet.cpp can't include net_matchmaking.h itself (see
// docs/refactor/stage-02-decouple-bflib.md).
static void copy_punch_addresses(struct EnetPunchAddresses *dst, const PunchAddresses *src)
{
    snprintf(dst->ipv4, sizeof(dst->ipv4), "%s", src->ipv4);
    snprintf(dst->ipv6, sizeof(dst->ipv6), "%s", src->ipv6);
    dst->ipv4_port = src->ipv4_port;
    dst->ipv6_port = src->ipv6_port;
}

static int64_t enet_services_matchmaking_punch(const char *lobby_id, int64_t udp_ipv4_port, int64_t udp_ipv6_port, struct EnetPunchAddresses *output)
{
    PunchAddresses real_output;
    int64_t result = matchmaking_punch(lobby_id, udp_ipv4_port, udp_ipv6_port, &real_output);
    if (result == 0) {
        copy_punch_addresses(output, &real_output);
    }
    return result;
}

static int64_t enet_services_matchmaking_poll_punch(struct EnetPunchAddresses *output)
{
    PunchAddresses real_output;
    int64_t result = matchmaking_poll_punch(&real_output);
    if (result) {
        copy_punch_addresses(output, &real_output);
    }
    return result;
}

// Thin adapters so the enet_connectivity_services static initializer
// below (which needs compile-time-constant function addresses) can
// target net_callbacks->* -- those are runtime indirections, not
// usable directly in a static initializer.
static void enet_services_display_attempting_to_join_message(int64_t seconds_remaining)
{
    net_callbacks->display_attempting_to_join_message(seconds_remaining);
}

static TbBool enet_services_attempting_to_join_cancel_requested(void)
{
    return net_callbacks->attempting_to_join_cancel_requested();
}

static const struct EnetConnectivityServices enet_connectivity_services = {
    .display_attempting_to_join_message = &enet_services_display_attempting_to_join_message,
    .attempting_to_join_cancel_requested = &enet_services_attempting_to_join_cancel_requested,
    .holepunch_stun_query = &holepunch_stun_query,
    .holepunch_punch_to = &holepunch_punch_to,
    .holepunch_receive = &holepunch_receive,
    .matchmaking_punch = &enet_services_matchmaking_punch,
    .matchmaking_poll_punch = &enet_services_matchmaking_poll_punch,
    .port_forward_add_mapping = &port_forward_add_mapping,
    .port_forward_remove_mapping = &port_forward_remove_mapping,
};

int64_t setup_network_service(enum FrontendNetService service)
{
  struct ServiceInitData *init_data = NULL;
  SYNCMSG("Initializing 4-players type %" PRId64 " network", (int64_t)(service));
  memset(net_user_info, 0, sizeof(net_user_info));
  network_lobby_ping = 0;
  if (service != FrontendNetSvc_Online && service != FrontendNetSvc_LAN) {
    net_callbacks->process_network_error(-800);
    return 0;
  }
  bf_enet_set_connectivity_services(&enet_connectivity_services);
  if ( LbNetwork_Init(NS_ENET_UDP, MAX_NET_USERS, &net_user_info[0], init_data) )
  {
    net_callbacks->process_network_error(-800);
    return 0;
  }
  net_service_index_selected = service;
  net_callbacks->set_lobby_button_labels(service == FrontendNetSvc_LAN);
  net_callbacks->enter_net_session_screen();
  return 1;
}

int64_t setup_old_network_service(void)
{
    return setup_network_service(net_service_index_selected);
}

TbBool network_is_host(void)
{
    return netstate.my_id == SERVER_ID;
}

// map NetUserId -> PlayerNumber, or -1 for a user without a player.
// Currently, this mapping is 1-1.
// Potential future work: "archon mode" (multiple users share a player)
static PlayerNumber net_user_player_number[MAX_NET_USERS];
// Local (non-networked) games: the same mapping for External seats. The local human is never in
// here (SOLO_HUMAN_ID always follows my_player_number), and it is kept apart from the table
// above so a stale networked mapping can never leak into a local game.
static PlayerNumber net_local_external_player[MAX_NET_USERS] = {-1, -1, -1, -1};
/** The built-in AI model each seat had before it became a seat, for net_release_external_seat(). */
static int64_t net_local_external_prev_model[MAX_NET_USERS];

PlayerNumber get_net_user_player_number(NetUserId user)
{
    if ((user < 0) || (user >= MAX_NET_USERS)) {
        return -1;
    }
    if (!network_is_active() && !kfx_net_state.packet_load_enable) {
        return (user == SOLO_HUMAN_ID) ? my_player_number : net_local_external_player[user];
    }
    return net_user_player_number[user];
}

void net_clear_external_seats(void)
{
    extseat_reset();
    for (NetUserId user = 0; user < MAX_NET_USERS; user++) {
        net_local_external_player[user] = -1;
    }
}

NetUserId net_add_external_seat(PlayerNumber plyr_idx)
{
    if (network_is_active() || (plyr_idx < 0) || (plyr_idx >= PLAYERS_COUNT) || (plyr_idx == my_player_number)) {
        return -1;
    }
    struct PlayerInfo *player = get_player(plyr_idx);
    const TbBool exists = player_exists(player);
    if (exists && !flag_is_set(player->allocflags, PlaF_CompCtrl)) {
        return -1; // a human, or already an External seat
    }
    NetUserId user = -1;
    for (NetUserId i = SOLO_HUMAN_ID + 1; i < MAX_NET_USERS; i++) {
        if (net_local_external_player[i] < 0) {
            user = i;
            break;
        }
    }
    if (user < 0) {
        return -1;
    }
    if (exists) {
        // Same fields the built-in AI would have used; without CompCtrl and the assist bit it is
        // never ticked again.
        clear_flag(player->allocflags, PlaF_CompCtrl);
        struct Dungeon *dungeon = get_players_dungeon(player);
        if (!dungeon_invalid(dungeon)) {
            dungeon->computer_enabled &= ~0x01;
        }
        struct Computer2 *comp = get_computer_player(plyr_idx);
        net_local_external_prev_model[user] = computer_player_invalid(comp) ? 0 : comp->model;
        if (!computer_player_invalid(comp)) {
            memset(comp, 0, sizeof(struct Computer2));
            computer_set_dungeon(comp, dungeon);
        }
    }
    player->id_number = plyr_idx;
    player->user_id = user;
    set_flag(player->allocflags, PlaF_Allocated | PlaF_ExternalSeat);
    player->is_active = 1;
    player->view_mode_restore = PVM_IsoWibbleView;
    init_player(player, 0);
    init_user_state(user);
    set_creature_tendencies(player, CrTend_Imprison, IMPRISON_BUTTON_DEFAULT);
    set_creature_tendencies(player, CrTend_Flee, FLEE_BUTTON_DEFAULT);
    snprintf(player->player_name, sizeof(player->player_name), "External %d", (int)user);
    net_local_external_player[user] = plyr_idx;
    SYNCLOG("External seat: user %" PRId64 " -> player %" PRId64, (int64_t)user, (int64_t)plyr_idx);
    return user;
}

TbBool net_release_external_seat(PlayerNumber plyr_idx)
{
    if ((plyr_idx < 0) || (plyr_idx >= PLAYERS_COUNT) || network_is_active()) {
        return false;
    }
    struct PlayerInfo *player = get_player(plyr_idx);
    if (!player_exists(player) || !flag_is_set(player->allocflags, PlaF_ExternalSeat)) {
        return false;
    }
    const NetUserId user = player->user_id;
    if ((user <= SOLO_HUMAN_ID) || (user >= MAX_NET_USERS) || (net_local_external_player[user] != plyr_idx)) {
        return false;
    }
    extseat_release_ordered_now(user, "seat_released");
    extseat_forget_user(user);
    net_local_external_player[user] = -1;
    clear_flag(player->allocflags, PlaF_ExternalSeat);
    player->user_id = SOLO_HUMAN_ID;
    player->player_name[0] = '\0';
    if (!script_support_setup_player_as_computer_keeper(plyr_idx, net_local_external_prev_model[user])) {
        WARNLOG("Released External seat %" PRId64 " could not be handed to the built-in AI", (int64_t)plyr_idx);
        return true; // still released: it is an idle keeper, not a seat
    }
    SYNCLOG("External seat released: player %" PRId64 " is computer-controlled again (model %" PRId64 ")", (int64_t)plyr_idx, (int64_t)net_local_external_prev_model[user]);
    return true;
}

int64_t net_release_all_external_seats(void)
{
    int64_t released = 0;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++) {
        if (net_release_external_seat(p)) {
            released++;
        }
    }
    return released;
}

static PlayerNumber net_pending_external_player[PLAYERS_COUNT];
static int64_t net_pending_external_count = 0;

void net_pending_external_seats_clear(void)
{
    net_pending_external_count = 0;
}

void net_pending_external_seats_add(PlayerNumber plyr_idx)
{
    if ((plyr_idx < 0) || (plyr_idx >= PLAYERS_COUNT)) {
        return;
    }
    for (int64_t i = 0; i < net_pending_external_count; i++) {
        if (net_pending_external_player[i] == plyr_idx) {
            return;
        }
    }
    net_pending_external_player[net_pending_external_count++] = plyr_idx;
}

int64_t net_pending_external_seats_count(void)
{
    return net_pending_external_count;
}

int64_t net_claim_pending_external_seats(void)
{
    int64_t claimed = 0;
    for (int64_t i = 0; i < net_pending_external_count; i++) {
        const PlayerNumber plyr_idx = net_pending_external_player[i];
        if (!player_exists(get_player(plyr_idx)) || thing_is_invalid(find_players_dungeon_heart(plyr_idx))) {
            WARNLOG("Skirmish External slot %" PRId64 " has no keeper with a dungeon heart; left as it is", (int64_t)plyr_idx);
            continue;
        }
        if (net_add_external_seat(plyr_idx) < 0) {
            WARNLOG("Skirmish External slot %" PRId64 " could not become a seat", (int64_t)plyr_idx);
            continue;
        }
        claimed++;
    }
    net_pending_external_count = 0;
    return claimed;
}

void net_restore_external_seats_after_load(void)
{
    net_clear_external_seats();
    for (PlayerNumber plyr_idx = 0; plyr_idx < PLAYERS_COUNT; plyr_idx++) {
        const struct PlayerInfo *player = get_player(plyr_idx);
        if (!player_exists(player) || !flag_is_set(player->allocflags, PlaF_ExternalSeat)) {
            continue;
        }
        if ((player->user_id <= SOLO_HUMAN_ID) || (player->user_id >= MAX_NET_USERS)) {
            WARNLOG("External seat player %" PRId64 " has unusable user id %" PRId64, (int64_t)plyr_idx, (int64_t)player->user_id);
            continue;
        }
        net_local_external_player[player->user_id] = plyr_idx;
    }
}

// user exists to the game layer (even if their connection has dropped,
// the game needs to hear about it through the proper channel first to avoid
// nondeterminism.)
TbBool user_present(NetUserId user)
{
    return get_net_user_player_number(user) >= 0;
}

void set_net_user_player_number(NetUserId user, PlayerNumber plyr_idx)
{
    if ((user < 0) || (user >= MAX_NET_USERS)) {
        return;
    }
    net_user_player_number[user] = plyr_idx;
}

static void setup_players_from_startup_packets(const struct StartupSyncPacket startup_sync_packets[MAX_NET_USERS])
{
    for (NetUserId i = 0; i < MAX_NET_USERS; i++) {
        const struct StartupSyncPacket *sync = &startup_sync_packets[i];
        if (!net_user_info[i].network_user_active) {
            continue;
        }
        int64_t k = get_net_user_player_number(i);
        if (k < 0) {
            continue;
        }
        struct PlayerInfo *player = get_player(k);
        player->id_number = k;
        player->user_id = i;
        player->allocflags |= PlaF_Allocated;
        switch (sync->video_rotate_mode) {
            case 0: player->view_mode_restore = PVM_IsoWibbleView; break;
            case 1: player->view_mode_restore = PVM_IsoStraightView; break;
            case 2: player->view_mode_restore = PVM_FrontView; break;
            default: player->view_mode_restore = PVM_IsoWibbleView; break;
        }
        player->is_active = 1;
        init_player(player, 0);
        init_user_state(player->user_id);
        player->isometric_view_zoom_level = sync->isometric_view_zoom_level;
        player->frontview_zoom_level = sync->frontview_zoom_level;
        TbBool imprison = (sync->initial_tendencies & CrTend_Imprison) != 0;
        TbBool flee = (sync->initial_tendencies & CrTend_Flee) != 0;
        set_creature_tendencies(player, CrTend_Imprison, imprison);
        set_creature_tendencies(player, CrTend_Flee, flee);
        if (player->id_number == my_player_number) {
            kfx_sim_state.creatures_tend_imprison = imprison;
            kfx_sim_state.creatures_tend_flee = flee;
        }
        snprintf(player->player_name, sizeof(struct TbNetworkPlayerName), "%s", network_user_name(i));
    }
}

static TbBool verify_map_checksums(const struct StartupSyncPacket startup_sync_packets[MAX_NET_USERS])
{
    const TbBigChecksum *host = startup_sync_packets[SERVER_ID].map_checksums;
    for (int64_t i = 0; i < MAX_NET_USERS; i++) {
        const TbBigChecksum *client = startup_sync_packets[i].map_checksums;
        if (!net_user_info[i].network_user_active) {
            continue;
        }
        int64_t diff_count = 0;
        for (int64_t j = 0; j < NETWORK_STARTUP_MAP_FILE_COUNT; j++) {
            if (client[j] == host[j]) {
                continue;
            }
            if (diff_count == 0) {
                ERRORLOG("Level checksums differ for player %" PRId64, (int64_t)(i));
            }
            ERRORLOG("Level file map%05" PRIu64 ".%s differs for player %" PRId64, (uint64_t)(sim_feedback->get_loaded_level_number()), network_startup_compare_files[j], (int64_t)(i));
            diff_count++;
        }
        if (diff_count != 0) {
            return false;
        }
    }
    NETLOG("Map checksums are verified");
    return true;
}

static TbBool verify_startup_sprite_zip_checksums(const struct StartupSyncPacket startup_sync_packets[MAX_NET_USERS])
{
    NetUserId host_user_id = SERVER_ID;
    const struct StartupSyncPacket *host_sync = &startup_sync_packets[host_user_id];
    TbBool verified = true;
    for (NetUserId i = 0; i < MAX_NET_USERS; i++) {
        if (!net_user_info[i].network_user_active || i == host_user_id) {
            continue;
        }
        const struct StartupSyncPacket *client_sync = &startup_sync_packets[i];
        for (int64_t zip_idx = 0; zip_idx < REQUIRED_SPRITE_ZIP_COUNT; zip_idx++) {
            if (client_sync->required_sprite_zip_checksums[zip_idx] == host_sync->required_sprite_zip_checksums[zip_idx]) {
                continue;
            }
            WARNLOG("Custom sprite zip differs between %s and %s: %s", network_user_name(host_user_id), network_user_name(i), required_sprite_zips[zip_idx]);
            {
                char msg_buf[128];
                snprintf(msg_buf, sizeof(msg_buf), "/fxdata/%.30s differs for %.12s", required_sprite_zips[zip_idx], network_user_name(i));
                sim_feedback->message_add(MsgType_Blank, 0, msg_buf);
            }
            verified = false;
        }
    }
    return verified;
}

static struct StartupSyncPacket s_local_startup_sync;
static struct StartupSyncPacket s_startup_sync_packets[MAX_NET_USERS];
static TbBool disconnect_victory_enabled[PLAYERS_COUNT];

static uint8_t calculate_initial_input_lag(void)
{
    int64_t player_count = 0;
    for (int64_t i = 0; i < MAX_NET_USERS; i++) {
        if (net_user_info[i].network_user_active) {
            player_count++;
        }
    }
    uint64_t ping = network_lobby_ping;
    if (player_count == 2) {
        ping /= 2;
    }
    uint64_t input_lag_turns = 0;
    if (kfx_sim_state.turns_per_second > 0) {
        input_lag_turns = (ping * kfx_sim_state.turns_per_second + 999) / 1000;
    }
    uint64_t uncapped_input_lag_turns = input_lag_turns;
    if (input_lag_turns > 0) {
        input_lag_turns -= 1;
    }
    if (input_lag_turns > MAXIMUM_INPUT_LAG_TURNS) {
        input_lag_turns = MAXIMUM_INPUT_LAG_TURNS;
    }
    JUSTLOG("Initial input lag: (%llu ms * %" PRId64 " turns/s + 999) / 1000 = %llu turns, adjusted to %llu", (unsigned long long)ping, (int64_t)(kfx_sim_state.turns_per_second), (unsigned long long)uncapped_input_lag_turns, (unsigned long long)input_lag_turns);
    return input_lag_turns;
}

static void build_local_startup_sync(void)
{
    memset(&s_local_startup_sync, 0, sizeof(s_local_startup_sync));
    s_local_startup_sync.startup_sync_packet_valid = 1;
    s_local_startup_sync.video_rotate_mode = settings.video_rotate_mode;
    calculate_network_startup_map_checksums(s_local_startup_sync.map_checksums);
    memcpy(s_local_startup_sync.required_sprite_zip_checksums, required_sprite_zip_checksums, sizeof(s_local_startup_sync.required_sprite_zip_checksums));
    int64_t initial_tendencies = 0;
    if (IMPRISON_BUTTON_DEFAULT) {initial_tendencies |= CrTend_Imprison;}
    if (FLEE_BUTTON_DEFAULT) {initial_tendencies |= CrTend_Flee;}
    s_local_startup_sync.initial_tendencies = initial_tendencies;
    s_local_startup_sync.isometric_view_zoom_level = settings.isometric_view_zoom_level;
    s_local_startup_sync.frontview_zoom_level = settings.frontview_zoom_level;
    s_local_startup_sync.zoom_distance_setting = kfx_config_state.zoom_distance_setting;
    s_local_startup_sync.frontview_zoom_distance_setting = kfx_config_state.frontview_zoom_distance_setting;
    s_local_startup_sync.initial_input_lag_turns = calculate_initial_input_lag();
}

static TbBool net_startup_sync_exchange_and_apply(void)
{
    memset(s_startup_sync_packets, 0, sizeof(s_startup_sync_packets));
    if (exchange_frame_block(NETMSG_STARTUP_SYNC, &s_local_startup_sync, s_startup_sync_packets, sizeof(struct StartupSyncPacket)) != Lb_OK) {
        ERRORLOG("Startup sync exchange failed");
        return false;
    }

    for (int64_t i = 0; i < MAX_NET_USERS; i++) {
        if (net_user_info[i].network_user_active && !s_startup_sync_packets[i].startup_sync_packet_valid) {
            ERRORLOG("Startup sync exchange missed one or more peers");
            return false;
        }
    }
    if (!verify_map_checksums(s_startup_sync_packets)) {
        net_callbacks->create_frontend_error_box(5000, get_string(GUIStr_NetUnsyncedMap));
        return false;
    }

    if (!verify_startup_sprite_zip_checksums(s_startup_sync_packets)) {
        net_callbacks->create_frontend_error_box(5000, get_string(GUIStr_NetVerifyFxdataSame));
        return false;
    }
    const struct StartupSyncPacket *host_sync = &s_startup_sync_packets[SERVER_ID];
    kfx_net_state.input_lag_turns = host_sync->initial_input_lag_turns;
    input_lag_reset();
    kfx_net_state.skip_initial_input_turns = calculate_skip_input();
    NETLOG("Startup input lag: %" PRId64, (int64_t)(kfx_net_state.input_lag_turns));
    kfx_config_state.zoom_distance_setting = host_sync->zoom_distance_setting;
    kfx_config_state.frontview_zoom_distance_setting = host_sync->frontview_zoom_distance_setting;
    setup_players_from_startup_packets(s_startup_sync_packets);
    return true;
}

void setup_network_player_numbers(void)
{
    TbBool is_set = false;
    int64_t k = 0;
    SYNCDBG(6, "Starting");
    for (NetUserId i = 0; i < MAX_NET_USERS; i++)
    {
        net_user_player_number[i] = -1;
        if (net_user_info[i].network_user_active)
        {
            net_user_player_number[i] = k;
            if ((!is_set) && (my_player_number == i))
            {
                is_set = true;
                my_player_number = k;
            }
            k++;
        }
    }
    if (!is_set) {
        ERRORLOG("Local player number %" PRId64 " not found among active network players", (int64_t)(my_player_number));
    }
}

void setup_count_players(void)
{
  if (kfx_sim_state.game_kind == GKind_LocalGame)
  {
    kfx_net_state.active_players_count = 1;
  } else
  {
    kfx_net_state.active_players_count = 0;
    for (int64_t i = 0; i < MAX_NET_USERS; i++)
    {
      if (net_user_info[i].network_user_active)
        kfx_net_state.active_players_count++;
    }
  }
}

TbBool init_players_network_game(void)
{
    SYNCDBG(4,"Starting");
    TbBool initialized = true;
    setup_network_player_numbers();
    for (int64_t zip_idx = 0; zip_idx < REQUIRED_SPRITE_ZIP_COUNT; zip_idx++) {
        if (required_sprite_zip_checksums[zip_idx] != 0) {
            continue;
        }
        WARNLOG("Required custom sprite zip missing: %s", required_sprite_zips[zip_idx]);
        {
            char msg_buf[128];
            snprintf(msg_buf, sizeof(msg_buf), "/fxdata/%.30s missing", required_sprite_zips[zip_idx]);
            sim_feedback->message_add(MsgType_Blank, 0, msg_buf);
        }
        net_callbacks->create_frontend_error_box(5000, get_string(GUIStr_NetVerifyFxdataSame));
        initialized = false;
        break;
    }
    if (initialized) {
        build_local_startup_sync();
        initialized = net_startup_sync_exchange_and_apply();
    }
    if (initialized && netstate.my_id == SERVER_ID && net_callbacks->frontnet_service_selected(FrontendNetSvc_Online)) {
        LevelNumber map_number = sim_feedback->get_level_number();
        struct LevelInformation *level_info = get_level_info(map_number);
        const char *map_name = "";
        if (level_info) {
            map_name = level_info->name;
            if (level_info->name_stridx > 0) {
                map_name = get_string(level_info->name_stridx);
            }
        }
        matchmaking_close_lobby(MMLobbyResult_Started, (int64_t)map_number, map_name);
    }
    if (!initialized) {
        LbNetwork_Stop();
    }
    return initialized;
}

void are_disconnect_victories_allowed(void)
{
    for (PlayerNumber plyr_idx = 0; plyr_idx < PLAYERS_COUNT; plyr_idx++) {
        disconnect_victory_enabled[plyr_idx] = false;
        if (!player_exists(get_player(plyr_idx))) {
            continue;
        }
        for (int64_t other_idx = 0; other_idx < kfx_net_state.active_players_count; other_idx++) {
            struct PlayerInfo *other = get_player(other_idx);
            if (player_exists(other) && (other_idx != plyr_idx) && players_are_enemies(plyr_idx, other->id_number)) {
                disconnect_victory_enabled[plyr_idx] = true;
                break;
            }
        }
    }
}

/** Check whether a network user is active.
 *
 * @param user
 * @return
 */
TbBool network_user_active(NetUserId user)
{
    if ((user < 0) || (user >= MAX_NET_USERS))
        return false;
    return (net_user_info[user].network_user_active != 0);
}

const char *network_user_name(NetUserId user)
{
    if ((user < 0) || (user >= MAX_NET_USERS))
        return NULL;
    return net_user_info[user].name;
}

static void resolve_network_quit_outcome(struct PlayerInfo *player)
{
    if (player->victory_state != VicS_Undecided) {
        return;
    }
    if (player_cannot_win(player->id_number)) {
        set_player_as_lost_level(player);
        return;
    }
    set_player_as_won_level(player);
}

TbBool player_has_enemies_to_defeat(const struct PlayerInfo *player)
{
    for (int64_t i = 0; i < PLAYERS_COUNT; i++) {
        struct PlayerInfo *other = get_player(i);
        TbBool is_active_enemy = player_exists(other) && (other != player) && other->is_active == 1 && !player_cannot_win(other->id_number) && players_are_enemies(player->id_number, other->id_number);
        TbBool is_human_driven = (other->allocflags & PlaF_CompCtrl) == 0 && user_present(other->user_id);
        TbBool is_initial_computer_player = (other->allocflags & PlaF_CompCtrl) != 0 && i >= kfx_net_state.active_players_count;
        if (is_active_enemy && (is_human_driven || is_initial_computer_player)) {
            return true;
        }
    }
    return false;
}

TbBool network_human_contenders_remain(void)
{
    for (PlayerNumber player_idx = 0; player_idx < PLAYERS_COUNT; player_idx++) {
        struct PlayerInfo *player = get_player(player_idx);
        if (player_exists(player) && (player->is_active == 1) && ((player->allocflags & PlaF_CompCtrl) == 0) && !player_cannot_win(player_idx)) {
            return true;
        }
    }
    return false;
}

static TbBool network_has_remote_users_remaining(void)
{
    for (NetUserId user_id = 0; user_id < MAX_NET_USERS; user_id += 1) {
        if (user_id == netstate.my_id) {
            continue;
        }
        if ((user_id < (NetUserId)netstate.max_users) && (netstate.users[user_id].progress != USER_UNUSED)) {
            return true;
        }
        if (user_present(user_id)) {
            const struct PlayerInfo *player = get_player(get_net_user_player_number(user_id));
            if (player_exists(player) && ((player->allocflags & PlaF_CompCtrl) == 0)) {
                return true;
            }
        }
    }
    return false;
}

static void replace_network_player_with_ai(struct PlayerInfo *player)
{
    player->allocflags |= PlaF_CompCtrl;
    toggle_computer_player(player->id_number);
    sim_feedback->message_add(MsgType_Player, player->id_number, get_string(GUIStr_NetAiTookOver));
    JUSTLOG("p:%" PRId64 " computer took over", (int64_t)(player->id_number));
}

// used when ending a netplay game or recording.
// local single-player must have the local user in slot 0.
void remap_local_user_to_solo(void)
{
    struct PlayerInfo *myplyr = get_my_player();
    NetUserId old_user = myplyr->user_id;
    for (NetUserId user = 0; user < MAX_NET_USERS; user++) {
        if (user == old_user) {
            continue;
        }
        struct UserState *ustate = get_user_state(user);
        if (ustate->cursor_light_idx != 0) {
            light_delete_light(ustate->cursor_light_idx);
        }
        memset(ustate, 0, sizeof(*ustate));
    }
    struct UserState *old_state = get_user_state(old_user);
    if ((old_user != SOLO_HUMAN_ID) && !user_state_invalid(old_state)) {
        *get_user_state(SOLO_HUMAN_ID) = *old_state;
        memset(old_state, 0, sizeof(*old_state));
    }
    for (PlayerNumber plyr_idx = 0; plyr_idx < PLAYERS_COUNT; plyr_idx++) {
        struct PlayerInfo *player = get_player(plyr_idx);
        if (player != myplyr) {
            player->user_id = -1;
        }
    }
    for (NetUserId user = 0; user < MAX_NET_USERS; user++) {
        set_net_user_player_number(user, -1);
    }
    myplyr->user_id = SOLO_HUMAN_ID;
    set_net_user_player_number(SOLO_HUMAN_ID, myplyr->id_number);
    if (myplyr->roomspace.is_active && (myplyr->roomspace.user == old_user)) {
        myplyr->roomspace.user = SOLO_HUMAN_ID;
    }
}

static void stop_network_game_state(void)
{
    memset(net_user_info, 0, sizeof(net_user_info));
    clear_flag(kfx_sim_state.system_flags, GSF_NetworkActive);
    remap_local_user_to_solo();
    clear_flag(kfx_sim_state.system_flags, GSF_NetGameNoSync);
    clear_flag(kfx_sim_state.system_flags, GSF_NetSeedNoSync);
    fe_network_active = 0;
    kfx_sim_state.game_kind = GKind_LocalGame;
    kfx_net_state.input_lag_turns = 0;
    kfx_net_state.skip_initial_input_turns = 0;
    input_lag_reset();
    multiplayer_speed_adjustment_ns = 0;
    setup_count_players();
}

static void stop_network_game_and_quit_to_main_menu(void)
{
    LbNetwork_Stop();
    stop_network_game_state();
    quit_game = 1;
}

static void stop_network_game_and_continue_locally(void)
{
    LbNetwork_Stop();
    stop_network_game_state();
    get_my_player()->display_objective_turn = get_gameturn() + 1;
}

static TbBool host_already_won_level(void)
{
    GameTurn newest_turn = get_gameturn();
    for (GameTurnDelta offset = 0; offset <= kfx_net_state.input_lag_turns; offset += 1) {
        if ((GameTurn)offset > newest_turn) {
            break;
        }
        const struct Packet *host_packet = get_history_packet(SERVER_ID, newest_turn - offset);
        if (host_packet != NULL && host_packet->action == PckA_FinishGame && host_packet->actn_par1 == VicS_WonLevel) {
            return true;
        }
    }
    return false;
}

static void resolve_disconnect_victories(struct PlayerInfo *departed)
{
    for (PlayerNumber plyr_idx = 0; plyr_idx < PLAYERS_COUNT; plyr_idx++) {
        struct PlayerInfo *player = get_player(plyr_idx);
        if (!player_exists(player) || (player == departed) || (player->is_active != 1) || ((player->allocflags & PlaF_CompCtrl) != 0)) {
            continue;
        }
        if (!disconnect_victory_enabled[plyr_idx] || !players_are_enemies(plyr_idx, departed->id_number)) {
            continue;
        }
        if (player_has_enemies_to_defeat(player)) {
            continue;
        }
        int64_t plyr_count = 0;
        TbBool winning_quit = net_callbacks->winning_player_quitting(departed, &plyr_count);
        if (winning_quit) {
            for (int64_t i = 0; i < PLAYERS_COUNT; i++) {
                struct PlayerInfo *swplyr = get_player(i);
                if (player_exists(swplyr) && (swplyr->is_active == 1)) {
                    resolve_network_quit_outcome(swplyr);
                }
            }
        }
        resolve_network_quit_outcome(player);
        if (winning_quit && (plyr_count > 1) && is_my_player(player)) {
            struct UserState *ustate = get_local_user_state();
            if (kfx_config_state.conf.rules[plyr_idx].gameplay.winner_tortures_loser) {
                ustate->additional_flags |= UsrAF_UnlockedLordTorture;
            } else {
                ustate->additional_flags &= ~UsrAF_UnlockedLordTorture;
            }
        }
    }
}

static void abandon_network_player(struct PlayerInfo *player, TbBool announce)
{
    if ((player->allocflags & PlaF_CompCtrl) == 0) {
        if (network_is_active()) {
            // re-negotiate input latency
            network_lobby_ping = GetPing(my_player_number, my_player_number);
            input_lag_reset_request(calculate_initial_input_lag());
        }
        if (announce && player->player_name[0] != '\0') {
            sim_feedback->message_add_fmt(MsgType_Blank, 0, get_string(GUIStr_NetPlayerDisconnected), player->player_name);
        }
        JUSTLOG("p:%" PRId64 " player %s departed", (int64_t)(player->id_number), player->player_name);
        if (player->victory_state == VicS_Undecided) {
            replace_network_player_with_ai(player);
        }
    }
    resolve_disconnect_victories(player);
    if (player->victory_state != VicS_Undecided) {
        player->allocflags &= ~PlaF_Allocated;
    }
}

static void remove_user_from_game(NetUserId user, TbBool announce)
{
    if ((user < 0) || (user >= MAX_NET_USERS) || (net_user_player_number[user] < 0)) {
        return;
    }
    struct PlayerInfo *player = get_player(net_user_player_number[user]);
    JUSTLOG("u:%" PRId64 " user left the game (player %" PRId64 ")", (int64_t)user, (int64_t)net_user_player_number[user]);
    net_user_player_number[user] = -1;
    if (!player_exists(player)) {
        return;
    }
    player->user_id = -1;
    abandon_network_player(player, announce);
}

static void leave_network_if_alone(void)
{
    if (!network_has_remote_users_remaining()) {
        stop_network_game_and_continue_locally();
    }
}

void process_player_leave_game_packet(struct PlayerInfo *player)
{
    if (player != get_my_player()) {
        if (network_is_active() || kfx_net_state.packet_load_enable /* handle replays */) {
            NetUserId user = player->user_id;
            if (network_is_active()) {
                OnDroppedUser(user, NETDROP_MANUAL);
            }
            remove_user_from_game(user, user != SERVER_ID);
            if (network_is_active()) {
                leave_network_if_alone();
            }
            return;
        }
    } else if (network_is_active()) {
        stop_network_game_and_quit_to_main_menu();
    } else {
        quit_game = 1;
    }
    player->allocflags &= ~PlaF_Allocated;
}

// (host-only) host sends packets for dropped users, indicating
// the user has dropped.
void host_spoof_dropped_user_packets(void)
{
    if (!network_is_active() || !network_is_host()) {
        return;
    }
    GameTurn first_turn = get_gameturn();
    if (first_turn > (GameTurn)kfx_net_state.input_lag_turns) {
        first_turn -= kfx_net_state.input_lag_turns;
    }
    for (NetUserId user = 0; user < MAX_NET_USERS; user++) {
        if ((user == netstate.my_id) || network_user_active(user) || !user_present(user)) {
            continue;
        }
        struct Packet spoofed;
        memset(&spoofed, 0, sizeof(spoofed));
        set_packet_action(&spoofed, PckA_ForceApplicationClose, 0, 0, 0, 0);
        // only the turns user_has_required_turn_packets expects
        for (GameTurn turn = first_turn; turn < first_turn + 2; turn++) {
            if (get_history_packet(user, turn) != NULL) {
                continue;
            }
            spoofed.turn = turn;
            store_packet_history(user, &spoofed);
        }
    }
}

void process_disconnected_network_players(void)
{
    if (!network_is_active() || network_is_host() || (netstate.users[SERVER_ID].progress != USER_UNUSED)) {
        return;
    }
    struct UserState *ustate = get_local_user_state();
    if (host_already_won_level()) {
        ustate->additional_flags &= ~UsrAF_UnlockedLordTorture;
        quit_game = 1;
        return;
    }
    sim_feedback->message_add(MsgType_Blank, 0, get_string(GUIStr_NetHostConnectionLost));
    for (NetUserId user = 0; user < MAX_NET_USERS; user++) {
        if (user != netstate.my_id) {
            remove_user_from_game(user, false);
        }
    }
    stop_network_game_and_continue_locally();
}

int64_t network_session_join(void)
{
    int64_t plyr_num;
    net_callbacks->reset_attempting_to_join_cancel();
    net_callbacks->display_attempting_to_join_message(-1);
    if (net_callbacks->attempting_to_join_cancel_requested())
        return -1;
    bf_enet_set_join_lobby_id(net_session[net_session_index_active]->join_address);
    if (LbNetwork_Join(net_session[net_session_index_active], net_player_name, &plyr_num, NULL) == 0)
        return plyr_num;
    bf_enet_set_join_lobby_id("");
    if (!net_callbacks->attempting_to_join_cancel_requested()) {
        if (net_callbacks->frontnet_service_selected(FrontendNetSvc_Online)) {
            net_session_index_active = -1;
            net_session_index_active_id = -1;
            matchmaking_request_list();
        }
        net_callbacks->process_network_error(-802);
    }
    return -1;
}

void sync_initial_network_seed(void)
{
   if (!network_is_active()) {
      return;
   }
   if (!LbNetwork_Resync(&kfx_sim_state.action_random_seed, sizeof(kfx_sim_state.action_random_seed))) {
      ERRORLOG("Initial sync failed");
      return;
   }
   kfx_sim_state.ai_random_seed = kfx_sim_state.action_random_seed * 9377 + 9391;
   kfx_sim_state.player_random_seed = kfx_sim_state.action_random_seed * 9473 + 9479;
   NETLOG("Initial network seed synced: action_seed=%" PRIu64, (uint64_t)(kfx_sim_state.action_random_seed));
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
