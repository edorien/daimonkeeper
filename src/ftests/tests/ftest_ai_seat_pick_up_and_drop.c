// pick_up_and_drop: one verb that lifts one of the seat's creatures with the hand and drops it at a subtile, so an agent
// can move creatures in a single batch (a separate drop is checked against the hand when submitted, so it cannot follow
// its pick_up in the same batch). Checks: the up-front refusals (MISSING_THING, MISSING_POSITION, NOT_YOURS,
// CANNOT_DROP_HERE, HAND_NOT_EMPTY), two creatures moved by one queued batch, and a creature that dies between the order
// and its pick: the drop is skipped (nothing else in the hand is ever dropped) and the verb reports PICK_UP_FAILED.
#include "ftest_ai_seat_pick_up_and_drop.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "config_creature.h"
#include "config_keeperfx.h"
#include "config_players.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_config_state.h"
#include "net_game.h"
#include "player_data.h"
#include "power_hand.h"
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
#define NC 4
static ThingIndex c[NC];
static ThingIndex s_foe = 0;
static MapSubtlCoord hx, hy;
static int64_t s_ids[4];

static struct Thing* T(int i) { return thing_get(c[i]); }
static struct ExtSeatVerb pud(ThingIndex t, int64_t x, int64_t y)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_PickUpAndDrop; v.has_thing = (t != 0); v.thing_id = t; v.has_pos = true; v.stl_x = x; v.stl_y = y;
    return v;
}
static const char* submit(struct ExtSeatVerb v, int64_t* id)
{
    struct ExtSeatSubmitInfo info; memset(&info, 0, sizeof(info));
    const char* e = extseat_submit_verb_ex(U, P, &v, true, &info);
    if (id != NULL) *id = info.id;
    return e;
}
static void expect(const char* what, struct ExtSeatVerb v, const char* code)
{
    const char* e = submit(v, NULL);
    if (e == NULL || strcmp(e, code) != 0) SOFT_FAIL("%s: expected %s, got %s", what, code, e ? e : "success");
}
static const struct ExtSeatResult* result_for(int64_t id)
{
    static struct ExtSeatResult res[EXTSEAT_RESULT_RING];
    const int64_t n = extseat_results(U, res, EXTSEAT_RESULT_RING);
    for (int64_t k = 0; k < n; k++) if (res[k].id == id) return &res[k];
    return NULL;
}
static TbBool near(int i, int64_t x, int64_t y)
{
    const struct Thing* t = T(i);
    const int64_t dx = (int64_t)t->mappos.x.stl.num - x, dy = (int64_t)t->mappos.y.stl.num - y;
    return (dx >= -1) && (dx <= 1) && (dy >= -1) && (dy <= 1);
}
static TbBool hand_empty(void) { return power_hand_is_empty(get_player(P)); }

FTestActionResult pd01_setup_and_refusals(struct FTestActionArgs* const args);
FTestActionResult pd02_wait_hand_full_refusal(struct FTestActionArgs* const args);
FTestActionResult pd03_wait_emptied_then_batch(struct FTestActionArgs* const args);
FTestActionResult pd04_check_batch_then_race(struct FTestActionArgs* const args);
FTestActionResult pd05_check_race(struct FTestActionArgs* const args);
FTestActionResult pd06_check_both_own(struct FTestActionArgs* const args);

void ftest_ai_seat_pick_up_and_drop_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_pick_up_and_drop_init()
{
    s_failures = 0;
    ftest_append_action(pd01_setup_and_refusals, 0, NULL);
    ftest_append_action(pd02_wait_hand_full_refusal, 1, NULL);
    ftest_append_action(pd03_wait_emptied_then_batch, 1, NULL);
    ftest_append_action(pd04_check_batch_then_race, 1, NULL);
    ftest_append_action(pd05_check_race, 1, NULL);
    ftest_append_action(pd06_check_both_own, 1, NULL);
    return true;
}

