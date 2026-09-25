#ifndef FRONTGUI_STYLE_H
#define FRONTGUI_STYLE_H

#include "bflib_basics.h" // TbBool

struct ImGuiCursorImage; // gui/FrontendImGui.h -- forward declared only, see .cpp

// Typography and ImGuiStyle for the ImGui frontend
// (docs/refactor/renderer/04-imgui-gui-foundation.md §4, Phase B). Pure
// C++: nothing in kfx_frontend's C code calls into this yet (no real
// screen has migrated -- Phase C), only frontgui_widgets.cpp and the
// Phase B style-sheet test screen.

// The type scale (§5.2's FeHeading/FeSubheading/FeBodyText/FeCaption
// wrappers push one of these). Exocet/Cinzel only ship two usable weights
// (Light/Heavy, or Cinzel's Regular/Black stand-ins for them -- §4.1), so
// Heading/Subheading share the heavy face and Body/Caption share the light
// face, differing only in size.
enum FeFontRole {
    FeFont_Heading = 0,
    FeFont_Subheading,
    FeFont_Body,
    FeFont_Caption,
    FeFont_COUNT,
};

// Loads the display face -- Exocet from fxdata/ if the installer copied it
// in (EXH_____.TTF/EXL_____.TTF, §4.1), the bundled Cinzel static
// instances otherwise (Cinzel-Black.ttf/Cinzel-Regular.ttf as the
// heavy/light stand-ins) -- and applies the KeeperFX ImGuiStyle. Call once
// after the ImGui context exists (e.g. from the Phase B style-sheet
// screen's first frame, or FeStyleEnsureInit() below), before the first
// FeStylePushFont call.
void FeStyleInit();

// FeStyleInit() is idempotent and safe to call every frame: it no-ops
// once the fonts are loaded. Prefer this over calling FeStyleInit()
// directly from a call site that runs every frame.
void FeStyleEnsureInit();

// Pushes/pops the given role's font at a size derived from the current
// ImGui display height (§4.3) -- safe to call every frame, no atlas
// rebuild cost: 1.92's dynamic font system rasterizes glyphs on demand at
// whatever size is requested, so resizing the window just changes the
// size passed here, not the loaded font set.
void FeStylePushFont(FeFontRole role);
void FeStylePopFont();
// Test-only: make FeStylePushFont/FeStylePopFont balanced no-ops (ImGui's default font stays
// active) so screen code can run in a headless unit-test frame with no game fonts on disk.
void FeStyleTestUseDefaultFont(bool use_default);

// True once Exocet was found and loaded; false while running on the
// Cinzel fallback (§4.1 point 1 -- the fallback is the common case and the
// style must look deliberate either way, not degraded).
bool FeStyleUsingExocet();

// RendererCursorImageFn-shaped (RendererManager.h): renders the classic
// MousePG_Arrow pointer (pointer_sprites, vidmode.h/.c -- the same plain
// arrow already shipped for the in-game cursor, hotspot (12, 15) taken
// verbatim from set_pointer_graphic()'s own MousePG_Arrow case, vidmode.c)
// into an off-screen RGBA buffer via RendererSwapFramebufferTarget, the
// same seam the eye-lens effect uses -- so the ImGui-drawn cursor (found
// live: ImGui's own built-in one is a generic arrow, mismatched against
// the game's actual cursor everywhere else) matches it exactly. Called
// directly from FrontendImGui.cpp's per-frame cursor refresh (same
// library, no callback needed).
TbBool FeStyleGetCursorImage(struct ImGuiCursorImage *out);

// docs/refactor/renderer/05-imgui-owned-menu-backdrop.md Phase A: decodes
// frontend_background (gui_draw.h -- the static 640x480 indexed bitmap
// behind every ImGui-migrated menu screen) through frontend_palette once,
// uploads it to a texture via RendererCreateDynamicTexture()/
// RendererUpdateDynamicTexture() (RendererManager.h -- same idiom
// frontgui_screens.cpp's land-preview panel already uses, just built once
// and never re-uploaded since the source image never changes), and caches
// the result for the life of the process. Returns nullptr (leaving
// *out_w/*out_h untouched) until frontend_background/frontend_palette are
// actually ready -- same retry-until-ready shape as FeStyleGetCursorImage
// above -- so callers must tolerate a null result on early frames.
void *FeStyleGetMenuBackdropTexture(int64_t *out_w, int64_t *out_h);

#endif // FRONTGUI_STYLE_H
