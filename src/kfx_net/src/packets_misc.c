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

void set_packet_pause_toggle()
{
    struct PlayerInfo* player = get_my_player();
    if (player_invalid(player))
        return;
    if (player->user_id >= PACKETS_COUNT)
        return;
    if (kfx_sim_state.game_kind != GKind_LocalGame) {
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
    if (kfx_sim_state.game_kind != GKind_LocalGame) {
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
