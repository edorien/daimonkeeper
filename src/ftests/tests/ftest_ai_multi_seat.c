// Phase M5 of docs/refactor/AI/LLM/01-integration-plan.md section 7: more than one External seat in one game, and
// External seats next to a built-in AI. Runs on original-pack multiplayer map 60 (human + two rival keepers).
//
// Stage 1: only the first rival becomes a seat; the second stays under the built-in AI. The AI keeper must keep its
// CompCtrl flag, and a packet from the seat must not reach it (or the human).
// Stage 2: the second rival becomes a seat too. Each seat gets a different state packet at once, and a different
// multi-turn verb at once (seat A marks slabs for digging, seat B builds a room); each effect must land on the
// issuing seat's dungeon only, and each seat's queue must drain independently.
// Also pins the identity rules: a human cannot be claimed and a seat cannot be claimed twice.
#include "ftest_ai_multi_seat.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"
#include "../ftest_packet_inject.h"

#include "frontend.h"
#include "game_legacy.h"
#include "config_keeperfx.h"
#include "config_players.h"
#include "config_terrain.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "map_data.h"
#include "net_game.h"
#include "packet_data.h"
#include "player_data.h"
#include "room_data.h"
#include "slab_data.h"
#include "tasks_list.h"
#include "thing_list.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK_TRUE(what, cond) do { if (!(cond)) SOFT_FAIL("%s", what); } while (0)

static PlayerNumber PA = -1, PB = -1; // seat A, seat B
static NetUserId UA = -1, UB = -1;
static MapSlabCoord ax, ay, bx, by; // heart slabs
static RoomKind treasure;

#define DIG_W 4
#define DIG_H 3
#define ROOM_W 4
#define ROOM_H 3

static TbBool tagged(PlayerNumber p, MapSlabCoord sx, MapSlabCoord sy) { return find_from_task_list(p, get_subtile_number(slab_subtile_center(sx), slab_subtile_center(sy))) != -1; }
static int64_t count_tagged(PlayerNumber p, MapSlabCoord x0, MapSlabCoord y0)
{
    int64_t n = 0;
    for (int dx = 0; dx < DIG_W; dx++) for (int dy = 0; dy < DIG_H; dy++) if (tagged(p, x0 + dx, y0 + dy)) n++;
    return n;
}
static int64_t room_slabs(PlayerNumber p, MapSlabCoord x0, MapSlabCoord y0)
{
    int64_t n = 0;
    for (int dx = 0; dx < ROOM_W; dx++) for (int dy = 0; dy < ROOM_H; dy++)
    {
        const struct SlabMap* slb = get_slabmap_block(x0 + dx, y0 + dy);
        if (slb->room_index > 0 && room_get(slb->room_index)->kind == treasure && slabmap_owner(slb) == p) n++;
    }
    return n;
}
static void inject_state(NetUserId u, PlayerState st)
{
    struct FtestPacketInject r = {0};
    r.action = PckA_SetPlyrState; r.par1 = st;
    ftest_packet_inject_queue(u, &r);
}
static struct ExtSeatVerb rect_verb(enum ExtSeatVerbKind k, const char* name, int64_t x0, int64_t y0, int64_t x1, int64_t y1)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = k; v.has_rect = true; v.slab_x0 = x0; v.slab_y0 = y0; v.slab_x1 = x1; v.slab_y1 = y1;
    if (name) snprintf(v.name, sizeof(v.name), "%s", name);
    return v;
}
static void submit_ok(const char* what, NetUserId u, PlayerNumber p, struct ExtSeatVerb v)
{
    const char* e = extseat_submit_verb(u, p, &v, NULL);
    if (e) SOFT_FAIL("%s: rejected with %s", what, e);
}

FTestActionResult m01_stage1_setup(struct FTestActionArgs* const args);
FTestActionResult m02_stage1_check(struct FTestActionArgs* const args);
FTestActionResult m03_stage2_setup(struct FTestActionArgs* const args);
FTestActionResult m04_stage2_states_check(struct FTestActionArgs* const args);
FTestActionResult m05_verbs_submit(struct FTestActionArgs* const args);
FTestActionResult m06_verbs_wait(struct FTestActionArgs* const args);
FTestActionResult m07_verbs_check(struct FTestActionArgs* const args);

