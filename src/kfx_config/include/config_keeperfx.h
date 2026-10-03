/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config.h
 *     Header file for config.c.
 * @par Purpose:
 *     Configuration and campaign files support.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     30 Jan 2009 - 11 Feb 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/

#ifndef DK_CONFIG_KEEPERFX_H
#define DK_CONFIG_KEEPERFX_H

#include "bflib_basics.h"
#include "globals.h"
#include "config.h"

#ifdef FUNCTESTING
#include "ftests/ftest.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct CreditsItem;
struct GameCampaign;
/******************************************************************************/

// Max length of the command line
#define CMDLN_MAXLEN 259
#define CMDLINE_OVERRIDES 7

/** Command Line overrides for config settings. Checked after the config file is loaded. */
enum CmdLineOverrides {
    Clo_ConfigFile = 0, /**< Special: handled before the config file is loaded. */
    Clo_CDMusic,
    Clo_GameTurns,
    Clo_FramesPerSecond,
    // docs/refactor/renderer/04-imgui-gui-foundation.md §6.2 finding 2 --
    // these three gained keeperfx.cfg keys (EASTER_EGG/VID_SMOOTH/
    // ALT_INPUT) alongside their pre-existing "-alex"/"-vidsmooth"/
    // "-altinput" launch flags; the flags are one-directional "force on"
    // switches, so process_cmdline_overrides() only ever pushes their
    // value to true, never restores a config-file "off".
    Clo_EasterEgg,
    Clo_VidSmooth,
    Clo_AltInput,
};

// Used by both StartupParameters.mode_flags (below) and struct Game's
// mode_flags field (kfx_game/game_legacy.h).
enum ModeFlags {
    MFlg_IsDemoMode         =  0x01,
    MFlg_EyeLensReady       =  0x02,
    MFlg_Unusedparam04      =  0x04,
    MFlg_DeadBackToPool     =  0x08,
    MFlg_NoCdMusic          =  0x10, // unused
    MFlg_Unusedparam20      =  0x20,
    MFlg_DemoMode           =  0x40,
    MFlg_NoHeroHealthFlower =  0x80,
};

// Used by StartupParameters.debug_flags (below).
enum DebugFlags {
    DFlg_ShotsDamage        =  0x01,
    DFlg_CreatrPaths        =  0x02,
    DFlg_ShowGameTurns      =  0x04,
    DFlg_FrameStep          =  0x08,
    DFlg_PauseAtGameTurn    =  0x10,
    // docs/refactor/renderer/04-imgui-gui-foundation.md §7 Phase A exit
    // criteria: -imguidemo shows imgui_demo.cpp's ShowDemoWindow() as a
    // proof that the backend wiring works, ahead of any real screen
    // migrating. Superseded by real screens from Phase C onward.
    DFlg_ImGuiDemo          =  0x20,
    // §7 Phase B exit criteria: -imguistyle shows the FeStyleSheetFrame()
    // proving ground, exercising every frontgui_widgets.h wrapper.
    DFlg_ImGuiStyleSheet    =  0x40,
};

#ifdef FUNCTESTING
// Used by StartupParameters.functest_flags (below).
enum FunctestFlags {
    FTF_Enabled             = 0x01, // Functional Tests Enabled
    FTF_TestFailed          = 0x02, // Test failed, causes exit code -1 for cmd line automation
    FTF_Abort               = 0x04, // Something went wrong, aborting
    FTF_LevelLoaded         = 0x08, // For tracking if map is ready
    FTF_ExitOnTestFailure   = 0x10, // If users want to exit on any test failure
    FTF_IncludeLongTests    = 0x20, // If users want to run the long running test list
};
#endif

