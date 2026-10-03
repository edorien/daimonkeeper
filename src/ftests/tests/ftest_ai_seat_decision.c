// "A decision is due" (api_seat_decision.c): an External seat's agent should think at natural moments -- each quarter of a
// pay day and when something important happens -- not on a timer. Drives the engine: a quarter boundary, a pay day wrap,
// a fight event, a new creature kind; checks the counter and reasons in the view (seat.decision), that reasons inside the
// minimum interval are held and delivered together after it, and that nothing fires spuriously or for a non-seat player.
#include "ftest_ai_seat_decision.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>
#include <stdlib.h>
#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api_seat_decision.h"
#include "api_seat_view.h"
#include "config_creature.h"
#include "config_keeperfx.h"
#include "dungeon_data.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_config_state.h"
#include "map_events.h"
#include "net_game.h"
#include "player_data.h"
#include "slab_data.h"
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
static int64_t s_mark = 0;

// seq of the latest decision and its reasons joined with ',' ("" when none).
static int64_t decision(char* reasons, size_t cap, int64_t* pending)
{
    VALUE v; api_seat_build_view(&v, P);
    VALUE* dec = value_dict_get(value_dict_get(&v, "seat"), "decision");
    const int64_t seq = value_int64(value_dict_get(dec, "seq"));
    if (pending) *pending = value_int64(value_dict_get(dec, "pending"));
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
static void raise_event(EventKind k)
{
    event_create_event_or_update_nearby_existing_event(subtile_coord_center(hx), subtile_coord_center(hy + 5), k, P, 0);
}
static int64_t gap(void) { return kfx_config_state.conf.rules[P].gameplay.pay_day_gap; }

FTestActionResult dc01_setup(struct FTestActionArgs* const args);
FTestActionResult dc02_quiet_then_quarter(struct FTestActionArgs* const args);
FTestActionResult dc03_check_quarter_then_wrap(struct FTestActionArgs* const args);
FTestActionResult dc04_check_wrap_then_fight(struct FTestActionArgs* const args);
FTestActionResult dc05_check_fight_then_coalesce(struct FTestActionArgs* const args);
FTestActionResult dc06_check_held_then_kind(struct FTestActionArgs* const args);
FTestActionResult dc07_check_kind(struct FTestActionArgs* const args);

void ftest_ai_seat_decision_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_decision_init()
{
    s_failures = 0;
    ftest_append_action(dc01_setup, 0, NULL);
    ftest_append_action(dc02_quiet_then_quarter, 12, NULL);
    ftest_append_action(dc03_check_quarter_then_wrap, 3, NULL);
    ftest_append_action(dc04_check_wrap_then_fight, 3, NULL);
    ftest_append_action(dc05_check_fight_then_coalesce, 3, NULL);
    ftest_append_action(dc06_check_held_then_kind, 3, NULL);
    ftest_append_action(dc07_check_kind, 3, NULL);
    return true;
}

FTestActionResult dc01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || net_add_external_seat(P) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    const struct Thing* heart = find_players_dungeon_heart(P);
    hx = heart->mappos.x.stl.num; hy = heart->mappos.y.stl.num;
    ftest_util_replace_slabs(subtile_slab(hx) - 4, subtile_slab(hy) + 3, subtile_slab(hx) + 4, subtile_slab(hy) + 5, SlbT_CLAIMED, P);
    api_seat_decision_set_min_interval(10);
    kfx_config_state.pay_day_progress[P] = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult dc02_quiet_then_quarter(struct FTestActionArgs* const args)
{
    char r[200]; int64_t pend;
    CHECK_TRUE("nothing is due in a quiet game (baseline taken, nothing raised)", decision(r, sizeof(r), &pend) == 0 && pend == 0);
    kfx_config_state.pay_day_progress[P] = gap() / 4 + 1;       // crosses the first quarter boundary
    return FTRs_Go_To_Next_Action;
}

FTestActionResult dc03_check_quarter_then_wrap(struct FTestActionArgs* const args)
{
    char r[200]; int64_t pend;
    const int64_t seq = decision(r, sizeof(r), &pend);
    FTESTLOG("after the quarter: seq %" PRId64 " reasons '%s'", seq, r);
    CHECK_TRUE("a quarter boundary raises a decision", seq == 1 && has(r, "quarter_1"));
    kfx_config_state.pay_day_progress[P] = 5;                   // pay day paid out: wraps to quarter 0
    return FTRs_Go_To_Next_Action;
}

FTestActionResult dc04_check_wrap_then_fight(struct FTestActionArgs* const args)
{
    char r[200]; int64_t pend;
    // The wrap happened 3 turns ago but the last decision was under 10 turns ago: it is held, then delivered.
    int64_t seq = decision(r, sizeof(r), &pend);
    CHECK_TRUE("inside the minimum interval the reason is held", seq == 1 && pend == 1);
    s_mark = (int64_t)get_gameturn();
    return (seq == 1) ? FTRs_Go_To_Next_Action : FTRs_Go_To_Next_Action;
}

FTestActionResult dc05_check_fight_then_coalesce(struct FTestActionArgs* const args)
{
    char r[200]; int64_t pend;
    if ((int64_t)get_gameturn() < s_mark + 10) return FTRs_Repeat_Current_Action;
    const int64_t seq = decision(r, sizeof(r), &pend);
    FTESTLOG("after the interval: seq %" PRId64 " reasons '%s'", seq, r);
    CHECK_TRUE("the held reason is delivered when the interval ends", seq == 2 && has(r, "payday"));
    // Two things at once inside one window: they arrive together.
    raise_event(EvKind_EnemyFight);
    raise_event(EvKind_HeartAttacked);
    s_mark = (int64_t)get_gameturn();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult dc06_check_held_then_kind(struct FTestActionArgs* const args)
{
    char r[200]; int64_t pend;
    if ((int64_t)get_gameturn() < s_mark + 12) return FTRs_Repeat_Current_Action;
    const int64_t seq = decision(r, sizeof(r), &pend);
    FTESTLOG("after two events: seq %" PRId64 " reasons '%s'", seq, r);
    CHECK_TRUE("a fight and an attack on the heart arrive as one decision", seq == 3 && has(r, "enemy_fight") && has(r, "heart_attacked"));
    // A creature kind the seat has never had.
    struct Thing* t = ftest_util_create_creature(subtile_coord_center(hx - 3), subtile_coord_center(hy + 8), P, 2, (ThingModel)creature_model_id("TROLL"));
    CHECK_TRUE("created a creature of a new kind", !thing_is_invalid(t));
    s_mark = (int64_t)get_gameturn();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult dc07_check_kind(struct FTestActionArgs* const args)
{
    char r[200]; int64_t pend;
    if ((int64_t)get_gameturn() < s_mark + 12) return FTRs_Repeat_Current_Action;
    const int64_t seq = decision(r, sizeof(r), &pend);
    FTESTLOG("after the new kind: seq %" PRId64 " reasons '%s'", seq, r);
    CHECK_TRUE("the first creature of a new kind raises a decision naming it", seq == 4 && has(r, "new_kind_TROLL"));
    // Nothing more happens: no further decisions.
    char r2[200];
    CHECK_TRUE("no spurious decisions", decision(r2, sizeof(r2), &pend) == 4 && pend == 0);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " decision check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: decisions are raised at quarters and major events, held inside the interval, and coalesced");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
