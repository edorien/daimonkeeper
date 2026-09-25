// kfx_config: config_settingschema.c -- the Phase G option schema's first,
// representative slice (docs/refactor/renderer/04-imgui-gui-foundation.md
// §6.3). Covers: the table's shape (count, one row per category, both
// apply-classes present), each row's get/set round-trips through the real
// underlying storage (features_enabled bits, keeperfx_ui_config fields,
// start_params.easter_egg, lbMouseGrab, display_id), the ALT_INPUT
// enable-condition pair, and setting_option_apply_bool/_int's clamping and
// type-mismatch guards.
//
// setting_option_apply_bool/_int's keeperfx_cfg_write_values() persistence
// half is not exercised here: that call targets loaded_keeperfx_cfg_path,
// which config_keeperfx_test.cpp's own header comment already establishes
// is only set by load_configuration() (a real file-I/O side effect against
// the actual game install, deliberately not attempted in this test binary)
// -- so apply_bool/_int's persistence call is expected to fail quietly
// (WARNMSG, return false) in this environment; only the live-apply half
// (set_bool/set_int actually being called) is checked.
#include <catch2/catch_test_macros.hpp>

#include "config_settingschema.h"
#include "config_keeperfx.h"
#include "config_settings.h" // struct GameSettings settings -- SHADOWS/VIEW_DISTANCE
#include "config_strings.h"
#include "kfx_config_state.h"
#include "bflib_mouse.h"
#include "bflib_video.h"
#include "bflib_sound.h"
#include "bflib_fmvids.h"
#include "renderer/RendererManager.h" // RENDERER's row

#include <cstring>
#include <cstdio>

namespace {
struct ResetSchemaState {
    uint64_t saved_features_enabled;
    TbBool saved_easter_egg;
    volatile TbBool saved_mouse_grab;
    int64_t saved_display_id;
    int64_t saved_gui_blink_rate;
    int64_t saved_creature_status_size;
    unsigned char saved_startup_flags;
    int64_t saved_num_fps_draw_main;
    int64_t saved_num_fps_draw_secondary;
    int64_t saved_line_box_size;
    int64_t saved_neutral_flash_rate;
    TbBool saved_tag_mode_toggle;
    TbBool saved_flee_button_default;
    TbBool saved_imprison_button_default;
    int64_t saved_atmos_volume;
    int64_t saved_atmos_frequency;
    int64_t saved_default_tag_mode;
    int64_t saved_lang_id;
    int64_t saved_zoom_to_mouse_option;
    int64_t saved_rotate_around_mouse_option;
    uint64_t saved_vid_scale_flags;
    int64_t saved_screen_mode;
    int64_t saved_ui_font_scale_pct;

    ResetSchemaState()
        : saved_features_enabled(features_enabled)
        , saved_easter_egg(start_params.easter_egg)
        , saved_mouse_grab(lbMouseGrab)
        , saved_display_id(display_id)
        , saved_gui_blink_rate(keeperfx_ui_config.gui_blink_rate)
        , saved_creature_status_size(keeperfx_ui_config.creature_status_size)
        , saved_startup_flags(start_params.startup_flags)
        , saved_num_fps_draw_main(start_params.num_fps_draw_main)
        , saved_num_fps_draw_secondary(start_params.num_fps_draw_secondary)
        , saved_line_box_size(keeperfx_ui_config.line_box_size)
        , saved_neutral_flash_rate(keeperfx_ui_config.neutral_flash_rate)
        , saved_tag_mode_toggle(keeperfx_ui_config.right_click_tag_mode_toggle)
        , saved_flee_button_default(FLEE_BUTTON_DEFAULT)
        , saved_imprison_button_default(IMPRISON_BUTTON_DEFAULT)
        , saved_atmos_volume(atmos_sound_volume)
        , saved_atmos_frequency(kfx_config_state.atmos_sound_frequency)
        , saved_default_tag_mode(keeperfx_ui_config.default_tag_mode)
        , saved_lang_id(install_info.lang_id)
        , saved_zoom_to_mouse_option(keeperfx_ui_config.zoom_to_mouse_option)
        , saved_rotate_around_mouse_option(keeperfx_ui_config.rotate_around_mouse_option)
        , saved_vid_scale_flags(vid_scale_flags)
        , saved_screen_mode(lbDisplay.ScreenMode)
        , saved_ui_font_scale_pct(keeperfx_ui_config.ui_font_scale_pct)
    {
        features_enabled = 0;
        start_params.easter_egg = false;
        lbMouseGrab = true;
    }
    ~ResetSchemaState()
    {
        features_enabled = saved_features_enabled;
        start_params.easter_egg = saved_easter_egg;
        lbMouseGrab = saved_mouse_grab;
        display_id = saved_display_id;
        keeperfx_ui_config.gui_blink_rate = saved_gui_blink_rate;
        keeperfx_ui_config.creature_status_size = saved_creature_status_size;
        start_params.startup_flags = saved_startup_flags;
        start_params.num_fps_draw_main = saved_num_fps_draw_main;
        start_params.num_fps_draw_secondary = saved_num_fps_draw_secondary;
        keeperfx_ui_config.line_box_size = saved_line_box_size;
        keeperfx_ui_config.neutral_flash_rate = saved_neutral_flash_rate;
        keeperfx_ui_config.right_click_tag_mode_toggle = saved_tag_mode_toggle;
        FLEE_BUTTON_DEFAULT = saved_flee_button_default;
        IMPRISON_BUTTON_DEFAULT = saved_imprison_button_default;
        atmos_sound_volume = saved_atmos_volume;
        kfx_config_state.atmos_sound_frequency = saved_atmos_frequency;
        keeperfx_ui_config.default_tag_mode = saved_default_tag_mode;
        install_info.lang_id = saved_lang_id;
        keeperfx_ui_config.zoom_to_mouse_option = saved_zoom_to_mouse_option;
        keeperfx_ui_config.rotate_around_mouse_option = saved_rotate_around_mouse_option;
        vid_scale_flags = saved_vid_scale_flags;
        lbDisplay.ScreenMode = saved_screen_mode;
        keeperfx_ui_config.ui_font_scale_pct = saved_ui_font_scale_pct;
    }
};

// SCREENSHOT/HAND_SIZE go through config_reload_callbacks (screenshot_format
// is kfx_render-owned, global_hand_scale is kfx_sim-owned -- neither
// reachable from this library directly). Same double-installing pattern
// config_sounds_test.cpp's own ResetConfigReloadCallbacks uses: copy the
// current (default noop) table, override just the fields under test.
struct ResetConfigReloadCallbacks {
    const struct ConfigReloadCallbacks *saved;
    ResetConfigReloadCallbacks() : saved(config_reload_callbacks) {}
    ~ResetConfigReloadCallbacks() { set_config_reload_callbacks(saved); }
};

const struct SettingOption *find_option(const char *cfg_key)
{
    for (int64_t i = 0; i < setting_options_count; i++) {
        if (std::strcmp(setting_options[i].cfg_key, cfg_key) == 0)
            return &setting_options[i];
    }
    return nullptr;
}

// STARTUP's two rows share one cfg_key (both reconstruct the whole token
// list on apply -- see format_cfg_value's own comment), so find_option()
// above only ever returns the first of them; distinguish by label instead.
const struct SettingOption *find_option_by_label(int64_t label_stridx)
{
    for (int64_t i = 0; i < setting_options_count; i++) {
        if (setting_options[i].label_stridx == label_stridx)
            return &setting_options[i];
    }
    return nullptr;
}
}

