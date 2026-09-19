/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_redraw.c
 *     Functions to redraw the engine screen.
 * @par Purpose:
 *     High level redrawing routines.
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
#include "sim_feedback.h"
#include "render_overlay.h"
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
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/******************************************************************************/
#ifdef __cplusplus
}
#endif
/******************************************************************************/
void redraw_isometric_view(void);
void redraw_frontview(void);
/******************************************************************************/
int32_t xtab[640][2];
int32_t ytab[480][2];

unsigned char smooth_on;
static TbPixel * map_fade_dest;
static TbPixel * map_fade_src;
static long draw_spell_cost;
/******************************************************************************/
static void draw_creature_view_icons(struct Thing* creatng)
{
    ScreenCoord x = render_overlay->get_main_menu_width() + scale_value_by_horizontal_resolution(5);
    ScreenCoord y;
    const struct TbSprite* spr;
    int ps_units_per_px;
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
        long spridx = spconf->medsym_sprite_idx;
        if (flag_is_set(spconf->spell_flags, CSAfF_Invisibility))
        {
            if (cctrl->force_visible & 2)
            {
                spridx++;
            }
        }
        if (flag_is_set(spconf->spell_flags, CSAfF_Timebomb))
        {
            int tx_units_per_px = (dbc_initialized && dbc_enabled) ? scale_ui_value_lofi(16) : (22 * units_per_pixel) / LbTextLineHeight();
            int h = LbTextLineHeight() * tx_units_per_px / 16;
            int w = scale_ui_value_lofi(spr->SWidth);
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
            snprintf(text, sizeof(text), "%u", (cctrl->timebomb_countdown / kfx_sim_state.turns_per_second));
            LbTextDrawResized(0, 0, tx_units_per_px, text);
        }
        render_overlay->draw_gui_panel_sprite_left(x, y, ps_units_per_px, spridx);
        x += scale_ui_value_lofi(spr->SWidth);
    }
    if ( (cctrl->dragtng_idx != 0) && ((creatng->alloc_flags & TAlF_IsDragged) == 0) )
    {
        struct Thing* dragtng = thing_get(cctrl->dragtng_idx);
        unsigned long spr_idx;
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
        render_overlay->draw_gui_panel_sprite_left(x, y, ps_units_per_px, spr_idx);
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
                render_overlay->draw_gui_panel_sprite_left(x, y, ps_units_per_px, inst_inf->symbol_spridx);
            }
        }
    }
}

void setup_engine_window(long x, long y, long width, long height)
{
    SYNCDBG(6,"Starting for size (%ld,%ld) at (%ld,%ld)",width,height,x,y);
    long status_panel_width_local = render_overlay->get_status_panel_width();
    if ((kfx_sim_state.operation_flags & GOF_ShowGui) != 0)
    {
      if (x > MyScreenWidth)
        x = MyScreenWidth;
      if (x < status_panel_width_local)
        x = status_panel_width_local;
    } else
    {
      if (x > MyScreenWidth)
        x = MyScreenWidth;
      if (x < 0)
        x = 0;
    }
    if (y > MyScreenHeight)
      y = MyScreenHeight;
    if (y < 0)
      y = 0;
    if (x+width > MyScreenWidth)
      width = MyScreenWidth-x;
    if (width < 0)
      width = 0;
    if (y+height > MyScreenHeight)
      height = MyScreenHeight-y;
    if (height < 0)
      height = 0;
    local_state.engine_window_x = x;
    local_state.engine_window_y = y;
    local_state.engine_window_width = width;
    local_state.engine_window_height = height;
}

void store_engine_window(TbGraphicsWindow *ewnd,int divider)
{
    if (divider <= 1)
    {
        ewnd->x = local_state.engine_window_x;
        ewnd->y = local_state.engine_window_y;
        ewnd->width = local_state.engine_window_width;
        ewnd->height = local_state.engine_window_height;
    } else
    {
        ewnd->x = local_state.engine_window_x/divider;
        ewnd->y = local_state.engine_window_y/divider;
        ewnd->width = local_state.engine_window_width/divider;
        ewnd->height = local_state.engine_window_height/divider;
    }
    ewnd->ptr = NULL;
}

void load_engine_window(TbGraphicsWindow *ewnd)
{
    local_state.engine_window_x = ewnd->x;
    local_state.engine_window_y = ewnd->y;
    local_state.engine_window_width = ewnd->width;
    local_state.engine_window_height = ewnd->height;
}

/* fade_tbl/ghost_tbl (the palette-index render_fade_tables/map_fade_ghost_table
 * lookups the original used) are retired now that both buffers hold real
 * TbPixel colours instead of palette indices: shading a captured snapshot no
 * longer needs a palette-index table lookup (render_shade() computes it
 * directly on the sample), and combining the two shaded snapshots was
 * always a straight additive RGB sum (docs/refactor/renderer/
 * 02a-pixel-format-design.md §2.5's generate_map_fade_ghost_table finding:
 * `output = colour1 + colour2`, clamped) rather than a genuine 1/3-2/3 ghost
 * blend -- so it needs clamp(), not render_ghost_blend(). */
