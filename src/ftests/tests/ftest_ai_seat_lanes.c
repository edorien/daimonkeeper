// The seat's two lanes (docs/plans/llm-bridge-realtime.md, Phase 3): an urgent verb (priority "now") runs ahead of the
// routine ones, and a long routine verb that can be cut is cut for it -- here a two-drag room build, cut while the
// engine lays its first drag -- then planned again and finished. Checks: the urgent pick_up_and_drop starts within 8
// turns of being sent; the build reports one cut (suspended) and still lays every slab; the results say which lane each
// came from. Then the combat assistant's banks: a rule set stored in bank 1 leaves the live set alone, and one urgent
// packet (use_assist_bank) makes it live. Last, find_drop_spot gives a spot the hand can drop on next to the heart, and the
// view shows the queue (seat.queue).
#include "ftest_ai_seat_lanes.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>
#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api_seat_view.h"
#include "config_creature.h"
#include "config_keeperfx.h"
#include "config_magic.h"
#include "config_terrain.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "keeper_assist_state.h"
#include "kfx_sim_state.h"
#include "net_game.h"
#include "player_data.h"
#include "room_data.h"
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
static NetUserId U = -1;
static MapSlabCoord hsx, hsy;
static RoomKind treasure;
static ThingIndex s_imp = 0;
static int64_t id_room = 0, id_lift = 0, lift_sent = 0, id_bank = 0, id_use = 0;

#define R_X0 (hsx + 3)
#define R_Y0 (hsy - 1)
#define R_W 12       /* two drags: 9 slabs a side at most */
#define R_H 4

