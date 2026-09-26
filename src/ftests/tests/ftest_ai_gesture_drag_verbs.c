// Phase T0b of docs/refactor/AI/LLM/01-integration-plan.md section 7: ground truth for the multi-turn verbs -- build a
// room (drag rectangle, and box click), mark slabs for digging (drag rectangle, and the brush path), sell (drag
// rectangle over traps, and a single click) and a held-power overcharge -- driven on an External seat as the exact
// per-turn packet sequences a human's mouse produces, through ftest_packet_inject.h, and asserted against the live sim.
// docs/refactor/AI/LLM/04-seat-and-action-api.md section 2.1-2.3 and 2.6 described these sequences from reading the
// code; this is where they meet the engine. Runs before, and independent of, the sequencer that will reproduce them.
//
// What it found (04 section 2.11): room drag and box work; the pickaxe "drag rectangle" is really a path brush, and the
// brush itself (including a one-turn sweep and a serpentine over several rows) tags exactly what it crosses; a sell drag
// sells every item in the rectangle, in either direction (it once appeared to skip items: the checks stopped one turn
// before the last slab, and a second drag reused the first drag's start slab, see get_dungeon_sell_user_roomspace());
// overcharge is (held turns / 4). The pickaxe drag remains asserted as a quirk: if the engine is fixed the test fails
// and the verbs and docs should be revisited.
#include "ftest_ai_gesture_drag_verbs.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"
#include "../ftest_packet_inject.h"

#include "frontend.h"
#include "game_legacy.h"
#include "net_game.h"
#include "config_keeperfx.h"
#include "config_magic.h"
#include "config_players.h"
#include "config_terrain.h"
#include "config_trapdoor.h"
#include "dungeon_data.h"
#include "map_data.h"
#include "packet_data.h"
#include "player_data.h"
#include "player_instances.h"
#include "roomspace.h"
#include "power_process.h"
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
#define QUIRK(...) FTESTLOG("known engine quirk: " __VA_ARGS__)

#define CLICK (PCtr_LBtnClick | PCtr_LBtnHeld) /* the frame the button goes down: the driver reports both */
#define HELD  (PCtr_LBtnHeld)
#define REL   (PCtr_LBtnRelease)

// ---- a tiny per-turn script: each ftest turn injects the next step ------------------------------------------
struct Step { unsigned char action; int64_t p1, p2; TbBool pos; MapSlabCoord sx, sy; unsigned char ctx; uint64_t ctrl; };
static PlayerNumber P = -1; /* the External seat the gestures run on */
static NetUserId U = -1;
static struct Step s_script[96];
static int64_t s_n = 0, s_i = 0;
static struct Dungeon* dg(void) { return get_players_dungeon(get_player(P)); }

static void script_reset(void) { s_n = 0; s_i = 0; }
static void S_act(unsigned char a, int64_t p1, int64_t p2) { struct Step s = {0}; s.action = a; s.p1 = p1; s.p2 = p2; s_script[s_n++] = s; }
static void S_pos(MapSlabCoord sx, MapSlabCoord sy, unsigned char ctx, uint64_t ctrl) { struct Step s = {0}; s.pos = true; s.sx = sx; s.sy = sy; s.ctx = ctx; s.ctrl = ctrl; s_script[s_n++] = s; }
static void S_idle(void) { struct Step s = {0}; s_script[s_n++] = s; }

static TbBool s_trace = false;
static FTestActionResult run_script(struct FTestActionArgs* const args)
{
    if (s_trace) { const struct PlayerInfo* pl = get_player(P); const struct UserState* us = get_user_state(U);
        FTESTLOG("  trace before step %" PRId64 ": work_state=%" PRId64 " mode=%" PRId64 " width=%" PRId64 " boxsize=%" PRId64 " drag_mode=%" PRId64 " expand=%" PRId64 " power=%" PRId64 " cta_start=%" PRId64, (int64_t)s_i, (int64_t)pl->work_state, (int64_t)pl->roomspace_mode, (int64_t)pl->roomspace_width, (int64_t)us->boxsize, (int64_t)pl->render_roomspace.drag_mode, (int64_t)pl->cast_expand_level, (int64_t)us->chosen_power_kind, (int64_t)dg()->cta_start_turn); }
    if (s_i >= s_n) return FTRs_Go_To_Next_Action;
    const struct Step* s = &s_script[s_i++];
    struct FtestPacketInject r = {0};
    r.action = s->action; r.par1 = s->p1; r.par2 = s->p2;
    if (s->pos) { r.has_position = true; r.pos_x = subtile_coord_center(slab_subtile_center(s->sx)); r.pos_y = subtile_coord_center(slab_subtile_center(s->sy)); r.context = s->ctx; r.control_flags = s->ctrl; }
    ftest_packet_inject_queue(U, &r);
    return FTRs_Repeat_Current_Action;
}

