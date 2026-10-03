// kfx_frontend: the unsupported-content warning (frontgui_ingame.cpp) owns
// the screen while it's pending -- ingame_imgui_modal_active() must say so,
// or world clicks behind it would reach the game. It isn't an active_menus
// entry, so this is checked with the menu stack empty.
#include <catch2/catch_test_macros.hpp>

#include "frontgui_ingame.h"
#include "compat_report.h"
#include "gui_frontmenu.h"
#include "bflib_guibtns.h"
#include "kfx_sim_state.h"

#include <cstring>

namespace {
struct NoMenus {
    NoMenus() {
        std::memset(active_menus, 0, sizeof(active_menus));
        no_of_active_menus = 0;
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        compat_report_clear();
    }
    ~NoMenus() { compat_report_clear(); }
};
}

TEST_CASE_METHOD(NoMenus, "a pending compat warning makes the in-game GUI modal", "[kfx_frontend][compat_warning]") {
    kfx_sim_state.game_kind = GKind_LocalGame;
    CHECK_FALSE(ingame_imgui_modal_active());
    compat_report_set_review_pending(1);
    CHECK(ingame_imgui_modal_active());
    compat_report_set_review_pending(0);
    CHECK_FALSE(ingame_imgui_modal_active());
}

TEST_CASE_METHOD(NoMenus, "outside a running game a pending warning changes nothing", "[kfx_frontend][compat_warning]") {
    compat_report_set_review_pending(1); // e.g. still set while the frontend shows
    CHECK_FALSE(ingame_imgui_modal_active());
}
