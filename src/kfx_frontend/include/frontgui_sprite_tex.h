#ifndef FRONTGUI_SPRITE_TEX_H
#define FRONTGUI_SPRITE_TEX_H

#include <stdint.h>
// Renders a classic GUI button sprite (get_button_sprite() / the GBS_*
// indices in sprites.h) into a GPU texture the ImGui menu layer can draw,
// and a button widget built on top of it. The off-screen-render-then-
// composite technique is the same one the ImGui cursor and land-preview
// panel use (RendererSwapFramebufferTarget, frontgui_style.cpp /
// frontgui_screens.cpp) -- here it turns a single static sprite into a
// cached texture, built once, keyed by sprite index.
//
// C++ only: the widget takes ImGui-flavoured defaults and this whole
// layer sits above <imgui.h> like frontgui_widgets.h.
#ifdef __cplusplus

// Lazily renders button sprite `sprite_idx` into a cached texture and
// returns its RendererCreateDynamicTexture handle. Returns nullptr until
// the button spritesheet is loaded and the render has succeeded -- safe to
// call every frame, it retries until it can build and caches thereafter.
// *out_w / *out_h receive the sprite's pixel size when non-null (set even
// on a frame the texture is not ready yet, once the sprite itself exists).
void *FeSpriteTexture(int64_t sprite_idx, int64_t *out_w, int64_t *out_h);

// Same, for a GUI *panel* sprite (get_panel_sprite() -- the GPS_* indices),
// cached separately since the panel and button index spaces overlap. Used
// by the in-game message queue's per-type icons.
void *FeGuiPanelTexture(int64_t sprite_idx, int64_t *out_w, int64_t *out_h);

// True when FeGuiPanelTexture() can produce a real picture for this panel sprite right now: the in-game
// sheet has it, or -- in the menu, where that sheet is not loaded -- the base sheet has it (custom
// campaign/mod icons never do until a level has loaded their zips). Use it to fall back to a text tile
// instead of the engine's magenta checkerboard placeholder.
bool FeGuiPanelSpriteAvailable(int64_t sprite_idx);

// Drops the menu's private copy of the panel sheet (loaded lazily by FeGuiPanelTexture() when the
// in-game sheet is absent). Normally unnecessary -- it is released automatically once the in-game sheet
// exists -- but lets a caller (and tests) return to the "nothing loaded" state.
void FeGuiPanelReleaseMenuSheet();

// A menu button drawn as a GUI sprite icon, with an optional localized text
// label to the icon's right (label == nullptr -> icon only). Mirrors
// fe_text_button()'s behaviour: transparent hit box, blood-red highlight
// while hovered / nav-focused, menu click sound on release, global-alpha
// aware so BeginDisabled() dims it. `icon_h` is the icon height in px
// (0 -> one line of the body font); width keeps the sprite's aspect ratio.
// Falls back to a plain text button until the texture is ready (or forever,
// if the sprite can't be found) so the menu is never blank.
// Returns true on click.
bool FeSpriteButton(const char *str_id, int64_t sprite_idx, const char *label, double icon_h = 0.0);

// Icon-only panel-sprite button: no label drawn beside the icon, but
// `fallback_label` is shown as a text button until the texture is ready.
bool FeGuiPanelIconButton(const char *str_id, int64_t sprite_idx, const char *fallback_label, double icon_h = 0.0);

// The layout width FeSpriteButton() will occupy for the same args -- for a
// caller that wants to centre a row of sprite buttons itself. 0 if the
// sprite can't be found (the widget would fall back to a text button, whose
// width the caller can get from ImGui directly).
double FeSpriteButtonWidth(int64_t sprite_idx, const char *label, double icon_h = 0.0);

#endif // __cplusplus
#endif // FRONTGUI_SPRITE_TEX_H
