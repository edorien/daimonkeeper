// Ground truth for "order creature" as a non-visual alternative to possession (M6c). The cheat menu's Order creature mode
// (PSt_OrderCreatr) selects a creature with one click and sends it to a subtile with the next; clicking the creature's own
// subtile hands it back to its normal AI. This test drives that from an External seat with raw packet steps and records what
// the engine does, so the verbs are built on facts: does the click select, does the creature walk, what state does it hold,
// does it stay put until released, does a state change clear the selection, and does the release resume its AI.
#include "ftest_ai_gesture_order_creature.h"

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
#include "packet_data.h"
#include "player_data.h"
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
static ThingIndex crtr = 0, other = 0;
static MapSubtlCoord tx, ty; // the target subtile

static struct ExtSeatStep act(unsigned char a, int64_t p1) { struct ExtSeatStep s; memset(&s, 0, sizeof(s)); s.action = a; s.par1 = p1; return s; }
static struct ExtSeatStep idle(void) { struct ExtSeatStep s; memset(&s, 0, sizeof(s)); return s; }
static struct ExtSeatStep click(MapSubtlCoord x, MapSubtlCoord y, uint64_t flags)
{
    struct ExtSeatStep s; memset(&s, 0, sizeof(s));
    s.has_pos = true; s.pos_x = subtile_coord_center(x); s.pos_y = subtile_coord_center(y); s.control_flags = flags;
    return s;
}
static const struct Thing* T(ThingIndex i) { return thing_get(i); }
static void log_state(const char* tag)
{
    const struct Thing* t = T(crtr);
    const struct PlayerInfo* pl = get_player(P);
    FTESTLOG("%s: turn %" PRId64 " pos (%" PRId64 ",%" PRId64 ") state %s continue %s | player work_state %" PRId64 " selected %" PRId64 " thing_under_hand %" PRId64,
        tag, (int64_t)get_gameturn(), (int64_t)t->mappos.x.stl.num, (int64_t)t->mappos.y.stl.num,
        creature_state_code_name(t->active_state), creature_state_code_name(t->continue_state),
        (int64_t)pl->work_state, (int64_t)pl->controlled_thing_idx, (int64_t)pl->thing_under_hand);
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

FTestActionResult o01_setup(struct FTestActionArgs* const args);
FTestActionResult o02_order(struct FTestActionArgs* const args);
FTestActionResult o03_walk(struct FTestActionArgs* const args);
FTestActionResult o04_check_walked_leave_state(struct FTestActionArgs* const args);
FTestActionResult o05_wait_after_leave(struct FTestActionArgs* const args);
FTestActionResult o06_check_stays_then_second_creature(struct FTestActionArgs* const args);
FTestActionResult o07_second_wait(struct FTestActionArgs* const args);
FTestActionResult o08_check_second_release_first(struct FTestActionArgs* const args);
FTestActionResult o09_release_wait(struct FTestActionArgs* const args);
FTestActionResult o10_check_released(struct FTestActionArgs* const args);

void ftest_ai_gesture_order_creature_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_gesture_order_creature_init()
{
    s_failures = 0; s_idle_since = -1;
    ftest_append_action(o01_setup, 0, NULL);
    ftest_append_action(o02_order, 2, NULL);
    ftest_append_action(o03_walk, 1, NULL);
    ftest_append_action(o04_check_walked_leave_state, 1, NULL);
    ftest_append_action(o05_wait_after_leave, 1, NULL);
    ftest_append_action(o06_check_stays_then_second_creature, 1, NULL);
    ftest_append_action(o07_second_wait, 1, NULL);
    ftest_append_action(o08_check_second_release_first, 1, NULL);
    ftest_append_action(o09_release_wait, 1, NULL);
    ftest_append_action(o10_check_released, 1, NULL);
    return true;
}

FTestActionResult o01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || (U = net_add_external_seat(P)) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    const struct Thing* heart = find_players_dungeon_heart(P);
    const MapSubtlCoord hx = heart->mappos.x.stl.num, hy = heart->mappos.y.stl.num;
    ftest_util_replace_slabs(subtile_slab(hx) - 4, subtile_slab(hy) + 3, subtile_slab(hx) + 9, subtile_slab(hy) + 5, SlbT_CLAIMED, P);
    struct Thing* a = ftest_util_create_creature(subtile_coord_center(hx - 3), subtile_coord_center(hy + 10), P, 9, (ThingModel)creature_model_id("ORC"));
    struct Thing* b = ftest_util_create_creature(subtile_coord_center(hx - 6), subtile_coord_center(hy + 10), P, 9, (ThingModel)creature_model_id("TROLL"));
    if (thing_is_invalid(a) || thing_is_invalid(b)) { FTEST_FAIL_TEST("no creatures"); return FTRs_Go_To_Next_Action; }
    crtr = a->index; other = b->index;
    tx = hx + 14; ty = hy + 10;
    log_state("start");
    return FTRs_Go_To_Next_Action;
}

