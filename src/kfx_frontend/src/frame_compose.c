/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frame_compose.c
 *     Composing the in-game frame.
 * @par Purpose:
 *     keeper_screen_redraw()/redraw_display(): kfx_render draws the world
 *     view (engine(), draw_frontview_engine(), draw_creature_view(), the
 *     hand), then this file lays the UI over it -- status panel, GUI,
 *     compass, messages, boxes, tooltips, the paused and armageddon
 *     captions, debug overlays -- and runs the map fade. Moved from
 *     kfx_render's engine_redraw.c in refactor pass 2 (S13,
 *     docs/refactor-pass2/stage-13-frame-composition.md), which reached
 *     all of this UI through RenderOverlayCallbacks.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     06 Nov 2010 - 03 Jul 2011
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "frame_compose.h"
#include "pointer_graphics.h"
#include "packets.h"               // unpausing_in_progress
#include "frontend.h"
#include "gui_parchment.h"
#include "gui_draw.h"
#include "gui_boxmenu.h"
#include "gui_msgs.h"
#include "gui_tooltips.h"
#include "gui_frontmenu.h"
#include "front_easter.h"
#include "frontmenu_ingame_evnt.h"
#include "frontmenu_ingame_map.h"
#include "frontmenu_ingame_tabs.h"
#include "frontgui_ingame_boxmenu.h"
#include "kfx_frontend_state.h"
#include "kfx_game_state.h"
#include "kjm_input.h"
#include "renderer/RendererManager.h"
#include "renderer/software/SwDrawTarget.h"
#include "config_keeperfx.h" // ingame_gui_use_classic_hud
#include "engine_redraw.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_math.h"
#include "bflib_sprfnt.h"
#include "bflib_sound.h"
#include "bflib_mouse.h"
#include "bflib_dernc.h"
#include "player_data.h"
#include "dungeon_data.h"
#include "player_instances.h"
#include "config_players.h"
#include "power_hand.h"
#include "power_process.h"
#include "engine_render.h"
#include "engine_lenses.h"
#include "local_camera.h"
#include "light_data.h"
#include "packet_data.h"
#include "creature_graphics.h"
#include "vidmode.h"
#include "config.h"
#include "config_strings.h"
#include "config_terrain.h"
#include "config_players.h"
#include "config_magic.h"
#include "config_spritecolors.h"
#include "magic_powers.h"
#include "kfx_render_state.h"
#include "creature_instances.h"
#include "custom_sprites.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "thing_objects.h"
#include "config_settings.h"
#include "render_power_hand.h"
#include "render_creature_view.h"
#include "ports/ui_port.h"
#include "local_state.h"
#include "post_inc.h"

/******************************************************************************/
static TbPixel * map_fade_dest;
static TbPixel * map_fade_src;
/******************************************************************************/
/* Were main.cpp's RenderOverlayCallbacks wrappers. */
static void load_and_redraw_minimal_overhead_view(void)
{
    load_parchment_file();
    redraw_minimal_overhead_view();
}

static int64_t get_main_menu_width(void)
{
    struct GuiMenu *gmnu = get_active_menu(menu_id_to_number(GMnu_MAIN));
    return gmnu->width;
}

static void sync_cheat_box_3_active_option(CrInstance active_instance_id)
{
    if (!gui_box_is_not_valid(kfx_frontend_local.gui_cheat_box_3))
    {
        struct GuiBoxOption* guop = kfx_frontend_local.gui_cheat_box_3->optn_list;
        while (guop->label[0] != '!')
        {
            guop->active = (active_instance_id == guop->cb_param1);
            guop++;
        }
    }
}

static TbBool bonus_script_or_variable_overlay_active(void)
{
    return bonus_timer_enabled() || script_timer_enabled() || display_variable_enabled();
}

