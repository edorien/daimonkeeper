// Time to think, where pausing is fair (docs/plans/llm-bridge-realtime.md, Phase 6), through the API as an agent uses it.
// set_decision_policy on_urgent "pause" is refused while a human plays a keeper (HUMAN_PLAYING), and accepted once the
// game's setup allows the agent to pause (the Skirmish page's LLM Pause). Then a fight on the seat's ground makes an
// urgent decision, and the game pauses for the agent's answer (seat.decision.hold says so); ack_decision lets it run on.
// Last, on_urgent "slow": the game runs at 3 turns a second until an accepted order answers, then at its own pace again.
#include "ftest_ai_seat_urgent_hold.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>
#include <stdio.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api.h"
#include "api_seat_view.h"
#include "bflib_datetm.h"
#include "config_keeperfx.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_sim_state.h"
#include "map_events.h"
#include "net_game.h"
#include "player_data.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _WIN32
static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK_TRUE(what, cond) do { if (!(cond)) SOFT_FAIL("%s", what); } while (0)

static PlayerNumber P = -1;
static MapSlabCoord hsx, hsy;
static TbClockMSec s_since = 0;
static int s_round = 0;

static TbBool paused(void) { return flag_is_set(kfx_sim_state.operation_flags, GOF_Paused); }

/** One API request as the agent sends it; returns the reply line. */
static const char* api_call(const char* json)
{
    static char reply[4096];
    int sv[2];
    reply[0] = 0;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return reply;
    api_process_message_for_test(json, strlen(json), sv[0]);
    const ssize_t got = recv(sv[1], reply, sizeof(reply) - 1, MSG_DONTWAIT);
    reply[(got > 0) ? got : 0] = 0;
    close(sv[0]); close(sv[1]);
    return reply;
}

static TbBool held_in_view(void)
{
    VALUE v; api_seat_build_view(&v, P);
    VALUE* hold = value_dict_get(value_dict_get(value_dict_get(&v, "seat"), "decision"), "hold");
    const TbBool held = (hold != NULL);
    value_fini(&v);
    return held;
}

static void a_fight(void)
{
    // Somewhere new each round: a fight event near an existing one only refreshes it.
    event_create_event_or_update_nearby_existing_event(subtile_coord_center(slab_subtile_center(hsx + 4 * s_round)),
        subtile_coord_center(slab_subtile_center(hsy + 3)), EvKind_EnemyFight, P, 0);
    s_round++;
}

FTestActionResult uh01_setup(struct FTestActionArgs* const args);
FTestActionResult uh02_policy(struct FTestActionArgs* const args);
FTestActionResult uh03_paused_for_the_answer(struct FTestActionArgs* const args);
FTestActionResult uh04_answered_runs_on(struct FTestActionArgs* const args);
FTestActionResult uh05_slowed(struct FTestActionArgs* const args);
FTestActionResult uh06_order_restores(struct FTestActionArgs* const args);

void ftest_ai_seat_urgent_hold_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_urgent_hold_init()
{
    s_failures = 0; s_round = 0;
    ftest_append_action(uh01_setup, 0, NULL);
    ftest_append_action(uh02_policy, 2, NULL);
    ftest_append_action(uh03_paused_for_the_answer, 0, NULL);
    ftest_append_action(uh04_answered_runs_on, 0, NULL);
    ftest_append_action(uh05_slowed, 120, NULL);
    ftest_append_action(uh06_order_restores, 0, NULL);
    return true;
}

