// kfx_net: net_resync.cpp -- targeted per the user's request to focus
// coverage around scripts/check_layering.py's ACCEPTED (by-design,
// architecture.md §8.2) kfx_net -> kfx_game/kfx_frontend residual: this
// file #includes and memcpy()s game/kfx_game_state/kfx_frontend_state
// wholesale, the intentional raw-blob network resync wire format. The
// violation itself isn't going away without restructuring netcode, but
// the surrounding functions in the same file are still worth covering.
//
// store_localised_game_structure()/recall_localised_game_structure()
// (the "boing" struct) are a clean self-contained round-trip pair, no
// blob/memcpy involved -- pattern A on kfx_sim_state/kfx_net_state.
// Neither had a header declaration anywhere (only called from within
// net_resync.cpp itself) -- added both to net_resync.h, the same "add
// the missing declaration" fix used repeatedly across this plan.
//
// animate_resync_progress_bar() is the first kfx_net test to combine
// three simultaneous pattern-B fakes (GetGameTurnFunc, NetCallbacks,
// RenderOverlayCallbacks) -- confirms both its early-return guards
// (gameturn 0, GOF_Paused) and, past them, the actual progress-bar pixel
// math it hands to draw_out_of_sync_box().
#include <catch2/catch_test_macros.hpp>

#include "net_resync.h"
#include "kfx_sim_state.h"
#include "kfx_net_state.h"
#include "net_callbacks.h"
#include "render_overlay.h"
#include "bflib_video.h" // units_per_pixel
#include "globals.h" // GetGameTurnFunc/set_get_gameturn_provider

#include <cstring>

namespace {
struct ResetStates {
    ResetStates() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_net_state, 0, sizeof(kfx_net_state));
    }
};
}

TEST_CASE_METHOD(ResetStates, "store_localised_game_structure/recall_localised_game_structure round-trip every field", "[kfx_net][net_resync]") {
    kfx_sim_state.active_panel_mnu_idx = 3;
    kfx_net_state.comp_player_aggressive = 1;
    kfx_net_state.comp_player_defensive = 2;
    kfx_net_state.comp_player_construct = 3;
    kfx_net_state.comp_player_creatrsonly = 4;
    kfx_sim_state.creatures_tend_imprison = 5;
    kfx_sim_state.creatures_tend_flee = 6;
    kfx_sim_state.hand_over_subtile_x = 100;
    kfx_sim_state.hand_over_subtile_y = 200;
    kfx_sim_state.chosen_room_kind = 7;
    kfx_sim_state.chosen_room_spridx = 8;
    kfx_sim_state.chosen_room_tooltip = 9;
    kfx_sim_state.chosen_spell_type = 10;
    kfx_sim_state.chosen_spell_spridx = 11;
    kfx_sim_state.chosen_spell_tooltip = 12;
    kfx_sim_state.manufactr_element = 13;
    kfx_sim_state.manufactr_spridx = 14;
    kfx_sim_state.manufactr_tooltip = 15;

    store_localised_game_structure();

    // Overwrite everything with different values -- recall must restore
    // the stored snapshot, not just leave these alone.
    kfx_sim_state.active_panel_mnu_idx = 99;
    kfx_net_state.comp_player_aggressive = 99;
    kfx_net_state.comp_player_defensive = 99;
    kfx_net_state.comp_player_construct = 99;
    kfx_net_state.comp_player_creatrsonly = 99;
    kfx_sim_state.creatures_tend_imprison = 99;
    kfx_sim_state.creatures_tend_flee = 99;
    kfx_sim_state.hand_over_subtile_x = 999;
    kfx_sim_state.hand_over_subtile_y = 999;
    kfx_sim_state.chosen_room_kind = 99;
    kfx_sim_state.chosen_room_spridx = 99;
    kfx_sim_state.chosen_room_tooltip = 99;
    kfx_sim_state.chosen_spell_type = 99;
    kfx_sim_state.chosen_spell_spridx = 99;
    kfx_sim_state.chosen_spell_tooltip = 99;
    kfx_sim_state.manufactr_element = 99;
    kfx_sim_state.manufactr_spridx = 99;
    kfx_sim_state.manufactr_tooltip = 99;

    recall_localised_game_structure();

    CHECK(kfx_sim_state.active_panel_mnu_idx == 3);
    CHECK(kfx_net_state.comp_player_aggressive == 1);
    CHECK(kfx_net_state.comp_player_defensive == 2);
    CHECK(kfx_net_state.comp_player_construct == 3);
    CHECK(kfx_net_state.comp_player_creatrsonly == 4);
    CHECK(kfx_sim_state.creatures_tend_imprison == 5);
    CHECK(kfx_sim_state.creatures_tend_flee == 6);
    CHECK(kfx_sim_state.hand_over_subtile_x == 100);
    CHECK(kfx_sim_state.hand_over_subtile_y == 200);
    CHECK(kfx_sim_state.chosen_room_kind == 7);
    CHECK(kfx_sim_state.chosen_room_spridx == 8);
    CHECK(kfx_sim_state.chosen_room_tooltip == 9);
    CHECK(kfx_sim_state.chosen_spell_type == 10);
    CHECK(kfx_sim_state.chosen_spell_spridx == 11);
    CHECK(kfx_sim_state.chosen_spell_tooltip == 12);
    CHECK(kfx_sim_state.manufactr_element == 13);
    CHECK(kfx_sim_state.manufactr_spridx == 14);
    CHECK(kfx_sim_state.manufactr_tooltip == 15);
}

