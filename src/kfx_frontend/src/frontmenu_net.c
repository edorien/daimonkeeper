/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontmenu_net.c
 *     GUI menus for network support.
 * @par Purpose:
 *     Functions to show and maintain network screens.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     05 Jan 2009 - 09 Oct 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "frontmenu_net.h"
#include "main_game.h"
#include "game_saves.h"
#include "globals.h"
#include "bflib_basics.h"

#include "bflib_datetm.h"
#include "bflib_guibtns.h"
#include "bflib_video.h"
#include "bflib_vidraw.h"
#include "bflib_sprite.h"
#include "bflib_sprfnt.h"

#include "front_network.h"
#include "gui_frontbtns.h"
#include "gui_draw.h"
#include "frontend.h"
#include "front_landview.h"
#include "front_input.h"
#include "net_game.h"
#include "player_data.h"
#include "kjm_input.h"
#include "game_merge.h"
#include "game_legacy.h"
#include "sprites.h"
#include "vidfade.h"
#include "custom_sprites.h"
#include "bflib_enet.h"
#include "net_exchange_common.h"
#include "net_lobby.h"
#include "packets.h"
#include "post_inc.h"

/******************************************************************************/
static int64_t net_service_count(void) { return net_number_of_services; }
static int64_t net_session_count(void) { return net_number_of_sessions; }
static int64_t net_player_count(void) { return net_number_of_enum_players; }
static int64_t net_message_count(void) { return net_number_of_messages; }

// items_visible=2 for session/player/message reproduces their original
// `scroll_offset < count - 1` down-bound and `first_elem = 0` scroll-tab
// sizing exactly -- see frontmenu_net.h's comment on these externs.
struct FrontendSelectList net_service_list = { .items_visible = frontend_services_menu_items_visible, .items_visible_max = frontend_services_menu_items_visible, .item_count = net_service_count, .row_base = FE_SELECTLIST_ROW_BASE };
struct FrontendSelectList net_session_list = { .items_visible = 2, .item_count = net_session_count, .row_base = FE_SELECTLIST_ROW_BASE };
struct FrontendSelectList net_player_list = { .items_visible = 2, .item_count = net_player_count, .row_base = FE_SELECTLIST_ROW_BASE };
struct FrontendSelectList net_message_list = { .items_visible = 2, .item_count = net_message_count, .row_base = FE_SELECTLIST_ROW_BASE };

void frontnet_session_up_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_up_maintain(&net_session_list, gbtn);
}

void frontnet_session_down_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_down_maintain(&net_session_list, gbtn);
}

void frontnet_session_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_row_maintain(&net_session_list, gbtn);
}

void frontnet_players_up_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_up_maintain(&net_player_list, gbtn);
}

void frontnet_players_down_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_down_maintain(&net_player_list, gbtn);
}

static TbBool frontnet_can_join_session(void)
{
    return (net_session_index_active >= 0)
        && (net_session_index_active < net_number_of_sessions)
        && (net_session[net_session_index_active] != NULL);
}

void frontnet_join_game_maintain(struct GuiButton *gbtn)
{
    gbtn->flags ^= (gbtn->flags ^ LbBtnF_Enabled * frontnet_can_join_session()) & LbBtnF_Enabled;
}

void frontnet_maintain_alliance(struct GuiButton *gbtn)
{
    int64_t plyr_idx1;
    int64_t plyr_idx2;
    plyr_idx1 = gbtn->btype_value & LbBFeF_IntValueMask;
    plyr_idx2 = gbtn->content.lval - 74;
    if ( plyr_idx2 >= net_number_of_enum_players || net_number_of_enum_players <= plyr_idx1 || plyr_idx2 == plyr_idx1 )
      gbtn->flags &= ~LbBtnF_Enabled;
    else
      gbtn->flags |= LbBtnF_Enabled;
}

void frontnet_messages_up_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_up_maintain(&net_message_list, gbtn);
}

void frontnet_messages_down_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_down_maintain(&net_message_list, gbtn);
}

