/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file packets_misc.c
 *     Packet storage and the local pause command.
 * @par Purpose:
 *     clear_packets() and set_packet_pause_toggle(). The replay file I/O that
 *     used to live here is kfx_game's game_replay.c (refactor pass 2, S12).
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     20 Sep 2020 - 20 Sep 2020
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "net_game.h"
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "packets.h"

#include "bflib_fileio.h"
#include "net_exchange_gameplay.h"
#include "bflib_datetm.h"
#include "save_catalogue.h"
#include "config_settings.h"
#include "kfx_net_state.h"
#include "kfx_sim_state.h"
#include "config_keeperfx.h"
#include "player_utils.h"
#include "slab_data.h"
#include "dungeon_data.h"
#include "tasks_list.h"
#include "spdigger_stack.h"
#include "ports/ui_port.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define MULTIPLAYER_PAUSE_COOLDOWN_MS 500
uint64_t last_pause_toggle_time = 0;
/******************************************************************************/
#ifdef __cplusplus
}
#endif

void clear_packets(void)
{
    for (int64_t i = 0; i < PACKETS_COUNT; i++)
    {
        memset(&sim_packets[i], 0, sizeof(struct Packet));
    }
}

static TbBool timestamp_packet_pending;

void request_timestamp_packet(void)
{
    timestamp_packet_pending = true;
}

// Sends the host's current time (not the time zone) as a packet, occasionally (upstream #5383).
void set_pending_timestamp_packet_action(struct Packet *pckt)
{
    const GameTurn turn = get_gameturn();
    if ((get_local_user() == SERVER_ID) && (turn % 256 == 128))
        timestamp_packet_pending = true;
    if (!timestamp_packet_pending || (pckt->action != PckA_None) || flag_is_set(kfx_sim_state.operation_flags, GOF_Paused))
        return;
    int32_t tz = (int32_t)kfx_sim_state.timestamp_tz;
    if (!get_local_timezone(&tz, NULL))
        tz = (int32_t)kfx_sim_state.timestamp_tz;

    // 11 bits for tz (in minutes).
    const int32_t tz_minutes = max(-1024, min(1023, tz / 60));
    const TbTimeSec now = LbTimeSec();
    const uint64_t timestamp = (now == (TbTimeSec)-1) ? 0 : (uint64_t)now;

    // par2: upper 21 bits of the timestamp, then tz
    const uint32_t par2 = ((uint32_t)(timestamp >> 32) & 0x1FFFFF) | ((uint32_t)tz_minutes << 21);
    set_packet_action(pckt, PckA_SetTimestamp, (uint32_t)timestamp, par2,
        (uint16_t)turn, (uint16_t)(turn >> 16));
    timestamp_packet_pending = false;
}

void set_packet_pause_toggle()
{
    struct PlayerInfo* player = get_my_player();
    if (player_invalid(player))
        return;
    if (player->user_id >= PACKETS_COUNT)
        return;
    if (network_is_active()) {
        uint64_t current_time = LbTimerClock();
        if (current_time - last_pause_toggle_time < MULTIPLAYER_PAUSE_COOLDOWN_MS) {
            MULTIPLAYER_LOG("set_packet_pause_toggle: cooldown active, ignoring");
            return;
        }
        last_pause_toggle_time = current_time;
    }
    if ((kfx_sim_state.operation_flags & GOF_Paused) == 0) {
        set_players_packet_action(player, PckA_TogglePause, 1, 0, 0, 0);
        return;
    }
    if (network_is_active()) {
        MULTIPLAYER_LOG("set_packet_pause_toggle: broadcasting unpause");
        unpausing_in_progress = 1;
        ui_redraw_gameplay_frame();
        RendererPresentStepFrame();
        LbNetwork_BroadcastUnpause();
        if (network_is_host()) {
            process_pause_packet(0, 0);
        }
        unpausing_in_progress = 0;
        return;
    }
    process_pause_packet(0, 0);
}