TEST_CASE("setting_options covers all five categories and both apply-classes", "[kfx_config][config_settingschema]") {
    REQUIRE(setting_options_count > 0);

    bool saw_game = false, saw_graphics = false, saw_gui = false, saw_sound = false, saw_input = false;
    bool saw_live = false, saw_needs_restart = false;
    for (int64_t i = 0; i < setting_options_count; i++) {
        const struct SettingOption &opt = setting_options[i];
        // SOptT_Action rows have no cfg_key -- they're not a keeperfx.cfg
        // value at all (enum SettingOptionType's own comment). Neither do
        // persist_via_save_settings rows -- they live in struct GameSettings
        // and persist to save/settings.toml instead (its own doc comment,
        // config_settingschema.h).
        if ((opt.type != SOptT_Action) && !opt.persist_via_save_settings)
            REQUIRE(opt.cfg_key != nullptr);
        // label_stridx == 0 is fine for a row that instead carries a
        // label_literal (KeeperFX-only rows added after gtext_eng.pot's
        // guitext numbering was frozen -- label_literal's own doc comment,
        // config_settingschema.h).
        REQUIRE(((opt.label_stridx != 0) || (opt.label_literal != nullptr)));
        // Phase G §6.3: every row now carries help text (get_string(0) isn't
        // "no help text" in its own id space, so a row must say so via 0
        // explicitly rather than accidentally leaving this unset) -- either
        // via help_stridx or, for the same label_literal rows above, a
        // help_literal.
        CHECK(((opt.help_stridx != 0) || (opt.help_literal != nullptr)));
        switch (opt.category) {
            case SCat_Game: saw_game = true; break;
            case SCat_Graphics: saw_graphics = true; break;
            case SCat_GUI: saw_gui = true; break;
            case SCat_Sound: saw_sound = true; break;
            case SCat_Input: saw_input = true; break;
        }
        if (opt.apply_class == SApply_Live) saw_live = true;
        else saw_needs_restart = true;

        // Exactly one accessor pair is populated, matching the row's type.
        if (opt.type == SOptT_Bool) {
            CHECK(opt.get_bool != nullptr);
            CHECK(opt.set_bool != nullptr);
            CHECK(opt.get_int == nullptr);
            CHECK(opt.set_int == nullptr);
            CHECK(opt.enum_table == nullptr);
        } else if (opt.type == SOptT_Enum) {
            CHECK(opt.enum_table != nullptr);
            CHECK(opt.get_enum != nullptr);
            CHECK(opt.set_enum != nullptr);
            CHECK(opt.get_bool == nullptr);
            CHECK(opt.get_int == nullptr);
            if (opt.ensure_enum_table != nullptr) {
                // INGAME_RES: enum_table starts out zero-initialized (no
                // entries) until ensure_enum_table() queries the platform
                // layer, and even then may legitimately come back empty in
                // an environment with no detected fullscreen resolutions
                // (this test binary's, potentially) -- so "at least one
                // entry" doesn't hold; just confirm populating it is safe
                // and the index helpers stay in range.
                opt.ensure_enum_table();
                int64_t count = setting_option_enum_count(&opt);
                int64_t current = setting_option_enum_current_index(&opt);
                CHECK(current >= 0);
                CHECK(current < (count > 0 ? count : 1));
            } else {
                REQUIRE(opt.enum_table[0].name != nullptr); // at least one entry
            }
        } else if (opt.type == SOptT_Int) {
            CHECK(opt.get_int != nullptr);
            CHECK(opt.set_int != nullptr);
            CHECK(opt.get_bool == nullptr);
            CHECK(opt.set_bool == nullptr);
            CHECK(opt.enum_table == nullptr);
            CHECK(opt.int_min <= opt.int_max);
        } else { // SOptT_Action
            CHECK(opt.on_action != nullptr);
            CHECK(opt.get_bool == nullptr);
            CHECK(opt.get_int == nullptr);
            CHECK(opt.enum_table == nullptr);
        }
    }
    CHECK(saw_game);
    CHECK(saw_graphics);
    CHECK(saw_gui);
    CHECK(saw_sound);
    CHECK(saw_input);
    CHECK(saw_live);
    CHECK(saw_needs_restart);
}

TEST_CASE_METHOD(ResetSchemaState, "FREEZE_GAME_ON_FOCUS_LOST's get/set round-trip through features_enabled", "[kfx_config][config_settingschema]") {
    const struct SettingOption *opt = find_option("FREEZE_GAME_ON_FOCUS_LOST");
    REQUIRE(opt != nullptr);
    CHECK_FALSE(opt->get_bool());
    opt->set_bool(true);
    CHECK(opt->get_bool());
    CHECK(is_feature_on(Ft_FreezeOnLoseFocus));
}

TEST_CASE_METHOD(ResetSchemaState, "EASTER_EGG's get/set round-trip through start_params.easter_egg", "[kfx_config][config_settingschema]") {
    const struct SettingOption *opt = find_option("EASTER_EGG");
    REQUIRE(opt != nullptr);
    CHECK_FALSE(opt->get_bool());
    opt->set_bool(true);
    CHECK(start_params.easter_egg);
    CHECK(opt->get_bool());
}

