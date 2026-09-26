// Phase M5 of docs/refactor/AI/LLM/01-integration-plan.md (owed by 06-lifecycle-and-robustness.md section 3.3): a seat
// that has reached a decided state. Two External seats on original-pack multiplayer map 60: seat B loses the natural
// way (its dungeon heart is destroyed and check_players_lost() fires), seat A is declared the winner
// (set_player_as_won_level(), which a script's WIN_GAME goal calls). The view must report "lost" / "won" for the
// right seat and "undecided" for the human; the game must keep running for the others; and a request from a decided
// seat must still be answered without crashing the engine or the driver.
#include "ftest_ai_seat_decided.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <stdlib.h>
#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"
#include "../ftest_packet_inject.h"

#include "api_seat_view.h"
#include "frontend.h"
#include "game_legacy.h"
#include "config_keeperfx.h"
#include "config_players.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "net_game.h"
#include "packet_data.h"
#include "player_data.h"
#include "player_utils.h"
#include "thing_list.h"
#include "thing_objects.h"
#include "thing_list.h"
#include <json.h>
#include <json-dom.h>

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK_TRUE(what, cond) do { if (!(cond)) SOFT_FAIL("%s", what); } while (0)

static PlayerNumber PW = -1, PL = -1; // winner, loser
static NetUserId UW = -1, UL = -1;
static int64_t s_turn_at_decision = 0;

// "won" / "lost" / "undecided" as the view reports it for `plyr`.
static const char* view_victory(PlayerNumber plyr)
{
    static char buf[16];
    VALUE view; api_seat_build_view(&view, plyr);
    VALUE* seat = value_dict_get(&view, "seat");
    VALUE* v = seat ? value_dict_get(seat, "victory_state") : NULL;
    snprintf(buf, sizeof(buf), "%s", (v && value_type(v) == VALUE_STRING) ? value_string(v) : "<missing>");
    value_fini(&view);
    return buf;
}

FTestActionResult d01_setup(struct FTestActionArgs* const args);
FTestActionResult d02_check_undecided_then_decide(struct FTestActionArgs* const args);
FTestActionResult d03_check_decided(struct FTestActionArgs* const args);
FTestActionResult d04_check_still_running(struct FTestActionArgs* const args);

void ftest_ai_seat_decided_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_decided_init()
{
    ftest_packet_inject_reset();
    s_failures = 0;
    ftest_append_action(d01_setup, 0, NULL);
    ftest_append_action(d02_check_undecided_then_decide, 2, NULL);
    ftest_append_action(d03_check_decided, 5, NULL);
    ftest_append_action(d04_check_still_running, 10, NULL);
    return true;
}

FTestActionResult d01_setup(struct FTestActionArgs* const args)
{
    PW = PL = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
    {
        if (p == my_player_number || p == PLAYER_GOOD || p == PLAYER_NEUTRAL) continue;
        if (!player_exists(get_player(p)) || thing_is_invalid(find_players_dungeon_heart(p))) continue;
        if (PW < 0) PW = p; else if (PL < 0) PL = p;
    }
    if (PW < 0 || PL < 0) { FTEST_FAIL_TEST("need two rival keepers with hearts (map 60)"); return FTRs_Go_To_Next_Action; }
    UW = net_add_external_seat(PW);
    UL = net_add_external_seat(PL);
    CHECK_TRUE("both seats claimed", UW > 0 && UL > 0);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult d02_check_undecided_then_decide(struct FTestActionArgs* const args)
{
    CHECK_TRUE("winner seat starts undecided", strcmp(view_victory(PW), "undecided") == 0);
    CHECK_TRUE("loser seat starts undecided", strcmp(view_victory(PL), "undecided") == 0);

    // The loser: destroy its heart and let check_players_lost() notice, as a battle would.
    struct Thing* heart = find_players_dungeon_heart(PL);
    if (thing_is_invalid(heart)) { FTEST_FAIL_TEST("loser has no heart"); return FTRs_Go_To_Next_Action; }
    delete_thing_structure(heart, 0);
    // The winner: what a script's win condition does.
    set_player_as_won_level(get_player(PW));
    s_turn_at_decision = (int64_t)get_gameturn();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult d03_check_decided(struct FTestActionArgs* const args)
{
    CHECK_TRUE("the seat that lost its heart reports lost", strcmp(view_victory(PL), "lost") == 0);
    CHECK_TRUE("the seat declared the winner reports won", strcmp(view_victory(PW), "won") == 0);
    CHECK_TRUE("the human is still undecided", strcmp(view_victory(my_player_number), "undecided") == 0);
    CHECK_TRUE("the loser is still a seat (the mapping is not torn down)", get_net_user_player_number(UL) == PL);

    // A decided seat is still a seat: verbs and view requests are answered, not crashed on.
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v)); v.kind = ESV_Slap; v.has_thing = true; v.thing_id = 1;
    (void)extseat_submit_verb(UL, PL, &v, NULL);
    struct FtestPacketInject r = {0}; r.action = PckA_SetPlyrState; r.par1 = PSt_Slap;
    ftest_packet_inject_queue(UL, &r);
    ftest_packet_inject_queue(UW, &r);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult d04_check_still_running(struct FTestActionArgs* const args)
{
    CHECK_TRUE("game turns keep advancing after the decision", (int64_t)get_gameturn() > s_turn_at_decision + 5);
    CHECK_TRUE("the loser stays lost", strcmp(view_victory(PL), "lost") == 0);
    CHECK_TRUE("the winner stays won", strcmp(view_victory(PW), "won") == 0);
    CHECK_TRUE("a packet from the decided winner still reaches it", get_player(PW)->work_state == PSt_Slap);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " decided-seat check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: a seat that lost by heart destruction reports lost, a declared winner reports won, and the game keeps running");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