FTestActionResult pd01_setup_and_refusals(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || (U = net_add_external_seat(P)) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    const struct Thing* heart = find_players_dungeon_heart(P);
    hx = heart->mappos.x.stl.num; hy = heart->mappos.y.stl.num;
    // Claimed floor rows hsy+3..hsy+5, earth on row hsy+7 (a spot the hand refuses).
    ftest_util_replace_slabs(subtile_slab(hx) - 4, subtile_slab(hy) + 3, subtile_slab(hx) + 9, subtile_slab(hy) + 5, SlbT_CLAIMED, P);
    ftest_util_replace_slabs(subtile_slab(hx) - 4, subtile_slab(hy) + 7, subtile_slab(hx) + 9, subtile_slab(hy) + 7, SlbT_EARTH, kfx_config_state.neutral_player_num);
    for (int i = 0; i < NC; i++) {
        struct Thing* t = ftest_util_create_creature(subtile_coord_center(hx - 6 + 3 * i), subtile_coord_center(hy + 10), P, 4, (ThingModel)creature_model_id("ORC"));
        if (thing_is_invalid(t)) { FTEST_FAIL_TEST("no creature %d", i); return FTRs_Go_To_Next_Action; }
        c[i] = t->index;
    }
    struct Thing* foe = ftest_util_create_creature(subtile_coord_center(hx + 25), subtile_coord_center(hy + 10), my_player_number, 1, (ThingModel)creature_model_id("ORC"));
    s_foe = thing_is_invalid(foe) ? 0 : foe->index;

    struct ExtSeatVerb v = pud(0, hx, hy + 13);
    expect("no creature", v, "MISSING_THING");
    v = pud(c[0], hx, hy + 13); v.has_pos = false;
    expect("no drop spot", v, "MISSING_POSITION");
    if (s_foe != 0) expect("someone else's creature", pud(s_foe, hx, hy + 13), "NOT_YOURS");
    if (s_foe != 0) kill_creature(thing_get(s_foe), INVALID_THING, P, CrDed_NoEffects);
    expect("a spot the hand refuses (earth)", pud(c[0], hx, subtile_slab(hy) * 3 + 7 * 3 + 1), "CANNOT_DROP_HERE");
    CHECK_TRUE("refused verbs queued nothing", extseat_idle(U) && hand_empty());

    // Something already in the hand: the drop would release it, not this creature, so the verb is refused.
    struct ExtSeatVerb pick; memset(&pick, 0, sizeof(pick));
    pick.kind = ESV_PickUp; pick.has_thing = true; pick.thing_id = c[3];
    CHECK_TRUE("a plain pick_up is accepted", submit(pick, NULL) == NULL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult pd02_wait_hand_full_refusal(struct FTestActionArgs* const args)
{
    if (!extseat_idle(U) || hand_empty()) return FTRs_Repeat_Current_Action;
    expect("the hand already holds a creature", pud(c[0], hx, hy + 13), "HAND_NOT_EMPTY");
    struct ExtSeatVerb drop; memset(&drop, 0, sizeof(drop));
    drop.kind = ESV_Drop; drop.has_pos = true; drop.stl_x = hx + 6; drop.stl_y = hy + 13;
    CHECK_TRUE("drop it again", submit(drop, NULL) == NULL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult pd03_wait_emptied_then_batch(struct FTestActionArgs* const args)
{
    if (!extseat_idle(U) || !hand_empty()) return FTRs_Repeat_Current_Action;
    // The point of the verb: two creatures moved by one batch, the second queued behind the first.
    CHECK_TRUE("first move accepted", submit(pud(c[0], hx - 3, hy + 13), &s_ids[0]) == NULL);
    CHECK_TRUE("second move queued behind it in the same batch", submit(pud(c[1], hx + 3, hy + 16), &s_ids[1]) == NULL && extseat_queued_verbs(U) == 1);
    return FTRs_Go_To_Next_Action;
}

// A verb's result is recorded on the tick after its last step (when the queue moves on), so wait for it, not for idle.
static TbBool settled(int64_t id, const struct FTestActionArgs* args)
{
    if (extseat_idle(U) && result_for(id) != NULL) return true;
    return (args->intended_start_at_game_turn > 0) && ((int64_t)get_gameturn() > args->intended_start_at_game_turn + 200);
}

FTestActionResult pd04_check_batch_then_race(struct FTestActionArgs* const args)
{
    if (!settled(s_ids[1], args)) return FTRs_Repeat_Current_Action;
    CHECK_TRUE("the first creature was dropped where asked", near(0, hx - 3, hy + 13));
    CHECK_TRUE("the second creature was dropped where asked", near(1, hx + 3, hy + 16));
    CHECK_TRUE("neither is left in the hand", hand_empty() && !thing_is_picked_up_by_player(T(0), P) && !thing_is_picked_up_by_player(T(1), P));
    const struct ExtSeatResult* r0 = result_for(s_ids[0]); const struct ExtSeatResult* r1 = result_for(s_ids[1]);
    CHECK_TRUE("both verbs report done", r0 && !r0->rejected && r1 && !r1->rejected);

    // The race: the creature dies after the order was accepted, before its pick lands.
    CHECK_TRUE("third move accepted", submit(pud(c[2], hx, hy + 13), &s_ids[2]) == NULL);
    kill_creature(T(2), INVALID_THING, P, CrDed_NoEffects);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult pd05_check_race(struct FTestActionArgs* const args)
{
    if (!settled(s_ids[2], args)) return FTRs_Repeat_Current_Action;
    const struct ExtSeatResult* r = result_for(s_ids[2]);
    CHECK_TRUE("the verb whose creature died reports PICK_UP_FAILED", r && r->rejected && strcmp(r->error, "PICK_UP_FAILED") == 0);
    CHECK_TRUE("and the hand is still empty", hand_empty());
    // Several of the seat's own creatures in one trip (thing_ids), all to one spot.
    struct ExtSeatVerb both; memset(&both, 0, sizeof(both));
    both.kind = ESV_PickUpAndDrop; both.has_pos = true; both.stl_x = hx + 6; both.stl_y = hy + 16;
    both.thing_ids[both.thing_count++] = c[0];
    both.thing_ids[both.thing_count++] = c[1];
    CHECK_TRUE("two of the seat's own creatures carried in one order is accepted", submit(both, &s_ids[3]) == NULL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult pd06_check_both_own(struct FTestActionArgs* const args)
{
    if (!settled(s_ids[3], args)) return FTRs_Repeat_Current_Action;
    const struct ExtSeatResult* r = result_for(s_ids[3]);
    CHECK_TRUE("the two-creature order completed", r && !r->rejected);
    CHECK_TRUE("both creatures landed on the spot", near(0, hx + 6, hy + 16) && near(1, hx + 6, hy + 16));
    CHECK_TRUE("and the hand is empty", hand_empty());
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " pick_up_and_drop check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: pick_up_and_drop validates both halves, moves creatures in one batch and several in one trip, and never drops the wrong thing");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
