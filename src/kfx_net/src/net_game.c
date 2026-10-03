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
#include "light_registry.h"
#include "packets.h"
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
#include "ports/ui_port.h"
#include "ports/game_port.h"
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
    TbBigChecksum map_checksums[NETWORK_STARTUP_MAP_FILE_COUNT];
    TbBigChecksum required_sprite_zip_checksums[REQUIRED_SPRITE_ZIP_COUNT];
    struct UserStartSettings user_start;
    uint8_t initial_input_lag_turns;
    uint32_t initial_action_seed;
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
    dst->direct_ipv4_port = src->direct_ipv4_port;
}

static int64_t enet_services_matchmaking_punch(const char *lobby_id, const char *udp_ipv4, int64_t udp_ipv4_port, int64_t udp_ipv6_port, struct EnetPunchAddresses *output)
{
    PunchAddresses real_output;
    int64_t result = matchmaking_punch(lobby_id, udp_ipv4, udp_ipv4_port, udp_ipv6_port, &real_output);
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
// target the ui_*() port wrappers -- those are runtime indirections, not
// usable directly in a static initializer.
static void enet_services_display_attempting_to_join_message(int64_t seconds_remaining)
{
    ui_display_attempting_to_join_message(seconds_remaining);
}

static TbBool enet_services_attempting_to_join_cancel_requested(void)
{
    return ui_attempting_to_join_cancel_requested();
}

static const struct EnetConnectivityServices enet_connectivity_services = {
    .display_attempting_to_join_message = &enet_services_display_attempting_to_join_message,
    .attempting_to_join_cancel_requested = &enet_services_attempting_to_join_cancel_requested,
    .holepunch_stun_query = &holepunch_stun_query,
    .holepunch_punch_to = &holepunch_punch_to,
    .holepunch_handle_packet = &holepunch_handle_packet,
    .holepunch_stun_keepalive = &holepunch_stun_keepalive,
    .matchmaking_punch = &enet_services_matchmaking_punch,
    .matchmaking_poll_punch = &enet_services_matchmaking_poll_punch,
    .port_forward_set_mapping = &port_forward_set_mapping,
};

int64_t setup_network_service(enum FrontendNetService service)
{
  struct ServiceInitData *init_data = NULL;
  SYNCMSG("Initializing 4-players type %" PRId64 " network", (int64_t)(service));
  memset(net_user_info, 0, sizeof(net_user_info));
  network_lobby_ping = 0;
  if (service != FrontendNetSvc_Online && service != FrontendNetSvc_LAN) {
    ui_process_network_error(-800);
    return 0;
  }
  bf_enet_set_connectivity_services(&enet_connectivity_services);
  if ( LbNetwork_Init(NS_ENET_UDP, MAX_NET_USERS, &net_user_info[0], init_data) )
  {
    ui_process_network_error(-800);
    return 0;
  }
  net_service_index_selected = service;
  ui_set_lobby_button_labels(service == FrontendNetSvc_LAN);
  ui_enter_net_session_screen();
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
    if (!network_is_active() && !replay.load_enable) {
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
    if (network_is_active() || (plyr_idx < 0) || (plyr_idx >= PLAYERS_COUNT)) {
        return -1;
    }
    struct PlayerInfo *player = get_player(plyr_idx);
    const TbBool exists = player_exists(player);
    // The local player's own seat is the one deliberate exception to "a human seat can't be claimed": they are
    // handing over their own dungeon, not someone else's. process_packets() (kfx_game's game_commands.c) is what keeps this
    // safe once claimed -- PVT_DungeonTop's dispatch for a PlaF_ExternalSeat player only accepts packets from
    // the seat's own user_id, so the local human's own leftover front_input.c packet (still generated every
    // frame regardless of seat type) can no longer act on it.
    if (exists && !flag_is_set(player->allocflags, PlaF_CompCtrl) && (plyr_idx != my_player_number)) {
        return -1; // a human other than the local player, or already an External seat
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
            // A bare memset (the previous code here) leaves every ComputerProcess entry zeroed rather than
            // populated from a real template -- harmless while computer_enabled stays off (just above), but a
            // latent crash (a null process func pointer) waiting for whatever next turns that bit back on for
            // this player, e.g. the co-op "computer assistant" option, still reachable for an External seat
            // since it is a global action, not a dungeon-control one (packets.c). setup_a_computer_player()
            // does the same memset plus populates a real (if never-ticked) process list.
            setup_a_computer_player(plyr_idx, comp_player_conf.player_assist_default);
        }
    }
    player->id_number = plyr_idx;
    player->user_id = user;
    set_flag(player->allocflags, PlaF_Allocated | PlaF_ExternalSeat);
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

TbBool keeper_name_is_set(void)
{
    return (net_player_name[0] != '\0') && (strcmp(net_player_name, get_string(GUIStr_MnuNoName)) != 0);
}

void net_apply_keeper_name(PlayerNumber plyr_idx)
{
    if ((plyr_idx < 0) || (plyr_idx >= PLAYERS_COUNT) || !keeper_name_is_set()) {
        return;
    }
    struct PlayerInfo *player = get_player(plyr_idx);
    snprintf(player->player_name, sizeof(player->player_name), "%s", net_player_name);
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
    if (plyr_idx == my_player_number) {
        // The local human reclaims their own seat: never hand it to the built-in AI --
        // script_support_setup_player_as_computer_keeper is for releasing a RIVAL back to being an idle AI
        // keeper, which is backwards for the human's own seat, and (like player_enter_spectator_mode's own
        // comment on the same hazard) unsafe to call on a live, already-explored dungeon regardless.
        SYNCLOG("External seat released: player %" PRId64 " is human-controlled again", (int64_t)plyr_idx);
        return true;
    }
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

void build_local_user_start_settings(struct UserStartSettings *us)
{
    memset(us, 0, sizeof(*us));
    us->video_rotate_mode = settings.video_rotate_mode;
    if (IMPRISON_BUTTON_DEFAULT)
        us->tendencies |= CrTend_Imprison;
    if (FLEE_BUTTON_DEFAULT)
        us->tendencies |= CrTend_Flee;
    us->isometric_view_zoom_level = settings.isometric_view_zoom_level;
    us->frontview_zoom_level = settings.frontview_zoom_level;
    us->zoom_distance = kfx_config_state.zoom_distance_setting;
    us->frontview_zoom_distance = kfx_config_state.frontview_zoom_distance_setting;
    if (kfx_sim_state.easter_eggs_enabled)
        us->flags |= USF_CheatsEnabled;
    if (get_skip_heart_zoom_feature())
        us->flags |= USF_SkipHeartZoom;
    us->highlight_mode = (keeperfx_ui_config.default_tag_mode != 3) ? keeperfx_ui_config.default_tag_mode - 1 : settings.highlight_mode;
    us->isometric_tilt = settings.isometric_tilt;
}

void apply_user_start_settings(struct PlayerInfo *player, const struct UserStartSettings *us, const struct UserStartSettings *host)
{
    player->view_mode_restore = rotate_mode_to_view_mode(us->video_rotate_mode);
    player->isometric_view_zoom_level = us->isometric_view_zoom_level;
    player->frontview_zoom_level = us->frontview_zoom_level;
    player->zoom_distance = us->zoom_distance;
    player->frontview_zoom_distance = us->frontview_zoom_distance;
    player->cheats_allowed = ((us->flags & USF_CheatsEnabled) != 0) && ((host->flags & USF_CheatsEnabled) != 0);
    player->skip_heart_zoom = ((us->flags & USF_SkipHeartZoom) != 0) && ((host->flags & USF_SkipHeartZoom) != 0);
    player->highlight_mode = us->highlight_mode;
    player->roomspace_highlight_mode = us->highlight_mode;
    player->roomspace_mode = us->highlight_mode;
    struct Camera *iso_cam = &player->cameras[CamIV_Isometric];
    iso_cam->rotation_angle_y = us->isometric_tilt;
    iso_cam->view_mode = (us->video_rotate_mode == 1) ? PVM_IsoStraightView : PVM_IsoWibbleView;
    iso_cam->zoom = us->isometric_view_zoom_level;
    player->cameras[CamIV_FrontView].zoom = us->frontview_zoom_level;
    TbBool imprison = (us->tendencies & CrTend_Imprison) != 0;
    TbBool flee = (us->tendencies & CrTend_Flee) != 0;
    set_creature_tendencies(player, CrTend_Imprison, imprison);
    set_creature_tendencies(player, CrTend_Flee, flee);
    if (player->id_number == my_player_number) {
        kfx_sim_state.creatures_tend_imprison = imprison;
        kfx_sim_state.creatures_tend_flee = flee;
    }
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
        player->view_mode_restore = rotate_mode_to_view_mode(sync->user_start.video_rotate_mode);
        init_player(player, 0);
        init_user_state(player->user_id);
        apply_user_start_settings(player, &sync->user_start, &startup_sync_packets[SERVER_ID].user_start);
        if (player->id_number == my_player_number) {
            my_local_user_id = i;
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
            ERRORLOG("Level file map%05" PRIu64 ".%s differs for player %" PRId64, (uint64_t)(get_loaded_level_number()), network_startup_compare_files[j], (int64_t)(i));
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
                ui_message_add(MsgType_Blank, 0, msg_buf);
            }
            verified = false;
        }
    }
    return verified;
}

static struct StartupSyncPacket s_local_startup_sync;
static struct StartupSyncPacket s_startup_sync_packets[MAX_NET_USERS];

// the settings a user sent in the startup sync, for network games
TbBool get_startup_user_settings(NetUserId user, struct UserStartSettings *us)
{
    if (!network_is_active() || (user < 0) || (user >= MAX_NET_USERS) || !s_startup_sync_packets[user].startup_sync_packet_valid)
        return false;
    *us = s_startup_sync_packets[user].user_start;
    return true;
}



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
    calculate_network_startup_map_checksums(s_local_startup_sync.map_checksums);
    memcpy(s_local_startup_sync.required_sprite_zip_checksums, required_sprite_zip_checksums, sizeof(s_local_startup_sync.required_sprite_zip_checksums));
    build_local_user_start_settings(&s_local_startup_sync.user_start);
    s_local_startup_sync.initial_input_lag_turns = calculate_initial_input_lag();
    s_local_startup_sync.initial_action_seed = (uint32_t)kfx_net_local.initial_replay_seed;
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
        ui_create_frontend_error_box(5000, get_string(GUIStr_NetUnsyncedMap));
        return false;
    }

    if (!verify_startup_sprite_zip_checksums(s_startup_sync_packets)) {
        ui_create_frontend_error_box(5000, get_string(GUIStr_NetVerifyFxdataSame));
        return false;
    }
    const struct StartupSyncPacket *host_sync = &s_startup_sync_packets[SERVER_ID];
    kfx_net_state.input_lag_turns = host_sync->initial_input_lag_turns;
    input_lag_reset();
    kfx_net_state.skip_initial_input_turns = calculate_skip_input();
    NETLOG("Startup input lag: %" PRId64, (int64_t)(kfx_net_state.input_lag_turns));
    if (host_sync->initial_action_seed != (uint32_t)kfx_net_local.initial_replay_seed)
    {
        ERRORLOG("Initial action seed %" PRIu64 " differs from host's %" PRIu64, (uint64_t)kfx_net_local.initial_replay_seed, (uint64_t)host_sync->initial_action_seed);
        kfx_net_local.initial_replay_seed = host_sync->initial_action_seed;
    }
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
    kfx_sim_state.human_players_count = 1;
  } else
  {
    kfx_sim_state.human_players_count = 0;
    for (int64_t i = 0; i < MAX_NET_USERS; i++)
    {
      if (net_user_info[i].network_user_active)
        kfx_sim_state.human_players_count++;
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
            ui_message_add(MsgType_Blank, 0, msg_buf);
        }
        ui_create_frontend_error_box(5000, get_string(GUIStr_NetVerifyFxdataSame));
        initialized = false;
        break;
    }
    if (initialized) {
        build_local_startup_sync();
        initialized = net_startup_sync_exchange_and_apply();
    }
    if (initialized) {
        net_lobby_set_phase(NetPhase_InGame);
    }
    if (initialized && netstate.my_id == SERVER_ID && ui_frontnet_service_selected(FrontendNetSvc_Online)) {
        LevelNumber map_number = get_level_number();
        struct LevelInformation *level_info = get_level_info(map_number);
        const char *map_name = "";
        if (level_info) {
            map_name = level_info->name;
            if (level_info->name_stridx > 0) {
                map_name = get_string(level_info->name_stridx);
            }
        }
        matchmaking_start_game((int64_t)map_number, map_name);
    }
    if (!initialized) {
        LbNetwork_Stop();
    }
    return initialized;
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

