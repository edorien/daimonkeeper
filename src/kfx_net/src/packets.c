/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file packets.c
 *     Packet exchange.
 * @par Purpose:
 *     Exchanging the turn's packets with the other players, pause and resync
 *     gating. Applying the packets to the game is kfx_game's
 *     game_commands.c (refactor pass 2, S12).
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     30 Jan 2009 - 11 Oct 2012
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "packets.h"
#include "net_input_lag.h"
#include "net_checksums.h"
#include "net_lobby.h"

#include <math.h>

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_datetm.h"
#include "bflib_math.h"
#include "bflib_video.h"
#include "bflib_sprite.h"
#include "bflib_vidraw.h"
#include "bflib_fileio.h"
#include "bflib_planar.h"
#include "bflib_dernc.h"
#include "net_exchange_gameplay.h"
#include "net_input_lag.h"
#include "bflib_sound.h"
#include "config_sounds.h"
#include "bflib_sndlib.h"
#include "bflib_sprfnt.h"
#include "bflib_inputctrl.h"

#include "vidmode.h"
#include "config.h"
#include "config_creature.h"
#include "config_crtrmodel.h"
#include "config_effects.h"
#include "config_terrain.h"
#include "config_settings.h"
#include "config_keeperfx.h"
#include "player_instances.h"
#include "player_computer.h"
#include "player_data.h"
#include "config_players.h"
#include "player_utils.h"
#include "engine_camera.h"
#include "engine_render.h"
#include "local_camera.h"
#include "thing_physics.h"
#include "thing_doors.h"
#include "thing_effects.h"
#include "thing_objects.h"
#include "thing_navigate.h"
#include "thing_creature.h"
#include "creature_states.h"
#include "creature_instances.h"
#include "creature_groups.h"
#include "dungeon_data.h"
#include "tasks_list.h"
#include "power_specials.h"
#include "power_hand.h"
#include "room_util.h"
#include "roomspace_prediction.h"
#include "room_workshop.h"
#include "room_data.h"
#include "thing_stats.h"
#include "thing_traps.h"
#include "magic_powers.h"
#include "map_blocks.h"
#include "map_utils.h"
#include "light_registry.h"
#include "net_game.h"
#include "net_resync.h"
#include "engine_redraw.h"
#include "vidfade.h"
#include "spdigger_stack.h"

#include "kfx_config_state.h"
#include "kfx_net_state.h"
#include "kfx_sim_state.h"
#include "power_process.h"
#include "player_camera.h"
#include "ports/ui_port.h"
#include "editor_types.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#ifdef __cplusplus
}
#endif
/******************************************************************************/
TbBool unpausing_in_progress = 0;
/******************************************************************************/
#define RESYNC_LIMIT_BEFORE_COOLDOWN 5
#define RESYNC_COOLDOWN_MS (5 * 60 * 1000)

static int64_t resync_attempt_count = 0;

TbBool is_desync_warning_active(void)
{
    return resync_attempt_count >= RESYNC_LIMIT_BEFORE_COOLDOWN && (kfx_sim_state.system_flags & (GSF_NetGameNoSync | GSF_NetSeedNoSync)) != 0;
}

TbBool resync_game_allowed(void)
{
    static TbClockMSec resync_cooldown_end = 0;
    static GameTurn resync_last_turn = 0;
    TbClockMSec now = LbTimerClock();
    GameTurn turn = get_gameturn();

    if (turn < resync_last_turn) {
        resync_attempt_count = 0;
        resync_cooldown_end = 0;
    }
    resync_last_turn = turn;

    if (resync_attempt_count >= RESYNC_LIMIT_BEFORE_COOLDOWN && (int64_t)(now - resync_cooldown_end) < 0) {
        return false;
    }

    if (resync_attempt_count < RESYNC_LIMIT_BEFORE_COOLDOWN) {
        resync_attempt_count++;
    }
    resync_cooldown_end = now + RESYNC_COOLDOWN_MS;
    return true;
}

void process_pause_packet(int64_t curr_pause, int64_t new_pause)
{
  struct PlayerInfo *player;
  TbBool can = true;
  for (int64_t i = 0; i < PLAYERS_COUNT; i++)
  {
    player = get_player(i);
    if (is_active_keeper(player))
    {
        if ((player->allocflags & PlaF_CompCtrl) == 0)
        {
            if ((player->instance_num == PI_MapFadeTo)
             || (player->instance_num == PI_MapFadeFrom)
             || (player->instance_num == PI_CrCtrlFade)
             || (player->instance_num == PI_DirctCtrl)
             || (player->instance_num == PI_PsngrCtrl)
             || (player->instance_num == PI_DirctCtLeave)
             || (player->instance_num == PI_PsngrCtLeave))
            {
              can = false;
              break;
            }
        }
    }
  }
  if ( can )
  {
      player = get_my_player();
      struct UserState* ustate = get_user_state(get_local_user());
      set_flag_value(kfx_sim_state.operation_flags, GOF_Paused, curr_pause);
      if ((kfx_sim_state.operation_flags & GOF_Paused) != 0) {
          set_flag_value(kfx_sim_state.operation_flags, GOF_WorldInfluence, new_pause);
          if (network_is_active()) {
              kfx_net_state.skip_initial_input_turns = kfx_net_state.input_lag_turns + 1;
          }
      }
      if ( !SoundDisabled )
      {
        if ((kfx_sim_state.operation_flags & GOF_Paused) != 0)
        {
          SetSoundMasterVolume(settings.sound_volume >> 1);
          set_music_volume((settings.music_volume * 181) >> 8); // half the gain: 1/sqrt(2) on volume_setting_gain()'s squared curve
        } else
        {
          SetSoundMasterVolume(settings.sound_volume);
          set_music_volume(settings.music_volume);
        }
      }
      if ((kfx_sim_state.operation_flags & GOF_Paused) != 0)
      {
          if ((ustate->additional_flags & UsrAF_LightningPaletteIsActive) != 0)
          {
              PaletteSetUserPalette(player->user_id, engine_palette);
              get_player_user_state(player)->additional_flags &= ~UsrAF_LightningPaletteIsActive;
          }
      }
  }
}

