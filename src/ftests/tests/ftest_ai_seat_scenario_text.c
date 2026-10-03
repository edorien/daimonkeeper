// The level talking to an External seat (docs/refactor/AI/omissions/02): the objective as the objective box shows it
// (own.objective, from evntbox_text_objective, which every DISPLAY_OBJECTIVE / QUICK_OBJECTIVE writes), the objectives
// before it (own.objective_history: the sim keeps only the latest), the text of information and quick-information events
// exactly as the event box reads (event_text_for), and the decision reasons for each -- objective, information, and
// victory, which must not wait out the minimum interval.
#include "ftest_ai_seat_scenario_text.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>
#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api_seat_decision.h"
#include "api_seat_view.h"
#include "config_keeperfx.h"
#include "config_strings.h"
#include "dungeon_data.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_config_state.h"
#include "main_game.h"
#include "map_events.h"
#include "net_game.h"
#include "player_data.h"
#include "player_utils.h"
#include "thing_list.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK_TRUE(what, cond) do { if (!(cond)) SOFT_FAIL("%s", what); } while (0)

static PlayerNumber P = -1;
static MapSubtlCoord hx, hy;
static EventIndex s_info_ev = 0, s_quick_ev = 0;
static TextStringId s_info_string = 0;
static const char* OBJ_A = "Objective A: find the portal and claim it.";
static const char* OBJ_B = "Objective B: destroy the heroes' heart.";
static const char* QUICK = "A quick word from the level: gold lies to the east.";

static int64_t decision(char* reasons, size_t cap)
{
    VALUE v; api_seat_build_view(&v, P);
    VALUE* dec = value_dict_get(value_dict_get(&v, "seat"), "decision");
    const int64_t seq = value_int64(value_dict_get(dec, "seq"));
    reasons[0] = 0;
    VALUE* arr = value_dict_get(dec, "reasons");
    for (size_t i = 0; arr && i < value_array_size(arr); i++) {
        if (i) strncat(reasons, ",", cap - strlen(reasons) - 1);
        strncat(reasons, value_string(value_array_get(arr, i)), cap - strlen(reasons) - 1);
    }
    value_fini(&v);
    return seq;
}
static TbBool has(const char* reasons, const char* r) { return strstr(reasons, r) != NULL; }

// The view's objective, its history (texts joined with '|'), and an event's text by id.
static void objectives(char* cur, size_t ccap, char* hist, size_t hcap)
{
    VALUE v; api_seat_build_view(&v, P);
    VALUE* own = value_dict_get(&v, "own");
    snprintf(cur, ccap, "%s", value_string(value_dict_get(own, "objective")));
    hist[0] = 0;
    VALUE* arr = value_dict_get(own, "objective_history");
    for (size_t i = 0; arr && i < value_array_size(arr); i++) {
        if (i) strncat(hist, "|", hcap - strlen(hist) - 1);
        strncat(hist, value_string(value_dict_get(value_array_get(arr, i), "text")), hcap - strlen(hist) - 1);
    }
    value_fini(&v);
}
static TbBool event_text_is(EventIndex id, const char* want)
{
    VALUE v; api_seat_build_view(&v, P);
    VALUE* evs = value_dict_get(value_dict_get(&v, "own"), "events");
    TbBool ok = false;
    for (size_t i = 0; evs && i < value_array_size(evs); i++) {
        VALUE* e = value_array_get(evs, i);
        if (value_int32(value_dict_get(e, "id")) != id) continue;
        VALUE* t = value_dict_get(e, "text");
        FTESTLOG("event %d text: '%s'", (int)id, t ? value_string(t) : "(none)");
        ok = (t != NULL) && (strcmp(value_string(t), want) == 0);
    }
    value_fini(&v);
    return ok;
}

FTestActionResult st01_setup(struct FTestActionArgs* const args);
FTestActionResult st02_opening_objective_then_replace(struct FTestActionArgs* const args);
FTestActionResult st03_check_new_objective_then_messages(struct FTestActionArgs* const args);
FTestActionResult st04_check_messages_then_win(struct FTestActionArgs* const args);
FTestActionResult st05_check_victory(struct FTestActionArgs* const args);

void ftest_ai_seat_scenario_text_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_scenario_text_init()
{
    s_failures = 0;
    ftest_append_action(st01_setup, 0, NULL);
    ftest_append_action(st02_opening_objective_then_replace, 12, NULL);
    ftest_append_action(st03_check_new_objective_then_messages, 12, NULL);
    ftest_append_action(st04_check_messages_then_win, 12, NULL);
    ftest_append_action(st05_check_victory, 3, NULL);
    return true;
}

