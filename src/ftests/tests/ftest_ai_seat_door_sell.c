// Doors placed and sold through an External seat, as an agent did live: three doors in a row along a corridor, then a
// sell over those slabs queued straight behind them. The agent reported the doors going back to stock rather than paying
// gold; the engine's door sale pays the door's selling value (DoorSellValuePercent of it) and returns nothing to stock.
// Checked: each placement was accepted when sent and carried out; the sale removed every door, paid exactly their
// selling values, and left stock where the placements put it. And a door where it cannot stand (not between two walls)
// is refused when sent (CANNOT_PLACE_HERE), not dropped silently.
#include "ftest_ai_seat_door_sell.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "config_keeperfx.h"
#include "config_rules.h"
#include "config_trapdoor.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_config_state.h"
#include "net_game.h"
#include "player_data.h"
#include "slab_data.h"
#include "thing_doors.h"
#include "thing_list.h"
#include "thing_stats.h"

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
static ThingModel wood, braced;
static int64_t s_gold_before = 0, s_wood_before = 0, s_braced_before = 0, s_sold_before = 0;
static int64_t s_ids[4];
static int64_t s_idle_since = -1;

#define C_Y (hsy + 7)            /* the corridor's row */
#define C_X0 (hsx - 5)
#define C_X1 (hsx + 5)
#define DOOR_X(i) (hsx - 1 + (i))  /* the three doors: hsx-1, hsx, hsx+1 */

static struct Dungeon* dg(void) { return get_players_dungeon(get_player(P)); }
static struct ExtSeatVerb door(const char* kind, MapSlabCoord sx, MapSlabCoord sy)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_PlaceDoor; snprintf(v.name, sizeof(v.name), "%s", kind);
    v.has_pos = true; v.stl_x = slab_subtile_center(sx); v.stl_y = slab_subtile_center(sy);
    return v;
}
static int64_t submit_queued(const char* what, struct ExtSeatVerb v)
{
    struct ExtSeatSubmitInfo info; memset(&info, 0, sizeof(info));
    const char* e = extseat_submit_verb_ex(U, P, &v, true, &info);
    if (e) { SOFT_FAIL("%s: rejected with %s", what, e); return 0; }
    return info.id;
}
static const char* result_of(int64_t id, TbBool* found)
{
    static struct ExtSeatResult res[EXTSEAT_RESULT_RING];
    const int64_t n = extseat_results(U, res, EXTSEAT_RESULT_RING);
    *found = false;
    for (int64_t i = 0; i < n; i++) if (res[i].id == id) { *found = true; return res[i].rejected ? res[i].error : NULL; }
    return NULL;
}
static int64_t sale_value(ThingModel m) { return compute_value_percentage(get_door_model_stats(m)->selling_value, kfx_config_state.conf.rules[P].gameplay.door_sale_percent); }

FTestActionResult ds01_setup(struct FTestActionArgs* const args);
FTestActionResult ds02_place_and_sell(struct FTestActionArgs* const args);
FTestActionResult ds03_check(struct FTestActionArgs* const args);

void ftest_ai_seat_door_sell_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_door_sell_init()
{
    s_failures = 0; s_idle_since = -1;
    ftest_append_action(ds01_setup, 0, NULL);
    ftest_append_action(ds02_place_and_sell, 2, NULL);
    ftest_append_action(ds03_check, 1, NULL);
    return true;
}