void frontnet_start_game_maintain(struct GuiButton *gbtn)
{
    TbBool enabled = net_number_of_enum_players > 1;
    gbtn->flags ^= (gbtn->flags ^ LbBtnF_Enabled * enabled) & LbBtnF_Enabled;
}

TbBool frontnet_start_input(void)
{
    struct PlayerInfo *player = get_my_player();
    if (lbInkey == KC_RETURN) {
        if (player->mp_message_text[0] != '\0') {
            send_network_chat_message(netstate.my_id, player->mp_message_text);
        }
        process_frontend_chat_message(netstate.my_id, player->mp_message_text);
        memset(player->mp_message_text, 0, PLAYER_MP_MESSAGE_LEN);
    } else if (lbInkey == KC_ESCAPE) {
        player->mp_message_text[0] = '\0';
    } else if (is_key_pressed(KC_BACK,KMod_DONTCARE)){
        int64_t chpos = strlen(player->mp_message_text);
        // Skip UTF-8 continuation bytes so the whole last character is removed
        while ((chpos > 0) && ((player->mp_message_text[chpos-1] & 0xc0) == 0x80))
            chpos--;
        if (chpos > 0)
            player->mp_message_text[chpos-1] = '\0';
        clear_key_pressed(KC_BACK);
    } else {
        add_input_text_to_message(player->mp_message_text, PLAYER_MP_MESSAGE_LEN, frontend_font[1] , 420);
    }
    lbInkey = KC_UNASSIGNED;
    return true;
}

void frontnet_draw_services_scroll_tab(struct GuiButton *gbtn)
{
    frontend_selectlist_draw_scroll_tab(&net_service_list, gbtn);
}

void frontnet_session_set_player_name(struct GuiButton *gbtn)
{
    strcpy(net_player_name, tmp_net_player_name);
    strcpy(net_config_info.net_player_name, tmp_net_player_name);
    net_write_config_file();
}

void frontnet_draw_text_bar(struct GuiButton *gbtn)
{
    const struct TbSprite *spr;
    int64_t i;
    int64_t pos_x;
    int64_t pos_y;
    pos_x = gbtn->scr_pos_x;
    pos_y = gbtn->scr_pos_y;
    int64_t fs_units_per_px;
    fs_units_per_px = simple_frontend_sprite_height_units_per_px(gbtn, GFS_largearea_nx1_tx5_c, 100);
    spr = get_frontend_sprite(GFS_largearea_nx1_cor_l);
    LbSpriteDrawResized(pos_x, pos_y, fs_units_per_px, spr);
    pos_x += spr->SWidth * fs_units_per_px / 16;
    spr = get_frontend_sprite(GFS_largearea_nx1_tx5_c);
    for (i=0; i < 4; i++)
    {
        LbSpriteDrawResized(pos_x, pos_y, fs_units_per_px, spr);
        pos_x += spr->SWidth * fs_units_per_px / 16;
    }
    spr = get_frontend_sprite(GFS_largearea_nx1_cor_r);
    LbSpriteDrawResized(pos_x, pos_y, fs_units_per_px, spr);
}

void frontnet_session_up(struct GuiButton *gbtn)
{
    frontend_selectlist_scroll_up(&net_session_list);
}

void frontnet_session_down(struct GuiButton *gbtn)
{
    frontend_selectlist_scroll_down(&net_session_list);
}

void frontnet_draw_sessions_scroll_tab(struct GuiButton *gbtn)
{
    frontend_selectlist_draw_scroll_tab(&net_session_list, gbtn);
}

void frontnet_players_up(struct GuiButton *gbtn)
{
    frontend_selectlist_scroll_up(&net_player_list);
}

void frontnet_players_down(struct GuiButton *gbtn)
{
    frontend_selectlist_scroll_down(&net_player_list);
}

void frontnet_draw_players_scroll_tab(struct GuiButton *gbtn)
{
    frontend_selectlist_draw_scroll_tab(&net_player_list, gbtn);
}

