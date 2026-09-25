/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file gui_frontbtns.c
 *     gui_frontbtns support functions.
 * @par Purpose:
 *     Functions to gui_frontbtns.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     11 Mar 2010 - 12 May 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "gui_frontbtns.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_guibtns.h"
#include "bflib_vidraw.h"
#include "bflib_sprite.h"
#include "bflib_sprfnt.h"
#include "bflib_datetm.h"
#include "bflib_inputctrl.h"
#include "kjm_input.h"
#include "gui_draw.h"
#include "kfx_config_state.h"
#include "gui_frontmenu.h"
#include "frontend.h"
#include "front_input.h"
#include "sprites.h"
#include "game_legacy.h"
#include "custom_sprites.h"
#include "kfx_frontend_state.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
void gui_clear_buttons_not_over_mouse(int64_t gmbtn_mouseover_idx)
{
    struct GuiButton *gbtn;
    int64_t gidx;
    for (gidx=0;gidx<ACTIVE_BUTTONS_COUNT;gidx++)
    {
      gbtn = &active_buttons[gidx];
      if (gbtn->flags & LbBtnF_Active)
        if ( ((gmbtn_mouseover_idx == -1) || (gmbtn_mouseover_idx != gidx)) &&
             (gbtn->gbtype != LbBtnT_RadioBtn) && (gbtn != input_button) )
        {
          gbtn->flags &= ~LbBtnF_MouseOver;
          gbtn->button_state_left_pressed = 0;
          gbtn->button_state_right_pressed = 0;
        }
    }
}

void fake_button_click(int64_t gmbtn_idx)
{
    int64_t i;
    for (i=0; i < ACTIVE_BUTTONS_COUNT; i++)
    {
        struct GuiButton *gbtn;
        gbtn = &active_buttons[i];
        struct GuiMenu *gmnu;
        gmnu = &active_menus[(uint64_t)gbtn->gmenu_idx];
        if (((gbtn->flags & LbBtnF_Active) != 0) && (gmnu->is_turned_on != 0) && (gbtn->id_num == gmbtn_idx))
        {
            if ((gbtn->click_event != NULL) || ((gbtn->flags & LbBtnF_Clickable) != 0) || (gbtn->parent_menu != NULL) || (gbtn->gbtype == LbBtnT_RadioBtn)) {
                do_button_press_actions(gbtn, &gbtn->button_state_left_pressed, gbtn->click_event);
            }
            if ((gbtn->click_event != NULL) || ((gbtn->flags & LbBtnF_Clickable) != 0) || (gbtn->parent_menu != NULL) || (gbtn->gbtype == LbBtnT_RadioBtn)) {
                do_button_click_actions(gbtn, &gbtn->button_state_left_pressed, gbtn->click_event);
            }
        }
    }
}

TbBool gui_button_release_inputs(int64_t gmbtn_idx)
{
    struct GuiButton *gbtn;
    SYNCDBG(17,"Starting");
    if (gmbtn_idx < 0)
      return false;
    Gf_Btn_Callback callback;
    gbtn = &active_buttons[gmbtn_idx%ACTIVE_BUTTONS_COUNT];
    if ((gbtn->button_state_left_pressed) && (left_button_released))
    {
        callback = gbtn->click_event;
        if ((callback != NULL) || ((gbtn->flags & LbBtnF_Clickable) != 0) ||
            (gbtn->parent_menu != NULL) || (gbtn->gbtype == LbBtnT_RadioBtn))
        {
            left_button_released = 0;
            do_button_release_actions(gbtn, &gbtn->button_state_left_pressed, callback);
        }
        return true;
    }
    if ((gbtn->button_state_right_pressed) && (right_button_released))
    {
        callback = gbtn->rclick_event;
        if (callback != NULL)
        {
          right_button_released = 0;
          do_button_release_actions(gbtn, &gbtn->button_state_right_pressed, callback);
        }
        return true;
    }
    return false;
}

TbBool gui_slider_button_inputs(int64_t gbtn_idx)
{
    Gf_Btn_Callback callback;
    int64_t mouse_x;
    int64_t slide_start;
    int64_t slide_end;
    struct GuiButton *gbtn;
    if (gbtn_idx < 0)
      return false;
    gbtn = &active_buttons[gbtn_idx];
    mouse_x = GetMouseX();
    gbtn->button_state_left_pressed = 1;
    int64_t bs_units_per_px;
    bs_units_per_px = simple_button_sprite_height_units_per_px(gbtn, GBS_frontend_button_std_c, 44);
    slide_start = gbtn->pos_x + 32*bs_units_per_px/16;
    slide_end = gbtn->pos_x + gbtn->width - 32*bs_units_per_px/16;
    if (mouse_x < slide_start)
    {
        gbtn->slide_val = 0;
    } else
    if (mouse_x >= slide_end)
    {
        gbtn->slide_val = 255;
    } else
    if (gbtn->width > 64*bs_units_per_px/16)
    {
        gbtn->slide_val = ((mouse_x-slide_start) << 8) / (gbtn->width-64*bs_units_per_px/16);
    } else
    {
        gbtn->slide_val = ((mouse_x-gbtn->pos_x) << 8) / (gbtn->width+1);
    }
    gbtn->content.lval = (gbtn->slide_val) * (((int64_t)gbtn->maxval)+1) >> 8;
    callback = gbtn->click_event;
    if (callback != NULL)
      callback(gbtn);
    return true;
}

TbBool gui_slider_button_mouse_over_slider_tracker(int64_t gbtn_idx)
{
    struct GuiButton *gbtn;
    if (gbtn_idx < 0)
      return false;
    gbtn = &active_buttons[gbtn_idx];
    int64_t bs_units_per_px;
    bs_units_per_px = gbtn->height * 16 / 22;
    int64_t slider_pos_x;
    slider_pos_x = gbtn->scr_pos_x + 32*bs_units_per_px/16 + ((gbtn->slide_val)*(gbtn->width-64*bs_units_per_px/16) >> 8);

    int64_t mouse_x;
    int64_t mouse_y;
    mouse_x = GetMouseX();
    if ((mouse_x >= (slider_pos_x-11*bs_units_per_px/16)) && (mouse_x <= (slider_pos_x+11*bs_units_per_px/16)))
    {
        mouse_y = GetMouseY();
        if ((mouse_y >= gbtn->pos_y) && (mouse_y <= (gbtn->pos_y+gbtn->height))) {
            return true;
        }
    }
    return false;
}

void clear_radio_buttons(struct GuiMenu *gmnu)
{
    struct GuiButton *gbtn;
    int64_t i;
    for (i=0; i<ACTIVE_BUTTONS_COUNT; i++)
    {
        gbtn = &active_buttons[i];
        if (gbtn->gbtype == LbBtnT_RadioBtn)
        {
            if (gmnu->number == gbtn->gmenu_idx)
                gbtn->button_state_left_pressed = 0;
        }
    }
}

