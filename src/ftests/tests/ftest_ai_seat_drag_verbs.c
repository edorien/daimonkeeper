// Phase M4 (A2) of docs/refactor/AI/LLM/01-integration-plan.md section 7: the multi-turn verbs -- build_room, mark_dig,
// sell, an overcharged cast and cancel -- from an External seat through the real driver (extseat_submit_verb +
// extseat_tick). ai_gesture_drag_verbs (T0b) established what each engine sequence does; this checks the sequencer
// reproduces exactly those sequences, so a wrong step order or count shows up as a verb that does not take effect.
#include "ftest_ai_seat_drag_verbs.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "frontend.h"
#include "game_legacy.h"
#include "config_keeperfx.h"
#include "config_magic.h"
#include "config_players.h"
#include "config_terrain.h"
#include "config_trapdoor.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "map_data.h"
#include "net_game.h"
#include "packet_data.h"
#include "player_data.h"
#include "player_instances.h"
#include "room_data.h"
#include "roomspace.h"
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
#define EXPECT_ERR(what, verb, code) do { const char* e_ = extseat_submit_verb(U, P, &(verb), NULL); \
    if (e_ == NULL || strcmp(e_, code) != 0) SOFT_FAIL("%s: expected %s, got %s", what, code, e_ ? e_ : "success"); } while (0)

static PlayerNumber P = -1;
static NetUserId U = -1;
static MapSlabCoord hsx, hsy;
static RoomKind treasure;
static ThingModel trap_model;

#define R_X0 (hsx + 3)
#define R_Y0 (hsy - 1)
#define R_W 4
#define R_H 3
#define D_X0 (hsx - 9)
#define D_Y0 (hsy - 1)
#define D_W 4
#define D_H 4 /* an even row count: a serpentine released back in its start column once tagged only that column */
#define S_Y (hsy + 7)
#define CAST_X (R_X0 + 1)
#define CAST_Y R_Y0