#pragma pack(1)
struct StartupParameters {
    LevelNumber selected_level_number;
    TbBool no_intro;
    TbBool one_player;
    TbBool easter_egg;
    TbBool ignore_mods;
    unsigned char operation_flags;
    unsigned char flags_font;
    unsigned char mode_flags;
    unsigned char debug_flags;
    int64_t computer_chat_flags;
    int64_t num_fps;
    int64_t num_fps_draw_main; // -1 if auto
    int64_t num_fps_draw_secondary;
    TbBool packet_save_enable;
    TbBool packet_load_enable;
    char packet_fname[150];
    unsigned char packet_checksum_verify;
    int64_t frame_skip;
    char selected_campaign[CMDLN_MAXLEN+1];
    TbBool overrides[CMDLINE_OVERRIDES];
    char config_file[CMDLN_MAXLEN+1];
    GameTurn pause_at_gameturn;
    unsigned char startup_flags;
    TbBool skip_heart_zoom;
    // Moved from main.cpp (2026-08-29, docs/refactor/todo/
    // check-layering-symbol-level-blind-spot.md) -- cmdline-parsed
    // process-startup config, same shape as this struct's other fields;
    // read by kfx_frontend's front_network.c and kfx_game's main_game.c,
    // both ranked below app_entry.
    char autostart_multiplayer_campaign[80];
    int64_t autostart_multiplayer_level;
    int64_t autostart_multiplayer_users_expected;
    TbBool force_player_num;
#ifdef FUNCTESTING
    unsigned char functest_flags;
    char functest_name[FTEST_MAX_NAME_LENGTH];
    uint64_t functest_seed;
#endif
};
#pragma pack()

extern struct StartupParameters start_params;
/******************************************************************************/


enum TbFeature {
    Ft_EyeLens      =  0x0001,
    Ft_HiResVideo   =  0x0002,
    Ft_BigPointer   =  0x0004,
    Ft_HiResCreatr  =  0x0008,
    Ft_AdvAmbSound  =  0x0010,
    Ft_Censorship   =  0x0020,
    Ft_Atmossounds  =  0x0040,
    Ft_Resizemovies =  0x0080,
    Ft_FreezeOnLoseFocus            = 0x0400,
    Ft_UnlockCursorOnPause          = 0x0800,
    Ft_LockCursorInPossession       = 0x1000,
    Ft_PauseMusicOnGamePause        = 0x2000,
    Ft_MuteAudioOnLoseFocus         = 0x4000,
    Ft_SkipHeartZoom                = 0x8000,
    Ft_SkipSplashScreens            = 0x10000, // no longer used
    Ft_DisableCursorCameraPanning   = 0x20000,
    Ft_DeltaTime                    = 0x40000,
    Ft_NoCdMusic                    = 0x80000,
    Ft_RelativeMouseMode            = 0x100000,
    // 0x200000 was Ft_ClassicMenu (-classicmenu/-noimgui), retired once the
    // frontend menus had no legacy sprite path left to fall back to -- the
    // in-game HUD's own classic-vs-ImGui choice lives on GUI_ICON_PACK's
    // "CLASSIC" value instead (ingame_gui_use_classic_hud()), not a feature
    // bit. Left unused rather than reassigned, same as 0x10000 above.
};

// enum TbLanguage moved to globals.h (stage 13.3) -- see there.

enum StartupFlags {
    SFlg_Legal        =  0x01,
    SFlg_FX           =  0x02,
    SFlg_Bullfrog     =  0x04,
    SFlg_EA           =  0x08,
    SFlg_Intro        =  0x10,
};


#pragma pack(1)


/******************************************************************************/

struct InstallInfo {
  char inst_path[150];
  int64_t lang_id;
};

extern int64_t AtmosRepeat;
extern int64_t AtmosStart;
extern int64_t AtmosEnd;
extern TbBool AssignCpuKeepers;

extern uint64_t vid_scale_flags;