FTestActionResult st01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || net_add_external_seat(P) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    const struct Thing* heart = find_players_dungeon_heart(P);
    hx = heart->mappos.x.stl.num; hy = heart->mappos.y.stl.num;
    api_seat_decision_set_min_interval(10);
    // What QUICK_OBJECTIVE / DISPLAY_OBJECTIVE do: the level opens with this objective before the seat first looks.
    process_objective_with_icon(OBJ_A, P, 0, hx, hy, -1);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult st02_opening_objective_then_replace(struct FTestActionArgs* const args)
{
    char r[200], cur[MESSAGE_TEXT_LEN], hist[4 * MESSAGE_TEXT_LEN];
    objectives(cur, sizeof(cur), hist, sizeof(hist));
    CHECK_TRUE("the view carries the objective the level opened with", strcmp(cur, OBJ_A) == 0);
    CHECK_TRUE("and it is in the history", strcmp(hist, OBJ_A) == 0);
    CHECK_TRUE("the opening objective is history, not news: no decision raised", decision(r, sizeof(r)) == 0);
    process_objective_with_icon(OBJ_B, P, 0, hx, hy, -1);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult st03_check_new_objective_then_messages(struct FTestActionArgs* const args)
{
    char r[200], cur[MESSAGE_TEXT_LEN], hist[4 * MESSAGE_TEXT_LEN];
    const int64_t seq = decision(r, sizeof(r));
    FTESTLOG("after the new objective: seq %" PRId64 " reasons '%s'", seq, r);
    CHECK_TRUE("a new objective raises a decision", seq == 1 && has(r, "objective"));
    objectives(cur, sizeof(cur), hist, sizeof(hist));
    CHECK_TRUE("the view carries the new objective", strcmp(cur, OBJ_B) == 0);
    char want[2 * MESSAGE_TEXT_LEN];
    snprintf(want, sizeof(want), "%s|%s", OBJ_A, OBJ_B);
    CHECK_TRUE("the history keeps both, oldest first", strcmp(hist, want) == 0);

    // Any string the loaded language has (which ids are filled depends on the campaign's text files).
    for (TextStringId id = 1; (id < 2000) && (s_info_string == 0); id++) if (get_string(id)[0] != 0) s_info_string = id;
    FTESTLOG("information message: string %d '%s'", (int)s_info_string, get_string(s_info_string));
    struct Event* info = event_create_event(subtile_coord_center(hx), subtile_coord_center(hy), EvKind_Information, P, -s_info_string);
    snprintf(kfx_sim_state.quick_messages[7], sizeof(kfx_sim_state.quick_messages[7]), "%s", QUICK);
    struct Event* quick = event_create_event(subtile_coord_center(hx), subtile_coord_center(hy + 3), EvKind_QuickInformation, P, 7);
    if (info == NULL || quick == NULL) { SOFT_FAIL("could not create the message events"); return FTRs_Go_To_Next_Action; }
    s_info_ev = info->index; s_quick_ev = quick->index;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult st04_check_messages_then_win(struct FTestActionArgs* const args)
{
    char r[200];
    const int64_t seq = decision(r, sizeof(r));
    FTESTLOG("after the messages: seq %" PRId64 " reasons '%s'", seq, r);
    CHECK_TRUE("an information message raises a decision", seq == 2 && has(r, "information"));
    CHECK_TRUE("an information event carries the string the event box shows", (s_info_string != 0) && event_text_is(s_info_ev, get_string(s_info_string)));
    CHECK_TRUE("a quick-information event carries the level's own message", event_text_is(s_quick_ev, QUICK));
    // Victory must not wait: set an interval far longer than the test and win.
    api_seat_decision_set_min_interval(100000);
    set_player_as_won_level(get_player(P));
    return FTRs_Go_To_Next_Action;
}

FTestActionResult st05_check_victory(struct FTestActionArgs* const args)
{
    char r[200];
    const int64_t seq = decision(r, sizeof(r));
    FTESTLOG("after the win: seq %" PRId64 " reasons '%s'", seq, r);
    CHECK_TRUE("victory raises a decision at once, whatever the minimum interval", seq == 3 && has(r, "victory"));
    api_seat_decision_set_min_interval(100);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " scenario-text check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: objectives, their history, message text and victory reach the seat as a human would see them");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