static struct Dungeon* dg(void) { return get_players_dungeon(get_player(P)); }
static TbBool tagged(MapSlabCoord sx, MapSlabCoord sy) { return find_from_task_list(P, get_subtile_number(slab_subtile_center(sx), slab_subtile_center(sy))) != -1; }
static TbBool trap_at(MapSlabCoord sx) { return !thing_is_invalid(get_trap_for_position(slab_subtile_center(sx), slab_subtile_center(S_Y))); }
static int64_t room_slabs_in(MapSlabCoord x0, MapSlabCoord y0, int w, int h)
{
    int64_t n = 0;
    for (int dx = 0; dx < w; dx++) for (int dy = 0; dy < h; dy++)
    {
        const struct SlabMap* slb = get_slabmap_block(x0 + dx, y0 + dy);
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
static void submit_ok(const char* what, struct ExtSeatVerb v)
{
    int64_t steps = 0;
    const char* e = extseat_submit_verb(U, P, &v, &steps);
    if (e) SOFT_FAIL("%s: rejected with %s", what, e); else FTESTLOG("%s: queued %" PRId64 " step(s)", what, steps);
}

// Repeat the current action until the seat has written every step, then a further `settle` turns for the engine to act.
static int64_t s_idle_since = -1;
static FTestActionResult wait_settled(struct FTestActionArgs* const args, int64_t settle)
{
    if (!extseat_idle(U)) { s_idle_since = -1; return FTRs_Repeat_Current_Action; }
    if (s_idle_since < 0) s_idle_since = (int64_t)get_gameturn();
    if ((int64_t)get_gameturn() < s_idle_since + settle) return FTRs_Repeat_Current_Action;
    s_idle_since = -1;
    return FTRs_Go_To_Next_Action;
}

void ftest_ai_seat_drag_verbs_pre_start() { fe_computer_players = 1; }

FTestActionResult sdv01_setup(struct FTestActionArgs* const args);
FTestActionResult sdv02_validation(struct FTestActionArgs* const args);
FTestActionResult sdv03_room(struct FTestActionArgs* const args);
FTestActionResult sdv04_room_wait(struct FTestActionArgs* const args);
FTestActionResult sdv05_room_check_dig(struct FTestActionArgs* const args);
FTestActionResult sdv06_dig_wait(struct FTestActionArgs* const args);
FTestActionResult sdv07_dig_check_sell(struct FTestActionArgs* const args);
FTestActionResult sdv08_sell_wait(struct FTestActionArgs* const args);
FTestActionResult sdv09_sell_check_cast(struct FTestActionArgs* const args);
FTestActionResult sdv10_cast_wait_short(struct FTestActionArgs* const args);
FTestActionResult sdv11_cast_long(struct FTestActionArgs* const args);
FTestActionResult sdv12_cast_wait_long(struct FTestActionArgs* const args);
FTestActionResult sdv13_cancel_start(struct FTestActionArgs* const args);
FTestActionResult sdv14_cancel(struct FTestActionArgs* const args);
FTestActionResult sdv15_cancel_check_end(struct FTestActionArgs* const args);

static int64_t s_short_level = -1;

TbBool ftest_ai_seat_drag_verbs_init()
{
    s_failures = 0; s_idle_since = -1;
    ftest_append_action(sdv01_setup, 0, NULL);
    ftest_append_action(sdv02_validation, 2, NULL);
    ftest_append_action(sdv03_room, 1, NULL);        ftest_append_action(sdv04_room_wait, 1, NULL);
    ftest_append_action(sdv05_room_check_dig, 1, NULL); ftest_append_action(sdv06_dig_wait, 1, NULL);
    ftest_append_action(sdv07_dig_check_sell, 1, NULL); ftest_append_action(sdv08_sell_wait, 1, NULL);
    ftest_append_action(sdv09_sell_check_cast, 1, NULL); ftest_append_action(sdv10_cast_wait_short, 1, NULL);
    ftest_append_action(sdv11_cast_long, 1, NULL);   ftest_append_action(sdv12_cast_wait_long, 1, NULL);
    ftest_append_action(sdv13_cancel_start, 1, NULL); ftest_append_action(sdv14_cancel, 3, NULL);
    ftest_append_action(sdv15_cancel_check_end, 1, NULL);
    return true;
}

FTestActionResult sdv01_setup(struct FTestActionArgs* const args)
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
    struct Dungeon* d = dg();
    d->total_money_owned = 100000;
    d->room_buildable[treasure] |= 1;
    d->magic_level[PwrK_CALL2ARMS] = 1;
    set_trap_buildable_and_add_to_amount(P, trap_model, 1, 8);

    ftest_util_replace_slabs(R_X0 - 1, R_Y0 - 1, R_X0 + R_W, R_Y0 + R_H, SlbT_CLAIMED, P);
    ftest_util_replace_slabs(D_X0, D_Y0, D_X0 + D_W - 1, D_Y0 + D_H - 1, SlbT_EARTH, PLAYER_NEUTRAL);
    ftest_util_replace_slabs(hsx - 2, S_Y, hsx + 6, S_Y, SlbT_CLAIMED, P);
    for (int i = -1; i <= 1; i++) player_place_trap_at_subtile_without_check(slab_subtile_center(hsx + i), slab_subtile_center(S_Y), P, trap_model, true);
    if (!trap_at(hsx - 1) || !trap_at(hsx) || !trap_at(hsx + 1)) SOFT_FAIL("could not place the three traps to sell");
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sdv02_validation(struct FTestActionArgs* const args)
{
    struct ExtSeatVerb v;
    v = V(ESV_BuildRoom, "TREASURE");                                        EXPECT_ERR("build_room without a rect", v, "MISSING_RECT");
    v = rect(V(ESV_BuildRoom, "TREASURE"), 0, 0, 9999, 9999);                EXPECT_ERR("build_room off the map", v, "POSITION_OFF_MAP");
    v = rect(V(ESV_BuildRoom, "NO_SUCH_ROOM"), R_X0, R_Y0, R_X0 + 1, R_Y0);  EXPECT_ERR("unknown room", v, "UNKNOWN_KIND");
    v = rect(V(ESV_BuildRoom, "TREASURE"), 0, 0, 40, 40);                    EXPECT_ERR("build_room over the area cap", v, "AREA_TOO_LARGE");
    v = rect(V(ESV_MarkDig, NULL), 0, 0, 2, EXTSEAT_MAX_DIG_ROWS + 1);       EXPECT_ERR("mark_dig with too many rows", v, "AREA_TOO_LARGE");
    v = V(ESV_MarkDig, NULL);                                                EXPECT_ERR("mark_dig without a rect", v, "MISSING_RECT");
    v = rect(V(ESV_Sell, NULL), 0, 0, 30, 30);                               EXPECT_ERR("sell over the area cap", v, "AREA_TOO_LARGE");
    v = rect(V(ESV_Sell, NULL), D_X0, D_Y0, D_X0 + 1, D_Y0);                 EXPECT_ERR("sell an area the seat owns none of", v, "NOTHING_TO_SELL");
    { struct ExtSeatVerb c = V(ESV_CastPower, "POWER_CALL_TO_ARMS"); c.has_pos = true; c.stl_x = slab_subtile_center(CAST_X); c.stl_y = slab_subtile_center(CAST_Y); c.overcharge_turns = EXTSEAT_MAX_OVERCHARGE_TURNS + 1;
      EXPECT_ERR("an overcharge beyond the cap", c, "BAD_OVERCHARGE"); }
    if (!extseat_idle(U)) SOFT_FAIL("a rejected verb left steps queued");
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sdv03_room(struct FTestActionArgs* const args)
{
    submit_ok("build_room", rect(V(ESV_BuildRoom, "TREASURE"), R_X0, R_Y0, R_X0 + R_W - 1, R_Y0 + R_H - 1));
    return FTRs_Go_To_Next_Action;
}
FTestActionResult sdv04_room_wait(struct FTestActionArgs* const args) { return wait_settled(args, 25); }

FTestActionResult sdv05_room_check_dig(struct FTestActionArgs* const args)
{
    const int64_t have = room_slabs_in(R_X0, R_Y0, R_W, R_H);
    if (have != R_W * R_H) SOFT_FAIL("build_room built %" PRId64 " of %d slabs", have, R_W * R_H);
    // The dig tool's mode is the player's, not the seat's: a local player gets it from DEFAULT_TAG_MODE, and DRAG tags
    // the box from press to release instead of the brush path. mark_dig must set the mode it needs, whatever it finds.
    get_player(P)->roomspace_highlight_mode = drag_placement_mode;
    submit_ok("mark_dig", rect(V(ESV_MarkDig, NULL), D_X0, D_Y0, D_X0 + D_W - 1, D_Y0 + D_H - 1));
    return FTRs_Go_To_Next_Action;
}
FTestActionResult sdv06_dig_wait(struct FTestActionArgs* const args) { return wait_settled(args, 3); }

FTestActionResult sdv07_dig_check_sell(struct FTestActionArgs* const args)
{
    int64_t have = 0;
    for (int dx = 0; dx < D_W; dx++) for (int dy = 0; dy < D_H; dy++) if (tagged(D_X0 + dx, D_Y0 + dy)) have++;
    if (have != D_W * D_H) SOFT_FAIL("mark_dig tagged %" PRId64 " of %d slabs", have, D_W * D_H);
    // Three traps in a row plus an empty slab on each side: the whole strip goes in one request.
    submit_ok("sell", rect(V(ESV_Sell, NULL), hsx - 2, S_Y, hsx + 2, S_Y));
    return FTRs_Go_To_Next_Action;
}
FTestActionResult sdv08_sell_wait(struct FTestActionArgs* const args) { return wait_settled(args, 8); }

FTestActionResult sdv09_sell_check_cast(struct FTestActionArgs* const args)
{
    const int64_t left = trap_at(hsx - 1) + trap_at(hsx) + trap_at(hsx + 1);
    if (left != 0) SOFT_FAIL("sell left %" PRId64 " of 3 traps (a sell drag would have left one)", left);
    struct ExtSeatVerb c = V(ESV_CastPower, "POWER_CALL_TO_ARMS"); c.has_pos = true; c.stl_x = slab_subtile_center(CAST_X); c.stl_y = slab_subtile_center(CAST_Y);
    submit_ok("cast_power (no overcharge)", c);
    return FTRs_Go_To_Next_Action;
}
FTestActionResult sdv10_cast_wait_short(struct FTestActionArgs* const args)
{
    const FTestActionResult r = wait_settled(args, 3);
    if (r == FTRs_Go_To_Next_Action)
    {
        s_short_level = dg()->cta_power_level;
        if (dg()->cta_start_turn == 0) SOFT_FAIL("the plain cast did not place Call to Arms");
        FTESTLOG("plain cast: Call to Arms at level %" PRId64, s_short_level);
    }
    return r;
}

FTestActionResult sdv11_cast_long(struct FTestActionArgs* const args)
{
    struct ExtSeatVerb c = V(ESV_CastPower, "POWER_CALL_TO_ARMS"); c.has_pos = true; c.stl_x = slab_subtile_center(CAST_X); c.stl_y = slab_subtile_center(CAST_Y); c.overcharge_turns = 12;
    // A Call to Arms already in place ignores a new cast; take it down first (a one-shot action, not a verb).
    turn_off_power_call_to_arms(P);
    submit_ok("cast_power (overcharge 12 turns)", c);
    return FTRs_Go_To_Next_Action;
}
FTestActionResult sdv12_cast_wait_long(struct FTestActionArgs* const args)
{
    const FTestActionResult r = wait_settled(args, 3);
    if (r == FTRs_Go_To_Next_Action)
    {
        FTESTLOG("overcharged cast: Call to Arms at level %" PRId64, (int64_t)dg()->cta_power_level);
        if (dg()->cta_power_level != 3) SOFT_FAIL("12 held turns should cast at level 3, got %" PRId64, (int64_t)dg()->cta_power_level);
    }
    return r;
}

// cancel: start a room build, drop it after the drag has begun, and check nothing is built and the seat can act again.
FTestActionResult sdv13_cancel_start(struct FTestActionArgs* const args)
{
    // The floor first: build_room is checked when submitted (CANNOT_BUILD_HERE on no claimed floor).
    ftest_util_replace_slabs(R_X0 + R_W + 2, R_Y0, R_X0 + R_W + 4, R_Y0 + 1, SlbT_CLAIMED, P);
    submit_ok("build_room to be cancelled", rect(V(ESV_BuildRoom, "TREASURE"), R_X0 + R_W + 2, R_Y0, R_X0 + R_W + 4, R_Y0 + 1));
    return FTRs_Go_To_Next_Action;
}
FTestActionResult sdv14_cancel(struct FTestActionArgs* const args)
{
    // Wait until the press (step 4) has been written: the button is down and the release has not been.
    if (!extseat_mid_gesture(U))
    {
        static int64_t started = -1;
        if (started < 0) started = (int64_t)get_gameturn();
        if ((int64_t)get_gameturn() > started + 20) { SOFT_FAIL("the seat never went mid-gesture"); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    struct ExtSeatVerb c = V(ESV_Cancel, NULL);
    int64_t dropped = -1;
    const char* e = extseat_submit_verb(U, P, &c, &dropped);
    if (e) SOFT_FAIL("cancel was rejected: %s", e); else FTESTLOG("cancel dropped %" PRId64 " step(s)", dropped);
    if (dropped < 1) SOFT_FAIL("cancel dropped no steps");
    return FTRs_Go_To_Next_Action;
}
FTestActionResult sdv15_cancel_check_end(struct FTestActionArgs* const args)
{
    const FTestActionResult r = wait_settled(args, 25);
    if (r != FTRs_Go_To_Next_Action) return r;
    const int64_t built = room_slabs_in(R_X0 + R_W + 2, R_Y0, 3, 2);
    if (built != 0) SOFT_FAIL("a cancelled build_room still built %" PRId64 " slab(s)", built);
    if (extseat_mid_gesture(U)) SOFT_FAIL("the seat is still mid-gesture after the cancel");
    if (get_player(P)->work_state != PSt_CtrlDungeon) SOFT_FAIL("the seat's work_state was not left as PSt_CtrlDungeon after the cancel: %" PRId64, (int64_t)get_player(P)->work_state);
    // The seat is usable again: a fresh gesture is accepted.
    struct ExtSeatVerb v = rect(V(ESV_MarkDig, NULL), D_X0, D_Y0, D_X0, D_Y0);
    const char* e = extseat_submit_verb(U, P, &v, NULL);
    if (e) SOFT_FAIL("a new verb after the cancel was rejected: %s", e);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: build_room, mark_dig, sell, overcharge and cancel work from an External seat through the driver");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
