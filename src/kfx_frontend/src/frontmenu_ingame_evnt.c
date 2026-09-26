/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontmenu_ingame_evnt.c
 *     In-game events GUI, visible during gameplay at bottom.
 * @par Purpose:
 *     Functions to show and maintain message menu appearing ingame.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     05 Jan 2009 - 03 Jan 2011
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "frontmenu_ingame_evnt.h"
#include "config_keeperfx.h"
#include "globals.h"
#include "bflib_basics.h"
#include "bflib_datetm.h"
#include "bflib_guibtns.h"
#include "bflib_vidraw.h"
#include "bflib_sprfnt.h"
#include "bflib_enet.h"
#include "custom_sprites.h"
#include "player_data.h"
#include "config_players.h"
#include "player_utils.h"
#include "dungeon_data.h"
#include "creature_battle.h"
#include "creature_graphics.h"
#include "config_creature.h"
#include "magic_powers.h"
#include "gui_draw.h"
#include "kfx_config_state.h"
#include "kfx_net_state.h"
#include "gui_frontbtns.h"
#include "gui_frontmenu.h"
#include "packets.h"
#include "net_input_lag.h"
#include "frontend.h"
#include "front_input.h"
#include "game_legacy.h"
#include "map_events.h"
#include "local_camera.h"
#include "sprites.h"

#include "kfx_frontend_state.h"
#include "post_inc.h"

extern int64_t multiplayer_speed_adjustment_ns;

uint64_t TimerTurns = 0;
int64_t battle_creature_over;
// my_visible_event_idx moved to gui_frontmenu.c (stage 10,
// docs/refactor/stage-10-kfx-frontend.md).
unsigned char my_event_button_state[EVENTS_COUNT];
int64_t debug_display_network_stats = 0;

/******************************************************************************/
EventIndex get_my_event_button_index(uint64_t button_idx)
{
    if (button_idx > EVENT_BUTTONS_COUNT) {
        return 0;
    }
    EventIndex evidx = get_my_dungeon()->event_button_index[button_idx];
    if (my_event_button_state[evidx] & EvBtnS_Hidden) {
        return 0;
    }
    return evidx;
}

void gui_open_event(struct GuiButton *gbtn)
{
    SYNCDBG(5,"Starting");
    EventIndex evidx = get_my_event_button_index(gbtn->content.lval);
    if (evidx == my_visible_event_idx)
    {
        gui_close_objective(gbtn);
    } else
    if (evidx != 0)
    {
        activate_event_box(evidx);
    }
}

void gui_kill_event(struct GuiButton *gbtn)
{
    struct PlayerInfo* player = get_my_player();
    EventIndex evidx = get_my_event_button_index(gbtn->content.lval);
    turn_off_event_box_if_necessary(player->id_number, evidx);
    if (kfx_sim_state.event[evidx].kind != EvKind_Objective) {
        my_event_button_state[evidx] |= EvBtnS_Hidden;
        set_players_packet_action(player, PckA_EventBoxTurnOff, evidx, 0, 0, 0);
    }
}

void turn_on_event_info_panel_if_necessary(EventIndex evidx)
{
    struct Event* event = &kfx_sim_state.event[evidx];
    if ((event->kind == EvKind_FriendlyFight) || (event->kind == EvKind_EnemyFight))
    {
        if (!menu_is_active(GMnu_BATTLE))
          turn_on_menu(GMnu_BATTLE);
    } else
    {
        if (!menu_is_active(GMnu_TEXT_INFO))
          turn_on_menu(GMnu_TEXT_INFO);
    }
}

void gui_previous_battle(struct GuiButton *gbtn)
{
    struct Dungeon* dungeon = get_my_dungeon();
    BattleIndex battle_id = dungeon->visible_battles[0];
    if (battle_id != 0)
    {
        battle_id = find_previous_battle_of_mine_excluding_current_list(dungeon->owner, battle_id);
        if (battle_id > 0)
        {
            dungeon->visible_battles[0] = battle_id;
            battle_id = find_next_battle_of_mine_excluding_current_list(dungeon->owner, battle_id);
            dungeon->visible_battles[1] = battle_id;
            battle_id = find_next_battle_of_mine_excluding_current_list(dungeon->owner, battle_id);
            dungeon->visible_battles[2] = battle_id;
        }
    }
}

void gui_next_battle(struct GuiButton *gbtn)
{
    struct Dungeon* dungeon = get_my_dungeon();
    BattleIndex battle_id = dungeon->visible_battles[2];
    if (battle_id != 0)
    {
        battle_id = find_next_battle_of_mine_excluding_current_list(dungeon->owner, battle_id);
        if (battle_id > 0)
        {
            dungeon->visible_battles[2] = battle_id;
            battle_id = find_previous_battle_of_mine_excluding_current_list(dungeon->owner, battle_id);
            dungeon->visible_battles[1] = battle_id;
            battle_id = find_previous_battle_of_mine_excluding_current_list(dungeon->owner, battle_id);
            dungeon->visible_battles[0] = battle_id;
        }
    }
}

void gui_get_creature_in_battle(struct GuiButton *gbtn)
{
    if (battle_creature_over <= 0) {
        return;
    }
    PowerKind pwkind = get_local_user_state()->chosen_power_kind;
    struct Thing* thing = thing_get(battle_creature_over);
    if (!thing_exists(thing)) {
        WARNLOG("Nonexisting thing %" PRId64 " in battle",(int64_t)battle_creature_over);
        battle_creature_over = 0;
        return;
    }
    TRACE_THING(thing);
    // If a spell is selected, try to cast it
    if (pwkind > 0)
    {
        if (can_cast_spell(my_player_number, pwkind, thing->mappos.x.stl.num, thing->mappos.y.stl.num, thing, CastChk_Default)) {
            set_packet_power_on_thing(get_local_packet(), pwkind, battle_creature_over);
        }
    } else
    {
        if (can_cast_spell(my_player_number, PwrK_HAND, thing->mappos.x.stl.num, thing->mappos.y.stl.num, thing, CastChk_Default)) {
            struct Packet* pckt = get_local_packet();
            set_packet_action(pckt, PckA_UsePwrHandPick, battle_creature_over, 0, 0, 0);
        }
    }
    battle_creature_over = 0;
}