static void draw_debug_overlays(void)
{
    // Phase 2 (docs/refactor/ingame-gui/03-debug-overlays-and-box-menus.md):
    // when the ImGui in-game HUD is active, ingame_debug_overlays_frame()
    // (frontgui_ingame_debug.cpp) draws all of these instead, from the
    // FrontendImGuiFrame submission -- same *_enabled() gates. Only reached
    // for the classic HUD (ingame_gui_use_classic_hud()).
    if (!ingame_gui_use_classic_hud())
        return;
    if (bonus_timer_enabled())
    {
        draw_bonus_timer();
    }
    else if (script_timer_enabled())
    {
        draw_script_timer(kfx_game_state.script_timer_player, kfx_game_state.script_timer_id, kfx_game_state.script_timer_limit, kfx_game_state.timer_real);
    }
    if (gameturn_timer_enabled())
    {
        draw_gameturn_timer();
    }
    if (display_variable_enabled())
    {
        draw_script_variable(kfx_game_state.script_variables[0].variable_player, kfx_game_state.script_variables[0].value_type, kfx_game_state.script_variables[0].value_id, kfx_game_state.script_variables[0].variable_target, kfx_game_state.script_variables[0].variable_target_type);
    }
    if (timer_enabled())
    {
        draw_timer();
    }
    if (frametime_enabled())
    {
        draw_frametime();
    }
    if (debug_display_network_stats != 0)
    {
        draw_network_stats();
    }
    if (consolelog_enabled())
    {
        draw_consolelog();
    }
}
/******************************************************************************/
/******************************************************************************/
static void draw_creature_view_icons(struct Thing* creatng)
{
    ScreenCoord x = get_main_menu_width() + scale_value_by_horizontal_resolution(5);
    ScreenCoord y;
    const struct TbSprite* spr;
    int64_t ps_units_per_px;
    {
        spr = get_panel_sprite(488);
        ps_units_per_px = (22 * units_per_pixel) / spr->SHeight;
        y = MyScreenHeight - scale_ui_value_lofi(spr->SHeight * 2);
    }
    struct CreatureControl *cctrl = creature_control_get_from_thing(creatng);
    for (SpellKind spell_idx = 0; spell_idx < CREATURE_MAX_SPELLS_CASTED_AT; spell_idx++)
    {
        struct CastedSpellData* cspell = &cctrl->casted_spells[spell_idx];
        if (cspell->spkind == 0)
        {
            continue;
        }
        struct SpellConfig *spconf = get_spell_config(cspell->spkind);
        int64_t spridx = spconf->medsym_sprite_idx;
        if (flag_is_set(spconf->spell_flags, CSAfF_Invisibility))
        {
            if (cctrl->force_visible & 2)
            {
                spridx++;
            }
        }
        if (flag_is_set(spconf->spell_flags, CSAfF_Timebomb))
        {
            int64_t tx_units_per_px = (dbc_initialized && dbc_enabled) ? scale_ui_value_lofi(16) : (22 * units_per_pixel) / LbTextLineHeight();
            int64_t h = LbTextLineHeight() * tx_units_per_px / 16;
            int64_t w = scale_ui_value_lofi(spr->SWidth);
            if (dbc_initialized && dbc_enabled)
            {
                if (MyScreenHeight < 400)
                {
                    w *= 2;
                }
            }
            LbTextSetWindow(x + scale_ui_value_lofi(spr->SWidth / 2), y - scale_ui_value_lofi(spr->SHeight), w, h);
            RendererSetDrawFlags(Lb_TEXT_HALIGN_CENTER);
            RendererSetDrawColour(LbTextGetFontFaceColor());
            lbDisplayEx.ShadowColour = LbTextGetFontBackColor();
            char text[16];
            snprintf(text, sizeof(text), "%" PRIu64, (uint64_t)((cctrl->timebomb_countdown / kfx_sim_state.turns_per_second)));
            LbTextDrawResized(0, 0, tx_units_per_px, text);
        }
        draw_gui_panel_sprite_left(x, y, ps_units_per_px, spridx);
        x += scale_ui_value_lofi(spr->SWidth);
    }
    if ( (cctrl->dragtng_idx != 0) && ((creatng->alloc_flags & TAlF_IsDragged) == 0) )
    {
        struct Thing* dragtng = thing_get(cctrl->dragtng_idx);
        uint64_t spr_idx;
        x = MyScreenWidth - (scale_value_by_horizontal_resolution(148) / 4);
        switch(dragtng->class_id)
        {
            case TCls_Object:
            {
                RoomKind rkind;
                struct RoomConfigStats *roomst;
                if (thing_is_workshop_crate(dragtng))
                {
                    rkind = find_first_roomkind_with_role(RoRoF_CratesStorage);
                }
                else
                {
                    rkind = find_first_roomkind_with_role(RoRoF_PowersStorage);
                }
                roomst = get_room_kind_stats(rkind);
                spr_idx = roomst->medsym_sprite_idx;
                break;
            }
            case TCls_DeadCreature:
            case TCls_Creature:
            {
                y -= scale_value_by_horizontal_resolution(spr->SHeight / 2);
                spr_idx = get_creature_model_graphics(dragtng->model, CGI_HandSymbol);
                if (dragtng->class_id == TCls_DeadCreature)
                {
                    spr_idx++;
                }
                break;
            }
            default:
            {
                spr_idx = 0;
                break;
            }
        }
        draw_gui_panel_sprite_left(x, y, ps_units_per_px, spr_idx);
    }
    else
    {
        struct PlayerInfo* player = get_my_player();
        if (player->view_type == PVT_CreatureContrl)
        {
            if (!creature_instance_is_available(creatng, cctrl->active_instance_id)
                && (kfx_config_state.conf.crtr_conf.instances_count > 0))
            {
                x = MyScreenWidth - (scale_value_by_horizontal_resolution(148) / 4);
                struct InstanceInfo* inst_inf = creature_instance_info_get(cctrl->active_instance_id % kfx_config_state.conf.crtr_conf.instances_count);
                draw_gui_panel_sprite_left(x, y, ps_units_per_px, inst_inf->symbol_spridx);
            }
        }
    }
}

