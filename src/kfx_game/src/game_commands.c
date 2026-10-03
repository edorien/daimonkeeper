/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_commands.c
 *     Applying the players' packets to the game.
 * @par Purpose:
 *     Each turn, process_packets() turns every player's packet into game
 *     actions: global actions, dungeon and creature control, map clicks.
 *     Moved from kfx_net's packets.c in refactor pass 2 (S12,
 *     docs/refactor-pass2/stage-12-net-split.md); kfx_net keeps the
 *     exchange. The dungeon-view clicks are game_commands_input.c, the cheats
 *     and editor actions game_commands_cheats.c.
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
#include "cheat_mode.h"
#include "game_commands.h"
#include "packets.h"
#include "game_replay.h"
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
#include "main_game.h"
#include "ports/ui_port.h"
#include "ports/audio_port.h"
#include "editor_types.h"
#include "local_state.h"
#include "render_creature_view.h"
#include "post_inc.h"

/******************************************************************************/
void update_double_click_detection(NetUserId user)
{
    struct Packet* pckt = get_packet(user);
    if ((pckt->control_flags & PCtr_LBtnRelease) != 0)
    {
        if (packet_left_button_click_space_count[user] < 5)
            packet_left_button_double_clicked[user] = 1;
        packet_left_button_click_space_count[user] = 0;
  }
  if ((pckt->control_flags & (PCtr_LBtnClick|PCtr_LBtnHeld)) == 0)
  {
    if (packet_left_button_click_space_count[user] < INT32_MAX)
      packet_left_button_click_space_count[user]++;
  }
}

// keeper_build_room lives in kfx_sim's roomspace.c (declared in
// roomspace.h) -- core room-building logic, not packet handling, and the
// function actually called from packets_input.c's real input path is
// keeper_build_roomspace() -> keeper_build_room() there. Upstream's
// version of this fix (#5229's UserState migration) was ported into that
// real implementation instead of duplicating it here.
TbBool process_dungeon_control_packet_spell_overcharge(NetUserId user)
{
    struct PlayerInfo* player = get_player(get_net_user_player_number(user));
    struct UserState* ustate = get_user_state(user);
    const PlayerNumber plyr_idx = player->id_number;
    struct Dungeon* dungeon = get_players_dungeon(player);
    SYNCDBG(6,"Starting for player %" PRId64 " state %s",(int64_t)plyr_idx,player_state_code_name(player->work_state));
    struct Packet* pckt = get_packet(user);

    while (kfx_config_state.conf.rules[plyr_idx].magic.allow_instant_charge_up && (pckt->additional_packet_values & PCAdV_SpeedupPressed))
    {
        struct PowerConfigStats *powerst = get_power_model_stats(ustate->chosen_power_kind);

        if (powerst->overcharge_check_idx == OcC_CallToArms_expand
            || powerst->overcharge_check_idx == OcC_SightOfEvil_expand
            || powerst->overcharge_check_idx == OcC_General_expand)
        {
            if (powerst->overcharge_check_idx == OcC_CallToArms_expand && player_uses_power_call_to_arms(plyr_idx))
                break;

            while(update_power_overcharge(player, ustate->chosen_power_kind))
            {}

            return true;
        }
        break;
    }

    if (flag_is_set(pckt->control_flags,PCtr_LBtnHeld))
    {
        struct PowerConfigStats *powerst = get_power_model_stats(ustate->chosen_power_kind);

        switch (powerst->overcharge_check_idx)
        {
            case OcC_CallToArms_expand:
                if (player_uses_power_call_to_arms(plyr_idx))
                    player->cast_expand_level = (dungeon->cta_power_level << 2);
                else
                    update_power_overcharge(player, ustate->chosen_power_kind);
                break;
            case OcC_SightOfEvil_expand:
            case OcC_General_expand:
                update_power_overcharge(player, ustate->chosen_power_kind);
                break;
            case OcC_do_not_expand:
            case OcC_Null:
            default:
                player->cast_expand_level++;
                break;
        }
        return true;
    }
    if ((pckt->control_flags & PCtr_LBtnRelease) == 0)
    {
        player->cast_expand_level = 0;
        return false;
    }
    return false;
}

void update_box_lag_compensation(struct PlayerInfo* player) {
    box_lag_compensation_x = 0;
    box_lag_compensation_y = 0;
    if (is_my_player(player)) {
        struct Packet* auth_pckt = get_packet(player->user_id);
        const struct Packet *visual_pckt = get_history_packet(player->user_id, get_gameturn());
        if (visual_pckt != NULL) {
            box_lag_compensation_x = coord_slab(auth_pckt->pos_x) - coord_slab(visual_pckt->pos_x);
            box_lag_compensation_y = coord_slab(auth_pckt->pos_y) - coord_slab(visual_pckt->pos_y);
            box_lag_compensation_x = slab_coord(box_lag_compensation_x);
            box_lag_compensation_y = slab_coord(box_lag_compensation_y);
        }
    }
}

void process_user_dungeon_control_packet_control(NetUserId user)
{
    const PlayerNumber plyr_idx = get_net_user_player_number(user);
    struct PlayerInfo* player = get_player(plyr_idx);
    struct Packet* pckt = get_packet(user);
    SYNCDBG(6,"Processing player %" PRId64 " action %" PRId64,(int64_t)plyr_idx,(int64_t)pckt->action);
    struct Camera* cam = get_player_active_camera(player);
    if (cam == NULL) {
        ERRORLOG("No active camera");
        return;
    }
    process_camera_controls(cam, pckt, player);
    if (is_my_player(player) && !replay.load_enable) {
        TbBool settings_changed = false;
        if ((pckt->control_flags & (PCtr_ViewTiltUp | PCtr_ViewTiltDown | PCtr_ViewTiltReset)) != 0) {
            settings.isometric_tilt = cam->rotation_angle_y;
            settings_changed = true;
        }
        if ((pckt->control_flags & (PCtr_ViewZoomIn | PCtr_ViewZoomOut)) != 0) {
            if (cam->view_mode == PVM_IsoWibbleView || cam->view_mode == PVM_IsoStraightView) {
                settings.isometric_view_zoom_level = cam->zoom;
            } else {
                settings.frontview_zoom_level = cam->zoom;
            }
            settings_changed = true;
        }
        if (settings_changed) {
            save_settings();
        }
    }
    if (is_my_player(player)) {
        update_box_lag_compensation(player);
    }
    process_dungeon_control_packet_clicks(user);
    update_mouse_light(user);
}

/** What process_user_global_packet_action() gives the handler of a global packet action. */
struct GlobalAction
{
    NetUserId user;
    PlayerNumber plyr_idx;
    struct PlayerInfo *player;
    struct Packet *pckt;
    struct UserState *ustate;
    struct UserState *local_ustate;
};

