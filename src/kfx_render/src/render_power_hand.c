/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file render_power_hand.c
 *     Drawing the local player's power hand and the things it holds.
 * @par Purpose:
 *     draw_power_hand() and draw_mini_things_in_hand(), moved from kfx_sim's
 *     power_hand.c (docs/refactor-pass2/stage-06-presentation-out-of-sim.md):
 *     their only caller is engine_redraw.c, and they only draw.
 * @par Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "render_power_hand.h"

#include <inttypes.h>
#include <stdlib.h>
#include "globals.h"
#include "bflib_basics.h"
#include "bflib_mouse.h"
#include "bflib_video.h"
#include "bflib_vidraw.h"
#include "config_creature.h"
#include "config_objects.h"
#include "config_powerhands.h"
#include "config_settings.h"
#include "config_terrain.h"
#include "creature_control.h"
#include "creature_graphics.h"
#include "custom_sprites.h"
#include "dungeon_data.h"
#include "engine_render.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "packet_data.h"
#include "player_availability.h"
#include "player_data.h"
#include "player_colours.h"
#include "player_instances.h"
#include "power_hand.h"
#include "room_data.h"
#include "sprites.h"
#include "thing_creature.h"
#include "thing_data.h"
#include "thing_objects.h"
#include "thing_stats.h"
#include "vidmode.h"
#include "ports/ui_port.h"
#include "local_state.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
void draw_power_hand(void)
{
    SYNCDBG(17,"Starting");
    struct PlayerInfo *player;
    struct PickedUpOffset *pickoffs;
    struct Thing *thing;
    struct Thing *picktng;
    struct Room *room;
    struct RoomConfigStats* roomst;
    player = get_my_player();
    struct UserState* ustate = get_user_state(get_local_user());
    if (local_state.display_needs_update)
        return;
    if (kfx_sim_state.small_map_state == 2)
        return;
    RendererSetDrawFlags(0x00);
    if (player->view_type != PVT_DungeonTop)
        return;
    // Color rendering array pointers used by draw_keepersprite()
    sync_render_globals();
    // Scale factor
    int64_t ps_units_per_px;
    {
        const struct TbSprite *spr = get_panel_sprite(GPS_trapdoor_bonus_box_std_s); // Use dungeon special box as reference
        ps_units_per_px = calculate_relative_upp(46, get_video_scale_values()->units_per_pixel_ui, spr->SHeight);
    }
    // Now draw
    if (((kfx_sim_state.operation_flags & GOF_ShowGui) != 0) && (kfx_sim_state.small_map_state != 2)
      && ui_mouse_is_over_panel_map(local_state.minimap_pos_x, local_state.minimap_pos_y))
    {
        MapSubtlCoord stl_x;
        MapSubtlCoord stl_y;
        stl_x = kfx_sim_state.hand_over_subtile_x;
        stl_y = kfx_sim_state.hand_over_subtile_y;
        SYNCDBG(7,"Drawing over pannel map");
        room = subtile_room_get(stl_x,stl_y);
        if ((!room_is_invalid(room)) && (subtile_revealed(stl_x, stl_y, player->id_number)))
        {
            roomst = get_room_kind_stats(room->kind);

            ui_draw_gui_panel_sprite_centered(GetMouseX()+scale_ui_value(24*kfx_runtime_settings.hand_scale), GetMouseY()+scale_ui_value(32*kfx_runtime_settings.hand_scale), ps_units_per_px, roomst->medsym_sprite_idx);
        }
        if ((!power_hand_is_empty(player)) && (kfx_sim_state.small_map_state == 1))
        {
            draw_mini_things_in_hand(GetMouseX()+scale_ui_value(10*kfx_runtime_settings.hand_scale), GetMouseY()+scale_ui_value(10*kfx_runtime_settings.hand_scale));
        }
        return;
    }
    if (ui_game_is_busy_doing_gui())
    {
        SYNCDBG(7,"Drawing while GUI busy");
        draw_mini_things_in_hand(GetMouseX()+scale_ui_value(10*kfx_runtime_settings.hand_scale), GetMouseY()+scale_ui_value(10*kfx_runtime_settings.hand_scale));
        return;
    }
    thing = thing_get(player->hand_thing_idx);
    if (!thing_exists(thing))
    {
        if ((local_state.local_thing_under_hand > 0) && (player->work_state == PSt_CtrlDungeon)) {
            process_keeper_sprite(GetMouseX()+scale_ui_value(60*kfx_runtime_settings.hand_scale), GetMouseY()+scale_ui_value(40*kfx_runtime_settings.hand_scale),
              kfx_config_state.conf.power_hand_conf.pwrhnd_cfg_stats[player->hand_idx].anim_idx[HndA_Hover], 0, 0, scale_ui_value(64*kfx_runtime_settings.hand_scale));
        }
        return;
    }
    if (player->hand_busy_until_turn > get_gameturn())
    {
        SYNCDBG(7,"Drawing hand %s index %" PRId64 ", busy state", thing_model_name(thing), (int64_t)thing->index);
        process_keeper_sprite(GetMouseX()+scale_ui_value(60*kfx_runtime_settings.hand_scale), GetMouseY()+scale_ui_value(40*kfx_runtime_settings.hand_scale),
          thing->anim_sprite, 0, thing->current_frame, scale_ui_value(64*kfx_runtime_settings.hand_scale));
        draw_mini_things_in_hand(GetMouseX()+scale_ui_value(60*kfx_runtime_settings.hand_scale), GetMouseY());
        return;
    }
    SYNCDBG(7,"Drawing hand %s index %" PRId64, thing_model_name(thing), (int64_t)thing->index);
    if ((ustate->additional_flags & UsrAF_ChosenSubTileIsHigh) != 0)
    {
        draw_mini_things_in_hand(GetMouseX()+scale_ui_value(18*kfx_runtime_settings.hand_scale), GetMouseY());
        return;
    }
    if (player->work_state != PSt_HoldInHand)
    {
      TbBool draw_hand = (local_state.local_thing_under_hand > 0);
      if ((player->work_state == PSt_CtrlDungeon) && !power_hand_is_empty(player)) {
        draw_hand = (ustate->primary_cursor_state != CSt_DoorKey) && (ustate->secondary_cursor_state != CSt_DoorKey);
      }
      if ((player->work_state != PSt_CtrlDungeon) || !draw_hand)
      {
        if ((player->instance_num != PI_Grab) && (player->instance_num != PI_Drop) && (player->instance_num != PI_Whip) && (player->instance_num != PI_WhipEnd))
        {
          if (player->work_state == PSt_Slap)
          {
            process_keeper_sprite(GetMouseX() + scale_ui_value(70*kfx_runtime_settings.hand_scale), GetMouseY() + scale_ui_value(46*kfx_runtime_settings.hand_scale),
                thing->anim_sprite, 0, thing->current_frame, scale_ui_value(64*kfx_runtime_settings.hand_scale));
          } else
          if (player->work_state == PSt_CtrlDungeon)
          {
            if ((ustate->secondary_cursor_state == CSt_DoorKey) || (ustate->primary_cursor_state == CSt_DoorKey))
            {
              draw_mini_things_in_hand(GetMouseX()+scale_ui_value(18*kfx_runtime_settings.hand_scale), GetMouseY());
            }
          }
          return;
        }
      }
    }
    int64_t inputpos_x;
    int64_t inputpos_y;
    picktng = get_first_thing_in_power_hand(player);
    if ((thing_exists(picktng)) && ((picktng->rendering_flags & TRF_Invisible) == 0))
    {
        SYNCDBG(7,"Holding %s",thing_model_name(picktng));
        switch (picktng->class_id)
        {
        case TCls_Creature:
            if (!creature_under_spell_effect(picktng, CSAfF_Chicken))
            {
                pickoffs = get_creature_picked_up_offset(picktng);
                inputpos_x = GetMouseX() + scale_ui_value(pickoffs->delta_x*kfx_runtime_settings.hand_scale);
                inputpos_y = GetMouseY() + scale_ui_value(pickoffs->delta_y*kfx_runtime_settings.hand_scale);
                struct CreatureModelConfig* crconf = creature_stats_get(picktng->model);
                if (crconf->transparency_flags == TRF_Transpar_8)
                {
                    RendererAddDrawFlags(Lb_SPRITE_TRANSPAR8);
                    RendererClearDrawFlags(Lb_SPRITE_REMAP);
                }
                else if (crconf->transparency_flags == TRF_Transpar_4)
                {
                    RendererAddDrawFlags(Lb_SPRITE_TRANSPAR4);
                    RendererClearDrawFlags(Lb_SPRITE_REMAP);
                }
                else if(crconf->transparency_flags == TRF_Transpar_Alpha)
                {
                    EngineSpriteDrawUsingAlpha = 1;
                }

                process_keeper_sprite(inputpos_x / pixel_size, inputpos_y / pixel_size,
                    picktng->anim_sprite, 0, picktng->current_frame, scale_ui_value(64*kfx_runtime_settings.hand_scale));
                RendererSetDrawFlags(0);
                EngineSpriteDrawUsingAlpha = 0;
            } else
            {
                inputpos_x = GetMouseX() + scale_ui_value(11*kfx_runtime_settings.hand_scale);
                inputpos_y = GetMouseY() + scale_ui_value(56*kfx_runtime_settings.hand_scale);
                process_keeper_sprite(inputpos_x / pixel_size, inputpos_y / pixel_size,
                    picktng->anim_sprite, 0, picktng->current_frame, scale_ui_value(64*kfx_runtime_settings.hand_scale));
            }
            break;
        case TCls_Object:
            if (object_is_mature_food(picktng))
            {
              inputpos_x = GetMouseX() + scale_ui_value(11*kfx_runtime_settings.hand_scale);
              inputpos_y = GetMouseY() + scale_ui_value(56*kfx_runtime_settings.hand_scale);
              process_keeper_sprite(inputpos_x / pixel_size, inputpos_y / pixel_size,
                  picktng->anim_sprite, 0, picktng->current_frame, scale_ui_value(64*kfx_runtime_settings.hand_scale));
              break;
            } else
            if ((picktng->class_id == TCls_Object) && object_is_gold_pile(picktng))
            {
                break;
            }
            else
            {
                pickoffs = get_object_picked_up_offset(picktng);
                inputpos_x = GetMouseX() + scale_ui_value(pickoffs->delta_x * kfx_runtime_settings.hand_scale);
                inputpos_y = GetMouseY() + scale_ui_value(pickoffs->delta_y * kfx_runtime_settings.hand_scale);
                process_keeper_sprite(inputpos_x / pixel_size, inputpos_y / pixel_size,
                    picktng->anim_sprite, 0, picktng->current_frame, scale_ui_value(64 * kfx_runtime_settings.hand_scale));
            }
            break;
        default:
            inputpos_x = GetMouseX();
            inputpos_y = GetMouseY();
            process_keeper_sprite(inputpos_x / pixel_size, inputpos_y / pixel_size,
                  picktng->anim_sprite, 0, picktng->current_frame, scale_ui_value(64*kfx_runtime_settings.hand_scale));
            break;
        }
    }
    if (player->hand_animationId == HndA_Hold)
    {
        inputpos_x = GetMouseX() + scale_ui_value(58*kfx_runtime_settings.hand_scale);
        inputpos_y = GetMouseY() +  scale_ui_value(6*kfx_runtime_settings.hand_scale);
        process_keeper_sprite(inputpos_x / pixel_size, inputpos_y / pixel_size,
            thing->anim_sprite, 0, thing->current_frame, scale_ui_value(64*kfx_runtime_settings.hand_scale));
        draw_mini_things_in_hand(GetMouseX()+scale_ui_value(60*kfx_runtime_settings.hand_scale), GetMouseY());
    } else
    {
        inputpos_x = GetMouseX() + scale_ui_value(60*kfx_runtime_settings.hand_scale);
        inputpos_y = GetMouseY() + scale_ui_value(40*kfx_runtime_settings.hand_scale);
        process_keeper_sprite(inputpos_x / pixel_size, inputpos_y / pixel_size,
            thing->anim_sprite, 0, thing->current_frame, scale_ui_value(64*kfx_runtime_settings.hand_scale));
        draw_mini_things_in_hand(GetMouseX()+scale_ui_value(60*kfx_runtime_settings.hand_scale), GetMouseY());
    }
}

