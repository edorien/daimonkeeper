// kfx_config: config_keeperfx.c -- the bit-check feature-flag family
// (censorship_enabled/atmos_sounds_enabled/resize_movies_enabled/
// freeze_game_on_focus_lost/unlock_cursor_when_game_paused/
// lock_cursor_in_possession/use_relative_mouse_mode/
// pause_music_when_game_paused/mute_audio_on_focus_lost/is_feature_on/
// get_skip_heart_zoom_feature+set_skip_heart_zoom_feature), each a thin
// `features_enabled & Ft_X` check against the plain top-level extern
// `features_enabled` global (not part of kfx_config_state -- confirmed
// by grep), plus get_language_lwrstr/is_dbc_language/
// parse_draw_fps_config_val/process_cmdline_overrides/prepare_diskpath.
//
// load_configuration()/load_configuration_for_mod_all() are not
// attempted here: same class of gap as config_settings_test.cpp's
// load_settings()/highscores_test.cpp's load_high_score_table() -- they
// unconditionally reset install_info.inst_path/keeper_runtime_directory
// and read real files via prepare_file_path(FGrp_Main, ...) with no
// caller-supplied path, so there's no way to point them at a test
// fixture without a real file-I/O side effect against the actual game
// install directory.
//
// config_network_is_active()/set_config_network_is_active_check() (also
// declared in this file's header) already have direct coverage in
// config_mods_test.cpp -- not re-tested here, only used as a dependency
// of freeze_game_on_focus_lost()'s network-active early-return.
//
// prepare_diskpath() had real external linkage but no header declaration
// at all; added (same situation as config_settings.c's
// setup_default_settings()).
// keeperfx_cfg_write_values_to_file() (docs/refactor/renderer/04-imgui-gui-
// foundation.md §6.2 finding 1, Phase G's cfg-writer prerequisite) is
// covered separately below, via the explicit-path variant rather than
// keeperfx_cfg_write_values() -- same reason load_configuration() isn't
// attempted above, plus bflib_fileio_test.cpp's own documented
// LbFileOpen(..., Lb_FILE_MODE_NEW) gotcha for absolute paths: a bare
// relative filename in the test binary's CWD, same ScratchFile pattern.
#include <catch2/catch_test_macros.hpp>

#include "config_keeperfx.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace {
struct ResetFeaturesEnabled {
    ResetFeaturesEnabled() { features_enabled = 0; }
    ~ResetFeaturesEnabled() { features_enabled = 0; set_config_network_is_active_check(nullptr); }
};
}

TEST_CASE_METHOD(ResetFeaturesEnabled, "is_feature_on reflects whichever bit is set in features_enabled, independent of other bits", "[kfx_config][config_keeperfx]") {
    CHECK_FALSE(is_feature_on(Ft_Censorship));
    features_enabled = Ft_Censorship | Ft_HiResVideo;
    CHECK(is_feature_on(Ft_Censorship));
    CHECK(is_feature_on(Ft_HiResVideo));
    CHECK_FALSE(is_feature_on(Ft_BigPointer));
}

TEST_CASE_METHOD(ResetFeaturesEnabled, "censorship_enabled/atmos_sounds_enabled/resize_movies_enabled each check their own Ft_* bit", "[kfx_config][config_keeperfx]") {
    CHECK_FALSE(censorship_enabled());
    CHECK_FALSE(atmos_sounds_enabled());
    CHECK_FALSE(resize_movies_enabled());

    features_enabled = Ft_Censorship;
    CHECK(censorship_enabled());
    CHECK_FALSE(atmos_sounds_enabled());

    features_enabled = Ft_Atmossounds;
    CHECK(atmos_sounds_enabled());
    CHECK_FALSE(censorship_enabled());

    features_enabled = Ft_Resizemovies;
    CHECK(resize_movies_enabled());
}

TEST_CASE_METHOD(ResetFeaturesEnabled, "unlock_cursor_when_game_paused/lock_cursor_in_possession/use_relative_mouse_mode/pause_music_when_game_paused/mute_audio_on_focus_lost each check their own Ft_* bit", "[kfx_config][config_keeperfx]") {
    features_enabled = Ft_UnlockCursorOnPause;
    CHECK(unlock_cursor_when_game_paused());
    CHECK_FALSE(lock_cursor_in_possession());

    features_enabled = Ft_LockCursorInPossession;
    CHECK(lock_cursor_in_possession());

    features_enabled = Ft_RelativeMouseMode;
    CHECK(use_relative_mouse_mode());

    features_enabled = Ft_PauseMusicOnGamePause;
    CHECK(pause_music_when_game_paused());

    features_enabled = Ft_MuteAudioOnLoseFocus;
    CHECK(mute_audio_on_focus_lost());
}

