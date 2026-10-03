/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_settingschema.c
 * @par Purpose:
 *     Declarative schema for keeperfx.cfg-backed settings options -- see
 *     the header for the design. Every option from §6.2's list that fits
 *     SOptT_Bool/Int/Enum is here (35 rows as of Phase G step 12);
 *     SOptT_Float/String/Keybind/Composite are still unimplemented (nothing
 *     currently in the schema needs them -- every option that looked like
 *     it might turned out to fit an existing type once its real
 *     config-parser semantics were traced, see individual rows' own
 *     comments below). Every row needs no new cross-layer plumbing beyond
 *     what Phase G's own steps already added: kfx_config already owns
 *     features_enabled/start_params/keeperfx_ui_config/kfx_config_state
 *     directly, and bflib_mouse.h's lbMouseGrab/bflib_video.h's display_id
 *     and screen-mode registry/bflib_sound.h's atmos_sound_volume/
 *     platform/PlatformManager.h's display-mode enumeration all live in
 *     kfx_platform, below kfx_config in the layering, so they're plain
 *     includes. A few rows (SCREENSHOT, HAND_SIZE, POINTER_SENSITIVITY)
 *     needed a new getter added to SimPort (config.h) because
 *     the value was previously only ever written, never read back.
 * @par Comment:
 *     None.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "config_settingschema.h"

#include "config_keeperfx.h"
#include "config_settings.h" // struct GameSettings settings, save_settings() -- SHADOWS/VIEW_DISTANCE
#include "config_strings.h"
#include "kfx_config_state.h"
#include "bflib_mouse.h"
#include "bflib_video.h"
#include "bflib_sound.h" // atmos_sound_volume
#include "bflib_fmvids.h" // SMK_FullscreenFit/Stretch/Crop -- RESIZE_MOVIES
#include "bflib_basics.h" // ERRORLOG
#include "bflib_fileio.h" // LbFileExists -- UI_FONT's Exocet probe
#include "globals.h"      // FGrp_FxData
#include "config.h"       // prepare_file_path -- UI_FONT's fxdata/font/ scan
#include "platform/PlatformManager.h" // INGAME_RES modes, UI_FONT sub-dir scan
#include "renderer/RendererManager.h" // RENDERER -- RendererGetDesiredType/RendererSetDesiredType

#include <stdio.h>
#include <string.h>
#include <strings.h> // strcasecmp -- UI_FONT token match
#include "ports/game_port.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

static TbBool get_autosave_replays(void) { return autosave_replays; }
static void set_autosave_replays(TbBool val) { autosave_replays = val; }

static TbBool get_freeze_on_focus_lost(void) { return is_feature_on(Ft_FreezeOnLoseFocus); }
static void set_freeze_on_focus_lost(TbBool val)
{
    if (val) features_enabled |= Ft_FreezeOnLoseFocus;
    else features_enabled &= ~Ft_FreezeOnLoseFocus;
}

static TbBool get_pause_music_on_pause(void) { return is_feature_on(Ft_PauseMusicOnGamePause); }
static void set_pause_music_on_pause(TbBool val)
{
    if (val) features_enabled |= Ft_PauseMusicOnGamePause;
    else features_enabled &= ~Ft_PauseMusicOnGamePause;
}

static TbBool get_mute_audio_on_focus_lost(void) { return is_feature_on(Ft_MuteAudioOnLoseFocus); }
static void set_mute_audio_on_focus_lost(TbBool val)
{
    if (val) features_enabled |= Ft_MuteAudioOnLoseFocus;
    else features_enabled &= ~Ft_MuteAudioOnLoseFocus;
}

// "-altinput"/ALT_INPUT disables the mouse auto-grab/reset behaviour, so
// the option's own on/off sense is the *inverse* of lbMouseGrab -- see
// main.cpp's "altinput" cmdline handler and config_keeperfx.c's ALT_INPUT
// case, both of which set lbMouseGrab = false for "alt input on".
static TbBool get_alt_input(void) { return !lbMouseGrab; }
static void set_alt_input(TbBool val) { lbMouseGrab = !val; }

// §6.2 finding 4: alt input enables the possession-cursor-lock option and
// disables the unlock-when-paused one, and vice versa.
static TbBool get_unlock_cursor_on_pause(void) { return is_feature_on(Ft_UnlockCursorOnPause); }
static void set_unlock_cursor_on_pause(TbBool val)
{
    if (val) features_enabled |= Ft_UnlockCursorOnPause;
    else features_enabled &= ~Ft_UnlockCursorOnPause;
}
static TbBool unlock_cursor_on_pause_enabled(void) { return lbMouseGrab; } // enabled while alt input is off

static TbBool get_lock_cursor_in_possession(void) { return is_feature_on(Ft_LockCursorInPossession); }
static void set_lock_cursor_in_possession(TbBool val)
{
    if (val) features_enabled |= Ft_LockCursorInPossession;
    else features_enabled &= ~Ft_LockCursorInPossession;
}
static TbBool lock_cursor_in_possession_enabled(void) { return !lbMouseGrab; } // enabled while alt input is on

static int64_t get_gui_blink_rate(void) { return keeperfx_ui_config.gui_blink_rate; }
static void set_gui_blink_rate(int64_t val) { keeperfx_ui_config.gui_blink_rate = (int64_t)val; }

static int64_t get_creature_status_size(void) { return keeperfx_ui_config.creature_status_size; }
static void set_creature_status_size(int64_t val) { keeperfx_ui_config.creature_status_size = (int64_t)val; }

// DISPLAY_NUMBER is stored 0-based (display_id) but shown to the user
// 1-based ("1 for the main monitor, 2 for the second monitor, ..." --
// config/keeperfx.cfg's own comment); the schema mirrors that convention
// rather than exposing the raw 0-based storage.
static int64_t get_display_number(void) { return (int64_t)display_id + 1; }
static void set_display_number(int64_t val) { display_id = (int64_t)(val - 1); }

// UI_FONT_SCALE (KeeperFX-only, no equivalent in original DK): a percentage
// multiplier FeStylePushFont (frontgui_style.cpp) applies on top of its own
// resolution-derived base size -- SApply_Live because that function reads
// keeperfx_ui_config.ui_font_scale_pct fresh every call, no restart needed.
//
// A curated set of "standard" sizes (SOptT_Enum) rather than a free-slider
// percentage (found live: a continuous 50-200 range let the player land on
// values that didn't rasterize particularly crisply -- see FeStylePushFont's
// own comment on the pixel-rounding fix that turned out insufficient on its
// own). .name is the literal percentage as a string, not a fancier label
// like "Large": setting_option_apply_enum_index() (config_settingschema.c)
// writes enum_table[index].name straight to keeperfx.cfg, and this key's
// own parser (config_keeperfx.c case 52) reads it back with plain atoi(),
// so the persisted format has to stay a bare number -- unchanged from
// before this row was an enum, and unchanged for any existing keeperfx.cfg
// already carrying a UI_FONT_SCALE line from the old slider.
static const struct NamedCommand ui_font_scale_enum[] = {
    {"50",  50},
    {"75",  75},
    {"100", 100},
    {"125", 125},
    {"150", 150},
    {"200", 200},
    {NULL,  0},
};
static int64_t get_ui_font_scale(void) { return keeperfx_ui_config.ui_font_scale_pct; }
static void set_ui_font_scale(int64_t val) { keeperfx_ui_config.ui_font_scale_pct = (int64_t)val; }

// GUI_POSITION (case 55, config_keeperfx.c): reuses hud_position_type[]
// (config_keeperfx.c) verbatim, the same way SCREENSHOT reuses scrshot_type[]
// above -- no alias/legacy-token concerns here to justify a separate table.
static int64_t get_hud_position(void) { return keeperfx_ui_config.hud_position; }
static void set_hud_position(int64_t val) { keeperfx_ui_config.hud_position = (int64_t)val; }

// MINIMAP_CORNER (case 57, config_keeperfx.c): reuses minimap_corner_type[]
// verbatim, same shape as GUI_POSITION above. Only meaningful when
// GUI_POSITION is MINIMAL -- is_enabled shows the row but greys it out
// otherwise (docs/refactor/ingame-gui/13-minimal-layout.md).
static int64_t get_minimap_corner(void) { return keeperfx_ui_config.minimap_corner; }
static void set_minimap_corner(int64_t val) { keeperfx_ui_config.minimap_corner = (int64_t)val; }
static TbBool minimap_corner_enabled(void) { return keeperfx_ui_config.hud_position == 4; }