void update_radio_button_data(struct GuiMenu *gmnu)
{
    struct GuiButton *gbtn;
    unsigned char *rbstate;
    int64_t i;
    for (i=0; i<ACTIVE_BUTTONS_COUNT; i++)
    {
        gbtn = &active_buttons[i];
        rbstate = gbtn->content.ptr;
        if ((rbstate != NULL) && (gbtn->gmenu_idx == gmnu->number))
        {
          if (gbtn->gbtype == LbBtnT_RadioBtn)
          {
              if (gbtn->button_state_left_pressed)
                *rbstate = 1;
              else
                *rbstate = 0;
          }
        }
    }
}

TbBool gui_button_click_inputs(int64_t gmbtn_idx)
{
    TbBool result;
    struct GuiButton *gbtn;
    if (gmbtn_idx < 0)
      return false;
    result = false;
    gbtn = &active_buttons[gmbtn_idx];
    Gf_Btn_Callback callback;
    if (lbDisplay.MLeftButton)
    {
        SYNCDBG(8,"Left down for button %" PRId64,(int64_t)gmbtn_idx);
        result = true;
        callback = gbtn->click_event;
        if ((callback != NULL) || ((gbtn->flags & LbBtnF_Clickable) != 0) ||
           (gbtn->parent_menu != NULL) || (gbtn->gbtype == LbBtnT_RadioBtn))
        {
            if ((gbtn->flags & LbBtnF_Enabled) != 0)
            {
                SYNCDBG(18,"Left down action for type %" PRId64,(int64_t)gbtn->gbtype);
                switch (gbtn->gbtype)
                {
                case LbBtnT_HoldableBtn:
                  if ((gbtn->button_state_left_pressed > 5) && (callback != NULL)) {
                      callback(gbtn);
                  } else {
                      gbtn->button_state_left_pressed++;
                  }
                  break;
                case LbBtnT_Hotspot:
                  if (callback != NULL) {
                      callback(gbtn);
                  }
                  break;
                }
            }
        }
    } else
    if (lbDisplay.MRightButton)
    {
        SYNCDBG(8,"Right down for button %" PRId64,(int64_t)gmbtn_idx);
        result = true;
        callback = gbtn->rclick_event;
        if ((callback != NULL) && ((gbtn->flags & LbBtnF_Enabled) != 0))
        {
            SYNCDBG(18,"Right down action for type %" PRId64,(int64_t)gbtn->gbtype);
            switch (gbtn->gbtype)
            {
            case LbBtnT_HoldableBtn:
              if ((gbtn->button_state_right_pressed > 5) && (callback != NULL)) {
                  callback(gbtn);
              } else {
                  gbtn->button_state_right_pressed++;
              }
              break;
            case LbBtnT_Hotspot:
              if (callback != NULL) {
                  callback(gbtn);
              }
              break;
            }
        }
    }
    if ( left_button_clicked )
    {
        SYNCDBG(8,"Left click for button %" PRId64,(int64_t)gmbtn_idx);
        result = true;
        if (kfx_frontend_state.flash_button_index != 0)
        {
          if (gbtn->id_num == kfx_frontend_state.flash_button_index)
            kfx_frontend_state.flash_button_index = 0;
        }
        callback = gbtn->click_event;
        if ((callback != NULL) || ((gbtn->flags & LbBtnF_Clickable) != 0) ||
           (gbtn->parent_menu != NULL) || (gbtn->gbtype == LbBtnT_RadioBtn))
        {
          left_button_clicked = 0;
          gui_last_left_button_pressed_id = gbtn->id_num;
          do_button_click_actions(gbtn, &gbtn->button_state_left_pressed, callback);
        }
    } else
    if ( right_button_clicked )
    {
        SYNCDBG(8,"Right click for button %" PRId64,(int64_t)gmbtn_idx);
        result = true;
        if (kfx_frontend_state.flash_button_index != 0)
        {
          if (gbtn->id_num == kfx_frontend_state.flash_button_index)
            kfx_frontend_state.flash_button_index = 0;
        }
        callback = gbtn->rclick_event;
        if ((callback != NULL))
        {
          right_button_clicked = 0;
          gui_last_right_button_pressed_id = gbtn->id_num;
          do_button_click_actions(gbtn, &gbtn->button_state_right_pressed, callback);
        }
    }
    return result;
}

/**
 * Returns index of an unused button slot.
 * @return
 */
int64_t guibutton_get_unused_slot(void)
{
    struct GuiButton *gbtn;
    int64_t i;
    for (i=0; i<ACTIVE_BUTTONS_COUNT; i++)
    {
        gbtn = &active_buttons[i];
        if ((gbtn->flags & LbBtnF_Active) == 0) {
            return i;
        }
    }
    return -1;
}

void init_slider_bars(struct GuiMenu *gmnu)
{
    for (int64_t i = 0; i < ACTIVE_BUTTONS_COUNT; i++)
    {
        struct GuiButton *gbtn = &active_buttons[i];
        if (gbtn->gmenu_idx == gmnu->number)
        {
            if (gbtn->gbtype == LbBtnT_HorizSlider)
            {
                int64_t sldpos = clamp(gbtn->content.lval, 0, gbtn->maxval);
                gbtn->slide_val = (sldpos << 8) / (gbtn->maxval + 1);
            }
        }
    }
}

void init_menu_buttons(struct GuiMenu *gmnu)
{
    struct GuiButton *gbtn;
    Gf_Btn_Callback callback;
    int64_t i;
    for (i=0; i<ACTIVE_BUTTONS_COUNT; i++)
    {
      gbtn = &active_buttons[i];
      callback = gbtn->maintain_call;
      if ((callback != NULL) && (gbtn->gmenu_idx == gmnu->number))
        callback(gbtn);
    }
}

void kill_button(struct GuiButton *gbtn)
{
    if (gbtn != NULL) {
        gbtn->flags &= ~LbBtnF_Active;
    }
}

void kill_button_area_input(void)
{
  if (input_button != NULL)
    strcpy(input_button->content.str, backup_input_field);
  input_button = NULL;
  if (LbIsTextInputActive())
    LbStopTextInput();
}

void setup_radio_buttons(struct GuiMenu *gmnu)
{
    struct GuiButton *gbtn;
    int64_t i;
    for (i=0; i<ACTIVE_BUTTONS_COUNT; i++)
    {
        gbtn = &active_buttons[i];
        if (gbtn->content.ptr && (gmnu->number == gbtn->gmenu_idx))
        {
            if (gbtn->gbtype == LbBtnT_RadioBtn)
            {
                if ( *(unsigned char *)gbtn->content.ptr )
                  gbtn->button_state_left_pressed = 1;
                else
                  gbtn->button_state_left_pressed = 0;
            }
        }
    }
}

void frontend_copy_background(void)
{
    draw_frontmenu_background(0,0,POS_AUTO,POS_AUTO);
}

