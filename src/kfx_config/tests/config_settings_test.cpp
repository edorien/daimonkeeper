// kfx_config: config_settings.c -- setup_default_settings() and
// get_max_i_can_see_from_settings() are pure; load_settings()/
// save_settings() are not attempted here -- they read/write a
// hardcoded real path (prepare_file_path(FGrp_Save, "settings.toml")),
// not a caller-supplied fname the way every other loader in this
// library takes, so there's no way to point them at a test fixture
// without either a real file-I/O side effect against the actual save
// directory or a change to the function's own signature.
// setup_default_settings() had real external linkage but no header
// declaration at all; added.
#include <catch2/catch_test_macros.hpp>

#include "config_settings.h"

#include <cstring>

TEST_CASE("setup_default_settings resets every field to its documented default", "[kfx_config][config_settings]") {
    std::memset(&settings, 0xAA, sizeof(settings)); // poison first

    setup_default_settings();
    CHECK(settings.video_detail_level == 0);
    CHECK(settings.video_shadows == 4);
    CHECK(settings.view_distance == 3);
    CHECK(settings.sound_volume == 127);
    CHECK(settings.music_volume == 90);
    CHECK(settings.tooltips_on == true);
    CHECK(settings.minimap_zoom == 256);
    CHECK(settings.highlight_mode == false);

    // Every key binding is reset from game_key_settings[]'s own defaults.
    for (int64_t i = 0; i < GAME_KEYS_COUNT; i++) {
        CHECK(settings.kbkeys[i].code == game_key_settings[i].default_code);
        CHECK(settings.kbkeys[i].mods == game_key_settings[i].default_mods);
        CHECK(settings.kbkeys[i].controller_buttons == game_key_settings[i].default_controller_buttons);
    }
}

TEST_CASE("get_max_i_can_see_from_settings indexes the visibility table by view_distance, wrapping at 4", "[kfx_config][config_settings]") {
    setup_default_settings(); // view_distance = 3
    int64_t at_3 = get_max_i_can_see_from_settings();

    settings.view_distance = 7; // 7 % 4 == 3, same slot as above
    CHECK(get_max_i_can_see_from_settings() == at_3);

    settings.view_distance = 0;
    int64_t at_0 = get_max_i_can_see_from_settings();
    CHECK(at_0 != at_3); // a real, distinct table entry, not a degenerate constant
}