void gui_go_to_person_in_battle(struct GuiButton *gbtn)
{
    struct Thing* thing = thing_get(battle_creature_over);
    if (thing_exists(thing))
    {
        move_local_camera_to_position(thing->mappos.x.val, thing->mappos.y.val);
    }
}

void gui_setup_friend_over(struct GuiButton *gbtn)
{
    int64_t visbtl_id = gbtn->btype_value & LbBFeF_IntValueMask;
    if (battle_creature_over == 0)
    {
        struct Dungeon* dungeon = get_my_dungeon();
        struct Thing* thing = INVALID_THING;
        if (dungeon->visible_battles[visbtl_id] != 0)
        {
            int64_t battlr_id = (gbtn->scr_pos_x - lbDisplay.MMouseX * pixel_size) / (gbtn->width / 7) + 6;
            if (battlr_id < MESSAGE_BATTLERS_COUNT-1) {
                thing = thing_get(friendly_battler_list[MESSAGE_BATTLERS_COUNT * visbtl_id + battlr_id]);
            }
        }
        if (thing_exists(thing) && thing_revealed(thing, dungeon->owner))
        {
            battle_creature_over = thing->index;
        }
    }
}

void draw_battle_head(struct Thing *thing, int64_t scr_x, int64_t scr_y, int64_t units_per_px)
{
    if (thing_is_invalid(thing)) {
        return;
    }
    int64_t spr_idx = get_creature_model_graphics(thing->model, CGI_HandSymbol);
    const struct TbSprite* spr = get_panel_sprite(spr_idx);
    if (spr->SHeight == 0)
    {
        ERRORLOG("Trying to draw non existing icon in battle menu for %s", thing_model_name(thing));
        return;
    }
    int64_t ps_units_per_px = (50 * units_per_px + spr->SHeight / 2) / spr->SHeight;
    int64_t curscr_x = scr_x - (spr->SWidth * ps_units_per_px / 16) / 2;
    int64_t curscr_y = scr_y - (spr->SHeight * ps_units_per_px / 16) / 2;
    if ((thing->creature.health_bar_turns) && ((get_gameturn() % (2 * kfx_config_state.gui_blink_rate)) >= kfx_config_state.gui_blink_rate)) {
        LbSpriteDrawResizedOneColour(curscr_x, curscr_y, ps_units_per_px, spr, player_flash_colours[get_player_color_idx(thing->owner)]);
    } else {
        LbSpriteDrawResized(curscr_x, curscr_y, ps_units_per_px, spr);
    }
    curscr_x = scr_x - 8*units_per_px/16;
    curscr_y = scr_y - 8*units_per_px/16 + (spr->SHeight*ps_units_per_px/16)/2;
    LbDrawBox(curscr_x, curscr_y, 16*units_per_px/16, 6*units_per_px/16, resolve_indexed_pixel(kfx_sim_state.colours[0][0][0], RendererGetActivePalette()));
    // Show health
    struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
    HitPoints health = thing->health;
    if (health < 0)
        health = 0;
    HitPoints max_health = cctrl->max_health;
    if (max_health < 1)
        max_health = 1;
    LbDrawBox(curscr_x + 2*units_per_px/16, curscr_y + 2*units_per_px/16, ((12 * health)/max_health)*units_per_px/16, 2*units_per_px/16, player_room_colours[get_player_color_idx(thing->owner)]);
    // Draw experience level
    spr = get_button_sprite(GBS_creature_flower_level_01);
    int64_t bs_units_per_px = (17 * units_per_px + spr->SHeight / 2) / spr->SHeight;
    TbBool high_res = (MyScreenHeight >= 400);
    curscr_y = (scr_y - ((spr->SHeight*bs_units_per_px/16) >> (unsigned char)high_res));
    curscr_x = (scr_x - ((spr->SWidth*bs_units_per_px/16) >> (unsigned char)high_res));
    spr = get_button_sprite(GBS_creature_flower_level_01 + cctrl->exp_level);
    LbSpriteDrawResized(curscr_x, curscr_y, ps_units_per_px, spr);
}

void gui_area_friendly_battlers(struct GuiButton *gbtn)
{
    int64_t visbtl_id = gbtn->btype_value & LbBFeF_IntValueMask;
    struct Dungeon* dungeon = get_players_num_dungeon(my_player_number);
    BattleIndex battle_id = dungeon->visible_battles[visbtl_id];
    struct CreatureBattle* battle = creature_battle_get(battle_id);
    if (creature_battle_invalid(battle)) {
        return;
    }
    if (battle->fighters_num <= 0) {
        return;
    }
    int64_t units_per_px = (gbtn->width * 16 + 160 / 2) / 160;
    int64_t wdelta = gbtn->width / 7;
    int64_t scr_pos_x = gbtn->scr_pos_x - wdelta + gbtn->width;
    RendererAddDrawFlags(Lb_SPRITE_TRANSPAR4);
    LbDrawBox(gbtn->scr_pos_x, gbtn->scr_pos_y,
        gbtn->width, gbtn->height, resolve_indexed_pixel(kfx_sim_state.colours[0][0][0], RendererGetActivePalette()));
    RendererClearDrawFlags(Lb_SPRITE_TRANSPAR4);
    for (int64_t battlr_id = 0; battlr_id < MESSAGE_BATTLERS_COUNT-1; battlr_id++)
    {
        int64_t i = friendly_battler_list[MESSAGE_BATTLERS_COUNT * visbtl_id + battlr_id];
        struct Thing* thing = thing_get(i);
        if (thing_is_creature(thing))
        {
            draw_battle_head(thing, scr_pos_x + wdelta / 2, gbtn->scr_pos_y, units_per_px);
            if (thing->index == battle_creature_over)
            {
              if ((get_gameturn() % (4 * kfx_config_state.gui_blink_rate)) >= 2 * kfx_config_state.gui_blink_rate)
              {
                  TbPixel col = player_flash_colours[(get_gameturn() % (4 * kfx_config_state.neutral_flash_rate)) / kfx_config_state.neutral_flash_rate];
                  RendererAddDrawFlags((Lb_SPRITE_OUTLINE|0x0004));
                  LbDrawBox(scr_pos_x, gbtn->scr_pos_y,
                    wdelta, gbtn->height, col);
                  RendererClearDrawFlags((Lb_SPRITE_OUTLINE|0x0004));
              }
            }
            scr_pos_x -= wdelta;
        }
    }
}