void frontnet_draw_net_session_players(struct GuiButton *gbtn)
{
    int64_t i;
    i = frontend_button_caption_font(gbtn, 0);
    RendererSetDrawFlags(0);
    LbTextSetFont(frontend_font[i]);
    int64_t tx_units_per_px;
    tx_units_per_px = gbtn->height * 16 / (2*LbTextLineHeight());
    const struct TbSprite *spr;
    spr = get_frontend_sprite(GFS_bullfrog_red_med);
    int64_t fs_units_per_px;
    fs_units_per_px = gbtn->height * 16 / (2*(spr->SHeight*13/8));
    int64_t height;
    height = LbTextLineHeight() * tx_units_per_px / 16;
    int64_t netplyr_idx;
    int64_t shift_y;
    netplyr_idx = net_player_list.scroll_offset;
    for (shift_y=0; shift_y < gbtn->height; shift_y += height, netplyr_idx++)
    {
        const char *text;
        text = net_player[netplyr_idx].name;
        if (netplyr_idx >= net_number_of_enum_players)
            break;
        spr = get_frontend_sprite(GFS_bullfrog_red_med+netplyr_idx);
        i = height - spr->SHeight * fs_units_per_px / 16;
        LbSpriteDrawResized(gbtn->scr_pos_x, gbtn->scr_pos_y + shift_y + llabs(i)/2, fs_units_per_px, spr);
        LbTextSetWindow(gbtn->scr_pos_x, shift_y + gbtn->scr_pos_y, gbtn->width - spr->SWidth * fs_units_per_px / 16, height);
        LbTextDrawResized(spr->SWidth * fs_units_per_px / 16, 0, tx_units_per_px, text);
    }
}

void frontnet_session_add(struct GuiButton *gbtn)
{
    turn_on_menu(GMnu_FEADD_SESSION);
    //TODO NET When clicked, it should display a modal text field (for IP address) and OK/Cancel buttons.
    set_menu_visible_on(GMnu_FEADD_SESSION);
}

/** frontnet_session_join's actual work, minus the state-transition call
 * itself: returns the FrontendMenuState to transition to (int, -1 =
 * nothing to do / join failed). Split out so the ImGui screen can request
 * the transition itself (frontend_set_state() is unsafe to call
 * synchronously from inside an active ImGui window) while the legacy
 * click_event below keeps calling frontend_set_state() directly,
 * unchanged -- same convention Phase E's frontend_land_selection_enter_resolve
 * established.
 */
int64_t frontnet_session_join_resolve(void)
{
    int64_t plyr_num;
    if (!frontnet_can_join_session())
        return -1;
    plyr_num = network_session_join();
    if (plyr_num < 0)
        return -1;
    frontend_set_player_number(plyr_num);
    return FeSt_NET_START;
}

void frontnet_session_join(struct GuiButton *gbtn)
{
    int64_t next_state = frontnet_session_join_resolve();
    if (next_state >= 0)
        frontend_set_state((FrontendMenuState)next_state);
}

/** frontnet_return_to_main_menu's actual work, minus the state-transition
 * call itself -- see frontnet_session_join_resolve's comment for why.
 */
int64_t frontnet_return_to_main_menu_resolve(void)
{
  if ( LbNetwork_Stop() )
  {
    ERRORLOG("LbNetwork_Stop() failed");
    return -1;
  }
  return FeSt_MAIN_MENU;
}

void frontnet_return_to_main_menu(struct GuiButton *gbtn)
{
    int64_t next_state = frontnet_return_to_main_menu_resolve();
    if (next_state >= 0)
        frontend_set_state((FrontendMenuState)next_state);
}

void frontnet_add_session_back(struct GuiButton *gbtn)
{
    //TODO NET Finish session add menu
    turn_off_menu(GMnu_FEADD_SESSION);
}

void frontnet_add_session_done(struct GuiButton *gbtn)
{
    //TODO NET Finish session add menu
    turn_off_menu(GMnu_FEADD_SESSION);
}

