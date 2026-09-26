// Real-time cadence of docs/refactor/AI/LLM (M6): an agent that thinks while the game runs sends a *batch* of orders and
// they run one after another (submit_action queue=true), each re-validated against the live state when it starts, so
// an order that went stale while it waited is dropped and reported instead of applied blindly.
//
// Checks: a room build followed at once by a dig mark and a sell all take full effect (the following verb's first packet
// does not truncate the room the engine is still building); the results list them in order as done; a verb queued while
// one is running is still refused without queue=true; two place_trap orders with stock for one -> the second is rejected
// NOT_AVAILABLE when it starts; an order whose expiry passed while it waited is EXPIRED; one submitted already stale is
// refused STALE_VIEW; the queue holds EXTSEAT_MAX_QUEUED_VERBS then says QUEUE_FULL; cancel drops the lot and reports
// CANCELLED; the view carries the pay day fields.
#include "ftest_ai_seat_queue.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>
#include <stdlib.h>
#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api_seat_view.h"
#include "frontend.h"
#include "game_legacy.h"
#include "config_keeperfx.h"
#include "config_players.h"
#include "config_terrain.h"
#include "config_trapdoor.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "map_data.h"
#include "net_game.h"
#include "player_data.h"
#include "player_instances.h"
#include "room_data.h"
#include "slab_data.h"
#include "tasks_list.h"
#include "thing_list.h"
#include "thing_traps.h"

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
static RoomKind treasure;
static ThingModel trap_model;
static int64_t id_room, id_dig, id_sell, id_t1, id_t2, id_exp;

#define R_X0 (hsx + 3)
#define R_Y0 (hsy - 1)
#define R_W 4
#define R_H 3
#define D_X0 (hsx - 9)
#define D_Y0 (hsy - 1)
#define D_W 4
#define D_H 3
#define S_Y (hsy + 7)
#define T_Y (hsy + 9)

static TbBool tagged(MapSlabCoord sx, MapSlabCoord sy) { return find_from_task_list(P, get_subtile_number(slab_subtile_center(sx), slab_subtile_center(sy))) != -1; }
static TbBool trap_at(MapSlabCoord sx, MapSlabCoord sy) { return !thing_is_invalid(get_trap_for_position(slab_subtile_center(sx), slab_subtile_center(sy))); }
static int64_t room_slabs(void)
{
    int64_t n = 0;
    for (int dx = 0; dx < R_W; dx++) for (int dy = 0; dy < R_H; dy++)
    {
        const struct SlabMap* slb = get_slabmap_block(R_X0 + dx, R_Y0 + dy);
        if (slb->room_index > 0 && room_get(slb->room_index)->kind == treasure && slabmap_owner(slb) == P) n++;
    }
    return n;
}
static struct ExtSeatVerb V(enum ExtSeatVerbKind k, const char* name)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v)); v.kind = k;
    if (name) snprintf(v.name, sizeof(v.name), "%s", name);
    return v;
}
static struct ExtSeatVerb rect(struct ExtSeatVerb v, int64_t x0, int64_t y0, int64_t x1, int64_t y1)
{
    v.has_rect = true; v.slab_x0 = x0; v.slab_y0 = y0; v.slab_x1 = x1; v.slab_y1 = y1; return v;
}
static struct ExtSeatVerb at(struct ExtSeatVerb v, int64_t stl_x, int64_t stl_y) { v.has_pos = true; v.stl_x = stl_x; v.stl_y = stl_y; return v; }

