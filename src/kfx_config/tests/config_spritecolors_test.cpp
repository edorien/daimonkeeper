// kfx_config: config_spritecolors.c's load_spritecolors_config_file()
// maps five TOML arrays-of-rows into five static short[] lookup tables
// (gui_panel_sprites_eq/pointer_sprites_eq/button_sprite_eq/
// animationIds_eq/objects_eq), each row shaped [base_icon_idx, color0,
// ..., color9] (PLAYER_COLORS_COUNT == 11: index 0 is the match key,
// indices 1..10 are get_player_color_idx()+1). Unlike kfx_config_state,
// these tables have no public reset function -- but load_array() itself
// memsets its target array whenever CnfLd_AcceptPartial isn't set (the
// flags==0 calls below), so calling the real loader with flags 0 before
// each assertion is the reset, same net effect as the ResetXxx pattern
// used elsewhere in this library.
//
// get_player_colored_button_sprite_idx()'s PLAYER_NEUTRAL branch divides
// by kfx_config_state.neutral_flash_rate -- left at its post-load_func
// value (unrelated to this loader), so it must be set to a nonzero
// value before exercising that branch or the divide is by zero.
#include <catch2/catch_test_macros.hpp>

#include "kfx_config_test_paths.h" // KFX_CONFIG_TEST_FIXTURES_DIR
#include "config_spritecolors.h"
#include "config_keeperfx.h" // for PlayerNumber -> pulled in via config.h too
#include "kfx_config_state.h"
#include "ports/sim_port.h"
#include "kfx_config/tests/scoped_port_override.h"

#include <cstring>

TEST_CASE("load_spritecolors_config_file returns false for a missing file", "[kfx_config][config_spritecolors]") {
    CHECK_FALSE(keeper_spritecolors_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/does_not_exist.toml", CnfLd_IgnoreErrors));
}

TEST_CASE("keeper_spritecolors_file_data has no pre/post-load hooks", "[kfx_config][config_spritecolors]") {
    CHECK(keeper_spritecolors_file_data.pre_load_func == nullptr);
    CHECK(keeper_spritecolors_file_data.post_load_func == nullptr);
    CHECK(std::strcmp(keeper_spritecolors_file_data.filename, "spritecolors.toml") == 0);
}

TEST_CASE("get_player_colored_icon_idx/pointer/object_model fall back to base_icon_idx for an icon absent from every configured row", "[kfx_config][config_spritecolors]") {
    REQUIRE(keeper_spritecolors_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/spritecolors_minimal.toml", 0));

    CHECK(get_player_colored_icon_idx(12345, 0) == 12345);
    CHECK(get_player_colored_pointer_icon_idx(12345, 0) == 12345);
    CHECK(get_player_colored_object_model(12345, 0) == 12345);
    CHECK(get_coloured_object_base_model(12345) == 0);
}

TEST_CASE("get_player_colored_icon_idx/pointer/object_model look up the row matching base_icon_idx and index by get_player_color_idx()+1", "[kfx_config][config_spritecolors]") {
    REQUIRE(keeper_spritecolors_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/spritecolors_minimal.toml", 0));

    // SimPort's unwired get_player_color_idx default
    // returns 0 for every player -> color_idx = 0 + 1 = 1 -> the second
    // column of each configured row.
    CHECK(get_player_colored_icon_idx(867, 0) == 900);
    CHECK(get_player_colored_pointer_icon_idx(10, 0) == 20);
    CHECK(get_player_colored_object_model(100, 0) == 150);
}

TEST_CASE("get_player_colored_icon_idx indexes by a non-default player color", "[kfx_config][config_spritecolors]") {
    REQUIRE(keeper_spritecolors_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/spritecolors_minimal.toml", 0));

    ScopedPortOverride<SimPort> port(sim_port, set_sim_port);
    port->get_player_color_idx = [](PlayerNumber) -> unsigned char { return 3; };

    // color_idx = 3 + 1 = 4 -> the fifth column of the gui_panel_sprites row.
    CHECK(get_player_colored_icon_idx(867, 0) == 903);
}

TEST_CASE("get_player_colored_icon_idx falls back to base_icon_idx once the color index reaches PLAYER_COLORS_COUNT", "[kfx_config][config_spritecolors]") {
    REQUIRE(keeper_spritecolors_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/spritecolors_minimal.toml", 0));

    ScopedPortOverride<SimPort> port(sim_port, set_sim_port);
    port->get_player_color_idx = [](PlayerNumber) -> unsigned char { return 250; };

    CHECK(get_player_colored_icon_idx(867, 0) == 867);
}

TEST_CASE("get_player_colored_button_sprite_idx uses get_player_color_idx for non-neutral players", "[kfx_config][config_spritecolors]") {
    REQUIRE(keeper_spritecolors_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/spritecolors_minimal.toml", 0));

    ScopedPortOverride<SimPort> port(sim_port, set_sim_port);
    port->get_player_color_idx = [](PlayerNumber) -> unsigned char { return 2; };

    // color_idx = 2 + 1 = 3 -> the fourth column of the button_sprite row.
    CHECK(get_player_colored_button_sprite_idx(5, 0) == 52);
}

TEST_CASE("get_player_colored_button_sprite_idx cycles PLAYER_NEUTRAL through neutral_flash_rate-sized bands of get_gameturn()", "[kfx_config][config_spritecolors]") {
    REQUIRE(keeper_spritecolors_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/spritecolors_minimal.toml", 0));
    kfx_config_state.neutral_flash_rate = 1; // get_gameturn() defaults to 0 -> color_idx = (0 % 4) / 1 = 0 -> color_idx+1 = 1

    CHECK(get_player_colored_button_sprite_idx(5, PLAYER_NEUTRAL) == 50);
}

TEST_CASE("get_coloured_object_base_model maps any configured column back to its row's base model, base included", "[kfx_config][config_spritecolors]") {
    REQUIRE(keeper_spritecolors_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/spritecolors_minimal.toml", 0));

    CHECK(get_coloured_object_base_model(100) == 100); // the base column itself
    CHECK(get_coloured_object_base_model(155) == 100); // a color column
    CHECK(get_coloured_object_base_model(999) == 0);   // not in any configured row
}