// PANEL_CORNER (case 59, config_keeperfx.c): the button cluster + pop-up
// panel's own (always-bottom) corner, independent of MINIMAP_CORNER above --
// reuses panel_corner_type[]'s LOWER_LEFT/LOWER_RIGHT tokens rather than
// minimap_corner_type[]'s UPPER_* ones, since this cluster only ever sits on
// the bottom edge. Same is_enabled gate.
static int64_t get_panel_corner(void) { return keeperfx_ui_config.panel_corner; }
static void set_panel_corner(int64_t val) { keeperfx_ui_config.panel_corner = (int64_t)val; }
static TbBool panel_corner_enabled(void) { return keeperfx_ui_config.hud_position == 4; }

// UI_FONT (case 53, config_keeperfx.c): the ImGui-frontend typeface. Like
// INGAME_RES, its list can't be a compile-time constant -- it's "AUTO",
// "Cinzel" (bundled), "Exocet" (only when the DK2 files are present in
// fxdata/), then one entry per family sub-directory the user has dropped
// into fxdata/font/. .name is the literal keeperfx.cfg token (the family
// dir name, or "AUTO"/"CINZEL"/"EXOCET"); .num is the row index.
// SApply_NeedsRestart -- swapping the live font atlas mid-session is a
// separate piece of work; frontgui_style.cpp reads this at startup.
#define UI_FONT_MAX_ENTRIES 32
#define UI_FONT_NAME_LEN 48
static struct NamedCommand ui_font_enum[UI_FONT_MAX_ENTRIES + 1];
static char ui_font_names[UI_FONT_MAX_ENTRIES][UI_FONT_NAME_LEN];
static TbBool ui_font_enum_ready = false;

static void ui_font_add(int64_t *n, const char *name)
{
    if (*n >= UI_FONT_MAX_ENTRIES)
        return;
    snprintf(ui_font_names[*n], UI_FONT_NAME_LEN, "%s", name);
    ui_font_enum[*n].name = ui_font_names[*n];
    ui_font_enum[*n].num = *n;
    (*n)++;
}

static void ensure_ui_font_enum(void)
{
    if (ui_font_enum_ready)
        return;
    ui_font_enum_ready = true;

    int64_t n = 0;
    ui_font_add(&n, "AUTO");
    ui_font_add(&n, "CINZEL");

    char *exh = prepare_file_path(FGrp_FxData, "EXH_____.TTF");
    TbBool have_exh = (exh != NULL) && LbFileExists(exh);
    char *exl = prepare_file_path(FGrp_FxData, "EXL_____.TTF");
    if (have_exh && (exl != NULL) && LbFileExists(exl))
        ui_font_add(&n, "EXOCET");

    char *font_dir = prepare_file_path(FGrp_FxData, "font");
    if (font_dir != NULL)
    {
        char subs[UI_FONT_MAX_ENTRIES][UI_FONT_NAME_LEN];
        int64_t count = PlatformManager_ListSubdirectories(font_dir, &subs[0][0],
                                                       UI_FONT_NAME_LEN, UI_FONT_MAX_ENTRIES);
        for (int64_t i = 0; i < count; i++)
        {
            // Skip the bundled Cinzel dir -- already offered as "Cinzel".
            if (strcasecmp(subs[i], "Cinzel") == 0)
                continue;
            ui_font_add(&n, subs[i]);
        }
    }

    ui_font_enum[n].name = NULL;
    ui_font_enum[n].num = 0;
}

static int64_t get_ui_font(void)
{
    ensure_ui_font_enum();
    for (int64_t i = 0; ui_font_enum[i].name != NULL; i++)
        if (strcasecmp(ui_font_enum[i].name, keeperfx_ui_config.ui_font) == 0)
            return i;
    return 0; // AUTO
}

static void set_ui_font(int64_t idx)
{
    ensure_ui_font_enum();
    if ((idx < 0) || (idx >= UI_FONT_MAX_ENTRIES) || (ui_font_enum[idx].name == NULL))
        return;
    snprintf(keeperfx_ui_config.ui_font, sizeof(keeperfx_ui_config.ui_font),
             "%s", ui_font_enum[idx].name);
}

// GUI_ICON_PACK (case 56, config_keeperfx.c,
// docs/refactor/ingame-gui/12-png-icon-overrides.md): same shape as UI_FONT
// above -- "NONE" (default, legacy sprites only) plus one entry per
// sub-directory of fxdata/gui/, each a pack of same-named PNGs overriding
// the matching HUD icon (frontgui_ingame_icon_overrides.cpp resolves the
// actual lookups; this row just offers the folder names). Unlike UI_FONT,
// not frontend_only -- clearing the (cheap) texture cache mid-session is
// safe, so the icon-override module polls this the same lazy way
// frontgui_style.cpp's refresh_fonts_if_changed() polls UI_FONT.
//
// "CLASSIC" is a second reserved entry (alongside "NONE"), added when the
// `-classicmenu` switch was retired: that switch used to take the whole
// session -- frontend menus and in-game HUD together -- to the pre-ImGui
// sprite path. The frontend side of that is gone for good (every FeSt_*
// screen is ImGui now), but the in-game HUD's legacy sprite renderer is
// kept available as a per-session *style* choice instead -- this is a much
// smaller axis than a full rendering-architecture switch, but the in-game
// HUD is the one piece of the old sprite GUI actually worth keeping
// reachable (some players prefer it outright). ingame_gui_use_classic_hud()
// (config_keeperfx.h/.c) is what every in-game HUD draw call now checks
// instead of the old RendererImGuiEnabled() to decide which renderer to
// use for *this one session* -- except the options/quit/load/save family
// (frontgui_ingame.cpp's menu_is_options_family()), which stays on the
// modern ImGui screens regardless: those are self-contained popups with no
// dependency on sidebar layout, so classic-sidebar players still get them.
// Reserved the same way "NONE" is -- added unconditionally before the
// folder scan below, not sourced from an actual fxdata/gui/CLASSIC/
// directory.
#define GUI_ICON_PACK_MAX_ENTRIES 32
#define GUI_ICON_PACK_NAME_LEN 48
static struct NamedCommand gui_icon_pack_enum[GUI_ICON_PACK_MAX_ENTRIES + 1];
static char gui_icon_pack_names[GUI_ICON_PACK_MAX_ENTRIES][GUI_ICON_PACK_NAME_LEN];
static TbBool gui_icon_pack_enum_ready = false;

static void gui_icon_pack_add(int64_t *n, const char *name)
{
    if (*n >= GUI_ICON_PACK_MAX_ENTRIES)
        return;
    // Skip a folder that happens to collide (case-insensitively) with one
    // of the two reserved names above -- added first, so this only ever
    // catches the folder-scan loop below trying to add a duplicate.
    for (int64_t i = 0; i < *n; i++)
        if (strcasecmp(gui_icon_pack_enum[i].name, name) == 0)
            return;
    snprintf(gui_icon_pack_names[*n], GUI_ICON_PACK_NAME_LEN, "%s", name);
    gui_icon_pack_enum[*n].name = gui_icon_pack_names[*n];
    gui_icon_pack_enum[*n].num = *n;
    (*n)++;
}

static void ensure_gui_icon_pack_enum(void)
{
    if (gui_icon_pack_enum_ready)
        return;
    gui_icon_pack_enum_ready = true;

    int64_t n = 0;
    gui_icon_pack_add(&n, "NONE");

    char *gui_dir = prepare_file_path(FGrp_FxData, "gui");
    if (gui_dir != NULL)
    {
        char subs[GUI_ICON_PACK_MAX_ENTRIES][GUI_ICON_PACK_NAME_LEN];
        int64_t count = PlatformManager_ListSubdirectories(gui_dir, &subs[0][0],
                                                       GUI_ICON_PACK_NAME_LEN, GUI_ICON_PACK_MAX_ENTRIES);
        for (int64_t i = 0; i < count; i++)
            gui_icon_pack_add(&n, subs[i]);
    }

    gui_icon_pack_enum[n].name = NULL;
    gui_icon_pack_enum[n].num = 0;
}

