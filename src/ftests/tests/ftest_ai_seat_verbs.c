// Phase M2 of docs/refactor/AI/LLM/01-integration-plan.md section 7: the single-turn verbs, this time from the
// second (External) seat and through the real driver -- extseat_submit_verb() validating and queuing, and
// extseat_tick() writing one packet per simulated turn. ai_gesture_single_turn (T0a) proved the packet
// sequences on the local seat; this proves the driver reproduces them for a seat nobody is typing for.
// The API layer on top (JSON, pause control) is exercised by scripts/run_ftest_ai_bridge_smoke.sh.
#include "ftest_ai_seat_verbs.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "game_legacy.h"
#include "config_keeperfx.h"
#include "config_creature.h"
#include "config_magic.h"
#include "config_players.h"
#include "config_trapdoor.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "net_game.h"
#include "player_data.h"
#include "player_instances.h"
#include "power_hand.h"
#include "thing_doors.h"
#include "thing_list.h"
#include "thing_traps.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define EXPECT_ERR(what, verb, code) do { const char *e_ = extseat_submit_verb(vars.user, vars.player, &(verb), NULL); \
    if (e_ == NULL || strcmp(e_, code) != 0) SOFT_FAIL("%s: expected %s, got %s", what, code, e_ ? e_ : "success"); } while (0)

struct vars_t {
    PlayerNumber player;
    NetUserId user;
    ThingIndex crtr;
    MapSubtlCoord hx, hy;
    ThingModel trap, door;
    int64_t slaps_before;
};
static struct vars_t vars = {-1, -1, 0, 0, 0, 0, 0, 0};

void ftest_ai_seat_verbs_pre_start()
{
    fe_computer_players = 1;
}

static struct Dungeon* dg(void) { return get_players_dungeon(get_player(vars.player)); }
static MapSubtlCoord slab_center_stl(MapSubtlCoord stl) { return slab_subtile_center(subtile_slab(stl)); }

static struct ExtSeatVerb verb_at(enum ExtSeatVerbKind k, const char* name, int64_t x, int64_t y)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = k;
    if (name) snprintf(v.name, sizeof(v.name), "%s", name);
    if (x >= 0) { v.has_pos = true; v.stl_x = x; v.stl_y = y; }
    return v;
}
static struct ExtSeatVerb verb_thing(enum ExtSeatVerbKind k, const char* name, int64_t id)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = k; v.has_thing = true; v.thing_id = id;
    if (name) snprintf(v.name, sizeof(v.name), "%s", name);
    return v;
}
static void submit_ok(const char* what, struct ExtSeatVerb v)
{
    int64_t steps = 0;
    const char *e = extseat_submit_verb(vars.user, vars.player, &v, &steps);
    if (e != NULL) SOFT_FAIL("%s: rejected with %s", what, e);
    else FTESTLOG("%s: queued %" PRId64 " step(s)", what, steps);
}

FTestActionResult v01_setup(struct FTestActionArgs* const args);
FTestActionResult v02_validation(struct FTestActionArgs* const args);
FTestActionResult v03_trap(struct FTestActionArgs* const args);
FTestActionResult v04_trap_check_door(struct FTestActionArgs* const args);
FTestActionResult v05_door_check_slap(struct FTestActionArgs* const args);
FTestActionResult v06_slap_check_pick(struct FTestActionArgs* const args);
FTestActionResult v07_pick_check_drop(struct FTestActionArgs* const args);
FTestActionResult v08_drop_check_cast(struct FTestActionArgs* const args);
FTestActionResult v09_cast_check_end(struct FTestActionArgs* const args);

TbBool ftest_ai_seat_verbs_init()
{
    s_failures = 0;
    ftest_append_action(v01_setup, 0, &vars);
    ftest_append_action(v02_validation, 2, &vars);
    ftest_append_action(v03_trap, 2, &vars);
    ftest_append_action(v04_trap_check_door, 5, &vars);
    // A slap is ignored for 10 turns after the last creature drop and that counter starts at 0.
    ftest_append_action(v05_door_check_slap, 20, &vars);
    ftest_append_action(v06_slap_check_pick, 6, &vars);
    ftest_append_action(v07_pick_check_drop, 5, &vars);
    ftest_append_action(v08_drop_check_cast, 5, &vars);
    ftest_append_action(v09_cast_check_end, 5, &vars);
    return true;
}