namespace {
GameTurn g_fake_gameturn = 1;
GameTurn fake_get_gameturn(void) { return g_fake_gameturn; }

int64_t g_captured_progress_pixels = -1;
int64_t g_captured_max_progress = -1;
int64_t g_captured_box_width = -1;
void fake_draw_out_of_sync_box(int64_t progress_pixels, int64_t max_progress, int64_t box_width) {
    g_captured_progress_pixels = progress_pixels;
    g_captured_max_progress = max_progress;
    g_captured_box_width = box_width;
}

int64_t g_fake_status_panel_width = 200;
int64_t fake_get_status_panel_width(void) { return g_fake_status_panel_width; }

struct AnimateResyncFixture : ResetStates {
    struct NetCallbacks net_cb{};
    struct RenderOverlayCallbacks overlay_cb{};

    AnimateResyncFixture() {
        g_fake_gameturn = 1;
        g_captured_progress_pixels = -1;
        g_captured_max_progress = -1;
        g_captured_box_width = -1;
        g_fake_status_panel_width = 200;
        units_per_pixel = 16; // "native" scale -- see bflib_video_test.cpp
        set_get_gameturn_provider(fake_get_gameturn);
        net_cb.draw_out_of_sync_box = fake_draw_out_of_sync_box;
        set_net_callbacks(&net_cb);
        overlay_cb.get_status_panel_width = fake_get_status_panel_width;
        set_render_overlay_callbacks(&overlay_cb);
    }
    ~AnimateResyncFixture() {
        set_get_gameturn_provider(nullptr);
        set_net_callbacks(nullptr);
        set_render_overlay_callbacks(nullptr);
    }
};
}

TEST_CASE_METHOD(AnimateResyncFixture, "animate_resync_progress_bar computes the progress-bar pixel width and forwards it to draw_out_of_sync_box", "[kfx_net][net_resync]") {
    animate_resync_progress_bar(1, 4);
    CHECK(g_captured_max_progress == 32);      // 32 * units_per_pixel(16) / 16
    CHECK(g_captured_progress_pixels == 8);    // max_progress * 1 / 4
    CHECK(g_captured_box_width == 200);        // read through render_overlay->get_status_panel_width()
}

TEST_CASE_METHOD(AnimateResyncFixture, "animate_resync_progress_bar is a no-op at gameturn 0 (the default GetGameTurnFunc)", "[kfx_net][net_resync]") {
    g_fake_gameturn = 0;
    animate_resync_progress_bar(1, 4);
    CHECK(g_captured_max_progress == -1); // draw_out_of_sync_box never called
}

TEST_CASE_METHOD(AnimateResyncFixture, "animate_resync_progress_bar is a no-op while the game is paused", "[kfx_net][net_resync]") {
    kfx_sim_state.operation_flags |= GOF_Paused;
    animate_resync_progress_bar(1, 4);
    CHECK(g_captured_max_progress == -1);
}