// UI/render tuning values resolved from config file commands, applied by
// their owning modules (engine_render.c/frontend.c/front_input.c/
// gui_draw.c) at startup instead of being written here directly, so this
// file doesn't need their headers. Defaults mirror each target global's
// own default. zoom_to_mouse_option/rotate_around_mouse_option are plain
// int (not front_input.h's enum types) for the same reason. See
// docs/refactor/stage-04-kfx-config.md issue B.
struct KeeperFxUiConfig {
    int64_t gui_blink_rate;
    int64_t neutral_flash_rate;
    int64_t creature_status_size;
    int64_t line_box_size;
    TbBool right_click_tag_mode_toggle;
    unsigned char default_tag_mode;
    int64_t zoom_to_mouse_option;
    int64_t rotate_around_mouse_option;
    // ImGui-frontend text size as a percentage of FeStylePushFont's own
    // resolution-derived base size (frontgui_style.cpp) -- no equivalent
    // option in original DK, KeeperFX-only. 100 = unscaled.
    int64_t ui_font_scale_pct;
    // ImGui-frontend typeface selector (frontgui_style.cpp / config_settingschema.c
    // UI_FONT row). "AUTO" = Exocet if the DK2 files are in fxdata/, else the
    // bundled Cinzel; "CINZEL"/"EXOCET" force one; anything else is a family
    // sub-directory name under fxdata/font/. KeeperFX-only, needs a restart.
    char ui_font[64];
    // In-game HUD sidebar position (frontgui_ingame_panel.cpp / config_settingschema.c
    // GUI_POSITION row): 1 = left edge, 2 = right edge, 3 = bottom strip
    // (docs/refactor/ingame-gui/11-horizontal-layout.md), 4 = minimal --
    // free-floating buttons + pop-up panels, no persistent panel
    // (docs/refactor/ingame-gui/13-minimal-layout.md), 5 = classic -- the legacy
    // sprite sidebar, CPU-drawn over the GPU world view (ingame_gui_use_classic_hud())
    // -- starts at 1, not
    // 0, see hud_position_type[]'s own comment (config_keeperfx.c). Plain int
    // (not a frontend enum type) for the same reason as zoom_to_mouse_option
    // above. KeeperFX-only.
    int64_t hud_position;
    // User PNG icon pack (frontgui_ingame_icon_overrides.cpp / config_settingschema.c
    // GUI_ICON_PACK row, docs/refactor/ingame-gui/12-png-icon-overrides.md): "NONE"
    // (default -- legacy sprites only) or a sub-directory name under
    // fxdata/gui/ whose same-named PNGs replace the matching HUD icon.
    // KeeperFX-only, live (checked lazily by the icon-override cache, same
    // poll-and-reload shape as UI_FONT's refresh_fonts_if_changed()).
    char gui_icon_pack[64];
    // Minimal layout only (hud_position == 4, config_settingschema.c
    // MINIMAP_CORNER row, docs/refactor/ingame-gui/13-minimal-layout.md):
    // which upper corner the minimap+gold+event-marker cluster sits in --
    // 1 = upper-left (default), 2 = upper-right (same 1-based-sentinel
    // reasoning as hud_position above; see minimap_corner_type[]'s own
    // comment, config_keeperfx.c). KeeperFX-only.
    int64_t minimap_corner;
    // Minimal layout only (hud_position == 4, config_settingschema.c
    // PANEL_CORNER row, docs/refactor/ingame-gui/13-minimal-layout.md):
    // which bottom corner the free-floating button cluster + its pop-up
    // panel sit in (always the bottom edge -- only left/right is a choice)
    // -- 1 = lower-left, 2 = lower-right (default, same 1-based-sentinel
    // reasoning as hud_position above; see panel_corner_type[]'s own
    // comment, config_keeperfx.c). Independent of minimap_corner -- either
    // may pick either corner, including both sharing one. KeeperFX-only.
    int64_t panel_corner;
    // OVERHEAD_FADE (case 63): strength of the overhead-view vignette (engine_render.c,
    // overhead_fade_shade()), 0..100: the edge of the screen is dimmed by this many percent
    // (0 = off, default 60). KeeperFX-only.
    int64_t overhead_fade;
};
extern struct KeeperFxUiConfig keeperfx_ui_config;

// Registered once at startup so freeze_game_on_focus_lost() doesn't need
// game_legacy.h's network_is_active() directly. Queried live since
// network state changes during a session.
typedef TbBool (*NetworkIsActiveFn)(void);
void set_config_network_is_active_check(NetworkIsActiveFn fn);
TbBool config_network_is_active(void);
/******************************************************************************/
extern struct InstallInfo install_info;
extern char keeper_runtime_directory[152];

