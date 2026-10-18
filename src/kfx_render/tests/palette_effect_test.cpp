// PaletteEffect (lens palette): a lens palette becomes the main palette that possession/pain
// fades work from. With a fade running it must NOT switch at once (that would reset the fade,
// upstream #5302) -- only local_state.main_palette is updated and the fade brings it in.
#include <catch2/catch_test_macros.hpp>

#include "PaletteEffect.h"
#include "config_lenses.h"
#include "player_data.h"
#include "packet_data.h"
#include "vidmode.h"
#include "vidfade.h"
#include "kfx_sim_state.h"
#include "local_state.h"

#include <cstring>

TEST_CASE("PaletteEffect keeps a running possession fade going when the lens palette is applied", "[kfx_render][palette_effect]") {
    std::memset(&local_state, 0, sizeof(local_state));
    std::memset(&lenses_conf, 0, sizeof(lenses_conf));
    local_state.palette_fade_step_possession = 5;

    PaletteEffect effect;
    CHECK(effect.Setup(1));
    CHECK(local_state.lens_palette == lenses_conf.lenses[1].palette);
    CHECK(local_state.main_palette == lenses_conf.lenses[1].palette);
    CHECK(local_state.palette_fade_step_possession == 5); // fade untouched

    effect.Cleanup();
    CHECK(local_state.lens_palette == nullptr);
    CHECK(local_state.main_palette == engine_palette);
    CHECK(local_state.palette_fade_step_possession == 5);
}

// Refactor pass 4, S08: kfx_sim names the palette it wants (enum ViewPalette, through RenderPort) instead of passing
// the palette, which made the palettes kfx_sim globals; render resolves the name. White is all 0x3F, as the sim's own
// white buffers were (temp_pal, zoom_to_heart_palette).
TEST_CASE("view_palette gives the palette each ViewPalette names", "[kfx_render][palette_effect]") {
    unsigned char game[PALETTE_SIZE] = {1}, freeze[PALETTE_SIZE] = {2}, lightning[PALETTE_SIZE] = {3};
    unsigned char *const saved[3] = {engine_palette, blue_palette, lightning_palette};
    engine_palette = game;
    blue_palette = freeze;
    lightning_palette = lightning;
    CHECK(view_palette(VPal_Engine) == game);
    CHECK(view_palette(VPal_Freeze) == freeze);
    CHECK(view_palette(VPal_Lightning) == lightning);
    const unsigned char *white = view_palette(VPal_White);
    REQUIRE(white != nullptr);
    for (int i = 0; i < PALETTE_SIZE; i++)
        REQUIRE(white[i] == 0x3F);
    engine_palette = saved[0];
    blue_palette = saved[1];
    lightning_palette = saved[2];
}

// An External seat moves the local player to the seat's user (net_add_external_seat()); the palette restores that
// name the player's user (lightning flash, possession leave, a load, the level start) must still reach the screen.
TEST_CASE("PaletteSetUserPalette reaches the screen for the local player's seat user", "[kfx_render][palette_effect]") {
    unsigned char game[PALETTE_SIZE] = {1}, other[PALETTE_SIZE] = {2};
    unsigned char *const saved_engine = engine_palette;
    const unsigned char saved_me = my_player_number;
    engine_palette = game;
    std::memset(&local_state, 0, sizeof(local_state));
    local_state.main_palette = other;
    my_player_number = 0;
    struct PlayerInfo *me = get_player(0);
    const unsigned char saved_alloc = me->allocflags;
    const NetUserId saved_user = me->user_id;
    me->allocflags |= PlaF_Allocated;

    SECTION("the seat's user is the screen's") {
        me->user_id = SOLO_HUMAN_ID + 1;
        PaletteSetUserPalette(me->user_id, game);
        CHECK(local_state.main_palette == game);
    }
    SECTION("this machine's user still is") {
        me->user_id = SOLO_HUMAN_ID + 1;
        PaletteSetUserPalette(get_local_user(), game);
        CHECK(local_state.main_palette == game);
    }
    SECTION("another user's palette stays off the screen") {
        me->user_id = SOLO_HUMAN_ID + 1;
        PaletteSetUserPalette(SOLO_HUMAN_ID + 2, game);
        CHECK(local_state.main_palette == other);
    }

    me->user_id = saved_user;
    me->allocflags = saved_alloc;
    my_player_number = saved_me;
    engine_palette = saved_engine;
}