void ftest_ai_multi_seat_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_multi_seat_init()
{
    ftest_packet_inject_reset();
    s_failures = 0;
    ftest_append_action(m01_stage1_setup, 0, NULL);
    ftest_append_action(m02_stage1_check, 3, NULL);
    ftest_append_action(m03_stage2_setup, 1, NULL);
    ftest_append_action(m04_stage2_states_check, 3, NULL);
    ftest_append_action(m05_verbs_submit, 1, NULL);
    ftest_append_action(m06_verbs_wait, 1, NULL);
    ftest_append_action(m07_verbs_check, 1, NULL);
    return true;
}

FTestActionResult m01_stage1_setup(struct FTestActionArgs* const args)
{
    PA = PB = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
    {
        if (p == my_player_number || p == PLAYER_GOOD || p == PLAYER_NEUTRAL) continue;
        if (!player_exists(get_player(p)) || thing_is_invalid(find_players_dungeon_heart(p))) continue;
        if (PA < 0) PA = p; else if (PB < 0) PB = p;
    }
    if (PA < 0 || PB < 0) { FTEST_FAIL_TEST("need two rival keepers with hearts (map 60)"); return FTRs_Go_To_Next_Action; }
    FTESTLOG("seat A player %" PRId64 ", seat B player %" PRId64, (int64_t)PA, (int64_t)PB);
    CHECK_TRUE("both rivals start computer-controlled", flag_is_set(get_player(PA)->allocflags, PlaF_CompCtrl) && flag_is_set(get_player(PB)->allocflags, PlaF_CompCtrl));

    CHECK_TRUE("the local human cannot be claimed", net_add_external_seat(my_player_number) < 0);
    UA = net_add_external_seat(PA);
    CHECK_TRUE("seat A is user 1", UA == 1);
    CHECK_TRUE("claiming a seat twice is refused", net_add_external_seat(PA) < 0);
    CHECK_TRUE("seat A did not disturb the still-AI keeper", flag_is_set(get_player(PB)->allocflags, PlaF_CompCtrl));
    CHECK_TRUE("user 2 maps to nobody yet", get_net_user_player_number(2) < 0);

    inject_state(UA, PSt_Slap);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult m02_stage1_check(struct FTestActionArgs* const args)
{
    CHECK_TRUE("seat A's packet reached seat A", get_player(PA)->work_state == PSt_Slap);
    CHECK_TRUE("seat A's packet did not reach the AI keeper", get_player(PB)->work_state != PSt_Slap);
    CHECK_TRUE("seat A's packet did not reach the human", get_player(my_player_number)->work_state != PSt_Slap);
    CHECK_TRUE("the AI keeper is still computer-controlled", flag_is_set(get_player(PB)->allocflags, PlaF_CompCtrl));
    CHECK_TRUE("the AI keeper is not an External seat", !flag_is_set(get_player(PB)->allocflags, PlaF_ExternalSeat));
    if (s_failures > 0) FTEST_FAIL_TEST("%" PRId64 " stage-1 check(s) failed", s_failures);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult m03_stage2_setup(struct FTestActionArgs* const args)
{
    UB = net_add_external_seat(PB);
    CHECK_TRUE("seat B is user 2", UB == 2);
    CHECK_TRUE("user 1 still maps to seat A", get_net_user_player_number(UA) == PA);
    CHECK_TRUE("user 2 maps to seat B", get_net_user_player_number(UB) == PB);
    CHECK_TRUE("seat B is no longer computer-controlled", !flag_is_set(get_player(PB)->allocflags, PlaF_CompCtrl));
    CHECK_TRUE("the two seats have distinct user ids", get_player(PA)->user_id != get_player(PB)->user_id);

    get_player(PA)->work_state = PSt_CtrlDungeon;
    inject_state(UA, PSt_Slap);
    inject_state(UB, PSt_Sell);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult m04_stage2_states_check(struct FTestActionArgs* const args)
{
    CHECK_TRUE("seat A got its own state", get_player(PA)->work_state == PSt_Slap);
    CHECK_TRUE("seat B got its own state", get_player(PB)->work_state == PSt_Sell);
    CHECK_TRUE("the human got neither", get_player(my_player_number)->work_state != PSt_Slap && get_player(my_player_number)->work_state != PSt_Sell);
    if (s_failures > 0) FTEST_FAIL_TEST("%" PRId64 " stage-2 dispatch check(s) failed", s_failures);
    return FTRs_Go_To_Next_Action;
}

// Different multi-turn verbs at once. Each seat gets earth to dig and claimed floor to build on next to its own heart.
FTestActionResult m05_verbs_submit(struct FTestActionArgs* const args)
{
    ftest_util_reveal_map(PA);
    ftest_util_reveal_map(PB);
    treasure = (RoomKind)get_rid(room_desc, "TREASURE");
    const struct Thing* ha = find_players_dungeon_heart(PA);
    const struct Thing* hb = find_players_dungeon_heart(PB);
    ax = subtile_slab(ha->mappos.x.stl.num); ay = subtile_slab(ha->mappos.y.stl.num);
    bx = subtile_slab(hb->mappos.x.stl.num); by = subtile_slab(hb->mappos.y.stl.num);

    struct Dungeon* da = get_players_dungeon(get_player(PA));
    struct Dungeon* db = get_players_dungeon(get_player(PB));
    da->total_money_owned = 100000; db->total_money_owned = 100000;
    da->room_buildable[treasure] |= 1; db->room_buildable[treasure] |= 1;

    // A: dig patch 3 slabs west of its heart; B: room floor 3 slabs east of its own. Neither patch touches the other keeper.
    ftest_util_replace_slabs(ax - 9, ay - 1, ax - 9 + DIG_W - 1, ay - 1 + DIG_H - 1, SlbT_EARTH, PLAYER_NEUTRAL);
    ftest_util_replace_slabs(bx + 2, by - 2, bx + 3 + ROOM_W, by + ROOM_H - 1, SlbT_CLAIMED, PB);

    submit_ok("seat A mark_dig", UA, PA, rect_verb(ESV_MarkDig, NULL, ax - 9, ay - 1, ax - 9 + DIG_W - 1, ay - 1 + DIG_H - 1));
    submit_ok("seat B build_room", UB, PB, rect_verb(ESV_BuildRoom, "TREASURE", bx + 3, by - 1, bx + 3 + ROOM_W - 1, by - 1 + ROOM_H - 1));
    CHECK_TRUE("both queues are busy at once", !extseat_idle(UA) && !extseat_idle(UB));
    return FTRs_Go_To_Next_Action;
}

static int64_t s_idle_since = -1;
FTestActionResult m06_verbs_wait(struct FTestActionArgs* const args)
{
    if (!extseat_idle(UA) || !extseat_idle(UB)) { s_idle_since = -1; return FTRs_Repeat_Current_Action; }
    if (s_idle_since < 0) s_idle_since = (int64_t)get_gameturn();
    if ((int64_t)get_gameturn() < s_idle_since + 25) return FTRs_Repeat_Current_Action;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult m07_verbs_check(struct FTestActionArgs* const args)
{
    const int64_t a_dug = count_tagged(PA, ax - 9, ay - 1);
    const int64_t b_dug = count_tagged(PB, ax - 9, ay - 1);
    const int64_t b_room = room_slabs(PB, bx + 3, by - 1);
    const int64_t a_room = room_slabs(PA, bx + 3, by - 1);
    if (a_dug != DIG_W * DIG_H) SOFT_FAIL("seat A tagged %" PRId64 " of %d slabs", a_dug, DIG_W * DIG_H);
    if (b_dug != 0) SOFT_FAIL("seat B has %" PRId64 " tags on seat A's patch", b_dug);
    if (b_room != ROOM_W * ROOM_H) SOFT_FAIL("seat B built %" PRId64 " of %d slabs", b_room, ROOM_W * ROOM_H);
    if (a_room != 0) SOFT_FAIL("seat A owns %" PRId64 " slabs of seat B's room", a_room);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " multi-seat check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: two External seats and a built-in AI keeper coexist; packets and verbs stay per seat");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