TEST_CASE_METHOD(ResetSchemaState, "GUI_BLINK_RATE's get/set round-trip through keeperfx_ui_config", "[kfx_config][config_settingschema]") {
    const struct SettingOption *opt = find_option("GUI_BLINK_RATE");
    REQUIRE(opt != nullptr);
    opt->set_int(42);
    CHECK(keeperfx_ui_config.gui_blink_rate == 42);
    CHECK(opt->get_int() == 42);
}

TEST_CASE_METHOD(ResetSchemaState, "DISPLAY_NUMBER's get/set present a 1-based value over 0-based display_id storage", "[kfx_config][config_settingschema]") {
    const struct SettingOption *opt = find_option("DISPLAY_NUMBER");
    REQUIRE(opt != nullptr);
    opt->set_int(2); // "the second monitor"
    CHECK(display_id == 1);
    CHECK(opt->get_int() == 2);
}

TEST_CASE_METHOD(ResetSchemaState, "ALT_INPUT's enable-condition pair flips which of Unlock/Lock Cursor is enabled", "[kfx_config][config_settingschema]") {
    const struct SettingOption *alt_input = find_option("ALT_INPUT");
    const struct SettingOption *unlock = find_option("UNLOCK_CURSOR_WHEN_GAME_PAUSED");
    const struct SettingOption *lock = find_option("LOCK_CURSOR_IN_POSSESSION");
    REQUIRE(alt_input != nullptr);
    REQUIRE(unlock != nullptr);
    REQUIRE(lock != nullptr);
    REQUIRE(unlock->is_enabled != nullptr);
    REQUIRE(lock->is_enabled != nullptr);

    alt_input->set_bool(false);
    CHECK(unlock->is_enabled());
    CHECK_FALSE(lock->is_enabled());

    alt_input->set_bool(true);
    CHECK_FALSE(unlock->is_enabled());
    CHECK(lock->is_enabled());
}

TEST_CASE_METHOD(ResetSchemaState, "setting_option_apply_bool calls set_bool even though persistence has nowhere to write in this test binary", "[kfx_config][config_settingschema]") {
    const struct SettingOption *opt = find_option("MUTE_AUDIO_ON_FOCUS_LOST");
    REQUIRE(opt != nullptr);
    setting_option_apply_bool(opt, true);
    CHECK(is_feature_on(Ft_MuteAudioOnLoseFocus));
}

TEST_CASE_METHOD(ResetSchemaState, "setting_option_apply_int clamps to the option's own range", "[kfx_config][config_settingschema]") {
    const struct SettingOption *opt = find_option("GUI_BLINK_RATE"); // range 1..160
    REQUIRE(opt != nullptr);
    setting_option_apply_int(opt, 9999);
    CHECK(keeperfx_ui_config.gui_blink_rate == 160);
    setting_option_apply_int(opt, -5);
    CHECK(keeperfx_ui_config.gui_blink_rate == 1);
}

TEST_CASE_METHOD(ResetSchemaState, "setting_option_apply_bool/_int reject a type-mismatched option", "[kfx_config][config_settingschema]") {
    const struct SettingOption *bool_opt = find_option("EASTER_EGG");
    const struct SettingOption *int_opt = find_option("GUI_BLINK_RATE");
    REQUIRE(bool_opt != nullptr);
    REQUIRE(int_opt != nullptr);

    setting_option_apply_int(bool_opt, 1); // wrong accessor for a bool row -- must not touch it
    CHECK_FALSE(start_params.easter_egg);

    int64_t before = keeperfx_ui_config.gui_blink_rate;
    setting_option_apply_bool(int_opt, true); // wrong accessor for an int row
    CHECK(keeperfx_ui_config.gui_blink_rate == before);
}

TEST_CASE_METHOD(ResetSchemaState, "CENSORSHIP/ATMOSPHERIC_SOUNDS/FLEE_BUTTON_DEFAULT/IMPRISON_BUTTON_DEFAULT round-trip through their own storage", "[kfx_config][config_settingschema]") {
    const struct SettingOption *censorship = find_option("CENSORSHIP");
    const struct SettingOption *atmos = find_option("ATMOSPHERIC_SOUNDS");
    const struct SettingOption *flee = find_option("FLEE_BUTTON_DEFAULT");
    const struct SettingOption *imprison = find_option("IMPRISON_BUTTON_DEFAULT");
    REQUIRE(censorship != nullptr);
    REQUIRE(atmos != nullptr);
    REQUIRE(flee != nullptr);
    REQUIRE(imprison != nullptr);

    censorship->set_bool(true);
    CHECK(is_feature_on(Ft_Censorship));
    CHECK(censorship->get_bool());

    atmos->set_bool(true);
    CHECK(is_feature_on(Ft_Atmossounds));
    CHECK(atmos->get_bool());

    flee->set_bool(true);
    CHECK(FLEE_BUTTON_DEFAULT);
    imprison->set_bool(true);
    CHECK(IMPRISON_BUTTON_DEFAULT);
}

TEST_CASE_METHOD(ResetSchemaState, "LINE_BOX_SIZE/NEUTRAL_FLASH_RATE round-trip through keeperfx_ui_config", "[kfx_config][config_settingschema]") {
    const struct SettingOption *line_box = find_option("LINE_BOX_SIZE");
    const struct SettingOption *flash = find_option("NEUTRAL_FLASH_RATE");
    REQUIRE(line_box != nullptr);
    REQUIRE(flash != nullptr);

    line_box->set_int(200);
    CHECK(keeperfx_ui_config.line_box_size == 200);
    CHECK(line_box->get_int() == 200);

    flash->set_int(80);
    CHECK(keeperfx_ui_config.neutral_flash_rate == 80);
    CHECK(flash->get_int() == 80);
}

TEST_CASE_METHOD(ResetSchemaState, "CURSOR_EDGE_CAMERA_PANNING/TAG_MODE_TOGGLING round-trip through their own storage", "[kfx_config][config_settingschema]") {
    const struct SettingOption *panning = find_option("CURSOR_EDGE_CAMERA_PANNING");
    const struct SettingOption *tag_toggle = find_option("TAG_MODE_TOGGLING");
    REQUIRE(panning != nullptr);
    REQUIRE(tag_toggle != nullptr);

    panning->set_bool(false); // "panning off" sets the inverted Ft_DisableCursorCameraPanning flag
    CHECK(is_feature_on(Ft_DisableCursorCameraPanning));
    CHECK_FALSE(panning->get_bool());
    panning->set_bool(true);
    CHECK_FALSE(is_feature_on(Ft_DisableCursorCameraPanning));
    CHECK(panning->get_bool());

    tag_toggle->set_bool(true);
    CHECK(keeperfx_ui_config.right_click_tag_mode_toggle);
    CHECK(tag_toggle->get_bool());
}

