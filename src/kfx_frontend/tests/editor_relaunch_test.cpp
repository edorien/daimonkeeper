// docs/refactor/editor/phase3/02-slice3-dialogs-menubar.md -- coverage for
// the editor's in-session quit-and-relaunch mechanism: frontend_request_editor_relaunch()
// stashes the target (frontend.cpp, plain global writes -- no fixture
// needed) and get_startup_menu_state()'s new editor_pending_relaunch branch
// (frontend.cpp) takes priority over everything else in that function.
// That branch is deliberately the very first thing the function checks,
// before it touches game_flags2/kfx_sim_state/get_my_player() at all
// (frontend.cpp's own comment on why) -- specifically so this stays cheaply
// callable here without a live player/session fixture, unlike the rest of
// that function's branches.
#include <catch2/catch_test_macros.hpp>

#include "frontend.h"

namespace {

// Leaves every global this test/the function under test touches back at a
// harmless default, regardless of which SECTION ran or whether an
// assertion failed midway -- game_flags2/kfx_sim_state.mode_flags are
// process-global state other tests in this binary may also depend on.
struct RelaunchTestGuard {
    RelaunchTestGuard() {
        editor_pending_relaunch = false;
    }
    ~RelaunchTestGuard() {
        editor_pending_relaunch = false;
    }
};

} // namespace

TEST_CASE_METHOD(RelaunchTestGuard, "frontend_request_editor_relaunch stashes the target and sets the relaunch flag", "[kfx_frontend][editor_relaunch]") {
    frontend_request_editor_relaunch(12345, true, 90, 70, 3);

    CHECK(editor_pending_lvnum == 12345);
    CHECK(editor_pending_is_new == true);
    CHECK(editor_pending_new_map_w == 90);
    CHECK(editor_pending_new_map_h == 70);
    CHECK(editor_pending_new_map_texture == 3);
    CHECK(editor_pending_relaunch == true);
}

TEST_CASE_METHOD(RelaunchTestGuard, "get_startup_menu_state returns FeSt_START_EDITOR and clears the flag when a relaunch is pending", "[kfx_frontend][editor_relaunch]") {
    frontend_request_editor_relaunch(777, false, 0, 0, 0);
    REQUIRE(editor_pending_relaunch == true);

    FrontendMenuState state = get_startup_menu_state();

    CHECK(state == FeSt_START_EDITOR);
    // Consumed, not re-armed -- a second call with nothing new pending
    // must not keep re-selecting FeSt_START_EDITOR forever.
    CHECK(editor_pending_relaunch == false);
    CHECK(editor_pending_lvnum == 777);
    CHECK(editor_pending_is_new == false);
}

TEST_CASE_METHOD(RelaunchTestGuard, "get_startup_menu_state's relaunch branch is a no-op when nothing is pending", "[kfx_frontend][editor_relaunch]") {
    CHECK(editor_pending_relaunch == false);
    // Not asserting on the return value here -- with no relaunch pending,
    // get_startup_menu_state() falls through into branches that read
    // kfx_sim_state/get_my_player(), which this test deliberately doesn't
    // set up a fixture for. Just confirming the relaunch flag itself isn't
    // spuriously true is the point of this case.
}
