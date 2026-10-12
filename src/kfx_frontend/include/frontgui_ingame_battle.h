#ifndef FRONTGUI_INGAME_BATTLE_H
#define FRONTGUI_INGAME_BATTLE_H

// Phase 3 (docs/refactor/ingame-gui/04-messages-tooltips-infobox.md §1):
// the battle-participants box (GMnu_BATTLE) as an ImGui window. The battle
// lists (friendly_battler_list / enemy_battler_list / kfx_local_view.visible_battles)
// and the scroll actions stay in the sim / frontmenu_ingame_evnt.c -- this
// only swaps drawing + hit-testing.

#ifdef __cplusplus
// C++ only: uses ImGui-flavoured layout, called from frontgui_ingame.cpp.
void battlemenu_frame(void);
#endif

#endif // FRONTGUI_INGAME_BATTLE_H
