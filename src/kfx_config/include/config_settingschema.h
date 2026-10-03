/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_settingschema.h
 *     Header file for config_settingschema.c.
 * @par Purpose:
 *     Declarative schema for keeperfx.cfg-backed settings options --
 *     docs/refactor/renderer/04-imgui-gui-foundation.md §6.3. One row per
 *     option (key, type, range, category/tab, apply-class, enable-condition,
 *     get/set accessors) so the Phase G settings screen can render *the
 *     schema* generically instead of a hand-placed widget per option.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_CONFIG_SETTINGSCHEMA_H
#define DK_CONFIG_SETTINGSCHEMA_H

#include "bflib_basics.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

// §6.3 calls for bool/enum/int/float/string/keybind/composite. Float/
// string/keybind/composite aren't modeled yet -- see config_settingschema.c's
// own header comment for which options are in the table today and which of
// §6.2's ~35 still need one of those.
enum SettingOptionType {
    SOptT_Bool,
    SOptT_Int,
    // A small closed set of named values (config_keeperfx.c's
    // struct NamedCommand tables, e.g. atmos_freq[]/tag_modes[]) --
    // get_enum/set_enum trade in the table's own .num values, not a
    // 0-based UI index; setting_option_enum_*() below do that translation
    // for callers (a combo box wants an index, keeperfx.cfg wants a name).
    SOptT_Enum,
    // A fire-and-forget action, not a stored value at all -- no cfg_key,
    // no get/set pair, just on_action(). The generic renderer draws a
    // plain button; docs/refactor/gui/05-campaign-progress-and-landview.md
    // §3.5/Phase E is the first (Reset Progress) and, as of this writing,
    // only row of this type -- the renderer gates every SOptT_Action
    // behind an "are you sure?" confirmation uniformly, not per-row, since
    // a fire-and-forget action in a settings screen is inherently
    // destructive-shaped even before this specific one is.
    SOptT_Action,
};

// Mirrors the settings screen's own tab grouping (§6.2/§6.3's own
// decision) so the schema's category maps directly to a settings-screen
// tab. SCat_GUI groups the KeeperFX-only HUD/interface options (the
// in-game GUI -> ImGui migration's font scale, etc.), separate from the
// engine's Graphics options.
enum SettingCategory {
    SCat_Game,
    SCat_Graphics,
    SCat_GUI,
    SCat_Sound,
    SCat_Input,
};

// §6.2's own "apply-class, which the UI must show honestly" finding: live
// options take effect immediately, needs-restart ones only show their
// effect after the engine restarts. Both still get their value applied to
// memory and written to keeperfx.cfg the moment the user changes them --
// "needs restart" is a UI-facing honesty label, not a deferred-write mode.
enum SettingApplyClass {
    SApply_Live,
    SApply_NeedsRestart,
};

struct SettingOption {
    // A conf_commands[] name (config_keeperfx.c) -- what
    // keeperfx_cfg_write_values() writes this option's value under.
    const char *cfg_key;
    enum SettingOptionType type;
    enum SettingCategory category;
    enum SettingApplyClass apply_class;
    // config_strings.h GUIStr_* ids. help_stridx == 0 means no tooltip
    // (0 is a real, unrelated low-numbered string id in get_string()'s own
    // space, not a sentinel) -- every row has one as of Phase G step 11.
    int64_t label_stridx;
    int64_t help_stridx;

    // When non-NULL, used verbatim as the on-screen label / help instead of
    // get_string(label_stridx / help_stridx). For KeeperFX-only rows added
    // after gtext_eng.pot's guitext numbering was frozen -- English-first,
    // the same accepted interim as the settings screen's own tab labels
    // ("Game"/"Graphics"/"GUI", frontgui_screens.cpp).
    const char *label_literal;
    const char *help_literal;

    // Show the row but disable it while the settings screen is the in-game
    // pause menu (s_options_in_game) -- same treatment SApply_NeedsRestart
    // rows already get, for a live option that is nonetheless only safe /
    // sensible to change from the main menu (UI_FONT: rebuilding the font
    // atlas is cheap in the frontend, undesirable mid-match).
    TbBool frontend_only;

    TbBool (*get_bool)(void);
    void (*set_bool)(TbBool val);

    int64_t (*get_int)(void);
    void (*set_int)(int64_t val);
    int64_t int_min;
    int64_t int_max;

    // SOptT_Enum only. enum_table is NULL-name-terminated (struct
    // NamedCommand's own convention); get_enum/set_enum trade in a
    // table entry's .num, e.g. atmos_freq[]'s 3200/800/400, not an index.
    const struct NamedCommand *enum_table;
    int64_t (*get_enum)(void);
    void (*set_enum)(int64_t val);