void gui_round_glass_background(struct GuiMenu *gmnu)
{
    SYNCDBG(19,"Starting");
    int64_t fade_h;
    int64_t i;
    fade_h = 0;
    if (kfx_frontend_state.time_delta < 12)
    {
      if (gmnu->visual_state == 1) {
          gmnu->fade_time = 1;
          gmnu->visual_state = 2;
      }
    } else
    {
        i = gmnu->menu_init->fade_time;
        if (i <= 0)
        {
            gmnu->visual_state = 2;
        } else {
            fade_h = ((int64_t)MyScreenHeight - (int64_t)gmnu->pos_y) / i;
            if (fade_h < 0)
                fade_h = 0;
        }
    }
    int64_t px;
    int64_t py;
    switch (gmnu->visual_state)
    {
    case 3:
        px = gmnu->pos_x;
        py = fade_h * (gmnu->menu_init->fade_time - gmnu->fade_time) + gmnu->pos_y;
        draw_round_slab64k(px, py, units_per_pixel, gmnu->width, gmnu->height, ROUNDSLAB64K_LIGHT);
        break;
    case 1:
        px = gmnu->pos_x;
        py = MyScreenHeight - fade_h * (gmnu->menu_init->fade_time - gmnu->fade_time);
        draw_round_slab64k(px, py, units_per_pixel, gmnu->width, gmnu->height, ROUNDSLAB64K_LIGHT);
        break;
    default:
        px = gmnu->pos_x;
        py = gmnu->pos_y;
        draw_round_slab64k(px, py, units_per_pixel, gmnu->width, gmnu->height, ROUNDSLAB64K_LIGHT);
        break;
    }
}

void gui_pretty_background(struct GuiMenu *gmnu)
{
    SYNCDBG(9,"Starting");
    int64_t fade_w;
    int64_t fade_h;
    int64_t i;
    fade_w = 0;
    fade_h = 0;
    if (kfx_frontend_state.time_delta < 12)
    {
      if (gmnu->visual_state == 1) {
          gmnu->fade_time = 1;
          gmnu->visual_state = 2;
      }
    } else
    {
        i = gmnu->menu_init->fade_time;
        if (i <= 0)
        {
            gmnu->visual_state = 2;
        } else {
            fade_w = (gmnu->width - 86*units_per_pixel/16) / i;
            if (fade_w < 0)
                fade_w = 0;
            fade_h = (gmnu->height - 64*units_per_pixel/16) / i;
            if (fade_h < 0)
                fade_h = 0;
        }
    }
    int64_t px;
    int64_t py;
    int64_t width;
    int64_t height;
    switch (gmnu->visual_state)
    {
    case 1:
        width = fade_w * (gmnu->menu_init->fade_time - gmnu->fade_time) + scale_ui_value_lofi(86);
        height = fade_h * (gmnu->menu_init->fade_time - gmnu->fade_time) + scale_ui_value_lofi(64);
        px = gmnu->pos_x + gmnu->width/2 - width/2;
        py = gmnu->pos_y + gmnu->height/2 - height/2;
        draw_ornate_slab_outline64k(px, py, units_per_pixel, width, height);
        break;
    case 3:
        width = gmnu->width;
        height = gmnu->height;
        px = gmnu->pos_x + gmnu->width/2 - width/2;
        py = gmnu->pos_y + gmnu->height/2 - (gmnu->height - fade_h)/2;
        draw_ornate_slab_outline64k(px, py, units_per_pixel, width, height);
        break;
    default:
        draw_ornate_slab64k(gmnu->pos_x, gmnu->pos_y, units_per_pixel, gmnu->width, gmnu->height);
        break;
    }
}

void gui_area_new_normal_button(struct GuiButton *gbtn)
{
    SYNCDBG(10,"Starting");
    int64_t i;
    int64_t ps_units_per_px;
    ps_units_per_px = simple_gui_panel_sprite_width_units_per_px(gbtn, gbtn->sprite_idx+1, 100);
    if ((gbtn->flags & LbBtnF_Enabled) != 0)
    {
        i = 0;
        if ((!gbtn->button_state_left_pressed) && (!gbtn->button_state_right_pressed))
            i = 1;
        draw_gui_panel_sprite_left(gbtn->scr_pos_x, gbtn->scr_pos_y, ps_units_per_px, gbtn->sprite_idx+i);
    } else
    {
        draw_gui_panel_sprite_rmleft(gbtn->scr_pos_x, gbtn->scr_pos_y, ps_units_per_px, gbtn->sprite_idx+1, 12);
    }
    SYNCDBG(12,"Finished");
}

void gui_area_new_vertical_button(struct GuiButton *gbtn)
{
    SYNCDBG(10,"Starting");
    int64_t i;
    int64_t ps_units_per_px;
    ps_units_per_px = simple_gui_panel_sprite_height_units_per_px(gbtn, gbtn->sprite_idx+1, 100);
    if ((gbtn->flags & LbBtnF_Enabled) != 0)
    {
        i = 0;
        if ((!gbtn->button_state_left_pressed) && (!gbtn->button_state_right_pressed))
            i = 1;
        draw_gui_panel_sprite_left(gbtn->scr_pos_x, gbtn->scr_pos_y, ps_units_per_px, gbtn->sprite_idx+i);
    } else
    {
        draw_gui_panel_sprite_rmleft(gbtn->scr_pos_x, gbtn->scr_pos_y, ps_units_per_px, gbtn->sprite_idx+1, 12);
    }
    SYNCDBG(12,"Finished");
}

void gui_draw_tab(struct GuiButton *gbtn)
{
    int64_t i;
    i = gbtn->sprite_idx;
    if (gbtn->gbtype == LbBtnT_ToggleBtn) {
        ERRORLOG("Cycle button cannot use this draw function!");
    }
    int64_t ps_units_per_px;
    ps_units_per_px = simple_gui_panel_sprite_width_units_per_px(gbtn, i, 100);
    if ((!gbtn->button_state_left_pressed) && (!gbtn->button_state_right_pressed))
        i++;
    draw_gui_panel_sprite_left(gbtn->scr_pos_x, gbtn->scr_pos_y, ps_units_per_px, i);
}

void gui_area_new_null_button(struct GuiButton *gbtn)
{
    int64_t ps_units_per_px;
    ps_units_per_px = simple_gui_panel_sprite_height_units_per_px(gbtn, gbtn->sprite_idx, 128);
    draw_gui_panel_sprite_left(gbtn->scr_pos_x, gbtn->scr_pos_y, ps_units_per_px, gbtn->sprite_idx);
}