// §6.2 finding 3 ("some controls are not 1:1 with keys"): "Use CD Music"
// (schema row "MUSIC_FROM_DISK") is stored as the *inverse* of the
// MUSIC_FROM_DISK config key's own TRUE/FALSE meaning -- get_bool/set_bool
// present the user-facing sense, cfg_bool_inverted flips it back for the
// actual keeperfx.cfg write inside setting_option_apply_bool().
TEST_CASE_METHOD(ResetSchemaState, "MUSIC_FROM_DISK's row presents the inverse of Ft_NoCdMusic and is marked cfg_bool_inverted", "[kfx_config][config_settingschema]") {
    const struct SettingOption *opt = find_option("MUSIC_FROM_DISK");
    REQUIRE(opt != nullptr);
    CHECK(opt->cfg_bool_inverted);

    features_enabled |= Ft_NoCdMusic; // config says "don't use CD" (disk/OGG music)
    CHECK_FALSE(opt->get_bool()); // "Use CD Music" reads as off

    opt->set_bool(true); // user turns "Use CD Music" on
    CHECK_FALSE(is_feature_on(Ft_NoCdMusic));
}

TEST_CASE_METHOD(ResetSchemaState, "ATMOS_VOLUME/ATMOS_FREQUENCY round-trip through their own storage via the enum_table's .num values", "[kfx_config][config_settingschema]") {
    const struct SettingOption *vol = find_option("ATMOS_VOLUME");
    const struct SettingOption *freq = find_option("ATMOS_FREQUENCY");
    REQUIRE(vol != nullptr);
    REQUIRE(freq != nullptr);

    vol->set_enum(128); // "MEDIUM"
    CHECK(atmos_sound_volume == 128);
    CHECK(vol->get_enum() == 128);

    freq->set_enum(400); // "HIGH"
    CHECK(kfx_config_state.atmos_sound_frequency == 400);
    CHECK(freq->get_enum() == 400);
}

TEST_CASE_METHOD(ResetSchemaState, "DEFAULT_TAG_MODE round-trips through keeperfx_ui_config.default_tag_mode", "[kfx_config][config_settingschema]") {
    const struct SettingOption *opt = find_option("DEFAULT_TAG_MODE");
    REQUIRE(opt != nullptr);
    opt->set_enum(2); // "DRAG"
    CHECK(keeperfx_ui_config.default_tag_mode == 2);
    CHECK(opt->get_enum() == 2);
}

TEST_CASE_METHOD(ResetSchemaState, "setting_option_enum_* translate between a combo box index and the enum_table's own .num values", "[kfx_config][config_settingschema]") {
    const struct SettingOption *vol = find_option("ATMOS_VOLUME"); // LOW=64, MEDIUM=128, HIGH=255
    REQUIRE(vol != nullptr);
    REQUIRE(setting_option_enum_count(vol) == 3);
    CHECK(std::strcmp(setting_option_enum_item_name(vol, 0), "LOW") == 0);
    CHECK(std::strcmp(setting_option_enum_item_name(vol, 1), "MEDIUM") == 0);
    CHECK(std::strcmp(setting_option_enum_item_name(vol, 2), "HIGH") == 0);

    vol->set_enum(255); // HIGH, directly via the raw accessor
    CHECK(setting_option_enum_current_index(vol) == 2);

    setting_option_apply_enum_index(vol, 0); // pick LOW by index
    CHECK(atmos_sound_volume == 64);
    CHECK(setting_option_enum_current_index(vol) == 0);
}

TEST_CASE_METHOD(ResetSchemaState, "setting_option_enum_* return 0/empty for a non-enum option rather than misreading its union of fields", "[kfx_config][config_settingschema]") {
    const struct SettingOption *bool_opt = find_option("EASTER_EGG");
    REQUIRE(bool_opt != nullptr);
    CHECK(setting_option_enum_count(bool_opt) == 0);
    CHECK(setting_option_enum_current_index(bool_opt) == 0);
    CHECK(std::strcmp(setting_option_enum_item_name(bool_opt, 0), "") == 0);
}

TEST_CASE_METHOD(ResetSchemaState, "LANGUAGE reuses lang_type[] verbatim and round-trips through install_info.lang_id", "[kfx_config][config_settingschema]") {
    const struct SettingOption *opt = find_option("LANGUAGE");
    REQUIRE(opt != nullptr);
    CHECK(opt->enum_table == lang_type);
    // lang_type[] has 24 real entries (config_keeperfx.c) -- this also
    // guards against the renderer's old fixed-size-8 items[] array
    // regressing (frontgui_screens.cpp now uses a std::vector instead).
    CHECK(setting_option_enum_count(opt) == 24);

    opt->set_enum(Lang_French);
    CHECK(install_info.lang_id == Lang_French);
    CHECK(opt->get_enum() == Lang_French);
}

TEST_CASE_METHOD(ResetSchemaState, "DELTA_TIME round-trips through Ft_DeltaTime", "[kfx_config][config_settingschema]") {
    const struct SettingOption *opt = find_option("DELTA_TIME");
    REQUIRE(opt != nullptr);
    CHECK_FALSE(opt->get_bool());
    opt->set_bool(true);
    CHECK(is_feature_on(Ft_DeltaTime));
    CHECK(opt->get_bool());
}

// ZOOM_TO_MOUSE/ROTATE_AROUND_MOUSE use purpose-built local tables (no
// single existing NamedCommand table covers ALWAYS/NEVER plus the
// WHEEL/rotation-key tokens the way atmos_volume[] does for its option) --
// pins down that their .num values match keeperfx_ui_config's own storage
// convention (ZoomToMouse_Always == 3, RotateAroundMouse_Never == 1, etc.).
TEST_CASE_METHOD(ResetSchemaState, "ZOOM_TO_MOUSE's table values match keeperfx_ui_config.zoom_to_mouse_option's own convention", "[kfx_config][config_settingschema]") {
    const struct SettingOption *opt = find_option("ZOOM_TO_MOUSE");
    REQUIRE(opt != nullptr);
    REQUIRE(setting_option_enum_count(opt) == 3);
    CHECK(std::strcmp(setting_option_enum_item_name(opt, 0), "NEVER") == 0);
    CHECK(std::strcmp(setting_option_enum_item_name(opt, 1), "WHEEL") == 0);
    CHECK(std::strcmp(setting_option_enum_item_name(opt, 2), "ALWAYS") == 0);

    setting_option_apply_enum_index(opt, 2); // ALWAYS
    CHECK(keeperfx_ui_config.zoom_to_mouse_option == 3); // ZoomToMouse_Always
    setting_option_apply_enum_index(opt, 0); // NEVER
    CHECK(keeperfx_ui_config.zoom_to_mouse_option == 1); // ZoomToMouse_Never
}

