#ifndef FRONTGUI_SKIRMISH_SETUP_H
#define FRONTGUI_SKIRMISH_SETUP_H

#include <stdint.h>
// docs/refactor/skirmish/ (S4): the Skirmish screen's "Setup" tab -- collapsing
// headers General / Availability / Win-Lose / Slots & AI over the state in
// skirmish_setup.h. Availability uses the same icon tiles as the in-game
// sidebar and the editor's toolbox (creature/room/spell/trap icons, panel
// sprites, GUI_ICON_PACK PNG overrides) with a player selector built from the
// coloured player symbols.

#ifdef __cplusplus

// Draws the tab's content in a scrolling area `height` px tall. Call
// skirmish_setup_sync() first (the screen does, every frame).
void frontgui_skirmish_setup_draw(double height);

// One short line for the screen's bottom row: what is wrong with the setup
// (blocks Play), or empty. Points into a static buffer.
const char *frontgui_skirmish_setup_status(void);

// True while the mouse is over the Setup tab: right-click there is a tile action (switch off / -1),
// so the legacy "right-click = back" screen input must not also fire (frontend_input(), frontend.cpp).
int64_t frontgui_skirmish_setup_captures_right_click(void);

#endif // __cplusplus
#endif // FRONTGUI_SKIRMISH_SETUP_H