struct GlobalActionHandler
{
    int64_t action;
    TbBool (*handler)(const struct GlobalAction *ctx);
};

static TbBool global_apply_roomspace_dig_tag(const struct GlobalAction *ctx);

/** PckA_QuitToMainMenu */
static TbBool global_quit_to_main_menu(const struct GlobalAction *ctx)
{
    struct PlayerInfo *player = ctx->player;
    if (is_my_player(player))
    {
      ui_turn_off_all_menus();
      ui_frontend_save_continue_game(true);
      free_swipe_graphic();
    }
    player->display_flags |= PlaF6_PlyrHasQuit;
    process_player_leave_game_packet(player);
    return 1;
}

/** PckA_ForceApplicationClose */
static TbBool global_force_application_close(const struct GlobalAction *ctx)
{
    struct PlayerInfo *player = ctx->player;
    if (is_my_player(player))
    {
      ui_turn_off_all_menus();
      ui_frontend_save_continue_game(true);
      free_swipe_graphic();
      exit_keeper = 1;
    }
    else
    {
      player->display_flags |= PlaF6_PlyrHasQuit;
      process_player_leave_game_packet(player);
    }
    return 1;
}

/** PckA_NoOperation */
static TbBool global_no_operation(const struct GlobalAction *ctx)
{
    return 1;
}

/** PckA_FinishGame */
static TbBool global_finish_game(const struct GlobalAction *ctx)
{
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    struct UserState *local_ustate = ctx->local_ustate;
    TbBool my_player = is_my_player(player);
    int64_t victory_state = pckt->actn_par1;
    if (my_player) {
      ui_turn_off_all_menus();
      free_swipe_graphic();
    }
    if (kfx_sim_state.game_kind == GKind_MultiGame) {
      if (victory_state == VicS_WonLevel) {
        player->victory_state = VicS_WonLevel;
        if (kfx_config_state.conf.rules[player->id_number].gameplay.winner_tortures_loser) {
            local_ustate->additional_flags |= UsrAF_UnlockedLordTorture;
        } else {
            local_ustate->additional_flags &= ~UsrAF_UnlockedLordTorture;
        }
        quit_game = 1;
        return 0;
      }
      TbBool host_packet = player->user_id == SERVER_ID;
      if (!my_player) {
        if (host_packet && (player->victory_state != VicS_LostLevel)) {
          local_ustate->additional_flags &= ~UsrAF_UnlockedLordTorture;
          quit_game = 1;
        }
        return 0;
      } else if (host_packet && (victory_state == VicS_LostLevel) && network_human_contenders_remain()) {
        return 0;
      }
    }
    switch (victory_state)
    {
    case VicS_WonLevel:
        complete_level(player);
        break;
    case VicS_LostLevel:
        lose_level(player);
        break;
    default:
        resign_level(player);
        break;
    }
    player->allocflags &= ~PlaF_Allocated;
    if (my_player) {
      ui_frontend_save_continue_game(false);
    }
    return 0;
}

/** PckA_PlyrMsgEnd */
// Local chat is queued with its cursor since upstream #5370 (queue_gameplay_chat_message());
// this packet carries only an External seat's chat (external_seat.c), at its packet's position.
static TbBool global_plyr_msg_end(const struct GlobalAction *ctx)
{
    struct PlayerInfo *player = ctx->player;
    if (player->mp_pending_message[0] != '\0')
        process_gameplay_chat_message(player->user_id, player->mp_pending_message, ctx->pckt->pos_x, ctx->pckt->pos_y);
    player->mp_pending_message[0] = '\0';
    return 0;
}

/** PckA_PlyrMsgClear */
static TbBool global_plyr_msg_clear(const struct GlobalAction *ctx)
{
    NetUserId user = ctx->user;
    struct PlayerInfo *player = ctx->player;
    get_user_state(user)->init_flags &= ~UsrIF_NewMPMessage;
    LbStopTextInput();
    memset(player->mp_message_text, 0, PLAYER_MP_MESSAGE_LEN);
    return 0;
}

/** PckA_ToggleLights */
static TbBool global_toggle_lights(const struct GlobalAction *ctx)
{
    struct PlayerInfo *player = ctx->player;
    if (is_my_player(player))
    {
        light_set_lights_on(kfx_sim_state.light_registry.light_enabled == 0);
    }
    return 1;
}

/** PckA_TogglePause */
static TbBool global_toggle_pause(const struct GlobalAction *ctx)
{
    struct Packet *pckt = ctx->pckt;
    process_pause_packet(pckt->actn_par1, 0);
    return 1;
}

/** PckA_SetCluedo */
static TbBool global_set_cluedo(const struct GlobalAction *ctx)
{
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    if (is_my_player(player))
    {
      settings.video_cluedo_mode = pckt->actn_par1;
      save_settings();
    }
    return 0;
}

/** PckA_ChangeWindowSize */
static TbBool global_change_window_size(const struct GlobalAction *ctx)
{
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    if (is_my_player(player) && !replay.load_enable)
    {
      change_engine_window_relative_size(pckt->actn_par1, pckt->actn_par2);
      centre_engine_window();
    }
    return 0;
}

/** PckA_SetGammaLevel */
static TbBool global_set_gamma_level(const struct GlobalAction *ctx)
{
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    if (is_my_player(player) && !replay.load_enable)
    {
      set_gamma(pckt->actn_par1, 1);
      save_settings();
    }
    return 0;
}

/** PckA_SetMinimapConf */
static TbBool global_set_minimap_conf(const struct GlobalAction *ctx)
{
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    if (is_my_player(player))
    {
      local_state.minimap_zoom = pckt->actn_par1;
      settings.minimap_zoom = local_state.minimap_zoom;
      save_settings();
    }
    return 0;
}

/** PckA_SetPlyrState */
static TbBool global_set_plyr_state(const struct GlobalAction *ctx)
{
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    set_player_state(player, pckt->actn_par1, pckt->actn_par2);
    return 0;
}

/** PckA_SwitchView */
static TbBool global_switch_view(const struct GlobalAction *ctx)
{
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    set_engine_view(player, pckt->actn_par1);
    return 0;
}

/** PckA_ToggleTendency */
static TbBool global_toggle_tendency(const struct GlobalAction *ctx)
{
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    struct Dungeon *dungeon;
    toggle_creature_tendencies(player, pckt->actn_par1);
    if (is_my_player(player)) {
        dungeon = get_players_dungeon(player);
        kfx_sim_state.creatures_tend_imprison = ((dungeon->creature_tendencies & CrTend_Imprison) != 0);
        kfx_sim_state.creatures_tend_flee = ((dungeon->creature_tendencies & CrTend_Flee) != 0);
    }
    return 0;
}