void gui_setup_enemy_over(struct GuiButton *gbtn)
{
    int64_t visbtl_id = gbtn->btype_value & LbBFeF_IntValueMask;
    if (battle_creature_over == 0)
    {
        struct Dungeon* dungeon = get_my_dungeon();
        struct Thing* thing = INVALID_THING;
        if (dungeon->visible_battles[visbtl_id] != 0)
        {
            int64_t battlr_id = (lbDisplay.MMouseX * pixel_size - gbtn->scr_pos_x) / (gbtn->width / 7);
            if (battlr_id < MESSAGE_BATTLERS_COUNT-1) {
                thing = thing_get(enemy_battler_list[MESSAGE_BATTLERS_COUNT * visbtl_id + battlr_id]);
            }
        }
        if (thing_exists(thing) && thing_revealed(thing, dungeon->owner))
        {
            battle_creature_over = thing->index;
        }
    }
}

void gui_area_enemy_battlers(struct GuiButton *gbtn)
{
    int64_t visbtl_id = gbtn->btype_value & LbBFeF_IntValueMask;
    struct Dungeon* dungeon = get_players_num_dungeon(my_player_number);
    BattleIndex battle_id = dungeon->visible_battles[visbtl_id];
    struct CreatureBattle* battle = creature_battle_get(battle_id);
    if (creature_battle_invalid(battle)) {
        return;
    }
    if (battle->fighters_num <= 0) {
        return;
    }
    int64_t units_per_px = (gbtn->width * 16 + 160 / 2) / 160;
    int64_t wdelta = gbtn->width / 7;
    int64_t scr_pos_x = gbtn->scr_pos_x;
    RendererAddDrawFlags(Lb_SPRITE_TRANSPAR4);
    LbDrawBox(gbtn->scr_pos_x, gbtn->scr_pos_y,
        gbtn->width, gbtn->height, resolve_indexed_pixel(kfx_sim_state.colours[0][0][0], RendererGetActivePalette()));
    RendererClearDrawFlags(Lb_SPRITE_TRANSPAR4);
    for (int64_t battlr_id = 0; battlr_id < MESSAGE_BATTLERS_COUNT-1; battlr_id++)
    {
        int64_t i = enemy_battler_list[MESSAGE_BATTLERS_COUNT * visbtl_id + battlr_id];
        struct Thing* thing = thing_get(i);
        if (thing_is_creature(thing))
        {
            draw_battle_head(thing, scr_pos_x + wdelta / 2, gbtn->scr_pos_y, units_per_px);
            if (thing->index == battle_creature_over)
            {
              if ((get_gameturn() % (4 * kfx_config_state.gui_blink_rate)) >= 2 * kfx_config_state.gui_blink_rate)
              {
                  TbPixel col = player_flash_colours[(get_gameturn() % (4 * kfx_config_state.neutral_flash_rate)) / kfx_config_state.neutral_flash_rate];
                  RendererAddDrawFlags((Lb_SPRITE_OUTLINE|0x0004));
                  LbDrawBox(scr_pos_x, gbtn->scr_pos_y,
                    wdelta, gbtn->height, col);
                  RendererClearDrawFlags((Lb_SPRITE_OUTLINE|0x0004));
              }
            }
            scr_pos_x += wdelta;
        }
    }
}

int64_t zoom_to_fight(PlayerNumber plyr_idx)
{
    if (active_battle_exists(plyr_idx))
    {
        struct Dungeon* dungeon = get_players_num_dungeon(my_player_number);
        struct CreatureBattle* battle = creature_battle_get(dungeon->visible_battles[0]);
        struct Thing* thing = thing_get(battle->first_creatr);
        if (thing_exists(thing)) {
            move_local_camera_to_position(thing->mappos.x.val, thing->mappos.y.val);
        }
        step_battles_forward(plyr_idx);
        return true;
    }
    return false;
}

void draw_bonus_timer(void)
{
    int64_t nturns = kfx_game_state.bonus_time - get_gameturn();
    char text[32];
    if (kfx_game_state.timer_real)
    {
        uint64_t total_seconds = ((nturns) / kfx_sim_state.turns_per_second) + 1;
        unsigned char seconds = total_seconds % 60;
        uint64_t total_minutes = total_seconds / 60;
        unsigned char minutes = total_minutes % 60;
        unsigned char hours = total_minutes / 60;
        if (nturns >= 0) {
            snprintf(text, sizeof(text), "%02" PRId64 ":%02" PRId64 ":%02" PRId64, (int64_t)(hours), (int64_t)(minutes), (int64_t)(seconds));
        } else {
            snprintf(text, sizeof(text), "%s", "00:00:00");
        }
    }
    else
    {
        if (nturns < 0)
        {
            nturns = 0;
        }
        else if (nturns > 99999)
        {
            nturns = 99999;
        }
        snprintf(text, sizeof(text), "%05" PRId64, (int64_t)(nturns / 2));
    }
    LbTextSetFont(winfont);
    int64_t width = 10 * (LbTextCharWidth('0') * units_per_pixel / 16);
    int64_t height = LbTextLineHeight() * units_per_pixel / 16 + (LbTextLineHeight() * units_per_pixel / 16) / 2;
    if (MyScreenHeight < 400)
    {
        height *= 2;
        width *= 2;
        if ((dbc_initialized && dbc_enabled) && (kfx_game_state.timer_real))
        {
            width += (width / 8);
        }
    }
    RendererSetDrawFlags(Lb_TEXT_HALIGN_CENTER);
    int64_t scr_x = MyScreenWidth - width - 16 * units_per_pixel / 16;
    int64_t scr_y = 16 * units_per_pixel / 16;
    if (kfx_sim_state.armageddon_cast_turn != 0)
    {
        struct GuiMenu *gmnu = get_active_menu(menu_id_to_number(GMnu_MAIN));
        scr_x = (gmnu->width + (width >> 1) - 16 * units_per_pixel / 16);
    }
    LbTextSetWindow(scr_x, scr_y, width, height);
    draw_slab64k(scr_x, scr_y, units_per_pixel, width, height);
    int64_t tx_units_per_px;
    int64_t y;
    if ( (MyScreenHeight < 400) && (dbc_initialized && dbc_enabled) )
    {
        tx_units_per_px = scale_ui_value(32);
        y = 0;
    }
    else if ( (MyScreenWidth > 1280) && (dbc_initialized && dbc_enabled) )
    {
        tx_units_per_px = scale_ui_value(16 - (MyScreenWidth / 640));
        y = height / 4;
    }
    else
    {
        tx_units_per_px = (22 * units_per_pixel) / LbTextLineHeight();
        y = 0;
    }
    LbTextDrawResized(0, y, tx_units_per_px, text);
    LbTextSetWindow(0/pixel_size, 0/pixel_size, MyScreenWidth/pixel_size, MyScreenHeight/pixel_size);
}

