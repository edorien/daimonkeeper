/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontmenu_ingame_map.c
 *     Map on in-game GUI panel drawing and support functions.
 * @par Purpose:
 *     On-screen drawing of the map area in GUI panel.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     20 Jan 2009 - 23 Nov 2012
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "frontmenu_ingame_map.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_video.h"
#include "bflib_sprite.h"
#include "bflib_vidraw.h"
#include "bflib_sprfnt.h"
#include "bflib_math.h"
#include "bflib_guibtns.h"
#include "bflib_mouse.h"
#include "bflib_planar.h"

#include "frontend.h"
#include "front_input.h"
#include "player_data.h"
#include "packet_data.h"
#include "game_legacy.h"
#include "creature_states.h"
#include "creature_states_hero.h"
#include "creature_battle.h"
#include "config_creature.h"
#include "config_terrain.h"
#include "power_hand.h"
#include "kjm_input.h"
#include "frontmenu_ingame_tabs.h"
#include "thing_doors.h"
#include "vidmode.h"
#include "vidfade.h"
#include "player_instances.h"
#include "engine_redraw.h"
#include "engine_render.h"
#include "gui_draw.h"
#include "kfx_config_state.h"
#include "local_camera.h"

#include <math.h>
#include "post_inc.h"

// Local constants
#define ANGLE_MASK_4 8188    // Angle mask rounded to multiples of 4 (0x1FFC)
#define MAP_ARROW_DISTANCE -1536    // Distance/scale factor for map arrow drawing

/******************************************************************************/
enum PanelColourIds
{
    PnC_Wall          = 1,
    PnC_Rock          = 2,
    PnC_Unexplored    = 3,
    PnC_Tagged_Gold   = 4,
    PnC_Gold          = 5,
    PnC_Lava          = 6,
    PnC_Water         = 7,
    PnC_purplePath    = 8,
    PnC_RockFloor     = 9,
    PnC_Tagged_Gems   = 10,
    PnC_Gems          = 11,
    //12-255 left free for future use
    PnC_Abyss         = 12,
    PnC_RoomsStart    = 256,  //rooms 256-2559  (9*256 entries) TERRAIN_ITEMS_MAX
    PnC_DoorsStart    = 2560, //doors 2560-38559 (9*2000*2 entries) TRAPDOOR_TYPES_MAX
    PnC_DoorsStartLocked  = 2569,
    PnC_PathStart     = 38560,  // path (9 entries)
    PnC_End           = 38569,
};

enum TbPixelsColours
{
    Tbp_OpenDoor   = 60,
    Tbp_LockedDoor = 79,
    Tbp_DoorHighlighted = 31

};
/**************************/
/**
 * Background behind the map area.
 */
static unsigned char *MapBackground = NULL;
static int64_t *MapShapeStart = NULL;
static int64_t *MapShapeEnd = NULL;

static int64_t PanelMapY;
static int64_t PanelMapX;
static int64_t NumBackColours;
static int64_t PrevPixelSize;
static unsigned char MapBackColours[256];
// PanelColours[] holds PnC_End colour slots per distinct captured
// background colour class; setup_background() must never let num_colours
// exceed this or it overflows the array below.
#define PANEL_MAP_BACKGROUND_COLOURS_MAX 16
static TbPixel PanelColours[PANEL_MAP_BACKGROUND_COLOURS_MAX*PnC_End];
static int64_t PrevRoomHighlight;
static int64_t PrevDoorHighlight;
static int64_t PanelMap[MAX_SUBTILES_X*MAX_SUBTILES_Y];

int64_t clicked_on_small_map;
unsigned char grabbed_small_map;
int64_t MapDiagonalLength = 0;



/******************************************************************************/

void panel_map_draw_pixel(RealScreenCoord x, RealScreenCoord y, TbPixel col)
{
    if ((y >= 0) && (y < MapDiagonalLength))
    {
        if ((x >= MapShapeStart[y]) && (x < MapShapeEnd[y]))
        {
            RendererGetFramebuffer()[(PanelMapY + y) * lbDisplay.GraphicsScreenWidth + (PanelMapX + x)] = col;
        }
    }
}

/**
 * Draws single call to arms overlay on minimap.
 * @param owner
 * @param x1
 * @param y1
 * @param x2
 * @param y2
 * @param zoom
 */
void draw_call_to_arms_circle(unsigned char owner, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t zoom)
{
    const struct PowerConfigStats *powerst;
    powerst = get_power_model_stats(PwrK_CALL2ARMS);
    struct Dungeon *dungeon;
    dungeon = get_players_num_dungeon(owner);
    int64_t units_per_px;
    units_per_px = (16*status_panel_width + 140/2) / 140;
    TbPixel col;
    col = player_room_colours[get_player_color_idx(owner)];
    int64_t i;
    i = 2*(PANEL_MAP_RADIUS*units_per_px/16) / 2;
    int64_t center_x;
    int64_t center_y;
    center_x = i + x2;
    center_y = i + y2;
    long long cscale;
    double circle_time;
    if ((kfx_sim_state.operation_flags & GOF_Paused) == 0) {
        circle_time = ((get_gameturn() + owner) & 7) + kfx_net_state.process_turn_time;
    } else {
        circle_time = ((get_gameturn() + owner) & 7);
    }
    cscale = circle_time * powerst->strength[dungeon->cta_power_level];
    int64_t dxq1;
    int64_t dyq1;
    int64_t dxq2;
    int64_t dyq2;
    int64_t dxq3;
    int64_t dyq3;
    int64_t dxq4;
    int64_t dyq4;

    int64_t sx;
    int64_t sy;
    int64_t base_y;
    base_y = ((cscale >> 3) << 8) / zoom;
    if ( base_y > 1 )
    {
      sy = base_y;
      i = 3 - 2 * base_y;
      for (sx=0; sx < sy; sx++)
      {
          dxq1 = center_x - sx;
          dyq1 = center_y - sy;
          panel_map_draw_pixel(x1 + dxq1, y1 + dyq1, col);
          dxq2 = center_x + sx;
          panel_map_draw_pixel(x1 + dxq2, y1 + dyq1, col);
          dyq2 = sy + center_y;
          panel_map_draw_pixel(x1 + dxq1, y1 + dyq2, col);
          panel_map_draw_pixel(x1 + dxq2, y1 + dyq2, col);
          dxq3 = center_x - sy;
          dyq3 = center_y - sx;
          panel_map_draw_pixel(x1 + dxq3, y1 + dyq3, col);
          dxq4 = center_x + sy;
          panel_map_draw_pixel(x1 + dxq4, y1 + dyq3, col);
          dyq4 = sx + center_y;
          panel_map_draw_pixel(x1 + dxq3, y1 + dyq4, col);
          panel_map_draw_pixel(x1 + dxq4, y1 + dyq4, col);
          if (i >= 0)
          {
              i += 4 * (sx - sy) + 10*units_per_px/16;
              sy--;
          } else
          {
              i += 4 * (sx - 1) + 10*units_per_px/16;
          }
      }

      if (sy == sx)
      {
        dxq1 = center_x - sx;
        dyq1 = center_y - sy;
        panel_map_draw_pixel(x1 + dxq1, y1 + dyq1, col);
        dxq2 = center_x + sx;
        panel_map_draw_pixel(x1 + dxq2, y1 + dyq1, col);
        dyq2 = sy + center_y;
        panel_map_draw_pixel(x1 + dxq1, y1 + dyq2, col);
        panel_map_draw_pixel(x1 + dxq2, y1 + dyq2, col);
        dxq3 = center_x - sy;
        dyq3 = center_y - sx;
        panel_map_draw_pixel(x1 + dxq3, y1 + dyq3, col);
        dxq4 = center_x + sy;
        panel_map_draw_pixel(x1 + dxq4, y1 + dyq3, col);
        dyq4 = sx + center_y;
        panel_map_draw_pixel(x1 + dxq3, y1 + dyq4, col);
        panel_map_draw_pixel(dxq4 + x1, dyq4 + y1, col);
      }
    }
}

