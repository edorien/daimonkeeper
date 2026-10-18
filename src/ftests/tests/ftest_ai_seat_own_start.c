// An agent claims the human's own keeper at the very start of a campaign level (the bridge's connect with claim 0, before
// the heart-zoom intro begins) and sends its opening orders at once: two small dig marks, a big dig rectangle,
// and two imps cast on the floor beside the heart. What the engine did with them is the observable: the marks on the task
// list, the gold the imps cost, the imps that arrived.
#include "ftest_ai_seat_own_start.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "config_keeperfx.h"
#include "config_magic.h"
#include "kfx_config_state.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "game_legacy.h"
#include "kfx_sim_state.h"
#include "net_game.h"
#include "player_data.h"
#include "player_instances.h"
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

static PlayerNumber P = -1;
static NetUserId U = -1;
/** 0: the keeper is claimed, then the intro is forced on it anyway (the seat's queue must wait it out); 1: claimed
 *  while the intro runs (it ends at once). */
static int64_t s_midzoom = 0;
static GoldAmount gold_before = 0;
static int64_t diggers_before = 0;
static MapSlabCoord hsx, hsy;

static struct ExtSeatVerb dig(int64_t x0, int64_t y0, int64_t x1, int64_t y1)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_MarkDig; v.has_rect = true;
    v.slab_x0 = x0; v.slab_y0 = y0; v.slab_x1 = x1; v.slab_y1 = y1;
    return v;
}
static struct ExtSeatVerb cast_imp(int64_t slb_x, int64_t slb_y)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_CastPower; snprintf(v.name, sizeof(v.name), "POWER_IMP");
    v.has_pos = true; v.stl_x = slb_x * 3 + 1; v.stl_y = slb_y * 3 + 1;
    return v;
}
static void submit(const char *what, struct ExtSeatVerb v)
{
    const char *e = extseat_submit_verb_ex(U, P, &v, true, NULL);
    FTESTLOG("OWNSTART: submit %s -> %s", what, e ? e : "accepted");
    if (e) SOFT_FAIL("%s: rejected with %s", what, e);
}
static struct Dungeon *dg(void) { return get_players_dungeon(get_player(P)); }

static TbBool ftest_ai_seat_own_start_actions(void);
FTestActionResult os01_claim_and_order(struct FTestActionArgs* const args);
FTestActionResult os02_wait(struct FTestActionArgs* const args);
FTestActionResult os03_check(struct FTestActionArgs* const args);

TbBool ftest_ai_seat_own_start_init()
{
    s_midzoom = 0;
    return ftest_ai_seat_own_start_actions();
}

TbBool ftest_ai_seat_own_start_midzoom_init()
{
    s_midzoom = 1;
    return ftest_ai_seat_own_start_actions();
}

static TbBool ftest_ai_seat_own_start_actions(void)
{
    s_failures = 0;
    ftest_append_action(os01_claim_and_order, 0, NULL);
    ftest_append_action(os02_wait, 1, NULL);
    ftest_append_action(os03_check, 1, NULL);
    return true;
}

FTestActionResult os01_claim_and_order(struct FTestActionArgs* const args)
{
    P = my_player_number;
    // The agent claims the keeper as the level loads, and then the level start (game_loop) gives it the heart zoom intro:
    // 16 turns of zoom and 16 of zoom out in which the keeper's input is locked (and in the first half it is possessing
    // the spectator creature, not in the dungeon view).
    if (s_midzoom) set_player_instance(get_my_player(), PI_HeartZoom, 0);
    U = net_add_external_seat(P);
    if (U < 1) { FTEST_FAIL_TEST("net_add_external_seat failed"); return FTRs_Go_To_Next_Action; }
    const struct PlayerInfo *me = get_player(P);
    CHECK_TRUE("a seat skips the level-start intro (game_loop() starts it only for keepers that do not)", me->skip_heart_zoom);
    if (s_midzoom) {
        CHECK_TRUE("claimed while the intro ran: it is over", me->instance_num == PI_Unset);
        CHECK_TRUE("and the keeper is back in the dungeon view", me->view_type == PVT_DungeonTop);
        CHECK_TRUE("with its input free", (get_player_user_state(me)->init_flags & UsrIF_MouseInputDisabled) == 0);
    } else {
        set_player_instance(get_my_player(), PI_HeartZoom, 0); // forced anyway: the queue waits
    }
    const struct Thing *heart = find_players_dungeon_heart(P);
    hsx = subtile_slab(heart->mappos.x.stl.num); hsy = subtile_slab(heart->mappos.y.stl.num);
    struct Dungeon *d = dg();
    d->total_money_owned = 9000;
    gold_before = d->total_money_owned;
    diggers_before = d->num_active_diggers;
    FTESTLOG("OWNSTART: turn %" PRId64 " heart slab (%" PRId64 ",%" PRId64 ") gold %" PRId64 " diggers %" PRId64 " seat user %" PRId64,
        (int64_t)get_gameturn(), (int64_t)hsx, (int64_t)hsy, (int64_t)gold_before, diggers_before, (int64_t)U);
    // The orders an agent sent on keeporig:8 (Tickle, heart at slab 15,73): the portal link, a corridor up from the heart
    // room, a block of rooms' worth of rock, and two imps beside the heart.
    submit("dig single slab", dig(11, 73, 11, 73));
    submit("dig corridor", dig(15, 63, 15, 69));
    submit("dig block", dig(6, 63, 28, 67));
    submit("imp 1", cast_imp(15, 71));
    submit("imp 2", cast_imp(13, 73));
    // A slab coordinate given where a subtile is wanted lands on rock far from the keeper's land: refused, not "done".
    struct ExtSeatVerb wrong = cast_imp(0, 0);
    wrong.stl_x = 15; wrong.stl_y = 71;
    const char *wrong_err = extseat_check_verb(U, P, &wrong, NULL);
    FTESTLOG("OWNSTART: a slab coordinate as the cast position -> %s", wrong_err ? wrong_err : "accepted");
    CHECK_TRUE("a cast at a spot the power cannot be cast on is refused", wrong_err != NULL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult os02_wait(struct FTestActionArgs* const args)
{
    if (!extseat_idle(U)) return FTRs_Repeat_Current_Action;
    FTESTLOG("OWNSTART: orders finished at turn %" PRId64, (int64_t)get_gameturn());
    return FTRs_Go_To_Next_Action;
}

FTestActionResult os03_check(struct FTestActionArgs* const args)
{
    struct Dungeon *d = dg();
    const int64_t single = find_from_task_list_by_slab(P, 11, 73) != -1;
    const int64_t corridor = find_from_task_list_by_slab(P, 15, 68) != -1;
    const int64_t block = find_from_task_list_by_slab(P, 10, 65) != -1;
    FTESTLOG("OWNSTART: marks single=%" PRId64 " corridor=%" PRId64 " block=%" PRId64 "; gold %" PRId64 " -> %" PRId64 ", diggers %" PRId64 " -> %" PRId64,
        single, corridor, block, (int64_t)gold_before, (int64_t)d->total_money_owned, diggers_before, (int64_t)d->num_active_diggers);
    CHECK_TRUE("the single-slab dig mark was made", single);
    CHECK_TRUE("the corridor dig marks were made", corridor);
    CHECK_TRUE("the block dig marks were made", block);
    CHECK_TRUE("the imps were paid for", d->total_money_owned < gold_before);
    CHECK_TRUE("the imps arrived", d->num_active_diggers >= diggers_before + 2);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: an agent's opening orders on its own keeper, queued before the heart zoom, all took");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