/**
 * Returns if there is a bonus timer visible on the level.
 */
TbBool bonus_timer_enabled(void)
{
  return ((kfx_game_state.flags_gui & GGUI_CountdownTimer) != 0);
}

void draw_timer(void)
{
    char text[32];
    if (kfx_sim_state.TimerGame)
    {
        if (kfx_sim_state.TimerGameReal)
        {
            snprintf(text, sizeof(text), "%02" PRId64 ":%02" PRId64 ":%02" PRId64, (int64_t)(kfx_sim_state.GameT.Hours), (int64_t)(kfx_sim_state.GameT.Minutes), (int64_t)(kfx_sim_state.GameT.Seconds));
        }
        else
        {
            snprintf(text, sizeof(text), "%08" PRId64, (int64_t)(TimerTurns));
        }
    }
    else
    {
        snprintf(text, sizeof(text), "%02" PRId64 ":%02" PRId64 ":%02" PRId64, (int64_t)(kfx_sim_state.Timer.Hours), (int64_t)(kfx_sim_state.Timer.Minutes), (int64_t)(kfx_sim_state.Timer.Seconds));
    }
    LbTextSetFont(winfont);
    int64_t width = 10 * (LbTextCharWidth('0') * units_per_pixel >> 4);
    int64_t height = LbTextLineHeight() * units_per_pixel / 16 + (LbTextLineHeight() * units_per_pixel / 16) / 2;
    if (MyScreenHeight < 400)
    {
        height *= 2;
        width *= 2;
        if (dbc_initialized && dbc_enabled)
        {
            if (kfx_sim_state.TimerGame)
            {
                width += (width / 4);
            }
            else
            {
                width += (width / 8);
            }
        }
    }
    RendererSetDrawFlags(Lb_TEXT_HALIGN_CENTER);
    int64_t scr_x = MyScreenWidth - width - 16 * units_per_pixel / 16;
    int64_t scr_y = 16 * units_per_pixel / 16;
    if ( (bonus_timer_enabled()) || (script_timer_enabled()) || (display_variable_enabled()) || (kfx_sim_state.armageddon_cast_turn != 0) )
    {
        scr_y <<= 2;
    }
    LbTextSetWindow(scr_x, scr_y, width, height);
    draw_slab64k(scr_x, scr_y, units_per_pixel, width, height);
    int64_t tx_units_per_px;
    int64_t y;
    if ( (MyScreenHeight < 400) && (dbc_initialized && dbc_enabled) )
    {
        tx_units_per_px = scale_ui_value(32);
        y = 0;
    }
    else if ( (MyScreenWidth > 1280) && (dbc_initialized && dbc_enabled) )
    {
        tx_units_per_px = scale_ui_value(16 - (MyScreenWidth / 640));
        y = height / 4;
    }
    else
    {
        tx_units_per_px = (22 * units_per_pixel) / LbTextLineHeight();
        y = 0;
    }
    LbTextDrawResized(0, y, tx_units_per_px, text);
    LbTextSetWindow(0/pixel_size, 0/pixel_size, MyScreenWidth/pixel_size, MyScreenHeight/pixel_size);
}

static void draw_bottom_right_text(const char *text, int line)
{
    LbTextSetFont(winfont);
    int64_t textLength = strlen(text);
    int64_t textCharWidth = 0;
    for(int64_t i = 0; i < textLength; ++i)
    {
        textCharWidth += LbTextCharWidth(text[i]);
    };

    int64_t width = textCharWidth * units_per_pixel / 16;
    int64_t height = LbTextLineHeight() * units_per_pixel / 16 + (LbTextLineHeight() * units_per_pixel / 16) / 2;
    if (MyScreenHeight < 400)
    {
        height *= 2;
        width *= 2;
        if ((dbc_initialized && dbc_enabled) && (kfx_game_state.timer_real))
        {
            width += (width / 8);
        }
    }
    RendererSetDrawFlags(Lb_TEXT_HALIGN_CENTER);
    int64_t scr_x = MyScreenWidth - width - 16 * units_per_pixel / 16;
    int64_t scr_y = MyScreenHeight - (line + 1) * height - 16 * units_per_pixel / 16;

    LbTextSetWindow(scr_x, scr_y, width, height);
    //draw_slab64k(scr_x, scr_y, units_per_pixel, width, height);
    int64_t tx_units_per_px;
    int64_t y;
    if ( (MyScreenHeight < 400) && (dbc_initialized && dbc_enabled) )
    {
        tx_units_per_px = scale_ui_value(32);
        y = 0;
    }
    else if ( (MyScreenWidth > 1280) && (dbc_initialized && dbc_enabled) )
    {
        tx_units_per_px = scale_ui_value(16 - (MyScreenWidth / 640));
        y = height / 4;
    }
    else
    {
        tx_units_per_px = (22 * units_per_pixel) / LbTextLineHeight();
        y = 0;
    }
    LbTextDrawResized(0, y, tx_units_per_px, text);
    LbTextSetWindow(0/pixel_size, 0/pixel_size, MyScreenWidth/pixel_size, MyScreenHeight/pixel_size);
}