TEST_CASE_METHOD(ResetSchemaState, "ROTATE_AROUND_MOUSE's table values match keeperfx_ui_config.rotate_around_mouse_option's own convention", "[kfx_config][config_settingschema]") {
    const struct SettingOption *opt = find_option("ROTATE_AROUND_MOUSE");
    REQUIRE(opt != nullptr);
    REQUIRE(setting_option_enum_count(opt) == 4);
    CHECK(std::strcmp(setting_option_enum_item_name(opt, 0), "NEVER") == 0);
    CHECK(std::strcmp(setting_option_enum_item_name(opt, 3), "ALWAYS") == 0);

    setting_option_apply_enum_index(opt, 3); // ALWAYS
    CHECK(keeperfx_ui_config.rotate_around_mouse_option == 4); // RotateAroundMouse_Always
    setting_option_apply_enum_index(opt, 0); // NEVER
    CHECK(keeperfx_ui_config.rotate_around_mouse_option == 1); // RotateAroundMouse_Never
}

TEST_CASE_METHOD(ResetConfigReloadCallbacks, "SCREENSHOT reuses scrshot_type[] and round-trips through config_reload_callbacks->get_screenshot_format/set_screenshot_format", "[kfx_config][config_settingschema]") {
    const struct SettingOption *opt = find_option("SCREENSHOT");
    REQUIRE(opt != nullptr);
    CHECK(opt->enum_table == scrshot_type);

    struct ConfigReloadCallbacks fake = *config_reload_callbacks;
    static unsigned char fake_format = 1;
    fake_format = 1;
    fake.get_screenshot_format = []() -> unsigned char { return fake_format; };
    fake.set_screenshot_format = [](unsigned char val) { fake_format = val; };
    set_config_reload_callbacks(&fake);

    CHECK(setting_option_enum_current_index(opt) == 0); // PNG == 1, index 0
    setting_option_apply_enum_index(opt, 1); // BMP == 2
    CHECK(fake_format == 2);
    CHECK(opt->get_enum() == 2);
}

// gpu-v2 Phase C.1: unlike SCREENSHOT/INGAME_RES above, RENDERER's
// get_enum/set_enum call RendererGetDesiredType/RendererSetDesiredType
// (renderer/RendererManager.h) directly -- kfx_platform is a layer *below*
// kfx_config, so no config_reload_callbacks indirection is needed. No
// ResetConfigReloadCallbacks fixture required for the same reason.
TEST_CASE("RENDERER reuses renderer_type[] and round-trips through RendererGetDesiredType/RendererSetDesiredType", "[kfx_config][config_settingschema]") {
    RendererSetDesiredType(RENDERER_SOFTWARE); // known starting state
    const struct SettingOption *opt = find_option("RENDERER");
    REQUIRE(opt != nullptr);
    CHECK(opt->enum_table == renderer_type);

    CHECK(setting_option_enum_current_index(opt) == 0); // SOFTWARE == 1, index 0
    setting_option_apply_enum_index(opt, 1); // VULKAN == RENDERER_GPU3D
    CHECK(RendererGetDesiredType() == RENDERER_GPU3D);
    CHECK(opt->get_enum() == RENDERER_GPU3D);

    RendererSetDesiredType(RENDERER_SOFTWARE); // leave global state as found
}

TEST_CASE_METHOD(ResetConfigReloadCallbacks, "HAND_SIZE presents global_hand_scale as an integer percentage, matching HAND_SIZE's own keeperfx.cfg format", "[kfx_config][config_settingschema]") {
    const struct SettingOption *opt = find_option("HAND_SIZE");
    REQUIRE(opt != nullptr);
    CHECK(opt->type == SOptT_Int);

    struct ConfigReloadCallbacks fake = *config_reload_callbacks;
    static double fake_scale = 1.0;
    fake_scale = 1.0;
    fake.get_hand_scale = []() -> double { return fake_scale; };
    fake.set_hand_scale = [](double val) { fake_scale = val; };
    set_config_reload_callbacks(&fake);

    CHECK(opt->get_int() == 100); // 1.0 scale == 100%
    opt->set_int(150);
    CHECK(fake_scale == 1.5);
}

// RESIZE_MOVIES is two storage locations (Ft_Resizemovies + vid_scale_flags)
// folded into one option, via a purpose-built table rather than reusing
// vidscale_type[]'s alias-laden entries verbatim -- pins down that OFF
// clears the feature flag entirely (leaving vid_scale_flags untouched) and
// that each non-OFF entry both sets the flag and writes the right combined
// vid_scale_flags value.
TEST_CASE_METHOD(ResetSchemaState, "RESIZE_MOVIES combines Ft_Resizemovies and vid_scale_flags into one enum row", "[kfx_config][config_settingschema]") {
    const struct SettingOption *opt = find_option("RESIZE_MOVIES");
    REQUIRE(opt != nullptr);

    features_enabled &= ~Ft_Resizemovies;
    CHECK(opt->get_enum() == 0); // OFF regardless of vid_scale_flags' leftover value

    setting_option_apply_enum_index(opt, 4); // "4BY3"
    CHECK(is_feature_on(Ft_Resizemovies));
    CHECK(vid_scale_flags == (SMK_FullscreenFit | SMK_FullscreenStretch));
    CHECK(opt->get_enum() == (int64_t)(SMK_FullscreenFit | SMK_FullscreenStretch));

    setting_option_apply_enum_index(opt, 0); // "OFF"
    CHECK_FALSE(is_feature_on(Ft_Resizemovies));
    CHECK(opt->get_enum() == 0);
}

