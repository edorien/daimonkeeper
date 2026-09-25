#ifndef FRONTGUI_INGAME_CREATURE_H
#define FRONTGUI_INGAME_CREATURE_H

// The creature-activity list, top-down query panel, and the creature-query
// / possession detail panel (docs/refactor/ingame-gui/10-maintainability-refactors.md
// §7). Called from frontgui_ingame_tabcontent.cpp's dispatch; creature_pick
// is exposed separately for its FUNCTESTING hook.

#ifdef __cplusplus

#include "globals.h"   // ThingModel

void creature_list(void);
void query_panel(void);
void creature_query_panel(void);
void spell_lost_panel(void);

// GUI_POSITION Bottom (docs/refactor/ingame-gui/11-horizontal-layout.md).
void creature_list_horizontal(void);
void creature_query_panel_horizontal(void);
void query_panel_horizontal(void);

void creature_pick(ThingModel crmodel, int64_t gj, bool pick_up);

#endif // __cplusplus
#endif // FRONTGUI_INGAME_CREATURE_H