static void map_to_minimap(MapCoord* x, MapCoord* y, const struct Camera *cam, int64_t zoom)
{
    const MapCoordDelta tmp_x = (*x - (MapCoordDelta)cam->mappos.x.val) / zoom;
    const MapCoordDelta tmp_y = (*y - (MapCoordDelta)cam->mappos.y.val) / zoom;
    const int64_t cos = LbCosL(cam->rotation_angle_x & ANGLE_MASK_4);
    const int64_t sin = LbSinL(cam->rotation_angle_x & ANGLE_MASK_4);
    *x = (tmp_x * cos + tmp_y * sin) >> 16;
    *y = (tmp_y * cos - tmp_x * sin) >> 16;
}

static struct Coord2d thing_minimap_position(struct Thing* thing, const struct Camera *cam, int64_t zoom)
{
    struct Coord2d result;
    const struct ThingInterpolateResult interp = interpolate_thing(thing);
    MapCoord x = interp.mappos.x.val;
    MapCoord y = interp.mappos.y.val;
    map_to_minimap(&x, &y, cam, zoom);
    result.x.val = x;
    result.y.val = y;
    return result;
}

/**
 * Draws all call to arms objects on minimap.
 * @param player The player for whom drawing occurs.
 * @param zoom Zoom level of the minimap.
 * @return Amount of objects drawn.
 */
int64_t draw_overlay_call_to_arms(struct PlayerInfo *player, int64_t units_per_px, int64_t zoom)
{
    uint64_t k;
    int64_t i;
    int64_t n;
    SYNCDBG(18,"Starting");
    struct Camera *cam = get_local_active_camera(player);
    if (cam == NULL)
        return 0;
    n = 0;
    const struct StructureList *slist = get_list_for_thing_class(TCls_Object);
    k = 0;
    i = slist->index;
    while (i != 0)
    {
        struct Thing *thing = thing_get(i);
        if (thing_is_invalid(thing))
        {
            ERRORLOG("Jump to invalid thing detected");
            break;
        }
        i = thing->next_of_class;
        // Per-thing code
        if (!thing_is_picked_up(thing))
        {
            if (thing->model == ObjMdl_CTAEnsign)//TODO CONFIG object model dependency, move to config
            {
                const struct Coord2d pos = thing_minimap_position(thing, cam, zoom);
                draw_call_to_arms_circle(thing->owner, 0, 0, pos.x.val, pos.y.val, zoom);
                n++;
            }
        }
        // Per-thing code ends
        k++;
        if (k > slist->count)
        {
            ERRORLOG("Infinite loop detected when sweeping things list");
            break;
        }
    }
    return n;
}

/**
 * Draws all owned traps on minimap.
 * @param player The player for whom drawing occurs.
 * @param zoom Scale between map coordinates and minimap pixels.
 * @return Amount of traps drawn.
 */
int64_t draw_overlay_traps(struct PlayerInfo *player, int64_t units_per_px, int64_t scaled_zoom, int64_t basic_zoom)
{
    uint64_t k;
    int64_t i;
    int64_t n;
    SYNCDBG(18,"Starting");
    struct Camera *cam = get_local_active_camera(player);
    if (cam == NULL)
        return 0;
    n = 0;
    k = 0;
    const struct StructureList *slist = get_list_for_thing_class(TCls_Trap);
    i = slist->index;
    while (i != 0)
    {
        struct Thing *thing = thing_get(i);
        if (thing_is_invalid(thing))
        {
            ERRORLOG("Jump to invalid thing detected");
            break;
        }
        i = thing->next_of_class;
        // Per-thing code
        if (player->id_number == thing->owner)
        {
            const struct Coord2d pos = thing_minimap_position(thing, cam, scaled_zoom);
            RealScreenCoord basepos;
            basepos = MapDiagonalLength/2;
            // Do the drawing
            if ((thing->trap.revealed) || (player->id_number == thing->owner))
            {
                TbPixel col;
                if ((thing->model == gui_trap_type_highlighted) && ((get_gameturn() % (2 * kfx_config_state.gui_blink_rate)) >= kfx_config_state.gui_blink_rate)) {
                    col = player_highlight_colours[thing->owner];
                } else {
                    col = resolve_indexed_pixel(60, RendererGetActivePalette());
                }
                int64_t pixels_amount = scale_pixel(basic_zoom*2);
                int64_t pixel_end = get_pixels_scaled_and_zoomed(basic_zoom*2);
                for (int64_t p = 0; p < pixel_end; p++)
                {
                    // Draw a cross
                    panel_map_draw_pixel(pos.x.val + basepos + draw_square[p].delta_x,
                                         pos.y.val + basepos + draw_square[p].delta_y,
                                         col);
                    panel_map_draw_pixel(pos.x.val + basepos + draw_square[p].delta_x + pixels_amount,
                                         pos.y.val + basepos + draw_square[p].delta_y,
                                         col);
                    panel_map_draw_pixel(pos.x.val + basepos + draw_square[p].delta_x - pixels_amount,
                                         pos.y.val + basepos + draw_square[p].delta_y,
                                         col);
                    panel_map_draw_pixel(pos.x.val + basepos + draw_square[p].delta_x,
                                         pos.y.val + basepos + draw_square[p].delta_y + pixels_amount,
                                         col);
                    panel_map_draw_pixel(pos.x.val + basepos + draw_square[p].delta_x,
                                         pos.y.val + basepos + draw_square[p].delta_y - pixels_amount,
                                         col);
                }
                n++;
            }
        }
        // Per-thing code ends
        k++;
        if (k > slist->count)
        {
            ERRORLOG("Infinite loop detected when sweeping things list");
            break;
        }
    }
    return n;
}

/**
 * Draws all spells and specials on minimap.
 * @param player The player for whom drawing occurs.
 * @param zoom Zoom level of the minimap.
 * @return Amount of objects drawn.
 */