FTestActionResult v01_setup(struct FTestActionArgs* const args)
{
    ftest_util_reveal_map(PLAYER0);
    vars.player = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
    {
        if (p == my_player_number || p == PLAYER_GOOD || p == PLAYER_NEUTRAL) continue;
        if (player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { vars.player = p; break; }
    }
    if (vars.player < 0) { FTEST_FAIL_TEST("no second keeper"); return FTRs_Go_To_Next_Action; }
    vars.user = net_add_external_seat(vars.player);
    if (vars.user < 1) { FTEST_FAIL_TEST("net_add_external_seat failed"); return FTRs_Go_To_Next_Action; }

    const struct Thing* heart = find_players_dungeon_heart(vars.player);
    vars.hx = heart->mappos.x.stl.num; vars.hy = heart->mappos.y.stl.num;
    struct Thing* c = ftest_util_create_creature(subtile_coord_center(vars.hx - 3), subtile_coord_center(vars.hy), vars.player, 9, (ThingModel)creature_model_id("ORC"));
    if (thing_is_invalid(c)) { FTEST_FAIL_TEST("no creature"); return FTRs_Go_To_Next_Action; }
    vars.crtr = c->index;

    vars.trap = (ThingModel)trap_model_id("BOULDER");
    vars.door = (ThingModel)door_model_id("WOOD");
    set_trap_buildable_and_add_to_amount(vars.player, vars.trap, 1, 2);
    set_door_buildable_and_add_to_amount(vars.player, vars.door, 1, 2);
    dg()->magic_level[PwrK_CALL2ARMS] = 1;
    dg()->total_money_owned += 5000;
    // Traps and doors are placed on the seat's own claimed floor; doors also need wall on two opposite sides.
    ftest_util_replace_slabs(subtile_slab(vars.hx), subtile_slab(vars.hy) + 2, subtile_slab(vars.hx), subtile_slab(vars.hy) + 2, SlbT_CLAIMED, vars.player);
    const MapSlabCoord dsx = subtile_slab(vars.hx) + 4, dsy = subtile_slab(vars.hy);
    ftest_util_replace_slabs(dsx - 1, dsy, dsx + 1, dsy, SlbT_CLAIMED, vars.player);
    ftest_util_replace_slabs(dsx - 1, dsy - 1, dsx + 1, dsy - 1, SlbT_WALLDRAPE, PLAYER_NEUTRAL);
    ftest_util_replace_slabs(dsx - 1, dsy + 1, dsx + 1, dsy + 1, SlbT_WALLDRAPE, PLAYER_NEUTRAL);
    return FTRs_Go_To_Next_Action;
}

// Every rejection the driver promises, before anything is queued.
FTestActionResult v02_validation(struct FTestActionArgs* const args)
{
    struct ExtSeatVerb v;
    v = verb_at(ESV_PlaceTrap, "NO_SUCH_TRAP", vars.hx, vars.hy + 6);          EXPECT_ERR("unknown trap", v, "UNKNOWN_KIND");
    v = verb_at(ESV_PlaceTrap, "ALARM", vars.hx, vars.hy + 6);                 EXPECT_ERR("trap with no stock", v, "NOT_AVAILABLE");
    v = verb_at(ESV_PlaceTrap, "BOULDER", 9999, 9999);                         EXPECT_ERR("trap off map", v, "POSITION_OFF_MAP");
    v = verb_at(ESV_PlaceTrap, "BOULDER", -1, -1);                             EXPECT_ERR("trap without position", v, "MISSING_POSITION");
    v = verb_thing(ESV_Slap, NULL, 1);                                         /* thing 1 is no creature of ours */
    { const char* e = extseat_submit_verb(vars.user, vars.player, &v, NULL); if (e == NULL) SOFT_FAIL("slap of a non-creature was accepted"); }
    v = verb_thing(ESV_Slap, NULL, vars.crtr);                                 EXPECT_ERR("slap this early in the level", v, "SLAP_NOT_READY");
    v = verb_thing(ESV_PickUp, NULL, 1);
    { const char* e = extseat_submit_verb(vars.user, vars.player, &v, NULL); if (e == NULL) SOFT_FAIL("pick-up of a non-creature was accepted"); }
    v = verb_at(ESV_Drop, NULL, vars.hx, vars.hy);                             EXPECT_ERR("drop with an empty hand", v, "HAND_EMPTY");
    v = verb_at(ESV_CastPower, "NO_SUCH_POWER", vars.hx, vars.hy);             EXPECT_ERR("unknown power", v, "UNKNOWN_KIND");
    v = verb_at(ESV_None, NULL, -1, -1);                                       EXPECT_ERR("unknown verb", v, "UNKNOWN_VERB");
    { struct ExtSeatVerb w = verb_at(ESV_PlaceTrap, "BOULDER", vars.hx, vars.hy + 6);
      const char* e = extseat_submit_verb(1 + (vars.user % 3), vars.player, &w, NULL);
      if (e == NULL || strcmp(e, "NOT_A_VALID_SEAT") != 0) SOFT_FAIL("submitting for the wrong user: expected NOT_A_VALID_SEAT, got %s", e ? e : "success"); }
    if (!extseat_idle(vars.user)) SOFT_FAIL("a rejected verb left steps queued");
    return FTRs_Go_To_Next_Action;
}

FTestActionResult v03_trap(struct FTestActionArgs* const args)
{
    submit_ok("place_trap", verb_at(ESV_PlaceTrap, "BOULDER", vars.hx, vars.hy + 6));
    struct ExtSeatVerb again = verb_at(ESV_PlaceTrap, "BOULDER", vars.hx, vars.hy + 6);
    EXPECT_ERR("second gesture while the first runs", again, "ACTION_ALREADY_QUEUED");
    return FTRs_Go_To_Next_Action;
}

FTestActionResult v04_trap_check_door(struct FTestActionArgs* const args)
{
    struct Thing* t = get_trap_for_position(slab_center_stl(vars.hx), slab_center_stl(vars.hy + 6));
    if (thing_is_invalid(t) || !thing_is_deployed_trap(t) || t->model != vars.trap || t->owner != vars.player) SOFT_FAIL("place_trap did not deploy the seat's trap");
    if (!extseat_idle(vars.user)) SOFT_FAIL("the queue is not empty after the trap gesture");
    submit_ok("place_door", verb_at(ESV_PlaceDoor, "WOOD", vars.hx + 12, vars.hy));
    return FTRs_Go_To_Next_Action;
}

FTestActionResult v05_door_check_slap(struct FTestActionArgs* const args)
{
    struct Thing* d = get_door_for_position(slab_center_stl(vars.hx + 12), slab_center_stl(vars.hy));
    if (thing_is_invalid(d) || !thing_is_deployed_door(d) || d->model != vars.door || d->owner != vars.player) SOFT_FAIL("place_door did not deploy the seat's door");
    vars.slaps_before = dg()->lvstats.num_slaps;
    submit_ok("slap", verb_thing(ESV_Slap, NULL, vars.crtr));
    return FTRs_Go_To_Next_Action;
}

FTestActionResult v06_slap_check_pick(struct FTestActionArgs* const args)
{
    if (dg()->lvstats.num_slaps <= vars.slaps_before) SOFT_FAIL("slap did not slap");
    if (get_player(vars.player)->instance_num == PI_Whip) return FTRs_Repeat_Current_Action;
    submit_ok("pick_up", verb_thing(ESV_PickUp, NULL, vars.crtr));
    return FTRs_Go_To_Next_Action;
}

FTestActionResult v07_pick_check_drop(struct FTestActionArgs* const args)
{
    const struct Thing* c = thing_get(vars.crtr);
    if (thing_is_invalid(c) || !thing_is_picked_up(c)) SOFT_FAIL("pick_up did not pick the creature up");
    submit_ok("drop", verb_at(ESV_Drop, NULL, vars.hx - 3, vars.hy + 3));
    return FTRs_Go_To_Next_Action;
}

FTestActionResult v08_drop_check_cast(struct FTestActionArgs* const args)
{
    const struct Thing* c = thing_get(vars.crtr);
    if (thing_is_invalid(c) || thing_is_picked_up(c) || c->mappos.x.stl.num != vars.hx - 3 || c->mappos.y.stl.num != vars.hy + 3) SOFT_FAIL("drop did not put the creature down at the target");
    submit_ok("cast_power", verb_at(ESV_CastPower, "POWER_CALL_TO_ARMS", vars.hx, vars.hy - 6));
    return FTRs_Go_To_Next_Action;
}

FTestActionResult v09_cast_check_end(struct FTestActionArgs* const args)
{
    if (dg()->cta_start_turn == 0 || dg()->cta_stl_x != vars.hx || dg()->cta_stl_y != vars.hy - 6) SOFT_FAIL("cast_power did not place Call to Arms at the target");
    if (!extseat_idle(vars.user)) SOFT_FAIL("the queue is not empty at the end");
    if (get_player(my_player_number)->work_state != PSt_CtrlDungeon) SOFT_FAIL("the local human's work_state was disturbed");
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: every verb validated and executed from an External seat through the driver");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