void frontnet_draw_alliance_box_tab(struct GuiButton *gbtn)
{
    int64_t units_per_px;
    units_per_px = (gbtn->width * 16 + 100/2) / 100;

    const struct TbSprite *spr;
    int64_t pos_x;
    int64_t pos_y;

    pos_x = gbtn->scr_pos_x;
    pos_y = gbtn->scr_pos_y;
    spr = get_frontend_sprite(GFS_hugearea_thc_cor_tl);
    int64_t fs_units_per_px;
    fs_units_per_px = spr->SHeight * units_per_px / 26;
    LbSpriteDrawResized(pos_x, pos_y, fs_units_per_px, spr);
    pos_x += spr->SWidth*fs_units_per_px/16;
    spr = get_frontend_sprite(GFS_hugearea_thc_tx2_tc);
    LbSpriteDrawResized(pos_x, pos_y, fs_units_per_px, spr);
    pos_x += spr->SWidth*fs_units_per_px/16;
    spr = get_frontend_sprite(GFS_hugearea_thc_cor_tr);
    LbSpriteDrawResized(pos_x, pos_y, fs_units_per_px, spr);

    pos_y += 5;
    pos_x = gbtn->scr_pos_x;
    spr = get_frontend_sprite(GFS_hugearea_thc_cor_tl);
    pos_x += spr->SWidth*fs_units_per_px/16 - 1;
    if (net_number_of_enum_players > 0)
    {
        spr = get_frontend_sprite(GFS_bullfrog_red_med);
        LbSpriteDrawResized(pos_x, pos_y, fs_units_per_px, spr);
        pos_x += spr->SWidth*fs_units_per_px/16;
        pos_x += (4 * fs_units_per_px + 8) / 16;
    }
    if (net_number_of_enum_players > 1)
    {
        spr = get_frontend_sprite(GFS_bullfrog_blue_med);
        LbSpriteDrawResized(pos_x, pos_y, fs_units_per_px, spr);
        pos_x += spr->SWidth*fs_units_per_px/16;
        pos_x += (4 * fs_units_per_px + 8) / 16;
    }
    if (net_number_of_enum_players > 2)
    {
        spr = get_frontend_sprite(GFS_bullfrog_green_med);
        LbSpriteDrawResized(pos_x, pos_y, fs_units_per_px, spr);
        pos_x += spr->SWidth*fs_units_per_px/16;
        pos_x += (4 * fs_units_per_px + 8) / 16;
    }
    if (net_number_of_enum_players > 3)
    {
        spr = get_frontend_sprite(GFS_bullfrog_yellow_med);
        LbSpriteDrawResized(pos_x, pos_y, fs_units_per_px, spr);
    }
}

void frontnet_draw_net_start_players(struct GuiButton *gbtn)
{
    int64_t i;
    i = frontend_button_caption_font(gbtn, 0);
    RendererSetDrawFlags(0);
    LbTextSetFont(frontend_font[i]);
    int64_t height;
    height = 0;
    int64_t netplyr_idx;
    int64_t shift_y;
    netplyr_idx = net_player_list.scroll_offset;
    int64_t tx_units_per_px;
    tx_units_per_px = gbtn->height * 16 / (4*LbTextLineHeight());
    const struct TbSprite *spr;
    spr = get_frontend_sprite(GFS_bullfrog_red_med);
    int64_t fs_units_per_px;
    fs_units_per_px = gbtn->height * 16 / (4*(spr->SHeight*13/8));
    height = LbTextLineHeight() * tx_units_per_px / 16;
    for (shift_y=0; shift_y < gbtn->height; shift_y += height, netplyr_idx++)
    {
        const char *text;
        text = net_player[netplyr_idx].name;
        if (netplyr_idx >= net_number_of_enum_players)
            break;

        NetUserId subplyr_idx;
        for (subplyr_idx = 0; subplyr_idx < net_number_of_enum_players; subplyr_idx++)
        {
            if (subplyr_idx >= MAX_NET_USERS)
                break;
            if (net_user_info[subplyr_idx].network_user_active)
            {
                if (subplyr_idx == netplyr_idx)
                    break;
            }
        }
        spr = get_frontend_sprite(GFS_bullfrog_red_med+netplyr_idx);
        i = height - spr->SHeight * fs_units_per_px / 16;
        LbSpriteDrawResized(gbtn->scr_pos_x, gbtn->scr_pos_y + shift_y + llabs(i)/2, fs_units_per_px, spr);

        char player_text[128];
        uint64_t ping = 0;
        if (netplyr_idx != my_player_number) {
            ping = GetPing(netplyr_idx, my_player_number);
        }
        if (ping > 0) {
            snprintf(player_text, sizeof(player_text), "%s - %" PRIu64 "ms", text, (uint64_t)(ping));
        } else {
            snprintf(player_text, sizeof(player_text), "%s", text);
        }

        LbTextSetWindow(gbtn->scr_pos_x + spr->SWidth * fs_units_per_px / 16, gbtn->scr_pos_y + shift_y, gbtn->width - spr->SWidth * fs_units_per_px / 16, height);
        LbTextDrawResized(0, 0, tx_units_per_px, player_text);
    }
}