/** PckA_CheatUnusedPlaceholder065 */
static TbBool global_cheat_unused_placeholder065(const struct GlobalAction *ctx)
{
    //TODO: remake from beta
    return 0;
}

/** PckA_CheatUnusedPlaceholder068 */
static TbBool global_cheat_unused_placeholder068(const struct GlobalAction *ctx)
{
    //TODO: remake from beta
    return 0;
}

/** PckA_CheatUnusedPlaceholder069 */
static TbBool global_cheat_unused_placeholder069(const struct GlobalAction *ctx)
{
    //TODO: remake from beta
    return 0;
}

/** PckA_SetViewType */
static TbBool global_set_view_type(const struct GlobalAction *ctx)
{
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    set_player_mode(player, pckt->actn_par1);
    return 0;
}

/** PckA_ZoomFromMap */
static TbBool global_zoom_from_map(const struct GlobalAction *ctx)
{
    NetUserId user = ctx->user;
    struct PlayerInfo *player = ctx->player;
    if (network_is_active()
        || (lbDisplay.PhysicalScreenWidth > 320))
    {
      if (get_local_user() == user)
        ui_toggle_status_menu((kfx_sim_state.operation_flags & GOF_ShowPanel) != 0);
      set_player_mode(player, PVT_DungeonTop);
    } else
    {
      set_player_mode(player, PVT_MapFadeOut);
    }
    return 0;
}

/** PckA_UpdatePause */
static TbBool global_update_pause(const struct GlobalAction *ctx)
{
    struct Packet *pckt = ctx->pckt;
    process_pause_packet(pckt->actn_par1, pckt->actn_par2);
    return 1;
}

/** PckA_ZoomToEvent */
static TbBool global_zoom_to_event(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    if (player->work_state == PSt_CreatrInfo)
      turn_off_query(plyr_idx);
    event_move_player_towards_event(player, pckt->actn_par1);
    return 0;
}

/** PckA_ZoomToRoom */
static TbBool global_zoom_to_room(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    if (player->instance_num == PI_ZoomToPos) {
        return 0;
    }
    if (player->work_state == PSt_CreatrInfo)
        turn_off_query(plyr_idx);
    struct Room* room = room_get(pckt->actn_par1);
    player->zoom_to_pos_x = subtile_coord_center(room->central_stl_x);
    player->zoom_to_pos_y = subtile_coord_center(room->central_stl_y);
    set_player_instance(player, PI_ZoomToPos, 0);
    if (player->work_state == PSt_BuildRoom) {
        set_player_state(player, PSt_BuildRoom, room->kind);
    }
    return 0;
}

/** PckA_ZoomToTrap */
static TbBool global_zoom_to_trap(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    struct Thing *thing;
    if (player->instance_num == PI_ZoomToPos) {
        return 0;
    }
    if (player->work_state == PSt_CreatrInfo)
      turn_off_query(plyr_idx);
    thing = thing_get(pckt->actn_par1);
    player->zoom_to_pos_x = thing->mappos.x.val;
    player->zoom_to_pos_y = thing->mappos.y.val;
    set_player_instance(player, PI_ZoomToPos, 0);
    if ((player->work_state == PSt_PlaceTrap) || (player->work_state == PSt_PlaceDoor)) {
        set_player_state(player, PSt_PlaceTrap, thing->model);
    }
    return 0;
}

/** PckA_ZoomToDoor */
static TbBool global_zoom_to_door(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    struct Thing *thing;
    if (player->instance_num == PI_ZoomToPos) {
        return 0;
    }
    if (player->work_state == PSt_CreatrInfo)
      turn_off_query(plyr_idx);
    thing = thing_get(pckt->actn_par1);
    player->zoom_to_pos_x = thing->mappos.x.val;
    player->zoom_to_pos_y = thing->mappos.y.val;
    set_player_instance(player, PI_ZoomToPos, 0);
    if ((player->work_state == PSt_PlaceTrap) || (player->work_state == PSt_PlaceDoor)) {
        set_player_state(player, PSt_PlaceDoor, thing->model);
    }
    return 0;
}

/** PckA_ZoomToPosition */
static TbBool global_zoom_to_position(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    if (player->instance_num == PI_ZoomToPos) {
        return 0;
    }
    if (player->work_state == PSt_CreatrInfo)
      turn_off_query(plyr_idx);
    player->zoom_to_pos_x = pckt->actn_par1;
    player->zoom_to_pos_y = pckt->actn_par2;
    set_player_instance(player, PI_ZoomToPos, 0);
    return 0;
}

/** PckA_ToggleComputerProcessing */
static TbBool global_toggle_computer_processing(const struct GlobalAction *ctx)
{
    kfx_sim_state.view_mode_flags ^= GNFldD_ComputerPlayerProcessing;
    return 0;
}

/** PckA_ToggleSpectate */
static TbBool global_toggle_spectate(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    struct PlayerInfo *player = ctx->player;
    // Global, not PVT_DungeonTop-specific, on purpose: this is a session-level toggle like pause/quit above,
    // and it must still reach a CompCtrl player -- the per-view dungeon-control dispatch below is exactly
    // what stays blocked for one, so putting this there (as it used to be) would mean the only way out of
    // spectator mode could never itself be processed once spectator mode was on.
    if (flag_is_set(player->allocflags, PlaF_CompCtrl))
        player_leave_spectator_mode(plyr_idx);
    else
        player_enter_spectator_mode(plyr_idx);
    return 0;
}

/** PckA_PwrCTADis */
static TbBool global_pwr_cta_dis(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    turn_off_power_call_to_arms(plyr_idx);
    return 0;
}

/** PckA_UsePwrHandPick */
static TbBool global_use_pwr_hand_pick(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    struct Packet *pckt = ctx->pckt;
    struct Thing *thing;
    thing = thing_get(pckt->actn_par1);
    if ((pckt->control_flags & PCtr_Gui) != 0) {
        magic_use_available_power_on_thing(plyr_idx, PwrK_HAND, 0, thing->mappos.x.stl.num, thing->mappos.y.stl.num, thing, PwMod_Default);
    } else {
        use_power_hand(plyr_idx, thing->mappos.x.stl.num, thing->mappos.y.stl.num, pckt->actn_par1);
    }
    return 0;
}

/** PckA_UsePwrHandDrop */
static TbBool global_use_pwr_hand_drop(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    struct Packet *pckt = ctx->pckt;
    dump_first_held_thing_on_map(plyr_idx, pckt->actn_par1, pckt->actn_par2, 1);
    return 0;
}

/** PckA_EventBoxTurnOff */
static TbBool global_event_box_turn_off(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    struct Packet *pckt = ctx->pckt;
    if (kfx_sim_state.event[pckt->actn_par1].kind != EvKind_Objective) {
        event_delete_event(plyr_idx, pckt->actn_par1);
    }
    return 0;
}