static void load_old_packets(void)
{
    GameTurn historical_turn = get_gameturn() - kfx_net_state.input_lag_turns;
    MULTIPLAYER_LOG("load_input_lag_packets: current_turn=%" PRIu64 " historical_turn=%" PRIu64, (uint64_t)get_gameturn(), (uint64_t)historical_turn);

    for (int64_t i = 0; i < PACKETS_COUNT; i++) {
        const char* player_name = (i == 0) ? "Host" : "Client";
        const struct Packet *packet = get_history_packet(i, historical_turn);
        if (packet != NULL) {
            sim_packets[i] = *packet;
            if (i <= 1) {
                if (is_packet_empty(&sim_packets[i])) {
                    MULTIPLAYER_LOG("load_input_lag_packets: loaded packet[%s] is EMPTY", player_name);
                } else {
                    MULTIPLAYER_LOG("load_input_lag_packets: loaded packet[%s] turn=%" PRIu64 " checksum=%08" PRIx64, player_name, (uint64_t)sim_packets[i].turn, (uint64_t)sim_packets[i].checksum);
                }
            }
            continue;
        }
        memset(&sim_packets[i], 0, sizeof(struct Packet));
        if (i <= 1) {
            MULTIPLAYER_LOG("load_input_lag_packets: cleared packet[%s] (no stored packet)", player_name);
        }
    }
    input_lag_observe_host_packet(&sim_packets[SERVER_ID]);
}

void set_local_packet_turn(void) {
    struct Packet* pckt = get_local_packet();
    pckt->turn = get_gameturn();
    MULTIPLAYER_LOG("set_local_packet_turn: turn=%" PRIu64 " checksum=%08" PRIx64, (uint64_t)get_gameturn(), (uint64_t)pckt->checksum);
}


/**
 * Exchange packets if MP game
 */
void exchange_packets(void)
{
    SYNCDBG(5, "Starting");

    MULTIPLAYER_LOG("process_packets: === BEGIN turn=%" PRIu64 " ===", (uint64_t)get_gameturn());
    const NetUserId local_user = get_local_user();
    input_lag_update(get_local_packet());
    set_local_packet_turn();
    update_turn_checksums();
    update_local_dig_tag_prediction(kfx_net_state.input_lag_turns);
    if (!kfx_sim_state.replay_active)
        camera_packet_set_state(get_local_packet());
    store_packet_history(local_user, get_local_packet());
    host_spoof_dropped_user_packets();
    if (kfx_sim_state.game_kind != GKind_LocalGame)
    {
        if (!kfx_sim_state.replay_active)
        {
            struct Packet* my_packet = get_local_packet();
            const char* player_name = (local_user == SERVER_ID) ? "Host" : "Client";
            MULTIPLAYER_LOG("process_packets: SENDING packet[%s] turn=%" PRIu64 " checksum=%08" PRIx64, player_name, (uint64_t)my_packet->turn, (uint64_t)my_packet->checksum);
            if (LbNetwork_ExchangeGameplay(my_packet, sim_packets, sizeof(struct Packet)) != Lb_OK) {
                ERRORLOG("LbNetwork_ExchangeGameplay failed");
            }
        }
        process_disconnected_network_players();
        if (quit_game || exit_keeper) {
            clear_packets();
            return;
        }
    }
    if (input_lag_skips_processing()) {
        clear_packets();
        return;
    }

    if (network_is_active()) {
        MULTIPLAYER_LOG("process_packets: Loading packets from packet history");
        load_old_packets();
    }

    if (network_is_active() && checksums_different()) {
        set_flag(kfx_sim_state.system_flags, GSF_NetGameNoSync);
        clear_flag(kfx_sim_state.system_flags, GSF_NetSeedNoSync);
    } else {
        clear_flag(kfx_sim_state.system_flags, GSF_NetGameNoSync);
        clear_flag(kfx_sim_state.system_flags, GSF_NetSeedNoSync);
    }
}

// Using Alt-F4, or similar operating system close requests
void force_application_close()
{
    if (ui_is_frontend_at_initial_state())
    {
        struct PlayerInfo* player = get_my_player();
        if (player != INVALID_PLAYER)
        {
            set_players_packet_action(player, PckA_ForceApplicationClose, 0, 0, 0, 0);
        }
        else
        {
            exit_keeper = 1;
        }
    }
    else
    {
        exit_keeper = 1;
    }
}


/******************************************************************************/