// name of user to display during replay
static const char *replay_get_displayed_user_name(void)
{
    if (!kfx_net_state.packet_load_enable || replay_camera_detached())
        return NULL;
    int users = 0;
    for (NetUserId user = 0; user < MAX_NET_USERS; user++) {
        if (kfx_net_state.packet_save_head.user_players[user] >= 0)
            users++;
    }
    const NetUserId user = get_local_user();
    if ((users < 2) || (user < 0) || (user >= MAX_NET_USERS))
        return NULL;
    return kfx_net_state.packet_save_head.user_names[user];
}

void draw_gameturn_timer(void)
{
    char text[32];
    snprintf(text, sizeof(text), "GameTurn %" PRIu64, (uint64_t)(get_gameturn()));
    draw_bottom_right_text(text, 0);
    const char *name = replay_get_displayed_user_name();
    if (name != NULL) {
        snprintf(text, sizeof(text), "%.*s", (int)sizeof(kfx_net_state.packet_save_head.user_names[0]), name);
        draw_bottom_right_text(text, 1);
    }
}

TbBool timer_enabled(void)
{
  return ((game_flags2 & GF2_Timer) != 0);
}

TbBool frametime_enabled(void)
{
  return (debug_display_frametime != 0);
}

TbBool consolelog_enabled(void)
{
    return (debug_display_consolelog != 0);
}

TbBool script_timer_enabled(void)
{
  return ((kfx_game_state.flags_gui & GGUI_ScriptTimer) != 0);
}

TbBool gameturn_timer_enabled(void)
{
    return flag_is_set(start_params.debug_flags, DFlg_ShowGameTurns);
}

void draw_script_timer(PlayerNumber plyr_idx, unsigned char timer_id, uint64_t limit, TbBool real)
{
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    int64_t nturns = (limit > 0) ? limit - (get_gameturn() - dungeon->turn_timers[timer_id].count) : get_gameturn() - dungeon->turn_timers[timer_id].count;
    if (nturns < 0)
    {
        kfx_game_state.flags_gui &= ~GGUI_ScriptTimer;
        return;
    }
    char text[32];
    if (real)
    {
        uint64_t total_seconds = ((nturns) / kfx_sim_state.turns_per_second) + 1;
        unsigned char seconds = total_seconds % 60;
        uint64_t total_minutes = total_seconds / 60;
        unsigned char minutes = total_minutes % 60;
        unsigned char hours = total_minutes / 60;
        if (nturns >= 0) {
            snprintf(text, sizeof(text), "%02" PRId64 ":%02" PRId64 ":%02" PRId64, (int64_t)(hours), (int64_t)(minutes), (int64_t)(seconds));
        } else {
            snprintf(text, sizeof(text), "%s", "00:00:00");
        }
    }
    else
    {
        snprintf(text, sizeof(text), "%08" PRId64, (int64_t)(nturns));
    }

    LbTextUseByteCoding(false);
    LbTextSetFont(winfont);
    int64_t width = 10 * (LbTextCharWidth('0') * units_per_pixel / 16);
    int64_t height = LbTextLineHeight() * units_per_pixel / 16 + (LbTextLineHeight() * units_per_pixel / 16) / 2;
    if (MyScreenHeight < 400)
    {
        height *= 2;
        width *= 2;
    }
    RendererSetDrawFlags(Lb_TEXT_HALIGN_CENTER);
    int64_t scr_x = MyScreenWidth - width - 16 * units_per_pixel / 16;
    int64_t scr_y = 16 * units_per_pixel / 16;
    if (kfx_sim_state.armageddon_cast_turn != 0)
    {
        struct GuiMenu *gmnu = get_active_menu(menu_id_to_number(GMnu_MAIN));
        scr_x = (gmnu->width + (width >> 1) - 16 * units_per_pixel / 16);
    }
    LbTextSetWindow(scr_x, scr_y, width, height);
    draw_slab64k(scr_x, scr_y, units_per_pixel, width, height);
    int64_t tx_units_per_px = (22 * units_per_pixel) / LbTextLineHeight();
    int64_t y = 0;


    LbTextDrawResized(0, y, tx_units_per_px, text);
    LbTextUseByteCoding(true);
    LbTextSetWindow(0/pixel_size, 0/pixel_size, MyScreenWidth/pixel_size, MyScreenHeight/pixel_size);
}

TbBool display_variable_enabled(void)
{
  return ((kfx_game_state.flags_gui & GGUI_Variable) != 0);
}