TEST_CASE_METHOD(ResetConfigReloadCallbacks, "POINTER_SENSITIVITY presents base_mouse_sensitivity as the same integer percentage keeperfx.cfg uses", "[kfx_config][config_settingschema]") {
    const struct SettingOption *opt = find_option("POINTER_SENSITIVITY");
    REQUIRE(opt != nullptr);
    CHECK(opt->type == SOptT_Int);

    struct ConfigReloadCallbacks fake = *config_reload_callbacks;
    static int64_t fake_sensitivity = 256; // config_keeperfx.c's own case 9: i*256/100, default i==100
    fake_sensitivity = 256;
    fake.get_base_mouse_sensitivity = []() -> int64_t { return fake_sensitivity; };
    fake.set_base_mouse_sensitivity = [](int64_t val) { fake_sensitivity = val; };
    set_config_reload_callbacks(&fake);

    CHECK(opt->get_int() == 100); // 256 scaled back down == 100%
    opt->set_int(50);
    CHECK(fake_sensitivity == 128); // 50*256/100
}

// STARTUP: two rows sharing one cfg_key, each toggling its own bits of
// start_params.startup_flags but both writing the *whole* reconstructed
// token list on apply -- pins down that BULLFROG/EA's hidden bits survive
// untouched when either checkbox is toggled, and that get_bool for each
// row reflects only its own bits.
TEST_CASE_METHOD(ResetSchemaState, "STARTUP's splash-screens row toggles LEGAL and FX together, independent of the intro row", "[kfx_config][config_settingschema]") {
    const struct SettingOption *splash = find_option_by_label(GUIStr_SetStartupSplash);
    const struct SettingOption *intro = find_option_by_label(GUIStr_SetStartupIntro);
    REQUIRE(splash != nullptr);
    REQUIRE(intro != nullptr);
    CHECK(std::strcmp(splash->cfg_key, "STARTUP") == 0);
    CHECK(std::strcmp(intro->cfg_key, "STARTUP") == 0);

    start_params.startup_flags = SFlg_Legal | SFlg_FX | SFlg_Intro;
    CHECK(splash->get_bool());
    CHECK(intro->get_bool());

    splash->set_bool(false);
    CHECK_FALSE(splash->get_bool());
    CHECK(intro->get_bool()); // untouched by the splash row's own set_bool
    CHECK((start_params.startup_flags & (SFlg_Legal | SFlg_FX)) == 0);
    CHECK((start_params.startup_flags & SFlg_Intro) != 0);
}

TEST_CASE_METHOD(ResetSchemaState, "STARTUP's format_cfg_value reconstructs the whole token list, preserving BULLFROG/EA's hidden bits", "[kfx_config][config_settingschema]") {
    const struct SettingOption *splash = find_option_by_label(GUIStr_SetStartupSplash);
    const struct SettingOption *intro = find_option_by_label(GUIStr_SetStartupIntro);
    REQUIRE(splash != nullptr);
    REQUIRE(intro != nullptr);
    REQUIRE(splash->format_cfg_value != nullptr);
    REQUIRE(intro->format_cfg_value == splash->format_cfg_value); // same shared formatter

    // Simulate a config file that already had the legacy hidden tokens.
    start_params.startup_flags = SFlg_Legal | SFlg_FX | SFlg_Bullfrog | SFlg_EA | SFlg_Intro;
    CHECK(std::strcmp(splash->format_cfg_value(), "LEGAL FX BULLFROG EA INTRO") == 0);

    setting_option_apply_bool(intro, false); // turn off just the intro checkbox
    CHECK_FALSE(intro->get_bool());
    CHECK(start_params.startup_flags & SFlg_Bullfrog); // hidden bits survive
    CHECK(start_params.startup_flags & SFlg_EA);
    CHECK(std::strcmp(splash->format_cfg_value(), "LEGAL FX BULLFROG EA") == 0);
}

// FRAMES_PER_SECOND: two rows (auto bool, limit int) sharing one cfg_key,
// same shared-key + format_cfg_value shape as STARTUP. Pins down the -1
// sentinel (bflib_video.h's own "-1 if auto" comment, confirmed against
// redetect_screen_refresh_rate_for_draw()'s actual use of it), that the
// limit row is gated off while Auto is on, and that num_fps_draw_secondary
// (not exposed as its own control) survives an apply untouched.
TEST_CASE_METHOD(ResetSchemaState, "FRAMES_PER_SECOND's Auto row toggles the -1 sentinel and gates the Limit row", "[kfx_config][config_settingschema]") {
    const struct SettingOption *auto_row = find_option_by_label(GUIStr_SetFpsAuto);
    const struct SettingOption *limit_row = find_option_by_label(GUIStr_SetFpsLimit);
    REQUIRE(auto_row != nullptr);
    REQUIRE(limit_row != nullptr);
    CHECK(std::strcmp(auto_row->cfg_key, "FRAMES_PER_SECOND") == 0);
    CHECK(std::strcmp(limit_row->cfg_key, "FRAMES_PER_SECOND") == 0);
    REQUIRE(limit_row->is_enabled != nullptr);

    start_params.num_fps_draw_main = 0; // the field's own real default: uncapped
    CHECK_FALSE(auto_row->get_bool());
    CHECK(limit_row->get_int() == 0);
    CHECK(limit_row->is_enabled()); // enabled while not Auto

    auto_row->set_bool(true);
    CHECK(start_params.num_fps_draw_main == -1);
    CHECK_FALSE(limit_row->is_enabled()); // greyed out while Auto is on

    auto_row->set_bool(false);
    CHECK(start_params.num_fps_draw_main == 0);

    limit_row->set_int(60);
    CHECK(start_params.num_fps_draw_main == 60);
    CHECK(limit_row->get_int() == 60);
}

TEST_CASE_METHOD(ResetSchemaState, "FRAMES_PER_SECOND's format_cfg_value writes AUTO/a plain number and preserves num_fps_draw_secondary", "[kfx_config][config_settingschema]") {
    const struct SettingOption *auto_row = find_option_by_label(GUIStr_SetFpsAuto);
    const struct SettingOption *limit_row = find_option_by_label(GUIStr_SetFpsLimit);
    REQUIRE(auto_row != nullptr);
    REQUIRE(limit_row != nullptr);
    REQUIRE(auto_row->format_cfg_value != nullptr);
    REQUIRE(limit_row->format_cfg_value == auto_row->format_cfg_value);

    start_params.num_fps_draw_main = 30;
    start_params.num_fps_draw_secondary = 0;
    CHECK(std::strcmp(auto_row->format_cfg_value(), "30") == 0);

    // A config file that already set a secondary (Auto-mode fallback) value.
    start_params.num_fps_draw_secondary = 75;
    setting_option_apply_bool(auto_row, true); // switch to Auto
    CHECK(std::strcmp(auto_row->format_cfg_value(), "AUTO 75") == 0);
    CHECK(start_params.num_fps_draw_secondary == 75); // untouched by the Auto row's own set_bool
}

