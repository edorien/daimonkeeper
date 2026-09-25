/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file front_network.h
 *     Header file for front_network.c.
 * @par Purpose:
 *     Front-end menus for network games.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   KeeperFX Team
 * @date     11 Mar 2010 - 09 Oct 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_FRONTNET_H
#define DK_FRONTNET_H

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_coroutine.h"
#include "bflib_netsession.h"
#include "net_main.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
#define NET_SERVICE_LEN 64

// enum FrontendNetService moved to net_main.h (kfx_net) -- see there.

#pragma pack(1)

/******************************************************************************/

struct ConfigInfo {
    char str_join[20];
    char net_player_name[20];
};

// fe_network_active/net_service_index_selected/net_session[]/
// net_session_index_active/net_player[]/net_player_name/net_screen_packet
// moved to net_main.h (kfx_net) -- see there. Only the fields net_game.c
// never touched (frontend-only session-browser state) stay here.
extern int64_t net_number_of_sessions;
extern struct ConfigInfo net_config_info;
extern char net_service[16][NET_SERVICE_LEN];
extern char tmp_net_player_name[24];

#pragma pack()
/******************************************************************************/
void process_network_error(int64_t errcode);
void draw_out_of_sync_box(int64_t a1, int64_t a2, int64_t box_width);
void display_attempting_to_join_message(int64_t remaining_s);
void reset_attempting_to_join_cancel(void);
TbBool attempting_to_join_cancel_requested(void);
void setup_alliances(void);
void frontnet_service_setup(void);
void frontnet_session_setup(void);
void frontnet_start_setup(void);
void frontnet_service_update(void);
void frontnet_session_update(void);
void frontnet_start_update(void);
TbBool frontnet_matchmaking_update(void);
TbBool frontnet_start_level(const char *campaign_fname, LevelNumber lvnum);
void process_frontend_chat_message(NetUserId user, const char *message);
TbBool frontnet_service_selected(enum FrontendNetService service);
void enum_sessions_callback(struct TbNetworkCallbackData *netcdat, void *ptr);

void net_load_config_file(void);
void net_write_config_file(void);

void frontnet_send_campaign_change_message(const char *campaign_fname);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
