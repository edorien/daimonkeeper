// kfx_config: sprite_lookup.c -- same callback-registration shape as
// net_callbacks_test.cpp/game_callbacks_test.cpp.
#include <catch2/catch_test_macros.hpp>

#include "sprite_lookup.h"

TEST_CASE("the default sprite_lookup table's every stub is a safe no-op returning its documented default", "[kfx_config][sprite_lookup]") {
    REQUIRE(sprite_lookup != nullptr);
    CHECK(sprite_lookup->get_icon_id("foo") == 0);
    CHECK(sprite_lookup->get_anim_id("foo", nullptr) == 0);
    CHECK(sprite_lookup->get_anim_id_("foo") == 0);
    CHECK(sprite_lookup->get_button_sprite(0) == nullptr);
    CHECK(sprite_lookup->get_panel_sprite(0) == nullptr);
    CHECK(sprite_lookup->get_ensign_id("foo") == -1);
    sprite_lookup->init_custom_campaign_sprites("dir", "desc");
}

TEST_CASE("set_sprite_lookup_callbacks installs a custom table and falls back to the default once cleared", "[kfx_config][sprite_lookup]") {
    struct SpriteLookupCallbacks fake = *sprite_lookup;
    static bool called = false;
    called = false;
    fake.get_ensign_id = [](const char *) -> int64_t { called = true; return 42; };

    set_sprite_lookup_callbacks(&fake);
    CHECK(sprite_lookup->get_ensign_id("x") == 42);
    CHECK(called);

    set_sprite_lookup_callbacks(nullptr);
    called = false;
    CHECK(sprite_lookup->get_ensign_id("x") == -1);
    CHECK_FALSE(called);
}