static int64_t get_gui_icon_pack(void)
{
    ensure_gui_icon_pack_enum();
    for (int64_t i = 0; gui_icon_pack_enum[i].name != NULL; i++)
        if (strcasecmp(gui_icon_pack_enum[i].name, keeperfx_ui_config.gui_icon_pack) == 0)
            return i;
    return 0; // NONE
}

static void set_gui_icon_pack(int64_t idx)
{
    ensure_gui_icon_pack_enum();
    if ((idx < 0) || (idx >= GUI_ICON_PACK_MAX_ENTRIES) || (gui_icon_pack_enum[idx].name == NULL))
        return;
    snprintf(keeperfx_ui_config.gui_icon_pack, sizeof(keeperfx_ui_config.gui_icon_pack),
             "%s", gui_icon_pack_enum[idx].name);
}

// Reset Progress (Phase E, docs/refactor/gui/05-campaign-progress-and-landview.md
// §3.5): SOptT_Action's only row so far. reset_all_campaign_progress()
// itself is kfx_game-owned (save/progress.cfg); reached the same way
// every other "kfx_config needs something from above" case in this
// schema is, through SimPort (config.h) -- not a
// bespoke callback struct just for this one row.
static void reset_campaign_progress_action(void) { game_reset_campaign_progress(); }

// SHADOWS/VIEW_DISTANCE: the in-game "Video" options menu's own
// shadows/view_distance toggles (frontmenu_ingame_opts_data.cpp's
// video_menu_buttons -- gui_video_shadows()/gui_video_view_distance_level()
// in frontmenu_options.c) were missing from this route entirely -- both are
// plain rendering preferences with no active-game dependency (unlike that
// same menu's rotate_mode/cluedo_mode/gamma_correction, which are routed
// through the packet/command system and need a live player, so aren't
// modeled here). Both read/write struct GameSettings (config_settings.h)
// directly -- already kfx_config-owned state, same layer as this file --
// and persist via save_settings() to save/settings.toml, not keeperfx.cfg
// (see persist_via_save_settings's own doc comment, config_settingschema.h).
// Range 0-3 for both, matching config_settings.c's own clamp() on load.
static int64_t get_video_shadows(void) { return settings.video_shadows; }
static void set_video_shadows(int64_t val) { settings.video_shadows = (unsigned char)val; }
static int64_t get_view_distance(void) { return settings.view_distance; }
static void set_view_distance(int64_t val) { settings.view_distance = (unsigned char)val; }

static TbBool get_easter_egg(void) { return start_params.easter_egg; }
static void set_easter_egg(TbBool val) { start_params.easter_egg = val; }

static TbBool get_censorship(void) { return is_feature_on(Ft_Censorship); }
static void set_censorship(TbBool val)
{
    if (val) features_enabled |= Ft_Censorship;
    else features_enabled &= ~Ft_Censorship;
}

static TbBool get_flee_button_default(void) { return FLEE_BUTTON_DEFAULT; }
static void set_flee_button_default(TbBool val) { FLEE_BUTTON_DEFAULT = val; }

static TbBool get_imprison_button_default(void) { return IMPRISON_BUTTON_DEFAULT; }
static void set_imprison_button_default(TbBool val) { IMPRISON_BUTTON_DEFAULT = val; }

static int64_t get_line_box_size(void) { return keeperfx_ui_config.line_box_size; }
static void set_line_box_size(int64_t val) { keeperfx_ui_config.line_box_size = (int64_t)val; }

static int64_t get_neutral_flash_rate(void) { return keeperfx_ui_config.neutral_flash_rate; }
static void set_neutral_flash_rate(int64_t val) { keeperfx_ui_config.neutral_flash_rate = (int64_t)val; }

static TbBool get_atmospheric_sounds(void) { return is_feature_on(Ft_Atmossounds); }
static void set_atmospheric_sounds(TbBool val)
{
    if (val) features_enabled |= Ft_Atmossounds;
    else features_enabled &= ~Ft_Atmossounds;
}

// The launcher's "Use CD Music" is the *inverse* of Ft_NoCdMusic
// (config_keeperfx.c's MUSIC_FROM_DISK case, config key 29: MUSIC_FROM_DISK
// = TRUE sets Ft_NoCdMusic, i.e. "don't use CD"). cfg_bool_inverted on this
// row's own SettingOption entry is what makes setting_option_apply_bool()
// write MUSIC_FROM_DISK's opposite polarity when this displayed value is
// toggled -- see the header's own comment.
static TbBool get_use_cd_music(void) { return !is_feature_on(Ft_NoCdMusic); }
static void set_use_cd_music(TbBool val)
{
    if (val) features_enabled &= ~Ft_NoCdMusic;
    else features_enabled |= Ft_NoCdMusic;
}

// CURSOR_EDGE_CAMERA_PANNING's own config key (case 24) already has "true
// enables panning" semantics against the inverted Ft_DisableCursor...
// flag -- no cfg_bool_inverted needed, only the underlying flag name reads
// backwards.
static TbBool get_cursor_edge_camera_panning(void) { return !is_feature_on(Ft_DisableCursorCameraPanning); }
static void set_cursor_edge_camera_panning(TbBool val)
{
    if (val) features_enabled &= ~Ft_DisableCursorCameraPanning;
    else features_enabled |= Ft_DisableCursorCameraPanning;
}

static TbBool get_tag_mode_toggling(void) { return keeperfx_ui_config.right_click_tag_mode_toggle; }
static void set_tag_mode_toggling(TbBool val) { keeperfx_ui_config.right_click_tag_mode_toggle = val; }

static int64_t get_atmos_volume(void) { return atmos_sound_volume; }
static void set_atmos_volume(int64_t val) { atmos_sound_volume = (int64_t)val; }

static int64_t get_atmos_frequency(void) { return kfx_config_state.atmos_sound_frequency; }
static void set_atmos_frequency(int64_t val) { kfx_config_state.atmos_sound_frequency = (int64_t)val; }

static int64_t get_default_tag_mode(void) { return keeperfx_ui_config.default_tag_mode; }
static void set_default_tag_mode(int64_t val) { keeperfx_ui_config.default_tag_mode = (int64_t)val; }

static int64_t get_language(void) { return install_info.lang_id; }
static void set_language(int64_t val) { install_info.lang_id = (int64_t)val; }

static TbBool get_delta_time(void) { return is_feature_on(Ft_DeltaTime); }
static void set_delta_time(TbBool val)
{
    if (val) features_enabled |= Ft_DeltaTime;
    else features_enabled &= ~Ft_DeltaTime;
}

// ZOOM_TO_MOUSE/ROTATE_AROUND_MOUSE's own config-key parsing (case 42/43,
// config_keeperfx.c) checks logicval_type for ALWAYS/NEVER *before* falling
// back to a dedicated table for the WHEEL/rotation-key tokens -- there's no
// single existing NamedCommand table covering the whole value space the way
// atmos_freq[]/tag_modes[] do for their options. These two purpose-built
// tables exist only for the schema: their names are literal tokens the
// parser's combined ALWAYS/NEVER-then-fallback logic already accepts, and
// their .num values match keeperfx_ui_config.zoom_to_mouse_option/
// rotate_around_mouse_option's own storage (confirmed by tracing the
// parser) -- e.g. writing "ALWAYS" here round-trips to zoom_to_mouse_option
// == 3 (ZoomToMouse_Always) exactly like a hand-written keeperfx.cfg line
// would.
static const struct NamedCommand zoom_to_mouse_enum[] = {
    {"NEVER", 1}, // ZoomToMouse_Never
    {"WHEEL", 2}, // ZoomToMouse_Wheel
    {"ALWAYS", 3}, // ZoomToMouse_Always
    {NULL, 0},
};
static int64_t get_zoom_to_mouse(void) { return keeperfx_ui_config.zoom_to_mouse_option; }
static void set_zoom_to_mouse(int64_t val) { keeperfx_ui_config.zoom_to_mouse_option = (int64_t)val; }

static const struct NamedCommand rotate_around_mouse_enum[] = {
    {"NEVER", 1}, // RotateAroundMouse_Never
    {"ROTATION_KEYS", 2},
    {"MOVEMENT_KEYS", 3},
    {"ALWAYS", 4}, // RotateAroundMouse_Always
    {NULL, 0},
};
static int64_t get_rotate_around_mouse(void) { return keeperfx_ui_config.rotate_around_mouse_option; }
static void set_rotate_around_mouse(int64_t val) { keeperfx_ui_config.rotate_around_mouse_option = (int64_t)val; }