/** PckA_EventBoxActivate, PckA_EventBoxClose */
static TbBool global_event_box_activate_or_close(const struct GlobalAction *ctx)
{
    return false;
}

/** PckA_GenericLevelPower */
static TbBool global_generic_level_power(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    struct Packet *pckt = ctx->pckt;
    magic_use_available_power_on_level(plyr_idx, pckt->actn_par2, 0, PwMod_Default);
    return 0;
}

/** PckA_UsePwrObey */
static TbBool global_use_pwr_obey(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    magic_use_available_power_on_level(plyr_idx, PwrK_OBEY, 0, PwMod_Default);
    return 0;
}

/** PckA_UsePwrArmageddon */
static TbBool global_use_pwr_armageddon(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    magic_use_available_power_on_level(plyr_idx, PwrK_ARMAGEDDON, 0, PwMod_Default);
    return 0;
}

/** PckA_TurnOffQuery */
static TbBool global_turn_off_query(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    turn_off_query(plyr_idx);
    return 0;
}

/** PckA_ZoomToBattle */
static TbBool global_zoom_to_battle(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    if (player->work_state == PSt_CreatrInfo)
      turn_off_query(plyr_idx);
    battle_move_player_towards_battle(player, pckt->actn_par1);
    return 0;
}

/** PckA_ZoomToSpell */
static TbBool global_zoom_to_spell(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    int64_t i;
    if (player->work_state == PSt_CreatrInfo)
      turn_off_query(plyr_idx);
    {
        struct Coord3d locpos;
        if (find_power_cast_place(plyr_idx, pckt->actn_par1, &locpos))
        {
            player->zoom_to_pos_x = locpos.x.val;
            player->zoom_to_pos_y = locpos.y.val;
            set_player_instance(player, PI_ZoomToPos, 0);
        }
    }
    if (!power_is_instinctive(pckt->actn_par1))
    {
        const struct PowerConfigStats *powerst;
        powerst = get_power_model_stats(pckt->actn_par1);
        i = get_power_index_for_work_state(player->work_state);
        if (i > 0)
          set_player_state(player, powerst->work_state, pckt->actn_par1);
    }
    return 0;
}

/** PckA_PlyrFastMsg */
static TbBool global_plyr_fast_msg(const struct GlobalAction *ctx)
{
    struct Packet *pckt = ctx->pckt;
    //show_onscreen_msg(game.num_fps, "Message from player %d", (int64_t)(plyr_idx));
    audio_output_message(SMsg_EnemyHarassments+pckt->actn_par1, 0);
    return 0;
}

/** PckA_SetComputerKind */
static TbBool global_set_computer_kind(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    struct Packet *pckt = ctx->pckt;
    set_autopilot_type(plyr_idx, pckt->actn_par1);
    return 0;
}

/** PckA_GoSpectator */
static TbBool global_go_spectator(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    level_lost_go_first_person(plyr_idx);
    return 0;
}

/** PckA_DumpHeldThingToOldPos */
static TbBool global_dump_held_thing_to_old_pos(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    struct PlayerInfo *player = ctx->player;
    struct Thing *thing;
    if (!power_hand_is_empty(player))
    {
        thing = get_first_thing_in_power_hand(player);
        dump_first_held_thing_on_map(plyr_idx, thing->mappos.x.stl.num, thing->mappos.y.stl.num, 1);
    }
    return false;
}

/** PckA_PwrSOEDis */
static TbBool global_pwr_soe_dis(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    turn_off_power_sight_of_evil(plyr_idx);
    return false;
}

/** PckA_UsePwrOnThing */
static TbBool global_use_pwr_on_thing(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    int64_t i;
    i = get_power_overcharge_level(player);
    directly_cast_spell_on_thing(plyr_idx, pckt->actn_par1, pckt->actn_par2, i);
    return 0;
}

/** PckA_PlyrToggleAlly */
static TbBool global_plyr_toggle_ally(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    struct Packet *pckt = ctx->pckt;
    if (!is_player_ally_locked(plyr_idx, pckt->actn_par1))
    {
       toggle_ally_with_player(plyr_idx, pckt->actn_par1);
       if (kfx_config_state.conf.rules[plyr_idx].gameplay.allies_share_vision)
       {
          ui_panel_map_update(0, 0, kfx_sim_state.map_subtiles_x+1, kfx_sim_state.map_subtiles_y+1);
       }
      update_navigation_around_all_doors();
    }
    return false;
}

/** PckA_SaveViewType */
static TbBool global_save_view_type(const struct GlobalAction *ctx)
{
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
          struct Camera* camera = get_player_active_camera(player);
          if (camera != NULL && player->view_type != pckt->actn_par1)
              player->view_mode_restore = camera->view_mode;
    set_player_mode(player, pckt->actn_par1);
    return false;
}

/** PckA_LoadViewType */
static TbBool global_load_view_type(const struct GlobalAction *ctx)
{
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    set_player_mode(player, pckt->actn_par1);
    return false;
}

/** PckA_SetRoomspaceAuto, PckA_SetRoomspaceMan, PckA_SetRoomspaceDragPaint, PckA_SetRoomspaceDrag, PckA_SetRoomspaceDefault, PckA_SetRoomspaceWholeRoom, PckA_SetRoomspaceSubtile */
static TbBool global_set_roomspace(const struct GlobalAction *ctx)
{
    NetUserId user = ctx->user;
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    apply_roomspace_packet_action(player, user, pckt);
    return false;
}

/** PckA_RoomspaceHighlightToggle */
static TbBool global_roomspace_highlight_toggle(const struct GlobalAction *ctx)
{
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    player->highlight_mode = pckt->actn_par1;
    if (is_my_player(player) && !replay.load_enable)
    {
        settings.highlight_mode = pckt->actn_par1;
        if (keeperfx_ui_config.default_tag_mode == 3)
        {
            save_settings();
        }
    }
    // fall through (into the next case of the switch this was)
    return global_apply_roomspace_dig_tag(ctx);
}