FTestActionResult uh01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if ((P < 0) || (net_add_external_seat(P) < 1)) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    const struct Thing* heart = find_players_dungeon_heart(P);
    hsx = subtile_slab(heart->mappos.x.stl.num); hsy = subtile_slab(heart->mappos.y.stl.num);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult uh02_policy(struct FTestActionArgs* const args)
{
    char req[160];
    snprintf(req, sizeof(req), "{\"action\":\"set_decision_policy\",\"player\":%d,\"on_urgent\":\"pause\",\"hold_seconds\":30,\"ack\":1}", (int)P);
    CHECK_TRUE("a human plays a keeper here: on_urgent is refused", extseat_human_playing() && strstr(api_call(req), "HUMAN_PLAYING") != NULL);
    extseat_allow_pause_with_human(true);   // as the Skirmish page's LLM Pause box does
    const char* r = api_call(req);
    CHECK_TRUE("with LLM Pause on it is accepted", strstr(r, "\"success\":true") != NULL);
    CHECK_TRUE("the game runs", !paused());
    a_fight();
    s_since = LbTimerClock();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult uh03_paused_for_the_answer(struct FTestActionArgs* const args)
{
    // The fight is noticed on the next poll, its decision emitted, and the game paused: this action keeps being called
    // while the game is paused (the turn does not move), until it sees the pause.
    if (!paused()) {
        if ((int64_t)(LbTimerClock() - s_since) > 10000) { SOFT_FAIL("the urgent decision never paused the game"); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    FTESTLOG("paused for the agent at turn %d, %d ms after the fight", (int)get_gameturn(), (int)(LbTimerClock() - s_since));
    CHECK_TRUE("the pause is the agent's", extseat_agent_owns_pause());
    CHECK_TRUE("the view says the game is held for this seat", held_in_view());
    char req[96];
    snprintf(req, sizeof(req), "{\"action\":\"ack_decision\",\"player\":%d,\"ack\":2}", (int)P);
    CHECK_TRUE("ack_decision is accepted", strstr(api_call(req), "\"success\":true") != NULL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult uh04_answered_runs_on(struct FTestActionArgs* const args)
{
    CHECK_TRUE("answered, the game runs again", !paused());
    CHECK_TRUE("and the view no longer says held", !held_in_view());
    char req[192];
    snprintf(req, sizeof(req), "{\"action\":\"set_decision_policy\",\"player\":%d,\"on_urgent\":\"slow\",\"slow_turns_per_second\":3,\"ack\":3}", (int)P);
    CHECK_TRUE("on_urgent slow is accepted", strstr(api_call(req), "\"success\":true") != NULL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult uh05_slowed(struct FTestActionArgs* const args)
{
    // Past the gap since the first fight's wake-up: a second fight.
    if (s_round == 1) { a_fight(); s_since = LbTimerClock(); }
    if (kfx_sim_state.turns_per_second != 3) {
        if ((int64_t)(LbTimerClock() - s_since) > 10000) { SOFT_FAIL("the urgent decision never slowed the game (%d turns/s)", (int)kfx_sim_state.turns_per_second); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    CHECK_TRUE("slowed, not paused", !paused());
    // An accepted order from the seat answers it.
    char req[160];
    snprintf(req, sizeof(req), "{\"action\":\"submit_action\",\"player\":%d,\"verb\":\"set_tendency\",\"kind\":\"flee\",\"enabled\":true,\"ack\":4}", (int)P);
    const char* r = api_call(req);
    CHECK_TRUE("the order is accepted", strstr(r, "\"success\":true") != NULL);
    if (strstr(r, "\"success\":true") == NULL) FTESTLOG("submit_action said: %s", r);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult uh06_order_restores(struct FTestActionArgs* const args)
{
    FTESTLOG("after the order: %d turns/s (the level's own: %d)", (int)kfx_sim_state.turns_per_second, (int)start_params.num_fps);
    CHECK_TRUE("the game runs at its own pace again", kfx_sim_state.turns_per_second == start_params.num_fps);
    char req[128];
    snprintf(req, sizeof(req), "{\"action\":\"set_decision_policy\",\"player\":%d,\"on_urgent\":\"none\",\"ack\":5}", (int)P);
    api_call(req);
    extseat_allow_pause_with_human(false);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " hold check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: an urgent decision pauses (or slows) the game for the agent's answer, where pausing is allowed");
    return FTRs_Go_To_Next_Action;
}

#else
TbBool ftest_ai_seat_urgent_hold_init() { FTESTLOG("Test passed: skipped on Windows (the API is called through a socketpair)"); return true; }
void ftest_ai_seat_urgent_hold_pre_start() { }
#endif // _WIN32

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
