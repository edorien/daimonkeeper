/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file gui_msgs.c
 *     Game GUI Messages functions.
 * @par Purpose:
 *     Functions to display and maintain GUI Messages.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     14 May 2010 - 21 Nov 2012
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "config_keeperfx.h" // ingame_gui_use_classic_hud
#include "gui_msgs.h"
#include <stdarg.h>

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_sprfnt.h"
#include "bflib_vidraw.h"
#include "config_spritecolors.h"
#include "creature_graphics.h"
#include "creature_instances.h"
#include "gui_draw.h"
#include "frontend.h"
#include "game_legacy.h"
#include "frontmenu_ingame_evnt.h"
#include "sprites.h"
#include "custom_sprites.h"
#include "front_input.h"
#include "kfx_frontend_state.h"
#include "post_inc.h"

/******************************************************************************/

/** Phase 3: the panel-sprite index for message i's left-hand icon, already
 *  player-colour-remapped where the type calls for it. -1 == no icon.
 *  Mirrors the per-type switch in message_draw() below (minus its x/y
 *  nudges) so the ImGui message overlay (frontgui_ingame_messages.cpp) can
 *  reuse the exact icon logic. */
short message_icon_spridx(int i)
{
    if ((i < 0) || (i >= kfx_sim_state.active_messages_count))
        return -1;
    const struct GuiMessage *msg = &kfx_sim_state.messages[i];
    PlayerNumber plyr_idx = msg->plyr_idx;
    switch (msg->type)
    {
        case MsgType_Player:
            if (player_is_roaming(plyr_idx))
                return get_player_colored_icon_idx(GPS_plyrsym_symbol_player_red_std_b, plyr_idx);
            if (msg->plyr_idx == kfx_config_state.neutral_player_num)
                return (short)(((get_gameturn() >> 1) & 3) + GPS_plyrsym_symbol_player_red_std_b);
            return get_player_colored_icon_idx(
                player_has_heart(msg->plyr_idx) ? GPS_plyrsym_symbol_player_red_std_b
                                                : GPS_plyrsym_symbol_player_red_dead,
                plyr_idx);
        case MsgType_Creature:
            return (short)get_creature_model_graphics(msg->plyr_idx, CGI_HandSymbol);
        case MsgType_CreatureSpell:
            return get_spell_config(msg->plyr_idx)->medsym_sprite_idx;
        case MsgType_Room:
            return get_room_kind_stats(msg->plyr_idx)->medsym_sprite_idx;
        case MsgType_KeeperSpell:
            return get_power_model_stats(msg->plyr_idx)->medsym_sprite_idx;
        case MsgType_Query:
            return (short)(msg->plyr_idx + GPS_plyrsym_symbol_room_yellow_std_a);
        case MsgType_Custom:
            return msg->plyr_idx;
        case MsgType_CreatureInstance:
            return creature_instance_info_get(msg->plyr_idx)->symbol_spridx;
        case MsgType_Blank:
        default:
            return -1;
    }
}