void gui_area_compsetting_button(struct GuiButton *gbtn)
{
    SYNCDBG(10,"Starting");
    int64_t spr_idx;
    spr_idx = gbtn->sprite_idx;
    if (gbtn->gbtype == LbBtnT_ToggleBtn)
    {
        if (gbtn->content.ptr != NULL) {
            spr_idx += *(unsigned char *)gbtn->content.ptr;
        } else {
            ERRORLOG("Cycle button must have a non-null UBYTE Data pointer!");
        }
        if (gbtn->maxval == 0) {
            ERRORLOG("Cycle button must have a non-zero MaxVal!");
        }
    }

    int64_t ps_units_per_px;
    ps_units_per_px = simple_gui_panel_sprite_height_units_per_px(gbtn, spr_idx, 100);
    if ((gbtn->flags & LbBtnF_Enabled) == 0)
    {
        draw_gui_panel_sprite_rmleft(gbtn->scr_pos_x, gbtn->scr_pos_y, ps_units_per_px, spr_idx, 12);
    } else
    if ((gbtn->button_state_left_pressed) || (gbtn->button_state_right_pressed))
    {
        draw_gui_panel_sprite_rmleft(gbtn->scr_pos_x, gbtn->scr_pos_y, ps_units_per_px, spr_idx, 44);
    } else
    {
        draw_gui_panel_sprite_left(gbtn->scr_pos_x, gbtn->scr_pos_y, ps_units_per_px, spr_idx);
    }
    SYNCDBG(12,"Finished");
}

void gui_area_creatrmodel_button(struct GuiButton *gbtn)
{
    SYNCDBG(10,"Starting");
    int64_t spr_idx;
    spr_idx = gbtn->sprite_idx;
    if (gbtn->gbtype == LbBtnT_ToggleBtn)
    {
        if (gbtn->content.ptr != NULL) {
            spr_idx += *(unsigned char *)gbtn->content.ptr;
        } else {
            ERRORLOG("Cycle button must have a non-null UBYTE Data pointer!");
        }
        if (gbtn->maxval == 0) {
            ERRORLOG("Cycle button must have a non-zero MaxVal!");
        }
    }
    int64_t ps_units_per_px;
    ps_units_per_px = simple_gui_panel_sprite_width_units_per_px(gbtn, spr_idx, 138);
    if ((gbtn->flags & LbBtnF_Enabled) == 0)
    {
        draw_gui_panel_sprite_rmleft(gbtn->scr_pos_x, gbtn->scr_pos_y, ps_units_per_px, spr_idx, 12);
    } else
    if ((gbtn->button_state_left_pressed) || (gbtn->button_state_right_pressed))
    {
        draw_gui_panel_sprite_rmleft(gbtn->scr_pos_x, gbtn->scr_pos_y, ps_units_per_px, spr_idx, 44);
    } else
    {
        draw_gui_panel_sprite_left(gbtn->scr_pos_x, gbtn->scr_pos_y, ps_units_per_px, spr_idx);
    }
    SYNCDBG(12,"Finished");
}

void gui_area_new_no_anim_button(struct GuiButton *gbtn)
{
    SYNCDBG(10,"Starting");
    int64_t spr_idx;
    spr_idx = gbtn->sprite_idx;
    if (gbtn->gbtype == LbBtnT_ToggleBtn)
    {
        if (gbtn->content.ptr != NULL) {
            spr_idx += *(unsigned char *)gbtn->content.ptr;
        } else {
            ERRORLOG("Cycle button must have a non-null UBYTE Data pointer!");
        }
        if (gbtn->maxval == 0) {
            ERRORLOG("Cycle button must have a non-zero MaxVal!");
        }
    }
    int64_t ps_units_per_px;
    ps_units_per_px = simple_gui_panel_sprite_height_units_per_px(gbtn, spr_idx, 128);
    if ((gbtn->flags & LbBtnF_Enabled) == 0)
    {
        draw_gui_panel_sprite_rmleft(gbtn->scr_pos_x, gbtn->scr_pos_y, ps_units_per_px, spr_idx, 12);
    } else
    if ((gbtn->button_state_left_pressed) || (gbtn->button_state_right_pressed))
    {
        draw_gui_panel_sprite_rmleft(gbtn->scr_pos_x, gbtn->scr_pos_y, ps_units_per_px, spr_idx, 44);
    } else
    {
        draw_gui_panel_sprite_left(gbtn->scr_pos_x, gbtn->scr_pos_y, ps_units_per_px, spr_idx);
    }
    SYNCDBG(12,"Finished");
}

void gui_area_no_anim_button(struct GuiButton *gbtn)
{
    int64_t spr_idx;
    spr_idx = gbtn->sprite_idx;
    if (gbtn->gbtype == LbBtnT_ToggleBtn)
    {
        unsigned char *ctptr;
        ctptr = (unsigned char *)gbtn->content.ptr;
        if (ctptr != NULL) {
            spr_idx += *ctptr;
        } else {
            ERRORLOG("Cycle button must have a non-null UBYTE Data pointer!");
        }
        if (gbtn->maxval == 0) {
            ERRORLOG("Cycle button must have a non-zero MaxVal!");
        }
    }
    int64_t bs_units_per_px;
    bs_units_per_px = simple_button_sprite_height_units_per_px(gbtn, spr_idx, 100);
    if ((gbtn->flags & LbBtnF_Enabled) == 0)
    {
        draw_button_sprite_rmleft(gbtn->scr_pos_x, gbtn->scr_pos_y, bs_units_per_px, spr_idx, 12);
    } else
    if ((gbtn->button_state_left_pressed) || (gbtn->button_state_right_pressed))
    {
        draw_button_sprite_rmleft(gbtn->scr_pos_x, gbtn->scr_pos_y, bs_units_per_px, spr_idx, 44);
    } else
    {
        draw_button_sprite_left(gbtn->scr_pos_x, gbtn->scr_pos_y, bs_units_per_px, spr_idx);
    }
}

void gui_area_normal_button(struct GuiButton *gbtn)
{
    int64_t spr_idx;
    spr_idx = gbtn->sprite_idx;
    if (gbtn->gbtype == LbBtnT_ToggleBtn)
    {
        ERRORLOG("Cycle button cannot have a normal button draw function!");
    }
    int64_t bs_units_per_px;
    bs_units_per_px = simple_button_sprite_width_units_per_px(gbtn, spr_idx, 114);
    if ((gbtn->flags & LbBtnF_Enabled) != 0)
    {
        if ( (gbtn->button_state_left_pressed != 0) || (gbtn->button_state_right_pressed != 0) )
            spr_idx++;
        draw_button_sprite_left(gbtn->scr_pos_x, gbtn->scr_pos_y, bs_units_per_px, spr_idx);
    } else
    {
        draw_button_sprite_rmleft(gbtn->scr_pos_x, gbtn->scr_pos_y, bs_units_per_px, spr_idx, 12);
    }
}

void frontend_over_button(struct GuiButton *gbtn)
{
    int64_t i;

    if (gbtn->gbtype == LbBtnT_EditBox)
      i = gbtn->btype_value & LbBFeF_IntValueMask;
    else
      i = gbtn->content.lval;
    if (old_mouse_over_button != i)
      frontend_mouse_over_button_start_time = LbTimerClock();
    frontend_mouse_over_button = i;
}