/** Toggles the alliance between plyr1_idx/plyr2_idx (queued via the
 * screen-packet system, not applied immediately -- same as the legacy
 * click_event, so an ImGui checkbox toggled this way shows the change
 * with the same one-frame-or-so lag the legacy sprite grid already has).
 * No frontend_set_state() call anywhere in this one, so it's directly
 * safe to call from an active ImGui window, same as
 * frontnet_session_select_by_index. Shared by the legacy click_event
 * (frontnet_select_alliance, below) and the ImGui screen
 * (frontgui_screens.cpp), which iterates player indices directly rather
 * than decoding them from two different GuiButton fields.
 */
void frontnet_select_alliance_by_index(int64_t plyr1_idx, int64_t plyr2_idx)
{
    struct PlayerInfo *myplyr;
    myplyr = get_my_player();
    if ( plyr1_idx == myplyr->id_number || plyr2_idx == myplyr->id_number )
    {
        struct ScreenPacket *nspck;
        nspck = &net_screen_packet[my_player_number];
        if (screen_packet_action(nspck) == NetAct_None)
        {
            screen_packet_set_action(nspck, NetAct_SetAlliance);
            nspck->action_par1 = plyr1_idx;
            nspck->action_par2 = plyr2_idx;
        }
    }
}

void frontnet_select_alliance(struct GuiButton *gbtn)
{
    int64_t plyr1_idx;
    int64_t plyr2_idx;
    plyr1_idx = gbtn->content.lval - 74;
    plyr2_idx = gbtn->btype_value & LbBFeF_IntValueMask;
    frontnet_select_alliance_by_index(plyr1_idx, plyr2_idx);
}

void frontnet_draw_alliance_grid(struct GuiButton *gbtn)
{
    int64_t pos_x;
    int64_t pos_y;
    pos_y = gbtn->scr_pos_y;
    const struct TbSprite *spr;
    int64_t netplyr_idx;
    int64_t units_per_px;
    units_per_px = gbtn->height * 16 / (22*4);

    pos_x = gbtn->scr_pos_x;
    spr = get_frontend_sprite(GFS_slidrect_indicator_std0);
    for (netplyr_idx=0; netplyr_idx < MAX_NET_USERS; netplyr_idx++)
    {
        LbSpriteDrawResized(pos_x / pixel_size, pos_y / pixel_size, units_per_px, spr);
        pos_x += spr->SWidth * units_per_px / 16;
    }
    pos_y += spr->SHeight * units_per_px / 16;

    pos_x = gbtn->scr_pos_x;
    spr = get_frontend_sprite(GFS_slidrect_indicator_std1);
    for (netplyr_idx=0; netplyr_idx < MAX_NET_USERS; netplyr_idx++)
    {
        LbSpriteDrawResized(pos_x / pixel_size, pos_y / pixel_size, units_per_px, spr);
        pos_x += spr->SWidth * units_per_px / 16;
    }
    pos_y += spr->SHeight * units_per_px / 16;

    pos_x = gbtn->scr_pos_x;
    spr = get_frontend_sprite(GFS_slidrect_indicator_std2);
    for (netplyr_idx=0; netplyr_idx < MAX_NET_USERS; netplyr_idx++)
    {
        LbSpriteDrawResized(pos_x / pixel_size, pos_y / pixel_size, units_per_px, spr);
        pos_x += spr->SWidth * units_per_px / 16;
    }
    pos_y += spr->SHeight * units_per_px / 16;

    pos_x = gbtn->scr_pos_x;
    spr = get_frontend_sprite(GFS_slidrect_indicator_std2);
    for (netplyr_idx=0; netplyr_idx < MAX_NET_USERS; netplyr_idx++)
    {
        LbSpriteDrawResized(pos_x / pixel_size, pos_y / pixel_size, units_per_px, spr);
        pos_x += spr->SWidth * units_per_px / 16;
    }
    pos_y += spr->SHeight * units_per_px / 16;
}

