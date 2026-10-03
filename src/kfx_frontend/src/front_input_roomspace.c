/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file front_input_roomspace.c
 *     Local input for the room-building, selling and dig-highlight cursors.
 * @par Purpose:
 *     Reads the roomspace keys (numpad sizes, +/-, "square" and "best" room,
 *     sell-on-subtile) and turns them into packet actions. Moved from kfx_sim's
 *     roomspace.c (docs/refactor-pass2/stage-06-presentation-out-of-sim.md):
 *     only front_input.c calls it, and the sim half of roomspace.c gets the
 *     size back from the packet.
 * @par Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "front_input_roomspace.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_keybrd.h"
#include "config_terrain.h"
#include "front_input.h"
#include "kjm_input.h"
#include "packet_data.h"
#include "player_data.h"
#include "roomspace.h"
#include "local_state.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
static TbBool reset_roomspace = false;

static int64_t numpad_to_value(TbBool allow_zero)
{
    int64_t value = 0;
    if (!allow_zero)
    {
        value = 1;
    }
    if (is_key_pressed(KC_NUMPAD0, KMod_DONTCARE) && allow_zero)
    {
        value = 0;
    }
    else if (is_key_pressed(KC_NUMPAD1, KMod_DONTCARE))
    {
        value = 1;
    }
    else if (is_key_pressed(KC_NUMPAD2, KMod_DONTCARE))
    {
        value = 2;
    }
    else if (is_key_pressed(KC_NUMPAD3, KMod_DONTCARE))
    {
        value = 3;
    }
    else if (is_key_pressed(KC_NUMPAD4, KMod_DONTCARE))
    {
        value = 4;
    }
    else if (is_key_pressed(KC_NUMPAD5, KMod_DONTCARE))
    {
        value = 5;
    }
    else if (is_key_pressed(KC_NUMPAD6, KMod_DONTCARE))
    {
        value = 6;
    }
    else if (is_key_pressed(KC_NUMPAD7, KMod_DONTCARE))
    {
        value = 7;
    }
    else if (is_key_pressed(KC_NUMPAD8, KMod_DONTCARE))
    {
        value = 8;
    }
    else if (is_key_pressed(KC_NUMPAD9, KMod_DONTCARE))
    {
        value = 9;
    }
    return value;
}

static int64_t get_roomspace_size_input(void)
{
    if ((is_game_key_pressed(Gkey_RoomSpaceIncSize, false, true) != 0)) {
        if (local_state.roomspace_size < MAX_USER_ROOMSPACE_WIDTH) {
            local_state.roomspace_size++;
        }
    } else if ((is_game_key_pressed(Gkey_RoomSpaceDecSize, false, true) != 0) && local_state.roomspace_size > MIN_USER_ROOMSPACE_WIDTH) {
        local_state.roomspace_size--;
    }
    return local_state.roomspace_size;
}

static void process_box_roomspace_inputs(struct Packet *pckt)
{
    if ((is_game_key_pressed(Gkey_SquareRoomSpace, false, true) != 0)) {
        set_packet_action(pckt, PckA_SetRoomspaceMan, get_roomspace_size_input(), 0, 0, 0);
    } else {
        local_state.roomspace_size = DEFAULT_USER_ROOMSPACE_WIDTH;
        int64_t size = numpad_to_value(false);
        if (size > 1) {
            set_packet_action(pckt, PckA_SetRoomspaceDefault, size, 0, 0, 0);
        } else {
            set_packet_action(pckt, PckA_SetRoomspaceDrag, 0, 0, 0, 0);
        }
    }
}