void frontend_draw_button(struct GuiButton *gbtn, int64_t btntype, const char *text, uint64_t drw_flags)
{
    static const int64_t large_button_sprite_anims[] = {
        GFS_hugebutton_a01l,
        GFS_hugebutton_a02l,
        GFS_hugebutton_a03l,
        GFS_hugebutton_a04l,
        GFS_hugebutton_a05l,
        GFS_hugebutton_a04l,
        GFS_hugebutton_a03l,
        GFS_hugebutton_a02l,
    };
    uint64_t febtn_idx;
    uint64_t spridx;
    int64_t fntidx;
    int64_t x;
    int64_t y;
    int64_t h;
    SYNCDBG(9,"Drawing type %" PRId64 ", text \"%s\"",(int64_t)btntype,text);
    febtn_idx = gbtn->content.lval;
    if ((gbtn->flags & LbBtnF_Enabled) == 0)
    {
        fntidx = 3;
        spridx = GFS_hugebutton_a05l;
    } else
    {
        fntidx = frontend_button_caption_font(gbtn, frontend_mouse_over_button);
        if ((febtn_idx > 0) && (frontend_mouse_over_button == febtn_idx)) {
            spridx = large_button_sprite_anims[((LbTimerClock()-frontend_mouse_over_button_start_time)/100) & 7];
        } else {
            spridx = GFS_hugebutton_a05l;
        }
    }
    const struct TbSprite *spr;
    // Detect scaling factor
    int64_t units_per_px;
    units_per_px = simple_frontend_sprite_height_units_per_px(gbtn, GFS_hugebutton_a05l, 100);
    x = gbtn->scr_pos_x;
    y = gbtn->scr_pos_y;
    switch (btntype)
    {
     case 1:
         spr = get_frontend_sprite(spridx);
         LbSpriteDrawResized(x, y, units_per_px, spr);
         x += spr->SWidth * units_per_px / 16;
         spr = get_frontend_sprite(spridx+1);
         LbSpriteDrawResized(x, y, units_per_px, spr);
         x += spr->SWidth * units_per_px / 16;
         break;
    case 2:
        spr = get_frontend_sprite(spridx);
        LbSpriteDrawResized(x, y, units_per_px, spr);
        x += spr->SWidth * units_per_px / 16;
        spr = get_frontend_sprite(spridx+1);
        LbSpriteDrawResized(x, y, units_per_px, spr);
        x += spr->SWidth * units_per_px / 16;
        LbSpriteDrawResized(x, y, units_per_px, spr);
        x += spr->SWidth * units_per_px / 16;
        break;
    default:
        spr = get_frontend_sprite(spridx);
        LbSpriteDrawResized(x, y, units_per_px, spr);
        x += spr->SWidth * units_per_px / 16;
        break;
    }
    spr = get_frontend_sprite(spridx+2);
    LbSpriteDrawResized(x, y, units_per_px, spr);
    if (text != NULL)
    {
        RendererSetDrawFlags(drw_flags);
        LbTextSetFont(frontend_font[fntidx]);
        spr = get_frontend_sprite(spridx);
        h = LbTextHeight(text) * units_per_px / 16;
        x = gbtn->scr_pos_x + ((40*units_per_px/16) >> 1);
        y = gbtn->scr_pos_y + ((spr->SHeight*units_per_px/16 - h) >> 1);
        LbTextSetWindow(x, y, gbtn->width-40*units_per_px/16, h);
        LbTextDrawResized(0, 0, units_per_px, text);
    }
}

/**
 * How many times to repeat a menu button's middle chrome piece so the
 * drawn width matches `width`, generalizing frontend_draw_button's
 * hardcoded 0/1/2-repeat presets (small/large/vlarge) to an arbitrary
 * target width. Pure arithmetic, no rendering -- unit-tested directly.
 */
int64_t frontend_button_chrome_repeat_count(int64_t width, int64_t left_w, int64_t right_w, int64_t mid_w)
{
    if (mid_w <= 0)
        return 0;
    int64_t avail = width - left_w - right_w;
    int64_t repeats = (avail + mid_w / 2) / mid_w;
    if (repeats < 0)
        repeats = 0;
    return repeats;
}

/**
 * The width frontend_draw_button_chrome_flexible will *actually* render
 * for a requested min_width, after frontend_button_chrome_repeat_count's
 * rounding to a whole middle-tile count. Callers that need a button's
 * width to track its caption (auto-sizing menu buttons) must size to
 * this, not to their own raw text-width-plus-padding estimate: the
 * middle tile can be wide enough that several different requested widths
 * all round to the same repeat count, so an un-quantized width silently
 * doesn't move the rendered edge, and desyncs gbtn->width (used for both
 * the hitbox and, by callers positioning a sibling button, layout) from
 * what's actually drawn.
 */
int64_t frontend_button_chrome_fit_width(uint64_t spridx, int64_t units_per_px, int64_t min_width)
{
    int64_t left_w = get_frontend_sprite(spridx)->SWidth * units_per_px / 16;
    int64_t right_w = get_frontend_sprite(spridx+2)->SWidth * units_per_px / 16;
    int64_t mid_w = get_frontend_sprite(spridx+1)->SWidth * units_per_px / 16;
    int64_t repeats = frontend_button_chrome_repeat_count((int64_t)min_width, left_w, right_w, mid_w);
    return (int64_t)left_w + (int64_t)repeats * (int64_t)mid_w + (int64_t)right_w;
}

/**
 * Draws the ornate "menu button" chrome (the same GFS_hugebutton_a0*l
 * pieces frontend_draw_button uses) at gbtn->width, tiling the middle
 * piece a width-derived number of times instead of a hardcoded preset.
 * Returns the x position immediately after the drawn chrome, so a caller
 * can lay out an icon/caption relative to it.
 */
int64_t frontend_draw_button_chrome_flexible(struct GuiButton *gbtn, uint64_t spridx, int64_t units_per_px)
{
    const struct TbSprite *spr;
    int64_t x = gbtn->scr_pos_x;
    int64_t y = gbtn->scr_pos_y;

    spr = get_frontend_sprite(spridx);
    int64_t left_w = spr->SWidth * units_per_px / 16;
    LbSpriteDrawResized(x, y, units_per_px, spr);
    x += left_w;

    int64_t right_w = get_frontend_sprite(spridx+2)->SWidth * units_per_px / 16;
    spr = get_frontend_sprite(spridx+1);
    int64_t mid_w = spr->SWidth * units_per_px / 16;
    int64_t repeats = frontend_button_chrome_repeat_count(gbtn->width, left_w, right_w, mid_w);
    for (int64_t i = 0; i < repeats; i++)
    {
        LbSpriteDrawResized(x, y, units_per_px, spr);
        x += mid_w;
    }

    spr = get_frontend_sprite(spridx+2);
    LbSpriteDrawResized(x, y, units_per_px, spr);
    x += right_w;
    return x;
}