void map_fade(TbPixel *outbuf, TbPixel *srcbuf1, TbPixel *srcbuf2, long a6, long const xmax, long const ymax, long a9)
{
    long ix;
    long iy;
    long x1base = 4 * a6;
    long x0base = 4 * (32 - a6);
    int32_t * xt = xtab[0];
    int vx0 = 0;
    int vx1 = 0;
    for (ix = xmax; ix > 0; ix--)
    {
        long val = x1base + vx1 / xmax;
        long m;
        if (val >= 0)
        {
            m = min(xmax,val);
        }
        else
        {
            m = 0;
        }
        xt[1] = m;
        val = x0base + vx0 / xmax;
        if (val >= 0) {
            m = min(xmax,val);
        } else {
            m = 0;
        }
        xt[0] = m;
        xt += 2;
        vx0 += xmax - 8 * (32 - a6);
        vx1 += xmax - 8 * a6;
    }

    long y1base = 8 * ymax / xmax * x1base / 8;
    long y0base = 8 * ymax / xmax * x0base / 8;
    int32_t * yt = ytab[0];
    int vy1 = 0;
    int vy0 = 0;
    for (iy = ymax; iy > 0; iy--)
    {
        long val = y1base + vy1 / ymax;
        long m;
        if (val >= 0)
        {
            m = min(ymax,val);
        }
        else
        {
            m = 0;
        }
        yt[1] = xmax * m;
        val = y0base + vy0 / ymax;
        if (val >= 0)
        {
            m = min(ymax,val);
        } else {
            m = 0;
        }
        yt[0] = xmax * m;
        yt += 2;
        vy0 += ymax - 2 * y0base;
        vy1 += ymax - 2 * y1base;
    }

    const int shade1 = a6;
    const int shade2 = 32 - a6;
    TbPixel* out = outbuf;
    yt = ytab[0];
    for (iy = ymax; iy > 0; iy--)
    {
        TbPixel* sbuf2 = &srcbuf2[yt[1]];
        TbPixel* sbuf1 = &srcbuf1[yt[0]];
        xt = xtab[0];
        for (ix = xmax; ix > 0; ix--)
        {
            TbPixel px1 = render_shade(sbuf1[xt[0]], shade1);
            TbPixel px2 = render_shade(sbuf2[xt[1]], shade2);
            *out = TbPixel_RGBA(
                (uint8_t)clamp((int)px1.r + px2.r, 0, 255),
                (uint8_t)clamp((int)px1.g + px2.g, 0, 255),
                (uint8_t)clamp((int)px1.b + px2.b, 0, 255),
                255);
            out++;
            xt += 2;
        }
        out += a9 - xmax;
        yt += 2;
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
void prepare_map_fade_buffers(TbPixel *fade_src, TbPixel *fade_dest, int scanline, int height)
{
    struct PlayerInfo* player = get_my_player();
    // render the 3D screen
    if (player->view_mode_restore == PVM_IsoWibbleView || player->view_mode_restore == PVM_IsoStraightView)
      redraw_isometric_view();
    else
      redraw_frontview();
    // Copy the screen to fade source temp buffer
    int i;
    int fadebuf_pos = 0;
    for (i = 0; i < height; i++)
    {
        TbPixel* src = SwTargetWScreen() + lbDisplay.GraphicsScreenWidth * i;
        TbPixel* dst = &fade_src[fadebuf_pos];
        fadebuf_pos += scanline;
        memcpy(dst, src, (MyScreenWidth/pixel_size) * sizeof(TbPixel));
    }
    // create the parchment screen
    render_overlay->load_and_redraw_minimal_overhead_view();
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

long map_fade_in(long palette_fade_step)
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

long map_fade_out(long palette_fade_step)
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

long dummy_sound_line_of_sight(long a1, long a2, long a3, long a4, long a5, long a6)
{
    return 1;
}

void set_engine_view(struct PlayerInfo *player, long val)
{
    switch ( val )
    {
    case PVM_EmptyView:
        set_player_active_camera(player, CamIV_Isometric);
        // Allow view mode 0 only for non-local-human players
        if (!is_my_player(player))
            break;
        // If it's local human player, then setting this mode is an error
        // fall through
    default:
        ERRORLOG("Invalid view mode %d",(int)val);
        val = PVM_CreatureView;
        // fall through
    case PVM_CreatureView:
        set_player_active_camera(player, CamIV_FirstPerson);
        sync_local_camera(player);
        if (!is_my_player(player))
            break;
        lens_mode = 2;
        S3DSetLineOfSightFunction(dummy_sound_line_of_sight);
        S3DSetDeadzoneRadius(0);
        LbMouseSetPosition((MyScreenWidth/pixel_size) >> 1,(MyScreenHeight/pixel_size) >> 1);
        break;
    case PVM_IsoWibbleView:
    case PVM_IsoStraightView:
    {
        struct Camera *camera = &player->cameras[CamIV_Isometric];
        set_player_active_camera(player, CamIV_Isometric);
        camera->view_mode = val;
        sync_local_camera(player);
        if (!is_my_player(player))
            break;
        lens_mode = 0;
        // no need to set temp_cluedo_mode here; it's done in update_engine_settings
        S3DSetLineOfSightFunction(dummy_sound_line_of_sight);
        S3DSetDeadzoneRadius(1280);
        break;
    }
    case PVM_ParchmentView:
        set_player_active_camera(player, CamIV_Parchment);
        sync_local_camera(player);
        if (!is_my_player(player))
            break;
        S3DSetLineOfSightFunction(dummy_sound_line_of_sight);
        S3DSetDeadzoneRadius(1280);
        break;
    case PVM_ParchFadeIn:
    case PVM_ParchFadeOut:
        // In fade states, keep the settings unchanged
        break;
    case PVM_FrontView:
        set_player_active_camera(player, CamIV_FrontView);
        sync_local_camera(player);
        if (!is_my_player(player))
            break;
        lens_mode = 0;
        temp_cluedo_mode = 0;
        S3DSetLineOfSightFunction(dummy_sound_line_of_sight);
        S3DSetDeadzoneRadius(1280);
        break;
    }
    player->view_mode = val;
}

void draw_overlay_compass(long base_x, long base_y)
{
    // Phase 4: drawn by ingame_panel_frame() (frontgui_ingame_panel.cpp)
    // over the ImGui minimap texture when the ImGui in-game HUD is active.
    // Only reached for the classic HUD (ingame_gui_use_classic_hud()).
    if (!ingame_gui_use_classic_hud())
        return;
    struct PlayerInfo* player = get_my_player();
    struct Camera* cam = get_local_active_camera(player);
    unsigned short flg_mem = RendererGetDrawFlags();
    long status_panel_width_local = render_overlay->get_status_panel_width();
    long map_diag = render_overlay->get_map_diagonal_length();
    render_overlay->set_winfont();
    RendererAddDrawFlags(Lb_SPRITE_TRANSPAR4);
    LbTextSetWindow(0, 0, MyScreenWidth, MyScreenHeight);
    int units_per_px = (16 * status_panel_width_local + 140 / 2) / 140;
    int tx_units_per_px = (22 * units_per_px) / LbTextLineHeight();
    int w = (LbSprFontCharWidth(lbFontPtr, '/') * tx_units_per_px / 16) / 2;
    int h = (LbSprFontCharHeight(lbFontPtr, '/') * tx_units_per_px / 16) / 2 + 2 * units_per_px / 16;
    int center_x = base_x * units_per_px / 16 + map_diag / 2;
    int center_y = base_y * units_per_px / 16 + map_diag / 2;
    int shift_x = (-(map_diag * 7 / 16) * LbSinL(cam->rotation_angle_x)) >> LbFPMath_TrigmBits;
    int shift_y = (-(map_diag * 7 / 16) * LbCosL(cam->rotation_angle_x)) >> LbFPMath_TrigmBits;
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
    if (smooth_on)
    {
        TbGraphicsWindow ewnd;
        store_engine_window(&ewnd, pixel_size);
        smooth_screen_area(SwTargetWScreen(), ewnd.x, ewnd.y,
            ewnd.width, ewnd.height, lbDisplay.GraphicsScreenWidth);
    }
    remove_explored_flags_for_power_sight(player);
    if ((kfx_sim_state.operation_flags & GOF_ShowGui) != 0) {
        render_overlay->draw_whole_status_panel();
    }
    render_overlay->draw_gui();
    if ((kfx_sim_state.operation_flags & GOF_ShowGui) != 0) {
        draw_overlay_compass(local_state.minimap_pos_x, local_state.minimap_pos_y);
    }
    render_overlay->message_draw();
    render_overlay->gui_draw_all_boxes();
    render_overlay->draw_tooltip();
    struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
    if (!creature_control_invalid(cctrl))
    {
        draw_creature_view_icons(thing);
        render_overlay->sync_cheat_box_3_active_option(cctrl->active_instance_id);
    }
}

/* Two-tap ghost-blend smoothing kernel: blend this pixel with its right
 * neighbour, then blend that result with the pixel below -- both taps are
 * the same render_ghost_blend() weighting (1/3 ref, 2/3 dest) the original
 * render_ghost[ref<<8|dest] table encoded. */
void smooth_screen_area(TbPixel *scrbuf, long x, long y, long w, long h, long scanln)
{
    SYNCDBG(7,"Starting");
    TbPixel* lnbuf = scrbuf + scanln * y + x;
    for (long i = h - y - 1; i > 0; i--)
    {
        TbPixel* buf = lnbuf;
        for (long k = w - x - 1; k > 0; k--)
        {
            TbPixel step1 = render_ghost_blend(buf[0], buf[1]);
            buf[0] = render_ghost_blend(buf[scanln], step1);
            buf++;
      }
      lnbuf += scanln;
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
    if (smooth_on)
    {
        store_engine_window(&ewnd,pixel_size);
        smooth_screen_area(SwTargetWScreen(), ewnd.x, ewnd.y,
            ewnd.width, ewnd.height, lbDisplay.GraphicsScreenWidth);
    }
    remove_explored_flags_for_power_sight(player);
    if ((kfx_sim_state.operation_flags & GOF_ShowGui) != 0) {
        render_overlay->draw_whole_status_panel();
    }
    render_overlay->draw_gui();
    if ((kfx_sim_state.operation_flags & GOF_ShowGui) != 0) {
        draw_overlay_compass(local_state.minimap_pos_x, local_state.minimap_pos_y);
    }
    render_overlay->message_draw();
    render_overlay->gui_draw_all_boxes();
    draw_power_hand();
    render_overlay->draw_tooltip();
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
        render_overlay->draw_whole_status_panel();
    }
    render_overlay->draw_gui();
    if (flag_is_set(kfx_sim_state.operation_flags,GOF_ShowGui)) {
        draw_overlay_compass(local_state.minimap_pos_x, local_state.minimap_pos_y);
    }
    render_overlay->message_draw();
    draw_power_hand();
    render_overlay->draw_tooltip();
    render_overlay->gui_draw_all_boxes();
}

int get_place_room_pointer_graphics(RoomKind rkind)
{
    struct RoomConfigStats* roomst = get_room_kind_stats(rkind);
    return roomst->pointer_sprite_idx;
}

int get_place_trap_pointer_graphics(ThingModel trmodel)
{
    struct TrapConfigStats* trapst = get_trap_model_stats(trmodel);
    return trapst->pointer_sprite_idx;
}

int get_place_door_pointer_graphics(ThingModel drmodel)
{
    struct DoorConfigStats* doorst = get_door_model_stats(drmodel);
    return doorst->pointer_sprite_idx;
}

/**
 * Draws a cursor for given spell.
 *
 * @return Gives true if cursor spell was drawn, false if the spell wasn't available and either no cursor or block cursor was drawn.
 */
TbBool draw_spell_cursor(ThingIndex tng_idx, MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{
    long i;
    long pwkind = -1;
    struct PlayerInfo* player = get_my_player();
    struct UserState* ustate = get_local_user_state();
    pwkind = ustate->chosen_power_kind;
    SYNCDBG(5,"Starting for power %d",(int)pwkind);
    if (pwkind <= 0)
    {
        set_pointer_graphic(MousePG_Invisible);
        return false;
    }

    struct Thing* thing = thing_get(tng_idx);
    TbBool allow_cast = false;
    const struct PowerConfigStats* powerst = get_power_model_stats(pwkind);
    allow_cast = can_cast_spell(player->id_number, pwkind, stl_x, stl_y, thing, CastChk_SkipThing);
    if (!allow_cast)
    {
        set_pointer_graphic(MousePG_DenyMark);
        return false;
    }
    Expand_Check_Func chkfunc = powermodel_expand_check_func_list[powerst->overcharge_check_idx];
    if (chkfunc != NULL)
    {
        if (chkfunc())
        {
            i = get_power_overcharge_level(player);
            set_pointer_graphic(MousePG_SpellCharge0+i);
            draw_spell_cost = compute_power_price(player->id_number, pwkind, i);

            // cheat mode. Everything is free. show charging level instead of none when cost is zero.
            if (draw_spell_cost == 0)
                draw_spell_cost = -(i+1);

            return true;
        }
    }
    i = get_player_colored_pointer_icon_idx(powerst->pointer_sprite_idx,my_player_number);
    set_pointer_graphic_spell(i, get_gameturn());
    return true;
}

void process_dungeon_top_pointer_graphic(struct PlayerInfo *player)
{
    struct Thing *thing;
    struct Dungeon* dungeon = get_dungeon(player->id_number);
    struct PlayerStateConfigStats* plrst_cfg_stat = get_player_state_stats(player->work_state);
    struct UserState* ustate = get_user_state(player->user_id);
    if (dungeon_invalid(dungeon))
    {
        set_pointer_graphic(MousePG_Invisible);
        return;
    }
    // During fade
    if (player->instance_num == PI_MapFadeFrom)
    {
        set_pointer_graphic(MousePG_Invisible);
        return;
    }
    // Mouse over panel map
    if (((kfx_sim_state.operation_flags & GOF_ShowGui) != 0) && sim_feedback->mouse_is_over_panel_map(local_state.minimap_pos_x, local_state.minimap_pos_y))
    {
        if (kfx_sim_state.small_map_state == 2) {
            set_pointer_graphic(MousePG_Invisible);
        } else {
            set_pointer_graphic(MousePG_Arrow);
        }
        return;
    }
    // Mouse over battle message box
    long battle_creature_over_local = render_overlay->get_battle_creature_over();
    if (battle_creature_over_local > 0)
    {
        PowerKind pwkind = ustate->chosen_power_kind;
        thing = thing_get(battle_creature_over_local);
        TRACE_THING(thing);
        if (can_cast_spell(player->id_number, pwkind, thing->mappos.x.stl.num, thing->mappos.y.stl.num, thing, CastChk_Default))
        {
            draw_spell_cursor(battle_creature_over_local, thing->mappos.x.stl.num, thing->mappos.y.stl.num);
        } else
        {
            set_pointer_graphic(MousePG_Arrow);
        }
        return;
    }
    // GUI action being processed
    if (render_overlay->game_is_busy_doing_gui())
    {
        set_pointer_graphic(MousePG_Arrow);
        return;
    }
    long i;
    short thing_under_hand;
    switch (plrst_cfg_stat->pointer_group)
    {
    case PsPg_CtrlDungeon:
        if (ustate->secondary_cursor_state)
          i = ustate->secondary_cursor_state;
        else
          i = ustate->primary_cursor_state;
        if ((player->instance_num == PI_Grab) || (player->instance_num == PI_Drop) || (player->instance_num == PI_Whip) || (player->instance_num == PI_WhipEnd) || (local_state.local_thing_under_hand > 0) || (!power_hand_is_empty(player) && (i != CSt_DoorKey))) {
            i = CSt_PowerHand;
        } else
        if ((i == CSt_PowerHand) && power_hand_is_empty(player))
        {
            i = CSt_DefaultArrow;
        }
        switch (i)
        {
        case CSt_PickAxe:
        {
            set_pointer_graphic((player->roomspace_highlight_mode == drag_placement_mode) ? MousePG_Pickaxe2 : MousePG_Pickaxe);
            break;
        }
        case CSt_DoorKey:
            set_pointer_graphic(MousePG_LockMark);
            break;
        case CSt_PowerHand:
            thing_under_hand = player->thing_under_hand;
            if (local_state.local_thing_under_hand > 0) {
                thing_under_hand = local_state.local_thing_under_hand;
            }
            thing = thing_get(thing_under_hand);
            TRACE_THING(thing);
            TbBool can_cast = false;
            if ((ustate->input_crtr_control) && (thing_exists(thing)) && (dungeon->things_in_hand[0] != thing_under_hand))
            {
                PowerKind pwkind = PwrK_POSSESS;
                if (can_cast_spell(player->id_number, pwkind, thing->mappos.x.stl.num, thing->mappos.y.stl.num, thing, CastChk_Default))
                {
                    // The condition above makes can_cast_spell() within draw_spell_cursor() to never fail; this is intentional
                    can_cast = true;
                }
                else
                {
                    thing = get_creature_near_for_controlling(player->id_number, thing->mappos.x.val, thing->mappos.y.val);
                    if (!thing_is_invalid(thing))
                    {
                        if (can_cast_spell(player->id_number, pwkind, thing->mappos.x.stl.num, thing->mappos.y.stl.num, thing, CastChk_Default))
                        {
                            can_cast = true;
                        }
                    }
                }
                if (can_cast)
                {
                    ustate->chosen_power_kind = pwkind;
                    draw_spell_cursor(0, thing->mappos.x.stl.num, thing->mappos.y.stl.num);
                    ustate->chosen_power_kind = 0;
                    player->thing_under_hand = thing->index;
                } else {
                    set_pointer_graphic(MousePG_Arrow);
                }

                local_state.display_needs_update = true;
            } else
            if (((ustate->input_crtr_query) && !thing_is_invalid(thing)) && (dungeon->things_in_hand[0] != thing_under_hand)
                && can_thing_be_queried(thing, player->id_number))
            {
                set_pointer_graphic(MousePG_Query);
                local_state.display_needs_update = true;
            } else
            {
                if ((ustate->additional_flags & UsrAF_ChosenSubTileIsHigh) != 0) {
                  set_pointer_graphic((player->roomspace_highlight_mode == drag_placement_mode) ? MousePG_Pickaxe2 : MousePG_Pickaxe);
                } else {
                  set_pointer_graphic(MousePG_Invisible);
                }
            }
            break;
        default:
            if (player->hand_busy_until_turn <= get_gameturn())
              set_pointer_graphic(MousePG_Arrow);
            else
              set_pointer_graphic(MousePG_Invisible);
            break;
        }
        break;
    case PsPg_BuildRoom:
        i = get_place_room_pointer_graphics(ustate->chosen_room_kind);
        set_pointer_graphic(i);
        break;
    case PsPg_Invisible:
        set_pointer_graphic(MousePG_Invisible);
        break;
    case PsPg_Spell:
        draw_spell_cursor(0, kfx_render_state.mouse_light_pos.x.stl.num, kfx_render_state.mouse_light_pos.y.stl.num);
        break;
    case PsPg_Query:
        set_pointer_graphic(MousePG_Query);
        break;
    case PsPg_PlaceTrap:
        i = get_place_trap_pointer_graphics(ustate->chosen_trap_kind);
        set_pointer_graphic(i);
        break;
    case PsPg_PlaceDoor:
        i = get_place_door_pointer_graphics(ustate->chosen_door_kind);
        set_pointer_graphic(i);
        break;
    case PsPg_Sell:
        set_pointer_graphic(MousePG_Sell);
        break;
    case PsPg_PlaceTerrain:
    {
        i = get_place_terrain_pointer_graphics(ustate->cheatselection.chosen_terrain_kind);
        set_pointer_graphic(i);
        break;
    }
    case PsPg_MkDigger:
        set_pointer_graphic(MousePG_MkDigger);
        break;
    case PsPg_MkCreatr:
        set_pointer_graphic(MousePG_MkCreature);
        break;
    case PsPg_OrderCreatr:
    {
        struct Thing* creatng = thing_get(player->controlled_thing_idx);
        i = (thing_is_creature(creatng)) ? MousePG_MvCreature : MousePG_Arrow;
        set_pointer_graphic(i);
        break;
    }
    case PsPg_None:
    default:
        set_pointer_graphic(MousePG_Arrow);
        break;
    }
}

void process_pointer_graphic(void)
{
    struct PlayerInfo* player = get_my_player();
    SYNCDBG(6,"Starting for view %d, player state %s, instance %d",(int)player->view_type,player_state_code_name(player->work_state),(int)player->instance_num);
    switch (get_local_view_type(player))
    {
    case PVT_DungeonTop:
        // This case is complicated
        process_dungeon_top_pointer_graphic(player);
        break;
    case PVT_CreatureContrl:
    case PVT_CreaturePasngr:
        if (render_overlay->cheat_or_menu_window_active())
          set_pointer_graphic(MousePG_Arrow);
        else
          set_pointer_graphic(MousePG_Invisible);
        break;
    case PVT_MapScreen:
    case PVT_MapFadeIn:
    case PVT_MapFadeOut:
        set_pointer_graphic(MousePG_Arrow);
        break;
    case PVT_None:
        set_pointer_graphic_none();
        break;
    default:
        WARNLOG("Unsupported view type");
        set_pointer_graphic_none();
        break;
    }
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
        render_overlay->set_parchment_loaded(0);
        break;
    case PVM_IsoWibbleView:
    case PVM_IsoStraightView:
        redraw_isometric_view();
        render_overlay->set_parchment_loaded(0);
        break;
    case PVM_ParchmentView:
        render_overlay->redraw_parchment_view();
        break;
    case PVM_FrontView:
        redraw_frontview();
        render_overlay->set_parchment_loaded(0);
        break;
    case PVM_ParchFadeIn:
        render_overlay->set_parchment_loaded(0);
        local_state.palette_fade_step_map = map_fade_in(local_state.palette_fade_step_map);
        break;
    case PVM_ParchFadeOut:
        render_overlay->set_parchment_loaded(0);
        local_state.palette_fade_step_map = map_fade_out(local_state.palette_fade_step_map);
        break;
    default:
        ERRORLOG("Unsupported drawing state, %d",(int)player->view_mode);
        break;
    }
    //LbTextSetWindow(0, 0, MyScreenWidth, MyScreenHeight);
    render_overlay->set_winfont();
    RendererClearDrawFlags(Lb_TEXT_ONE_COLOR);
    int tx_units_per_px = ( (MyScreenHeight < 400) && (dbc_initialized && dbc_enabled) ) ? scale_ui_value(32) : (22 * units_per_pixel) / LbTextLineHeight();
    LbTextSetWindow(0, 0, MyScreenWidth, MyScreenHeight);
    // Phase 3: the MP chat input line moves to ingame_text_overlays_frame()
    // under the ImGui HUD (input handling -- get_players_message_inputs() --
    // is unchanged; only this echo of player->mp_message_text moves).
    if (((get_local_user_state()->init_flags & UsrIF_NewMPMessage) != 0) && ingame_gui_use_classic_hud())
    {
        char text[sizeof(player->mp_message_text) + 4];
        snprintf(text, sizeof(text), ">%s_", player->mp_message_text);
        long pos_x = 148*units_per_pixel/16;
        long pos_y = 8*units_per_pixel/16;
        if (kfx_sim_state.armageddon_cast_turn != 0)
        {
            if (render_overlay->bonus_script_or_variable_overlay_active())
            {
                pos_y = ((pos_y << 3) + ((LbTextLineHeight()*units_per_pixel/16) * (kfx_sim_state.active_messages_count << (MyScreenHeight < 400))));
            }
        }
        LbTextDrawResized(pos_x, pos_y, tx_units_per_px, text);
    }
    if ( draw_spell_cost )
    {
        unsigned short drwflags_mem = RendererGetDrawFlags();
        LbTextSetWindow(0, 0, MyScreenWidth, MyScreenHeight);
        RendererSetDrawFlags(0);
        render_overlay->set_winfont();
        char text[16];
        if (draw_spell_cost > 0)
            snprintf(text, sizeof(text), "%ld", draw_spell_cost);
	else
            snprintf(text, sizeof(text), "lv%ld", (-draw_spell_cost));
        long pos_y = sim_feedback->GetMouseY() - (LbTextStringHeight(text) * units_per_pixel / 16) / 2 - 2 * units_per_pixel / 16;
        long pos_x = sim_feedback->GetMouseX() - (LbTextStringWidth(text) * units_per_pixel / 16) / 2;
        LbTextDrawResized(pos_x, pos_y, tx_units_per_px, text);
        RendererSetDrawFlags(drwflags_mem);
        draw_spell_cost = 0;
    }
    render_overlay->draw_debug_overlays();

    // Phase 3 (docs/refactor/ingame-gui/04-messages-tooltips-infobox.md):
    // with the ImGui HUD on, ingame_text_overlays_frame()
    // (frontgui_ingame_text.cpp) draws the "Paused" caption instead, from
    // the FrontendImGuiFrame submission -- same GOF_Paused/WorldInfluence/
    // unpausing gate.
    if (ingame_gui_use_classic_hud()
     && ((kfx_sim_state.operation_flags & GOF_Paused) != 0) && ((kfx_sim_state.operation_flags & GOF_WorldInfluence) == 0) && !render_overlay->get_unpausing_in_progress())
    {
          render_overlay->set_winfont();
          const char * text = get_string(GUIStr_PausedMsg);
          long w = (LbTextStringWidth(text) * units_per_pixel / 16 + 2 * (LbTextCharWidth(' ') * units_per_pixel / 16));
          long pos_x;
          struct Camera *camera = get_local_active_camera(player);
          if (camera->view_mode == PVM_IsoWibbleView || camera->view_mode == PVM_FrontView || camera->view_mode == PVM_IsoStraightView || camera->view_mode == PVM_CreatureView) {
              pos_x = local_state.engine_window_x + (MyScreenWidth - w - local_state.engine_window_x) / 2;
          } else {
              pos_x = (MyScreenWidth-w)/2;
          }
          long pos_y = 16 * units_per_pixel / 16;
          RendererSetDrawFlags(Lb_TEXT_HALIGN_CENTER);
          long h = LbTextLineHeight() * units_per_pixel / 16;
          int text_w = w;
          int text_x = pos_x;
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
          render_overlay->draw_slab64k(pos_x, pos_y, units_per_pixel, w, h);
          LbTextDrawResized(0/pixel_size, 0/pixel_size, tx_units_per_px, text);
          LbTextSetWindow(0/pixel_size, 0/pixel_size, MyScreenWidth/pixel_size, MyScreenHeight/pixel_size);
    }
    if (kfx_sim_state.armageddon_cast_turn != 0)
    {
        int i = 0;
        if (kfx_sim_state.armageddon_cast_turn + kfx_config_state.conf.rules[kfx_sim_state.armageddon_caster_idx].magic.armageddon_count_down <= get_gameturn())
        {
            if (kfx_sim_state.armageddon_over_turn - kfx_config_state.conf.rules[kfx_sim_state.armageddon_caster_idx].magic.armageddon_duration <= get_gameturn())
                i = kfx_sim_state.armageddon_over_turn - get_gameturn();
        } else
        {
            i = get_gameturn() - kfx_sim_state.armageddon_cast_turn - kfx_config_state.conf.rules[kfx_sim_state.armageddon_caster_idx].magic.armageddon_count_down;
        }
        render_overlay->set_winfont();
        char text[64];
        snprintf(text, sizeof(text), " %s %03d", get_string(get_power_name_strindex(PwrK_ARMAGEDDON)), i/2); // Armageddon message
        i = LbTextCharWidth(' ')*units_per_pixel/16;
        long w = LbTextStringWidth(text) * units_per_pixel / 16 + 6 * i;
        i = LbTextLineHeight()*units_per_pixel/16;
        RendererSetDrawFlags(Lb_TEXT_HALIGN_CENTER);
        long h = pixel_size * i + pixel_size * i / 2;
        if (MyScreenHeight < 400)
        {
            w *= 2;
            h *= 2;
        }
        long pos_x = MyScreenWidth - w - 16 * units_per_pixel / 16;
        long pos_y = 16 * units_per_pixel / 16;
        LbTextSetWindow(pos_x, pos_y, w, h);
        render_overlay->draw_slab64k(pos_x, pos_y, units_per_pixel, w, h);
        LbTextDrawResized(0/pixel_size, 0/pixel_size, tx_units_per_px, text);
        LbTextSetWindow(0/pixel_size, 0/pixel_size, MyScreenWidth/pixel_size, MyScreenHeight/pixel_size);
    }
    render_overlay->draw_eastegg();
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

int get_place_terrain_pointer_graphics(SlabKind skind)
{
    int result;
    switch (skind)
    {
        case SlbT_ROCK:
        {
            result = MousePG_PlaceImpRock;
            break;
        }
        case SlbT_GOLD:
        {
            result = MousePG_PlaceGold;
            break;
        }
        case SlbT_EARTH:
        case SlbT_TORCHDIRT:
        {
            result = MousePG_PlaceEarth;
            break;
        }
        case SlbT_WALLDRAPE:
        case SlbT_WALLTORCH:
        case SlbT_WALLWTWINS:
        case SlbT_WALLWWOMAN:
        case SlbT_WALLPAIRSHR:
        case SlbT_DAMAGEDWALL:
        {
            result = MousePG_PlaceWall;
            break;
        }
        case SlbT_PATH:
        {
            result = MousePG_PlacePath;
            break;
        }
        case SlbT_CLAIMED:
        {
            result = MousePG_PlaceClaimed;
            break;
        }
        case SlbT_LAVA:
        {
            result = MousePG_PlaceLava;
            break;
        }
        case SlbT_WATER:
        {
            result = MousePG_PlaceWater;
            break;
        }
        case SlbT_GEMS:
        {
            result = MousePG_PlaceGems;
            break;
        }
        default:
        {
            result = MousePG_Arrow;
            break;
        }
    }
    return result;
}

/** Returns if cursor for local player is at top of the dungeon in 3D view.
 *  Cursor placed at top of dungeon is marked by green/red "volume box";
 *   if there's no volume box, cursor should be of the field behind it
 *   (the exact field in a line of view through cursor). If cursor is at top
 *   of view, then pointed map field is a bit lower than the line of view
 *   through cursor.
 *
 *  This function reverse-engineers the decisions made by
 *  get_player_coords_and_context() (front_input.c).
 */
TbBool players_cursor_is_at_top_of_view(void)
{
    const struct PlayerInfo *const player = get_my_player();
    const struct UserState *const ustate = get_local_user_state();
    switch (player->work_state)
    {
    case PSt_BuildRoom:
    case PSt_PlaceDoor:
    case PSt_PlaceTrap:
    case PSt_SightOfEvil:
    case PSt_Sell:
    case PSt_PlaceTerrain:
    case PSt_MkDigger:
        return true;

    case PSt_OrderCreatr:
        return (player->controlled_thing_idx > 0);

    case PSt_CtrlDungeon:
        switch (ustate->primary_cursor_state)
        {
            case CSt_DefaultArrow:
                return false;

            case CSt_PickAxe:
            case CSt_DoorKey:
                return true;

            case CSt_PowerHand:
                return (local_state.local_thing_under_hand == 0)
                    || (! power_hand_is_empty(player));
        }
    }
    return false;
}

TbBool engine_point_to_map(struct Camera *camera, long screen_x, long screen_y, int32_t *map_x, int32_t *map_y)
{
    *map_x = 0;
    *map_y = 0;
    if ( (kfx_render_state.pointer_x >= 0) && (kfx_render_state.pointer_y >= 0)
      && (kfx_render_state.pointer_x < (local_state.engine_window_width/pixel_size))
      && (kfx_render_state.pointer_y < (local_state.engine_window_height/pixel_size)) )
    {
        if ( players_cursor_is_at_top_of_view() )
        {
              *map_x = subtile_coord(kfx_render_state.top_pointed_at_x,kfx_render_state.top_pointed_at_frac_x);
              *map_y = subtile_coord(kfx_render_state.top_pointed_at_y,kfx_render_state.top_pointed_at_frac_y);
        } else
        {
              *map_x = subtile_coord(kfx_render_state.block_pointed_at_x,kfx_render_state.pointed_at_frac_x);
              *map_y = subtile_coord(kfx_render_state.block_pointed_at_y,kfx_render_state.pointed_at_frac_y);
        }
        // Clipping coordinates
        if (*map_y < 0)
          *map_y = 0;
        else
        if (*map_y > subtile_coord(kfx_sim_state.map_subtiles_y,-1))
          *map_y = subtile_coord(kfx_sim_state.map_subtiles_y,-1);
        if (*map_x < 0)
          *map_x = 0;
        else
        if (*map_x > subtile_coord(kfx_sim_state.map_subtiles_x,-1))
          *map_x = subtile_coord(kfx_sim_state.map_subtiles_x,-1);
        return true;
    }
    return false;
}

TbBool screen_to_map(struct Camera *camera, int32_t screen_x, int32_t screen_y, struct Coord3d *mappos)
{
    TbBool result;
    int32_t x;
    int32_t y;
    SYNCDBG(19,"Starting");
    result = false;
    if (camera != NULL)
    {
      switch (camera->view_mode)
      {
        case PVM_CreatureView:
        case PVM_IsoWibbleView:
        case PVM_FrontView:
        case PVM_IsoStraightView:
          // 3D view mode
          result = engine_point_to_map(camera,screen_x,screen_y,&x,&y);
          break;
        case PVM_ParchmentView: //map mode
          result = render_overlay->point_to_overhead_map(camera,screen_x/pixel_size,screen_y/pixel_size,&x,&y);
          break;
        default:
          result = false;
          break;
      }
    }
    if ( result )
    {
      mappos->x.val = x;
      mappos->y.val = y;
    }
    if ( mappos->x.val > ((kfx_sim_state.map_subtiles_x<<8)-1) )
      mappos->x.val = ((kfx_sim_state.map_subtiles_x<<8)-1);
    if ( mappos->y.val > ((kfx_sim_state.map_subtiles_y<<8)-1) )
      mappos->y.val = ((kfx_sim_state.map_subtiles_y<<8)-1);
    SYNCDBG(19,"Finished");
    return result;
}

static void set_mouse_light(NetUserId user, TbBool valid, struct Coord3d pos)
{
    const int idx = get_user_state(user)->cursor_light_idx;
    if (idx == 0)
        return;

    if (valid)
    {
        pos.z.val = get_floor_height_at(&pos);
        light_turn_light_on(idx);
        light_set_light_position(idx, &pos);

        if (user == get_local_user())
            kfx_render_state.mouse_light_pos = pos;
    }
    else
    {
        light_turn_light_off(idx);
    }
}

void update_local_mouse_light(void)
{
    SYNCDBG(6,"Starting");
    struct PlayerInfo *player = get_my_player();

    // Avoid glitching during level intro or possess animation
    if (player->instance_num != PI_Unset)
        return;
    // ... or when watching a replay
    if (sim_feedback->get_packet_load_enable())
        return;
    // ... or during text input (save menu)
    if (render_overlay->game_is_busy_doing_gui_string_input())
        return;

    struct Camera *cam = get_local_active_camera(player);
    struct Coord3d pos;
    const TbBool valid = screen_to_map(cam, sim_feedback->GetMouseX(), sim_feedback->GetMouseY(), &pos);

    set_mouse_light(player->user_id, valid, pos);

    int cursor_light_idx = get_player_user_state(player)->cursor_light_idx;
    if (cursor_light_idx != 0)
        light_reset_interpolation(cursor_light_idx);
}

void update_mouse_light(NetUserId user)
{
    SYNCDBG(6,"Starting");
    const struct Packet *pckt = NULL;

    if (user == get_local_user())
        pckt = sim_feedback->get_history_packet(user, get_gameturn());
    if (pckt == NULL)
        pckt = get_packet(user);

    const TbBool valid = (pckt->control_flags & PCtr_MapCoordsValid) != 0;
    struct Coord3d pos;
    pos.x.val = pckt->pos_x;
    pos.y.val = pckt->pos_y;
    set_mouse_light(user, valid, pos);
}

/******************************************************************************/
