// Regression coverage for a latent bug found while working on
// docs/refactor/editor/fx-plans/08-campaign-editor.md §12.10: Skirmish's non-networked
// start (frontend_freeplay_enter_resolve() below, the "Play this level" commit on the
// merged Free play/Skirmish screen) sets fe_computer_players = 1 so the engine fills the
// other dungeons with default computer AI (main_game.c's startup_network_game_tail). The
// only functions that used to clear it back to 0 were frontmap_load() (the legacy
// FeSt_LAND_VIEW cutscene) and entering multiplayer service selection -- neither of which a
// plain (non-Skirmish) Free play commit passes through, so a stale 1 from an earlier
// Skirmish visit this session could otherwise leak into a Free play level that should have
// no computer opponents at all. Fixed by making the assignment explicit either way.
#include <catch2/catch_test_macros.hpp>

#include "frontmenu_select.h"
#include "frontend.h"
#include "net_main.h" // net_service_index_selected, FrontendNetSvc_Skirmish/Online
#include "kfx_sim_state.h"
#include "skirmish_setup.h"

namespace {

// Leaves every global this test touches back at a harmless default, regardless of which
// TEST_CASE ran or whether an assertion failed midway -- these are process-global state
// other tests in this binary may also depend on.
struct FreeplayTestGuard {
    FreeplayTestGuard()
    {
        skirmish_setup_forget();
    }
    ~FreeplayTestGuard()
    {
        freeplay_highlighted_level = 0;
        net_service_index_selected = FrontendNetSvc_Online;
        fe_computer_players = 0;
        skirmish_setup_forget();
    }
};

} // namespace

TEST_CASE_METHOD(FreeplayTestGuard, "frontend_freeplay_enter_resolve does nothing when nothing is highlighted", "[kfx_frontend][frontmenu_select]") {
    freeplay_highlighted_level = 0;
    fe_computer_players = 1; // must be left untouched by the early return

    CHECK(frontend_freeplay_enter_resolve() == -1);
    CHECK(fe_computer_players == 1);
}

TEST_CASE_METHOD(FreeplayTestGuard, "frontend_freeplay_enter_resolve resets a stale fe_computer_players for a plain Free play level", "[kfx_frontend][frontmenu_select]") {
    net_service_index_selected = FrontendNetSvc_Online; // not Skirmish
    freeplay_highlighted_level = 1;
    fe_computer_players = 1; // simulates a leftover flag from an earlier Skirmish visit this session

    CHECK(frontend_freeplay_enter_resolve() == FeSt_START_KPRLEVEL);
    CHECK(fe_computer_players == 0);
}

TEST_CASE_METHOD(FreeplayTestGuard, "frontend_freeplay_enter_resolve still sets fe_computer_players for Skirmish", "[kfx_frontend][frontmenu_select]") {
    net_service_index_selected = FrontendNetSvc_Skirmish;
    freeplay_highlighted_level = 1;
    fe_computer_players = 0;

    CHECK(frontend_freeplay_enter_resolve() == FeSt_START_KPRLEVEL);
    CHECK(fe_computer_players == 1);
}

// Campaign Select's "Enter this land" (and Continue Game, which routes here via
// frontend_load_continue_game_resolve()) is the other single-player-only entry point that used
// to leave a stale fe_computer_players untouched -- it reaches FeSt_START_KPRLEVEL directly,
// never passing through frontmap_load()'s reset. No campaign-loading fixture is set up here, so
// only the early-return guard (nothing highlighted) is exercised, but the reset in
// frontend_land_selection_enter_resolve() runs unconditionally before that guard, so it still
// covers the fix.
TEST_CASE("frontend_land_selection_enter_resolve resets a stale fe_computer_players even when nothing is highlighted", "[kfx_frontend][frontmenu_select]") {
    land_selection_highlighted_campaign = nullptr;
    fe_computer_players = 1; // simulates a leftover flag from an earlier Skirmish visit this session

    CHECK(frontend_land_selection_enter_resolve() == -1);
    CHECK(fe_computer_players == 0);

    fe_computer_players = 0;
}