/**
 * A menu button that draws an icon (gbtn->sprite_idx) beside its caption
 * text using the flexible-width chrome above, instead of
 * frontend_draw_button's centered-text-only layout. New draw_call for
 * buttons that want both an icon and a persistent label; existing
 * frontend_draw_large_menu_button/_vlarge_menu_button/_small_menu_button
 * callers are untouched and keep using frontend_draw_button as before.
 */
void frontend_draw_button_icon(struct GuiButton *gbtn)
{
    static const int64_t large_button_sprite_anims[] = {
        GFS_hugebutton_a01l, GFS_hugebutton_a02l, GFS_hugebutton_a03l, GFS_hugebutton_a04l,
        GFS_hugebutton_a05l, GFS_hugebutton_a04l, GFS_hugebutton_a03l, GFS_hugebutton_a02l,
    };
    uint64_t febtn_idx = gbtn->content.lval;
    uint64_t spridx;
    int64_t fntidx;
    if ((gbtn->flags & LbBtnF_Enabled) == 0)
    {
        fntidx = 3;
        spridx = GFS_hugebutton_a05l;
    } else
    {
        fntidx = frontend_button_caption_font(gbtn, frontend_mouse_over_button);
        if ((febtn_idx > 0) && (frontend_mouse_over_button == febtn_idx)) {
            spridx = large_button_sprite_anims[((LbTimerClock()-frontend_mouse_over_button_start_time)/100) & 7];
        } else {
            spridx = GFS_hugebutton_a05l;
        }
    }

    int64_t units_per_px = simple_frontend_sprite_height_units_per_px(gbtn, GFS_hugebutton_a05l, 100);
    frontend_draw_button_chrome_flexible(gbtn, spridx, units_per_px);

    int64_t inset = 20 * units_per_px / 16;
    int64_t content_x = gbtn->scr_pos_x + inset;
    if (gbtn->sprite_idx > 0)
    {
        int64_t icon_units_per_px = simple_frontend_sprite_height_units_per_px(gbtn, gbtn->sprite_idx, 100);
        const struct TbSprite *icon_spr = get_frontend_sprite(gbtn->sprite_idx);
        int64_t icon_y = gbtn->scr_pos_y + ((gbtn->height - icon_spr->SHeight * icon_units_per_px / 16) >> 1);
        LbSpriteDrawResized(content_x, icon_y, icon_units_per_px, icon_spr);
        content_x += icon_spr->SWidth * icon_units_per_px / 16 + inset;
    }

    const char *text = frontend_button_caption_text(gbtn);
    if (text != NULL)
    {
        RendererSetDrawFlags(Lb_TEXT_HALIGN_LEFT);
        LbTextSetFont(frontend_font[fntidx]);
        int64_t h = LbTextHeight(text) * units_per_px / 16;
        int64_t text_y = gbtn->scr_pos_y + ((gbtn->height - h) >> 1);
        LbTextSetWindow(content_x, text_y, gbtn->width - (content_x - gbtn->scr_pos_x), h);
        LbTextDrawResized(0, 0, units_per_px, text);
    }
}

void frontend_draw_large_menu_button(struct GuiButton *gbtn)
{
    const char *text;
    text = frontend_button_caption_text(gbtn);
    frontend_draw_button(gbtn, 1, text, Lb_TEXT_HALIGN_CENTER);
}

void frontend_draw_vlarge_menu_button(struct GuiButton *gbtn)
{
    const char *text;
    text = frontend_button_caption_text(gbtn);
    frontend_draw_button(gbtn, 2, text, Lb_TEXT_HALIGN_CENTER);
}

void frontend_draw_scroll_box_tab(struct GuiButton *gbtn)
{
    const struct TbSprite *spr;
    int64_t pos_x;
    int64_t pos_y;
    int64_t fs_units_per_px;
    fs_units_per_px = simple_frontend_sprite_height_units_per_px(gbtn, GFS_hugearea_thc_tx1_tc, 100);
    spr = get_frontend_sprite(GFS_hugearea_thc_tx1_tc);
    pos_x = gbtn->scr_pos_x;
    // Since this tab is attachable from top, it is important to keep bottom position without variation
    pos_y = gbtn->scr_pos_y + gbtn->height - spr->SHeight * fs_units_per_px / 16;
    spr = get_frontend_sprite(GFS_hugearea_thc_cor_tl);
    LbSpriteDrawResized(pos_x, pos_y, fs_units_per_px, spr);
    pos_x += spr->SWidth * fs_units_per_px / 16;
    spr = get_frontend_sprite(GFS_hugearea_thc_tx1_tc);
    LbSpriteDrawResized(pos_x, pos_y, fs_units_per_px, spr);
    pos_x += spr->SWidth * fs_units_per_px / 16;
    spr = get_frontend_sprite(GFS_hugearea_thc_tx1_tc);
    LbSpriteDrawResized(pos_x, pos_y, fs_units_per_px, spr);
    pos_x += spr->SWidth * fs_units_per_px / 16;
    spr = get_frontend_sprite(GFS_hugearea_thc_cor_tr);
    LbSpriteDrawResized(pos_x, pos_y, fs_units_per_px, spr);
}

void frontend_draw_scroll_box(struct GuiButton *gbtn)
{
    int64_t height_lines;
    TbBool draw_scrollbar;
    switch (gbtn->content.lval)
    {
      case 24:
        height_lines = 2;
        draw_scrollbar = true;
        break;
      case 25:
        height_lines = 3;
        draw_scrollbar = true;
        break;
      case 26:
        height_lines = 7;
        draw_scrollbar = true;
        break;
      case 89:
        height_lines = 3;
        draw_scrollbar = false;
        break;
      case 90:
        height_lines = 4;
        draw_scrollbar = false;
        break;
      case 91:
        height_lines = 4;
        draw_scrollbar = true;
        break;
      case 94:
        height_lines = 10;
        draw_scrollbar = true;
        break;
      default:
        height_lines = 0;
        draw_scrollbar = false;
        break;
    }
    gui_draw_scroll_box(gbtn, height_lines, draw_scrollbar);
}