int64_t draw_overlay_spells_and_boxes(struct PlayerInfo *player, int64_t units_per_px, int64_t scaled_zoom, int64_t basic_zoom)
{
    uint64_t k;
    int64_t i;
    int64_t n;
    SYNCDBG(18,"Starting");
    struct Camera *cam = get_local_active_camera(player);
    if (cam == NULL)
        return 0;
    n = 0;
    const struct StructureList *slist = get_list_for_thing_class(TCls_Object);
    k = 0;
    i = slist->index;
    while (i != 0)
    {
        struct Thing *thing = thing_get(i);
        if (thing_is_invalid(thing))
        {
            ERRORLOG("Jump to invalid thing detected");
            break;
        }
        i = thing->next_of_class;
        // Per-thing code
        if (!thing_is_picked_up(thing))
        {
            if (thing_revealed(thing, player->id_number))
            {
                const struct Coord2d pos = thing_minimap_position(thing, cam, scaled_zoom);
                RealScreenCoord basepos;
                basepos = MapDiagonalLength/2;

                // Do the drawing
                if (((get_gameturn() % (4 * kfx_config_state.gui_blink_rate)) / kfx_config_state.gui_blink_rate) == 1) {
                    if (thing_is_special_box(thing) || thing_is_spellbook(thing))
                    {
                        int64_t pixel_end = get_pixels_scaled_and_zoomed(basic_zoom);
                        int64_t p;
                        for (p = 0; p < pixel_end; p++)
                        {
                            panel_map_draw_pixel(pos.x.val + basepos + draw_square[p].delta_x,
                                                 pos.y.val + basepos + draw_square[p].delta_y,
                                                 resolve_indexed_pixel(kfx_sim_state.colours[15][0][15], RendererGetActivePalette()));
                        }
                        n++;
                    }
                    else if (thing_is_workshop_crate(thing))
                    {
                        int64_t pixel_end = get_pixels_scaled_and_zoomed(basic_zoom);
                        int64_t p;
                        for (p = 0; p < pixel_end; p++)
                        {
                            panel_map_draw_pixel(pos.x.val + basepos + draw_square[p].delta_x,
                                                 pos.y.val + basepos + draw_square[p].delta_y,
                                                 resolve_indexed_pixel(kfx_sim_state.colours[7][6][7], RendererGetActivePalette()));
                        }
                        n++;
                    }
                }
            }
        }
        // Per-thing code ends
        k++;
        if (k > slist->count)
        {
            ERRORLOG("Infinite loop detected when sweeping things list");
            break;
        }
    }
    return n;
}

void panel_map_draw_creature_dot(int64_t mapos_x, int64_t mapos_y, RealScreenCoord basepos, TbPixel col, int64_t basic_zoom, TbBool isLowRes)
{
    if (isLowRes)
    {
        // At low resolutions, we only need the single pixel
        panel_map_draw_pixel(mapos_x + basepos, mapos_y + basepos, col);
        return;
    }
    int64_t pixel_end = get_pixels_scaled_and_zoomed(basic_zoom);
    for (int64_t i = 0; i < pixel_end; i++)
    {
        panel_map_draw_pixel(mapos_x + basepos + draw_square[i].delta_x, mapos_y + basepos + draw_square[i].delta_y, col);
    }
}

int64_t draw_overlay_possessed_thing(struct PlayerInfo* player, int64_t mapos_x, int64_t mapos_y, RealScreenCoord basepos, TbPixel col, int64_t basic_zoom, TbBool isLowRes)
{
    const struct Camera* cam;
    cam = get_local_active_camera(player);
    if (cam == NULL)
        return 0;
    if (cam->view_mode != PVM_CreatureView)
        return 0;
    if ((get_gameturn() % (8 * kfx_config_state.gui_blink_rate)) >= 4 * kfx_config_state.gui_blink_rate)
    {
        col = resolve_indexed_pixel(kfx_sim_state.colours[15][15][15], RendererGetActivePalette());
    }
    if (isLowRes)
    {
        // At low resolutions, we only need the single pixel
        panel_map_draw_pixel(mapos_x + basepos, mapos_y + basepos, col);
        return 1;
    }
    int64_t pixel_end = get_pixels_scaled_and_zoomed(basic_zoom * 2);
    int64_t pixels_amount = scale_pixel(basic_zoom * 2);
    for (int64_t i = 0; i < pixel_end; i++)
    {
        panel_map_draw_pixel(mapos_x + basepos + draw_square[i].delta_x, mapos_y + basepos + draw_square[i].delta_y, col);
        panel_map_draw_pixel(mapos_x + basepos + pixels_amount + draw_square[i].delta_x, mapos_y + basepos + draw_square[i].delta_y, col);
        panel_map_draw_pixel(mapos_x + basepos - pixels_amount + draw_square[i].delta_x, mapos_y + basepos + draw_square[i].delta_y, col);
        panel_map_draw_pixel(mapos_x + basepos + draw_square[i].delta_x, mapos_y + basepos + pixels_amount + draw_square[i].delta_y, col);
        panel_map_draw_pixel(mapos_x + basepos + draw_square[i].delta_x, mapos_y + basepos - pixels_amount + draw_square[i].delta_y, col);
    }
    return 1;
}

int64_t draw_overlay_creatures(struct PlayerInfo *player, int64_t units_per_px, int64_t zoom, int64_t basic_zoom)
{
    TbBool isLowRes = 0;
    if (units_per_px < 16)
    {
       isLowRes = 1;
    }

    uint64_t k;
    int64_t i;
    int64_t n;
    SYNCDBG(18,"Starting");
    struct Camera *cam = get_local_active_camera(player);
    if (cam == NULL)
        return 0;
    n = 0;
    k = 0;
    const struct StructureList *slist = get_list_for_thing_class(TCls_Creature);
    i = slist->index;
    while (i != 0)
    {
        struct Thing *thing = thing_get(i);
        if (thing_is_invalid(thing))
        {
            ERRORLOG("Jump to invalid thing detected");
            break;
        }
        i = thing->next_of_class;
        // Per-thing code
        TbPixel col1;
        TbPixel col2;
        TbPixel col;
        col1 = resolve_indexed_pixel(31, RendererGetActivePalette());
        col2 = resolve_indexed_pixel(1, RendererGetActivePalette());
        if (!thing_is_picked_up(thing))
        {
            if (thing_revealed(thing, player->id_number))
            {
                if ((get_gameturn() % (8 * kfx_config_state.gui_blink_rate)) < 4 * kfx_config_state.gui_blink_rate)
                {
                    col1 = player_room_colours[get_player_color_idx(thing->owner)];
                    col2 = player_room_colours[get_player_color_idx(thing->owner)];
                }
                const struct Coord2d pos = thing_minimap_position(thing, cam, zoom);
                RealScreenCoord basepos;
                basepos = MapDiagonalLength/2;
                // Do the drawing
                if (thing->owner == player->id_number)
                {
                    if ((thing->model == gui_creature_type_highlighted) && ((get_gameturn() % (4 * kfx_config_state.gui_blink_rate)) >= 2 * kfx_config_state.gui_blink_rate))
                    {
                        int64_t pixels_amount = scale_pixel(basic_zoom * 4);
                        panel_map_draw_creature_dot(pos.x.val + pixels_amount, pos.y.val, basepos, col2, basic_zoom, isLowRes);
                        panel_map_draw_creature_dot(pos.x.val - pixels_amount, pos.y.val, basepos, col2, basic_zoom, isLowRes);
                        panel_map_draw_creature_dot(pos.x.val, pos.y.val + pixels_amount, basepos, col2, basic_zoom, isLowRes);
                        panel_map_draw_creature_dot(pos.x.val, pos.y.val - pixels_amount, basepos, col2, basic_zoom, isLowRes);
                        panel_map_draw_creature_dot(pos.x.val, pos.y.val, basepos, resolve_indexed_pixel(31, RendererGetActivePalette()), basic_zoom, isLowRes);
                    } else
                    {
                        if ((is_thing_directly_controlled_by_player(thing, my_player_number)) || (is_thing_passenger_controlled_by_player(thing, my_player_number)))
                        {
                            draw_overlay_possessed_thing(player, pos.x.val, pos.y.val, basepos, col2, basic_zoom, isLowRes);
                        }
                        else
                        {
                            panel_map_draw_creature_dot(pos.x.val, pos.y.val, basepos, col2, basic_zoom, isLowRes);
                        }
                    }
                } else
                {
                    if (thing->owner == kfx_config_state.neutral_player_num) {
                        col = player_room_colours[get_player_color_idx(((get_gameturn() + 1) % (4 * kfx_config_state.neutral_flash_rate)) / kfx_config_state.neutral_flash_rate)];
                    } else {
                        col = col1;
                    }
                    panel_map_draw_creature_dot(pos.x.val, pos.y.val, basepos, col, basic_zoom, isLowRes);
                }
            } else
            // Hero tunnelers may be visible on unrevealed terrain too (if on revealed, then they're already drawn)
            if (is_hero_tunnelling_to_attack(thing))
            {
                struct CreatureControl *cctrl;
                cctrl = creature_control_get_from_thing(thing);
                int64_t m;
                for (m=0; m < 5; m++)
                {
                    int64_t memberpos;
                    memberpos = cctrl->party.member_pos_stl[m];
                    if (memberpos == 0)
                        break;
                    if ((get_gameturn() % (8 * kfx_config_state.gui_blink_rate)) < 4 * kfx_config_state.gui_blink_rate)
                    {
                        col1 = player_room_colours[get_player_color_idx((int64_t)(cctrl->party.target_plyr_idx >= 0 ? cctrl->party.target_plyr_idx : 0))];
                        col2 = player_room_colours[get_player_color_idx(thing->owner)];
                    }
                    MapCoord x = subtile_coord_center(stl_num_decode_x(memberpos));
                    MapCoord y = subtile_coord_center(stl_num_decode_y(memberpos));
                    map_to_minimap(&x, &y, cam, zoom);

                    RealScreenCoord basepos;
                    basepos = MapDiagonalLength/2;
                    // Do the drawing
                    if (thing->owner == player->id_number) {
                        col = col2;
                    } else {
                        col = col1;
                    }
                    panel_map_draw_creature_dot(x, y, basepos, col, basic_zoom, isLowRes);
                }
            }
        }
        // Per-thing code ends
        k++;
        if (k > THINGS_COUNT)
        {
            ERRORLOG("Infinite loop detected when sweeping things list");
            break;
        }
    }
    return n;
}

