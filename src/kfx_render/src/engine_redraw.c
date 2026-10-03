/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_redraw.c
 *     The engine window and screen-to-map helpers.
 * @par Purpose:
 *     Engine window setup, the map fade blend, screen-to-map conversion and
 *     the mouse light. Composing the frame (the view redraws, the UI layers,
 *     the pointer) is kfx_frontend's frame_compose.c/pointer_graphics.c
 *     since refactor pass 2 (S13).
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
#include "ports/net_port.h"
#include "local_state.h"
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
/******************************************************************************/
int64_t xtab[640][2];
int64_t ytab[480][2];

void setup_engine_window(int64_t x, int64_t y, int64_t width, int64_t height)
{
    SYNCDBG(6,"Starting for size (%" PRId64 ",%" PRId64 ") at (%" PRId64 ",%" PRId64 ")",(int64_t)(width),(int64_t)(height),(int64_t)(x),(int64_t)(y));
    int64_t status_panel_width_local = ui_get_status_panel_width();
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

void store_engine_window(TbGraphicsWindow *ewnd,int64_t divider)
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

/* fade_tbl/ghost_tbl (the palette-index render_fade_tables/map_fade_ghost_table
 * lookups the original used) are retired now that both buffers hold real
 * TbPixel colours instead of palette indices: shading a captured snapshot no
 * longer needs a palette-index table lookup (render_shade() computes it
 * directly on the sample), and combining the two shaded snapshots was
 * always a straight additive RGB sum (docs/refactor/renderer/
 * 02a-pixel-format-design.md §2.5's generate_map_fade_ghost_table finding:
 * `output = colour1 + colour2`, clamped) rather than a genuine 1/3-2/3 ghost
 * blend -- so it needs clamp(), not render_ghost_blend(). */
void map_fade(TbPixel *outbuf, TbPixel *srcbuf1, TbPixel *srcbuf2, int64_t a6, int64_t const xmax, int64_t const ymax, int64_t a9)
{
    int64_t ix;
    int64_t iy;
    int64_t x1base = 4 * a6;
    int64_t x0base = 4 * (32 - a6);
    int64_t * xt = xtab[0];
    int64_t vx0 = 0;
    int64_t vx1 = 0;
    for (ix = xmax; ix > 0; ix--)
    {
        int64_t val = x1base + vx1 / xmax;
        int64_t m;
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

    int64_t y1base = 8 * ymax / xmax * x1base / 8;
    int64_t y0base = 8 * ymax / xmax * x0base / 8;
    int64_t * yt = ytab[0];
    int64_t vy1 = 0;
    int64_t vy0 = 0;
    for (iy = ymax; iy > 0; iy--)
    {
        int64_t val = y1base + vy1 / ymax;
        int64_t m;
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

    const int64_t shade1 = a6;
    const int64_t shade2 = 32 - a6;
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
                (uint8_t)clamp((int64_t)px1.r + px2.r, 0, 255),
                (uint8_t)clamp((int64_t)px1.g + px2.g, 0, 255),
                (uint8_t)clamp((int64_t)px1.b + px2.b, 0, 255),
                255);
            out++;
            xt += 2;
        }
        out += a9 - xmax;
        yt += 2;
    }
}

int64_t dummy_sound_line_of_sight(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6)
{
    return 1;
}

void set_engine_view(struct PlayerInfo *player, int64_t val)
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
        ERRORLOG("Invalid view mode %" PRId64,(int64_t)val);
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

/* Two-tap ghost-blend smoothing kernel: blend this pixel with its right
 * neighbour, then blend that result with the pixel below -- both taps are
 * the same render_ghost_blend() weighting (1/3 ref, 2/3 dest) the original
 * render_ghost[ref<<8|dest] table encoded. */
void smooth_screen_area(TbPixel *scrbuf, int64_t x, int64_t y, int64_t w, int64_t h, int64_t scanln)
{
    SYNCDBG(7,"Starting");
    TbPixel* lnbuf = scrbuf + scanln * y + x;
    for (int64_t i = h - y - 1; i > 0; i--)
    {
        TbPixel* buf = lnbuf;
        for (int64_t k = w - x - 1; k > 0; k--)
        {
            TbPixel step1 = render_ghost_blend(buf[0], buf[1]);
            buf[0] = render_ghost_blend(buf[scanln], step1);
            buf++;
      }
      lnbuf += scanln;
    }
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

TbBool engine_point_to_map(struct Camera *camera, int64_t screen_x, int64_t screen_y, int64_t *map_x, int64_t *map_y)
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

TbBool screen_to_map(struct Camera *camera, int64_t screen_x, int64_t screen_y, struct Coord3d *mappos)
{
    TbBool result;
    int64_t x;
    int64_t y;
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
          result = ui_point_to_overhead_map(camera,screen_x/pixel_size,screen_y/pixel_size,&x,&y);
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
    const int64_t idx = get_user_state(user)->cursor_light_idx;
    if (idx == 0)
        return;

    if (valid)
    {
        pos.z.val = get_floor_height_at(&pos);
        light_turn_light_on(idx);
        light_set_light_position(idx, &pos);
    }
    else
    {
        light_turn_light_off(idx);
    }
}

/**
 * Where this frame's mouse points: the local user's cursor light is drawn there (light_data.c), and the spell cursor.
 * Nothing of the light registry changes: the cursor light moves in it from the user's packets
 * (update_mouse_light()), as the other users' (refactor pass 5, S11: this moved and switched it every frame, and
 * reset its interpolation, in simulation state).
 */
void update_local_mouse_light(void)
{
    SYNCDBG(6,"Starting");
    struct PlayerInfo *player = get_my_player();
    kfx_render_state.local_cursor_valid = false;

    // Avoid glitching during level intro or possess animation
    if (player->instance_num != PI_Unset)
        return;
    // ... or when watching a replay
    if (replay.load_enable)
        return;
    // ... or during text input (save menu)
    if (ui_game_is_busy_doing_gui_string_input())
        return;

    struct Camera *cam = get_local_active_camera(player);
    struct Coord3d pos;
    if (screen_to_map(cam, GetMouseX(), GetMouseY(), &pos))
    {
        pos.z.val = get_floor_height_at(&pos);
        kfx_render_state.mouse_light_pos = pos;
        kfx_render_state.local_cursor_valid = true;
    }
}

void update_mouse_light(NetUserId user)
{
    SYNCDBG(6,"Starting");
    const struct Packet *pckt = NULL;

    if (user == get_local_user())
        pckt = netport_get_history_packet(user, get_gameturn());
    if (pckt == NULL)
        pckt = get_packet(user);

    const TbBool valid = (pckt->control_flags & PCtr_MapCoordsValid) != 0;
    struct Coord3d pos;
    pos.x.val = pckt->pos_x;
    pos.y.val = pckt->pos_y;
    set_mouse_light(user, valid, pos);
}

/******************************************************************************/