void frontend_draw_slider_button(struct GuiButton *gbtn)
{
    int64_t spr_idx;
    int64_t btn_id;
    if ((gbtn->flags & LbBtnF_Enabled) != 0)
    {
        btn_id = gbtn->content.lval;
        if ( (btn_id != 0) && (frontend_mouse_over_button == btn_id) )
        {
            if ( (btn_id == 17) || (btn_id == 36) || (btn_id == 38) ) {
                spr_idx = GFS_scrollbar_toparrow_act;
            } else {
                spr_idx = GFS_scrollbar_btmarrow_act;
            }
        } else
        {
            if ( (btn_id == 17) || (btn_id == 36) || (btn_id == 38) ) {
                spr_idx = GFS_scrollbar_toparrow_std;
            } else {
                spr_idx = GFS_scrollbar_btmarrow_std;
            }
        }
    } else
    {
      spr_idx = 0;
    }
    if (spr_idx > 0)
    {
        // Detect scaling factor
        int64_t units_per_px;
        units_per_px = simple_frontend_sprite_height_units_per_px(gbtn, spr_idx, 100);
        draw_frontend_sprite_left(gbtn->scr_pos_x, gbtn->scr_pos_y, units_per_px, spr_idx);
    }
}

void gui_area_null(struct GuiButton *gbtn)
{
    int64_t bs_units_per_px;
    bs_units_per_px = simple_button_sprite_height_units_per_px(gbtn, gbtn->sprite_idx, 100);
    if ((gbtn->flags & LbBtnF_Enabled) != 0)
    {
        draw_button_sprite_left(gbtn->scr_pos_x, gbtn->scr_pos_y, bs_units_per_px, gbtn->sprite_idx);
    } else
    {
        draw_button_sprite_left(gbtn->scr_pos_x, gbtn->scr_pos_y, bs_units_per_px, gbtn->sprite_idx);
    }
}

void reset_scroll_window(struct GuiMenu *gmnu)
{
    kfx_sim_state.evntbox_scroll_window.start_y = 0;
    kfx_sim_state.evntbox_scroll_window.action = 0;
    kfx_sim_state.evntbox_scroll_window.text_height = 0;
    kfx_sim_state.evntbox_scroll_window.window_height = 0;
}

void gui_set_menu_mode(struct GuiButton *gbtn)
{
    int64_t mnu_idx = gbtn->btype_value & LbBFeF_IntValueMask;
    if (mnu_idx == GMnu_SPELL)
    {
        if (menu_is_active(GMnu_SPELL2))
        {
            mnu_idx = GMnu_SPELL2;
        }
    }
    else if (mnu_idx == GMnu_ROOM)
    {
        if (menu_is_active(GMnu_ROOM2))
        {
            mnu_idx = GMnu_ROOM2;
        }
    }
    else if (mnu_idx == GMnu_TRAP)
    {
        if (menu_is_active(GMnu_TRAP2))
        {
            mnu_idx = GMnu_TRAP2;
        }
    }
    set_menu_mode(mnu_idx);
}

void gui_area_flash_cycle_button(struct GuiButton *gbtn)
{
    SYNCDBG(10,"Starting");
    int64_t spr_idx;
    spr_idx = gbtn->sprite_idx;
    int64_t ps_units_per_px;
    ps_units_per_px = simple_gui_panel_sprite_width_units_per_px(gbtn, spr_idx, 113);
    if ((gbtn->flags & LbBtnF_Enabled) != 0)
    {
        if ((!gbtn->button_state_left_pressed) && (!gbtn->button_state_right_pressed))
        {
            // If function is active, the button should blink
            unsigned char *ctptr;
            ctptr = (unsigned char *)gbtn->content.ptr;
            if ((ctptr != NULL) && (*ctptr > 0))
            {
                if ((get_gameturn() % (2 * kfx_config_state.gui_blink_rate)) >= kfx_config_state.gui_blink_rate) {
                    spr_idx += 2;
                }
            }
        }
        if ((!gbtn->button_state_left_pressed) && (!gbtn->button_state_right_pressed)) {
            spr_idx++;
        }
        draw_gui_panel_sprite_left(gbtn->scr_pos_x, gbtn->scr_pos_y, ps_units_per_px, spr_idx);
    } else
    {
        draw_gui_panel_sprite_rmleft(gbtn->scr_pos_x, gbtn->scr_pos_y, ps_units_per_px, spr_idx, 12);
    }
    SYNCDBG(12,"Finished");
}

struct GuiButton* get_gui_button(int64_t id)
{
    for (int64_t i=0; i < ACTIVE_BUTTONS_COUNT; i++)
    {
        struct GuiButton *gbtn = &active_buttons[i];
        if (gbtn->id_num == id)
        {
            return gbtn;
        }
    }
    return NULL;
}

struct GuiButtonInit * get_gui_button_init(struct GuiMenu * menu, int64_t id)
{
    for (int64_t i = 0 ;; i++)
    {
        struct GuiButtonInit * button = &menu->buttons[i];
        if (button->gbtype < 0) {
            return NULL;
        } else if (button->id_num == id)
        {
            return button;
        }
    }
}

void gui_draw_scroll_box(struct GuiButton *gbtn, int64_t height_lines, TbBool draw_scrollbar)
{
    const struct TbSprite *spr;
    int64_t pos_x;
    int64_t pos_y = gbtn->scr_pos_y;
    int64_t spr_idx;
    int64_t secspr_idx;
    int64_t i;
    int64_t delta;
    // Detect scaling factor is quite complicated for this item
    int64_t units_per_px;
    {
        int64_t orig_size = 0;
        spr = get_frontend_sprite(GFS_hugearea_thn_cor_ml);
        for (i=0; i < 6; i++)
        {
            orig_size += spr->SWidth;
            spr++;
        }
        units_per_px = (gbtn->width * 16 + orig_size/2) / orig_size;
    }
    // Draw top border
    spr = get_frontend_sprite(GFS_hugearea_thn_cor_tl);
    pos_x = gbtn->scr_pos_x;
    for (i=0; i < 6; i++)
    {
        LbSpriteDrawResized(pos_x, pos_y, units_per_px, spr);
        pos_x += spr->SWidth * units_per_px / 16;
        spr++;
    }
    if ( draw_scrollbar )
    {
        pos_x = gbtn->scr_pos_x + gbtn->width;
        draw_frontend_sprite_left(pos_x, pos_y - units_per_px/16, units_per_px, GFS_scrollbar_toparrow_std);
    }
    // Draw inside
    spr = get_frontend_sprite(GFS_hugearea_thn_cor_tl);
    pos_y += spr->SHeight * units_per_px / 16;
    for (; height_lines > 0; height_lines -= delta )
    {
      if (height_lines < 3)
          spr_idx = GFS_hugearea_thn_cor_ml;
      else
          spr_idx = GFS_hugearea_thc_cor_ml;
      spr = get_frontend_sprite(spr_idx);
      pos_x = gbtn->scr_pos_x;
      for (i=0; i < 6; i++)
      {
          LbSpriteDrawResized(pos_x, pos_y, units_per_px, spr);
          pos_x += spr->SWidth * units_per_px / 16;
          spr++;
      }
      if ( draw_scrollbar )
      {
        if ( height_lines < 3 )
            secspr_idx = GFS_scrollbar_vert_ct_short;
        else
            secspr_idx = GFS_scrollbar_vert_ct_long;
        pos_x = gbtn->scr_pos_x + gbtn->width;
        draw_frontend_sprite_left(pos_x, pos_y, units_per_px, secspr_idx);
      }
      spr = get_frontend_sprite(spr_idx);
      pos_y += spr->SHeight * units_per_px / 16;
      if (height_lines < 3)
          delta = 1;
      else
          delta = 3;
    }
    // Draw bottom border
    spr = get_frontend_sprite(GFS_hugearea_thn_cor_bl);
    pos_x = gbtn->scr_pos_x;
    for (i=0; i < 6; i++)
    {
        LbSpriteDrawResized(pos_x, pos_y, units_per_px, spr);
        pos_x += spr->SWidth * units_per_px / 16;
        spr++;
    }
    if ( draw_scrollbar )
    {
        pos_x = gbtn->scr_pos_x + gbtn->width;
        draw_frontend_sprite_left(pos_x, pos_y, units_per_px, GFS_scrollbar_btmarrow_std);
    }
}