/**
 * Draws own dungeon heart line on minimap.
 * @param player The player for whom drawing occurs.
 * @param zoom Zoom level of the minimap.
 * @return Amount of hearts drawn, either 0 or 1.
 */
int64_t draw_line_to_heart(struct PlayerInfo *player, int64_t units_per_px, int64_t zoom)
{
    struct Camera *cam = get_local_active_camera(player);
    if (cam == NULL)
        return 0;
    struct Thing *thing = get_player_soul_container(player->id_number);

    if (!thing_exists(thing)) {
        return 0;
    }
    RendererAddDrawFlags(Lb_SPRITE_TRANSPAR4);

    const struct Coord2d pos = thing_minimap_position(thing, cam, zoom);
    RealScreenCoord basepos;
    basepos = MapDiagonalLength/2;
    // Do the drawing
    int64_t dist;
    int64_t angle;
    dist = get_distance_xy(basepos, basepos, pos.x.val + basepos, pos.y.val + basepos);
    angle = -(LbArcTanAngle(pos.x.val, pos.y.val) & ANGLE_MASK) & ANGLE_MASK_4;
    int64_t delta_x;
    int64_t delta_y;
    delta_x = scale_ui_value(MAP_ARROW_DISTANCE) * LbSinL(angle) >> 16;
    delta_y = scale_ui_value(MAP_ARROW_DISTANCE) * LbCosL(angle) >> 16;
    int64_t frame;
    frame = (get_gameturn() & 3) + 1;
    int64_t draw_x;
    int64_t draw_y;
    draw_x = -delta_x / 2 + (frame * delta_x) / 4 + (basepos << 8);
    draw_y = -delta_y / 2 + (frame * delta_y) / 4 + (basepos << 8);
    int64_t i;
    for (i = dist - 4; i > 0; i -= 4)
    {
        if ((draw_x < 0) || (draw_x >> 8 >= MapDiagonalLength))
            break;
        if ((draw_y < 0) || (draw_y >> 8 >= MapDiagonalLength))
            break;
        draw_x += delta_x;
        draw_y += delta_y;
        int64_t pixel_end = get_pixels_scaled_and_zoomed(zoom * 2);
        TbPixel col = resolve_indexed_pixel(15, RendererGetActivePalette());
        for (int64_t p = 0; p < pixel_end; p++)
        {
            panel_map_draw_pixel((draw_x >> 8) + draw_square[p].delta_x, (draw_y >> 8) + draw_square[p].delta_y, col);
        }
    }
    RendererClearDrawFlags(Lb_SPRITE_TRANSPAR4);
    return 1;
}

void panel_map_draw_overlay_things(int64_t units_per_px, int64_t scaled_zoom, int64_t basic_zoom)
{
    SYNCDBG(7,"Starting");
    if (scaled_zoom < 1) {
        return;
    }
    struct PlayerInfo *player = get_my_player();
    draw_overlay_call_to_arms(player, units_per_px, scaled_zoom);
    draw_overlay_traps(player, units_per_px, scaled_zoom,basic_zoom);
    draw_overlay_creatures(player, units_per_px, scaled_zoom, basic_zoom);
    draw_overlay_spells_and_boxes(player, units_per_px, scaled_zoom, basic_zoom);
    draw_line_to_heart(player, units_per_px, basic_zoom);
}