// SCREENSHOT reuses scrshot_type[] (config_keeperfx.c) the same way
// ATMOS_FREQUENCY reuses atmos_freq[] -- but unlike every other row so far,
// screenshot_format (kfx_render-owned, scrcapt.h) only had a *setter*
// callback (config_keeperfx.c's own SCREENSHOT config-key case has never
// needed to read it back). Added get_screenshot_format to
// SimPort (config.h) alongside the existing
// set_screenshot_format for this row to have something to read the
// current value from.
static int64_t get_screenshot_format(void) { return kfx_runtime_settings.screenshot_format; }
static void set_screenshot_format_val(int64_t val) { kfx_runtime_settings.screenshot_format = (unsigned char)val; }

// HAND_SIZE's own config-key parsing (case 30, config_keeperfx.c) already
// reads/writes it as an integer *percentage* (atoi(word_buf), then
// set_hand_scale(i/100.0)) even though the live engine value
// (global_hand_scale) is a float scale factor -- so this is a plain
// SOptT_Int row keyed on the same percentage the file already uses, not a
// new float schema type. Needed a new get_hand_scale callback the same way
// SCREENSHOT needed get_screenshot_format -- config_keeperfx.c's own
// parser only ever wrote this value, never read it back.
static int64_t get_hand_size_pct(void) { return (int64_t)(kfx_runtime_settings.hand_scale * 100.0 + 0.5); }
static void set_hand_size_pct(int64_t val) { kfx_runtime_settings.hand_scale = (double)val / 100.0; }

// RESIZE_MOVIES's own config-key parsing (case 14, config_keeperfx.c) is
// two storage locations combined into one option: Ft_Resizemovies (on/off)
// plus vid_scale_flags (which scaling mode, only meaningful while the
// feature is on). vidscale_type[] (config_keeperfx.c) itself isn't a clean
// fit for a combo box -- it's an *alias* table for the parser ("ON"/
// "ENABLED"/"TRUE"/"YES"/"1" all mean the same thing as "FIT"), so reusing
// it verbatim would show duplicate-valued entries. This purpose-built local
// table keeps just the canonical one name per distinct value, the same
// "not every table is fit to reuse" reasoning ZOOM_TO_MOUSE/
// ROTATE_AROUND_MOUSE's own tables were built for.
static const struct NamedCommand resize_movies_enum[] = {
    {"OFF", 0},
    {"FIT", SMK_FullscreenFit},
    {"STRETCH", SMK_FullscreenStretch},
    {"CROP", SMK_FullscreenCrop},
    {"4BY3", SMK_FullscreenFit | SMK_FullscreenStretch},
    {"PIXELPERFECT", SMK_FullscreenFit | SMK_FullscreenCrop},
    {"4BY3PP", SMK_FullscreenFit | SMK_FullscreenStretch | SMK_FullscreenCrop},
    {NULL, 0},
};
static int64_t get_resize_movies(void) { return is_feature_on(Ft_Resizemovies) ? (int64_t)vid_scale_flags : 0; }
static void set_resize_movies(int64_t val)
{
    if (val == 0)
    {
        features_enabled &= ~Ft_Resizemovies;
    }
    else
    {
        features_enabled |= Ft_Resizemovies;
        vid_scale_flags = (uint64_t)val;
    }
}

// INGAME_RES (case 7, config_keeperfx.c) is a fullscreen "WxHxBPP" string
// (e.g. "1920x1080x32") handed to LbRegisterVideoModeString(), which
// registers a *new* mode on the fly if the exact string hasn't been seen
// before (bflib_video.c) -- it's not limited to a fixed, pre-registered
// list. That, plus the fact the real list of resolutions a monitor
// supports isn't known until the platform layer is asked, makes this the
// one enum row whose table can't be a compile-time constant like every
// other SOptT_Enum row -- see ensure_enum_table's own doc comment
// (config_settingschema.h). Only fullscreen resolutions are offered here
// (PlatformManager_GetFullscreenDisplayModeAt); DESKTOP/DESKTOP_FULL/
// windowed "WxHwBPP"/ALL are all still valid INGAME_RES values but aren't
// surfaced by this picker.
//
// .num encodes width/height as (width << 16) | height rather than reusing
// LbRegisterVideoModeString()'s own TbScreenMode return value, because
// that's a registry *index* assigned in registration order -- unstable
// across runs/platforms and meaningless to compare against "what
// resolution is active now" without first re-deriving width/height from
// it anyway. Both values comfortably fit 16 bits (SDL reports pixel
// dimensions, never anywhere near 65536).
#define INGAME_RES_ENCODE(w, h) (((int64_t)(w) << 16) | (int64_t)((h) & 0xFFFF))
#define INGAME_RES_WIDTH(val) (int64_t)((val) >> 16)
#define INGAME_RES_HEIGHT(val) (int64_t)((val) & 0xFFFF)

#define INGAME_RES_MAX_ENTRIES 32
static struct NamedCommand ingame_res_enum[INGAME_RES_MAX_ENTRIES + 1];
static char ingame_res_names[INGAME_RES_MAX_ENTRIES][16];
static TbBool ingame_res_enum_ready = false;

static void ensure_ingame_res_enum(void)
{
    // Detected resolutions don't change while the game is running (barring
    // a monitor hotplug, an edge case not worth re-querying every frame
    // for) -- populate once, lazily, on first actual use.
    if (ingame_res_enum_ready)
        return;
    ingame_res_enum_ready = true;
    int64_t count = PlatformManager_GetFullscreenDisplayModeCount(0);
    if (count > INGAME_RES_MAX_ENTRIES) count = INGAME_RES_MAX_ENTRIES;
    int64_t n = 0;
    for (int64_t i = 0; i < count; i++)
    {
        int64_t w = 0, h = 0;
        if (!PlatformManager_GetFullscreenDisplayModeAt(0, i, &w, &h) || (w <= 0) || (h <= 0))
            continue;
        snprintf(ingame_res_names[n], sizeof(ingame_res_names[n]), "%" PRId64 "x%" PRId64 "x32", (int64_t)(w), (int64_t)(h));
        ingame_res_enum[n].name = ingame_res_names[n];
        ingame_res_enum[n].num = INGAME_RES_ENCODE(w, h);
        n++;
    }
    ingame_res_enum[n].name = NULL;
    ingame_res_enum[n].num = 0;
}

static int64_t get_ingame_res(void)
{
    // INGAME_RES is SApply_NeedsRestart: reading LbScreenActiveMode() (the
    // currently-applied mode) would make the combo immediately revert to
    // the old resolution the instant a new one is picked, since the active
    // mode doesn't actually change until next launch. get_screen_vidmode()
    // reads the *pending* mode (screen_vidmode itself, kfx_render/vidmode.c)
    // instead, which set_ingame_res() below updates immediately.
    TbScreenMode mode = get_screen_vidmode();
    TbScreenModeInfo *info = LbScreenGetModeInfo(mode);
    if (info == NULL)
        return 0;
    return INGAME_RES_ENCODE(info->Width, info->Height);
}

static void set_ingame_res(int64_t val)
{
    char word_buf[16];
    snprintf(word_buf, sizeof(word_buf), "%" PRId64 "x%" PRId64 "x32", (int64_t)(INGAME_RES_WIDTH(val)), (int64_t)(INGAME_RES_HEIGHT(val)));
    TbScreenMode mode = LbRegisterVideoModeString(word_buf);
    if (mode != Lb_SCREEN_MODE_INVALID)
        set_screen_vidmode(mode);
    else
        ERRORLOG("Couldn't register video mode \"%s\" chosen from the settings screen.", word_buf);
}

// RENDERER (gpu-v2 Phase C.1) is SApply_NeedsRestart, same reasoning as
// INGAME_RES above: read back the *desired* (pending) choice, not
// RendererGetActiveType() -- the active backend doesn't actually change
// until next launch, and reading it here would make the combo immediately
// revert to the old choice the instant a new one is picked. Unlike
// INGAME_RES/SCREENSHOT, no SimPort indirection is needed:
// RendererManager.h's functions live in kfx_platform, a layer *below*
// kfx_config, so this file can call them directly.
static int64_t get_renderer(void)
{
    return (int64_t)RendererGetDesiredType();
}

static void set_renderer(int64_t val)
{
    RendererSetDesiredType((RendererType)val);
}