/** PckA_ApplyRoomspaceDigTag, PckA_SetRoomspaceHighlight (and PckA_RoomspaceHighlightToggle's second half) */
static TbBool global_apply_roomspace_dig_tag(const struct GlobalAction *ctx)
{
    PlayerNumber plyr_idx = ctx->plyr_idx;
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    struct UserState *ustate = ctx->ustate;
    player->roomspace_mode = pckt->actn_par1;
    if ( (pckt->actn_par2 == 1) || (pckt->actn_par1 == roomspace_detection_mode) )
    {
        // exit out of click and drag mode
        if (player->render_roomspace.drag_mode)
        {
            ustate->cursor_button_down = 0;
            ustate->one_click_lock_cursor = false;
            if ((pckt->control_flags & PCtr_LBtnHeld) == PCtr_LBtnHeld)
            {
                ustate->ignore_next_PCtr_LBtnRelease = true;
            }
        }
        player->render_roomspace.drag_mode = false;
    }
    player->roomspace_highlight_mode = pckt->actn_par1;
    if (pckt->actn_par1 == box_placement_mode) {
        reset_dungeon_build_room_ui_variables(plyr_idx);
    }
    if (pckt->actn_par1 == box_placement_mode || pckt->actn_par1 == roomspace_detection_mode || (pckt->actn_par1 == drag_placement_mode && pckt->actn_par2 == 1)) {
        player->roomspace_width = player->roomspace_height = pckt->actn_par2;
    }
    return false;
}

/** PckA_PlyrQueryCreature */
static TbBool global_plyr_query_creature(const struct GlobalAction *ctx)
{
    struct PlayerInfo *player = ctx->player;
    struct Packet *pckt = ctx->pckt;
    query_creature(player, pckt->actn_par1, (pckt->actn_par2 & 0x01) != 0, (pckt->actn_par2 & 0x02) != 0);
    return false;
}

/** The global packet actions and their handlers (refactor pass 4 S05: was a switch); the rest are the global cheats'. */
static const struct GlobalActionHandler global_actions[] = {
    {PckA_QuitToMainMenu, global_quit_to_main_menu},
    {PckA_ForceApplicationClose, global_force_application_close},
    {PckA_NoOperation, global_no_operation},
    {PckA_FinishGame, global_finish_game},
    {PckA_PlyrMsgEnd, global_plyr_msg_end},
    {PckA_PlyrMsgClear, global_plyr_msg_clear},
    {PckA_ToggleLights, global_toggle_lights},
    {PckA_TogglePause, global_toggle_pause},
    {PckA_SetCluedo, global_set_cluedo},
    {PckA_ChangeWindowSize, global_change_window_size},
    {PckA_SetGammaLevel, global_set_gamma_level},
    {PckA_SetMinimapConf, global_set_minimap_conf},
    {PckA_SetPlyrState, global_set_plyr_state},
    {PckA_SwitchView, global_switch_view},
    {PckA_ToggleTendency, global_toggle_tendency},
    {PckA_CheatUnusedPlaceholder065, global_cheat_unused_placeholder065},
    {PckA_CheatUnusedPlaceholder068, global_cheat_unused_placeholder068},
    {PckA_CheatUnusedPlaceholder069, global_cheat_unused_placeholder069},
    {PckA_SetViewType, global_set_view_type},
    {PckA_ZoomFromMap, global_zoom_from_map},
    {PckA_UpdatePause, global_update_pause},
    {PckA_ZoomToEvent, global_zoom_to_event},
    {PckA_ZoomToRoom, global_zoom_to_room},
    {PckA_ZoomToTrap, global_zoom_to_trap},
    {PckA_ZoomToDoor, global_zoom_to_door},
    {PckA_ZoomToPosition, global_zoom_to_position},
    {PckA_ToggleComputerProcessing, global_toggle_computer_processing},
    {PckA_ToggleSpectate, global_toggle_spectate},
    {PckA_PwrCTADis, global_pwr_cta_dis},
    {PckA_UsePwrHandPick, global_use_pwr_hand_pick},
    {PckA_UsePwrHandDrop, global_use_pwr_hand_drop},
    {PckA_EventBoxTurnOff, global_event_box_turn_off},
    {PckA_EventBoxActivate, global_event_box_activate_or_close},
    {PckA_EventBoxClose, global_event_box_activate_or_close},
    {PckA_GenericLevelPower, global_generic_level_power},
    {PckA_UsePwrObey, global_use_pwr_obey},
    {PckA_UsePwrArmageddon, global_use_pwr_armageddon},
    {PckA_TurnOffQuery, global_turn_off_query},
    {PckA_ZoomToBattle, global_zoom_to_battle},
    {PckA_ZoomToSpell, global_zoom_to_spell},
    {PckA_PlyrFastMsg, global_plyr_fast_msg},
    {PckA_SetComputerKind, global_set_computer_kind},
    {PckA_GoSpectator, global_go_spectator},
    {PckA_DumpHeldThingToOldPos, global_dump_held_thing_to_old_pos},
    {PckA_PwrSOEDis, global_pwr_soe_dis},
    {PckA_UsePwrOnThing, global_use_pwr_on_thing},
    {PckA_PlyrToggleAlly, global_plyr_toggle_ally},
    {PckA_SaveViewType, global_save_view_type},
    {PckA_LoadViewType, global_load_view_type},
    {PckA_SetRoomspaceAuto, global_set_roomspace},
    {PckA_SetRoomspaceMan, global_set_roomspace},
    {PckA_SetRoomspaceDragPaint, global_set_roomspace},
    {PckA_SetRoomspaceDrag, global_set_roomspace},
    {PckA_SetRoomspaceDefault, global_set_roomspace},
    {PckA_SetRoomspaceWholeRoom, global_set_roomspace},
    {PckA_SetRoomspaceSubtile, global_set_roomspace},
    {PckA_RoomspaceHighlightToggle, global_roomspace_highlight_toggle},
    {PckA_ApplyRoomspaceDigTag, global_apply_roomspace_dig_tag},
    {PckA_SetRoomspaceHighlight, global_apply_roomspace_dig_tag},
    {PckA_PlyrQueryCreature, global_plyr_query_creature},
};

TbBool process_user_global_packet_action(NetUserId user)
{
  //TODO PACKET add commands from beta
  PlayerNumber plyr_idx = get_net_user_player_number(user);
  struct PlayerInfo* player = get_player(plyr_idx);
  struct Packet* pckt = get_packet(user);
  struct UserState* ustate = get_user_state(user);
  struct UserState* local_ustate = get_local_user_state();
  SYNCDBG(6,"Processing user %" PRId64 " action %" PRId64,(int64_t)user,(int64_t)pckt->action);

  process_camera_action(player->cameras, pckt);

  const struct GlobalAction ctx = { user, plyr_idx, player, pckt, ustate, local_ustate };
  for (size_t n = 0; n < sizeof(global_actions) / sizeof(global_actions[0]); n++)
  {
      if (global_actions[n].action == pckt->action)
      {
          return global_actions[n].handler(&ctx);
      }
  }
  return process_user_global_cheats_packet_action(user, pckt);
}

void process_user_map_packet_control(NetUserId user)
{
    const PlayerNumber plyr_idx = get_net_user_player_number(user);
    SYNCDBG(6,"Starting");
    struct PlayerInfo* player = get_player(plyr_idx);
    struct Packet* pckt = get_packet(user);
    // Get map coordinates
    process_map_packet_clicks(user);
    player->cameras[CamIV_Parchment].mappos.x.val = pckt->pos_x;
    player->cameras[CamIV_Parchment].mappos.y.val = pckt->pos_y;
    update_mouse_light(user);
    SYNCDBG(8,"Finished");
}