void panel_map_update_subtile(PlayerNumber plyr_idx, MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{
    MapSlabCoord slb_x = subtile_slab(stl_x);
    MapSlabCoord slb_y = subtile_slab(stl_y);
    SubtlCodedCoords stl_num = get_subtile_number(stl_x, stl_y);
    struct Map *mapblk = get_map_block_at_pos(stl_num);
    struct SlabMap *slb = get_slabmap_block(slb_x, slb_y);
    int64_t col = 0;
    int64_t owner_col = slabmap_owner(slb);
    if ((mapblk->flags & SlbAtFlg_Unexplored) != 0)
    {
        col = PnC_Unexplored;
    }
    else if (map_block_revealed(mapblk, plyr_idx))
    {
        if ((slb->kind == SlbT_GOLD) || (slb->kind == SlbT_DENSEGOLD))
        {
            col = PnC_Gold;
            if ((mapblk->flags & SlbAtFlg_TaggedValuable) != 0)
            {
                col--;
            }
        } else
        if (slb->kind == SlbT_GEMS)
        {
            col = PnC_Gems;
            if ((mapblk->flags & SlbAtFlg_TaggedValuable) != 0)
            {
                col--;
            }
        }
        else
        if ((mapblk->flags & SlbAtFlg_IsRoom) != 0)
        {
            struct Room *room;
            room = room_get(slb->room_index);
            col = owner_col + PLAYERS_COUNT * room->kind + PnC_RoomsStart;
        } else
        if (slb->kind == SlbT_ROCK)
        {
            col = PnC_Rock;
        } else
        if (slb->kind == SlbT_ROCK_FLOOR)
        {
            col = PnC_RockFloor;
        }
        else
        if (subtile_has_abyss_on_top(stl_x, stl_y)) {
            col = PnC_Abyss;
        }
        else
        if ((mapblk->flags & SlbAtFlg_Filled) != 0)
        {
            col = PnC_Wall;
        } else
        if ((mapblk->flags & SlbAtFlg_IsDoor) != 0)
        {
            struct Thing *doortng;
            doortng = get_door_for_position(stl_x, stl_y);
            if (!thing_is_invalid(doortng)) {
                if(door_is_hidden_to_player(doortng,plyr_idx))
                {
                    col = PnC_Wall;
                }
                else
                {
                    col = owner_col + PLAYERS_COUNT * ((doortng->door.is_locked == 1) + 2 * doortng->model ) + PnC_DoorsStart;
                }
            }
        } else
        if ((mapblk->flags & SlbAtFlg_Blocking) == 0)
        {
            if (slb->kind == SlbT_LAVA) {
                col = PnC_Lava;
            } else
            if (slb->kind == SlbT_WATER) {
                col = PnC_Water;
            }  else
            if (slb->kind == SlbT_PURPLE)
            {
                col = PnC_purplePath;
            } else {
                col = owner_col + PnC_PathStart;
            }
        }

    }
    int64_t *mapptr = &PanelMap[stl_num];
    *mapptr = col;
}

void panel_map_update(int64_t x, int64_t y, int64_t w, int64_t h)
{
    SYNCDBG(17,"Starting for rect (%" PRId64 ",%" PRId64 ") at (%" PRId64 ",%" PRId64 ")",(int64_t)(w),(int64_t)(h),(int64_t)(x),(int64_t)(y));
    MapSubtlCoord stl_x;
    MapSubtlCoord stl_y;
    for (stl_y = y; stl_y < y + h; stl_y++)
    {
        if (stl_y > kfx_sim_state.map_subtiles_y)
            break;
        for (stl_x = x; stl_x < x + w; stl_x++)
        {
            if (stl_x > kfx_sim_state.map_subtiles_x)
                break;
            if (subtile_has_slab(stl_x, stl_y))
            {
                panel_map_update_subtile(my_player_number, stl_x, stl_y); //player->id number is still unitialized when this function is called at level start
            }
        }
    }
}

static void do_map_rotate_stuff(double relpos_x, double relpos_y, double *coord_x, double *coord_y, int64_t zoom)
{
    struct PlayerInfo *player = get_my_player();
    const struct Camera *cam = get_local_active_camera(player);
    const int64_t angle = cam->rotation_angle_x & ANGLE_MASK_4;
    const int64_t shift_x = -LbSinL(angle);
    const int64_t shift_y = LbCosL(angle);
    const double dx = (shift_y * relpos_x + shift_x * relpos_y) / (1U << 16);
    const double dy = (shift_y * relpos_y - shift_x * relpos_x) / (1U << 16);
    *coord_x = zoom * dx + cam->mappos.x.val;
    *coord_y = zoom * dy + cam->mappos.y.val;
}

static void do_map_rotate_stuff_subtile(double relpos_x, double relpos_y, MapSubtlCoord *stl_x, MapSubtlCoord *stl_y, int64_t zoom)
{
    double tmp_x, tmp_y;
    do_map_rotate_stuff(relpos_x, relpos_y, &tmp_x, &tmp_y, zoom);
    *stl_x = lround(tmp_x) / COORD_PER_STL;
    *stl_y = lround(tmp_y) / COORD_PER_STL;
}

int64_t do_left_map_drag(int64_t begin_x, int64_t begin_y, int64_t curr_x, int64_t curr_y, int64_t zoom, struct Packet *pckt)
{
  SYNCDBG(17,"Starting");
  static double frac_x, frac_y;
  if (!clicked_on_small_map)
  {
    grabbed_small_map = 0;
    return 0;
  }
  TbGraphicsWindow ewnd;
  store_engine_window(&ewnd, 1);
  double x = (curr_x - (ewnd.x + (ewnd.width  >> 1))) / 2.;
  double y = (curr_y - (ewnd.y + (ewnd.height >> 1))) / 2.;
  if (!grabbed_small_map)
  {
    if ((llabs(curr_x - old_mx) < 2) && (llabs(curr_y - old_my) < 2))
      return 0;
    grabbed_small_map = 1;
    x = y = 0;
    frac_x = frac_y = 0;
  }
  double exact_x, exact_y;
  do_map_rotate_stuff(x, y, &exact_x, &exact_y, zoom);
  exact_x += frac_x;
  exact_y += frac_y;
  const MapCoord coord_x = lround(exact_x);
  const MapCoord coord_y = lround(exact_y);
  const MapSubtlCoord stl_x = coord_x / COORD_PER_STL;
  const MapSubtlCoord stl_y = coord_y / COORD_PER_STL;
  frac_x = exact_x - coord_x;
  frac_y = exact_y - coord_y;
  kfx_sim_state.hand_over_subtile_x = stl_x;
  kfx_sim_state.hand_over_subtile_y = stl_y;
  if (subtile_has_slab(stl_x, stl_y))
  {
    set_packet_action(pckt, PckA_BookmarkLoad, coord_x, coord_y, 0, 0);
  }
  return 1;
}

int64_t do_left_map_click(int64_t begin_x, int64_t begin_y, int64_t curr_x, int64_t curr_y, int64_t zoom, struct Packet *pckt)
{
  SYNCDBG(17,"Starting");
  int64_t result;
  result = 0;
  if ((left_button_released) && (clicked_on_small_map))
  {
      if (grabbed_small_map)
      {
        kfx_sim_state.small_map_state = 2;
        LbMouseSetPosition(begin_x + MapDiagonalLength/2, begin_y + MapDiagonalLength/2);
      } else
      {
        do_map_rotate_stuff_subtile(curr_x - begin_x - MapDiagonalLength/2, curr_y - begin_y - MapDiagonalLength/2, &curr_x, &curr_y, zoom);
        kfx_sim_state.hand_over_subtile_x = curr_x;
        kfx_sim_state.hand_over_subtile_y = curr_y;
        if (subtile_has_slab(curr_x, curr_y))
        {
          const MapCoord x = subtile_coord_center(curr_x);
          const MapCoord y = subtile_coord_center(curr_y);
          set_packet_action(pckt, PckA_BookmarkLoad, x, y, 0, 0);
          result = 1;
        }
      }
    grabbed_small_map = 0;
    clicked_on_small_map = 0;
    left_button_released = 0;
  }
  return result;
}

int64_t do_right_map_click(int64_t start_x, int64_t start_y, int64_t curr_mx, int64_t curr_my, int64_t zoom, struct Packet *pckt)
{
    int64_t x;
    int64_t y;
    SYNCDBG(17,"Starting");
    struct PlayerInfo *player;
    struct Thing *thing;
    do_map_rotate_stuff_subtile(curr_mx - start_x - MapDiagonalLength/2, curr_my - start_y - MapDiagonalLength/2, &x, &y, zoom);
    kfx_sim_state.hand_over_subtile_x = x;
    kfx_sim_state.hand_over_subtile_y = y;
    player = get_my_player();
    thing = get_first_thing_in_power_hand(player);
    if (!thing_is_invalid(thing))
    {
        if (can_place_thing_here(thing, x, y, player->id_number))
          kfx_sim_state.small_map_state = 1;
    }
    if (right_button_clicked)
      right_button_clicked = 0;
    if (right_button_released)
    {
        right_button_released = 0;
        if (subtile_has_slab(x, y))
        {
            set_packet_action(pckt, PckA_UsePwrHandDrop, x, y, 0, 0);
            return 1;
        }
    }
    return 0;
}

/* Shared by setup_background()'s three independently-sized minimap scratch
 * arrays, replacing three copies of the same free-then-calloc dance. */
static void *resize_scratch_array(void *old_ptr, size_t count, size_t elem_size)
{
    free(old_ptr);
    return calloc(count, elem_size);
}

void setup_background(int64_t units_per_px)
{
    if (MapDiagonalLength != 2*(PANEL_MAP_RADIUS*units_per_px/16))
    {
        MapDiagonalLength = 2*(PANEL_MAP_RADIUS*units_per_px/16);
        MapBackground = resize_scratch_array(MapBackground,
            (size_t)MapDiagonalLength*(size_t)MapDiagonalLength, sizeof(*MapBackground));
        MapShapeStart = (int64_t *)resize_scratch_array(MapShapeStart,
            (size_t)MapDiagonalLength, sizeof(int64_t));
        MapShapeEnd = (int64_t *)resize_scratch_array(MapShapeEnd,
            (size_t)MapDiagonalLength, sizeof(int64_t));
    }
    if ((MapBackground == NULL) || (MapShapeStart == NULL) || (MapShapeEnd == NULL)) {
        MapDiagonalLength = 0;
        return;
    }
    int64_t radius;
    radius = MapDiagonalLength / 2;
    int64_t quarter_area;
    quarter_area = radius * radius;
    int64_t i;
    for (i=0; i < MapDiagonalLength; i++)
    {
        int64_t n;
        n = (radius - i - 1) * (i - radius + 1) + quarter_area;
        MapShapeStart[i] = radius - LbSqrL(n);
        MapShapeEnd[i] = radius + LbSqrL(n);
    }

    int64_t num_colours;
    num_colours = 0;
    TbBool cap_warned = false;
    int64_t out_scanline;
    out_scanline = lbDisplay.GraphicsScreenWidth;
    int64_t bkgnd_pos;
    bkgnd_pos = 0;
    TbPixel *out;
    out = &RendererGetFramebuffer()[PanelMapX + out_scanline * PanelMapY];
    int64_t w;
    int64_t h;
    for (h=0; h < MapDiagonalLength; h++)
    {
        for (w = MapShapeStart[h]; w < MapShapeEnd[h]; w++)
        {
            if (w < 0) continue;

            TbPixel orig;
            orig = out[w];
            out[w] = resolve_indexed_pixel(255, RendererGetActivePalette());
            // Legacy code captured the palette index directly off the 8bpp
            // screen; the framebuffer is true-colour now, so recover the
            // nearest equivalent index -- MapBackColours[]/MapBackground[]
            // still feed the still-index-based pixmap.ghost/map_abyss tables.
            // O(1) quantized lookup instead of LbPaletteFindColour()'s O(256)
            // linear scan -- see docs/refactor/renderer/02c.
            unsigned char orig_idx;
            orig_idx = TbRGBColorTable_Lookup(kfx_sim_state.colours, orig);
            int64_t colour;
            for (colour=0; colour < num_colours; colour++)
            {
                if (MapBackColours[colour] == orig_idx) {
                    break;
                }
            }
            if (num_colours == colour)
            {
                // PanelColours[] only has room for PANEL_MAP_BACKGROUND_COLOURS_MAX
                // distinct background colour classes. The legacy 8bpp renderer
                // already quantized every on-screen pixel to a palette index
                // before it reached the framebuffer, so a decorative background
                // naturally stayed within that count; true-colour rendering's
                // smooth shading/ghost blends produce far more distinct nearest-
                // palette-index results when reverse-mapped here, so without this
                // cap num_colours can exceed 16 and overflow PanelColours[] --
                // fold any excess into the last bucket instead.
                if (num_colours < PANEL_MAP_BACKGROUND_COLOURS_MAX)
                {
                    MapBackColours[num_colours] = orig_idx;
                    num_colours++;
                }
                else
                {
                    if (!cap_warned)
                    {
                        WARNLOG("Minimap background has more than %" PRId64 " distinct colours; "
                            "approximating the rest with the last colour class",
                            (int64_t)(PANEL_MAP_BACKGROUND_COLOURS_MAX));
                        cap_warned = true;
                    }
                    colour = num_colours - 1;
                }
            }
            MapBackground[bkgnd_pos+w] = colour;
        }
        bkgnd_pos += MapDiagonalLength;
        out += out_scanline;
    }
    NumBackColours = num_colours;
}

/* Shared between setup_panel_colors() and update_panel_colors(): both
 * derive the "unexplored fog of war" and "tagged valuable tile" background
 * colours from the same bkcol/frame blink state, identically. What differs
 * between the two callers is the third (gems) colour's destination slot and
 * literal offset, so that stays local to each caller instead of being
 * folded in here. */
static void resolve_common_background_colours(unsigned char bkcol, int64_t frame, const unsigned char *pal,
                                               TbPixel *out_unexplored, TbPixel *out_tagged_gold)
{
    if (frame != 0)
    {
        *out_unexplored = resolve_indexed_pixel(pixmap.ghost[bkcol + 26*256], pal);
        *out_tagged_gold = resolve_indexed_pixel(pixmap.ghost[bkcol + 140*256], pal);
    }
    else
    {
        *out_unexplored = resolve_indexed_pixel(bkcol, pal);
        *out_tagged_gold = resolve_indexed_pixel(bkcol, pal);
    }
}

void setup_panel_colors(void)
{
    const unsigned char *pal = RendererGetActivePalette();
    int64_t frame;
    frame = (get_gameturn() % (4 * kfx_config_state.gui_blink_rate)) / kfx_config_state.gui_blink_rate;
    TbPixel frcol;
    frcol = player_room_colours[(get_gameturn() % (4 * kfx_config_state.neutral_flash_rate)) / kfx_config_state.neutral_flash_rate];
    int64_t bkcol_idx;
    int64_t pncol_idx;
    pncol_idx = 0;
    for (bkcol_idx=0; bkcol_idx < NumBackColours; bkcol_idx++)
    {
        unsigned char bkcol;
        bkcol = MapBackColours[bkcol_idx];
        int64_t n;
        n = pncol_idx;
        resolve_common_background_colours(bkcol, frame, pal,
            &PanelColours[n + PnC_Unexplored], &PanelColours[n + PnC_Tagged_Gold]);
        if (frame != 0)
        {
            PanelColours[n + PnC_Gems] = resolve_indexed_pixel(102 + (pixmap.ghost[bkcol] >> 6), pal);
        } else //as this is during setup at gameturn 1, the else looks like it is never used.
        {
            PanelColours[n + PnC_Tagged_Gems] = resolve_indexed_pixel(104 + (pixmap.ghost[bkcol] >> 6), pal);
        }
        PanelColours[n + 0] = resolve_indexed_pixel(bkcol, pal);
        PanelColours[n + PnC_Wall]    = resolve_indexed_pixel(pixmap.ghost[bkcol + 16*256], pal);
        PanelColours[n + PnC_Rock]      = resolve_indexed_pixel(0, pal);
        PanelColours[n + PnC_Gold]      = resolve_indexed_pixel(pixmap.ghost[bkcol + 140*256], pal);
        PanelColours[n + PnC_Lava]      = resolve_indexed_pixel(146, pal);
        PanelColours[n + PnC_Water]     = resolve_indexed_pixel(85, pal);
        PanelColours[n + PnC_purplePath]    = resolve_indexed_pixel(255, pal);
        PanelColours[n + PnC_Gems]      = resolve_indexed_pixel(102 + (pixmap.ghost[bkcol] >> 6), pal);
        PanelColours[n + PnC_RockFloor] = resolve_indexed_pixel(145, pal);
        PanelColours[n + PnC_Abyss]     = resolve_indexed_pixel(pixmap.map_abyss[bkcol], pal);

        n = pncol_idx + PnC_RoomsStart;
        int64_t i;
        int64_t k;
        for (i=TERRAIN_ITEMS_MAX; i > 0; i--)
        {
            PanelColours[n + 0] = player_room_colours[get_player_color_idx(PLAYER0)];
            PanelColours[n + 1] = player_room_colours[get_player_color_idx(PLAYER1)];
            PanelColours[n + 2] = player_room_colours[get_player_color_idx(PLAYER2)];
            PanelColours[n + 3] = player_room_colours[get_player_color_idx(PLAYER3)];
            PanelColours[n + 4] = player_room_colours[get_player_color_idx(PLAYER_GOOD)];
            PanelColours[n + 5] = frcol;
            PanelColours[n + 6] = player_room_colours[get_player_color_idx(PLAYER4)];
            PanelColours[n + 7] = player_room_colours[get_player_color_idx(PLAYER5)];
            PanelColours[n + 8] = player_room_colours[get_player_color_idx(PLAYER6)];
            n += PLAYERS_COUNT;
        }

        n = pncol_idx + PnC_PathStart;
        {
            PanelColours[n + 0] = player_path_colours[get_player_color_idx(PLAYER0)];
            PanelColours[n + 1] = player_path_colours[get_player_color_idx(PLAYER1)];
            PanelColours[n + 2] = player_path_colours[get_player_color_idx(PLAYER2)];
            PanelColours[n + 3] = player_path_colours[get_player_color_idx(PLAYER3)];
            PanelColours[n + 4] = player_path_colours[get_player_color_idx(PLAYER_GOOD)];
            PanelColours[n + 5] = player_path_colours[PLAYER_NEUTRAL];
            PanelColours[n + 6] = player_path_colours[get_player_color_idx(PLAYER4)];
            PanelColours[n + 7] = player_path_colours[get_player_color_idx(PLAYER5)];
            PanelColours[n + 8] = player_path_colours[get_player_color_idx(PLAYER6)];
        }
        n = pncol_idx + PnC_DoorsStart;
        for (i=TRAPDOOR_TYPES_MAX; i > 0; i--)
        {
            for (k=0; k < PLAYERS_COUNT; k++)
            {
              PanelColours[n + k] = resolve_indexed_pixel(Tbp_OpenDoor, pal);
            }
            n += PLAYERS_COUNT;
            for (k=0; k < PLAYERS_COUNT; k++)
            {
              PanelColours[n + k] = resolve_indexed_pixel(Tbp_LockedDoor, pal);
            }
            n += PLAYERS_COUNT;
        }
        pncol_idx += PnC_End;
    }
}

void update_panel_color_player_color(PlayerNumber plyr_idx, unsigned char color_idx)
{
    int64_t n = 0;
    int64_t pncol_idx = 0;
    for (int64_t bkcol_idx=0; bkcol_idx < NumBackColours; bkcol_idx++)
    {
        n = pncol_idx + PnC_RoomsStart;

        for (int64_t i=TERRAIN_ITEMS_MAX; i > 0; i--)
        {
            PanelColours[n + plyr_idx] = player_room_colours[color_idx];
            n += PLAYERS_COUNT;
        }
        n = pncol_idx + PnC_PathStart;
        {
            PanelColours[n + plyr_idx] = player_path_colours[color_idx];
        }

        pncol_idx += PnC_End;
    }
}

void update_panel_colors(void)
{
    const unsigned char *pal = RendererGetActivePalette();
    int64_t frame;
    frame = (get_gameturn() % (4 * kfx_config_state.gui_blink_rate)) / kfx_config_state.gui_blink_rate;
    TbPixel frcol;
    frcol = player_room_colours[(get_gameturn() % (4 * kfx_config_state.neutral_flash_rate)) / kfx_config_state.neutral_flash_rate];
    int64_t bkcol_idx;
    int64_t pncol_idx;
    pncol_idx = 0;
    for (bkcol_idx=0; bkcol_idx < NumBackColours; bkcol_idx++)
    {
        unsigned char bkcol;
        bkcol = MapBackColours[bkcol_idx];
        int64_t n;
        n = pncol_idx;
        resolve_common_background_colours(bkcol, frame, pal,
            &PanelColours[n + PnC_Unexplored], &PanelColours[n + PnC_Tagged_Gold]);
        if (frame != 0)
        {
            PanelColours[n + PnC_Tagged_Gems] = resolve_indexed_pixel(102 + (pixmap.ghost[bkcol] >> 6), pal);
        } else
        {
            PanelColours[n + PnC_Tagged_Gems] = resolve_indexed_pixel(100 + (pixmap.ghost[bkcol] >> 6), pal);
        }
        n = pncol_idx + PnC_RoomsStart;
        int64_t i;
        for (i=TERRAIN_ITEMS_MAX; i > 0; i--)
        {
            PanelColours[n + PLAYER_NEUTRAL] = frcol;
            n += PLAYERS_COUNT;
        }
        pncol_idx += PnC_End;
    }

    int64_t highlight;
    highlight = gui_room_type_highlighted;
    frame = get_gameturn() % (2 * kfx_config_state.gui_blink_rate);
    if (frame >= kfx_config_state.gui_blink_rate)
        highlight = -1;
    if (PrevRoomHighlight != highlight)
    {
        if ((PrevRoomHighlight >= 0) && (NumBackColours > 0))
        {
            int64_t i;
            int64_t n;
            n = PLAYERS_COUNT * PrevRoomHighlight + PnC_RoomsStart;
            for (i=NumBackColours; i > 0; i--)
            {
                PanelColours[n + 0] = player_room_colours[get_player_color_idx(0)];
                PanelColours[n + 1] = player_room_colours[get_player_color_idx(1)];
                PanelColours[n + 2] = player_room_colours[get_player_color_idx(2)];
                PanelColours[n + 3] = player_room_colours[get_player_color_idx(3)];
                PanelColours[n + 4] = player_room_colours[get_player_color_idx(4)];
                PanelColours[n + 5] = frcol;
                PanelColours[n + 6] = player_room_colours[get_player_color_idx(6)];
                PanelColours[n + 7] = player_room_colours[get_player_color_idx(7)];
                PanelColours[n + 8] = player_room_colours[get_player_color_idx(8)];
                n += PnC_End;
            }
        }

        if ((highlight >= 0) && (NumBackColours > 0))
        {
            int64_t i;
            int64_t n;
            n = PLAYERS_COUNT * highlight + PnC_RoomsStart;
            for (i=NumBackColours; i > 0; i--)
            {
                TbPixel hlcol = resolve_indexed_pixel(31, pal);
                PanelColours[n + 0] = hlcol;
                PanelColours[n + 1] = hlcol;
                PanelColours[n + 2] = hlcol;
                PanelColours[n + 3] = hlcol;
                PanelColours[n + 4] = hlcol;
                PanelColours[n + 5] = hlcol;
                PanelColours[n + 6] = hlcol;
                PanelColours[n + 7] = hlcol;
                PanelColours[n + 8] = hlcol;
                n += PnC_End;
            }
        }

        PrevRoomHighlight = highlight;
    }

    highlight = gui_door_type_highlighted;
    if (frame >= kfx_config_state.gui_blink_rate)
        highlight = -1;
    if (highlight != PrevDoorHighlight)
    {
        if ((PrevDoorHighlight >= 0) && (PrevDoorHighlight != TRAPDOOR_TYPES_MAX) && (NumBackColours > 0))
        {
            int64_t i;
            int64_t n;
            n = 2 * PLAYERS_COUNT * PrevDoorHighlight;
            for (i=NumBackColours; i > 0; i--)
            {
                int64_t k;
                for (k=0; k < PLAYERS_COUNT; k+=2)
                {
                  PanelColours[n + PnC_DoorsStart       + k] = resolve_indexed_pixel(Tbp_OpenDoor, pal);
                  PanelColours[n + PnC_DoorsStartLocked + k] = resolve_indexed_pixel(Tbp_LockedDoor, pal);
                }
                n += PnC_End;
            }
        }
        if ((highlight >= 0) && (NumBackColours > 0))
        {
            int64_t i;
            int64_t n;
            n = 2 * PLAYERS_COUNT * highlight;
            for (i = NumBackColours; i > 0; i--)
            {
                int64_t k;
                for (k=0; k < PLAYERS_COUNT; k+=2)
                {
                  PanelColours[n + PnC_DoorsStart       + k] = resolve_indexed_pixel(Tbp_DoorHighlighted, pal);
                  PanelColours[n + PnC_DoorsStartLocked + k] = resolve_indexed_pixel(Tbp_DoorHighlighted, pal);
                }
                n += PnC_End;
            }
        }
        PrevDoorHighlight = highlight;
    }
}

/* ui_reset_panel_map_background_cache(): called once
 * early during level (re)start (main_game.c, right after the existing
 * setup_panel_colors() call), well before this level's own map/camera has
 * rendered a single frame. auto_gen_tables()'s PrevPixelSize gate only
 * tracks UI scale, not "which level" -- so on any level after the first in
 * a process, PrevPixelSize is already frozen at the right value and the
 * lazy rebuild below silently never runs again, leaving PanelColours[]
 * built from whatever MapBackColours[]/palette happened to be around at
 * this early, pre-render point in a *previous* level's lifetime as the
 * permanent (wrong) result for every level after the first. Setting
 * PrevPixelSize to an impossible value forces the next real
 * panel_map_draw_slabs() call -- once this level is actually rendering,
 * with its own correct active palette -- to recapture and rebuild for
 * real. See docs/refactor/renderer/02b-legacy-bugs-found.md. */
void reset_panel_map_background_cache(void)
{
    PrevPixelSize = -1;
}

void auto_gen_tables(int64_t units_per_px)
{
    if (PrevPixelSize != 256 * units_per_px / 16)
    {
        SYNCDBG(7, "Rebuilding minimap background tables: units_per_px=%" PRId64 ", PrevPixelSize %" PRId64 " -> %" PRId64,
            (int64_t)(units_per_px), (int64_t)(PrevPixelSize), (int64_t)(256 * units_per_px / 16));
        PrevPixelSize = 256 * units_per_px / 16;
        setup_background(units_per_px);
        setup_panel_colors();
        SYNCDBG(7, "Minimap background tables rebuilt: NumBackColours=%" PRId64 ", MapDiagonalLength=%" PRId64,
            (int64_t)(NumBackColours), (int64_t)(MapDiagonalLength));
    }
}

void panel_map_draw_slabs(int64_t x, int64_t y, int64_t units_per_px, int64_t zoom)
{
    PanelMapX = scale_value_for_resolution_with_upp(x,units_per_px);
    PanelMapY = scale_value_for_resolution_with_upp(y,units_per_px);
    auto_gen_tables(units_per_px);
    update_panel_colors();
    struct PlayerInfo *player = get_my_player();
    struct Camera *cam = get_local_active_camera(player);

    if ((cam == NULL) || (MapDiagonalLength < 1))
        return;

    const int64_t shift_x = -LbSinL(cam->rotation_angle_x) * zoom / 256;
    const int64_t shift_y = LbCosL(cam->rotation_angle_x) * zoom / 256;
    int64_t shift_stl_x = (cam->mappos.x.val << 8) - MapDiagonalLength * shift_x / 2 - MapDiagonalLength * shift_y / 2;
    int64_t shift_stl_y = (cam->mappos.y.val << 8) - MapDiagonalLength * shift_y / 2 + MapDiagonalLength * shift_x / 2;

    unsigned char *bkgnd_line;
    bkgnd_line = MapBackground;
    TbPixel *out_line;
    out_line = &RendererGetFramebuffer()[PanelMapX + lbDisplay.GraphicsScreenWidth * PanelMapY];
    int64_t h;
    for (h = 0; h < MapDiagonalLength; h++)
    {
        int64_t start_w;
        int64_t end_w;
        start_w = MapShapeStart[h];
        end_w = MapShapeEnd[h];
        int64_t subpos_x;
        int64_t subpos_y;
        subpos_y = shift_stl_x + shift_y * (end_w - 1);
        subpos_x = shift_stl_y - shift_x * (end_w - 1);
        for (; end_w > start_w; end_w--)
        {
            if ((subpos_y >= 0) && (subpos_x >= 0) && (subpos_y < (1<<16)*kfx_sim_state.map_subtiles_x) && (subpos_x < (1<<16)*kfx_sim_state.map_subtiles_y)) {
                break;
            }
            subpos_y -= shift_y;
            subpos_x += shift_x;
        }
        subpos_y = shift_stl_x + shift_y * start_w;
        subpos_x = shift_stl_y - shift_x * start_w;
        for (; start_w < end_w; start_w++)
        {
            if ((subpos_y >= 0) && (subpos_x >= 0) && (subpos_y < (1<<16)*kfx_sim_state.map_subtiles_x) && (subpos_x < (1<<16)*kfx_sim_state.map_subtiles_y)) {
                break;
            }
            subpos_y += shift_y;
            subpos_x -= shift_x;
        }
        unsigned char *bkgnd;
        bkgnd = &bkgnd_line[start_w];
        TbPixel *out;
        out = &out_line[start_w];
        uint64_t precor_y;
        uint64_t precor_x;
        precor_x = subpos_y;
        precor_y = subpos_x;
        int64_t w;
        for (w = end_w-start_w; w > 0; w--)
        {
            int64_t pnmap_idx;
            pnmap_idx = ((precor_x>>16)) + (((precor_y>>16)) * (kfx_sim_state.map_subtiles_x + 1) );
            int64_t pncol_idx;
            //TODO reenable background
            pncol_idx = PanelMap[pnmap_idx] + (*bkgnd * PnC_End);
            *out = PanelColours[pncol_idx];
            precor_x += shift_y;
            precor_y -= shift_x;
            out++;
            bkgnd++;
        }
        out_line += lbDisplay.GraphicsScreenWidth;
        bkgnd_line += MapDiagonalLength;
        shift_stl_x += shift_x;
        shift_stl_y += shift_y;
    }
}
/******************************************************************************/