    // SOptT_Enum only, optional (NULL for every row whose enum_table is a
    // plain compile-time-constant array, e.g. LANGUAGE's lang_type[]).
    // When set, called first by every setting_option_enum_*() helper below
    // to (re)populate enum_table's *contents* in place before they're read
    // -- enum_table's own pointer never changes, only what it points to.
    // INGAME_RES is the one row that needs this: its list of resolutions
    // isn't known until the platform layer is queried, unlike every other
    // enum row's fixed NamedCommand table.
    void (*ensure_enum_table)(void);

    // §6.2 finding 4 ("some controls gate others"): NULL means always
    // enabled; otherwise the settings screen should show this option
    // disabled (but still visible) when this returns false.
    TbBool (*is_enabled)(void);

    // §6.2 finding 3 ("some controls are not 1:1 with keys"): true when
    // this bool option's on-screen sense is the logical inverse of what
    // actually gets written under cfg_key -- e.g. "Use CD Music" writes
    // MUSIC_FROM_DISK, whose own TRUE/FALSE meaning is the opposite.
    // Meaningless for SOptT_Int rows. Every row omits this (defaults to
    // false/not-inverted via C's aggregate-init zero-fill) unless it
    // actually needs it.
    TbBool cfg_bool_inverted;

    // Overrides how this option's value is written to keeperfx.cfg,
    // consulted by both setting_option_apply_bool() and _apply_int(). NULL
    // (most rows) means the generic per-type format already used
    // (TRUE/FALSE, "%ld", or enum_table[index].name). Needed when several
    // rows share one cfg_key built from more state than any single row's
    // own get_bool/get_int exposes -- STARTUP is a space-separated token
    // list (LEGAL/FX/BULLFROG/EA/INTRO) with two bool rows (splash
    // screens, intro) each toggling their own bits of it; FRAMES_PER_SECOND
    // is "AUTO [secondary]" or a plain number, with a bool row (auto
    // on/off) and an int row (the fixed-cap number) sharing it. On apply,
    // every row pointing at the same formatter writes the *whole*
    // reconstructed value -- hidden/unexposed bits (STARTUP's BULLFROG/EA,
    // FRAMES_PER_SECOND's secondary) preserved rather than dropped, not
    // just the one value being applied. Returns a pointer valid until the
    // next call (a shared static buffer), which is fine since it's read
    // once, immediately, inside the same apply call.
    const char *(*format_cfg_value)(void);

    // SOptT_Action only: the action itself. Not a keeperfx.cfg value --
    // nothing here reads/writes cfg_key.
    void (*on_action)(void);

    // SOptT_Bool/SOptT_Int only, opt-in (defaults false): this row's value
    // lives in struct GameSettings (config_settings.h) and persists to
    // save/settings.toml via save_settings(), not to keeperfx.cfg -- a
    // genuinely different backing store from every other row (the in-game
    // Video options menu's shadows/view_distance are the first two; both
    // already plain kfx_config-owned state, so get_bool/get_int/set_bool/
    // set_int read/write settings.* directly like any other accessor, no
    // extra plumbing needed for that half). cfg_key stays NULL for these
    // (nothing to look up in keeperfx.cfg), and setting_option_apply_bool/
    // _apply_int() call save_settings() instead of keeperfx_cfg_write_values()
    // when this is set.
    TbBool persist_via_save_settings;

    // SOptT_Int only, opt-in: a 0-VOLUME_SETTING_MAX volume, which the
    // settings screen shows as 0-100 like the other volume sliders
    // (volume_setting_to_percent(), config_settings.h). int_min/int_max
    // still bound the stored value.
    TbBool int_is_volume;
};

extern const struct SettingOption setting_options[];
extern const int64_t setting_options_count;

// Applies a new value to the live engine/config state via the option's own
// set_bool/set_int, then persists it -- to keeperfx.cfg via
// keeperfx_cfg_write_values() (config_keeperfx.h) under its cfg_key, or to
// save/settings.toml via save_settings() (config_settings.h) when
// opt->persist_via_save_settings is set. Does nothing (beyond an ERRORLOG)
// if opt->type doesn't match the call.
void setting_option_apply_bool(const struct SettingOption *opt, TbBool val);
void setting_option_apply_int(const struct SettingOption *opt, int64_t val);

// SOptT_Enum helpers -- translate between enum_table's own .num values (what
// keeperfx.cfg and get_enum/set_enum use) and a 0-based index (what a combo
// box widget wants). All three return/accept 0 for a NULL or malformed opt.
int64_t setting_option_enum_count(const struct SettingOption *opt);
int64_t setting_option_enum_current_index(const struct SettingOption *opt);
const char *setting_option_enum_item_name(const struct SettingOption *opt, int64_t index);
// Applies enum_table[index]'s value via set_enum, then persists
// enum_table[index]'s own *name* (the literal keeperfx.cfg token, e.g.
// "MEDIUM") under cfg_key.
void setting_option_apply_enum_index(const struct SettingOption *opt, int64_t index);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
