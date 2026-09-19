// set_map_ui_hidden(): hides the status menu / tooltips for the parchment map and its fades,
// and puts them back. Holding twice must keep the FIRST saved state, otherwise a second
// hold (or an exit that skipped the restore) saves the already-hidden state and the UI
// stays hidden for good (upstream #5294).
#include <catch2/catch_test_macros.hpp>

#include "player_instances.h"
#include "player_data.h"
#include "sim_feedback.h"
#include "config_settings.h"
#include "kfx_sim_state.h"

#include <cstring>

namespace {
struct MapUiFixture {
    static inline bool menu_visible;
    static unsigned long toggle(short visible) { unsigned long old = menu_visible; menu_visible = visible != 0; return old; }
    struct SimFeedbackCallbacks callbacks;
    MapUiFixture() {
        std::memset(&local_state, 0, sizeof(local_state));
        callbacks = *sim_feedback;
        callbacks.toggle_status_menu = &MapUiFixture::toggle;
        set_sim_feedback_callbacks(&callbacks);
        menu_visible = true;
        settings.tooltips_on = true;
    }
    ~MapUiFixture() { set_sim_feedback_callbacks(nullptr); }
};
}

TEST_CASE_METHOD(MapUiFixture, "set_map_ui_hidden hides then restores the status menu and tooltips", "[kfx_sim][map_ui]") {
    set_map_ui_hidden(true, true);
    CHECK_FALSE(menu_visible);
    CHECK_FALSE(settings.tooltips_on);
    set_map_ui_hidden(false, false);
    CHECK(menu_visible);
    CHECK(settings.tooltips_on);
}

TEST_CASE_METHOD(MapUiFixture, "a second hold keeps the first saved state", "[kfx_sim][map_ui]") {
    set_map_ui_hidden(true, true);
    set_map_ui_hidden(true, true); // e.g. an interrupted fade started again: would save "hidden"
    set_map_ui_hidden(false, false);
    CHECK(menu_visible);
    CHECK(settings.tooltips_on);
}

TEST_CASE_METHOD(MapUiFixture, "tooltips can be restored while the status menu stays hidden for the map", "[kfx_sim][map_ui]") {
    set_map_ui_hidden(true, true);
    set_map_ui_hidden(true, false); // fade-to-map end: map is up, tooltips return
    CHECK_FALSE(menu_visible);
    CHECK(settings.tooltips_on);
}

TEST_CASE_METHOD(MapUiFixture, "restoring with nothing held changes nothing", "[kfx_sim][map_ui]") {
    menu_visible = false;
    settings.tooltips_on = false;
    set_map_ui_hidden(false, false);
    CHECK_FALSE(menu_visible);
    CHECK_FALSE(settings.tooltips_on);
}