FTestActionResult o02_order(struct FTestActionArgs* const args)
{
    const struct Thing* t = T(crtr);
    struct ExtSeatStep steps[6];
    steps[0] = act(PckA_SetPlyrState, PSt_OrderCreatr);
    steps[1] = idle();
    steps[2] = click(t->mappos.x.stl.num, t->mappos.y.stl.num, PCtr_LBtnRelease);   // select
    steps[3] = idle();
    steps[4] = click(tx, ty, PCtr_LBtnRelease);                                    // send
    steps[5] = idle();
    CHECK_TRUE("steps queued", extseat_enqueue(U, steps, 6));
    return FTRs_Go_To_Next_Action;
}

FTestActionResult o03_walk(struct FTestActionArgs* const args)
{
    if (!extseat_idle(U)) return FTRs_Repeat_Current_Action;
    log_state("after the two clicks");
    // Let it walk; log now and then.
    if ((int64_t)get_gameturn() % 10 == 0) log_state("walking");
    const struct Thing* t = T(crtr);
    const TbBool there = (t->mappos.x.stl.num == tx) && (t->mappos.y.stl.num == ty);
    if (there || (args->intended_start_at_game_turn > 0 && (int64_t)get_gameturn() > args->intended_start_at_game_turn + 400)) return FTRs_Go_To_Next_Action;
    return FTRs_Repeat_Current_Action;
}

FTestActionResult o04_check_walked_leave_state(struct FTestActionArgs* const args)
{
    log_state("arrived?");
    const struct Thing* t = T(crtr);
    CHECK_TRUE("the ordered creature reached the target subtile", t->mappos.x.stl.num == tx && t->mappos.y.stl.num == ty);
    // Leave the mode; does the selection clear?
    struct ExtSeatStep s[2] = { act(PckA_SetPlyrState, PSt_CtrlDungeon), idle() };
    CHECK_TRUE("leave-state queued", extseat_enqueue(U, s, 2));
    return FTRs_Go_To_Next_Action;
}
FTestActionResult o05_wait_after_leave(struct FTestActionArgs* const args) { return wait_settled(30); }

FTestActionResult o06_check_stays_then_second_creature(struct FTestActionArgs* const args)
{
    log_state("30 turns after leaving the mode");
    const struct Thing* t = T(crtr);
    CHECK_TRUE("the creature stays where it was sent (manual control) after the mode is left", t->mappos.x.stl.num == tx && t->mappos.y.stl.num == ty);
    FTESTLOG("selection after leaving the mode: %" PRId64, (int64_t)get_player(P)->controlled_thing_idx);
    // Now order the second creature: enter the mode again and select it.
    const struct Thing* o = T(other);
    struct ExtSeatStep steps[6];
    steps[0] = act(PckA_SetPlyrState, PSt_OrderCreatr);
    steps[1] = idle();
    steps[2] = click(o->mappos.x.stl.num, o->mappos.y.stl.num, PCtr_LBtnRelease);
    steps[3] = idle();
    steps[4] = click(tx - 4, ty, PCtr_LBtnRelease);
    steps[5] = idle();
    CHECK_TRUE("second order queued", extseat_enqueue(U, steps, 6));
    return FTRs_Go_To_Next_Action;
}
FTestActionResult o07_second_wait(struct FTestActionArgs* const args) { return wait_settled(60); }

FTestActionResult o08_check_second_release_first(struct FTestActionArgs* const args)
{
    const struct Thing* o = T(other);
    FTESTLOG("second creature at (%" PRId64 ",%" PRId64 ") state %s selected %" PRId64, (int64_t)o->mappos.x.stl.num, (int64_t)o->mappos.y.stl.num, creature_state_code_name(o->active_state), (int64_t)get_player(P)->controlled_thing_idx);
    const struct Thing* t = T(crtr);
    CHECK_TRUE("the first creature was not disturbed by ordering the second", t->mappos.x.stl.num == tx && t->mappos.y.stl.num == ty);
    // Release the first: select it (state re-entered), then click its own subtile.
    struct ExtSeatStep steps[8];
    steps[0] = act(PckA_SetPlyrState, PSt_CtrlDungeon);
    steps[1] = idle();
    steps[2] = act(PckA_SetPlyrState, PSt_OrderCreatr);
    steps[3] = idle();
    steps[4] = click(t->mappos.x.stl.num, t->mappos.y.stl.num, PCtr_LBtnRelease); // select it
    steps[5] = idle();
    steps[6] = click(t->mappos.x.stl.num, t->mappos.y.stl.num, PCtr_LBtnRelease); // its own subtile: hand it back to the AI
    steps[7] = idle();
    CHECK_TRUE("release queued", extseat_enqueue(U, steps, 8));
    return FTRs_Go_To_Next_Action;
}
FTestActionResult o09_release_wait(struct FTestActionArgs* const args) { return wait_settled(40); }

FTestActionResult o10_check_released(struct FTestActionArgs* const args)
{
    log_state("40 turns after the release");
    const struct Thing* t = T(crtr);
    FTESTLOG("released creature state %s (continue %s)", creature_state_code_name(t->active_state), creature_state_code_name(t->continue_state));
    CHECK_TRUE("the released creature is no longer in manual control", t->active_state != CrSt_ManualControl && t->continue_state != CrSt_ManualControl);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " order-creature check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: order creature selects, moves, holds and releases from an External seat");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