void draw_script_variable_list(void)
{
    LbTextSetFont(winfont);    
    int64_t valid_vars = 0;

    for (int64_t i = 0; i < kfx_game_state.active_script_var_count; i++)
    {
        if (kfx_game_state.script_variables[i].is_active)
            valid_vars++;
    }
    if(valid_vars > 0){
        int64_t h = LbTextLineHeight();
        int64_t row_height = h * units_per_pixel / 16;
        
        int64_t width = 10 * (LbTextCharWidth('0') * units_per_pixel / 16);
        int64_t height = row_height + (row_height) / 2;
        if (MyScreenHeight < 400)
        {
            height *= 2;
            width *= 2;
            if (dbc_initialized && dbc_enabled)
            {
                width += (width / 3);
            }
        }
        RendererSetDrawFlags(Lb_TEXT_HALIGN_CENTER);
        int64_t scr_x = MyScreenWidth - width - 16 * units_per_pixel / 16;
        int64_t scr_y = 16 * units_per_pixel / 16;
        if (kfx_sim_state.armageddon_cast_turn != 0)
        {
            struct GuiMenu *gmnu = get_active_menu(menu_id_to_number(GMnu_MAIN));
            scr_x = (gmnu->width + (width >> 1) - 16 * units_per_pixel / 16);
            if ( (bonus_timer_enabled()) || (script_timer_enabled()) )
            {
                scr_x += ((width + (width >> 1)) - 16 * units_per_pixel / 16);
            }
        }
        else if ( (bonus_timer_enabled()) || (script_timer_enabled()) )
        {
            scr_x -= ((width + (width >> 1)) - 16 * units_per_pixel / 16);
        }
        int64_t padding = 8 * units_per_pixel / 16;
        height += row_height*(valid_vars-1);
        draw_round_slab64k(scr_x, scr_y, units_per_pixel, width, height + padding, ROUNDSLAB64K_DARK);
    
        scr_y += padding;
        width -= 4 * units_per_pixel / 16;    
        LbTextSetWindow(scr_x, scr_y, width, height);  
        // draw_slab64k(scr_x, scr_y, units_per_pixel, width, height);
        int64_t y;
        int64_t tx_units_per_px;
                
        if ( (dbc_initialized && dbc_enabled) && (MyScreenWidth > 1280) )
        {
            tx_units_per_px = scale_ui_value(16 - (MyScreenWidth / 640));
            y = height / 4;
        }
        else
        {
            tx_units_per_px = ( (MyScreenHeight < 400) && (dbc_initialized && dbc_enabled) ) ? scale_ui_value(32) : (22 * units_per_pixel) / LbTextLineHeight();
            y = 0;
        }
        for (int64_t i = 0; i < kfx_game_state.active_script_var_count; i++)
        {
            struct ScriptVariable scval = kfx_game_state.script_variables[i];
            int64_t sprite_x = scr_x + 4 * units_per_pixel / 16;
            int64_t sprite_y = scr_y;

            struct ScriptVariableDetails details = get_condition_details(scval.variable_player, scval.value_type, scval.value_id);
        
            int64_t icon_idx = scval.icon_idx;
            if(scval.include_icon && icon_idx < 0)
                icon_idx = details.icon_idx;
            if (scval.variable_target != 0)
            {
                if ((scval.variable_target_type == 0) || (scval.variable_target_type == 2) )
                {
                    details.value = scval.variable_target - details.value;
                }
                else if (scval.variable_target_type == 1)
                {
                    details.value = ((~scval.variable_target)+1) + details.value;
                }
            }
            if (scval.variable_target_type != 2)
            {
                if (details.value < 0)
                {
                    details.value = 0;
                }
            }
            char value_text[32];
            snprintf(value_text, sizeof(value_text), "%" PRId64, (int64_t)(details.value));

            if ((icon_idx > -1 && scval.include_icon)) {
                RendererSetDrawFlags(Lb_TEXT_HALIGN_RIGHT);          
                LbTextDrawResized(4, y, tx_units_per_px, value_text);
            } else {                
                RendererSetDrawFlags(Lb_TEXT_HALIGN_CENTER);          
                LbTextDrawResized(0, y, tx_units_per_px, value_text);
            }
            if(icon_idx > -1 && scval.include_icon){
                const struct TbSprite* spr;
                int64_t ps_units_per_px = 0;
                if(scval.icon_idx == -1){
                    spr = get_panel_sprite(GPS_message_rpanel_msg_blank_std);                
                    ps_units_per_px = (22 * units_per_pixel) / spr->SHeight;
                    LbSpriteDrawResized(sprite_x, sprite_y + (2.5 * units_per_pixel / 16), ps_units_per_px, spr);
                }
                sprite_x += details.x_offset;                
                sprite_y += details.y_offset;
                spr = get_panel_sprite(icon_idx);
                ps_units_per_px = (22 * units_per_pixel) / spr->SHeight;
                LbSpriteDrawResized(sprite_x, sprite_y, ps_units_per_px, spr);
            }            
            y += row_height;
            scr_y += row_height;
        
        }
    }

    LbTextSetWindow(0/pixel_size, 0/pixel_size, MyScreenWidth/pixel_size, MyScreenHeight/pixel_size);
}

void draw_script_variable(PlayerNumber plyr_idx, unsigned char valtype, unsigned char validx, int64_t target, unsigned char targettype)
{
    int64_t value = get_condition_value(plyr_idx, valtype, validx);
    if (target != 0)
    {
        if ( (targettype == 0) || (targettype == 2) )
        {
            value = target - value;
        }
        else if (targettype == 1)
        {
            value = ((~target)+1) + value;
        }
    }
    if (targettype != 2)
    {
        if (value < 0)
        {
            value = 0;
        }
    }
    char text[16];
    snprintf(text, sizeof(text), "%" PRId64, (int64_t)(value));
    LbTextSetFont(winfont);
    int64_t width = 10 * (LbTextCharWidth('0') * units_per_pixel / 16);
    int64_t height = LbTextLineHeight() * units_per_pixel / 16 + (LbTextLineHeight() * units_per_pixel / 16) / 2;
    if (MyScreenHeight < 400)
    {
        height *= 2;
        width *= 2;
        if (dbc_initialized && dbc_enabled)
        {
            width += (width / 3);
        }
    }
    RendererSetDrawFlags(Lb_TEXT_HALIGN_CENTER);
    int64_t scr_x = MyScreenWidth - width - 16 * units_per_pixel / 16;
    int64_t scr_y = 16 * units_per_pixel / 16;
    if (kfx_sim_state.armageddon_cast_turn != 0)
    {
        struct GuiMenu *gmnu = get_active_menu(menu_id_to_number(GMnu_MAIN));
        scr_x = (gmnu->width + (width >> 1) - 16 * units_per_pixel / 16);
        if ( (bonus_timer_enabled()) || (script_timer_enabled()) )
        {
            scr_x += ((width + (width >> 1)) - 16 * units_per_pixel / 16);
        }
    }
    else if ( (bonus_timer_enabled()) || (script_timer_enabled()) )
    {
        scr_x -= ((width + (width >> 1)) - 16 * units_per_pixel / 16);
    }
    LbTextSetWindow(scr_x, scr_y, width, height);
    draw_slab64k(scr_x, scr_y, units_per_pixel, width, height);
    int64_t tx_units_per_px;
    int64_t y;
    if ( (dbc_initialized && dbc_enabled) && (MyScreenWidth > 1280) )
    {
        tx_units_per_px = scale_ui_value(16 - (MyScreenWidth / 640));
        y = height / 4;
    }
    else
    {
        tx_units_per_px = ( (MyScreenHeight < 400) && (dbc_initialized && dbc_enabled) ) ? scale_ui_value(32) : (22 * units_per_pixel) / LbTextLineHeight();
        y = 0;
    }
    LbTextDrawResized(0, y, tx_units_per_px, text);
    LbTextSetWindow(0/pixel_size, 0/pixel_size, MyScreenWidth/pixel_size, MyScreenHeight/pixel_size);
}