TEST_CASE_METHOD(ResetFeaturesEnabled, "set_skip_heart_zoom_feature toggles the Ft_SkipHeartZoom bit; get_skip_heart_zoom_feature reads it back", "[kfx_config][config_keeperfx]") {
    CHECK_FALSE(get_skip_heart_zoom_feature());

    set_skip_heart_zoom_feature(true);
    CHECK(get_skip_heart_zoom_feature());
    CHECK(is_feature_on(Ft_SkipHeartZoom));

    set_skip_heart_zoom_feature(false);
    CHECK_FALSE(get_skip_heart_zoom_feature());
}

TEST_CASE_METHOD(ResetFeaturesEnabled, "freeze_game_on_focus_lost checks Ft_FreezeOnLoseFocus but always returns false while a network-active check reports true", "[kfx_config][config_keeperfx]") {
    features_enabled = Ft_FreezeOnLoseFocus;
    CHECK(freeze_game_on_focus_lost());

    // A real behavior, not incidental: multiplayer sessions must never
    // silently freeze on focus loss, so this overrides the feature bit
    // regardless of its value.
    set_config_network_is_active_check([]() -> TbBool { return true; });
    CHECK_FALSE(freeze_game_on_focus_lost());

    set_config_network_is_active_check(nullptr);
    CHECK(freeze_game_on_focus_lost());
}

TEST_CASE("get_language_lwrstr returns a lowercased 3-letter code for a known language id", "[kfx_config][config_keeperfx]") {
    // lang_type[0] is Lang_Default's entry; use whatever index 0 resolves
    // to rather than assuming a specific string, since only the
    // lowercasing behavior is this function's own responsibility (the
    // text itself comes from lang_type[], not owned by this function).
    const char *result = get_language_lwrstr(0);
    REQUIRE(result != nullptr);
    for (const char *p = result; *p != '\0'; p++) {
        CHECK(*p == (char)std::tolower((unsigned char)*p));
    }
}

TEST_CASE("is_dbc_language is true only for the four double-byte-character languages", "[kfx_config][config_keeperfx]") {
    CHECK(is_dbc_language(Lang_Japanese));
    CHECK(is_dbc_language(Lang_ChineseInt));
    CHECK(is_dbc_language(Lang_ChineseTra));
    CHECK(is_dbc_language(Lang_Korean));
    CHECK_FALSE(is_dbc_language(Lang_English));
}

TEST_CASE("parse_draw_fps_config_val parses two non-negative numbers, writing only as many outputs as tokens found", "[kfx_config][config_keeperfx]") {
    int64_t main_fps = -99, secondary_fps = -99;

    CHECK(parse_draw_fps_config_val("30 60", &main_fps, &secondary_fps) == 2);
    CHECK(main_fps == 30);
    CHECK(secondary_fps == 60);

    main_fps = -99; secondary_fps = -99;
    CHECK(parse_draw_fps_config_val("30", &main_fps, &secondary_fps) == 1);
    CHECK(main_fps == 30);
    CHECK(secondary_fps == -99); // untouched -- only 1 token was found
}

TEST_CASE("parse_draw_fps_config_val treats \"auto\" as -1 for the first (main) value only", "[kfx_config][config_keeperfx]") {
    int64_t main_fps = -99, secondary_fps = -99;

    CHECK(parse_draw_fps_config_val("auto 60", &main_fps, &secondary_fps) == 2);
    CHECK(main_fps == -1);
    CHECK(secondary_fps == 60);
}

TEST_CASE("parse_draw_fps_config_val stops and returns 0 on a negative first value, writing nothing", "[kfx_config][config_keeperfx]") {
    int64_t main_fps = -99, secondary_fps = -99;

    CHECK(parse_draw_fps_config_val("-5 60", &main_fps, &secondary_fps) == 0);
    CHECK(main_fps == -99);
    CHECK(secondary_fps == -99);
}

TEST_CASE("parse_draw_fps_config_val stops after a negative second value, keeping the first", "[kfx_config][config_keeperfx]") {
    int64_t main_fps = -99, secondary_fps = -99;

    CHECK(parse_draw_fps_config_val("30 -5", &main_fps, &secondary_fps) == 1);
    CHECK(main_fps == 30);
    CHECK(secondary_fps == -99);
}

