// M6c: move_creature / release_creature through the seat driver (the LLM-friendly alternative to possession: no live view
// needed). Two creatures are ordered to different subtiles as one batch (queue=true); both must arrive and hold there;
// releasing them returns them to their own AI; the validation errors are pinned. Builds on the facts recorded by
// ai_gesture_order_creature.
#include "ftest_ai_seat_order_creature.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "config_creature.h"
#include "config_crtrstates.h"
#include "config_keeperfx.h"
#include "config_players.h"
#include "creature_control.h"
#include "creature_states.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "net_game.h"
#include "player_data.h"
#include "slab_data.h"
#include "thing_creature.h"
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
static ThingIndex a = 0, b = 0, foe = 0;
static MapSubtlCoord ax, ay, bx, by, hx, hy;

static const struct Thing* T(ThingIndex i) { return thing_get(i); }
static struct ExtSeatVerb mv(ThingIndex t, int64_t x, int64_t y, enum ExtSeatVerbKind k)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = k; v.has_thing = true; v.thing_id = t;
    if (k == ESV_MoveCreature) { v.has_pos = true; v.stl_x = x; v.stl_y = y; }
    return v;
}
static void expect_err(const char* what, struct ExtSeatVerb v, const char* code)
{
    const char* e = extseat_submit_verb_ex(U, P, &v, true, NULL);
    if (e == NULL || strcmp(e, code) != 0) SOFT_FAIL("%s: expected %s, got %s", what, code, e ? e : "success");
}
static TbBool at(ThingIndex t, MapSubtlCoord x, MapSubtlCoord y) { const struct Thing* th = T(t); return th->mappos.x.stl.num == x && th->mappos.y.stl.num == y; }
static TbBool manual(ThingIndex t) { const struct Thing* th = T(t); return th->active_state == CrSt_ManualControl || th->continue_state == CrSt_ManualControl; }

FTestActionResult m01_setup(struct FTestActionArgs* const args);
FTestActionResult m02_validation_and_batch(struct FTestActionArgs* const args);
FTestActionResult m03_walk(struct FTestActionArgs* const args);
FTestActionResult m04_check_held_release(struct FTestActionArgs* const args);
FTestActionResult m05_release_wait(struct FTestActionArgs* const args);
FTestActionResult m06_check_released(struct FTestActionArgs* const args);

void ftest_ai_seat_order_creature_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_order_creature_init()
{
    s_failures = 0;
    ftest_append_action(m01_setup, 0, NULL);
    ftest_append_action(m02_validation_and_batch, 2, NULL);
    ftest_append_action(m03_walk, 1, NULL);
    ftest_append_action(m04_check_held_release, 1, NULL);
    ftest_append_action(m05_release_wait, 1, NULL);
    ftest_append_action(m06_check_released, 1, NULL);
    return true;
}

