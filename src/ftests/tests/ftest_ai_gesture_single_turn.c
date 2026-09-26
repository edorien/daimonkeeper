// Ground truth for docs/refactor/AI/LLM/04-seat-and-action-api.md §2: the single-turn
// gameplay verbs an external agent seat will have to reproduce, driven through the packet
// fields a real input device writes (work_state via PckA_SetPlyrState, ambient pos_x/pos_y +
// PCtr_LBtnRelease/Click, or the one-shot PckA_UsePwr* actions the client emits when a
// creature is under the cursor) and asserted against the live sim.
//
// The packet is written through ftest_packet_inject.h, which applies it after input() and
// before process_packets() -- an ftest action cannot otherwise set the ambient fields, see
// that header. Every verb is two steps on purpose: turn T queues PckA_SetPlyrState, turn T+1
// (or later) queues the click, because the work_state must already be current when the
// click is dispatched. Results are asserted one turn after the packet is dispatched.
//
// Uses the local human's seat (PLAYER0) on map00011 ("Hearth"), which makes POWER_HAND and
// POWER_SLAP available. Trap/door stock is granted directly; the point is the packet path,
// not the workshop.
#include "ftest_ai_gesture_single_turn.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"
#include "../ftest_packet_inject.h"
#include "../ftest_packet_capture.h"

#include "game_legacy.h"
#include "config_keeperfx.h"
#include "config_creature.h"
#include "config_trapdoor.h"
#include "config_players.h"
#include "config_magic.h"
#include "creature_control.h"
#include "dungeon_data.h"
#include "packet_data.h"
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

#define OFFSET_STL 3

// Every verb is checked even if an earlier one fails, so one run reports which gestures work.
static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); ftest_packet_capture_dump(); } while (0)

struct ftest_ai_gesture__variables
{
    MapSubtlCoord heart_stl_x, heart_stl_y;
    ThingIndex crtr_idx;
    int64_t slaps_before;
    ThingModel trap_model;
    ThingModel door_model;
    MapSubtlCoord trap_stl_x, trap_stl_y;
    MapSubtlCoord door_stl_x, door_stl_y;
    MapSubtlCoord drop_stl_x, drop_stl_y;
    MapSubtlCoord cast_stl_x, cast_stl_y;
};
static struct ftest_ai_gesture__variables vars = {0};

static void inject_set_state(PlayerState state, int64_t param)
{
    struct FtestPacketInject r = {0};
    r.action = PckA_SetPlyrState;
    r.par1 = state;
    r.par2 = param;
    ftest_packet_inject_queue_local(&r);
}

// A subtile-targeted ambient click: position + MapCoordsValid, one PCtr_* button edge.
static void inject_ambient_click(MapSubtlCoord stl_x, MapSubtlCoord stl_y, uint64_t button_flags)
{
    struct FtestPacketInject r = {0};
    r.has_position = true;
    r.pos_x = subtile_coord_center(stl_x);
    r.pos_y = subtile_coord_center(stl_y);
    r.control_flags = button_flags;
    ftest_packet_inject_queue_local(&r);
}

static void inject_action(unsigned char action, int64_t p1, int64_t p2, int64_t p3, int64_t p4)
{
    struct FtestPacketInject r = {0};
    r.action = action;
    r.par1 = p1; r.par2 = p2; r.par3 = p3; r.par4 = p4;
    ftest_packet_inject_queue_local(&r);
}

static struct Thing* crtr(void)
{
    struct Thing* t = thing_get(vars.crtr_idx);
    return (thing_is_invalid(t) || !thing_is_creature(t)) ? NULL : t;
}

static int64_t slaps_now(void)
{
    return get_players_dungeon(get_player(PLAYER0))->lvstats.num_slaps;
}