void message_draw(void)
{
    SYNCDBG(7,"Starting");
    // Phase 3: the ImGui message overlay (frontgui_ingame_messages.cpp)
    // draws the queue instead when the ImGui in-game HUD is active -- same
    // kfx_sim_state.messages[] source, same target-idx filter. Only reached
    // for the classic HUD (ingame_gui_use_classic_hud()).
    if (!ingame_gui_use_classic_hud())
        return;
    LbTextSetFont(winfont);
    int ps_units_per_px;
    const struct TbSprite* spr;
    {
        //just used for height, color irrelevant here
        spr = get_panel_sprite(GPS_plyrsym_symbol_player_red_std_b);
        ps_units_per_px = (22 * units_per_pixel) / spr->SHeight;
    }
    TbBool low_res = (MyScreenHeight < 400);
    int tx_units_per_px = ( (low_res) && (dbc_initialized && dbc_enabled) ) ? ps_units_per_px : (22 * units_per_pixel) / LbTextLineHeight();
    int h = LbTextLineHeight();
    long y = 28 * units_per_pixel / 16;
    if (kfx_sim_state.armageddon_cast_turn != 0)
    {
        if ( (bonus_timer_enabled()) || (script_timer_enabled()) || display_variable_enabled() )
        {
            y += (h*units_per_pixel/16) << (unsigned char)low_res;
        }
    }
    for (int i = 0; i < kfx_sim_state.active_messages_count; i++)
    {
        if ( (kfx_sim_state.messages[i].target_idx == my_player_number) || (kfx_sim_state.messages[i].target_idx == -1) )
        {
            long x = 148 * units_per_pixel / 16;
            LbTextSetWindow(0, 0, MyScreenWidth, MyScreenHeight);
            RendererClearDrawFlags(Lb_TEXT_ONE_COLOR);
            LbTextDrawResized(x+32*units_per_pixel/16, y, tx_units_per_px, kfx_sim_state.messages[i].text);
            unsigned long spr_idx = 0;
            PlayerNumber plyr_idx = kfx_sim_state.messages[i].plyr_idx;
            switch (kfx_sim_state.messages[i].type)
            {
                case MsgType_Player:
                {
                    if (player_is_roaming(plyr_idx))
                    {
                        spr_idx = GPS_plyrsym_symbol_player_red_std_b;
                    }
                    else if (kfx_sim_state.messages[i].plyr_idx == kfx_config_state.neutral_player_num)
                    {
                        spr_idx = ((get_gameturn() >> 1) & 3) + GPS_plyrsym_symbol_player_red_std_b;
                        plyr_idx = 0;
                    }
                    else
                    {
                        spr_idx = (player_has_heart(kfx_sim_state.messages[i].plyr_idx)) ? GPS_plyrsym_symbol_player_red_std_b : GPS_plyrsym_symbol_player_red_dead;
                    }
                    break;
                }
                case MsgType_Creature:
                {
                    spr_idx = get_creature_model_graphics(kfx_sim_state.messages[i].plyr_idx, CGI_HandSymbol);
                    x -= (7 * units_per_pixel / 16);
                    y -= (20 * units_per_pixel / 16);
                    break;
                }
                case MsgType_CreatureSpell:
                {
                    struct SpellConfig* spconf = get_spell_config(kfx_sim_state.messages[i].plyr_idx);
                    spr_idx = spconf->medsym_sprite_idx;
                    x -= (10 * units_per_pixel / 16);
                    y -= (10 * units_per_pixel / 16);
                    break;
                }
                case MsgType_Room:
                {
                    const struct RoomConfigStats* roomst = get_room_kind_stats(kfx_sim_state.messages[i].plyr_idx);
                    spr_idx = roomst->medsym_sprite_idx;
                    x -= (10 * units_per_pixel / 16);
                    y -= (10 * units_per_pixel / 16);
                    break;
                }
                case MsgType_KeeperSpell:
                {
                    struct PowerConfigStats* powerst = get_power_model_stats(kfx_sim_state.messages[i].plyr_idx);
                    spr_idx = powerst->medsym_sprite_idx;
                    x -= (10 * units_per_pixel / 16);
                    y -= (10 * units_per_pixel / 16);
                    break;
                }
                case MsgType_Query:
                {
                    spr_idx = (kfx_sim_state.messages[i].plyr_idx + GPS_plyrsym_symbol_room_yellow_std_a);
                    x -= (10 * units_per_pixel / 16);
                    y -= (10 * units_per_pixel / 16);
                    break;
                }
                case MsgType_Custom:
                {
                    spr_idx = kfx_sim_state.messages[i].plyr_idx;
                    break;
                }
                case MsgType_Blank:
                {
                    break;
                }
                case MsgType_CreatureInstance:
                {
                    struct InstanceInfo* inst_inf = creature_instance_info_get(kfx_sim_state.messages[i].plyr_idx);
                    spr_idx = inst_inf->symbol_spridx;
                    x -= (10 * units_per_pixel / 16);
                    y -= (10 * units_per_pixel / 16);
                    break;
                }
                default:
                {
                    ERRORLOG("Unrecognised message type: %u", kfx_sim_state.messages[i].type);
                    break;
                }
            }
            switch (kfx_sim_state.messages[i].type)
            {
                case MsgType_Player:
                {
                    draw_gui_panel_sprite_left_player(x, y, ps_units_per_px, spr_idx, plyr_idx);
                    break;
                }
                case MsgType_Creature:
                {
                    spr = get_panel_sprite(spr_idx);
                    LbSpriteDrawResized(x, y, ps_units_per_px, spr);
                    break;
                }
                case MsgType_CreatureSpell:
                case MsgType_Room:
                case MsgType_KeeperSpell:
                case MsgType_Query:
                case MsgType_CreatureInstance:
                case MsgType_Custom:
                {
                    spr = get_panel_sprite(spr_idx);                    
                    LbSpriteDrawResized(x, y, ps_units_per_px, spr);
                    break;
                }
            }
            y += (h*units_per_pixel/16) << (unsigned char)low_res;
            switch (kfx_sim_state.messages[i].type)
            {
                case MsgType_Player:
                {
                    break;
                }
                case MsgType_Creature:
                {
                    y += (20 * units_per_pixel / 16) << (unsigned char)low_res;
                    break;
                }
                case MsgType_CreatureSpell:
                case MsgType_Room:
                case MsgType_KeeperSpell:
                case MsgType_Query:
                case MsgType_CreatureInstance:
                {
                    y += (10 * units_per_pixel / 16) << (unsigned char)low_res;
                    break;
                }
            }
        }
    }
}

void message_update(void)
{
    SYNCDBG(6,"Starting");
    int i = kfx_sim_state.active_messages_count - 1;
    // Set end turn for all messages
    while (i >= 0)
    {
        struct GuiMessage* gmsg = &kfx_sim_state.messages[i];
        if (get_gameturn() > gmsg->expiration_turn)
        {
            kfx_sim_state.active_messages_count--;
            kfx_sim_state.messages[kfx_sim_state.active_messages_count].text[0] = 0;
        }
        i--;
    }
}