FTestActionResult m01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || (U = net_add_external_seat(P)) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    const struct Thing* heart = find_players_dungeon_heart(P);
    hx = heart->mappos.x.stl.num; hy = heart->mappos.y.stl.num;
    ftest_util_replace_slabs(subtile_slab(hx) - 4, subtile_slab(hy) + 3, subtile_slab(hx) + 9, subtile_slab(hy) + 5, SlbT_CLAIMED, P);
    struct Thing* ca = ftest_util_create_creature(subtile_coord_center(hx - 3), subtile_coord_center(hy + 10), P, 9, (ThingModel)creature_model_id("ORC"));
    struct Thing* cb = ftest_util_create_creature(subtile_coord_center(hx - 6), subtile_coord_center(hy + 10), P, 9, (ThingModel)creature_model_id("TROLL"));
    struct Thing* cf = ftest_util_create_creature(subtile_coord_center(hx + 20), subtile_coord_center(hy + 10), my_player_number, 9, (ThingModel)creature_model_id("ORC"));
    if (thing_is_invalid(ca) || thing_is_invalid(cb) || thing_is_invalid(cf)) { FTEST_FAIL_TEST("no creatures"); return FTRs_Go_To_Next_Action; }
    a = ca->index; b = cb->index; foe = cf->index;
    ax = hx + 6; ay = hy + 10; bx = hx + 3; by = hy + 10;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult m02_validation_and_batch(struct FTestActionArgs* const args)
{
    struct ExtSeatVerb v;
    v = mv(999999, ax, ay, ESV_MoveCreature);             expect_err("a thing that is not there", v, "NO_SUCH_THING");
    v = mv(foe, ax, ay, ESV_MoveCreature);                expect_err("someone else's creature", v, "NOT_YOURS");
    // Only needed for that check: left alive, it walks over and attacks the held creatures, which hands them back (attacked).
    kill_creature(thing_get(foe), INVALID_THING, P, CrDed_NoEffects);
    v = mv(a, ax, ay, ESV_MoveCreature); v.has_pos = false; expect_err("no target", v, "MISSING_POSITION");
    v = mv(a, 9999, 9999, ESV_MoveCreature);              expect_err("off the map", v, "POSITION_OFF_MAP");
    v = mv(a, T(a)->mappos.x.stl.num, T(a)->mappos.y.stl.num, ESV_MoveCreature); expect_err("its own subtile is a release, not a move", v, "ALREADY_THERE");
    v = mv(a, hx - 30, hy - 30, ESV_MoveCreature);        expect_err("into solid rock", v, "CANNOT_REACH");
    v = mv(999999, 0, 0, ESV_ReleaseCreature);            expect_err("release of a thing that is not there", v, "NO_SUCH_THING");
    if (!extseat_idle(U)) SOFT_FAIL("a refused verb left something queued");

    // The batch: two creatures, two destinations, one after the other.
    struct ExtSeatVerb m1 = mv(a, ax, ay, ESV_MoveCreature), m2 = mv(b, bx, by, ESV_MoveCreature);
    CHECK_TRUE("first order starts now", extseat_submit_verb_ex(U, P, &m1, true, NULL) == NULL);
    CHECK_TRUE("second order waits behind it", extseat_submit_verb_ex(U, P, &m2, true, NULL) == NULL && extseat_queued_verbs(U) == 1);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult m03_walk(struct FTestActionArgs* const args)
{
    if (at(a, ax, ay) && at(b, bx, by)) return FTRs_Go_To_Next_Action;
    if (args->intended_start_at_game_turn > 0 && (int64_t)get_gameturn() > args->intended_start_at_game_turn + 600) return FTRs_Go_To_Next_Action;
    return FTRs_Repeat_Current_Action;
}

FTestActionResult m04_check_held_release(struct FTestActionArgs* const args)
{
    FTESTLOG("after the walk: orc at (%" PRId64 ",%" PRId64 ") wants (%" PRId64 ",%" PRId64 "), troll at (%" PRId64 ",%" PRId64 ") wants (%" PRId64 ",%" PRId64 ")",
        (int64_t)T(a)->mappos.x.stl.num, (int64_t)T(a)->mappos.y.stl.num, (int64_t)ax, (int64_t)ay, (int64_t)T(b)->mappos.x.stl.num, (int64_t)T(b)->mappos.y.stl.num, (int64_t)bx, (int64_t)by);
    CHECK_TRUE("the first creature arrived at its subtile", at(a, ax, ay));
    CHECK_TRUE("the second creature arrived at its subtile (batch: the first order's selection did not capture the second)", at(b, bx, by));
    CHECK_TRUE("both hold under manual control", manual(a) && manual(b));
    CHECK_TRUE("the seat is left out of the order mode", get_player(P)->work_state != PSt_OrderCreatr && get_player(P)->controlled_thing_idx == 0);
    struct ExtSeatVerb r1 = mv(a, 0, 0, ESV_ReleaseCreature), r2 = mv(b, 0, 0, ESV_ReleaseCreature);
    CHECK_TRUE("release 1 accepted", extseat_submit_verb_ex(U, P, &r1, true, NULL) == NULL);
    CHECK_TRUE("release 2 queued behind it", extseat_submit_verb_ex(U, P, &r2, true, NULL) == NULL);
    return FTRs_Go_To_Next_Action;
}
FTestActionResult m05_release_wait(struct FTestActionArgs* const args)
{
    if (!extseat_idle(U)) return FTRs_Repeat_Current_Action;
    return (args->intended_start_at_game_turn > 0 && (int64_t)get_gameturn() < args->intended_start_at_game_turn + 60) ? FTRs_Repeat_Current_Action : FTRs_Go_To_Next_Action;
}

FTestActionResult m06_check_released(struct FTestActionArgs* const args)
{
    CHECK_TRUE("the first creature is back under its own AI", !manual(a));
    CHECK_TRUE("the second creature is back under its own AI", !manual(b));
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " order-creature check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: move_creature/release_creature as a batch, with validation");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