FTestActionResult a01_setup(struct FTestActionArgs* const args);
FTestActionResult a02_slap_ambient_select(struct FTestActionArgs* const args);
FTestActionResult a03_slap_ambient_click(struct FTestActionArgs* const args);
FTestActionResult a04_slap_ambient_verify(struct FTestActionArgs* const args);
FTestActionResult a04b_wait_whip_done(struct FTestActionArgs* const args);
FTestActionResult a05_slap_action(struct FTestActionArgs* const args);
FTestActionResult a06_slap_action_verify(struct FTestActionArgs* const args);
FTestActionResult a07_hand_select(struct FTestActionArgs* const args);
FTestActionResult a08_hand_pick(struct FTestActionArgs* const args);
FTestActionResult a09_hand_verify_and_drop(struct FTestActionArgs* const args);
FTestActionResult a10_trap_select(struct FTestActionArgs* const args);
FTestActionResult a11_trap_click(struct FTestActionArgs* const args);
FTestActionResult a12_trap_verify(struct FTestActionArgs* const args);
FTestActionResult a13_door_select(struct FTestActionArgs* const args);
FTestActionResult a14_door_click(struct FTestActionArgs* const args);
FTestActionResult a15_door_verify(struct FTestActionArgs* const args);
FTestActionResult a16_cast_select(struct FTestActionArgs* const args);
FTestActionResult a17_cast_release(struct FTestActionArgs* const args);
FTestActionResult a18_cast_verify_and_end(struct FTestActionArgs* const args);

TbBool ftest_ai_gesture_single_turn_init()
{
    ftest_packet_inject_reset();
    s_failures = 0;
    ftest_append_action(a01_setup, 0, &vars);
    // Slaps are ignored for 10 turns after the last creature drop, and that counter starts at 0
    // (magic_use_power_slap_thing), so wait it out before the first slap.
    ftest_append_action(a02_slap_ambient_select, 20, &vars);
    ftest_append_action(a03_slap_ambient_click, 2, &vars);
    ftest_append_action(a04_slap_ambient_verify, 2, &vars);
    ftest_append_action(a04b_wait_whip_done, 1, &vars);
    ftest_append_action(a05_slap_action, 2, &vars);
    ftest_append_action(a06_slap_action_verify, 2, &vars);
    ftest_append_action(a07_hand_select, 2, &vars);
    ftest_append_action(a08_hand_pick, 2, &vars);
    ftest_append_action(a09_hand_verify_and_drop, 2, &vars);
    ftest_append_action(a10_trap_select, 2, &vars);
    ftest_append_action(a11_trap_click, 2, &vars);
    ftest_append_action(a12_trap_verify, 2, &vars);
    ftest_append_action(a13_door_select, 2, &vars);
    ftest_append_action(a14_door_click, 2, &vars);
    ftest_append_action(a15_door_verify, 2, &vars);
    ftest_append_action(a16_cast_select, 2, &vars);
    ftest_append_action(a17_cast_release, 2, &vars);
    ftest_append_action(a18_cast_verify_and_end, 2, &vars);
    return true;
}

FTestActionResult a01_setup(struct FTestActionArgs* const args)
{
    ftest_util_reveal_map(PLAYER0);
    struct Dungeon* dungeon = get_players_dungeon(get_player(PLAYER0));
    struct Thing* heart = thing_get(dungeon->dnheart_idx);
    if (thing_is_invalid(heart))
    {
        FTEST_FAIL_TEST("no PLAYER0 dungeon heart");
        return FTRs_Go_To_Next_Action;
    }
    vars.heart_stl_x = heart->mappos.x.stl.num;
    vars.heart_stl_y = heart->mappos.y.stl.num;

    ThingModel orc = (ThingModel)creature_model_id("ORC");
    struct Thing* c = ftest_util_create_creature(subtile_coord_center(vars.heart_stl_x - OFFSET_STL),
        subtile_coord_center(vars.heart_stl_y), PLAYER0, 9, orc);
    if (thing_is_invalid(c))
    {
        FTEST_FAIL_TEST("failed to create ORC");
        return FTRs_Go_To_Next_Action;
    }
    vars.crtr_idx = c->index;

    vars.trap_model = (ThingModel)trap_model_id("BOULDER");
    vars.door_model = (ThingModel)door_model_id("WOOD");
    if (vars.trap_model < 1 || vars.door_model < 1)
    {
        FTEST_FAIL_TEST("trap/door model lookup failed (trap=%" PRId64 " door=%" PRId64 ")", (int64_t)vars.trap_model, (int64_t)vars.door_model);
        return FTRs_Go_To_Next_Action;
    }
    set_trap_buildable_and_add_to_amount(PLAYER0, vars.trap_model, 1, 2);
    set_door_buildable_and_add_to_amount(PLAYER0, vars.door_model, 1, 2);

    vars.trap_stl_x = vars.heart_stl_x;
    vars.trap_stl_y = vars.heart_stl_y + 6;
    vars.door_stl_x = vars.heart_stl_x + 12;
    vars.door_stl_y = vars.heart_stl_y;
    // Call to Arms is used as the subtile-targeted power: it is unconditional to observe (the
    // dungeon records the flag position) and shares its dispatch case with the other
    // PSt_CastPowerOnSubtile powers.
    dungeon->magic_level[PwrK_CALL2ARMS] = 1;
    dungeon->total_money_owned += 5000;
    vars.cast_stl_x = vars.heart_stl_x;
    vars.cast_stl_y = vars.heart_stl_y - 6;
    vars.drop_stl_x = vars.heart_stl_x - OFFSET_STL;
    vars.drop_stl_y = vars.heart_stl_y + OFFSET_STL;

    ftest_util_move_camera_to_thing(heart, PLAYER0);
    ftest_packet_capture_begin();
    return FTRs_Go_To_Next_Action;
}