FTestActionResult ds01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0) { FTEST_FAIL_TEST("no second keeper"); return FTRs_Go_To_Next_Action; }
    U = net_add_external_seat(P);
    if (U < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    const struct Thing* heart = find_players_dungeon_heart(P);
    hsx = subtile_slab(heart->mappos.x.stl.num); hsy = subtile_slab(heart->mappos.y.stl.num);
    wood = (ThingModel)door_model_id("WOOD");
    braced = (ThingModel)door_model_id("BRACED");
    dg()->total_money_owned = 50000;
    kfx_config_state.pay_day_progress[P] = 0;
    set_door_buildable_and_add_to_amount(P, wood, 1, 3);
    set_door_buildable_and_add_to_amount(P, braced, 1, 1);
    // A corridor one slab wide, rock either side; and an open patch past its east end, where a door has no walls.
    ftest_util_replace_slabs(C_X0, C_Y - 1, C_X1, C_Y + 1, SlbT_ROCK, PLAYER_NEUTRAL);
    ftest_util_replace_slabs(C_X0, C_Y, C_X1, C_Y, SlbT_CLAIMED, P);
    ftest_util_replace_slabs(C_X1 + 1, C_Y - 1, C_X1 + 3, C_Y + 1, SlbT_CLAIMED, P);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ds02_place_and_sell(struct FTestActionArgs* const args)
{
    struct ExtSeatVerb open = door("WOOD", C_X1 + 2, C_Y);
    const char* e = extseat_submit_verb_ex(U, P, &open, true, NULL);
    if ((e == NULL) || (strcmp(e, "CANNOT_PLACE_HERE") != 0)) SOFT_FAIL("a door with no walls beside it: expected CANNOT_PLACE_HERE, got %s", e ? e : "success");
    s_gold_before = dg()->total_money_owned;
    s_wood_before = dg()->mnfct_info.door_amount_placeable[wood];
    s_braced_before = dg()->mnfct_info.door_amount_placeable[braced];
    s_sold_before = dg()->doors_sold;
    s_ids[0] = submit_queued("BRACED door", door("BRACED", DOOR_X(0), C_Y));
    s_ids[1] = submit_queued("first WOOD door", door("WOOD", DOOR_X(1), C_Y));
    s_ids[2] = submit_queued("second WOOD door", door("WOOD", DOOR_X(2), C_Y));
    struct ExtSeatVerb sell; memset(&sell, 0, sizeof(sell));
    sell.kind = ESV_Sell; sell.has_rect = true;
    sell.slab_x0 = DOOR_X(0); sell.slab_y0 = C_Y; sell.slab_x1 = DOOR_X(2); sell.slab_y1 = C_Y;
    s_ids[3] = submit_queued("sell over the doors", sell);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ds03_check(struct FTestActionArgs* const args)
{
    if (!extseat_idle(U)) { s_idle_since = -1; return FTRs_Repeat_Current_Action; }
    if (s_idle_since < 0) s_idle_since = (int64_t)get_gameturn();
    if ((int64_t)get_gameturn() < s_idle_since + 8) return FTRs_Repeat_Current_Action;
    const char* what[] = { "BRACED door", "first WOOD door", "second WOOD door", "sell" };
    for (int i = 0; i < 4; i++) {
        TbBool found;
        const char* err = result_of(s_ids[i], &found);
        if (!found) SOFT_FAIL("%s: no result recorded", what[i]);
        else if (err) SOFT_FAIL("%s: failed when it ran: %s", what[i], err);
    }
    const struct Dungeon* d = dg();
    int64_t left = 0;
    for (int i = 0; i < 3; i++) if (slab_has_door_thing_on(DOOR_X(i), C_Y)) left++;
    const int64_t paid = d->total_money_owned - s_gold_before;
    const int64_t expect = sale_value(braced) + 2 * sale_value(wood);
    FTESTLOG("after the sale: %" PRId64 " door(s) left, gold %+" PRId64 " (expected %+" PRId64 "), stock WOOD %" PRId64 " -> %" PRId64 ", BRACED %" PRId64 " -> %" PRId64 ", doors sold %+" PRId64,
             left, paid, expect, s_wood_before, (int64_t)d->mnfct_info.door_amount_placeable[wood], s_braced_before,
             (int64_t)d->mnfct_info.door_amount_placeable[braced], (int64_t)d->doors_sold - s_sold_before);
    CHECK_TRUE("the sale removed all three doors", left == 0);
    CHECK_TRUE("it paid their selling values in gold", paid == expect);
    CHECK_TRUE("and put nothing back in stock", (d->mnfct_info.door_amount_placeable[wood] == s_wood_before - 2)
               && (d->mnfct_info.door_amount_placeable[braced] == s_braced_before - 1));
    CHECK_TRUE("the game counts three doors sold", d->doors_sold - s_sold_before == 3);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " door check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: doors placed in a row and sold through the seat pay gold, and a door with no walls is refused when sent");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