TEST_CASE("parse_atmos_volume_config_val takes KeeperFX's LOW/MEDIUM/HIGH presets or a number 0-255", "[kfx_config][config_keeperfx]") {
    CHECK(parse_atmos_volume_config_val("LOW") == 64);
    CHECK(parse_atmos_volume_config_val("medium") == 128);
    CHECK(parse_atmos_volume_config_val("HIGH") == 255);
    CHECK(parse_atmos_volume_config_val("0") == 0);
    CHECK(parse_atmos_volume_config_val("100") == 100);
    CHECK(parse_atmos_volume_config_val("255") == 255);
}

TEST_CASE("parse_atmos_volume_config_val rejects anything else with -1", "[kfx_config][config_keeperfx]") {
    CHECK(parse_atmos_volume_config_val("256") == -1);
    CHECK(parse_atmos_volume_config_val("-1") == -1);
    CHECK(parse_atmos_volume_config_val("LOUD") == -1);
    CHECK(parse_atmos_volume_config_val("12x") == -1);
    CHECK(parse_atmos_volume_config_val("") == -1);
}

TEST_CASE("parse_draw_fps_config_val returns 0 for an empty string", "[kfx_config][config_keeperfx]") {
    int64_t main_fps = -99, secondary_fps = -99;
    CHECK(parse_draw_fps_config_val("", &main_fps, &secondary_fps) == 0);
}

namespace {
struct ResetStartParams {
    StartupParameters saved;
    ResetStartParams() : saved(start_params) { std::memset(&start_params, 0, sizeof(start_params)); }
    ~ResetStartParams() { start_params = saved; }
};
}

TEST_CASE_METHOD(ResetStartParams, "process_cmdline_overrides clears Ft_NoCdMusic only when the Clo_CDMusic override is set", "[kfx_config][config_keeperfx]") {
    features_enabled = Ft_NoCdMusic;
    process_cmdline_overrides();
    CHECK(is_feature_on(Ft_NoCdMusic)); // no override set -- untouched

    start_params.overrides[Clo_CDMusic] = true;
    process_cmdline_overrides();
    CHECK_FALSE(is_feature_on(Ft_NoCdMusic));

    features_enabled = 0;
}

TEST_CASE("prepare_diskpath strips trailing path separators, whitespace, and a trailing \"/.\" current-directory component", "[kfx_config][config_keeperfx]") {
    char buf[64];

    std::strcpy(buf, "data/keeperfx/");
    CHECK(prepare_diskpath(buf, sizeof(buf)));
    CHECK(std::strcmp(buf, "data/keeperfx") == 0);

    std::strcpy(buf, "data/keeperfx/./");
    CHECK(prepare_diskpath(buf, sizeof(buf)));
    CHECK(std::strcmp(buf, "data/keeperfx") == 0);

    std::strcpy(buf, "data\\keeperfx\\");
    CHECK(prepare_diskpath(buf, sizeof(buf)));
    CHECK(std::strcmp(buf, "data\\keeperfx") == 0);
}

TEST_CASE("prepare_diskpath returns false for an empty string", "[kfx_config][config_keeperfx]") {
    char buf[64] = "";
    CHECK_FALSE(prepare_diskpath(buf, sizeof(buf)));
}

namespace {
// One file per process: ctest runs each test case as its own process, in
// parallel, so a shared name lets one case delete another's file.
const std::string kCfgWriterTestFileStr = "kfx_config_utest_keeperfx_cfg_writer_" + std::to_string(getpid()) + ".cfg";
const char *const kCfgWriterTestFile = kCfgWriterTestFileStr.c_str();

struct ScratchCfgFile {
    ScratchCfgFile() { std::remove(kCfgWriterTestFile); }
    ~ScratchCfgFile() { std::remove(kCfgWriterTestFile); }