// --- slap, ambient form: PSt_Slap, then pos_x/pos_y + PCtr_LBtnRelease over the creature ---
FTestActionResult a02_slap_ambient_select(struct FTestActionArgs* const args)
{
    if (crtr() == NULL) { FTEST_FAIL_TEST("creature gone"); return FTRs_Go_To_Next_Action; }
    vars.slaps_before = slaps_now();
    inject_set_state(PSt_Slap, 0);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult a03_slap_ambient_click(struct FTestActionArgs* const args)
{
    struct Thing* c = crtr();
    if (c == NULL) { FTEST_FAIL_TEST("creature gone"); return FTRs_Go_To_Next_Action; }
    if (get_player(PLAYER0)->work_state != PSt_Slap)
    {
        SOFT_FAIL("PckA_SetPlyrState(PSt_Slap) did not take: work_state=%" PRId64, (int64_t)get_player(PLAYER0)->work_state);
        return FTRs_Go_To_Next_Action;
    }
    inject_ambient_click(c->mappos.x.stl.num, c->mappos.y.stl.num, PCtr_LBtnRelease);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult a04_slap_ambient_verify(struct FTestActionArgs* const args)
{
    // A slap is a player instance (PI_Whip) on the creature, counted in lvstats.num_slaps;
    // it is not an immediate health change.
    FTESTLOG("ambient slap: num_slaps %" PRId64 " -> %" PRId64 ", player instance %" PRId64,
        (int64_t)vars.slaps_before, (int64_t)slaps_now(), (int64_t)get_player(PLAYER0)->instance_num);
    if (slaps_now() <= vars.slaps_before)
        SOFT_FAIL("ambient PSt_Slap + LBtnRelease did not slap the creature");
    return FTRs_Go_To_Next_Action;
}

// A second slap is ignored while the whip instance from the first is still running.
FTestActionResult a04b_wait_whip_done(struct FTestActionArgs* const args)
{
    if (get_player(PLAYER0)->instance_num == PI_Whip)
    {
        if (get_gameturn() > args->intended_start_at_game_turn + 100)
        {
            SOFT_FAIL("PI_Whip instance never finished");
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }
    return FTRs_Go_To_Next_Action;
}

// --- slap, action form: what the client emits when a creature is under the cursor ---
FTestActionResult a05_slap_action(struct FTestActionArgs* const args)
{
    struct Thing* c = crtr();
    if (c == NULL) { FTEST_FAIL_TEST("creature gone"); return FTRs_Go_To_Next_Action; }
    vars.slaps_before = slaps_now();
    inject_action(PckA_UsePwrOnThing, PwrK_SLAP, c->index, 0, 0);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult a06_slap_action_verify(struct FTestActionArgs* const args)
{
    FTESTLOG("action slap: num_slaps %" PRId64 " -> %" PRId64, (int64_t)vars.slaps_before, (int64_t)slaps_now());
    if (slaps_now() <= vars.slaps_before)
        SOFT_FAIL("PckA_UsePwrOnThing(PwrK_SLAP) did not slap the creature");
    return FTRs_Go_To_Next_Action;
}

// --- hand: pick with PckA_UsePwrHandPick(thing), drop with PckA_UsePwrHandDrop(x, y) ---
FTestActionResult a07_hand_select(struct FTestActionArgs* const args)
{
    if (crtr() == NULL) { FTEST_FAIL_TEST("creature gone"); return FTRs_Go_To_Next_Action; }
    inject_set_state(PSt_CtrlDungeon, 0);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult a08_hand_pick(struct FTestActionArgs* const args)
{
    struct Thing* c = crtr();
    if (c == NULL) { FTEST_FAIL_TEST("creature gone"); return FTRs_Go_To_Next_Action; }
    inject_action(PckA_UsePwrHandPick, c->index, 0, 0, 0);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult a09_hand_verify_and_drop(struct FTestActionArgs* const args)
{
    struct Thing* c = crtr();
    if (c == NULL) { FTEST_FAIL_TEST("creature gone"); return FTRs_Go_To_Next_Action; }
    if (!thing_is_picked_up(c))
    {
        SOFT_FAIL("PckA_UsePwrHandPick did not pick the creature up");
        return FTRs_Go_To_Next_Action;
    }
    inject_action(PckA_UsePwrHandDrop, vars.drop_stl_x, vars.drop_stl_y, 0, 0);
    return FTRs_Go_To_Next_Action;
}

// --- trap: PckA_SetPlyrState(PSt_PlaceTrap, model), then ambient position + PCtr_LBtnClick ---
FTestActionResult a10_trap_select(struct FTestActionArgs* const args)
{
    struct Thing* c = crtr();
    if (c == NULL) { FTEST_FAIL_TEST("creature gone"); return FTRs_Go_To_Next_Action; }
    if (thing_is_picked_up(c) || c->mappos.x.stl.num != vars.drop_stl_x || c->mappos.y.stl.num != vars.drop_stl_y)
    {
        SOFT_FAIL("PckA_UsePwrHandDrop did not put the creature at (%" PRId64 ",%" PRId64 "): held=%" PRId64 " at (%" PRId64 ",%" PRId64 ")",
            (int64_t)vars.drop_stl_x, (int64_t)vars.drop_stl_y, (int64_t)thing_is_picked_up(c),
            (int64_t)c->mappos.x.stl.num, (int64_t)c->mappos.y.stl.num);
    }
    ftest_util_replace_slabs(subtile_slab(vars.trap_stl_x), subtile_slab(vars.trap_stl_y),
        subtile_slab(vars.trap_stl_x), subtile_slab(vars.trap_stl_y), SlbT_CLAIMED, PLAYER0);
    inject_set_state(PSt_PlaceTrap, vars.trap_model);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult a11_trap_click(struct FTestActionArgs* const args)
{
    if (get_player(PLAYER0)->work_state != PSt_PlaceTrap)
    {
        SOFT_FAIL("PckA_SetPlyrState(PSt_PlaceTrap) did not take: work_state=%" PRId64, (int64_t)get_player(PLAYER0)->work_state);
        return FTRs_Go_To_Next_Action;
    }
    inject_ambient_click(vars.trap_stl_x, vars.trap_stl_y, PCtr_LBtnClick);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult a12_trap_verify(struct FTestActionArgs* const args)
{
    struct Thing* t = get_trap_for_position(slab_subtile_center(subtile_slab(vars.trap_stl_x)), slab_subtile_center(subtile_slab(vars.trap_stl_y)));
    if (thing_is_invalid(t) || !thing_is_deployed_trap(t) || t->model != vars.trap_model || t->owner != PLAYER0)
        SOFT_FAIL("ambient PSt_PlaceTrap + LBtnClick did not deploy a PLAYER0 trap at the slab");
    return FTRs_Go_To_Next_Action;
}

// --- door: PckA_SetPlyrState(PSt_PlaceDoor, model), then ambient position + PCtr_LBtnClick ---
FTestActionResult a13_door_select(struct FTestActionArgs* const args)
{
    // A door needs a claimed slab with wall on two opposite sides (find_door_angle()): claim a
    // three-slab corridor east-west and wall in its north and south.
    const MapSlabCoord dsx = subtile_slab(vars.door_stl_x), dsy = subtile_slab(vars.door_stl_y);
    ftest_util_replace_slabs(dsx - 1, dsy, dsx + 1, dsy, SlbT_CLAIMED, PLAYER0);
    ftest_util_replace_slabs(dsx - 1, dsy - 1, dsx + 1, dsy - 1, SlbT_WALLDRAPE, PLAYER_NEUTRAL);
    ftest_util_replace_slabs(dsx - 1, dsy + 1, dsx + 1, dsy + 1, SlbT_WALLDRAPE, PLAYER_NEUTRAL);
    inject_set_state(PSt_PlaceDoor, vars.door_model);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult a14_door_click(struct FTestActionArgs* const args)
{
    if (get_player(PLAYER0)->work_state != PSt_PlaceDoor)
    {
        SOFT_FAIL("PckA_SetPlyrState(PSt_PlaceDoor) did not take: work_state=%" PRId64, (int64_t)get_player(PLAYER0)->work_state);
        return FTRs_Go_To_Next_Action;
    }
    FTESTLOG("door slab: kind=%" PRId64 " owner=%" PRId64 " door angle=%" PRId64 ", door stock placeable=%" PRId64,
        (int64_t)get_slabmap_block(subtile_slab(vars.door_stl_x), subtile_slab(vars.door_stl_y))->kind,
        (int64_t)slabmap_owner(get_slabmap_block(subtile_slab(vars.door_stl_x), subtile_slab(vars.door_stl_y))),
        (int64_t)find_door_angle(vars.door_stl_x, vars.door_stl_y, PLAYER0),
        (int64_t)get_players_dungeon(get_player(PLAYER0))->mnfct_info.door_amount_placeable[vars.door_model]);
    inject_ambient_click(vars.door_stl_x, vars.door_stl_y, PCtr_LBtnClick);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult a15_door_verify(struct FTestActionArgs* const args)
{
    struct Thing* d = get_door_for_position(slab_subtile_center(subtile_slab(vars.door_stl_x)), slab_subtile_center(subtile_slab(vars.door_stl_y)));
    if (thing_is_invalid(d) || !thing_is_deployed_door(d) || d->model != vars.door_model || d->owner != PLAYER0)
        SOFT_FAIL("ambient PSt_PlaceDoor + LBtnClick did not deploy a PLAYER0 door at the slab");
    return FTRs_Go_To_Next_Action;
}

// --- power on a subtile: PckA_SetPlyrState(work_state, power kind), then ambient position +
// PCtr_LBtnRelease. The cast fires on release, not click. ---
FTestActionResult a16_cast_select(struct FTestActionArgs* const args)
{
    const struct PowerConfigStats* powerst = get_power_model_stats(PwrK_CALL2ARMS);
    inject_set_state(powerst->work_state, PwrK_CALL2ARMS);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult a17_cast_release(struct FTestActionArgs* const args)
{
    const struct PowerConfigStats* powerst = get_power_model_stats(PwrK_CALL2ARMS);
    if (get_player(PLAYER0)->work_state != powerst->work_state)
    {
        SOFT_FAIL("PckA_SetPlyrState(work_state %" PRId64 ", CALL2ARMS) did not take: work_state=%" PRId64,
            (int64_t)powerst->work_state, (int64_t)get_player(PLAYER0)->work_state);
        return FTRs_Go_To_Next_Action;
    }
    inject_ambient_click(vars.cast_stl_x, vars.cast_stl_y, PCtr_LBtnRelease);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult a18_cast_verify_and_end(struct FTestActionArgs* const args)
{
    const struct Dungeon* dungeon = get_players_dungeon(get_player(PLAYER0));
    if (dungeon->cta_start_turn == 0 || dungeon->cta_stl_x != vars.cast_stl_x || dungeon->cta_stl_y != vars.cast_stl_y)
        SOFT_FAIL("ambient subtile cast + LBtnRelease did not place Call to Arms at (%" PRId64 ",%" PRId64 "): start_turn=%" PRId64 " at (%" PRId64 ",%" PRId64 ")",
            (int64_t)vars.cast_stl_x, (int64_t)vars.cast_stl_y, (int64_t)dungeon->cta_start_turn,
            (int64_t)dungeon->cta_stl_x, (int64_t)dungeon->cta_stl_y);
    ftest_packet_capture_end();
    if (s_failures > 0)
    {
        FTEST_FAIL_TEST("%" PRId64 " gesture check(s) failed", s_failures);
        return FTRs_Go_To_Next_Action;
    }
    FTESTLOG("Test passed: slap (ambient + action), hand pick/drop, trap, door and subtile power cast driven through injected packets");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