// LOG_LEVEL / GPU_DEBUG -- docs/refactor-pass2/stage-02-logging-option.md.
// log_level_type[] is 1-based (config_keeperfx.c), enum LogLevel is 0-based.
static int64_t get_log_level_opt(void) { return get_log_level() + 1; }
static void set_log_level_opt(int64_t val) { set_log_level(val - 1); }
static TbBool get_gpu_debug(void) { return RendererGetGpuDebug(); }
static void set_gpu_debug(TbBool val) { RendererSetGpuDebug(val); }

static int64_t get_overhead_fade(void) { return keeperfx_ui_config.overhead_fade; }
static void set_overhead_fade(int64_t val) { keeperfx_ui_config.overhead_fade = val; }

// LIGHTING (gpu-v2 Phase C.5): config values are 1-based (see lighting_type[]).
static int64_t get_lighting(void)
{
    return (int64_t)RendererGetLightingMode() + 1;
}

static void set_lighting(int64_t val)
{
    RendererSetLightingMode((int)(val - 1));
}

// POINTER_SENSITIVITY's own config-key parsing (case 9, config_keeperfx.c)
// stores an integer percentage in the file (0-10000) but scales it by
// 256/100 before handing it to set_base_mouse_sensitivity -- same
// percentage-in-file-vs-scaled-internal-value shape as HAND_SIZE, so this
// is a plain SOptT_Int row too. (§6.2 finding 3 describes the launcher's
// own UI as a checkbox-gated slider where 0 has a special "raw input"
// meaning; not modeled here -- what "0" actually does at the engine level
// beyond zeroing LbMouseChangeMoveRatio() isn't something this pass could
// confirm, and a checkbox asserting an unverified behaviour is worse than
// no checkbox. The plain slider still reaches 0 by dragging it down.)
static int64_t get_pointer_sensitivity_pct(void) { return kfx_runtime_settings.base_mouse_sensitivity * 100 / 256; }
static void set_pointer_sensitivity_pct(int64_t val) { kfx_runtime_settings.base_mouse_sensitivity = val * 256 / 100; }

// STARTUP's own config-key parsing (case 22, config_keeperfx.c) accepts a
// space-separated token list -- LEGAL/FX/BULLFROG(hidden)/EA(hidden)/INTRO,
// each just a bit in start_params.startup_flags -- and §6.2's own Game-tab
// description names it "the splash-screens + intro list" (the legacy
// DISABLE_SPLASH_SCREENS + "-nointro" pair). Two rows expose it, matching
// finding 3's "two checkboxes": "splash screens" toggles LEGAL and FX
// together (set_default_startup_parameters()'s own default is
// SFlg_Legal|SFlg_FX|SFlg_Intro -- they've always moved as a pair), "intro"
// toggles INTRO alone. BULLFROG/EA are legacy/hidden -- never toggled by
// either checkbox, but format_startup_cfg_value() reconstructs the *whole*
// list from the live bits on every apply, so a config file that already had
// them keeps them rather than losing them the moment either checkbox is
// touched.
static TbBool get_startup_splash(void) { return (start_params.startup_flags & (SFlg_Legal | SFlg_FX)) == (SFlg_Legal | SFlg_FX); }
static void set_startup_splash(TbBool val)
{
    if (val) start_params.startup_flags |= (SFlg_Legal | SFlg_FX);
    else start_params.startup_flags &= ~(SFlg_Legal | SFlg_FX);
}

static TbBool get_startup_intro(void) { return (start_params.startup_flags & SFlg_Intro) != 0; }
static void set_startup_intro(TbBool val)
{
    if (val) start_params.startup_flags |= SFlg_Intro;
    else start_params.startup_flags &= ~SFlg_Intro;
}

static const char *format_startup_cfg_value(void)
{
    static char buf[48];
    buf[0] = '\0';
    if (start_params.startup_flags & SFlg_Legal) strcat(buf, "LEGAL ");
    if (start_params.startup_flags & SFlg_FX) strcat(buf, "FX ");
    if (start_params.startup_flags & SFlg_Bullfrog) strcat(buf, "BULLFROG ");
    if (start_params.startup_flags & SFlg_EA) strcat(buf, "EA ");
    if (start_params.startup_flags & SFlg_Intro) strcat(buf, "INTRO ");
    size_t len = strlen(buf);
    if (len > 0) buf[len - 1] = '\0'; // trim the trailing space
    return buf;
}

// FRAMES_PER_SECOND's own config-key parsing (case 39, config_keeperfx.c,
// via parse_draw_fps_config_val()) stores a mode plus a number: "AUTO" (or
// "AUTO <secondary>") sets num_fps_draw_main to -1 (bflib_video.h's own
// comment: "-1 if auto" -- confirmed against redetect_screen_refresh_rate_
// for_draw()'s actual use of it: -1 means "use the display's own refresh
// rate, falling back to num_fps_draw_secondary if that can't be
// detected"), while a plain number sets num_fps_draw_main directly (0,
// its real default, means uncapped; positive means a fixed FPS cap).
// Two SOptT_Bool/Int rows share the cfg_key, same shared-key +
// format_cfg_value shape STARTUP already established: "Auto Frame Rate"
// toggles the -1 sentinel, "Frame Rate Limit" edits the fixed-cap number
// (greyed out while Auto is on -- same is_enabled gating ALT_INPUT's own
// pair of rows uses). num_fps_draw_secondary isn't exposed as a control
// of its own (a rarely-needed fallback-only-under-Auto value) but
// format_fps_cfg_value() preserves it verbatim if a config file already
// set it, the same "don't drop what wasn't touched" reasoning STARTUP's
// hidden BULLFROG/EA bits needed.
static TbBool get_fps_auto(void) { return start_params.num_fps_draw_main == -1; }
static void set_fps_auto(TbBool val)
{
    start_params.num_fps_draw_main = val ? -1 : 0; // 0 is num_fps_draw_main's own real default (uncapped)
}

static int64_t get_fps_limit(void) { return (start_params.num_fps_draw_main > 0) ? start_params.num_fps_draw_main : 0; }
static void set_fps_limit(int64_t val) { start_params.num_fps_draw_main = val; }
static TbBool fps_limit_enabled(void) { return start_params.num_fps_draw_main != -1; }

static const char *format_fps_cfg_value(void)
{
    static char buf[32];
    if (start_params.num_fps_draw_main == -1)
    {
        if (start_params.num_fps_draw_secondary > 0)
            snprintf(buf, sizeof(buf), "AUTO %" PRId64, (int64_t)start_params.num_fps_draw_secondary);
        else
            snprintf(buf, sizeof(buf), "AUTO");
    }
    else
    {
        snprintf(buf, sizeof(buf), "%" PRId64, (int64_t)start_params.num_fps_draw_main);
    }
    return buf;
}