void process_map_packet_clicks(NetUserId user)
{
    SYNCDBG(7,"Starting");
    packet_left_button_double_clicked[user] = 0;
    struct Packet* pckt = get_packet(user);
    if ((pckt->control_flags & PCtr_Gui) == 0)
    {
        update_double_click_detection(user);
    }
    SYNCDBG(8,"Finished");
}

/**
 * Process packet with input commands for given player.
 * @param user User to process packet for.
 */
void process_user_packet(NetUserId user)
{
    struct PlayerInfo* player = get_player(get_net_user_player_number(user));
    struct Packet* pckt = get_packet(user);
    if (is_packet_empty(pckt))
    {
        MULTIPLAYER_LOG("process_user_packet: Skipping empty packet for user %" PRId64, (int64_t)(user));
        return;
    }
    SYNCDBG(6, "Processing user %" PRId64 " packet of type %" PRId64 ".", (int64_t)(user), (int64_t)pckt->action);
    if (flag_is_set(kfx_sim_state.operation_flags, GOF_Paused))
        replay_record_paused_action(user, pckt);
    struct UserState* ustate = get_user_state(user);
    ustate->input_crtr_control = ((pckt->additional_packet_values & PCAdV_CrtrContrlPressed) != 0);
    ustate->input_crtr_query = ((pckt->additional_packet_values & PCAdV_CrtrQueryPressed) != 0);

  if (!process_user_global_packet_action(user))
  {
      // Different changes to the game are possible for different views.
      // For each there can be a control change (which is view change or mouse event not translated to action),
      // and action perform (which does specific action set in packet).
      switch (player->view_type)
      {
          case PVT_DungeonTop:
            // A CompCtrl player (spectator handoff, or a genuine AI rival) must never have build/dig/slap/
            // spell click-input processed -- that is the one thing that must stay exclusively the AI's.
            // Global actions above (pause, quit, camera, view switching, PckA_ToggleSpectate itself) are not
            // gated here and still work for a spectating local player.
            //
            // An External seat on the local player's own number (net_add_external_seat's own exception, so an
            // agent can play a campaign/scenario seat) is trickier: get_net_user_player_number(SOLO_HUMAN_ID)
            // always resolves to my_player_number, so front_input.c's own packet for user 0 (still generated
            // every frame regardless of seat type) and the seat's real packet (a different user, written by
            // external_seat.c exactly like a human's) both resolve to the SAME player here. Only the packet
            // from the seat's own current user_id may act -- the stale user-0 one must not, or the human's own
            // leftover clicks and the agent's submitted verbs would both mutate the same dungeon.
            if (((player->allocflags & PlaF_CompCtrl) == 0)
             && (((player->allocflags & PlaF_ExternalSeat) == 0) || (user == player->user_id))) {
                process_user_dungeon_control_packet_control(user);
                process_user_dungeon_control_packet_action(user);
            }
            break;
          case PVT_CreatureContrl:
            process_user_creature_control_packet_control(user);
            process_user_creature_control_packet_action(user);
            break;
          case PVT_CreaturePasngr:
            //process_user_creature_passenger_packet_control(user); -- there are no control changes in passenger mode
            process_user_creature_passenger_packet_action(user);
            break;
          case PVT_MapScreen:
            process_user_map_packet_control(user);
            //process_user_map_packet_action(user); -- there are no actions to perform from map screen
            break;
          default:
            break;
      }
  }
  SYNCDBG(8,"Finished");
}

void process_user_creature_passenger_packet_action(NetUserId user)
{
    const PlayerNumber plyr_idx = get_net_user_player_number(user);
    struct PlayerInfo* player = get_player(plyr_idx);
    struct Packet* pckt = get_packet(user);
    SYNCDBG(6,"Processing player %" PRId64 " action %" PRId64,(int64_t)plyr_idx,(int64_t)pckt->action);
    if (pckt->action == PckA_PasngrCtrlExit)
    {
        player->influenced_thing_idx = pckt->actn_par1;
        player->influenced_thing_creation = pckt->actn_par2;
        set_player_instance(player, PI_PsngrCtLeave, 0);
    }
    SYNCDBG(8,"Finished");
}

TbBool process_user_dungeon_control_packet_action(NetUserId user)
{
    const PlayerNumber plyr_idx = get_net_user_player_number(user);
    struct PlayerInfo* player = get_player(plyr_idx);
    struct Packet* pckt = get_packet(user);
    SYNCDBG(6,"Processing player %" PRId64 " action %" PRId64,(int64_t)plyr_idx,(int64_t)pckt->action);
    switch (pckt->action)
    {
    case PckA_HoldAudience:
        magic_use_available_power_on_level(plyr_idx, PwrK_HOLDAUDNC, 0, PwMod_Default);
        break;
    case PckA_UseSpecialBox:
        activate_dungeon_special(thing_get(pckt->actn_par1), player);
        break;
    case PckA_ResurrectCrtr:
        resurrect_creature(thing_get(pckt->actn_par1), (pckt->actn_par2) & 0x0F, (pckt->actn_par2 >> 4) & 0xFF,
            (pckt->actn_par2 >> 12) & 0x0F);
        break;
    case PckA_TransferCreatr:
        transfer_creature(thing_get(pckt->actn_par1), thing_get(pckt->actn_par2), plyr_idx);
        break;
    case PckA_ToggleComputer:
        toggle_computer_player(plyr_idx);
        break;
    default:
        return process_players_dungeon_control_cheats_packet_action(plyr_idx, pckt);
    }
    return true;
}