void draw_mini_things_in_hand(int64_t x, int64_t y)
{
    SYNCDBG(7,"Starting");
    struct Dungeon *dungeon = get_my_dungeon();
    int64_t i;
    int64_t expshift_x;
    int64_t flash_color;
    // Scale factor
    int64_t ps_units_per_px;
    {
        const struct TbSprite *spr = get_panel_sprite(GPS_trapdoor_bonus_box_std_s); // Use dungeon special box as reference
        ps_units_per_px = calculate_relative_upp(46, get_video_scale_values()->units_per_pixel_ui, spr->SHeight);
    }
    uint64_t spr_idx = get_creature_model_graphics(get_players_special_digger_model(dungeon->owner), CGI_HandSymbol);
    if (spr_idx > 0) {
        i = get_panel_sprite(spr_idx)->SWidth - get_button_sprite(GBS_creature_flower_level_01)->SWidth;
    } else {
        i = 0;
    }
    int64_t scrbase_x = x;
    int64_t scrbase_y = y - scale_ui_value(58);
    expshift_x = scale_ui_value(llabs(i)) / 2;
    for (i = dungeon->num_things_in_hand-1; i >= 0; i--)
    {
        unsigned char ratio = (kfx_config_state.conf.rules[my_player_number].gameplay.max_things_in_hand / 2);
        if (kfx_config_state.conf.rules[my_player_number].gameplay.max_things_in_hand % 2)
        {
            ratio ++;
        }
        int64_t icol = i % ratio;
        int64_t irow = i / ratio;
        struct Thing *thing = thing_get(dungeon->things_in_hand[i]);
        if (!thing_exists(thing)) {
            continue;
        }
        flash_color = get_player_color_idx(thing->owner);
        int64_t scrpos_x;
        int64_t scrpos_y;
        int64_t shift_y;
        if (thing->class_id == TCls_Creature)
        {
            spr_idx = get_creature_model_graphics(thing->model, CGI_HandSymbol);
            if (spr_idx > 0)
            {
                struct CreatureControl *cctrl = creature_control_get_from_thing(thing);
                int64_t expspr_idx = GBS_creature_flower_level_01 + cctrl->exp_level;
                if (irow > 0)
                    shift_y = 40;
                else
                    shift_y = 6;
                scrpos_x = scrbase_x + scale_ui_value(16) * icol;
                scrpos_y = scrbase_y + scale_ui_value(18) * irow;
                // Draw creature symbol
                ui_draw_panel_sprite_left(scrpos_x, scrpos_y, ps_units_per_px, spr_idx);
                char ownshift_y;
                if (MyScreenHeight < 400)
                {
                    char expshift_y = (irow > 0) ? 32 : -6;
                    ui_draw_button_sprite_left(scrpos_x, scrpos_y + scale_ui_value(expshift_y), ps_units_per_px, expspr_idx);
                    if (thing->owner != my_player_number)
                    {
                        ownshift_y = (irow == 0) ? 1 : 56;
                        LbDrawCircle(scrpos_x + scale_ui_value(16), scrpos_y + scale_ui_value(ownshift_y), ps_units_per_px / 16, player_path_colours[flash_color]);
                    }
                }
                else
                {
                    ownshift_y = (irow > 0) ? 44 : 10;
                    if (thing->owner != my_player_number)
                    {
                        int64_t relative_window_a = lbDisplay.GraphicsScreenWidth;
                        int64_t relative_window_b = lbDisplay.GraphicsScreenHeight;
                        int64_t n = min(scale_ui_value(1),4);
                        ScreenCoord coord_y = scrpos_y + scale_ui_value(ownshift_y);
                        ScreenCoord draw_y;
                        ScreenCoord draw_x;
                        for (int64_t p = 0; p < (n*n); p++)
                        {
                            draw_y = coord_y + draw_square[p].delta_y;
                            if (draw_y >= 0)
                            {
                                draw_x = scrpos_x + ((expshift_x * 3)) + draw_square[p].delta_x;
                                // Draw the pixel if it's within the bounds of the window
                                if ((draw_x >= 0) && (draw_x < relative_window_a) && (draw_y < relative_window_b))
                                {
                                    LbDrawPixel(draw_x, draw_y, player_flash_colours[flash_color]);
                                }
                            }
                        }
                        for (int64_t p = (n * n); p < (n * n)+(4 * n + 4); p++)
                        {
                            draw_y = coord_y + draw_square[p].delta_y;
                            if (draw_y >= 0)
                            {
                                draw_x = scrpos_x + ((expshift_x * 3)) + draw_square[p].delta_x;
                                // Draw the pixel if it's within the bounds of the window
                                if ((draw_x >= 0) && (draw_x < relative_window_a) && (draw_y < relative_window_b))
                                {
                                    LbDrawPixel(draw_x, draw_y, player_path_colours[flash_color]);
                                }
                            }
                        }
                    }
                    // Draw exp level
                    ui_draw_button_sprite_left(scrpos_x + expshift_x, scrpos_y + scale_ui_value(shift_y), ps_units_per_px, expspr_idx);
                }
            }
        } else
        if (thing->class_id == TCls_DeadCreature)
        {
            spr_idx = GPS_room_graveyard_std_s;
            if (irow > 0)
                shift_y = 20;
            else
                shift_y = 0;
            scrpos_x = scrbase_x + scale_ui_value(16) * icol;
            scrpos_y = scrbase_y + scale_ui_value(14) * irow;
            ui_draw_panel_sprite_left(scrpos_x - 2, scrpos_y + scale_ui_value(shift_y), ps_units_per_px, spr_idx);
        } else
        if ((thing->class_id == TCls_Object))
        {
            spr_idx = get_object_model_stats(thing->model)->hand_icon;
            if (irow > 0)
                shift_y = 20;
            else
                shift_y = 0;
            scrpos_x = scrbase_x + scale_ui_value(16) * icol;
            scrpos_y = scrbase_y + scale_ui_value(14) * irow;
            ui_draw_panel_sprite_left(scrpos_x - 2, scrpos_y + scale_ui_value(shift_y), ps_units_per_px, spr_idx);
        } else
        {
            spr_idx = GPS_room_hatchery_std_s;
            if (irow > 0)
                shift_y = 20;
            else
                shift_y = 0;
            scrpos_x = scrbase_x + scale_ui_value(16) * icol;
            scrpos_y = scrbase_y + scale_ui_value(14) * irow;
            ui_draw_panel_sprite_left(scrpos_x - 2, scrpos_y + scale_ui_value(shift_y), ps_units_per_px, spr_idx);
        }
    }
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