TbBool network_human_contenders_remain(void)
{
    for (PlayerNumber player_idx = 0; player_idx < PLAYERS_COUNT; player_idx++) {
        struct PlayerInfo *player = get_player(player_idx);
        if (is_active_keeper(player) && ((player->allocflags & PlaF_CompCtrl) == 0) && !player_cannot_win(player_idx)) {
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

static TbBool replay_has_remote_humans(void)
{
    const NetUserId local_user = replay.head.recording_user;
    for (NetUserId user_id = 0; user_id < MAX_NET_USERS; user_id++) {
        const PlayerNumber plyr_idx = get_net_user_player_number(user_id);
        if ((user_id == local_user) || (plyr_idx < 0)) {
            continue;
        }
        const struct PlayerInfo *player = get_player(plyr_idx);
        if (player_exists(player) && ((player->allocflags & PlaF_CompCtrl) == 0)) {
            return true;
        }
    }
    return false;
}

static void replace_network_player_with_ai(struct PlayerInfo *player)
{
    player->allocflags |= PlaF_CompCtrl | PlaF_Placeholder;
    toggle_computer_player(player->id_number);
    ui_message_add(MsgType_Player, player->id_number, get_string(GUIStr_NetAiTookOver));
    JUSTLOG("p:%" PRId64 " computer took over", (int64_t)(player->id_number));
}

// used when ending a netplay game or recording.
// local single-player must have the local user in slot 0.
void remap_user_to_solo(struct PlayerInfo *myplyr)
{
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
    clear_flag(local_system_flags, GSF_NetworkActive);
    remap_user_to_solo(get_my_player());
    clear_flag(local_system_flags, GSF_NetGameNoSync);
    clear_flag(local_system_flags, GSF_NetSeedNoSync);
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
    struct PlayerInfo *survivor;
    if (network_is_active()) {
        LbNetwork_Stop();
        stop_network_game_state();
        survivor = get_my_player();
    } else {
        const PlayerNumber plyr_idx = get_net_user_player_number(replay.head.recording_user);
        survivor = (plyr_idx >= 0) ? get_player(plyr_idx) : get_my_player();
        remap_user_to_solo(survivor);
        kfx_sim_state.game_kind = GKind_LocalGame;
        setup_count_players();
    }
    survivor->display_objective_turn = get_gameturn() + 1;
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

static void abandon_network_player(struct PlayerInfo *player, TbBool announce)
{
    if ((player->allocflags & PlaF_CompCtrl) == 0) {
        if (network_is_active()) {
            // re-negotiate input latency
            network_lobby_ping = GetPing(my_player_number, my_player_number);
            input_lag_reset_request(calculate_initial_input_lag());
        }
        if (announce && player->player_name[0] != '\0') {
            ui_message_add_fmt(MsgType_Blank, 0, get_string(GUIStr_NetPlayerDisconnected), player->player_name);
        }
        JUSTLOG("p:%" PRId64 " player %s departed", (int64_t)(player->id_number), player->player_name);
        if (player->victory_state == VicS_Undecided) {
            replace_network_player_with_ai(player);
        }
    }
    resolve_placeholders();
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
        if (kfx_sim_state.game_kind == GKind_MultiGame) {
            NetUserId user = player->user_id;
            if (network_is_active()) {
                OnDroppedUser(user, NETDROP_MANUAL);
            }
            remove_user_from_game(user, user != SERVER_ID);
            if (network_is_active()) {
                leave_network_if_alone();
            } else if (replay.load_enable && !replay_has_remote_humans()) {
                stop_network_game_and_continue_locally();
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
    ui_message_add(MsgType_Blank, 0, get_string(GUIStr_NetHostConnectionLost));
    for (NetUserId user = 0; user < MAX_NET_USERS; user++) {
        if (user != netstate.my_id) {
            remove_user_from_game(user, false);
        }
    }
    game_replay_record_network_stopped();
    stop_network_game_and_continue_locally();
}

void apply_recorded_network_stop(void)
{
    ui_message_add(MsgType_Blank, 0, get_string(GUIStr_NetHostConnectionLost));
    const NetUserId local_user = replay.head.recording_user;
    for (NetUserId user = 0; user < MAX_NET_USERS; user++) {
        if (user != local_user) {
            remove_user_from_game(user, false);
        }
    }
    stop_network_game_and_continue_locally();
}

int64_t network_session_join(void)
{
    int64_t plyr_num;
    net_join_rejection = NetJoin_Accepted;
    ui_reset_attempting_to_join_cancel();
    ui_display_attempting_to_join_message(-1);
    if (ui_attempting_to_join_cancel_requested())
        return -1;
    bf_enet_set_join_lobby_id(net_session[net_session_index_active]->join_address);
    if (LbNetwork_Join(net_session[net_session_index_active], net_player_name, &plyr_num, NULL) == 0)
        return plyr_num;
    bf_enet_set_join_lobby_id("");
    if (!ui_attempting_to_join_cancel_requested()) {
        if (ui_frontnet_service_selected(FrontendNetSvc_Online)) {
            net_session_index_active = -1;
            net_session_index_active_id = -1;
            matchmaking_request_list();
        }
        const char *error = net_join_error_text(net_join_rejection);
        if (error) {
            ui_create_frontend_error_box(5000, error);
        } else {
            ui_process_network_error(-802);
        }
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
   kfx_net_local.initial_replay_seed = kfx_sim_state.action_random_seed;
   NETLOG("Initial network seed synced: action_seed=%" PRIu64, (uint64_t)(kfx_sim_state.action_random_seed));
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