// Designated initializers throughout -- every field not named defaults to
// 0/NULL/false, and (unlike positional init) doesn't trip
// -Wmissing-field-initializers/-Werror when a row leaves most of them out,
// which almost every row here does (only one accessor pair, rarely
// is_enabled, almost never cfg_bool_inverted).
const struct SettingOption setting_options[] = {
    {
        .cfg_key = "FREEZE_GAME_ON_FOCUS_LOST", .type = SOptT_Bool, .category = SCat_Game, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetFreezeOnFocusLost,
        .help_stridx = GUIStr_HelpFreezeOnFocusLost,
        .get_bool = &get_freeze_on_focus_lost, .set_bool = &set_freeze_on_focus_lost,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "STARTUP", .type = SOptT_Bool, .category = SCat_Game, .apply_class = SApply_NeedsRestart,
        .label_stridx = GUIStr_SetStartupSplash,
        .help_stridx = GUIStr_HelpStartupSplash,
        .get_bool = &get_startup_splash, .set_bool = &set_startup_splash,
        .format_cfg_value = &format_startup_cfg_value,
    },
    {
        .cfg_key = "STARTUP", .type = SOptT_Bool, .category = SCat_Game, .apply_class = SApply_NeedsRestart,
        .label_stridx = GUIStr_SetStartupIntro,
        .help_stridx = GUIStr_HelpStartupIntro,
        .get_bool = &get_startup_intro, .set_bool = &set_startup_intro,
        .format_cfg_value = &format_startup_cfg_value,
    },
    {
        .cfg_key = "EASTER_EGG", .type = SOptT_Bool, .category = SCat_Game, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetEasterEgg,
        .help_stridx = GUIStr_HelpEasterEgg,
        .get_bool = &get_easter_egg, .set_bool = &set_easter_egg,
    },
    {
        .cfg_key = "CENSORSHIP", .type = SOptT_Bool, .category = SCat_Game, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetCensorship,
        .help_stridx = GUIStr_HelpCensorship,
        .get_bool = &get_censorship, .set_bool = &set_censorship,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "FLEE_BUTTON_DEFAULT", .type = SOptT_Bool, .category = SCat_Game, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetFleeButtonDefault,
        .help_stridx = GUIStr_HelpFleeButtonDefault,
        .get_bool = &get_flee_button_default, .set_bool = &set_flee_button_default,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "IMPRISON_BUTTON_DEFAULT", .type = SOptT_Bool, .category = SCat_Game, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetImprisonButtonDefault,
        .help_stridx = GUIStr_HelpImprisonButtonDefault,
        .get_bool = &get_imprison_button_default, .set_bool = &set_imprison_button_default,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "LANGUAGE", .type = SOptT_Enum, .category = SCat_Game, .apply_class = SApply_NeedsRestart,
        .label_stridx = GUIStr_SetLanguage,
        .help_stridx = GUIStr_HelpLanguage,
        .enum_table = lang_type, .get_enum = &get_language, .set_enum = &set_language,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "DELTA_TIME", .type = SOptT_Bool, .category = SCat_Game, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetDeltaTime,
        .help_stridx = GUIStr_HelpDeltaTime,
        .get_bool = &get_delta_time, .set_bool = &set_delta_time,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "DISPLAY_NUMBER", .type = SOptT_Int, .category = SCat_Graphics, .apply_class = SApply_NeedsRestart,
        .label_stridx = GUIStr_SetDisplayNumber,
        .help_stridx = GUIStr_HelpDisplayNumber,
        .get_int = &get_display_number, .set_int = &set_display_number, .int_min = 1, .int_max = 8,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "FRAMES_PER_SECOND", .type = SOptT_Bool, .category = SCat_Graphics, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetFpsAuto,
        .help_stridx = GUIStr_HelpFpsAuto,
        .get_bool = &get_fps_auto, .set_bool = &set_fps_auto,
        .format_cfg_value = &format_fps_cfg_value,
    },
    {
        .cfg_key = "FRAMES_PER_SECOND", .type = SOptT_Int, .category = SCat_Graphics, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetFpsLimit,
        .help_stridx = GUIStr_HelpFpsLimit,
        .get_int = &get_fps_limit, .set_int = &set_fps_limit, .int_min = 0, .int_max = 500,
        .is_enabled = &fps_limit_enabled,
        .format_cfg_value = &format_fps_cfg_value,
    },
    {
        .cfg_key = "GUI_BLINK_RATE", .type = SOptT_Int, .category = SCat_Graphics, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetGuiBlinkRate,
        .help_stridx = GUIStr_HelpGuiBlinkRate,
        .get_int = &get_gui_blink_rate, .set_int = &set_gui_blink_rate, .int_min = 1, .int_max = 160,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "CREATURE_STATUS_SIZE", .type = SOptT_Int, .category = SCat_Graphics, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetCreatureStatusSize,
        .help_stridx = GUIStr_HelpCreatureStatusSize,
        .get_int = &get_creature_status_size, .set_int = &set_creature_status_size, .int_min = 8, .int_max = 64,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "LINE_BOX_SIZE", .type = SOptT_Int, .category = SCat_Graphics, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetLineBoxSize,
        .help_stridx = GUIStr_HelpLineBoxSize,
        .get_int = &get_line_box_size, .set_int = &set_line_box_size, .int_min = 0, .int_max = 500,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "NEUTRAL_FLASH_RATE", .type = SOptT_Int, .category = SCat_Graphics, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetNeutralFlashRate,
        .help_stridx = GUIStr_HelpNeutralFlashRate,
        .get_int = &get_neutral_flash_rate, .set_int = &set_neutral_flash_rate, .int_min = 1, .int_max = 160,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "PAUSE_MUSIC_WHEN_GAME_PAUSED", .type = SOptT_Bool, .category = SCat_Sound, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetPauseMusicOnPause,
        .help_stridx = GUIStr_HelpPauseMusicOnPause,
        .get_bool = &get_pause_music_on_pause, .set_bool = &set_pause_music_on_pause,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "MUTE_AUDIO_ON_FOCUS_LOST", .type = SOptT_Bool, .category = SCat_Sound, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetMuteAudioOnFocusLost,
        .help_stridx = GUIStr_HelpMuteAudioOnFocusLost,
        .get_bool = &get_mute_audio_on_focus_lost, .set_bool = &set_mute_audio_on_focus_lost,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "ATMOSPHERIC_SOUNDS", .type = SOptT_Bool, .category = SCat_Sound, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetAtmosphericSounds,
        .help_stridx = GUIStr_HelpAtmosphericSounds,
        .get_bool = &get_atmospheric_sounds, .set_bool = &set_atmospheric_sounds,
        .cfg_read_by_row = true,
    },
    {
        // See get_use_cd_music()'s own comment -- this row's displayed
        // sense is the inverse of the MUSIC_FROM_DISK key it writes.
        .cfg_key = "MUSIC_FROM_DISK", .type = SOptT_Bool, .category = SCat_Sound, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetUseCdMusic,
        .help_stridx = GUIStr_HelpUseCdMusic,
        .get_bool = &get_use_cd_music, .set_bool = &set_use_cd_music,
        .cfg_bool_inverted = true,
        .cfg_read_by_row = true,
    },
    {
        // A volume slider like the sound/music/mentor ones (0-255, shown 0-100). The parser
        // (config_keeperfx.c, case 11) still reads KeeperFX's LOW/MEDIUM/HIGH
        // (64/128/255); this writes the number.
        .cfg_key = "ATMOS_VOLUME", .type = SOptT_Int, .category = SCat_Sound, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetAtmosVolume,
        .help_stridx = GUIStr_HelpAtmosVolume,
        .get_int = &get_atmos_volume, .set_int = &set_atmos_volume, .int_min = 0, .int_max = ATMOS_VOLUME_MAX,
        .int_is_volume = true,
    },
    {
        .cfg_key = "ATMOS_FREQUENCY", .type = SOptT_Enum, .category = SCat_Sound, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetAtmosFrequency,
        .help_stridx = GUIStr_HelpAtmosFrequency,
        .enum_table = atmos_freq, .get_enum = &get_atmos_frequency, .set_enum = &set_atmos_frequency,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "POINTER_SENSITIVITY", .type = SOptT_Int, .category = SCat_Input, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetPointerSensitivity,
        .help_stridx = GUIStr_HelpPointerSensitivity,
        .get_int = &get_pointer_sensitivity_pct, .set_int = &set_pointer_sensitivity_pct, .int_min = 0, .int_max = 500,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "ALT_INPUT", .type = SOptT_Bool, .category = SCat_Input, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetAltInput,
        .help_stridx = GUIStr_HelpAltInput,
        .get_bool = &get_alt_input, .set_bool = &set_alt_input,
    },
    {
        .cfg_key = "UNLOCK_CURSOR_WHEN_GAME_PAUSED", .type = SOptT_Bool, .category = SCat_Input, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetUnlockCursorOnPause,
        .help_stridx = GUIStr_HelpUnlockCursorOnPause,
        .get_bool = &get_unlock_cursor_on_pause, .set_bool = &set_unlock_cursor_on_pause,
        .is_enabled = &unlock_cursor_on_pause_enabled,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "LOCK_CURSOR_IN_POSSESSION", .type = SOptT_Bool, .category = SCat_Input, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetLockCursorInPossession,
        .help_stridx = GUIStr_HelpLockCursorInPossession,
        .get_bool = &get_lock_cursor_in_possession, .set_bool = &set_lock_cursor_in_possession,
        .is_enabled = &lock_cursor_in_possession_enabled,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "CURSOR_EDGE_CAMERA_PANNING", .type = SOptT_Bool, .category = SCat_Input, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetCursorEdgeCameraPanning,
        .help_stridx = GUIStr_HelpCursorEdgeCameraPanning,
        .get_bool = &get_cursor_edge_camera_panning, .set_bool = &set_cursor_edge_camera_panning,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "TAG_MODE_TOGGLING", .type = SOptT_Bool, .category = SCat_Input, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetTagModeToggling,
        .help_stridx = GUIStr_HelpTagModeToggling,
        .get_bool = &get_tag_mode_toggling, .set_bool = &set_tag_mode_toggling,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "DEFAULT_TAG_MODE", .type = SOptT_Enum, .category = SCat_Input, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetDefaultTagMode,
        .help_stridx = GUIStr_HelpDefaultTagMode,
        .enum_table = tag_modes, .get_enum = &get_default_tag_mode, .set_enum = &set_default_tag_mode,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "ZOOM_TO_MOUSE", .type = SOptT_Enum, .category = SCat_Input, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetZoomToMouse,
        .help_stridx = GUIStr_HelpZoomToMouse,
        .enum_table = zoom_to_mouse_enum, .get_enum = &get_zoom_to_mouse, .set_enum = &set_zoom_to_mouse,
    },
    {
        .cfg_key = "ROTATE_AROUND_MOUSE", .type = SOptT_Enum, .category = SCat_Input, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetRotateAroundMouse,
        .help_stridx = GUIStr_HelpRotateAroundMouse,
        .enum_table = rotate_around_mouse_enum, .get_enum = &get_rotate_around_mouse, .set_enum = &set_rotate_around_mouse,
    },
    {
        // Applies live: set_log_level() only updates the threshold. The font
        // test screen (Shift+F in the menus) also needs DEBUG.
        .cfg_key = "LOG_LEVEL", .type = SOptT_Enum, .category = SCat_Game, .apply_class = SApply_Live,
        .label_literal = "Logging",
        .help_literal = "How much the game writes to " DEFAULT_LOG_FILENAME ". OFF: nothing, except crash reports. "
                        "NORMAL: errors, warnings and progress (the default). DEBUG: extra detail for bug "
                        "reports. DEBUGMAX: everything; the log grows quickly.",
        .enum_table = log_level_type, .get_enum = &get_log_level_opt, .set_enum = &set_log_level_opt,
    },
    {
        // Read when a level starts (setup_auto_replay_save), so it applies from the next game.
        .cfg_key = "AUTOMATIC_REPLAYS", .type = SOptT_Bool, .category = SCat_Game, .apply_class = SApply_Live,
        .label_literal = "Save replays",
        .help_literal = "Record every game you play as a replay in the replays folder, keeping the "
                        "newest few of each kind (MAX_REPLAYS in " PRODUCT_SLUG ".cfg). Applies from the next game.",
        .get_bool = &get_autosave_replays, .set_bool = &set_autosave_replays,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "GPU_DEBUG", .type = SOptT_Bool, .category = SCat_Graphics, .apply_class = SApply_NeedsRestart,
        .frontend_only = true,
        .label_literal = "GPU validation",
        .help_literal = "Vulkan validation layers, for developers: slow, and needs the Vulkan SDK's "
                        "validation layers installed. Vulkan renderer only. Takes effect after a restart.",
        .get_bool = &get_gpu_debug, .set_bool = &set_gpu_debug,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "SCREENSHOT", .type = SOptT_Enum, .category = SCat_Graphics, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetScreenshotFormat,
        .help_stridx = GUIStr_HelpScreenshotFormat,
        .enum_table = scrshot_type, .get_enum = &get_screenshot_format, .set_enum = &set_screenshot_format_val,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "HAND_SIZE", .type = SOptT_Int, .category = SCat_Graphics, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetHandSize,
        .help_stridx = GUIStr_HelpHandSize,
        .get_int = &get_hand_size_pct, .set_int = &set_hand_size_pct, .int_min = 10, .int_max = 500,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "RESIZE_MOVIES", .type = SOptT_Enum, .category = SCat_Graphics, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetResizeMovies,
        .help_stridx = GUIStr_HelpResizeMovies,
        .enum_table = resize_movies_enum, .get_enum = &get_resize_movies, .set_enum = &set_resize_movies,
    },
    {
        // set_screen_vidmode() (kfx_render/vidmode.c) just stores the mode
        // in screen_vidmode for setup_screen_mode() to pick up -- same
        // "takes effect next launch" shape as DISPLAY_NUMBER above, hence
        // SApply_NeedsRestart.
        .cfg_key = "INGAME_RES", .type = SOptT_Enum, .category = SCat_Graphics, .apply_class = SApply_NeedsRestart,
        .label_stridx = GUIStr_SetIngameRes,
        .help_stridx = GUIStr_HelpIngameRes,
        .enum_table = ingame_res_enum, .get_enum = &get_ingame_res, .set_enum = &set_ingame_res,
        .ensure_enum_table = &ensure_ingame_res_enum,
    },
    {
        // gpu-v2 Phase C.1 (docs/refactor/renderer/gpu-v2/07-phased-delivery.md):
        // The 3D view (terrain, sprites, shadows) renders on the GPU; a few
        // CPU-only overlays remain, hence the honest help text.
        .cfg_key = "RENDERER", .type = SOptT_Enum, .category = SCat_Graphics, .apply_class = SApply_NeedsRestart,
        .label_literal = "Renderer",
        .help_literal = "Software (CPU) or Vulkan (GPU). Vulkan is experimental and "
                        "falls back to Software if no GPU is available.",
        .enum_table = renderer_type, .get_enum = &get_renderer, .set_enum = &set_renderer,
        .cfg_read_by_row = true,
    },
    {
        // gpu-v2 Phase C.5 lighting pass. Applies live; only has an effect with the Vulkan renderer.
        .cfg_key = "LIGHTING", .type = SOptT_Enum, .category = SCat_Graphics, .apply_class = SApply_Live,
        .label_literal = "Lighting",
        .help_literal = "Classic: lights are baked into the map per tile. Per-pixel: dynamic lights "
                        "(torches, spells, lava) are calculated per pixel, giving smoother light "
                        "pools. Vulkan renderer only; experimental.",
        .enum_table = lighting_type, .get_enum = &get_lighting, .set_enum = &set_lighting,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "OVERHEAD_FADE", .type = SOptT_Int, .category = SCat_Graphics, .apply_class = SApply_Live,
        .label_literal = "Overhead Fade",
        .help_literal = "Darkens the overhead view toward the edges of the screen (from about 3 tiles "
                        "out to 10). The number is how many percent darker the edge gets; 0 is off.",
        .get_int = &get_overhead_fade, .set_int = &set_overhead_fade, .int_min = 0, .int_max = 100,
    },
    {
        .cfg_key = "UI_FONT_SCALE", .type = SOptT_Enum, .category = SCat_GUI, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetUiFontScale,
        .help_stridx = GUIStr_HelpUiFontScale,
        .enum_table = ui_font_scale_enum, .get_enum = &get_ui_font_scale, .set_enum = &set_ui_font_scale,
    },
    {
        .cfg_key = "UI_FONT", .type = SOptT_Enum, .category = SCat_GUI, .apply_class = SApply_Live,
        .label_literal = "UI Font",
        .help_literal = "Typeface for menus and the HUD. Add more by dropping a font family "
                        "folder into fxdata/font/. Only changeable from the main menu.",
        .frontend_only = true,
        .enum_table = ui_font_enum, .get_enum = &get_ui_font, .set_enum = &set_ui_font,
        .ensure_enum_table = &ensure_ui_font_enum,
    },
    {
        // Recomputed every frame from the live setting (frontgui_ingame_panel.cpp's
        // read_menu_rect()/btn_rect() offset), not baked into the legacy
        // GMnu_MAIN menu's one-time create_menu() positioning -- so, unlike
        // UI_FONT, this is safe to change mid-match: not frontend_only.
        .cfg_key = "GUI_POSITION", .type = SOptT_Enum, .category = SCat_GUI, .apply_class = SApply_Live,
        .label_literal = "Position",
        .help_literal = "Which edge of the screen the in-game sidebar sits against, or CLASSIC for the original sprite-drawn sidebar.",
        .enum_table = hud_position_type, .get_enum = &get_hud_position, .set_enum = &set_hud_position,
    },
    {
        .cfg_key = "MINIMAP_CORNER", .type = SOptT_Enum, .category = SCat_GUI, .apply_class = SApply_Live,
        .label_literal = "Minimap Corner",
        .help_literal = "Minimal layout only: which upper corner the minimap sits in.",
        .enum_table = minimap_corner_type, .get_enum = &get_minimap_corner, .set_enum = &set_minimap_corner,
        .is_enabled = &minimap_corner_enabled,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "PANEL_CORNER", .type = SOptT_Enum, .category = SCat_GUI, .apply_class = SApply_Live,
        .label_literal = "Panel Corner",
        .help_literal = "Minimal layout only: which upper corner the button cluster and its "
                        "pop-up panel sit in. Independent of Minimap Corner -- either may share "
                        "a corner or take opposite ones.",
        .enum_table = panel_corner_type, .get_enum = &get_panel_corner, .set_enum = &set_panel_corner,
        .is_enabled = &panel_corner_enabled,
        .cfg_read_by_row = true,
    },
    {
        .cfg_key = "GUI_ICON_PACK", .type = SOptT_Enum, .category = SCat_GUI, .apply_class = SApply_Live,
        .label_literal = "Icon Pack",
        .help_literal = "Replaces HUD icons with PNGs from a fxdata/gui/ sub-folder (add more by "
                        "dropping a folder of same-named PNGs in there).",
        .enum_table = gui_icon_pack_enum, .get_enum = &get_gui_icon_pack, .set_enum = &set_gui_icon_pack,
        .ensure_enum_table = &ensure_gui_icon_pack_enum,
    },
    {
        // Ported from the in-game Video options menu -- see
        // get_video_shadows/get_view_distance's own doc comment above for
        // why this isn't a keeperfx.cfg-backed row like everything else in
        // this table (persist_via_save_settings).
        .type = SOptT_Int, .category = SCat_Graphics, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetVideoShadows,
        .help_stridx = GUIStr_HelpVideoShadows,
        .get_int = &get_video_shadows, .set_int = &set_video_shadows, .int_min = 0, .int_max = 3,
        .persist_via_save_settings = true,
    },
    {
        .type = SOptT_Int, .category = SCat_Graphics, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetViewDistance,
        .help_stridx = GUIStr_HelpViewDistance,
        .get_int = &get_view_distance, .set_int = &set_view_distance, .int_min = 0, .int_max = 3,
        .persist_via_save_settings = true,
    },
    {
        // No cfg_key -- see enum SettingOptionType's own SOptT_Action
        // comment (config_settingschema.h).
        .type = SOptT_Action, .category = SCat_Game, .apply_class = SApply_Live,
        .label_stridx = GUIStr_SetResetCampaignProgress,
        .help_stridx = GUIStr_HelpResetCampaignProgress,
        .on_action = &reset_campaign_progress_action,
    },
};