// ---- scene ---------------------------------------------------------------------------------------------------
static MapSlabCoord hsx, hsy;
static RoomKind treasure;
static ThingModel trap_model;

// Rectangles, in slabs, relative to the player's heart.
#define R1_X0 (hsx + 3)
#define R1_Y0 (hsy - 1)
#define R1_W 4
#define R1_H 3
#define R2_X0 (hsx + 9)
#define R2_Y0 (hsy - 1)
#define D1_X0 (hsx - 9)
#define D1_Y0 (hsy - 1)
#define D1_W 4
#define D1_H 3
#define D2_Y (hsy + 4)
#define D2_X0 (hsx - 9)
#define D2_LEN 5
#define S_Y (hsy + 7)

static TbBool tagged(MapSlabCoord sx, MapSlabCoord sy) { return find_from_task_list(P, get_subtile_number(slab_subtile_center(sx), slab_subtile_center(sy))) != -1; }

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

FTestActionResult t01_setup(struct FTestActionArgs* const args);
FTestActionResult t02_room_drag_script(struct FTestActionArgs* const args);
FTestActionResult t03_room_drag_verify(struct FTestActionArgs* const args);
FTestActionResult t04_room_box_script(struct FTestActionArgs* const args);
FTestActionResult t05_room_box_verify(struct FTestActionArgs* const args);
FTestActionResult t06_dig_drag_script(struct FTestActionArgs* const args);
FTestActionResult t07_dig_drag_verify(struct FTestActionArgs* const args);
FTestActionResult t08_dig_brush_script(struct FTestActionArgs* const args);
FTestActionResult t09_dig_brush_verify(struct FTestActionArgs* const args);
FTestActionResult t09b_dig_serpentine_script(struct FTestActionArgs* const args);
FTestActionResult t09b_dig_serpentine_verify(struct FTestActionArgs* const args);
FTestActionResult t10_sell_drag_script(struct FTestActionArgs* const args);
FTestActionResult t11_sell_drag_verify(struct FTestActionArgs* const args);
FTestActionResult t11b_sell_past_script(struct FTestActionArgs* const args);
FTestActionResult t11b_sell_past_verify(struct FTestActionArgs* const args);
FTestActionResult t11c_sell_reverse_script(struct FTestActionArgs* const args);
FTestActionResult t11c_sell_reverse_verify(struct FTestActionArgs* const args);
FTestActionResult t12_sell_click_script(struct FTestActionArgs* const args);
FTestActionResult t13_sell_click_verify(struct FTestActionArgs* const args);
FTestActionResult t14_overcharge_short_script(struct FTestActionArgs* const args);
FTestActionResult t15_overcharge_short_verify(struct FTestActionArgs* const args);
FTestActionResult t16_overcharge_long_script(struct FTestActionArgs* const args);
FTestActionResult t17_overcharge_long_verify_and_end(struct FTestActionArgs* const args);

FTestActionResult run(struct FTestActionArgs* const args) { return run_script(args); }