    void write(const char *content)
    {
        std::ofstream f(kCfgWriterTestFile, std::ios::binary | std::ios::trunc);
        f << content;
    }
    std::string read()
    {
        std::ifstream f(kCfgWriterTestFile, std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }
};
}

TEST_CASE_METHOD(ScratchCfgFile, "keeperfx_cfg_write_values_to_file replaces one key's value, leaving comments/order/other keys untouched", "[kfx_config][config_keeperfx]") {
    write(
        "; a leading comment\n"
        "LANGUAGE=ENG\n"
        "DISPLAY_NUMBER=1\n"
        "; a trailing comment\n"
    );

    struct KeeperfxCfgEdit edits[] = { { "DISPLAY_NUMBER", "2" } };
    CHECK(keeperfx_cfg_write_values_to_file(kCfgWriterTestFile, edits, 1));

    CHECK(read() ==
        "; a leading comment\n"
        "LANGUAGE=ENG\n"
        "DISPLAY_NUMBER=2\n"
        "; a trailing comment\n");
}

TEST_CASE_METHOD(ScratchCfgFile, "keeperfx_cfg_write_values_to_file applies several edits in one pass", "[kfx_config][config_keeperfx]") {
    write(
        "LANGUAGE=ENG\n"
        "DISPLAY_NUMBER=1\n"
        "INGAME_RES=DESKTOP\n"
    );

    struct KeeperfxCfgEdit edits[] = {
        { "DISPLAY_NUMBER", "2" },
        { "INGAME_RES", "1920x1080x32" },
    };
    CHECK(keeperfx_cfg_write_values_to_file(kCfgWriterTestFile, edits, 2));

    CHECK(read() ==
        "LANGUAGE=ENG\n"
        "DISPLAY_NUMBER=2\n"
        "INGAME_RES=1920x1080x32\n");
}

TEST_CASE_METHOD(ScratchCfgFile, "keeperfx_cfg_write_values_to_file appends a key that has no existing line", "[kfx_config][config_keeperfx]") {
    write("LANGUAGE=ENG\n");

    struct KeeperfxCfgEdit edits[] = { { "DISPLAY_NUMBER", "3" } };
    CHECK(keeperfx_cfg_write_values_to_file(kCfgWriterTestFile, edits, 1));

    CHECK(read() == "LANGUAGE=ENG\nDISPLAY_NUMBER=3\n");
}

TEST_CASE_METHOD(ScratchCfgFile, "keeperfx_cfg_write_values_to_file updates every occurrence of a duplicated key", "[kfx_config][config_keeperfx]") {
    write(
        "DISPLAY_NUMBER=1\n"
        "LANGUAGE=ENG\n"
        "DISPLAY_NUMBER=1\n"
    );

    struct KeeperfxCfgEdit edits[] = { { "DISPLAY_NUMBER", "2" } };
    CHECK(keeperfx_cfg_write_values_to_file(kCfgWriterTestFile, edits, 1));

    CHECK(read() ==
        "DISPLAY_NUMBER=2\n"
        "LANGUAGE=ENG\n"
        "DISPLAY_NUMBER=2\n");
}

TEST_CASE_METHOD(ScratchCfgFile, "keeperfx_cfg_write_values_to_file does not false-match a key that is a prefix of a longer one", "[kfx_config][config_keeperfx]") {
    write("INGAME_RES=DESKTOP\n");

    struct KeeperfxCfgEdit edits[] = { { "INGAME_RE", "bogus" } };
    CHECK(keeperfx_cfg_write_values_to_file(kCfgWriterTestFile, edits, 1));

    // INGAME_RE has no real line of its own -- it must be appended, not
    // matched against (and corrupt) INGAME_RES's line.
    CHECK(read() == "INGAME_RES=DESKTOP\nINGAME_RE=bogus\n");
}

TEST_CASE_METHOD(ScratchCfgFile, "keeperfx_cfg_write_values_to_file creates a new file when none exists yet", "[kfx_config][config_keeperfx]") {
    struct KeeperfxCfgEdit edits[] = { { "LANGUAGE", "ENG" } };
    CHECK(keeperfx_cfg_write_values_to_file(kCfgWriterTestFile, edits, 1));

    CHECK(read() == "LANGUAGE=ENG\n");
}

TEST_CASE_METHOD(ScratchCfgFile, "keeperfx_cfg_write_values_to_file rejects a null/empty filename or a non-positive edit count", "[kfx_config][config_keeperfx]") {
    struct KeeperfxCfgEdit edits[] = { { "LANGUAGE", "ENG" } };
    CHECK_FALSE(keeperfx_cfg_write_values_to_file(nullptr, edits, 1));
    CHECK_FALSE(keeperfx_cfg_write_values_to_file("", edits, 1));
    CHECK_FALSE(keeperfx_cfg_write_values_to_file(kCfgWriterTestFile, edits, 0));
}

TEST_CASE("keeperfx_cfg_write_values fails before load_configuration() has ever run", "[kfx_config][config_keeperfx]") {
    // No fixture reset available for the remembered path (it's set once by
    // load_configuration(), never cleared) -- this only holds true so long
    // as no other test in this binary calls load_configuration() first.
    struct KeeperfxCfgEdit edits[] = { { "LANGUAGE", "ENG" } };
    CHECK_FALSE(keeperfx_cfg_write_values(edits, 1));
}