int64_t consolelog_font_size = 11;
int64_t consolelog_simultaneous_message_count = 21;
int64_t consolelog_max_line_width = 1250; // Maximum line width
void draw_consolelog()
{
    draw_round_slab64k(0, 0, units_per_pixel, lbDisplay.GraphicsScreenWidth, (lbDisplay.GraphicsScreenHeight/2), ROUNDSLAB64K_DARK);
    LbTextSetFont(winfont);
    RendererSetDrawFlags(Lb_TEXT_HALIGN_LEFT);

    int64_t text_height = (consolelog_font_size * units_per_pixel) / LbTextLineHeight();
    int64_t draw_ypos = text_height / 2; // Starting ypos

    int64_t totalLinesDrawn = 0;

    size_t startIdx = 0;
    if (consoleLogArraySize > consolelog_simultaneous_message_count) {
        startIdx = consoleLogArraySize - consolelog_simultaneous_message_count;
    }

    for (int64_t i = startIdx; i < consoleLogArraySize && totalLinesDrawn < consolelog_simultaneous_message_count; i++) {
        char* text = consoleLogArray[i];
        int64_t offset = 0; // Initialize offset for each text line
        while (text[offset] != '\0' && totalLinesDrawn < consolelog_simultaneous_message_count) {
            int64_t currentLineWidth = 0; // Reset line width for each new line
            int64_t sub_len = 1;

            // Iterate over the characters in the string to find the substring length
            while (text[offset + sub_len] != '\0') {
                int64_t charWidth = LbTextCharWidth(text[offset + sub_len - 1]);
                if (currentLineWidth + charWidth > consolelog_max_line_width) {
                    break; // Exit the loop if adding the next character would exceed the line width
                }

                currentLineWidth += charWidth; // Add the width of the current character
                sub_len++; // Move to the next character
            }

            //char line_buffer[sub_len + 1];
            char *line_buffer = (char*)malloc((sub_len + 1) * sizeof(char));
            if (!line_buffer) continue;
            strncpy(line_buffer, text + offset, sub_len);
            line_buffer[sub_len] = '\0';

            LbTextDrawResized(text_height, draw_ypos, text_height, line_buffer);
            free(line_buffer);
            draw_ypos += text_height; // Move to the next line position
            offset += sub_len;
            totalLinesDrawn++;
        }
    }
    RendererSetDrawFlags(Lb_TEXT_HALIGN_LEFT);
}

void draw_frametime()
{
    char text[64];
    LbTextSetFont(winfont);
    RendererSetDrawFlags(Lb_TEXT_HALIGN_RIGHT);
    int64_t tx_units_per_px = (11 * units_per_pixel) / LbTextLineHeight();
    if (tx_units_per_px < 16)
        tx_units_per_px = 16;

    int64_t iStartLine = (MyScreenHeight / tx_units_per_px) / 2 + 1;
    memset(text, 0, sizeof(text));
    if(debug_display_frametime == 1) {
        snprintf(text, sizeof(text), "%-13s", "Current");
    } else if(debug_display_frametime == 2) {
        snprintf(text, sizeof(text), "%-7s | %-7s | %-10s", "Current", "Min", "Max");
    }
    if (text[0] != 0)
        LbTextDrawResized(0, (iStartLine)*tx_units_per_px, tx_units_per_px, text);

    iStartLine += 1;
    // Frametimes
    for (int64_t i = 0; i < TOTAL_FRAMETIME_KINDS; i++) {
        memset(text, 0, sizeof(text));
        const char *frame_type = NULL;
        switch (i) {
            case Frametime_FullFrame:
                frame_type = "Frame";
                break;
            case Frametime_Logic:
                frame_type = "Logic";
                break;
            case Frametime_Draw:
                frame_type = "Draw";
                break;
            case Frametime_Sleep:
                frame_type = "Sleep";
                break;
        }
        if (frame_type != NULL) {
            if (debug_display_frametime == 1) {
                snprintf(text, sizeof(text), "%s: %010.6f ms", frame_type, frametime_measurements.frametime_display[i]);
            } else if (debug_display_frametime == 2) {
                snprintf(text, sizeof(text), "%s: %07.3f | %07.3f | %07.3f ms", frame_type, frametime_measurements.frametime_display[i], frametime_measurements.frametime_get_min[i], frametime_measurements.frametime_get_max[i]);
            }
        }
        if (text[0] != 0)
            LbTextDrawResized(0, (iStartLine+i)*tx_units_per_px, tx_units_per_px, text);
    }

    // Framerates
    iStartLine += TOTAL_FRAMETIME_KINDS;
    for (int64_t i = 0; i < TOTAL_FRAMERATE_KINDS; i++) {
        memset(text, 0, sizeof(text));
        const char *frame_type = NULL;
        switch (i) {
            case Framerate_FullFrame:
                frame_type = "Frame FPS";
                break;
            case Framerate_Logic:
                frame_type = "Logic FPS";
                break;
            case Framerate_Draw:
                frame_type = "Draw FPS";
                break;
        }
        if (frame_type != NULL) {
            if (debug_display_frametime == 1) {
                snprintf(text, sizeof(text), "%s: %03" PRId64, frame_type, (int64_t)(frametime_measurements.framerate_display[i]));
            } else if (debug_display_frametime == 2) {
                snprintf(text, sizeof(text), "%s: %03" PRId64 " | %03" PRId64 " | %03" PRId64, frame_type, (int64_t)(frametime_measurements.framerate_display[i]), (int64_t)(frametime_measurements.framerate_min[i]), (int64_t)(frametime_measurements.framerate_max[i]));
            }
        }
        if (text[0] > 0)
            LbTextDrawResized(0, (iStartLine+i)*tx_units_per_px, tx_units_per_px, text);
    }

    RendererSetDrawFlags(Lb_TEXT_HALIGN_LEFT);
}