void ftest_ai_gesture_drag_verbs_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_gesture_drag_verbs_init()
{
    ftest_packet_inject_reset();
    s_failures = 0;
    ftest_append_action(t01_setup, 0, NULL);
    ftest_append_action(t02_room_drag_script, 5, NULL);   ftest_append_action(run, 1, NULL);  ftest_append_action(t03_room_drag_verify, 1, NULL);
    ftest_append_action(t04_room_box_script, 1, NULL);    ftest_append_action(run, 1, NULL);  ftest_append_action(t05_room_box_verify, 1, NULL);
    ftest_append_action(t06_dig_drag_script, 1, NULL);    ftest_append_action(run, 1, NULL);  ftest_append_action(t07_dig_drag_verify, 1, NULL);
    ftest_append_action(t08_dig_brush_script, 1, NULL);   ftest_append_action(run, 1, NULL);  ftest_append_action(t09_dig_brush_verify, 1, NULL);
    ftest_append_action(t09b_dig_serpentine_script, 1, NULL); ftest_append_action(run, 1, NULL);  ftest_append_action(t09b_dig_serpentine_verify, 1, NULL);
    ftest_append_action(t10_sell_drag_script, 1, NULL);   ftest_append_action(run, 1, NULL);  ftest_append_action(t11_sell_drag_verify, 1, NULL);
    ftest_append_action(t11b_sell_past_script, 1, NULL);  ftest_append_action(run, 1, NULL);  ftest_append_action(t11b_sell_past_verify, 1, NULL);
    ftest_append_action(t11c_sell_reverse_script, 1, NULL);  ftest_append_action(run, 1, NULL);  ftest_append_action(t11c_sell_reverse_verify, 1, NULL);
    ftest_append_action(t12_sell_click_script, 1, NULL);  ftest_append_action(run, 1, NULL);  ftest_append_action(t13_sell_click_verify, 1, NULL);
    ftest_append_action(t14_overcharge_short_script, 1, NULL); ftest_append_action(run, 1, NULL);  ftest_append_action(t15_overcharge_short_verify, 1, NULL);
    ftest_append_action(t16_overcharge_long_script, 1, NULL);  ftest_append_action(run, 1, NULL);  ftest_append_action(t17_overcharge_long_verify_and_end, 1, NULL);
    return true;
}