/**
 * Renders source and destination screens for map fading.
 * Stores them in given buffers.
 * @param fade_src
 * @param fade_dest
 * @param scanline Line width of the two given buffers.
 * @param height Height to be filled in given buffers.
 */
void prepare_map_fade_buffers(TbPixel *fade_src, TbPixel *fade_dest, int64_t scanline, int64_t height)
{
    struct PlayerInfo* player = get_my_player();
    // render the 3D screen
    if (player->view_mode_restore == PVM_IsoWibbleView || player->view_mode_restore == PVM_IsoStraightView)
      redraw_isometric_view();
    else
      redraw_frontview();
    // Copy the screen to fade source temp buffer
    int64_t i;
    int64_t fadebuf_pos = 0;
    for (i = 0; i < height; i++)
    {
        TbPixel* src = SwTargetWScreen() + lbDisplay.GraphicsScreenWidth * i;
        TbPixel* dst = &fade_src[fadebuf_pos];
        fadebuf_pos += scanline;
        memcpy(dst, src, (MyScreenWidth/pixel_size) * sizeof(TbPixel));
    }
    // create the parchment screen
    load_and_redraw_minimal_overhead_view();
    // Copy the screen to fade destination temp buffer
    fadebuf_pos = 0;
    for (i = 0; i < height; i++)
    {
        TbPixel* src = SwTargetWScreen() + lbDisplay.GraphicsScreenWidth * i;
        TbPixel* dst = &fade_dest[fadebuf_pos];
        fadebuf_pos += scanline;
        memcpy(dst, src, (MyScreenWidth/pixel_size) * sizeof(TbPixel));
    }
}

int64_t map_fade_in(int64_t palette_fade_step)
{
    SYNCDBG(6,"Starting");
    if (palette_fade_step == 0)
    {
        /* Carved out of the shared poly_pool scratch buffer, same as before
         * the map_fade_ghost_table slot (now retired -- see map_fade()'s
         * comment) was dropped; poly_pool is large enough (16MB) either way. */
        map_fade_src = (TbPixel *)poly_pool;
        map_fade_dest = map_fade_src + 320*200;
        prepare_map_fade_buffers(map_fade_src, map_fade_dest, 320, MyScreenHeight/pixel_size);
    }
    map_fade(SwTargetWScreen(), map_fade_dest, map_fade_src,
        palette_fade_step, 320, 200, lbDisplay.GraphicsScreenWidth);
    return (8 - get_my_player()->instance_remain_turns) * 4;
}

