// kfx_net: net_resync.cpp -- targeted per the user's request to focus
// coverage around scripts/check_layering.py's ACCEPTED (by-design,
// architecture.md §8.2) kfx_net -> kfx_game/kfx_frontend residual: this
// file #includes and memcpy()s game/kfx_game_state/kfx_frontend_state
// wholesale, the intentional raw-blob network resync wire format. The
// violation itself isn't going away without restructuring netcode, but
// the surrounding functions in the same file are still worth covering.
//
// (store/recall_localised_game_structure(), the resync's keep-list, are gone: this machine's view is kfx_render's
// kfx_local_view, which a resync doesn't touch -- refactor pass 5, S05.)
//
// animate_resync_progress_bar() is the first kfx_net test to combine
// two simultaneous fakes (the get_gameturn() source and UiPort)
// -- confirms both its early-return guards
// (gameturn 0, GOF_Paused) and, past them, the actual progress-bar pixel
// math it hands to draw_out_of_sync_box().
#include <catch2/catch_test_macros.hpp>

#include "net_resync.h"
#include "kfx_sim_state.h"
#include "kfx_net_state.h"
#include "bflib_video.h" // units_per_pixel
#include "globals.h" // set_gameturn_source
#include "ports/ui_port.h"

#include <cstring>

namespace {
struct ResetStates {
    ResetStates() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        local_system_flags = 0; // out of kfx_sim_state since 1ec821fa4, so the memset no longer clears it
        std::memset(&kfx_net_state, 0, sizeof(kfx_net_state));
    }
};
}

namespace {
GameTurn g_fake_gameturn = 1;

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
    struct UiPort ui_cb{};

    AnimateResyncFixture() {
        g_fake_gameturn = 1;
        g_captured_progress_pixels = -1;
        g_captured_max_progress = -1;
        g_captured_box_width = -1;
        g_fake_status_panel_width = 200;
        units_per_pixel = 16; // "native" scale -- see bflib_video_test.cpp
        set_gameturn_source(&g_fake_gameturn);
        ui_cb.draw_out_of_sync_box = fake_draw_out_of_sync_box;
        ui_cb.get_status_panel_width = fake_get_status_panel_width;
        set_ui_port(&ui_cb);
    }
    ~AnimateResyncFixture() {
        set_gameturn_source(nullptr);
        set_ui_port(nullptr);
    }
};
}

TEST_CASE_METHOD(AnimateResyncFixture, "animate_resync_progress_bar computes the progress-bar pixel width and forwards it to draw_out_of_sync_box", "[kfx_net][net_resync]") {
    animate_resync_progress_bar(1, 4);
    CHECK(g_captured_max_progress == 32);      // 32 * units_per_pixel(16) / 16
    CHECK(g_captured_progress_pixels == 8);    // max_progress * 1 / 4
    CHECK(g_captured_box_width == 200);        // read through ui_get_status_panel_width()
}

TEST_CASE_METHOD(AnimateResyncFixture, "animate_resync_progress_bar is a no-op at gameturn 0 (the unwired default)", "[kfx_net][net_resync]") {
    g_fake_gameturn = 0;
    animate_resync_progress_bar(1, 4);
    CHECK(g_captured_max_progress == -1); // draw_out_of_sync_box never called
}

TEST_CASE_METHOD(AnimateResyncFixture, "animate_resync_progress_bar is a no-op while the game is paused", "[kfx_net][net_resync]") {
    kfx_sim_state.operation_flags |= GOF_Paused;
    animate_resync_progress_bar(1, 4);
    CHECK(g_captured_max_progress == -1);
}

// The turn clock's phase, the fast-forward and the GUI's visibility are this machine's, kept across a resync or a
// load that replaces the state blobs wholesale (upstream #5441); the other operation flags are the new state's.
TEST_CASE("local_pacing_keep/restore keep this machine's pacing and GUI across a blob copy", "[kfx_net][net_resync]") {
    kfx_net_state.process_turn_time = 0.25;
    kfx_net_state.frame_skip = 8;
    kfx_sim_state.operation_flags = GOF_ShowPanel;
    const struct LocalPacingState kept = local_pacing_keep();
    // the incoming blobs: the other machine's pacing, its GUI hidden, another flag set
    kfx_net_state.process_turn_time = 0.75;
    kfx_net_state.frame_skip = 0;
    kfx_sim_state.operation_flags = GOF_ShowGui | GOF_Paused;
    local_pacing_restore(&kept);
    CHECK(kfx_net_state.process_turn_time == 0.25);
    CHECK(kfx_net_state.frame_skip == 8);
    CHECK((kfx_sim_state.operation_flags & GOF_ShowPanel) != 0);
    CHECK((kfx_sim_state.operation_flags & GOF_ShowGui) == 0);
    CHECK((kfx_sim_state.operation_flags & GOF_Paused) != 0);
}