const int64_t setting_options_count = sizeof(setting_options) / sizeof(setting_options[0]);

/** The row the base config reader reads cfg_key through (see cfg_read_by_row), or NULL. */
const struct SettingOption *setting_option_read_by_row(const char *cfg_key)
{
    for (int64_t i = 0; i < setting_options_count; i++)
    {
        const struct SettingOption *opt = &setting_options[i];
        if (opt->cfg_read_by_row && (opt->cfg_key != NULL) && (strcasecmp(opt->cfg_key, cfg_key) == 0))
            return opt;
    }
    return NULL;
}

/** Reads the value of opt's key from a config line into the option (see cfg_read_by_row). */
void setting_option_read_cfg_value(const struct SettingOption *opt, const char *buf, int64_t *pos, int64_t len,
    const char *config_textname)
{
    if (opt->type == SOptT_Int)
    {
        // A whole number within the screen's limits (refactor pass 4 finding P4-F6: the file took wider ranges)
        char word_buf[128];
        if ((get_conf_parameter_single(buf, pos, len, word_buf, sizeof(word_buf)) <= 0) || !parameter_is_number(word_buf))
        {
            CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", opt->cfg_key, config_textname);
            return;
        }
        int64_t val = atoll(word_buf);
        const int64_t limited = (val < opt->int_min) ? opt->int_min : ((val > opt->int_max) ? opt->int_max : val);
        if (limited != val)
        {
            CONFWRNLOG("Value %" PRId64 " out of range for \"%s\" command of %s file. Set to %" PRId64 ".",
                val, opt->cfg_key, config_textname, limited);
        }
        opt->set_int(limited);
        return;
    }
    const int64_t i = recognize_conf_parameter(buf, pos, len, (opt->type == SOptT_Bool) ? logicval_type : opt->enum_table);
    if (i <= 0)
    {
        CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", opt->cfg_key, config_textname);
        return;
    }
    if (opt->type == SOptT_Bool)
    {
        const TbBool val = (i == 1);
        opt->set_bool(opt->cfg_bool_inverted ? !val : val);
    }
    else
    {
        opt->set_enum(i);
    }
}

