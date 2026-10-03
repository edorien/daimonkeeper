// The campaign/scenario select screen's "Seat control" dropdown (Human/Scripted/LLM), specifically its
// "Scripted" option (fe_spectate_campaign, frontend.h), at the point where it actually matters: set *before*
// the level even starts, so there is no local player/dungeon yet to hand off. Proves the wiring through
// UiPort (kfx_game can't see kfx_frontend's global directly) to
// main_game.c::startup_network_game_tail(), which applies it once the local player's dungeon exists (right
// where net_claim_pending_external_seats() does the analogous thing for the "LLM" option, and for Skirmish's
// own External-agent slots) -- not player_enter_spectator_mode itself, which ftest_spectator_handoff.c already
// covers directly and via its own packet round trip.
#include "ftest_campaign_spectate_checkbox.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"

#include "config_keeperfx.h"
#include "frontend.h"
#include "player_data.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK_TRUE(what, cond) do { if (!(cond)) SOFT_FAIL("%s", what); } while (0)

FTestActionResult csc01_check(struct FTestActionArgs* const args);

void ftest_campaign_spectate_checkbox_pre_start() { fe_spectate_campaign = 1; }

TbBool ftest_campaign_spectate_checkbox_init()
{
    s_failures = 0;
    ftest_append_action(csc01_check, 0, NULL);
    return true;
}

FTestActionResult csc01_check(struct FTestActionArgs* const args)
{
    struct PlayerInfo* player = get_player(my_player_number);
    CHECK_TRUE("checking Spectate before starting a campaign level hands the local player's seat to the AI",
        flag_is_set(player->allocflags, PlaF_CompCtrl));

    // Same cross-test-contamination lesson as ftest_spectator_handoff.c: consecutive ftests share one process.
    clear_flag(player->allocflags, PlaF_CompCtrl);
    fe_spectate_campaign = 0;

    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " campaign spectate checkbox check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: the campaign select screen's Seat control dropdown (Scripted) hands the seat to the AI at level start");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