void frontnet_draw_alliance_button(struct GuiButton *gbtn)
{
    int64_t plyr1_idx;
    int64_t plyr2_idx;
    const struct TbSprite *spr;
    plyr2_idx = gbtn->btype_value & LbBFeF_IntValueMask;
    plyr1_idx = gbtn->content.lval - 74;
    if ((plyr1_idx == plyr2_idx) || (frontend_alliances & alliance_grid[plyr1_idx][plyr2_idx]))
      spr = get_frontend_sprite(GFS_scrollbar_indicator_std);
    else
      spr = get_frontend_sprite(GFS_slidrect_indicator_std1);
    int64_t units_per_px;
    units_per_px = gbtn->height * 16 / spr->SHeight;
    LbSpriteDrawResized(gbtn->scr_pos_x, gbtn->scr_pos_y, units_per_px, spr);
}

void frontnet_messages_up(struct GuiButton *gbtn)
{
    frontend_selectlist_scroll_up(&net_message_list);
}

void frontnet_messages_down(struct GuiButton *gbtn)
{
    frontend_selectlist_scroll_down(&net_message_list);
}

void frontnet_draw_bottom_scroll_box_tab(struct GuiButton *gbtn)
{
    int64_t units_per_px;
    units_per_px = (gbtn->width * 16 + 240/2) / 240;

    int64_t pos_x;
    int64_t pos_y;
    const struct TbSprite *spr;
    pos_x = gbtn->scr_pos_x;
    pos_y = gbtn->scr_pos_y;
    RendererSetDrawFlags(Lb_SPRITE_FLIP_VERTIC);
    spr = get_frontend_sprite(GFS_hugearea_thc_cor_tl);
    int64_t fs_units_per_px;
    fs_units_per_px = spr->SHeight * units_per_px / 26;
    LbSpriteDrawResized(pos_x, pos_y, fs_units_per_px, spr);
    pos_x += spr->SWidth*fs_units_per_px/16;
    spr = get_frontend_sprite(GFS_hugearea_thc_tx1_tc);
    LbSpriteDrawResized(pos_x, pos_y, fs_units_per_px, spr);
    pos_x += spr->SWidth*fs_units_per_px/16;
    LbSpriteDrawResized(pos_x, pos_y, fs_units_per_px, spr);
    pos_x += spr->SWidth*fs_units_per_px/16;
    spr = get_frontend_sprite(GFS_hugearea_thc_cor_tr);
    LbSpriteDrawResized(pos_x, pos_y, fs_units_per_px, spr);
    RendererSetDrawFlags(0);
}

void frontnet_draw_messages_scroll_tab(struct GuiButton *gbtn)
{
    frontend_selectlist_draw_scroll_tab(&net_message_list, gbtn);
}

void frontnet_draw_scroll_selection_box(struct GuiButton *gbtn, int64_t font_idx, const char *text)
{
    const struct TbSprite * spr;
    int64_t pos_x;
    int64_t i;
    unsigned char height;
    spr = get_frontend_sprite(GFS_largearea_xts_cor_l);
    int64_t fs_units_per_px;
    fs_units_per_px = gbtn->height * 16 / spr->SHeight;
    pos_x = gbtn->scr_pos_x;
    for (i = 6; i > 0; i--)
    {
        LbSpriteDrawResized(pos_x, gbtn->scr_pos_y, fs_units_per_px, spr);
        pos_x += spr->SWidth * fs_units_per_px / 16;
        spr++;
    }

    if (text != NULL)
    {
        LbTextSetFont(frontend_font[font_idx]);
        RendererSetDrawFlags(0);
        int64_t tx_units_per_px;
        tx_units_per_px = (gbtn->height*13/14) * 16 / LbTextLineHeight();
        height = LbTextLineHeight() * tx_units_per_px / 16;
        LbTextSetWindow(gbtn->scr_pos_x + 13*fs_units_per_px/16, gbtn->scr_pos_y, gbtn->width - 26*fs_units_per_px/16, height);
        LbTextDrawResized(0, 0, tx_units_per_px, text);
    }
}

