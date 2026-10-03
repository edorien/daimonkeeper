/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file pointer_graphics.c
 *     Choosing the mouse pointer.
 * @par Purpose:
 *     Which pointer the local player sees over the dungeon: room, trap,
 *     door and terrain placement, spell cursors (and the spell cost shown
 *     under them), the hand. Moved from kfx_render's engine_redraw.c in
 *     refactor pass 2 (S13): choosing the cursor is UI.
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
#include "local_state.h"
#include "post_inc.h"

/******************************************************************************/
int64_t draw_spell_cost;
/******************************************************************************/
int64_t get_place_room_pointer_graphics(RoomKind rkind)
{
    struct RoomConfigStats* roomst = get_room_kind_stats(rkind);
    return roomst->pointer_sprite_idx;
}

int64_t get_place_trap_pointer_graphics(ThingModel trmodel)
{
    struct TrapConfigStats* trapst = get_trap_model_stats(trmodel);
    return trapst->pointer_sprite_idx;
}

int64_t get_place_door_pointer_graphics(ThingModel drmodel)
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
    int64_t i;
    int64_t pwkind = -1;
    struct PlayerInfo* player = get_my_player();
    struct UserState* ustate = get_local_user_state();
    pwkind = ustate->chosen_power_kind;
    SYNCDBG(5,"Starting for power %" PRId64,(int64_t)pwkind);
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
    if (((kfx_sim_state.operation_flags & GOF_ShowGui) != 0) && mouse_is_over_panel_map(local_state.minimap_pos_x, local_state.minimap_pos_y))
    {
        if (kfx_sim_state.small_map_state == 2) {
            set_pointer_graphic(MousePG_Invisible);
        } else {
            set_pointer_graphic(MousePG_Arrow);
        }
        return;
    }
    // Mouse over battle message box
    int64_t battle_creature_over_local = battle_creature_over;
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
    if ((game_is_busy_doing_gui() != 0))
    {
        set_pointer_graphic(MousePG_Arrow);
        return;
    }
    int64_t i;
    int64_t thing_under_hand;
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
    SYNCDBG(6,"Starting for view %" PRId64 ", player state %s, instance %" PRId64,(int64_t)player->view_type,player_state_code_name(player->work_state),(int64_t)player->instance_num);
    switch (get_local_view_type(player))
    {
    case PVT_DungeonTop:
        // This case is complicated
        process_dungeon_top_pointer_graphic(player);
        break;
    case PVT_CreatureContrl:
    case PVT_CreaturePasngr:
        if ((cheat_menu_is_active() || a_menu_window_is_active()))
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

int64_t get_place_terrain_pointer_graphics(SlabKind skind)
{
    int64_t result;
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