void process_build_roomspace_inputs(PlayerNumber plyr_idx)
{
    struct PlayerInfo* player = get_player(plyr_idx);
    struct UserState* ustate = get_local_user_state(); // local input path
    struct Packet* pckt = get_local_packet() /* local input path */;
    if (room_role_matches(ustate->chosen_room_kind,RoRoF_PassLava|RoRoF_PassWater|RoRoF_PassAbyss)) {
        local_state.roomspace_size = DEFAULT_USER_ROOMSPACE_WIDTH;
        TbBool drag_check = ( ( ((is_game_key_pressed(Gkey_BestRoomSpace, false, true) != 0)) || ((is_game_key_pressed(Gkey_SquareRoomSpace, false, true) != 0)) ) && (left_button_held));
        if (drag_check) { // Enable "paint mode" if Ctrl or Shift are held
            set_packet_action(pckt, PckA_SetRoomspaceDragPaint, 0, 0, 0, 0);
        } else {
            set_packet_action(pckt, PckA_SetRoomspaceDrag, 0, 0, 0, 0);
        }
    } else if ((is_game_key_pressed(Gkey_BestRoomSpace, false, true) != 0)) { // Find "best" room
        unsigned char looseness = player->roomspace_detection_looseness;
        if ((is_game_key_pressed(Gkey_RoomSpaceIncSize, false, true) != 0)) {
            if (looseness < tolerate_gold) {
                looseness = tolerate_gold;
            } else if (looseness != tolerate_rock) {
                looseness = tolerate_rock;
            }
        } else if ((is_game_key_pressed(Gkey_RoomSpaceDecSize, false, true) != 0)) {
            if (looseness == tolerate_rock) {
                looseness = tolerate_gold;
            } else if (looseness != disable_tolerance_layers) {
                looseness = disable_tolerance_layers;
            }
        }
        if (looseness != player->roomspace_detection_looseness || player->roomspace_mode != roomspace_detection_mode) {
            set_packet_action(pckt, PckA_SetRoomspaceAuto, looseness, 0, 0, 0);
        }
    } else {
        process_box_roomspace_inputs(pckt);
    }
}

void process_sell_roomspace_inputs(PlayerNumber plyr_idx)
{
    struct Packet* pckt = get_local_packet() /* local input path */;
    if ((is_game_key_pressed(Gkey_SellTrapOnSubtile, false, true) != 0)) {
        set_packet_action(pckt, PckA_SetRoomspaceSubtile, 0, 0, 0, 0);
    } else if ((is_game_key_pressed(Gkey_BestRoomSpace, false, true) != 0)) {
        set_packet_action(pckt, PckA_SetRoomspaceWholeRoom, 0, 0, 0, 0);
    } else {
        process_box_roomspace_inputs(pckt);
    }
}

void process_highlight_roomspace_inputs(PlayerNumber plyr_idx)
{
    struct UserState* ustate = get_local_user_state(); // local input path
    struct PlayerInfo* player = get_player(plyr_idx);
    if ((is_game_key_pressed(Gkey_BestRoomSpace, false, true) != 0)) {
        set_players_packet_action(player, PckA_SetRoomspaceHighlight, settings.highlight_mode ^ 1, settings.highlight_mode, 0, 0);
        reset_roomspace = true;
        return;
    } else if ((is_game_key_pressed(Gkey_SquareRoomSpace, false, true) != 0)) {
        set_players_packet_action(player, PckA_SetRoomspaceHighlight, roomspace_detection_mode, get_roomspace_size_input(), 0, 0);
        reset_roomspace = true;
        return;
    } else if ((is_game_key_pressed(Gkey_SellTrapOnSubtile, false, true) != 0)) {
        if (ustate->primary_cursor_state == CSt_PowerHand && player->roomspace_mode != single_subtile_mode) {
            set_players_packet_action(player, PckA_SetRoomspaceSubtile, 0, 0, 0, 0);
            reset_roomspace = true;
        }
        return;
    } else {
        int64_t par2 = numpad_to_value(false);
        if (par2 > 1) {
            local_state.roomspace_size = par2;
            set_players_packet_action(player, PckA_SetRoomspaceHighlight, roomspace_detection_mode, par2, 0, 0);
            reset_roomspace = true;
            return;
        }
    }
    local_state.roomspace_size = DEFAULT_USER_ROOMSPACE_WIDTH;
    if (reset_roomspace) {
        set_players_packet_action(player, PckA_SetRoomspaceHighlight, settings.highlight_mode, 1, 0, 0);
        reset_roomspace = false; // don't constantly send packets we don't need to
    }
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