void frontnet_draw_current_message(struct GuiButton *gbtn)
{
    static TbClockMSec last_time = 0;
    static TbBool print_with_cursor = 1;

    struct PlayerInfo *player;
    int64_t font_idx;
    char text[2048];
    // Blink cursor - switch state every 100ms
    if (LbTimerClock() >= last_time + 100)
    {
        print_with_cursor = !print_with_cursor;
        last_time = LbTimerClock();
    }

    // Get player
    player = get_my_player();
    if (player_invalid(player)) {
        return;
    }

    // Prepare text buffer and font
    snprintf(text, sizeof(text), "%s%s", player->mp_message_text, print_with_cursor?"_":"");
    font_idx = frontend_button_caption_font(gbtn, 0);
    // And draw the message
    frontnet_draw_scroll_selection_box(gbtn, font_idx, text);
}

void frontnet_draw_messages(struct GuiButton *gbtn)
{
    static TbPixel player_left_remap[PALETTE_COLORS];
    static int64_t player_left_remap_initialized;
    if (!player_left_remap_initialized) {
        for (int64_t i = 0; i < PALETTE_COLORS; i++) {
            int64_t intensity = max(frontend_palette[3*i], max(frontend_palette[3*i+1], frontend_palette[3*i+2]));
            player_left_remap[i] = TbPixel_RGB(chan6_to_8(intensity*5/8), chan6_to_8(intensity*3/8), chan6_to_8(intensity/4));
        }
        player_left_remap_initialized = 1;
    }
    int64_t font_idx;
    font_idx = frontend_button_caption_font(gbtn, 0);
    LbTextSetFont(frontend_font[font_idx]);
    RendererSetDrawFlags(0);
    // While setting scale, aim for 4 lines of text
    int64_t tx_units_per_px;
    tx_units_per_px = gbtn->height * 16 / (4*LbTextLineHeight());
    const struct TbSprite *spr;
    spr = get_frontend_sprite(GFS_bullfrog_red_med);
    int64_t fs_units_per_px;
    fs_units_per_px = gbtn->height * 16 / (4*(spr->SHeight*13/8));
    int64_t font_height;
    font_height = LbTextLineHeight() * tx_units_per_px / 16;
    int64_t y;
    y = 0;
    int64_t netmsg_id;
    for (netmsg_id=net_message_list.scroll_offset; netmsg_id < net_number_of_messages; netmsg_id++)
    {
        if (y + font_height/2 > gbtn->height)
            break;
        struct NetMessage *nmsg;
        nmsg = &net_message[netmsg_id];
        TbBool sender_active = nmsg->connection_id == net_user_info[nmsg->plyr_idx].connection_id;
        int64_t num_active;
        num_active = 0;
        if (sender_active) {
            for (int64_t i = nmsg->plyr_idx; i > 0; i--) {
                if (net_user_info[i].network_user_active) {
                    num_active++;
                }
            }
        }

        spr = get_frontend_sprite(GFS_bullfrog_red_med+num_active);

        int64_t icon_y = font_height - spr->SHeight * fs_units_per_px / 16;
        if (sender_active) {
            LbSpriteDrawResized(gbtn->scr_pos_x, y + gbtn->scr_pos_y + (icon_y >> 1), fs_units_per_px, spr);
        } else {
            lbSpriteReMapPtr = player_left_remap;
            RendererSetDrawFlags(Lb_TEXT_REMAP);
            LbSpriteDrawResizedRemap(gbtn->scr_pos_x, y + gbtn->scr_pos_y + (icon_y >> 1), fs_units_per_px, spr, player_left_remap);
        }

        LbTextSetWindow(gbtn->scr_pos_x, y + gbtn->scr_pos_y, gbtn->width, min(font_height, gbtn->height-y));
        LbTextDrawResized(spr->SWidth * fs_units_per_px / 16, 0, tx_units_per_px, nmsg->text);
        RendererSetDrawFlags(0);

        y += font_height;
    }
}

