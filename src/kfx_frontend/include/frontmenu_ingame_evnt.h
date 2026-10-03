/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontmenu_ingame_evnt.h
 *     Header file for frontmenu_ingame_evnt.c.
 * @par Purpose:
 *     In-game events GUI, visible during gameplay at bottom.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   KeeperFX Team
 * @date     05 Jan 2009 - 03 Jan 2011
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_FRONTMENU_INGAMEVNT_H
#define DK_FRONTMENU_INGAMEVNT_H

#include "globals.h"

#include "bflib_guibtns.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

struct GuiMenu;
struct GuiButton;

/******************************************************************************/
enum EventButtonState {
    EvBtnS_Read = 1,
    EvBtnS_Hidden = 2,
};

extern int64_t battle_creature_over;
extern int64_t friendly_battler_list[];
extern int64_t enemy_battler_list[];
void maintain_my_battle_list(void);
// my_visible_event_idx moved to gui_frontmenu.h (stage 10,
// docs/refactor/stage-10-kfx-frontend.md).
extern unsigned char my_event_button_state[];

#pragma pack()
/******************************************************************************/
extern struct GuiMenu text_info_menu;
extern struct GuiMenu battle_menu;
/******************************************************************************/
void gui_open_event(struct GuiButton *gbtn);
void gui_kill_event(struct GuiButton *gbtn);
EventIndex get_my_event_button_index(uint64_t button_idx);
void turn_on_event_info_panel_if_necessary(EventIndex evidx);
void activate_event_box(EventIndex evidx);
void gui_next_battle(struct GuiButton *gbtn);
void gui_previous_battle(struct GuiButton *gbtn);
// Phase 3: the battle-box battler actions (act on the battle_creature_over
// global; all ignore their GuiButton* arg) -- exposed for the ImGui box.
void gui_get_creature_in_battle(struct GuiButton *gbtn);   // cast selected power / pick up
void gui_go_to_person_in_battle(struct GuiButton *gbtn);   // zoom camera to it

int64_t zoom_to_fight(PlayerNumber plyr_idx);

void draw_bonus_timer(void);
TbBool bonus_timer_enabled(void);
void draw_timer(void);
void draw_frametime(void);
void draw_gameturn_timer(void);
void draw_consolelog(void);
void draw_network_stats(void);
extern int64_t debug_display_network_stats;
TbBool timer_enabled(void);
TbBool frametime_enabled(void);
TbBool consolelog_enabled(void);
TbBool script_timer_enabled(void);
TbBool gameturn_timer_enabled(void);
void draw_script_timer(PlayerNumber plyr_idx, unsigned char timer_id, uint64_t limit, TbBool real);
TbBool display_variable_enabled(void);
void draw_script_variable(PlayerNumber plyr_idx, unsigned char valtype, unsigned char validx, int64_t target, unsigned char targettype);

extern uint64_t TimerTurns;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