void process_user_creature_control_packet_control(NetUserId user)
{
    struct UserState* ustate = get_user_state(user);
    const PlayerNumber plyr_idx = get_net_user_player_number(user);
    SYNCDBG(6,"Starting");
    struct InstanceInfo *inst_inf;
    int64_t i;
    struct PlayerInfo* player = get_player(plyr_idx);
    struct Thing* cctng = thing_get(player->controlled_thing_idx);
    struct Packet* pckt = get_packet(user);
    struct CreatureControl* ccctrl = creature_control_get_from_thing(cctng);
    ThingIndex target_idx;
    if (can_process_creature_input(cctng))
    {
        int64_t speed_limit = get_creature_speed(cctng);
        if ((pckt->control_flags & PCtr_MoveUp) != 0)
        {
            if (!creature_control_invalid(ccctrl))
            {
                ccctrl->move_speed = compute_controlled_speed_increase(ccctrl->move_speed, speed_limit);
                ccctrl->creature_control_flags |= CCFlg_MoveY;
            } else
            {
                ERRORLOG("No creature to increase speed");
            }
        }
        if ((pckt->control_flags & PCtr_MoveDown) != 0)
        {
            if (!creature_control_invalid(ccctrl))
            {
                ccctrl->move_speed = compute_controlled_speed_decrease(ccctrl->move_speed, speed_limit);
                ccctrl->creature_control_flags |= CCFlg_MoveY;
            } else
            {
                ERRORLOG("No creature to decrease speed");
            }
        }
        if ((pckt->control_flags & PCtr_MoveLeft) != 0)
        {
            if (!creature_control_invalid(ccctrl))
            {
                ccctrl->orthogn_speed = compute_controlled_speed_increase(ccctrl->orthogn_speed, speed_limit);
                ccctrl->creature_control_flags |= CCFlg_MoveX;
            } else
            {
                ERRORLOG("No creature to increase speed");
            }
        }
        if ((pckt->control_flags & PCtr_MoveRight) != 0)
        {
            if (!creature_control_invalid(ccctrl))
            {
                ccctrl->orthogn_speed = compute_controlled_speed_decrease(ccctrl->orthogn_speed, speed_limit);
                ccctrl->creature_control_flags |= CCFlg_MoveX;
            } else
            {
                ERRORLOG("No creature to decrease speed");
            }
        }
        if (flag_is_set(cctng->movement_flags, TMvF_Flying))
        {
            MapCoord floor_height, ceiling_height;
            if ((pckt->control_flags & PCtr_Ascend) != 0)
            {
                if (!creature_control_invalid(ccctrl))
                {
                    ccctrl->vertical_speed = compute_controlled_speed_increase(ccctrl->vertical_speed, speed_limit);
                    ccctrl->creature_control_flags |= CCFlg_MoveZ;
                    if (ccctrl->vertical_speed != 0)
                    {
                        get_floor_and_ceiling_height_under_thing_at(cctng, &cctng->mappos, &floor_height, &ceiling_height);
                        if ( (cctng->mappos.z.val >= floor_height) && (cctng->mappos.z.val <= ceiling_height) )
                        {
                            ccctrl->moveaccel.z.val = distance_with_angle_to_coord_z(ccctrl->vertical_speed, 227);
                        }
                        else
                        {
                            ccctrl->moveaccel.z.val = 0;
                        }
                    }
                } else
                {
                    ERRORLOG("No creature to ascend");
                }
            }
            if ((pckt->control_flags & PCtr_Descend) != 0)
            {
                if (!creature_control_invalid(ccctrl))
                {
                    // We want increase here, not decrease, because we don't want it angle-dependent
                    ccctrl->vertical_speed = compute_controlled_speed_increase(ccctrl->vertical_speed, speed_limit);
                    ccctrl->creature_control_flags |= CCFlg_MoveZ;
                    if (ccctrl->vertical_speed != 0)
                    {
                        get_floor_and_ceiling_height_under_thing_at(cctng, &cctng->mappos, &floor_height, &ceiling_height);
                        if ( (cctng->mappos.z.val >= floor_height) && (cctng->mappos.z.val <= ceiling_height) )
                        {
                            ccctrl->moveaccel.z.val = distance_with_angle_to_coord_z(ccctrl->vertical_speed, 1820);
                        }
                        else
                        {
                            ccctrl->moveaccel.z.val = 0;
                        }
                    }
                } else
                {
                    ERRORLOG("No creature to descend");
                }
            }
        }
        if (ustate->first_person_unfreeze_delay <= 0)
        {
            int64_t new_horizontal, new_vertical, new_roll;
            process_first_person_look(cctng, pckt, cctng->move_angle_xy, cctng->move_angle_z, &new_horizontal, &new_vertical, &new_roll);
            cctng->move_angle_xy = new_horizontal;
            cctng->move_angle_z = new_vertical;
            ccctrl->roll = new_roll;
        }
        else --ustate->first_person_unfreeze_delay;
    }
    else
    {
        // The local_camera is delayed by input_lag_turns, and will remain
        // frozen for this duration after the creature is allowed to move again.
        // Apply this same delay to the creature's move_angle_{xy,z}, to keep it
        // synchronized.
        ustate->first_person_unfreeze_delay = kfx_net_state.input_lag_turns;
    }

    if ((thing_is_creature(cctng) && !creature_is_dying(cctng)) && (cctng->active_state != CrSt_CreatureUnconscious))
    {
        TbBool allowed;
        if ((pckt->control_flags & PCtr_LBtnRelease) != 0)
        {
            i = ccctrl->active_instance_id;
            if (ccctrl->instance_id == CrInst_NULL)
            {
                if (creature_instance_is_available(cctng, i))
                {
                    if (creature_instance_has_reset(cctng, i))
                    {
                        target_idx = get_human_controlled_creature_target(cctng, i, pckt);
                        if (creature_under_spell_effect(cctng, CSAfF_Chicken))
                        {
                            inst_inf = creature_instance_info_get(i);
                            allowed = inst_inf->fp_allow_self_cast_when_chicken & (cctng->index == target_idx);
                        }
                        else
                        {
                            allowed = true;
                        }
                        if (allowed)
                        {
                            if (creature_under_spell_effect(cctng, CSAfF_Freeze))
                            {
                                inst_inf = creature_instance_info_get(i);
                                allowed = inst_inf->fp_allow_self_cast_while_frozen & (cctng->index == target_idx);
                            }
                            if (allowed)
                            {
                                process_player_use_instance(cctng, i, pckt);
                            }
                        }
                    }
                }
                else
                {
                    // cheat mode
                    inst_inf = creature_instance_info_get(i);
                    process_player_use_instance(cctng, i, pckt);
                }
            }
        }
        if ((pckt->control_flags & PCtr_LBtnHeld) != 0)
        {
            // Button is held down - check whether the instance has auto-repeat
            i = ccctrl->active_instance_id;
            inst_inf = creature_instance_info_get(i);
            if ((inst_inf->instance_property_flags & InstPF_RepeatTrigger) != 0)
            {
                if (ccctrl->instance_id == CrInst_NULL)
                {
                    if (creature_instance_is_available(cctng, i))
                    {
                        if (creature_instance_has_reset(cctng, i))
                        {
                            if (creature_under_spell_effect(cctng, CSAfF_Freeze))
                            {
                                target_idx = get_human_controlled_creature_target(cctng, i, pckt);
                                allowed = inst_inf->fp_allow_self_cast_while_frozen & (cctng->index == target_idx);
                            }
                            else
                            {
                                allowed = true;
                            }
                            if (allowed)
                            {
                                process_player_use_instance(cctng, i, pckt);
                            }
                        }
                    }
                    else
                    {
                        // cheat mode
                        process_player_use_instance(cctng, i, pckt);
                    }
                }
            }
        }
    }
}

