/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontmenu_net.h
 *     Header file for frontmenu_net.c.
 * @par Purpose:
 *     GUI menus for network support.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   KeeperFX Team
 * @date     05 Jan 2009 - 09 Oct 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_FRONTMENU_NET_H
#define DK_FRONTMENU_NET_H

#include "globals.h"

#include "bflib_basics.h"
#include "frontmenu_selectlist.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define frontend_services_menu_items_visible  6

#pragma pack(1)

struct GuiMenu;
struct GuiButton;

#pragma pack()
/******************************************************************************/
extern struct GuiMenu frontend_net_service_menu;
extern struct GuiMenu frontend_net_session_menu;
extern struct GuiMenu frontend_net_start_menu;
extern struct GuiMenu frontend_add_session_box;
/** Service/session/player/message lists page via FrontendSelectList
 *  (docs/refactor/gui/00-overview.md Phase 1). Session/player/message have
 *  no real paging window (no items_visible concept, no wheel/key scroll,
 *  unlike the service list or the campaign/level/mappack selects) -- their
 *  items_visible is fixed at 2, which reproduces their original
 *  `scroll_offset < count - 1` bound and `first_elem = 0` scroll-tab sizing
 *  exactly (see frontmenu_net.c). front_network.c and frontend.cpp write
 *  these directly (`net_session_list.scroll_offset = 0`, etc.) to reset
 *  scroll position when a list's contents change. */
extern struct FrontendSelectList net_service_list;
extern struct FrontendSelectList net_session_list;
extern struct FrontendSelectList net_player_list;
extern struct FrontendSelectList net_message_list;
/******************************************************************************/
void frontnet_session_up_maintain(struct GuiButton *gbtn);
void frontnet_session_down_maintain(struct GuiButton *gbtn);
void frontnet_session_maintain(struct GuiButton *gbtn);
void frontnet_players_up_maintain(struct GuiButton *gbtn);
void frontnet_players_down_maintain(struct GuiButton *gbtn);
void frontnet_join_game_maintain(struct GuiButton *gbtn);
void frontnet_maintain_alliance(struct GuiButton *gbtn);
void frontnet_messages_up_maintain(struct GuiButton *gbtn);
void frontnet_messages_down_maintain(struct GuiButton *gbtn);
void frontnet_start_game_maintain(struct GuiButton *gbtn);
TbBool frontnet_start_input(void);
void frontnet_draw_services_scroll_tab(struct GuiButton *gbtn);
void frontnet_draw_service_button(struct GuiButton *gbtn);
void frontnet_service_maintain(struct GuiButton *gbtn);
void frontnet_service_up_maintain(struct GuiButton *gbtn);
void frontnet_service_down_maintain(struct GuiButton *gbtn);
void frontnet_service_up(struct GuiButton *gbtn);
void frontnet_service_down(struct GuiButton *gbtn);
// frontnet_service_select_by_index is NOT safe to call directly from an
// active ImGui window even though it takes an index -- see its own
// comment (frontmenu_net.c) for why this one needed a different treatment
// than every other extraction in this phase.
void frontnet_service_select_by_index(int64_t srvidx);
void frontnet_service_select(struct GuiButton *gbtn);
void frontnet_session_set_player_name(struct GuiButton *gbtn);
void frontnet_draw_text_bar(struct GuiButton *gbtn);
void frontnet_session_up(struct GuiButton *gbtn);
void frontnet_session_up_maintain(struct GuiButton *gbtn);
void frontnet_session_down(struct GuiButton *gbtn);
void frontnet_session_down_maintain(struct GuiButton *gbtn);
void frontnet_session_maintain(struct GuiButton *gbtn);
void frontnet_draw_sessions_scroll_tab(struct GuiButton *gbtn);
void frontnet_draw_session_selected(struct GuiButton *gbtn);
void frontnet_session_select_by_index(int64_t i);
void frontnet_session_select(struct GuiButton *gbtn);
void frontnet_draw_session_button(struct GuiButton *gbtn);
void frontnet_players_up(struct GuiButton *gbtn);
void frontnet_players_up_maintain(struct GuiButton *gbtn);
void frontnet_players_down(struct GuiButton *gbtn);
void frontnet_players_down_maintain(struct GuiButton *gbtn);
void frontnet_draw_players_scroll_tab(struct GuiButton *gbtn);
void frontnet_draw_net_session_players(struct GuiButton *gbtn);
void frontnet_session_add(struct GuiButton *gbtn);
// _resolve()/by-index siblings below return the FrontendMenuState to
// transition to (int, -1 = nothing to do), for the ImGui screen to
// request itself -- see frontnet_session_join_resolve's comment
// (frontmenu_net.c) for the convention.
int64_t frontnet_session_join_resolve(void);
void frontnet_session_join(struct GuiButton *gbtn);
int64_t frontnet_session_create_resolve(void);
void frontnet_session_create(struct GuiButton *gbtn);
int64_t frontnet_return_to_main_menu_resolve(void);
void frontnet_return_to_main_menu(struct GuiButton *gbtn);
void frontnet_add_session_done(struct GuiButton *gbtn);
void frontnet_add_session_back(struct GuiButton *gbtn);
void frontnet_join_game_maintain(struct GuiButton *gbtn);
void frontnet_draw_alliance_box_tab(struct GuiButton *gbtn);
void frontnet_draw_net_start_players(struct GuiButton *gbtn);
void frontnet_select_alliance_by_index(int64_t plyr1_idx, int64_t plyr2_idx);
void frontnet_select_alliance(struct GuiButton *gbtn);
void frontnet_draw_alliance_grid(struct GuiButton *gbtn);
void frontnet_draw_alliance_button(struct GuiButton *gbtn);
void frontnet_maintain_alliance(struct GuiButton *gbtn);
void frontnet_messages_up(struct GuiButton *gbtn);
void frontnet_messages_up_maintain(struct GuiButton *gbtn);
void frontnet_messages_down(struct GuiButton *gbtn);
void frontnet_messages_down_maintain(struct GuiButton *gbtn);
void frontnet_draw_bottom_scroll_box_tab(struct GuiButton *gbtn);
void frontnet_draw_messages_scroll_tab(struct GuiButton *gbtn);
void frontnet_draw_current_message(struct GuiButton *gbtn);
void frontnet_draw_messages(struct GuiButton *gbtn);
void frontnet_start_game_maintain(struct GuiButton *gbtn);
void frontnet_return_to_session_menu(struct GuiButton *gbtn);
void frontnet_service_select(struct GuiButton *gbtn);
void frontnet_service_up_maintain(struct GuiButton *gbtn);
void frontnet_service_down_maintain(struct GuiButton *gbtn);
void frontnet_service_up(struct GuiButton *gbtn);
void frontnet_service_down(struct GuiButton *gbtn);
void frontnet_service_maintain(struct GuiButton *gbtn);
void frontnet_draw_service_button(struct GuiButton *gbtn);

#define frontnet_draw_scroll_box_tab frontend_draw_scroll_box_tab
#define frontnet_draw_scroll_box frontend_draw_scroll_box
#define frontnet_draw_slider_button frontend_draw_slider_button
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