// ---- scene ---------------------------------------------------------------------------------------------------
FTestActionResult t01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0) { FTEST_FAIL_TEST("no second keeper"); return FTRs_Go_To_Next_Action; }
    U = net_add_external_seat(P);
    if (U < 1) { FTEST_FAIL_TEST("net_add_external_seat failed"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    struct Thing* heart = find_players_dungeon_heart(P);
    if (thing_is_invalid(heart)) { FTEST_FAIL_TEST("no heart"); return FTRs_Go_To_Next_Action; }
    hsx = subtile_slab(heart->mappos.x.stl.num); hsy = subtile_slab(heart->mappos.y.stl.num);
    treasure = (RoomKind)get_rid(room_desc, "TREASURE");
    trap_model = (ThingModel)trap_model_id("BOULDER");
    struct Dungeon* d = dg();
    d->total_money_owned = 100000;
    d->room_buildable[treasure] |= 1;
    d->magic_level[PwrK_CALL2ARMS] = 1;
    set_trap_buildable_and_add_to_amount(P, trap_model, 1, 8);

    ftest_util_replace_slabs(R1_X0 - 1, R1_Y0 - 1, R1_X0 + R1_W, R1_Y0 + R1_H, SlbT_CLAIMED, P);   /* room drag rectangle, with a claimed margin */
    ftest_util_replace_slabs(R2_X0 - 1, R1_Y0 - 1, R2_X0 + 2, R1_Y0 + 2, SlbT_CLAIMED, P);          /* room box click */
    ftest_util_replace_slabs(D1_X0, D1_Y0, D1_X0 + D1_W - 1, D1_Y0 + D1_H - 1, SlbT_EARTH, PLAYER_NEUTRAL);
    ftest_util_replace_slabs(D2_X0, D2_Y, D2_X0 + D2_LEN - 1, D2_Y, SlbT_EARTH, PLAYER_NEUTRAL);
    ftest_util_replace_slabs(hsx - 2, S_Y, hsx + 6, S_Y, SlbT_CLAIMED, P);                            /* the row the traps are sold from */
    ftest_util_move_camera_to_thing(heart, P);
    FTESTLOG("scene: heart slab (%" PRId64 ",%" PRId64 "), treasure room kind %" PRId64, (int64_t)hsx, (int64_t)hsy, (int64_t)treasure);
    return FTRs_Go_To_Next_Action;
}

// ---- build room, drag rectangle -------------------------------------------------------------------------------------
FTestActionResult t02_room_drag_script(struct FTestActionArgs* const args)
{
    script_reset();
    S_act(PckA_SetPlyrState, PSt_BuildRoom, treasure);
    S_act(PckA_SetRoomspaceDrag, 0, 0);
    S_idle();
    S_pos(R1_X0, R1_Y0, 0, CLICK);
    S_pos(R1_X0 + 2, R1_Y0, 0, HELD);
    S_pos(R1_X0 + 3, hsy, 0, HELD);
    S_pos(R1_X0 + 3, R1_Y0 + 2, 0, HELD);
    S_pos(R1_X0 + 3, R1_Y0 + 2, 0, REL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult t03_room_drag_verify(struct FTestActionArgs* const args)
{
    const int64_t want = R1_W * R1_H;
    const int64_t built = room_slabs_in(R1_X0, R1_Y0, R1_W, R1_H);
    if (built < want && get_gameturn() < args->intended_start_at_game_turn + 60) return FTRs_Repeat_Current_Action;
    FTESTLOG("room drag: %" PRId64 " of %" PRId64 " slabs built, %" PRId64 " turns after the release", built, want, (int64_t)(get_gameturn() - args->intended_start_at_game_turn));
    if (built != want) SOFT_FAIL("dragging a %dx%d rectangle in PckA_SetRoomspaceDrag mode did not build it", R1_W, R1_H);
    return FTRs_Go_To_Next_Action;
}

// ---- build room, box click --------------------------------------------------------------------------------------------
FTestActionResult t04_room_box_script(struct FTestActionArgs* const args)
{
    s_trace = true;
    script_reset();
    S_act(PckA_SetPlyrState, PSt_BuildRoom, treasure);
    S_act(PckA_SetRoomspaceMan, 2, 0);
    S_idle();
    S_pos(R2_X0, R1_Y0, 0, CLICK);
    S_pos(R2_X0, R1_Y0, 0, REL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult t05_room_box_verify(struct FTestActionArgs* const args)
{
    s_trace = false;
    const int64_t nbuilt = room_slabs_in(R2_X0 - 1, R1_Y0 - 1, 4, 4);
    if (nbuilt <= 3 && get_gameturn() < args->intended_start_at_game_turn + 40) return FTRs_Repeat_Current_Action;
    FTESTLOG("room box (size 2): %" PRId64 " slab(s) built around the click at (%" PRId64 ",%" PRId64 ")", nbuilt, (int64_t)(R2_X0), (int64_t)(R1_Y0));
    {
        const struct PlayerInfo* pl = get_player(P);
        const struct UserState* us = get_user_state(U);
        FTESTLOG("  state: work_state=%" PRId64 " roomspace_mode=%" PRId64 " width=%" PRId64 " height=%" PRId64 " boxsize=%" PRId64 " drag_mode=%" PRId64 " roomspace.active=%" PRId64 " cursor_button_down=%" PRId64,
            (int64_t)pl->work_state, (int64_t)pl->roomspace_mode, (int64_t)pl->roomspace_width, (int64_t)pl->roomspace_height, (int64_t)us->boxsize, (int64_t)pl->render_roomspace.drag_mode, (int64_t)pl->roomspace.is_active, (int64_t)us->cursor_button_down);
    }
    for (int dy = -1; dy <= 1; dy++)
    {
        char row[4];
        for (int dx = -1; dx <= 1; dx++)
        {
            const struct SlabMap* slb = get_slabmap_block(R2_X0 + dx, R1_Y0 + dy);
            row[dx + 1] = (slb->room_index > 0) ? 'R' : (slb->kind == SlbT_CLAIMED ? 'c' : '?');
        }
        row[3] = 0;
        FTESTLOG("  %s", row);
    }
    if (nbuilt != 4) SOFT_FAIL("a click in box mode of size 2 did not build 4 slabs");
    if (room_slabs_in(R2_X0, R1_Y0, 2, 2) != 4) SOFT_FAIL("the box did not extend right and down from the click");
    return FTRs_Go_To_Next_Action;
}

// ---- dig, drag rectangle with the pickaxe cursor -----------------------------------------------------------------------
FTestActionResult t06_dig_drag_script(struct FTestActionArgs* const args)
{
    script_reset();
    S_act(PckA_SetPlyrState, PSt_CtrlDungeon, 0);
    S_act(PckA_SetRoomspaceDrag, 0, 0);
    S_idle();
    S_pos(D1_X0, D1_Y0, 1, CLICK);
    S_pos(D1_X0 + 2, D1_Y0, 1, HELD);
    S_pos(D1_X0 + 3, hsy, 1, HELD);
    S_pos(D1_X0 + 3, D1_Y0 + 2, 1, HELD);
    S_pos(D1_X0 + 3, D1_Y0 + 2, 1, REL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult t07_dig_drag_verify(struct FTestActionArgs* const args)
{
    int64_t n = 0;
    for (int dx = 0; dx < D1_W; dx++) for (int dy = 0; dy < D1_H; dy++) if (tagged(D1_X0 + dx, D1_Y0 + dy)) n++;
    FTESTLOG("dig drag: %" PRId64 " of %d slabs tagged", n, D1_W * D1_H);
    for (int dy = 0; dy < D1_H; dy++)
    {
        char row[D1_W + 1];
        for (int dx = 0; dx < D1_W; dx++) row[dx] = tagged(D1_X0 + dx, D1_Y0 + dy) ? 'T' : '.';
        row[D1_W] = 0;
        FTESTLOG("  %s", row);
    }
    // Known quirk: with the pickaxe cursor, PckA_SetRoomspaceDrag does not fill the rectangle -- it tags the path the cursor
    // took, so the sequence of positions matters. The mark_dig verb sweeps rows with a 1x1 brush instead.
    if (n == D1_W * D1_H) SOFT_FAIL("the pickaxe drag now fills the rectangle: the engine changed; revisit the mark_dig verb (it sweeps rows instead)");
    else QUIRK("the pickaxe drag tagged %" PRId64 " of %d slabs (a path, not a fill)", n, D1_W * D1_H);
    return FTRs_Go_To_Next_Action;
}

// ---- dig, 1x1 brush swept along a row in one turn ------------------------------------------------------------------------
FTestActionResult t08_dig_brush_script(struct FTestActionArgs* const args)
{
    script_reset();
    S_act(PckA_SetPlyrState, PSt_CtrlDungeon, 0);
    S_act(PckA_SetRoomspaceMan, 1, 0);
    S_idle();
    S_pos(D2_X0, D2_Y, 1, CLICK);
    S_pos(D2_X0 + 4, D2_Y, 1, HELD);
    S_pos(D2_X0 + 4, D2_Y, 1, REL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult t09_dig_brush_verify(struct FTestActionArgs* const args)
{
    int64_t n = 0;
    for (int i = 0; i < D2_LEN; i++) if (tagged(D2_X0 + i, D2_Y)) n++;
    FTESTLOG("dig brush: %" PRId64 " of %d slabs tagged after a one-turn sweep", n, D2_LEN);
    if (n != D2_LEN) SOFT_FAIL("a 1x1 brush swept along a row in one turn did not tag all %d slabs (interpolation)", D2_LEN);
    return FTRs_Go_To_Next_Action;
}

// ---- dig, serpentine brush over several rows -----------------------------------------------------------------------------
FTestActionResult t09b_dig_serpentine_script(struct FTestActionArgs* const args)
{
    const MapSlabCoord y0 = hsy - 7;
    ftest_util_replace_slabs(D1_X0, y0, D1_X0 + D1_W - 1, y0 + D1_H - 1, SlbT_EARTH, PLAYER_NEUTRAL);
    script_reset();
    S_act(PckA_SetPlyrState, PSt_CtrlDungeon, 0);
    S_act(PckA_SetRoomspaceMan, 1, 0);
    S_idle();
    S_pos(D1_X0, y0, 1, CLICK);
    MapSlabCoord prev_x = D1_X0;
    for (int r = 0; r < D1_H; r++)
    {
        const MapSlabCoord y = y0 + r;
        if (r > 0) S_pos(prev_x, y, 1, HELD); /* step down onto the next row */
        const MapSlabCoord x = (r % 2 == 0) ? (D1_X0 + D1_W - 1) : D1_X0;
        S_pos(x, y, 1, HELD);
        prev_x = x;
    }
    S_pos(prev_x, y0 + D1_H - 1, 1, REL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult t09b_dig_serpentine_verify(struct FTestActionArgs* const args)
{
    int64_t n = 0;
    for (int dx = 0; dx < D1_W; dx++) for (int dy = 0; dy < D1_H; dy++) if (tagged(D1_X0 + dx, hsy - 7 + dy)) n++;
    FTESTLOG("dig serpentine: %" PRId64 " of %d slabs tagged", n, D1_W * D1_H);
    if (n != D1_W * D1_H) SOFT_FAIL("a serpentine brush over %dx%d slabs did not tag them all", D1_W, D1_H);
    return FTRs_Go_To_Next_Action;
}

static int64_t s_sell_deadline = 0; /* turn after which the sell checks give up waiting */

// ---- sell, drag rectangle over three traps ---------------------------------------------------------------------------
static TbBool trap_at(MapSlabCoord sx) { return !thing_is_invalid(get_trap_for_position(slab_subtile_center(sx), slab_subtile_center(S_Y))); }

FTestActionResult t10_sell_drag_script(struct FTestActionArgs* const args)
{
    s_sell_deadline = get_gameturn() + 60;
    for (int i = -1; i <= 1; i++) player_place_trap_at_subtile_without_check(slab_subtile_center(hsx + i), slab_subtile_center(S_Y), P, trap_model, true);
    if (!trap_at(hsx - 1) || !trap_at(hsx) || !trap_at(hsx + 1)) SOFT_FAIL("could not place the three traps to sell");
    script_reset();
    S_act(PckA_SetPlyrState, PSt_Sell, 0);
    S_act(PckA_SetRoomspaceDrag, 0, 0);
    S_idle();
    S_pos(hsx - 1, S_Y, 0, CLICK);
    S_pos(hsx, S_Y, 0, HELD);
    S_pos(hsx + 1, S_Y, 0, HELD);
    S_pos(hsx + 1, S_Y, 0, REL);
    return FTRs_Go_To_Next_Action;
}

// A sell drag sells one slab per turn from the roomspace's update, so poll until the sweep has finished. (An earlier
// version stopped polling at a fixed turn after the action began, which was one turn short of the last slab.)
static TbBool sell_sweep_pending(void);

FTestActionResult t11_sell_drag_verify(struct FTestActionArgs* const args)
{
    s_trace = false;
    if (sell_sweep_pending()) return FTRs_Repeat_Current_Action;
    const int64_t left = trap_at(hsx - 1) + trap_at(hsx) + trap_at(hsx + 1);
    FTESTLOG("sell drag: %" PRId64 " of 3 traps left (west, middle, east: %d%d%d)", left, (int)trap_at(hsx - 1), (int)trap_at(hsx), (int)trap_at(hsx + 1));
    if (left != 0) SOFT_FAIL("a forward sell drag left %" PRId64 " of 3 traps", left);
    return FTRs_Go_To_Next_Action;
}


static void place_row_traps(void) { for (int i = -1; i <= 1; i++) if (!trap_at(hsx + i)) player_place_trap_at_subtile_without_check(slab_subtile_center(hsx + i), slab_subtile_center(S_Y), P, trap_model, true); }
static int64_t traps_left(void) { return trap_at(hsx - 1) + trap_at(hsx) + trap_at(hsx + 1); }
static TbBool sell_sweep_pending(void) { return (traps_left() > 0 || get_player(P)->roomspace.is_active) && get_gameturn() < s_sell_deadline; }

// Variant B: the same drag, but the cursor goes one slab past the last trap (an empty slab) before the release.
FTestActionResult t11b_sell_past_script(struct FTestActionArgs* const args)
{
    s_sell_deadline = get_gameturn() + 60;
    place_row_traps();
    script_reset();
    S_act(PckA_SetPlyrState, PSt_Sell, 0);
    S_act(PckA_SetRoomspaceDrag, 0, 0);
    S_idle();
    S_pos(hsx - 1, S_Y, 0, CLICK);
    S_pos(hsx, S_Y, 0, HELD);
    S_pos(hsx + 1, S_Y, 0, HELD);
    S_pos(hsx + 2, S_Y, 0, HELD);
    S_pos(hsx + 2, S_Y, 0, REL);
    return FTRs_Go_To_Next_Action;
}
FTestActionResult t11b_sell_past_verify(struct FTestActionArgs* const args)
{
    if (sell_sweep_pending()) return FTRs_Repeat_Current_Action;
    FTESTLOG("sell drag past the last trap: %" PRId64 " of 3 left (west, middle, east: %d%d%d)", traps_left(), (int)trap_at(hsx - 1), (int)trap_at(hsx), (int)trap_at(hsx + 1));
    if (traps_left() != 0) SOFT_FAIL("dragging past the last trap left %" PRId64 " of 3 traps", traps_left());
    return FTRs_Go_To_Next_Action;
}

// Variant C: dragging the other way, east to west.
FTestActionResult t11c_sell_reverse_script(struct FTestActionArgs* const args)
{
    s_sell_deadline = get_gameturn() + 60;
    place_row_traps();
    script_reset();
    S_act(PckA_SetPlyrState, PSt_Sell, 0);
    S_act(PckA_SetRoomspaceDrag, 0, 0);
    S_idle();
    S_pos(hsx + 1, S_Y, 0, CLICK);
    S_pos(hsx, S_Y, 0, HELD);
    S_pos(hsx - 1, S_Y, 0, HELD);
    S_pos(hsx - 1, S_Y, 0, REL);
    return FTRs_Go_To_Next_Action;
}
FTestActionResult t11c_sell_reverse_verify(struct FTestActionArgs* const args)
{
    if (sell_sweep_pending()) return FTRs_Repeat_Current_Action;
    FTESTLOG("sell drag east to west: %" PRId64 " of 3 left (west, middle, east: %d%d%d)", traps_left(), (int)trap_at(hsx - 1), (int)trap_at(hsx), (int)trap_at(hsx + 1));
    if (traps_left() != 0) SOFT_FAIL("a reverse (east to west) sell drag left %" PRId64 " of 3 traps", traps_left());
    return FTRs_Go_To_Next_Action;
}

// ---- sell, single click -------------------------------------------------------------------------------------------------
FTestActionResult t12_sell_click_script(struct FTestActionArgs* const args)
{
    player_place_trap_at_subtile_without_check(slab_subtile_center(hsx + 5), slab_subtile_center(S_Y), P, trap_model, true);
    if (!trap_at(hsx + 5)) SOFT_FAIL("could not place the trap to sell");
    script_reset();
    S_act(PckA_SetPlyrState, PSt_Sell, 0);
    S_act(PckA_SetRoomspaceMan, 1, 0);
    S_idle();
    S_pos(hsx + 5, S_Y, 0, CLICK);
    S_pos(hsx + 5, S_Y, 0, REL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult t13_sell_click_verify(struct FTestActionArgs* const args)
{
    if (trap_at(hsx + 5) && get_gameturn() < args->intended_start_at_game_turn + 20) return FTRs_Repeat_Current_Action;
    FTESTLOG("sell click: trap %s", trap_at(hsx + 5) ? "still there" : "sold");
    if (trap_at(hsx + 5)) SOFT_FAIL("a click in PSt_Sell did not sell the trap under it");
    return FTRs_Go_To_Next_Action;
}

// ---- overcharge: a power cast on release reads the charge built up while the button was held -------------------------
static int64_t s_short_level = -1;

static void overcharge_script(int held_turns)
{
    const struct PowerConfigStats* ps = get_power_model_stats(PwrK_CALL2ARMS);
    script_reset();
    S_act(PckA_PwrCTADis, 0, 0);        /* clear any Call to Arms from an earlier cast */
    S_idle();
    S_act(PckA_SetPlyrState, ps->work_state, PwrK_CALL2ARMS);
    S_idle();
    S_pos(R1_X0 + 1, R1_Y0, 0, CLICK);
    for (int i = 0; i < held_turns; i++) S_pos(R1_X0 + 1, R1_Y0, 0, HELD);
    S_pos(R1_X0 + 1, R1_Y0, 0, REL);
}

FTestActionResult t14_overcharge_short_script(struct FTestActionArgs* const args) { s_trace = false; overcharge_script(0); return FTRs_Go_To_Next_Action; }

FTestActionResult t15_overcharge_short_verify(struct FTestActionArgs* const args)
{
    s_trace = false;
    s_short_level = dg()->cta_power_level;
    FTESTLOG("overcharge: a click-release cast placed Call to Arms at level %" PRId64 " (start turn %" PRId64 ")", s_short_level, (int64_t)dg()->cta_start_turn);
    if (dg()->cta_start_turn == 0) SOFT_FAIL("the click-release cast did not place Call to Arms at all");
    return FTRs_Go_To_Next_Action;
}

FTestActionResult t16_overcharge_long_script(struct FTestActionArgs* const args) { overcharge_script(12); return FTRs_Go_To_Next_Action; }

FTestActionResult t17_overcharge_long_verify_and_end(struct FTestActionArgs* const args)
{
    const int64_t level = dg()->cta_power_level;
    FTESTLOG("overcharge: a cast after 12 held turns placed Call to Arms at level %" PRId64, level);
    if (level <= s_short_level) SOFT_FAIL("holding the button did not raise the cast level (short %" PRId64 ", long %" PRId64 ")", s_short_level, level);
    if (level != 3) SOFT_FAIL("12 held turns should give level 12/4 = 3, got %" PRId64, level);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " gesture check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: build room (drag, box), dig (drag, brush), sell (drag, click) and overcharge behave as catalogued");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