// INGAME_RES (Phase G step 12): the one row whose enum_table is built at
// runtime (ensure_enum_table) from PlatformManager_GetFullscreenDisplayMode*
// rather than being a compile-time NamedCommand array -- see the row's own
// comment (config_settingschema.c) for why, and .num's (width<<16)|height
// encoding, mirrored here rather than shared, since both are file-local.
namespace {
int64_t ingame_res_encode(int64_t w, int64_t h) { return ((int64_t)w << 16) | (int64_t)(h & 0xFFFF); }

// Lb_SCREEN_MODE_INVALID is 0 -- indistinguishable from a real mode that
// happens to land at registry index 0 (LbRegisterVideoMode's own return
// value, bflib_video.c). This used to be assumed safe in the real game
// because LbRegisterStandardVideoModes() fills index 0 before any user
// config gets parsed -- but that assumption was wrong: load_configuration()
// (main.cpp) runs well before LbScreenInitialize() (which is what actually
// calls LbRegisterStandardVideoModes()), so a fresh install's very first
// INGAME_RES line was landing on index 0 and being silently rejected by the
// "mode > 0" check in its case-7 parser (config_keeperfx.c) -- confirmed
// live: INGAME_RES never actually changed the window's real resolution
// across a restart. Fixed by calling LbRegisterDefaultVideoModesIfNeeded()
// early in setup_game(), before load_configuration(); this test binary never
// goes through setup_game() at all, so it still needs its own reservation --
// call this before registering/reading any mode this test actually cares
// about.
void ensure_screen_mode_registry_nonempty()
{
    LbRegisterVideoModeString("320x200x32");
}
}

TEST_CASE("INGAME_RES's row shape: NeedsRestart, Graphics, dynamic table", "[kfx_config][config_settingschema]") {
    const struct SettingOption *opt = find_option("INGAME_RES");
    REQUIRE(opt != nullptr);
    CHECK(opt->type == SOptT_Enum);
    CHECK(opt->category == SCat_Graphics);
    CHECK(opt->apply_class == SApply_NeedsRestart); // takes effect next launch, like DISPLAY_NUMBER
    CHECK(opt->label_stridx == GUIStr_SetIngameRes);
    CHECK(opt->help_stridx == GUIStr_HelpIngameRes);
    REQUIRE(opt->ensure_enum_table != nullptr);

    // Calling it repeatedly is safe (idempotent lazy-populate, not a
    // re-query every time) and never leaves a malformed (non-NULL-name'd
    // final) table, whatever the detected mode count in this environment.
    opt->ensure_enum_table();
    opt->ensure_enum_table();
    int64_t count = setting_option_enum_count(opt);
    CHECK(opt->enum_table[count].name == nullptr);
    for (int64_t i = 0; i < count; i++) {
        CHECK(opt->enum_table[i].name != nullptr);
        CHECK((opt->enum_table[i].num >> 16) > 0);       // width
        CHECK((opt->enum_table[i].num & 0xFFFF) > 0);    // height
    }
}

TEST_CASE_METHOD(ResetSchemaState, "INGAME_RES's get_enum reads the pending screen mode, not the currently-active one", "[kfx_config][config_settingschema]") {
    // INGAME_RES is SApply_NeedsRestart -- get_ingame_res() must read
    // get_screen_vidmode() (the pending mode, updated immediately when a
    // new resolution is picked), not LbScreenActiveMode() (which only
    // changes on next launch). Reading the active mode would make the
    // combo immediately revert to the old resolution the instant a new one
    // is chosen -- confirmed live ("the ingame res button isnt changing
    // from 640x when clicked").
    ResetConfigReloadCallbacks callbacks_guard;
    const struct SettingOption *opt = find_option("INGAME_RES");
    REQUIRE(opt != nullptr);

    ensure_screen_mode_registry_nonempty();
    TbScreenMode active_mode = LbRegisterVideoModeString("640x480x32");
    REQUIRE(active_mode != Lb_SCREEN_MODE_INVALID);
    static TbScreenMode pending_mode = Lb_SCREEN_MODE_INVALID;
    pending_mode = LbRegisterVideoModeString("800x600x32");
    REQUIRE(pending_mode != Lb_SCREEN_MODE_INVALID);
    lbDisplay.ScreenMode = active_mode; // still-active mode -- must NOT be what's read

    struct ConfigReloadCallbacks overridden = *config_reload_callbacks;
    overridden.get_screen_vidmode = []() -> int64_t { return pending_mode; };
    set_config_reload_callbacks(&overridden);

    CHECK(opt->get_enum() == ingame_res_encode(800, 600));
}

TEST_CASE_METHOD(ResetConfigReloadCallbacks, "INGAME_RES's set_enum registers the chosen resolution and applies it via set_screen_vidmode", "[kfx_config][config_settingschema]") {
    const struct SettingOption *opt = find_option("INGAME_RES");
    REQUIRE(opt != nullptr);

    static TbScreenMode applied_mode = Lb_SCREEN_MODE_INVALID;
    applied_mode = Lb_SCREEN_MODE_INVALID;
    struct ConfigReloadCallbacks overridden = *config_reload_callbacks;
    overridden.set_screen_vidmode = [](int64_t nmode) { applied_mode = nmode; };
    set_config_reload_callbacks(&overridden);

    ensure_screen_mode_registry_nonempty();
    opt->set_enum(ingame_res_encode(1024, 768));

    REQUIRE(applied_mode != Lb_SCREEN_MODE_INVALID);
    TbScreenModeInfo *info = LbScreenGetModeInfo(applied_mode);
    CHECK(info->Width == 1024);
    CHECK(info->Height == 768);
    CHECK(info->BitsPerPixel == 32);
}

