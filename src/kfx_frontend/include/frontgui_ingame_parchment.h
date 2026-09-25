#ifndef FRONTGUI_INGAME_PARCHMENT_H
#define FRONTGUI_INGAME_PARCHMENT_H

#include "globals.h" // TbBool

// Phase 7 (docs/refactor/ingame-gui/08-parchment-map.md): the full-screen
// parchment / overhead-map view as ImGui. Chunk 1 keeps the legacy
// framebuffer raster (parchment paper + overhead map + zoom box) and adds
// an ImGui overlay -- crisp level name + a frame around the map area.
// Later chunks route the map through a dynamic texture and migrate the
// zoom box + click-to-recentre.

#ifdef __cplusplus
extern "C" {
#endif

void ingame_parchment_frame(void);

// True while the ImGui parchment view owns the whole screen -- checked by
// FrontendImGuiScreenOwned() (gui/FrontendImGui.cpp), the screen_owned
// member of main.cpp's RendererImGuiCallbacks, so PresentFrame() skips the
// (now-empty) framebuffer blit and ImGui draws the cursor itself.
TbBool ingame_parchment_active(void);

#ifdef __cplusplus
}
#endif

#endif // FRONTGUI_INGAME_PARCHMENT_H
