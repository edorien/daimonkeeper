// Unreachable dig marks: a mark_dig corridor that touches no walkable ground is tagged by the engine but no imp can ever
// get to it. api_seat_unreachable_dig_slabs finds them (a slab flood fill from the seat's own walkable ground, carried
// on through marked slabs); the view lists them and a mark_dig reply warns. Layout, in slabs from the heart (hsx, hsy):
// a claimed strip on row hsy+3, earth on rows hsy+4..hsy+10 -- a corridor from row hsy+4 is reachable, one starting at
// row hsy+6 is not until the gap (rows hsy+4..hsy+5) is marked too.
#include "ftest_ai_seat_dig_reach.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>
#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api_seat_view.h"
#include "config_keeperfx.h"
#include "config_players.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_config_state.h"
#include "net_game.h"
#include "player_data.h"
#include "slab_data.h"
#include "tasks_list.h"
#include "thing_list.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK_TRUE(what, cond) do { if (!(cond)) SOFT_FAIL("%s", what); } while (0)

static PlayerNumber P = -1;
static NetUserId U = -1;
static MapSlabCoord hsx, hsy;

static struct ExtSeatVerb dig(int64_t x0, int64_t y0, int64_t x1, int64_t y1)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_MarkDig; v.has_rect = true;
    v.slab_x0 = x0; v.slab_y0 = y0; v.slab_x1 = x1; v.slab_y1 = y1;
    return v;
}
static int64_t unreachable(const int64_t *assume4, int64_t n_assume, const int64_t *report4)
{
    return api_seat_unreachable_dig_slabs(P, assume4, n_assume, report4, NULL, 0);
}
static int64_t view_unreachable_count(void)
{
    VALUE v; api_seat_build_view(&v, P);
    const int64_t n = value_int32(value_dict_get(value_dict_get(&v, "own"), "unreachable_dig_marks_count"));
    value_fini(&v);
    return n;
}

FTestActionResult dr01_setup_and_direct_checks(struct FTestActionArgs* const args);
FTestActionResult dr02_wait_tagged(struct FTestActionArgs* const args);
FTestActionResult dr03_view_lists_it_then_connect(struct FTestActionArgs* const args);
FTestActionResult dr04_wait_tagged_and_check_fixed(struct FTestActionArgs* const args);

void ftest_ai_seat_dig_reach_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_dig_reach_init()
{
    s_failures = 0;
    ftest_append_action(dr01_setup_and_direct_checks, 0, NULL);
    ftest_append_action(dr02_wait_tagged, 1, NULL);
    ftest_append_action(dr03_view_lists_it_then_connect, 1, NULL);
    ftest_append_action(dr04_wait_tagged_and_check_fixed, 1, NULL);
    return true;
}

FTestActionResult dr01_setup_and_direct_checks(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || (U = net_add_external_seat(P)) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    const struct Thing* heart = find_players_dungeon_heart(P);
    hsx = subtile_slab(heart->mappos.x.stl.num); hsy = subtile_slab(heart->mappos.y.stl.num);
    ftest_util_replace_slabs(hsx - 4, hsy + 3, hsx + 4, hsy + 10, SlbT_EARTH, kfx_config_state.neutral_player_num);
    ftest_util_replace_slabs(hsx - 4, hsy + 3, hsx + 4, hsy + 3, SlbT_CLAIMED, P);

    const int64_t touching[4] = { hsx, hsy + 4, hsx, hsy + 6 };
    const int64_t detached[4] = { hsx + 2, hsy + 6, hsx + 2, hsy + 9 };
    const int64_t gap[4] = { hsx + 2, hsy + 4, hsx + 2, hsy + 5 };
    CHECK_TRUE("a corridor that starts next to claimed floor is reachable", unreachable(NULL, 0, touching) == 0);
    CHECK_TRUE("a corridor two slabs short of it is unreachable, all four slabs", unreachable(NULL, 0, detached) == 4);
    CHECK_TRUE("the same corridor is reachable once the gap is marked too (a batch's earlier order)", unreachable(gap, 1, detached) == 0);
    const int64_t claimed_only[4] = { hsx - 4, hsy + 3, hsx + 4, hsy + 3 };
    CHECK_TRUE("a rectangle of floor takes no mark and is not reported", unreachable(NULL, 0, claimed_only) == 0);
    CHECK_TRUE("nothing is marked yet", view_unreachable_count() == 0);

    struct ExtSeatVerb v = dig(detached[0], detached[1], detached[2], detached[3]);
    CHECK_TRUE("the detached corridor is still accepted: a warning, not a refusal", extseat_submit_verb_ex(U, P, &v, true, NULL) == NULL);
    CHECK_TRUE("while its steps are pending it counts as marked, so a check of the gap sees them joined", unreachable(NULL, 0, gap) == 0);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult dr02_wait_tagged(struct FTestActionArgs* const args)
{
    if (!extseat_idle(U)) return FTRs_Repeat_Current_Action;
    CHECK_TRUE("the detached corridor was tagged", find_from_task_list_by_slab(P, hsx + 2, hsy + 7) != -1);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult dr03_view_lists_it_then_connect(struct FTestActionArgs* const args)
{
    VALUE v; api_seat_build_view(&v, P);
    VALUE* own = value_dict_get(&v, "own");
    CHECK_TRUE("the view counts the four unreachable marks", value_int32(value_dict_get(own, "unreachable_dig_marks_count")) == 4);
    VALUE* list = value_dict_get(own, "unreachable_dig_marks");
    TbBool listed = false;
    for (size_t i = 0; list && i < value_array_size(list); i++) {
        VALUE* e = value_array_get(list, i);
        if (value_int32(value_array_get(e, 0)) == hsx + 2 && value_int32(value_array_get(e, 1)) == hsy + 9) listed = true;
    }
    CHECK_TRUE("and lists the far end of the corridor", listed);
    value_fini(&v);
    struct ExtSeatVerb g = dig(hsx + 2, hsy + 4, hsx + 2, hsy + 5);
    CHECK_TRUE("marking the gap is accepted", extseat_submit_verb_ex(U, P, &g, true, NULL) == NULL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult dr04_wait_tagged_and_check_fixed(struct FTestActionArgs* const args)
{
    if (!extseat_idle(U)) return FTRs_Repeat_Current_Action;
    CHECK_TRUE("with the gap marked, no mark is unreachable any more", view_unreachable_count() == 0);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " dig-reach check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: unreachable dig marks are found, listed in the view, and cleared once connected");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