void zero_messages(void)
{
    kfx_sim_state.active_messages_count = 0;
    for (int i = 0; i < 3; i++)
    {
      memset(&kfx_sim_state.messages[i], 0, sizeof(struct GuiMessage));
    }
}

void clear_messages_from_player(char type, PlayerNumber plyr_idx)
{
    for (int i = 0; i < kfx_sim_state.active_messages_count; i++)
    {
        if (kfx_sim_state.messages[i].type == type)
        {
            if ( (kfx_sim_state.messages[i].plyr_idx == plyr_idx) || (plyr_idx == -1) )
            {
                delete_message(i);
            }
        }
    }
}

void delete_message(unsigned char msg_idx)
{
    memset(&kfx_sim_state.messages[msg_idx], 0, sizeof(struct GuiMessage));
    if (msg_idx < kfx_sim_state.active_messages_count - 1)
    {
        for (int i = msg_idx; i < kfx_sim_state.active_messages_count; i++)
        {
            kfx_sim_state.messages[i] = kfx_sim_state.messages[i+1];
        }
        memset(&kfx_sim_state.messages[kfx_sim_state.active_messages_count - 1], 0, sizeof(struct GuiMessage));
    }
    kfx_sim_state.active_messages_count--;
}

void message_add(char type, short idx, const char *text)
{
    SYNCDBG(2,"Player %d: %s",idx,text);
    for (int i = GUI_MESSAGES_COUNT - 1; i > 0; i--)
    {
        memcpy(&kfx_sim_state.messages[i], &kfx_sim_state.messages[i-1], sizeof(struct GuiMessage));
    }
    snprintf(kfx_sim_state.messages[0].text, sizeof(kfx_sim_state.messages[0].text), "%s", text);
    kfx_sim_state.messages[0].plyr_idx = idx;
    kfx_sim_state.messages[0].expiration_turn = get_gameturn() + GUI_MESSAGES_DELAY;
    kfx_sim_state.messages[0].target_idx = -1;
    kfx_sim_state.messages[0].type = type;
    kfx_sim_state.messages[0].icon_idx = 0;
    if (kfx_sim_state.active_messages_count < GUI_MESSAGES_COUNT) {
        kfx_sim_state.active_messages_count++;
    }
}

void message_add_fmt(char type, short idx, const char *fmt_str, ...)
{
    static char full_msg_text[2048];
    va_list val;
    va_start(val, fmt_str);
    vsnprintf(full_msg_text, sizeof(full_msg_text), fmt_str, val);
    message_add(type, idx, full_msg_text);
    va_end(val);
}

void targeted_message_add(char type, PlayerNumber plyr_idx, PlayerNumber target_idx, unsigned long timeout, const char *fmt_str, ...)
{
    va_list val;
    va_start(val, fmt_str);
    static char full_msg_text[2048];
    vsnprintf(full_msg_text, sizeof(full_msg_text), fmt_str, val);
    SYNCDBG(2,"Player %d: %s",(int)plyr_idx,full_msg_text);
    for (int i = GUI_MESSAGES_COUNT - 1; i > 0; i--)
    {
        memcpy(&kfx_sim_state.messages[i], &kfx_sim_state.messages[i-1], sizeof(struct GuiMessage));
    }
    snprintf(kfx_sim_state.messages[0].text, sizeof(kfx_sim_state.messages[0].text), "%s", full_msg_text);
    kfx_sim_state.messages[0].plyr_idx = plyr_idx;
    kfx_sim_state.messages[0].expiration_turn = get_gameturn() + timeout;
    kfx_sim_state.messages[0].target_idx = target_idx;
    kfx_sim_state.messages[0].type = type;
    kfx_sim_state.messages[0].icon_idx = 0;
    if (kfx_sim_state.active_messages_count < GUI_MESSAGES_COUNT) {
        kfx_sim_state.active_messages_count++;
    }
    va_end(val);
}

void show_game_time_taken(unsigned long fps, unsigned long turns)
{
    struct GameTime gt;
    get_game_time(&gt, turns, fps);
    struct PlayerInfo* player = get_my_player();
    targeted_message_add(MsgType_Player, player->id_number, player->id_number, GUI_MESSAGES_DELAY, "%s: %02ld:%02ld:%02ld", get_string(GUIStr_Time), gt.Hours, gt.Minutes, gt.Seconds);
}

void show_real_time_taken(void)
{
    update_time();
    struct PlayerInfo* player = get_my_player();
    targeted_message_add(MsgType_Player, player->id_number, player->id_number, GUI_MESSAGES_DELAY, "%s: %02ld:%02ld:%02ld:%03ld", get_string(GUIStr_Time), kfx_sim_state.Timer.Hours, kfx_sim_state.Timer.Minutes, kfx_sim_state.Timer.Seconds, kfx_sim_state.Timer.MSeconds);
}
/******************************************************************************/