void draw_network_stats()
{
    char text[128];
    LbTextSetFont(winfont);
    RendererSetDrawFlags(Lb_TEXT_HALIGN_RIGHT);
    int64_t tx_units_per_px = (11 * units_per_pixel) / LbTextLineHeight();
    if (tx_units_per_px < 16)
        tx_units_per_px = 16;

    uint64_t ping = GetPing(my_player_number, my_player_number);
    uint64_t half_ping = ping / 2;
    uint64_t packet_loss_percent = GetPacketLoss(my_player_number, my_player_number);
    uint64_t transit = GetClientDataInTransit();
    uint64_t lost_packet_count = GetClientPacketsLost();
    uint64_t outgoing_rate_kb10 = (GetUploadRateBytesPerSecond() * 10) / 1024;
    uint64_t incoming_rate_kb10 = (GetDownloadRateBytesPerSecond() * 10) / 1024;
    int64_t increase_wait_time;
    int64_t increase_turn_time;
    int64_t decrease_wait_time;
    int64_t decrease_sample_time;
    input_lag_get_stats(&increase_wait_time, &increase_turn_time, &decrease_wait_time, &decrease_sample_time);
    int64_t turn_length_ns = 0;
    if (kfx_sim_state.turns_per_second > 0) {
        turn_length_ns = 1000000000 / kfx_sim_state.turns_per_second;
        turn_length_ns += multiplayer_speed_adjustment_ns;
        if (turn_length_ns < 0) {
            turn_length_ns = 0;
        }
    }

    snprintf(text, sizeof(text), "Full ping: %" PRIu64 "ms", (uint64_t)(ping));
    LbTextDrawResized(0, 0, tx_units_per_px, text);
    snprintf(text, sizeof(text), "Half ping: %" PRIu64 "ms", (uint64_t)(half_ping));
    LbTextDrawResized(0, tx_units_per_px, tx_units_per_px, text);
    snprintf(text, sizeof(text), "Input lag: %" PRId64, (int64_t)(kfx_net_state.input_lag_turns));
    LbTextDrawResized(0, tx_units_per_px * 2, tx_units_per_px, text);
    snprintf(text, sizeof(text), "Packet wait increase: %" PRId64 "/%" PRId64 "ms in %" PRId64 "ms", (int64_t)(increase_wait_time),
        (int64_t)(increase_turn_time), (int64_t)(INPUT_LAG_INCREASE_SAMPLE_MS));
    LbTextDrawResized(0, tx_units_per_px * 3, tx_units_per_px, text);
    snprintf(text, sizeof(text), "Packet wait decrease: %" PRId64 "/%" PRId64 "ms", (int64_t)(decrease_wait_time), (int64_t)(decrease_sample_time));
    LbTextDrawResized(0, tx_units_per_px * 4, tx_units_per_px, text);
    snprintf(text, sizeof(text), "Download: %" PRIu64 ".%" PRIu64 " KB/s",
        (uint64_t)(incoming_rate_kb10 / 10), (uint64_t)(incoming_rate_kb10 % 10));
    LbTextDrawResized(0, tx_units_per_px * 5, tx_units_per_px, text);
    snprintf(text, sizeof(text), "Upload: %" PRIu64 ".%" PRIu64 " KB/s",
        (uint64_t)(outgoing_rate_kb10 / 10), (uint64_t)(outgoing_rate_kb10 % 10));
    LbTextDrawResized(0, tx_units_per_px * 6, tx_units_per_px, text);
    snprintf(text, sizeof(text), "Congestion: %" PRIu64 " bytes", (uint64_t)(transit));
    LbTextDrawResized(0, tx_units_per_px * 7, tx_units_per_px, text);
    snprintf(text, sizeof(text), "Loss rate: %" PRIu64 "%%", (uint64_t)(packet_loss_percent));
    LbTextDrawResized(0, tx_units_per_px * 8, tx_units_per_px, text);
    snprintf(text, sizeof(text), "Lost packets: %" PRIu64, (uint64_t)(lost_packet_count));
    LbTextDrawResized(0, tx_units_per_px * 9, tx_units_per_px, text);
    snprintf(text, sizeof(text), "Stutter: %" PRId64 "ms", (int64_t)(stutter_detection_current));
    LbTextDrawResized(0, tx_units_per_px * 10, tx_units_per_px, text);
    snprintf(text, sizeof(text), "Average stutter: %" PRId64 "ms", (int64_t)(stutter_detection_average));
    LbTextDrawResized(0, tx_units_per_px * 11, tx_units_per_px, text);
    snprintf(text, sizeof(text), "Max stutter: %" PRId64 "ms", (int64_t)(stutter_detection_max));
    LbTextDrawResized(0, tx_units_per_px * 12, tx_units_per_px, text);
    snprintf(text, sizeof(text), "Turn length: %" PRId64, (int64_t)(turn_length_ns));
    LbTextDrawResized(0, tx_units_per_px * 13, tx_units_per_px, text);
    snprintf(text, sizeof(text), "Gameturn: %" PRIu64, (uint64_t)(get_gameturn()));
    LbTextDrawResized(0, tx_units_per_px * 14, tx_units_per_px, text);
}
/******************************************************************************/