static int64_t room_slabs(void)
{
    int64_t n = 0;
    for (int dx = 0; dx < R_W; dx++) for (int dy = 0; dy < R_H; dy++) {
        const struct SlabMap* slb = get_slabmap_block(R_X0 + dx, R_Y0 + dy);
        if (slb->room_index > 0 && room_get(slb->room_index)->kind == treasure && slabmap_owner(slb) == P) n++;
    }
    return n;
}
static const struct ExtSeatResult* result_of(int64_t id)
{
    static struct ExtSeatResult res[EXTSEAT_RESULT_RING];
    const int64_t n = extseat_results(U, res, EXTSEAT_RESULT_RING);
    for (int64_t i = 0; i < n; i++) if (res[i].id == id) return &res[i];
    return NULL;
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

FTestActionResult ln01_setup(struct FTestActionArgs* const args);
FTestActionResult ln02_build(struct FTestActionArgs* const args);
FTestActionResult ln03_urgent_lift(struct FTestActionArgs* const args);
FTestActionResult ln04_wait(struct FTestActionArgs* const args);
FTestActionResult ln05_check_and_bank(struct FTestActionArgs* const args);
FTestActionResult ln06_wait(struct FTestActionArgs* const args);
FTestActionResult ln07_use_bank(struct FTestActionArgs* const args);
FTestActionResult ln08_wait(struct FTestActionArgs* const args);
FTestActionResult ln09_check(struct FTestActionArgs* const args);

void ftest_ai_seat_lanes_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_lanes_init()
{
    s_failures = 0; s_idle_since = -1;
    ftest_append_action(ln01_setup, 0, NULL);
    ftest_append_action(ln02_build, 2, NULL);
    ftest_append_action(ln03_urgent_lift, 14, NULL);
    ftest_append_action(ln04_wait, 1, NULL);
    ftest_append_action(ln05_check_and_bank, 1, NULL);
    ftest_append_action(ln06_wait, 1, NULL);
    ftest_append_action(ln07_use_bank, 1, NULL);
    ftest_append_action(ln08_wait, 1, NULL);
    ftest_append_action(ln09_check, 1, NULL);
    return true;
}

FTestActionResult ln01_setup(struct FTestActionArgs* const args)
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
    struct Dungeon* d = get_players_dungeon(get_player(P));
    d->total_money_owned = 100000;
    d->room_buildable[treasure] |= 1;
    d->magic_level[power_model_id("POWER_HEAL_CREATURE")] = 8;
    ftest_util_replace_slabs(R_X0 - 1, R_Y0 - 1, R_X0 + R_W, R_Y0 + R_H, SlbT_CLAIMED, P);
    struct Coord3d pos;
    set_coords_to_slab_center(&pos, R_X0 - 1, R_Y0 + 1);   // on the claimed border, beside the room
    struct Thing* imp = ftest_util_create_creature(pos.x.val, pos.y.val, P, 1, (ThingModel)creature_model_id("IMP"));
    if (thing_is_invalid(imp)) { FTEST_FAIL_TEST("no imp"); return FTRs_Go_To_Next_Action; }
    s_imp = imp->index;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ln02_build(struct FTestActionArgs* const args)
{
    struct ExtSeatVerb b; memset(&b, 0, sizeof(b));
    b.kind = ESV_BuildRoom; snprintf(b.name, sizeof(b.name), "TREASURE");
    b.has_rect = true; b.slab_x0 = R_X0; b.slab_y0 = R_Y0; b.slab_x1 = R_X0 + R_W - 1; b.slab_y1 = R_Y0 + R_H - 1;
    struct ExtSeatSubmitInfo info = {0};
    const char* e = extseat_submit_verb_ex(U, P, &b, true, &info);
    if (e) { SOFT_FAIL("build_room rejected with %s", e); return FTRs_Go_To_Next_Action; }
    id_room = info.id;
    FTESTLOG("build_room #%d: %d steps", (int)id_room, (int)info.steps);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ln03_urgent_lift(struct FTestActionArgs* const args)
{
    // The first drag is being laid now (one slab a turn): an urgent hand order comes in.
    struct ExtSeatQueueInfo qi;
    extseat_queue_info(U, &qi);
    FTESTLOG("before the urgent order: running #%d (%d steps left), building %d", (int)qi.running_id, (int)qi.running_steps_left,
        (int)get_player(P)->roomspace.is_active);
    CHECK_TRUE("the build is still running", qi.running_id == id_room);
    const struct Room* hroom = room_get(get_slabmap_block(hsx, hsy)->room_index);
    struct ExtSeatVerb l; memset(&l, 0, sizeof(l));
    l.kind = ESV_PickUpAndDrop;
    l.has_thing = true; l.thing_id = s_imp;
    l.has_room = true; l.room_id = hroom->index;
    l.priority = true;
    struct ExtSeatSubmitInfo info = {0};
    const char* e = extseat_submit_verb_ex(U, P, &l, false, &info);
    if (e) { SOFT_FAIL("urgent pick_up_and_drop rejected with %s", e); return FTRs_Go_To_Next_Action; }
    id_lift = info.id;
    lift_sent = (int64_t)get_gameturn();
    CHECK_TRUE("nothing is ahead of the urgent order (the build will be cut)", info.queued_behind == 0);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ln04_wait(struct FTestActionArgs* const args)
{
    if (((int64_t)get_gameturn() - lift_sent > 900)) { SOFT_FAIL("the queue did not settle"); return FTRs_Go_To_Next_Action; }
    return wait_settled(90);   // the last drag is laid one slab a turn after its last step
}

FTestActionResult ln05_check_and_bank(struct FTestActionArgs* const args)
{
    const struct ExtSeatResult* lift = result_of(id_lift);
    const struct ExtSeatResult* room = result_of(id_room);
    if ((lift == NULL) || (room == NULL)) { SOFT_FAIL("missing results (lift %p, room %p)", (const void*)lift, (const void*)room); }
    else {
        FTESTLOG("lift #%d: %s, sent %d, started %d (%d turns), lane %d | room #%d: %s, cut %d time(s), done turn %d, slabs %d of %d",
            (int)id_lift, lift->rejected ? lift->error : "done", (int)lift->submitted, (int)lift->started,
            (int)(lift->started - lift->submitted), (int)lift->lane, (int)id_room, room->rejected ? room->error : "done",
            (int)room->suspended, (int)room->turn, (int)room_slabs(), R_W * R_H);
        CHECK_TRUE("the urgent lift ran", !lift->rejected);
        CHECK_TRUE("and started within 8 turns of being sent", (lift->started >= lift->submitted) && (lift->started - lift->submitted <= 8));
        CHECK_TRUE("from the urgent lane", lift->lane == EXTSEAT_LANE_NOW);
        CHECK_TRUE("the build was cut for it", room->suspended >= 1);
        CHECK_TRUE("and still finished", !room->rejected);
        CHECK_TRUE("every slab of the room is laid", room_slabs() == R_W * R_H);
        CHECK_TRUE("the lift finished before the build", lift->turn < room->turn);
    }
    const struct Thing* imp = thing_get(s_imp);
    CHECK_TRUE("the imp is out of the hand", !thing_is_invalid(imp) && !thing_is_picked_up(imp));
    // Bank 1: a heal rule, stored, not live.
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_SetAssist;
    v.assist_bank = 1;
    keeper_assist_default_globals(&v.assist_globals);   // this keeper never had any: its zeroed ones are out of range
    v.assist_globals.enabled = 1;
    keeper_assist_default_rule(&v.assist_rules[0], KAA_Cast, PwrK_HEALCRTR, KAT_AllyInFight);
    v.assist_rules[0].cond[0].fact = KAF_HealthPct; v.assist_rules[0].cond[0].op = KAO_Lt; v.assist_rules[0].cond[0].value = 30;
    v.assist_rules[0].cond_count = 1;
    struct ExtSeatSubmitInfo info = {0};
    const char* e = extseat_submit_verb_ex(U, P, &v, true, &info);
    if (e) { SOFT_FAIL("set_assist into bank 1 rejected with %s", e); return FTRs_Go_To_Next_Action; }
    id_bank = info.id;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ln06_wait(struct FTestActionArgs* const args) { return wait_settled(4); }

FTestActionResult ln07_use_bank(struct FTestActionArgs* const args)
{
    const struct KeeperAssistState* st = &kfx_sim_state.keeper_assist[P];
    CHECK_TRUE("bank 1 holds the set", st->bank[0].used && st->bank[0].rules[0].used);
    CHECK_TRUE("the live set is untouched", !st->live[0].used && (st->active_bank == 0));
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_AssistUseBank;
    v.assist_bank = 1;
    v.priority = true;
    struct ExtSeatSubmitInfo info = {0};
    const char* e = extseat_submit_verb_ex(U, P, &v, true, &info);
    if (e) { SOFT_FAIL("use_assist_bank rejected with %s", e); return FTRs_Go_To_Next_Action; }
    id_use = info.id;
    CHECK_TRUE("switching to a bank is one packet", info.steps == 1);
    struct ExtSeatVerb empty = v;
    empty.assist_bank = 2;
    e = extseat_submit_verb_ex(U, P, &empty, true, NULL);
    CHECK_TRUE("an empty bank is refused", e && strcmp(e, "EMPTY_BANK") == 0);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ln08_wait(struct FTestActionArgs* const args) { return wait_settled(3); }

FTestActionResult ln09_check(struct FTestActionArgs* const args)
{
    const struct KeeperAssistState* st = &kfx_sim_state.keeper_assist[P];
    FTESTLOG("after use_assist_bank: active bank %d, live rule 0 used %d, enabled %d", (int)st->active_bank, (int)st->live[0].used, (int)st->globals.enabled);
    CHECK_TRUE("the bank is live", (st->active_bank == 1) && st->live[0].used && st->globals.enabled);
    // find_drop_spot: next to the heart, a spot the hand may drop on.
    int64_t x = -1, y = -1;
    const char* e = extseat_find_drop_spot(P, 0, slab_subtile_center(hsx), slab_subtile_center(hsy) + 6, 15, &x, &y);
    FTESTLOG("find_drop_spot near the heart: %s (%d,%d)", e ? e : "ok", (int)x, (int)y);
    CHECK_TRUE("a drop spot is found near the heart", e == NULL);
    // The view's queue.
    VALUE v; api_seat_build_view(&v, P);
    VALUE* q = value_dict_get(value_dict_get(&v, "seat"), "queue");
    CHECK_TRUE("the view shows the queue's lanes", q != NULL && value_dict_get(q, "now") != NULL && value_dict_get(q, "routine") != NULL);
    VALUE* banks = value_dict_get(value_dict_get(value_dict_get(&v, "own"), "assist"), "banks");
    CHECK_TRUE("and the assistant's banks", banks != NULL && value_array_size(banks) == 1);
    value_fini(&v);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " lane check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: an urgent order cuts a long build and runs at once, the build finishes, and a rule bank goes live in one packet");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