/** Draws one row of the hugearea border/fill art as corner + repeating-
 * middle-tile + corner, at a fixed native scale. Every row "family" in
 * this art (top/middle-short/middle-long/bottom) is exactly 6 consecutive
 * sprite ids: [corner_L, tile1..tile4, corner_R] -- see sprites.h's
 * GFS_hugearea_* block. The corners are always drawn flush to the row's
 * own left/right edges at their native width; only the middle is tiled
 * (and, if it doesn't divide evenly, the last tile's right edge overlaps
 * under the corner rather than leaving a gap -- the corner is drawn last,
 * on top). This is what lets gui_draw_scroll_box_cropped shrink a box's
 * width without ever losing its left or right border, unlike clipping
 * straight across the whole 6-sprite sequence. Returns the row's height.
 */
static int64_t gui_draw_scroll_box_row(int64_t pos_y, int64_t width, int64_t units_per_px, int64_t base_spr_idx)
{
    const struct TbSprite *lspr = get_frontend_sprite(base_spr_idx);
    const struct TbSprite *rspr = get_frontend_sprite(base_spr_idx + 5);
    int64_t row_h = lspr->SHeight * units_per_px / 16;
    int64_t left_w = lspr->SWidth * units_per_px / 16;
    int64_t right_w = rspr->SWidth * units_per_px / 16;

    LbSpriteDrawResized(0, pos_y, units_per_px, lspr);

    int64_t x = left_w;
    int64_t fill_end = width - right_w;
    int64_t tile = 0;
    while (x < fill_end)
    {
        const struct TbSprite *tspr = get_frontend_sprite(base_spr_idx + 1 + (tile % 4));
        if (tspr->SWidth <= 0)
            break;
        LbSpriteDrawResized(x, pos_y, units_per_px, tspr);
        x += tspr->SWidth * units_per_px / 16;
        tile++;
    }

    LbSpriteDrawResized(width - right_w, pos_y, units_per_px, rspr);
    return row_h;
}

/** A resizable variant of gui_draw_scroll_box for panels narrower than the
 * border art's natural composed width at a legible scale -- gui_draw_scroll_box's
 * own units_per_px is derived from gbtn->width (see its own comment), which
 * squashes every row down to an illegible height once width drops much
 * below the art's full size. This draws the same top-border/repeating-rows/
 * bottom-border sequence at native 1:1 scale (so row height stays legible
 * regardless of width) via gui_draw_scroll_box_row (corners fixed, middle
 * tiled/clipped -- never the border itself), and extends the repeating
 * rows until the accumulated height fills gbtn->height (measured at draw
 * time via get_frontend_sprite, not a caller-supplied line count -- there's
 * no static sprite-size data to precompute this from). A graphics-window
 * clip to gbtn's own rect is still kept as a safety net against vertical
 * overshoot and the degenerate case of gbtn->width being narrower than
 * both corners combined.
 */
void gui_draw_scroll_box_cropped(struct GuiButton *gbtn, TbBool draw_scrollbar)
{
    const struct TbSprite *spr;
    int64_t pos_y;
    int64_t spr_idx;
    const int64_t units_per_px = 16;

    // Once a graphics window is active, draw coordinates are relative to
    // the window's own origin, not absolute screen position (confirmed
    // against gui_vscroll.c's scrl_draw_track, the only other user of this
    // idiom: it draws at literal (0,0) after LbScreenSetGraphicsWindow) --
    // every position below is local to gbtn's own rect, starting at (0,0),
    // not gbtn->scr_pos_x/y.
    TbGraphicsWindow grwnd;
    LbScreenStoreGraphicsWindow(&grwnd);
    LbScreenSetGraphicsWindow(gbtn->scr_pos_x, gbtn->scr_pos_y, gbtn->width, gbtn->height);

    // Draw top border
    pos_y = 0;
    int64_t top_h = gui_draw_scroll_box_row(pos_y, gbtn->width, units_per_px, GFS_hugearea_thn_cor_tl);
    if ( draw_scrollbar )
        draw_frontend_sprite_left(gbtn->width, pos_y - units_per_px/16, units_per_px, GFS_scrollbar_toparrow_std);
    pos_y += top_h;

    spr = get_frontend_sprite(GFS_hugearea_thn_cor_bl);
    int64_t bottom_h = spr->SHeight * units_per_px / 16;
    int64_t target_y = gbtn->height - bottom_h;

    // Draw inside -- prefer the taller "long" 3-line row group while it
    // still fits the remaining room, same choice gui_draw_scroll_box makes
    // from its height_lines countdown, just measured against accumulated
    // pixel height here instead.
    while (pos_y < target_y)
    {
        int64_t remaining = target_y - pos_y;
        spr = get_frontend_sprite(GFS_hugearea_thc_cor_ml);
        long long_h = spr->SHeight * units_per_px / 16;
        spr_idx = (long_h <= remaining) ? GFS_hugearea_thc_cor_ml : GFS_hugearea_thn_cor_ml;
        int64_t row_h = gui_draw_scroll_box_row(pos_y, gbtn->width, units_per_px, spr_idx);
        if ( draw_scrollbar )
        {
            int64_t secspr_idx = (spr_idx == GFS_hugearea_thc_cor_ml) ? GFS_scrollbar_vert_ct_long : GFS_scrollbar_vert_ct_short;
            draw_frontend_sprite_left(gbtn->width, pos_y, units_per_px, secspr_idx);
        }
        pos_y += row_h;
    }

    // Draw bottom border, flush with gbtn's own bottom edge regardless of
    // whether the last content row landed exactly at target_y.
    pos_y = gbtn->height - bottom_h;
    gui_draw_scroll_box_row(pos_y, gbtn->width, units_per_px, GFS_hugearea_thn_cor_bl);
    if ( draw_scrollbar )
        draw_frontend_sprite_left(gbtn->width, pos_y, units_per_px, GFS_scrollbar_btmarrow_std);

    LbScreenLoadGraphicsWindow(&grwnd);
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