int64_t map_fade_out(int64_t palette_fade_step)
{
    SYNCDBG(6,"Starting");
    if (palette_fade_step == 32)
    {
        map_fade_src = (TbPixel *)poly_pool;
        map_fade_dest = map_fade_src + 320*200;
        prepare_map_fade_buffers(map_fade_src, map_fade_dest, 320, MyScreenHeight/pixel_size);
    }
    map_fade(SwTargetWScreen(), map_fade_dest, map_fade_src,
      palette_fade_step, 320, 200, lbDisplay.GraphicsScreenWidth);
    return get_my_player()->instance_remain_turns * 4;
}

void draw_overlay_compass(int64_t base_x, int64_t base_y)
{
    // Phase 4: drawn by ingame_panel_frame() (frontgui_ingame_panel.cpp)
    // over the ImGui minimap texture when the ImGui in-game HUD is active.
    // Only reached for the classic HUD (ingame_gui_use_classic_hud()).
    if (!ingame_gui_use_classic_hud())
        return;
    struct PlayerInfo* player = get_my_player();
    struct Camera* cam = get_local_active_camera(player);
    int64_t flg_mem = RendererGetDrawFlags();
    int64_t status_panel_width_local = ui_get_status_panel_width();
    int64_t map_diag = MapDiagonalLength;
    LbTextSetFont(winfont);
    RendererAddDrawFlags(Lb_SPRITE_TRANSPAR4);
    LbTextSetWindow(0, 0, MyScreenWidth, MyScreenHeight);
    int64_t units_per_px = (16 * status_panel_width_local + 140 / 2) / 140;
    int64_t tx_units_per_px = (22 * units_per_px) / LbTextLineHeight();
    int64_t w = (LbSprFontCharWidth(lbFontPtr, '/') * tx_units_per_px / 16) / 2;
    int64_t h = (LbSprFontCharHeight(lbFontPtr, '/') * tx_units_per_px / 16) / 2 + 2 * units_per_px / 16;
    int64_t center_x = base_x * units_per_px / 16 + map_diag / 2;
    int64_t center_y = base_y * units_per_px / 16 + map_diag / 2;
    int64_t shift_x = (-(map_diag * 7 / 16) * LbSinL(cam->rotation_angle_x)) >> LbFPMath_TrigmBits;
    int64_t shift_y = (-(map_diag * 7 / 16) * LbCosL(cam->rotation_angle_x)) >> LbFPMath_TrigmBits;
    if (LbScreenIsLocked()) {
        LbTextDrawResized(center_x + shift_x - w, center_y + shift_y - h, tx_units_per_px, get_string(GUIStr_MapN));
    }
    shift_x = ( (map_diag*7/16) * LbSinL(cam->rotation_angle_x)) >> LbFPMath_TrigmBits;
    shift_y = ( (map_diag*7/16) * LbCosL(cam->rotation_angle_x)) >> LbFPMath_TrigmBits;
    if (LbScreenIsLocked()) {
        LbTextDrawResized(center_x + shift_x - w, center_y + shift_y - h, tx_units_per_px, get_string(GUIStr_MapS));
    }
    shift_x = ( (map_diag*7/16) * LbCosL(cam->rotation_angle_x)) >> LbFPMath_TrigmBits;
    shift_y = (-(map_diag*7/16) * LbSinL(cam->rotation_angle_x)) >> LbFPMath_TrigmBits;
    if (LbScreenIsLocked()) {
        LbTextDrawResized(center_x + shift_x - w, center_y + shift_y - h, tx_units_per_px, get_string(GUIStr_MapE));
    }
    shift_x = (-(map_diag*7/16) * LbCosL(cam->rotation_angle_x)) >> LbFPMath_TrigmBits;
    shift_y = ( (map_diag*7/16) * LbSinL(cam->rotation_angle_x)) >> LbFPMath_TrigmBits;
    if (LbScreenIsLocked()) {
        LbTextDrawResized(center_x + shift_x - w, center_y + shift_y - h, tx_units_per_px, get_string(GUIStr_MapW));
    }
    RendererSetDrawFlags(flg_mem);
}

