/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file kfx_local_view.h
 *     This machine's view of the game: what the local player has chosen on the panels, the hand over the minimap,
 *     the eye lens it looks through, the open panel menu, the computer assistant's settings, the battles on the
 *     battle panel. A save holds it (SGC_KfxLocalView), so a load restores it; a resync leaves it alone, and the
 *     simulation state doesn't hold it (refactor pass 5, S05: it was in kfx_sim_state and kfx_net_state, kept
 *     over a resync by hand). kfx_render owns it (it reads the lens and the minimap's hand); kfx_game and
 *     kfx_frontend read it directly, kfx_sim through RenderPort/UiPort.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_KFX_LOCAL_VIEW_H
#define DK_KFX_LOCAL_VIEW_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

struct KfxLocalView {
    /** The room, keeper power and workshop item chosen on the panels, with their panel sprite and tooltip. */
    int64_t chosen_room_kind;
    int64_t chosen_room_spridx;
    int64_t chosen_room_tooltip;
    int64_t chosen_spell_type;
    int64_t chosen_spell_spridx;
    int64_t chosen_spell_tooltip;
    int64_t manufactr_element;
    int64_t manufactr_spridx;
    int64_t manufactr_tooltip;
    /** The subtile the hand is over on the minimap, and the minimap's state (2: the hand is on it). */
    MapSubtlCoord hand_over_subtile_x;
    MapSubtlCoord hand_over_subtile_y;
    unsigned char small_map_state;
    /** The eye lens in use (the possession view's); a load sets it up again. */
    unsigned char applied_lens_type;
    /** The MenuID of the open panel menu, or 0. */
    char active_panel_mnu_idx;
    /** The computer assistant's setting for the local player. */
    char comp_player_aggressive;
    char comp_player_defensive;
    char comp_player_construct;
    char comp_player_creatrsonly;
    /** The battles on the battle panel (the local player's). */
    BattleIndex visible_battles[3];
};

#pragma pack()

extern struct KfxLocalView kfx_local_view;

/** The keeper power chosen on the panel (was set_chosen_spell()). */
void set_chosen_power(PowerKind pwkind, TextStringId sptooltip);
void set_chosen_power_none(void);
/** A player lost the power: if it's the chosen one, nothing is chosen (RenderPort's local_view_power_removed). */
void local_view_power_removed(PowerKind pwkind);
/** The open panel menu (RenderPort's local_view_active_panel_menu). */
char local_view_active_panel_menu(void);
/** The battle panel's list starts again from this battle (RenderPort's local_view_battles_restart). */
void local_view_battles_restart(BattleIndex first);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
