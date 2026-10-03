/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file local_view.c
 *     The local player's view transitions.
 * @par Purpose:
 *     kfx_sim's player instances used to set these palette fades, menus
 *     and map UI holds themselves, inside is_my_player() blocks, reaching
 *     into LocalState. Each case below is one of those blocks, statement
 *     for statement, in its original order (refactor pass 2, S15). All of
 *     it is local presentation: no case changes the simulation, except the
 *     local-only GOF_ShowPanel flag the blocks already set.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "local_view.h"
#include "local_state.h"
#include "player_data.h"
#include "kfx_sim_state.h"
#include "config_settings.h"
#include "frontend.h"
#include "engine_redraw.h"
#include "bflib_inputctrl.h"
#include "bflib_vidraw.h"
#include "vidfade.h"
#include "ports/ui_port.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/** Saves `current` as the value to put back when a hold ends. Holding again
 *  keeps the first saved value, unless the value was put back in between (an
 *  exit that skipped the restore). */
static void begin_map_ui_hold(TbBool* held, TbBool* saved, TbBool current)
{
    if (current || !*held)
        *saved = current;
    *held = true;
}

void set_map_ui_hidden_with(TbBool status_menu, TbBool tooltips, uint64_t (*toggle_status)(int64_t visible))
{
    if (status_menu)
    {
        begin_map_ui_hold(&local_state.status_menu_hidden_for_map, &local_state.status_menu_restore, toggle_status(0));
    }
    else if (local_state.status_menu_hidden_for_map)
    {
        toggle_status(local_state.status_menu_restore);
        local_state.status_menu_hidden_for_map = false;
    }

    if (tooltips)
    {
        begin_map_ui_hold(&local_state.tooltips_hidden_for_map, &local_state.tooltips_restore, settings.tooltips_on);
        settings.tooltips_on = false;
    }
    else if (local_state.tooltips_hidden_for_map)
    {
        settings.tooltips_on = local_state.tooltips_restore;
        local_state.tooltips_hidden_for_map = false;
    }
}

void set_map_ui_hidden(TbBool status_menu, TbBool tooltips)
{
    set_map_ui_hidden_with(status_menu, tooltips, toggle_status_menu);
}

void local_view_transition(struct PlayerInfo *player, int64_t kind)
{
    switch (kind)
    {
    case LVTr_LevelStart: // init_player()
        local_state.minimap_pos_x = 11;
        local_state.minimap_pos_y = 11;
        local_state.minimap_zoom = settings.minimap_zoom;
        local_state.roomspace_size = DEFAULT_USER_ROOMSPACE_WIDTH;
        setup_engine_window(0, 0, MyScreenWidth, MyScreenHeight);
        local_state.main_palette = engine_palette;
        //workaround until settings are synced through multiplayer
        if (kfx_sim_state.game_kind == GKind_MultiGame)
            local_state.minimap_zoom = 256;
        break;
    case LVTr_PassengerBegin: // pinstfs_passenger_control_creature()
        local_state.palette_fade_step_possession = 1;
        turn_off_all_window_menus();
        turn_off_menu(GMnu_CREATURE_QUERY1);
        turn_off_menu(GMnu_CREATURE_QUERY2);
        break;
    case LVTr_PossessionLeave: // pinstfs_direct_leave_creature()
        PaletteSetUserPalette(player->user_id, engine_palette);
        local_state.palette_fade_step_possession = 11;
        turn_off_all_window_menus();
        turn_off_query_menus();
        turn_on_main_panel_menu();
        set_flag_value(kfx_sim_state.operation_flags, GOF_ShowPanel, (kfx_sim_state.operation_flags & GOF_ShowGui) != 0);
        LbGrabMouseCheck(MG_OnPossessionLeave);
        break;
    case LVTr_PassengerLeave: // pinstfs_passenger_leave_creature()
        PaletteSetUserPalette(player->user_id, engine_palette);
        local_state.palette_fade_step_possession = 11;
        turn_off_all_window_menus();
        turn_off_query_menus();
        turn_off_all_panel_menus();
        turn_on_main_panel_menu();
        set_flag_value(kfx_sim_state.operation_flags, GOF_ShowPanel, (kfx_sim_state.operation_flags & GOF_ShowGui) != 0);
        break;
    case LVTr_ControlledCreatureDied: // prepare_to_controlled_creature_death()
        turn_off_all_window_menus();
        turn_off_query_menus();
        turn_on_main_panel_menu();
        set_flag_value(kfx_sim_state.operation_flags, GOF_ShowPanel, (kfx_sim_state.operation_flags & GOF_ShowGui) != 0);
        PaletteSetUserPalette(player->user_id, engine_palette);
        local_state.palette_fade_step_possession = 11;
        break;
    case LVTr_MapFadeInBegin: // pinstfs_fade_to_map()
        local_state.palette_fade_step_map = 0;
        set_map_ui_hidden(true, true);
        break;
    case LVTr_MapShown: // pinstfe_fade_to_map()
        set_map_ui_hidden(true, false);
        break;
    case LVTr_MapFadeOutBegin: // pinstfs_fade_from_map()
        set_map_ui_hidden(true, true);
        kfx_sim_state.operation_flags &= ~GOF_ShowPanel;
        local_state.palette_fade_step_map = 32;
        break;
    case LVTr_MapHidden: // pinstfe_fade_from_map()
        set_map_ui_hidden(false, false);
        break;
    default:
        ERRORLOG("Unknown local view transition %d", (int)kind);
        break;
    }
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
