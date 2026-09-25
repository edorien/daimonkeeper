#ifndef FRONTGUI_INGAME_ICON_OVERRIDES_H
#define FRONTGUI_INGAME_ICON_OVERRIDES_H

#include <stdint.h>
// docs/refactor/ingame-gui/12-png-icon-overrides.md: user PNG icon packs
// (fxdata/gui/<pack>/, the GUI_ICON_PACK setting) overriding the legacy
// sprites this project's ImGui HUD draws, plus the procedural marble panel
// background. A pack is just a folder of PNGs named per the doc's naming
// scheme -- no manifest, no JSON. Every lookup here is a pure add-on:
// nullptr always means "GUI_ICON_PACK is NONE, or this name isn't in the
// active pack" -- callers fall back to their existing legacy-sprite path
// unchanged, never treat nullptr as an error.

#ifdef __cplusplus

// Raw lookup: `name` is the exact PNG basename (no extension; lower-cased
// internally, so callers don't have to) to look for in the active pack --
// e.g. "job_idle", "battle_vs", "background_horizontal". Returns nullptr
// immediately if GUI_ICON_PACK == "NONE". Cheap on repeat calls -- both
// hits and misses are cached by name, so a pack with no match for `name`
// only touches the filesystem once per name per pack selection.
void *FeIconOverrideTexture(const char *name, int64_t *out_w, int64_t *out_h);

// `<category>_<code_name>_active` / `_inactive` lookup for the data-driven
// categories with an affordable/unaffordable or ready/on-cooldown split
// (room_/power_/trap_/ability_, doc §2/§3.2). Tries the requested variant;
// if `active` is false and no "_inactive" file exists, falls back to the
// "_active" file and sets *out_dim so the caller applies the same
// alpha-dim tint it would over the legacy sprite (doc §3.3) -- so a
// pack supplying only "_active" PNGs still looks reasonable. `code_name`
// is lower-cased internally; `category` is assumed already lower-case
// (every call site passes a literal).
void *FeIconOverrideActiveInactive(const char *category, const char *code_name, bool active,
                                   int64_t *out_w, int64_t *out_h, bool *out_dim);

// `<category>_<code_name>` lookup for the data-driven categories with no
// active/inactive split (creature_icon_/creature_portrait_).
void *FeIconOverrideSingle(const char *category, const char *code_name, int64_t *out_w, int64_t *out_h);

// Static sprite-index -> friendly-name lookup (doc §3.1: job_*/tendency_*/
// bar_*/battle_vs/confirm_*/launcher_*, every one of them already a fixed
// compile-time GPS_*/GBS_* constant). Checked from
// FeSpriteTexture()/FeGuiPanelTexture() (frontgui_sprite_tex.cpp) before
// their legacy resolve() path, so none of those categories' *other* call
// sites need any change -- they keep passing the same legacy sprite_idx
// they always have. `is_button_sheet` must match which of
// FeSpriteTexture/FeGuiPanelTexture is calling -- the GBS_ (button_sprite[])
// and GPS_ (panel_sprite[]) index spaces overlap numerically.
void *FeIconOverrideForStaticIndex(int64_t sprite_idx, bool is_button_sheet, int64_t *out_w, int64_t *out_h);

#endif // __cplusplus
#endif // FRONTGUI_INGAME_ICON_OVERRIDES_H