// Submits with queue=true; returns the id (0 and a logged failure on refusal).
static int64_t submit_q(const char* what, struct ExtSeatVerb v)
{
    struct ExtSeatSubmitInfo info = {0};
    const char* e = extseat_submit_verb_ex(U, P, &v, true, &info);
    if (e) { SOFT_FAIL("%s: rejected with %s", what, e); return 0; }
    return info.id;
}
static const char* status_of(int64_t id)
{
    static char buf[40];
    struct ExtSeatResult res[EXTSEAT_RESULT_RING];
    const int64_t n = extseat_results(U, res, EXTSEAT_RESULT_RING);
    for (int64_t i = 0; i < n; i++)
        if (res[i].id == id) { snprintf(buf, sizeof(buf), "%s", res[i].rejected ? res[i].error : "done"); return buf; }
    return "none";
}
static int64_t s_idle_since = -1;
static FTestActionResult wait_settled(int64_t settle)
{
    if (!extseat_idle(U)) { s_idle_since = -1; return FTRs_Repeat_Current_Action; }
    if (s_idle_since < 0) s_idle_since = (int64_t)get_gameturn();
    if ((int64_t)get_gameturn() < s_idle_since + settle) return FTRs_Repeat_Current_Action;
    s_idle_since = -1;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult q01_setup(struct FTestActionArgs* const args);
FTestActionResult q02_batch(struct FTestActionArgs* const args);
FTestActionResult q03_batch_wait(struct FTestActionArgs* const args);
FTestActionResult q04_batch_check_stale(struct FTestActionArgs* const args);
FTestActionResult q05_stale_wait(struct FTestActionArgs* const args);
FTestActionResult q06_stale_check_full(struct FTestActionArgs* const args);
FTestActionResult q07_cancel_check_payday(struct FTestActionArgs* const args);

void ftest_ai_seat_queue_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_queue_init()
{
    s_failures = 0; s_idle_since = -1;
    ftest_append_action(q01_setup, 0, NULL);
    ftest_append_action(q02_batch, 2, NULL);
    ftest_append_action(q03_batch_wait, 1, NULL);
    ftest_append_action(q04_batch_check_stale, 1, NULL);
    ftest_append_action(q05_stale_wait, 1, NULL);
    ftest_append_action(q06_stale_check_full, 1, NULL);
    ftest_append_action(q07_cancel_check_payday, 2, NULL);
    return true;
}

FTestActionResult q01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0) { FTEST_FAIL_TEST("no second keeper"); return FTRs_Go_To_Next_Action; }
    U = net_add_external_seat(P);
    if (U < 1) { FTEST_FAIL_TEST("net_add_external_seat failed"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    const struct Thing* heart = find_players_dungeon_heart(P);
    hsx = subtile_slab(heart->mappos.x.stl.num); hsy = subtile_slab(heart->mappos.y.stl.num);
    treasure = (RoomKind)get_rid(room_desc, "TREASURE");
    trap_model = (ThingModel)trap_model_id("BOULDER");
    struct Dungeon* d = get_players_dungeon(get_player(P));
    d->total_money_owned = 100000;
    d->room_buildable[treasure] |= 1;
    set_trap_buildable_and_add_to_amount(P, trap_model, 1, 1); // stock for ONE trap
    ftest_util_replace_slabs(R_X0 - 1, R_Y0 - 1, R_X0 + R_W, R_Y0 + R_H, SlbT_CLAIMED, P);
    ftest_util_replace_slabs(D_X0, D_Y0, D_X0 + D_W - 1, D_Y0 + D_H - 1, SlbT_EARTH, PLAYER_NEUTRAL);
    ftest_util_replace_slabs(hsx - 2, S_Y, hsx + 6, S_Y, SlbT_CLAIMED, P);
    ftest_util_replace_slabs(hsx - 2, T_Y, hsx + 6, T_Y, SlbT_CLAIMED, P);
    player_place_trap_at_subtile_without_check(slab_subtile_center(hsx), slab_subtile_center(S_Y), P, trap_model, true);
    CHECK_TRUE("the trap to sell is placed", trap_at(hsx, S_Y));
    return FTRs_Go_To_Next_Action;
}

FTestActionResult q02_batch(struct FTestActionArgs* const args)
{
    // Without queue=true a second verb is still refused while one runs (unchanged behaviour).
    struct ExtSeatVerb b = rect(V(ESV_BuildRoom, "TREASURE"), R_X0, R_Y0, R_X0 + R_W - 1, R_Y0 + R_H - 1);
    struct ExtSeatSubmitInfo info = {0};
    CHECK_TRUE("the first verb starts at once", extseat_submit_verb_ex(U, P, &b, false, &info) == NULL && info.queued_behind == 0);
    id_room = info.id;
    struct ExtSeatVerb d = rect(V(ESV_MarkDig, NULL), D_X0, D_Y0, D_X0 + D_W - 1, D_Y0 + D_H - 1);
    const char* e = extseat_submit_verb_ex(U, P, &d, false, NULL);
    CHECK_TRUE("without queue=true a busy seat still refuses", e && strcmp(e, "ACTION_ALREADY_QUEUED") == 0);

    id_dig = submit_q("queued mark_dig", d);
    id_sell = submit_q("queued sell", rect(V(ESV_Sell, NULL), hsx, S_Y, hsx, S_Y));
    CHECK_TRUE("two verbs are waiting", extseat_queued_verbs(U) == 2);
    CHECK_TRUE("ids grow", id_room > 0 && id_dig > id_room && id_sell > id_dig);
    // Stale-at-start: two place_trap orders with stock for one, and one with an expiry that will have passed.
    const int64_t x = slab_subtile_center(hsx + 2), y = slab_subtile_center(T_Y);
    id_t1 = submit_q("place_trap 1", at(V(ESV_PlaceTrap, "BOULDER"), x, y));
    id_t2 = submit_q("place_trap 2 (no stock left by then)", at(V(ESV_PlaceTrap, "BOULDER"), x + 6, y));
    struct ExtSeatVerb ex = at(V(ESV_PlaceTrap, "BOULDER"), x + 12, y);
    ex.expires_turn = (int64_t)get_gameturn() + 2; // cannot start for far longer than that
    id_exp = submit_q("place_trap with an expiry", ex);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult q03_batch_wait(struct FTestActionArgs* const args) { return wait_settled(30); }

FTestActionResult q04_batch_check_stale(struct FTestActionArgs* const args)
{
    const int64_t have = room_slabs();
    if (have != R_W * R_H) SOFT_FAIL("the room built %" PRId64 " of %d slabs when a verb followed it at once", have, R_W * R_H);
    int64_t tags = 0;
    for (int dx = 0; dx < D_W; dx++) for (int dy = 0; dy < D_H; dy++) if (tagged(D_X0 + dx, D_Y0 + dy)) tags++;
    if (tags != D_W * D_H) SOFT_FAIL("the queued mark_dig tagged %" PRId64 " of %d", tags, D_W * D_H);
    CHECK_TRUE("the queued sell sold the trap", !trap_at(hsx, S_Y));
    CHECK_TRUE("the first place_trap took effect", trap_at(hsx + 2, T_Y));
    CHECK_TRUE("the second place_trap (no stock) did not", !trap_at(hsx + 4, T_Y));
    CHECK_TRUE("the expired place_trap did not", !trap_at(hsx + 6, T_Y));
    CHECK_TRUE("results: build_room done", strcmp(status_of(id_room), "done") == 0);
    CHECK_TRUE("results: mark_dig done", strcmp(status_of(id_dig), "done") == 0);
    CHECK_TRUE("results: sell done", strcmp(status_of(id_sell), "done") == 0);
    CHECK_TRUE("results: place_trap 1 done", strcmp(status_of(id_t1), "done") == 0);
    FTESTLOG("status of place_trap 2: %s; trap 1 at %d, x+3 at %d", status_of(id_t2), (int)trap_at(hsx + 2, T_Y), (int)trap_at(hsx + 4, T_Y));
    CHECK_TRUE("results: place_trap 2 rejected NOT_AVAILABLE", strcmp(status_of(id_t2), "NOT_AVAILABLE") == 0);
    CHECK_TRUE("results: expired order EXPIRED", strcmp(status_of(id_exp), "EXPIRED") == 0);

    // Already stale at submit.
    struct ExtSeatVerb st = at(V(ESV_PlaceTrap, "BOULDER"), slab_subtile_center(hsx), slab_subtile_center(S_Y));
    st.expires_turn = (int64_t)get_gameturn() - 1;
    const char* e = extseat_submit_verb_ex(U, P, &st, true, NULL);
    CHECK_TRUE("an order already past its expiry is refused STALE_VIEW", e && strcmp(e, "STALE_VIEW") == 0);

    // Fill the queue: one running plus EXTSEAT_MAX_QUEUED_VERBS waiting, then QUEUE_FULL.
    struct ExtSeatVerb dig = rect(V(ESV_MarkDig, NULL), D_X0, D_Y0, D_X0, D_Y0);
    struct ExtSeatSubmitInfo info = {0};
    CHECK_TRUE("the running verb is accepted", extseat_submit_verb_ex(U, P, &dig, true, &info) == NULL);
    for (int i = 0; i < EXTSEAT_MAX_QUEUED_VERBS; i++)
        if (extseat_submit_verb_ex(U, P, &dig, true, &info) != NULL) { SOFT_FAIL("waiting verb %d refused", i); break; }
    CHECK_TRUE("the queue is full", extseat_queued_verbs(U) == EXTSEAT_MAX_QUEUED_VERBS);
    e = extseat_submit_verb_ex(U, P, &dig, true, NULL);
    CHECK_TRUE("one more is QUEUE_FULL", e && strcmp(e, "QUEUE_FULL") == 0);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult q05_stale_wait(struct FTestActionArgs* const args)
{
    // Let a couple of the queued verbs run, then cancel with the rest still waiting.
    return (extseat_queued_verbs(U) > EXTSEAT_MAX_QUEUED_VERBS - 3) ? FTRs_Repeat_Current_Action : FTRs_Go_To_Next_Action;
}

FTestActionResult q06_stale_check_full(struct FTestActionArgs* const args)
{
    const int64_t waiting = extseat_queued_verbs(U);
    CHECK_TRUE("some verbs are still waiting", waiting > 0);
    struct ExtSeatVerb c = V(ESV_Cancel, NULL);
    struct ExtSeatSubmitInfo info = {0};
    CHECK_TRUE("cancel is accepted", extseat_submit_verb_ex(U, P, &c, true, &info) == NULL);
    CHECK_TRUE("cancel counts the waiting verbs it dropped", info.steps >= waiting);
    CHECK_TRUE("the queue is empty after cancel", extseat_queued_verbs(U) == 0);
    struct ExtSeatResult res[EXTSEAT_RESULT_RING];
    const int64_t n = extseat_results(U, res, EXTSEAT_RESULT_RING);
    int64_t cancelled = 0;
    for (int64_t i = 0; i < n; i++) if (res[i].rejected && strcmp(res[i].error, "CANCELLED") == 0) cancelled++;
    CHECK_TRUE("the dropped verbs are reported CANCELLED", cancelled >= 1);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult q07_cancel_check_payday(struct FTestActionArgs* const args)
{
    CHECK_TRUE("the seat is idle again", extseat_idle(U) || extseat_queued_steps(U) > 0);
    VALUE view; api_seat_build_view(&view, P);
    VALUE* seat = value_dict_get(&view, "seat");
    VALUE* pd = seat ? value_dict_get(seat, "payday") : NULL;
    CHECK_TRUE("the view has seat.queued_verbs", seat && value_dict_get(seat, "queued_verbs") != NULL);
    CHECK_TRUE("the view has seat.results", seat && value_dict_get(seat, "results") != NULL);
    CHECK_TRUE("the view has seat.payday", pd != NULL);
    if (pd)
    {
        const int64_t gap = value_int64(value_dict_get(pd, "gap"));
        const int64_t progress = value_int64(value_dict_get(pd, "progress"));
        const int32_t quarter = value_int32(value_dict_get(pd, "quarter"));
        FTESTLOG("payday: progress %" PRId64 " of %" PRId64 ", quarter %d", progress, gap, (int)quarter);
        CHECK_TRUE("pay day gap is positive", gap > 0);
        CHECK_TRUE("quarter matches progress", quarter == (int32_t)((4 * progress / gap) % 4));
        CHECK_TRUE("turns_to_payday is reported", value_dict_get(pd, "turns_to_payday") != NULL);
        CHECK_TRUE("turns_to_next_quarter is within a quarter", value_int64(value_dict_get(pd, "turns_to_next_quarter")) <= gap / 4 + 1);
    }
    value_fini(&view);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " queue check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: queued verbs run in order, stale ones are dropped and reported, cancel clears the queue");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
