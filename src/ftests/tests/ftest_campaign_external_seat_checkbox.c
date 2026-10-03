// The campaign/scenario select screen's "Seat control" dropdown (Human/Scripted/LLM), specifically its "LLM"
// option (fe_external_campaign, frontend.h) -- mutually exclusive with "Scripted" (fe_spectate_campaign) by
// construction, being one combo rather than independent checkboxes. Unlike Scripted (UiPort-mediated,
// applied directly in main_game.c::startup_network_game_tail()), "LLM" is consumed at the "Enter this land"
// moment, in kfx_frontend itself -- frontend_land_selection_enter_resolve() (frontmenu_select.c) arms the
// local player's own seat the same way Skirmish arms a rival's slot
// (skirmish_setup_install_for_play()/skirmish_setup_set_controller(SkirmishCtl_External), see
// ftest_skirmish_external_slot.cpp's own pre_start for the same "call the narrow arming step directly, not the
// whole UI resolve function" pattern), via net_pending_external_seats_add(). That queue is drained by the same
// net_claim_pending_external_seats() call startup_network_game_tail() already makes unconditionally for
// GKind_LocalGame -- this test proves that draining actually reaches my_player_number, not just a rival (every
// other seat/pending-queue test uses a rival slot; ftest_campaign_external_seat.c covers net_add_external_seat()
// itself for the local player directly, bypassing the pending queue this test is about).
#include "ftest_campaign_external_seat_checkbox.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"

#include "config_keeperfx.h"
#include "external_seat.h"
#include "frontend.h"
#include "net_game.h"
#include "player_data.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK_TRUE(what, cond) do { if (!(cond)) SOFT_FAIL("%s", what); } while (0)

FTestActionResult cesc01_check(struct FTestActionArgs* const args);

void ftest_campaign_external_seat_checkbox_pre_start()
{
    // What frontend_land_selection_enter_resolve() does when "Seat control" is set to "LLM" -- the arming
    // step alone, not the whole "Enter this land" resolve (which also picks a campaign/level the ftest
    // harness's own level_file/level config already handles).
    net_pending_external_seats_clear();
    net_pending_external_seats_add(my_player_number);
}

TbBool ftest_campaign_external_seat_checkbox_init()
{
    s_failures = 0;
    ftest_append_action(cesc01_check, 0, NULL);
    return true;
}

FTestActionResult cesc01_check(struct FTestActionArgs* const args)
{
    struct PlayerInfo* player = get_player(my_player_number);
    CHECK_TRUE("the queued seat was claimed as External by level start",
        flag_is_set(player->allocflags, PlaF_ExternalSeat) && !flag_is_set(player->allocflags, PlaF_CompCtrl));
    CHECK_TRUE("the player's user_id points at a real seat", player->user_id > SOLO_HUMAN_ID);

    // Cross-test contamination lesson (same as ftest_spectator_handoff.c/ftest_campaign_spectate_checkbox.c):
    // consecutive ftests share one process, so leave the player exactly as ftest_campaign_external_seat.c's
    // own ces01_claim expects to find them.
    net_release_external_seat(my_player_number);

    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " campaign external seat checkbox check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: the campaign select screen's Seat control dropdown (LLM) arms the local player's own "
             "seat, claimed at level start the same way Skirmish's own External slots are");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