void redraw_creature_view(void)
{
    SYNCDBG(6,"Starting");
    struct PlayerInfo* player = get_my_player();
    update_explored_flags_for_power_sight(player);
    struct Thing* thing = thing_get(player->controlled_thing_idx);
    TRACE_THING(thing);
    if (thing_exists(thing))
      draw_creature_view(thing);
    if (kfx_runtime_settings.vid_smooth)
    {
        TbGraphicsWindow ewnd;
        store_engine_window(&ewnd, pixel_size);
        smooth_screen_area(SwTargetWScreen(), ewnd.x, ewnd.y,
            ewnd.width, ewnd.height, lbDisplay.GraphicsScreenWidth);
    }
    remove_explored_flags_for_power_sight(player);
    if ((kfx_sim_state.operation_flags & GOF_ShowGui) != 0) {
        draw_whole_status_panel();
    }
    draw_gui();
    if ((kfx_sim_state.operation_flags & GOF_ShowGui) != 0) {
        draw_overlay_compass(local_state.minimap_pos_x, local_state.minimap_pos_y);
    }
    message_draw();
    gui_draw_all_boxes();
    draw_tooltip();
    struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
    if (!creature_control_invalid(cctrl))
    {
        draw_creature_view_icons(thing);
        sync_cheat_box_3_active_option(cctrl->active_instance_id);
    }
}

void redraw_isometric_view(void)
{
    SYNCDBG(6,"Starting");

    struct PlayerInfo* player = get_my_player();
    if (player_invalid(player) || (get_player_active_camera(player) == NULL))
        return;
    TbGraphicsWindow ewnd;
    memset(&ewnd, 0, sizeof(TbGraphicsWindow));
    struct Camera* render_cam = get_local_active_camera(player);
    update_explored_flags_for_power_sight(player);
    engine(player,render_cam);
    if (kfx_runtime_settings.vid_smooth)
    {
        store_engine_window(&ewnd,pixel_size);
        smooth_screen_area(SwTargetWScreen(), ewnd.x, ewnd.y,
            ewnd.width, ewnd.height, lbDisplay.GraphicsScreenWidth);
    }
    remove_explored_flags_for_power_sight(player);
    if ((kfx_sim_state.operation_flags & GOF_ShowGui) != 0) {
        draw_whole_status_panel();
    }
    draw_gui();
    if ((kfx_sim_state.operation_flags & GOF_ShowGui) != 0) {
        draw_overlay_compass(local_state.minimap_pos_x, local_state.minimap_pos_y);
    }
    message_draw();
    gui_draw_all_boxes();
    draw_power_hand();
    draw_tooltip();
    SYNCDBG(8,"Finished");
}

void redraw_frontview(void)
{
    SYNCDBG(6,"Starting");
    struct PlayerInfo* player = get_my_player();
    struct Camera* render_cam = get_local_active_camera(player);
    update_explored_flags_for_power_sight(player);
    draw_frontview_engine(render_cam);
     remove_explored_flags_for_power_sight(player);
    if (flag_is_set(kfx_sim_state.operation_flags,GOF_ShowGui)) {
        draw_whole_status_panel();
    }
    draw_gui();
    if (flag_is_set(kfx_sim_state.operation_flags,GOF_ShowGui)) {
        draw_overlay_compass(local_state.minimap_pos_x, local_state.minimap_pos_y);
    }
    message_draw();
    draw_power_hand();
    draw_tooltip();
    gui_draw_all_boxes();
}