TEST_CASE("setting_option_apply_enum_index writes INGAME_RES's WxHx32 name, not a raw number", "[kfx_config][config_settingschema]") {
    // apply_enum_index (config_settingschema.c) persists enum_table[index]'s
    // own .name string verbatim -- for INGAME_RES that string already *is*
    // the exact "WxHx32" token INGAME_RES's own config-key parser
    // (config_keeperfx.c case 7) expects, so no format_cfg_value override
    // is needed here the way STARTUP/FRAMES_PER_SECOND needed one.
    const struct SettingOption *opt = find_option("INGAME_RES");
    REQUIRE(opt != nullptr);
    opt->ensure_enum_table();
    int64_t count = setting_option_enum_count(opt);
    if (count == 0) {
        // No fullscreen display modes detected in this environment (a
        // headless test runner, most likely) -- nothing to check.
        return;
    }
    const char *name = setting_option_enum_item_name(opt, 0);
    int64_t w = 0, h = 0, bpp = 0;
    CHECK(std::sscanf(name, "%" SCNd64 "x%" SCNd64 "x%" SCNd64, &w, &h, &bpp) == 3);
    CHECK(bpp == 32);
}

TEST_CASE_METHOD(ResetSchemaState, "UI_FONT_SCALE is a curated enum of standard sizes, round-tripping through keeperfx_ui_config.ui_font_scale_pct", "[kfx_config][config_settingschema]") {
    // Switched from a free 50-200 slider to a curated enum -- a continuous
    // range let the player land on sizes that didn't rasterize particularly
    // crisply (FeStylePushFont's own comment).
    const struct SettingOption *opt = find_option("UI_FONT_SCALE");
    REQUIRE(opt != nullptr);
    CHECK(opt->type == SOptT_Enum);
    CHECK(opt->category == SCat_GUI);
    CHECK(opt->apply_class == SApply_Live); // FeStylePushFont reads it fresh every call
    REQUIRE(setting_option_enum_count(opt) == 6);

    keeperfx_ui_config.ui_font_scale_pct = 100;
    CHECK(opt->get_enum() == 100);
    CHECK(setting_option_enum_current_index(opt) == 2); // 50, 75, [100], 125, 150, 200

    setting_option_apply_enum_index(opt, 4); // 150
    CHECK(keeperfx_ui_config.ui_font_scale_pct == 150);
    CHECK(opt->get_enum() == 150);

    // The persisted name is a bare number, not a fancier label -- this
    // key's own parser (config_keeperfx.c case 52) still reads it with
    // plain atoi(), unchanged from when this row was a slider.
    CHECK(std::strcmp(setting_option_enum_item_name(opt, 4), "150") == 0);
}

TEST_CASE("Reset Campaign Progress's row shape: SOptT_Action, Game category, no cfg_key", "[kfx_config][config_settingschema]") {
    const struct SettingOption *opt = find_option_by_label(GUIStr_SetResetCampaignProgress);
    REQUIRE(opt != nullptr);
    CHECK(opt->type == SOptT_Action);
    CHECK(opt->category == SCat_Game);
    CHECK(opt->cfg_key == nullptr);
    CHECK(opt->help_stridx == GUIStr_HelpResetCampaignProgress);
    REQUIRE(opt->on_action != nullptr);
}

TEST_CASE_METHOD(ResetConfigReloadCallbacks, "Reset Campaign Progress's on_action calls config_reload_callbacks->reset_campaign_progress", "[kfx_config][config_settingschema]") {
    const struct SettingOption *opt = find_option_by_label(GUIStr_SetResetCampaignProgress);
    REQUIRE(opt != nullptr);

    struct ConfigReloadCallbacks fake = *config_reload_callbacks;
    static bool called = false;
    called = false;
    fake.reset_campaign_progress = []() { called = true; };
    set_config_reload_callbacks(&fake);

    opt->on_action();
    CHECK(called);
}

TEST_CASE("SHADOWS/VIEW_DISTANCE's row shape: SOptT_Int, Graphics, no cfg_key, persist_via_save_settings", "[kfx_config][config_settingschema]") {
    const struct SettingOption *shadows = find_option_by_label(GUIStr_SetVideoShadows);
    REQUIRE(shadows != nullptr);
    CHECK(shadows->type == SOptT_Int);
    CHECK(shadows->category == SCat_Graphics);
    CHECK(shadows->cfg_key == nullptr);
    CHECK(shadows->persist_via_save_settings);
    CHECK(shadows->help_stridx == GUIStr_HelpVideoShadows);
    CHECK(shadows->int_min == 0);
    CHECK(shadows->int_max == 3);
    REQUIRE(shadows->get_int != nullptr);
    REQUIRE(shadows->set_int != nullptr);

    const struct SettingOption *view_distance = find_option_by_label(GUIStr_SetViewDistance);
    REQUIRE(view_distance != nullptr);
    CHECK(view_distance->type == SOptT_Int);
    CHECK(view_distance->category == SCat_Graphics);
    CHECK(view_distance->cfg_key == nullptr);
    CHECK(view_distance->persist_via_save_settings);
    CHECK(view_distance->help_stridx == GUIStr_HelpViewDistance);
    CHECK(view_distance->int_min == 0);
    CHECK(view_distance->int_max == 3);
    REQUIRE(view_distance->get_int != nullptr);
    REQUIRE(view_distance->set_int != nullptr);
}

// get_int/set_int round-trip against struct GameSettings directly, saving
// and restoring the real fields around the test -- set_int() alone (as
// opposed to going through setting_option_apply_int()) never touches
// save_settings()'s own real file I/O against save/settings.toml, same
// class of gap config_settings_test.cpp already documents for
// load_settings()/save_settings() themselves, so this is the safe half to
// test directly.
TEST_CASE("SHADOWS/VIEW_DISTANCE's get_int/set_int read/write struct GameSettings directly", "[kfx_config][config_settingschema]") {
    unsigned char saved_shadows = settings.video_shadows;
    unsigned char saved_view_distance = settings.view_distance;

    const struct SettingOption *shadows = find_option_by_label(GUIStr_SetVideoShadows);
    const struct SettingOption *view_distance = find_option_by_label(GUIStr_SetViewDistance);
    REQUIRE(shadows != nullptr);
    REQUIRE(view_distance != nullptr);

    settings.video_shadows = 2;
    CHECK(shadows->get_int() == 2);
    shadows->set_int(3);
    CHECK(settings.video_shadows == 3);

    settings.view_distance = 1;
    CHECK(view_distance->get_int() == 1);
    view_distance->set_int(0);
    CHECK(settings.view_distance == 0);

    settings.video_shadows = saved_shadows;
    settings.view_distance = saved_view_distance;
}