void setting_option_apply_bool(const struct SettingOption *opt, TbBool val)
{
    if ((opt == NULL) || (opt->type != SOptT_Bool) || (opt->set_bool == NULL))
    {
        ERRORLOG("setting_option_apply_bool called on a non-bool or incomplete option.");
        return;
    }
    opt->set_bool(val);
    if (opt->persist_via_save_settings)
    {
        save_settings();
        return;
    }
    const char *write_val;
    if (opt->format_cfg_value != NULL)
    {
        write_val = opt->format_cfg_value();
    }
    else
    {
        TbBool inverted_val = opt->cfg_bool_inverted ? !val : val;
        write_val = inverted_val ? "TRUE" : "FALSE";
    }
    struct KeeperfxCfgEdit edit = { opt->cfg_key, write_val };
    keeperfx_cfg_write_values(&edit, 1);
}

void setting_option_apply_int(const struct SettingOption *opt, int64_t val)
{
    if ((opt == NULL) || (opt->type != SOptT_Int) || (opt->set_int == NULL))
    {
        ERRORLOG("setting_option_apply_int called on a non-int or incomplete option.");
        return;
    }
    if (val < opt->int_min) val = opt->int_min;
    if (val > opt->int_max) val = opt->int_max;
    opt->set_int(val);
    if (opt->persist_via_save_settings)
    {
        save_settings();
        return;
    }
    char valbuf[16];
    const char *write_val;
    if (opt->format_cfg_value != NULL)
    {
        write_val = opt->format_cfg_value();
    }
    else
    {
        snprintf(valbuf, sizeof(valbuf), "%" PRId64, (int64_t)(val));
        write_val = valbuf;
    }
    struct KeeperfxCfgEdit edit = { opt->cfg_key, write_val };
    keeperfx_cfg_write_values(&edit, 1);
}

int64_t setting_option_enum_count(const struct SettingOption *opt)
{
    if ((opt == NULL) || (opt->type != SOptT_Enum) || (opt->enum_table == NULL))
        return 0;
    if (opt->ensure_enum_table != NULL) opt->ensure_enum_table();
    int64_t n = 0;
    while (opt->enum_table[n].name != NULL) n++;
    return n;
}

int64_t setting_option_enum_current_index(const struct SettingOption *opt)
{
    if ((opt == NULL) || (opt->type != SOptT_Enum) || (opt->enum_table == NULL) || (opt->get_enum == NULL))
        return 0;
    if (opt->ensure_enum_table != NULL) opt->ensure_enum_table();
    int64_t val = opt->get_enum();
    for (int64_t i = 0; opt->enum_table[i].name != NULL; i++)
    {
        if (opt->enum_table[i].num == val)
            return i;
    }
    return 0;
}

const char *setting_option_enum_item_name(const struct SettingOption *opt, int64_t index)
{
    if ((opt == NULL) || (opt->type != SOptT_Enum) || (opt->enum_table == NULL))
        return "";
    // setting_option_enum_count() below already calls ensure_enum_table().
    if ((index < 0) || (index >= setting_option_enum_count(opt)))
        return "";
    return opt->enum_table[index].name;
}

void setting_option_apply_enum_index(const struct SettingOption *opt, int64_t index)
{
    if ((opt == NULL) || (opt->type != SOptT_Enum) || (opt->enum_table == NULL) || (opt->set_enum == NULL))
    {
        ERRORLOG("setting_option_apply_enum_index called on a non-enum or incomplete option.");
        return;
    }
    // setting_option_enum_count() below already calls ensure_enum_table().
    if ((index < 0) || (index >= setting_option_enum_count(opt)))
    {
        ERRORLOG("setting_option_apply_enum_index called with an out-of-range index.");
        return;
    }
    opt->set_enum(opt->enum_table[index].num);
    struct KeeperfxCfgEdit edit = { opt->cfg_key, opt->enum_table[index].name };
    keeperfx_cfg_write_values(&edit, 1);
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