#pragma pack()
/******************************************************************************/
extern uint64_t features_enabled;
extern const struct NamedCommand lang_type[];
extern const struct NamedCommand scrshot_type[];
// Exposed for config_settingschema.c's SOptT_Enum rows (ATMOS_FREQUENCY/
// DEFAULT_TAG_MODE) to reuse verbatim -- same tables the parser itself
// matches keeperfx.cfg values against. atmos_volume[] is only the parser's
// named presets now: ATMOS_VOLUME is a 0-ATMOS_VOLUME_MAX slider row.
extern const struct NamedCommand atmos_volume[];
extern const struct NamedCommand atmos_freq[];
extern const struct NamedCommand tag_modes[];
// RENDERER (gpu-v2 Phase C.1) -- same reuse as above.
extern const struct NamedCommand renderer_type[];
extern const struct NamedCommand lighting_type[];
extern const struct NamedCommand log_level_type[];
extern const struct NamedCommand hud_position_type[];
extern const struct NamedCommand minimap_corner_type[];
extern const struct NamedCommand panel_corner_type[];
extern char cmd_char;
extern int64_t api_enabled;
extern int64_t api_port;
extern TbBool exit_on_lua_error;
extern uint64_t packetsave_max_kb;
enum ReplayTypes {
    ReplTyp_Campaign = 0,
    ReplTyp_Freeplay,
    ReplTyp_Multiplayer,
    ReplTyp_Count,
};
extern uint64_t max_replays[ReplTyp_Count];
// AUTOMATIC_REPLAYS: record every game under replays/ (off unless the player turns it on;
// upstream KeeperFX records by default).
extern TbBool automatic_replays;
extern TbBool FLEE_BUTTON_DEFAULT;
extern TbBool IMPRISON_BUTTON_DEFAULT;
/** Test hook: reads one file as the base config (no mods, no keeperfx.cfg seeding), from a fresh start. */
void load_base_config_file_for_test(const char *fname);
/******************************************************************************/
void load_configuration_for_mod_all(void);
int64_t load_configuration(void);
/** Copies theirs (KeeperFX's keeperfx.cfg) to ours (this game's base config) when ours
 *  doesn't exist yet; theirs is never changed. True if it copied. */
TbBool import_kfx_base_config(const char *ours, const char *theirs);
void process_cmdline_overrides(void);
int64_t parse_draw_fps_config_val(const char *arg, int64_t *fps_draw_main, int64_t *fps_draw_secondary);
int64_t parse_atmos_volume_config_val(const char *arg);
/******************************************************************************/
// docs/refactor/renderer/04-imgui-gui-foundation.md §6.2 finding 1: the
// engine has never had a keeperfx.cfg writer -- save_settings() persists a
// different, much smaller binary struct. This is the comment- and
// order-preserving writer the settings screen (Phase G) needs to actually
// apply config-file-backed options, built first and independent of any
// ImGui work since it's plain text-file surgery.
struct KeeperfxCfgEdit {
    const char *key;   // a conf_commands[] name (config_keeperfx.c), matched case-insensitively
    const char *value; // raw value text to write after "KEY=", e.g. "50" or "AUTO 30"
};
// Rewrites the given keeperfx.cfg-format file in place: each edit's key is
// found the same way the loader recognizes it (recognize_conf_command's own
// case-insensitive, whitespace/'='-bounded match) and only that line's value
// is replaced -- every other line (comments, blank lines, key order, keys
// this call doesn't mention) is copied through untouched. A key with no
// existing line is appended at the end. If the same key appears more than
// once in the file, every occurrence is updated, so no stale duplicate is
// left behind. Exposed with an explicit path so it's independently testable
// against a fixture file; keeperfx_cfg_write_values() below is the
// real-usage wrapper.
TbBool keeperfx_cfg_write_values_to_file(const char *fname, const struct KeeperfxCfgEdit *edits, int64_t edits_count);
// Same as keeperfx_cfg_write_values_to_file(), targeting the exact path
// load_configuration() loaded from (including a "-config <file>" override),
// remembered at load time so a settings screen writes back to the file the
// game actually read, not a freshly re-resolved default.
TbBool keeperfx_cfg_write_values(const struct KeeperfxCfgEdit *edits, int64_t edits_count);
/******************************************************************************/
TbBool is_feature_on(uint64_t feature);
void set_skip_heart_zoom_feature(TbBool enable);
TbBool get_skip_heart_zoom_feature(void);
TbBool censorship_enabled(void);
TbBool atmos_sounds_enabled(void);
TbBool resize_movies_enabled(void);
TbBool freeze_game_on_focus_lost(void);
TbBool unlock_cursor_when_game_paused(void);
TbBool lock_cursor_in_possession(void);
TbBool use_relative_mouse_mode(void);
TbBool ingame_gui_use_classic_hud(void);
/** While set, ingame_gui_use_classic_hud() reports false regardless of GUI_ICON_PACK (the editor forces the ImGui HUD). */
void ingame_gui_force_imgui_hud(TbBool force);
TbBool pause_music_when_game_paused(void);
TbBool mute_audio_on_focus_lost(void);
// Had real external linkage but no header declaration at all; added
// (same situation as config_settings.c's setup_default_settings()).
TbBool prepare_diskpath(char *buf, int64_t buflen);
/******************************************************************************/
const char *get_language_lwrstr(int64_t lang_id);
TbBool is_dbc_language(int64_t language);
/******************************************************************************/

#ifdef __cplusplus
}
#endif
#endif