void process_user_creature_control_packet_action(NetUserId user)
{
  const PlayerNumber plyr_idx = get_net_user_player_number(user);
  struct CreatureControl *cctrl;
  struct InstanceInfo *inst_inf;
  struct PlayerInfo *player;
  struct Thing *thing;
  struct Packet *pckt;
  int64_t i;
  player = get_player(plyr_idx);
  struct UserState* ustate = get_user_state(user);
  pckt = get_packet(user);
  SYNCDBG(6,"Processing player %" PRId64 " action %" PRId64,(int64_t)plyr_idx,(int64_t)pckt->action);
  switch (pckt->action)
  {
  case PckA_DirectCtrlExit:
      player->influenced_thing_idx = pckt->actn_par1;
      player->influenced_thing_creation = pckt->actn_par2;
      thing = thing_get(player->controlled_thing_idx);
      cctrl = creature_control_get_from_thing(thing);
      struct Thing* dragtng = thing_get(cctrl->dragtng_idx);
      if (!thing_is_invalid(dragtng))
      {
          creature_drop_dragged_object(thing, dragtng);
      }
      set_player_instance(player, PI_DirctCtLeave, 0);
      break;
  case PckA_CtrlCrtrSetInstnc:
      thing = thing_get(player->controlled_thing_idx);
      if (!thing_exists(thing))
        break;
      cctrl = creature_control_get_from_thing(thing);
      if (creature_control_invalid(cctrl))
        break;
      i = pckt->actn_par1;
      inst_inf = creature_instance_info_get(i);
      if (!inst_inf->instant || pckt->actn_par2)
      {
        cctrl->active_instance_id = i;
      } else
      if (cctrl->instance_id == CrInst_NULL)
      {
        if (creature_instance_is_available(thing,i) && creature_instance_has_reset(thing, pckt->actn_par1))
        {
            TbBool allowed;
            TbBool frozen = creature_under_spell_effect(thing, CSAfF_Freeze);
            TbBool chicken = creature_under_spell_effect(thing, CSAfF_Chicken);
            ThingIndex target_idx = get_human_controlled_creature_target(thing, i, pckt);
            if (frozen && chicken)
            {
                allowed = (inst_inf->fp_allow_self_cast_while_frozen & inst_inf->fp_allow_self_cast_when_chicken) && (thing->index == target_idx);
            }
            else if (frozen)
            {
                allowed = inst_inf->fp_allow_self_cast_while_frozen & (thing->index == target_idx);
            }
            else if (chicken)
            {
                allowed = inst_inf->fp_allow_self_cast_when_chicken & (thing->index == target_idx);
            }
            else
            {
                allowed = true;
            }
            if (allowed)
            {
              i = pckt->actn_par1;
              process_player_use_instance(thing, i, pckt);
              if (plyr_idx == my_player_number) {
                  ui_instant_instance_selected(i);
              }
            }
        }
      }
      break;
  case PckA_CheatCtrlCrtrSetInstnc:
      if (game_refuses_cheats())
        break;
      thing = thing_get(player->controlled_thing_idx);
      if (!thing_exists(thing))
        break;
      cctrl = creature_control_get_from_thing(thing);
      if (creature_control_invalid(cctrl))
        break;
      i = pckt->actn_par1;
      // Cheat mode no need check any, just do/select it.
      cctrl->active_instance_id = i;
      break;
      case PckA_DirectCtrlDragDrop:
      {
         thing = thing_get(player->controlled_thing_idx);
         direct_control_pick_up_or_drop(plyr_idx, thing);
         break;
      }
    case PckA_SetFirstPersonDigMode:
    {
        ustate->first_person_dig_claim_mode = pckt->actn_par1;
        break;
    }
    case PckA_SwitchTeleportDest:
    {
        ustate->teleport_destination = pckt->actn_par1;
        break;
    }
    case PckA_SelectFPPickup:
    {
        ustate->selected_fp_thing_pickup = pckt->actn_par1;
        break;
    }
    case PckA_SetNearestTeleport:
    {
        ustate->nearest_teleport = pckt->actn_par1;
        break;
    }
  }
}

/**
 * Releases every user's held mouse buttons, so a drag (tagging, a held slap) doesn't run on
 * after a pause or a replay's playback pause.
 */
void clear_users_button_state(void)
{
    for (NetUserId user = 0; user < MAX_NET_USERS; user++)
    {
        struct UserState *ustate = get_user_state(user);
        if (user_state_invalid(ustate))
            continue;
        ustate->cursor_button_down = 0;
        ustate->interpolated_tagging = false;
    }
}

/**
 * Process all packets influencing local game state.
 */
void process_packets(void)
{
    if (!replay.load_enable && flag_is_set(kfx_sim_state.operation_flags, GOF_Paused))
        clear_users_button_state();
    process_queued_chat_messages();
    if (replay.load_enable)
        verify_replay_checksum();
    // Write packets into file, if requested (a paused turn is not a turn of the game)
    if ((replay.save_enable) && (replay.fopened) && !flag_is_set(kfx_sim_state.operation_flags, GOF_Paused)) {
        save_packets();
    } else {
        replay_forget_saved_turn();
    }
    //Debug code, to find packet errors
    #if DEBUG_NETWORK_PACKETS
    write_debug_packets();
    #endif
    // Process the packets
    for (NetUserId user = 0; (user < PACKETS_COUNT) && !replay_playback_is_paused(); user++)
    {
        const PlayerNumber plyr_idx = get_net_user_player_number(user);
        if (plyr_idx < 0) {
            continue;
        }
        struct PlayerInfo* packet_player = get_player(plyr_idx);
        // Always dispatched, even for a CompCtrl player: process_user_packet()'s own global/per-view split
        // (below) is where a CompCtrl player's dungeon-mutating input specifically gets skipped. A genuinely
        // AI-only player's packet is simply empty here (nothing ever writes to it), so this is a no-op for them
        // regardless.
        if (player_exists(packet_player)) {
            process_user_packet(user);
        }
    }
    update_local_dig_prediction_cursor_preview(kfx_net_state.input_lag_turns);
    // Clear all packets
    clear_packets();
    if (quit_game || exit_keeper) {
        return;
    }
    if (network_is_active()
     && ((local_system_flags & (GSF_NetGameNoSync | GSF_NetSeedNoSync)) != 0))
    {
        if (resync_game_allowed()) {
            SYNCDBG(0,"Resyncing");
            resync_game();
        }
    }
    if (replay.load_enable)
        replay_apply_pending_resync();
    get_current_stutter_milliseconds();
    MULTIPLAYER_LOG("process_packets: === END turn=%" PRIu64 " ===", (uint64_t)get_gameturn());
    SYNCDBG(7,"Finished");
}