void redraw_display(void)
{
    SYNCDBG(5,"Starting");
    struct PlayerInfo* player = get_my_player();
    local_state.display_needs_update = false;
    if (kfx_sim_state.game_kind == GKind_NonInteractiveState)
      return;
    if (kfx_sim_state.small_map_state == 2)
      set_pointer_graphic_none();
    else
      process_pointer_graphic();
    interpolate_local_cameras();
    switch (get_local_active_camera(player)->view_mode)
    {
    case PVM_EmptyView:
        break;
    case PVM_CreatureView:
        redraw_creature_view();
        parchment_loaded = 0;
        break;
    case PVM_IsoWibbleView:
    case PVM_IsoStraightView:
        redraw_isometric_view();
        parchment_loaded = 0;
        break;
    case PVM_ParchmentView:
        redraw_parchment_view();
        break;
    case PVM_FrontView:
        redraw_frontview();
        parchment_loaded = 0;
        break;
    case PVM_ParchFadeIn:
        parchment_loaded = 0;
        local_state.palette_fade_step_map = map_fade_in(local_state.palette_fade_step_map);
        break;
    case PVM_ParchFadeOut:
        parchment_loaded = 0;
        local_state.palette_fade_step_map = map_fade_out(local_state.palette_fade_step_map);
        break;
    default:
        ERRORLOG("Unsupported drawing state, %" PRId64,(int64_t)player->view_mode);
        break;
    }
    //LbTextSetWindow(0, 0, MyScreenWidth, MyScreenHeight);
    LbTextSetFont(winfont);
    RendererClearDrawFlags(Lb_TEXT_ONE_COLOR);
    int64_t tx_units_per_px = ( (MyScreenHeight < 400) && (dbc_initialized && dbc_enabled) ) ? scale_ui_value(32) : (22 * units_per_pixel) / LbTextLineHeight();
    LbTextSetWindow(0, 0, MyScreenWidth, MyScreenHeight);
    // Phase 3: the MP chat input line moves to ingame_text_overlays_frame()
    // under the ImGui HUD (input handling -- get_players_message_inputs() --
    // is unchanged; only this echo of player->mp_message_text moves).
    if (((get_local_user_state()->init_flags & UsrIF_NewMPMessage) != 0) && ingame_gui_use_classic_hud())
    {
        char text[sizeof(player->mp_message_text) + 4];
        snprintf(text, sizeof(text), ">%s_", player->mp_message_text);
        int64_t pos_x = 148*units_per_pixel/16;
        int64_t pos_y = 8*units_per_pixel/16;
        if (kfx_sim_state.armageddon_cast_turn != 0)
        {
            if (bonus_script_or_variable_overlay_active())
            {
                pos_y = ((pos_y << 3) + ((LbTextLineHeight()*units_per_pixel/16) * (kfx_sim_state.active_messages_count << (MyScreenHeight < 400))));
            }
        }
        LbTextDrawResized(pos_x, pos_y, tx_units_per_px, text);
    }
    if ( draw_spell_cost )
    {
        int64_t drwflags_mem = RendererGetDrawFlags();
        LbTextSetWindow(0, 0, MyScreenWidth, MyScreenHeight);
        RendererSetDrawFlags(0);
        LbTextSetFont(winfont);
        char text[16];
        if (draw_spell_cost > 0)
            snprintf(text, sizeof(text), "%" PRId64, (int64_t)(draw_spell_cost));
	else
            snprintf(text, sizeof(text), "lv%" PRId64, (int64_t)((-draw_spell_cost)));
        int64_t pos_y = GetMouseY() - (LbTextStringHeight(text) * units_per_pixel / 16) / 2 - 2 * units_per_pixel / 16;
        int64_t pos_x = GetMouseX() - (LbTextStringWidth(text) * units_per_pixel / 16) / 2;
        LbTextDrawResized(pos_x, pos_y, tx_units_per_px, text);
        RendererSetDrawFlags(drwflags_mem);
        draw_spell_cost = 0;
    }
    draw_debug_overlays();

    // Phase 3 (docs/refactor/ingame-gui/04-messages-tooltips-infobox.md):
    // with the ImGui HUD on, ingame_text_overlays_frame()
    // (frontgui_ingame_text.cpp) draws the "Paused" caption instead, from
    // the FrontendImGuiFrame submission -- same GOF_Paused/WorldInfluence/
    // unpausing gate.
    if (ingame_gui_use_classic_hud()
     && ((kfx_sim_state.operation_flags & GOF_Paused) != 0) && ((kfx_sim_state.operation_flags & GOF_WorldInfluence) == 0) && !unpausing_in_progress)
    {
          LbTextSetFont(winfont);
          const char * text = get_string(GUIStr_PausedMsg);
          int64_t w = (LbTextStringWidth(text) * units_per_pixel / 16 + 2 * (LbTextCharWidth(' ') * units_per_pixel / 16));
          int64_t pos_x;
          struct Camera *camera = get_local_active_camera(player);
          if (camera->view_mode == PVM_IsoWibbleView || camera->view_mode == PVM_FrontView || camera->view_mode == PVM_IsoStraightView || camera->view_mode == PVM_CreatureView) {
              pos_x = local_state.engine_window_x + (MyScreenWidth - w - local_state.engine_window_x) / 2;
          } else {
              pos_x = (MyScreenWidth-w)/2;
          }
          int64_t pos_y = 16 * units_per_pixel / 16;
          RendererSetDrawFlags(Lb_TEXT_HALIGN_CENTER);
          int64_t h = LbTextLineHeight() * units_per_pixel / 16;
          int64_t text_w = w;
          int64_t text_x = pos_x;
          if (MyScreenHeight < 400)
          {
              w *= 2;
              h *= 3;
              text_w = w;
              if (dbc_initialized && dbc_enabled)
              {
                  text_w += 32;
                  text_x -= 12;
              }
          }
          LbTextSetWindow(text_x, pos_y, text_w, h);
          draw_slab64k(pos_x, pos_y, units_per_pixel, w, h);
          LbTextDrawResized(0/pixel_size, 0/pixel_size, tx_units_per_px, text);
          LbTextSetWindow(0/pixel_size, 0/pixel_size, MyScreenWidth/pixel_size, MyScreenHeight/pixel_size);
    }
    if (kfx_sim_state.armageddon_cast_turn != 0)
    {
        int64_t i = 0;
        if (kfx_sim_state.armageddon_cast_turn + kfx_config_state.conf.rules[kfx_sim_state.armageddon_caster_idx].magic.armageddon_count_down <= get_gameturn())
        {
            if (kfx_sim_state.armageddon_over_turn - kfx_config_state.conf.rules[kfx_sim_state.armageddon_caster_idx].magic.armageddon_duration <= get_gameturn())
                i = kfx_sim_state.armageddon_over_turn - get_gameturn();
        } else
        {
            i = get_gameturn() - kfx_sim_state.armageddon_cast_turn - kfx_config_state.conf.rules[kfx_sim_state.armageddon_caster_idx].magic.armageddon_count_down;
        }
        LbTextSetFont(winfont);
        char text[64];
        snprintf(text, sizeof(text), " %s %03" PRId64, get_string(get_power_name_strindex(PwrK_ARMAGEDDON)), (int64_t)(i/2)); // Armageddon message
        i = LbTextCharWidth(' ')*units_per_pixel/16;
        int64_t w = LbTextStringWidth(text) * units_per_pixel / 16 + 6 * i;
        i = LbTextLineHeight()*units_per_pixel/16;
        RendererSetDrawFlags(Lb_TEXT_HALIGN_CENTER);
        int64_t h = pixel_size * i + pixel_size * i / 2;
        if (MyScreenHeight < 400)
        {
            w *= 2;
            h *= 2;
        }
        int64_t pos_x = MyScreenWidth - w - 16 * units_per_pixel / 16;
        int64_t pos_y = 16 * units_per_pixel / 16;
        LbTextSetWindow(pos_x, pos_y, w, h);
        draw_slab64k(pos_x, pos_y, units_per_pixel, w, h);
        LbTextDrawResized(0/pixel_size, 0/pixel_size, tx_units_per_px, text);
        LbTextSetWindow(0/pixel_size, 0/pixel_size, MyScreenWidth/pixel_size, MyScreenHeight/pixel_size);
    }
    draw_eastegg();
  //show_onscreen_msg(8, "Physical(%d,%d) Graphics(%d,%d) Lens(%d,%d)", (int)lbDisplay.PhysicalScreenWidth, (int)lbDisplay.PhysicalScreenHeight, (int)lbDisplay.GraphicsScreenWidth, (int)lbDisplay.GraphicsScreenHeight, (int)eye_lens_width, (int)eye_lens_height);
    SYNCDBG(7,"Finished");
}

/**
 * Redraws the game display buffer.
 */
TbBool keeper_screen_redraw(void)
{
    SYNCDBG(5,"Starting");
    RendererClearScreen(144);
    if (RendererLockFramebuffer() == Lb_SUCCESS)
    {
        setup_engine_window(local_state.engine_window_x, local_state.engine_window_y,
            local_state.engine_window_width, local_state.engine_window_height);
        redraw_display();
        RendererUnlockFramebuffer();
        return true;
    }
    return false;
}