void frontnet_return_to_session_menu(struct GuiButton *gbtn)
{
    if (LbNetwork_Stop()) {
        ERRORLOG("LbNetwork_Stop() failed");
    }
    FrontendMenuState nstate;
    nstate = get_menu_state_when_back_from_substate(FeSt_NET_START);
    if (nstate == FeSt_NET_SESSION)
    {
        // If the parent state is network session state, try to stay in net service
        if (!setup_old_network_service()) {
            nstate = get_menu_state_when_back_from_substate(nstate);
        }
    }
    frontend_set_state(nstate);
}

void frontnet_service_up_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_up_maintain(&net_service_list, gbtn);
}

void frontnet_service_down_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_down_maintain(&net_service_list, gbtn);
}

void frontnet_service_up(struct GuiButton *gbtn)
{
    frontend_selectlist_scroll_up(&net_service_list);
}

void frontnet_service_down(struct GuiButton *gbtn)
{
    frontend_selectlist_scroll_down(&net_service_list);
}

void frontnet_service_maintain(struct GuiButton *gbtn)
{
    frontend_selectlist_row_maintain(&net_service_list, gbtn);
}

void frontnet_draw_service_button(struct GuiButton *gbtn)
{
  int64_t srvidx;
  // Find and verify selected network service
  srvidx = frontend_selectlist_row_to_item_index(&net_service_list, gbtn);
  if (srvidx >= net_number_of_services)
    return;
  // Select font to draw
  int64_t font_idx;
  font_idx = frontend_button_caption_font(gbtn,frontend_mouse_over_button);
  LbTextSetFont(frontend_font[font_idx]);
  RendererSetDrawFlags(Lb_TEXT_HALIGN_LEFT);
  // Set drawing window and draw the text
  int64_t tx_units_per_px;
  tx_units_per_px = gbtn->height * 16 / LbTextLineHeight();
  int64_t height;
  height = LbTextLineHeight() * tx_units_per_px / 16;
  LbTextSetWindow(gbtn->scr_pos_x, gbtn->scr_pos_y, gbtn->width, height);
  LbTextDrawResized(0, 0, tx_units_per_px, net_service[srvidx]);
}

/** frontnet_service_select's actual work, keyed by index directly rather
 * than decoding one from a row button's content.lval -- shared by the
 * legacy click_event (frontnet_service_select, below) and the ImGui
 * screen (frontgui_screens.cpp), which iterates net_service[] directly
 * and already has a real index. Unlike every other _resolve()-style
 * extraction so far, this one is NOT safe to call directly from an active
 * ImGui window even once split: setup_network_service() (kfx_net/
 * net_game.c) itself calls frontend_set_state() three layers down, via
 * ui_enter_net_session_screen() -- across the kfx_net/
 * kfx_frontend layering boundary, too deep to return a target state the
 * way the single-layer cases elsewhere in this file could. The ImGui
 * screen defers this whole function via frontgui_screens.cpp's generic
 * pending-action mechanism instead of calling it inline.
 */
void frontnet_service_select_by_index(int64_t srvidx)
{
  // Used to have a special-case branch here for Skirmish, appended as an
  // extra row past the real services -- moved to its own Main Menu button
  // (frontend_start_skirmish(), frontend.cpp), so net_service[] now only
  // ever lists real network services and srvidx always indexes one directly.
  if (srvidx < 0)
  {
      frontend_set_state(FeSt_NET_SERVICE);
  } else
  {
      setup_network_service(srvidx);
  }
}

void frontnet_service_select(struct GuiButton *gbtn)
{
  int64_t srvidx;
  srvidx = frontend_selectlist_row_to_item_index(&net_service_list, gbtn);
  frontnet_service_select_by_index(srvidx);
}

/******************************************************************************/
