// submit_action dry_run=true (extseat_check_verb): the same validation a real submit would run, the same error codes,
// but never queued, never tracked, and not subject to the seat's queue-busy state, so it costs nothing to check "would
// this be accepted" -- including while a real gesture is already running. Checked at the driver level (extseat_check_verb
// directly) since the API layer's own JSON parsing is exercised by every other ai_seat_* test already.
#include "ftest_ai_seat_dry_run.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "config_keeperfx.h"
#include "config_terrain.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
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

static struct ExtSeatVerb room_verb(int64_t x0, int64_t y0, int64_t x1, int64_t y1)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_BuildRoom; snprintf(v.name, sizeof(v.name), "TREASURE");
    v.has_rect = true; v.slab_x0 = x0; v.slab_y0 = y0; v.slab_x1 = x1; v.slab_y1 = y1;
    return v;
}
static int64_t room_slabs(void)
{
    int64_t n = 0;
    for (int dx = 0; dx < 3; dx++) for (int dy = 0; dy < 3; dy++) {
        const struct SlabMap* slb = get_slabmap_block(hsx + 4 + dx, hsy - 1 + dy);
        if (slb->room_index > 0 && room_get(slb->room_index)->kind == treasure && slabmap_owner(slb) == P) n++;
    }
    return n;
}

FTestActionResult dr01_setup(struct FTestActionArgs* const args);
FTestActionResult dr02_check_dry_run(struct FTestActionArgs* const args);

void ftest_ai_seat_dry_run_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_dry_run_init()
{
    s_failures = 0;
    ftest_append_action(dr01_setup, 0, NULL);
    ftest_append_action(dr02_check_dry_run, 2, NULL);
    return true;
}

FTestActionResult dr01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || (U = net_add_external_seat(P)) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    const struct Thing* heart = find_players_dungeon_heart(P);
    hsx = subtile_slab(heart->mappos.x.stl.num); hsy = subtile_slab(heart->mappos.y.stl.num);
    treasure = (RoomKind)get_rid(room_desc, "TREASURE");
    struct Dungeon* d = get_players_dungeon(get_player(P));
    d->total_money_owned = 100000;
    d->room_buildable[treasure] |= 1;
    ftest_util_replace_slabs(hsx + 3, hsy - 2, hsx + 7, hsy + 2, SlbT_CLAIMED, P);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult dr02_check_dry_run(struct FTestActionArgs* const args)
{
    struct ExtSeatVerb valid = room_verb(hsx + 4, hsy - 1, hsx + 6, hsy + 1);
    int64_t steps = 0;
    const char* e = extseat_check_verb(U, P, &valid, &steps);
    CHECK_TRUE("a valid order dry-runs to success", e == NULL);
    CHECK_TRUE("and reports the steps a real submit would need", steps > 0);
    CHECK_TRUE("nothing was actually built", room_slabs() == 0);
    CHECK_TRUE("nothing was queued", extseat_idle(U));

    struct ExtSeatVerb no_kind; memset(&no_kind, 0, sizeof(no_kind));
    no_kind.kind = ESV_BuildRoom; snprintf(no_kind.name, sizeof(no_kind.name), "NOT_A_REAL_ROOM_KIND");
    no_kind.has_rect = true; no_kind.slab_x0 = hsx; no_kind.slab_y0 = hsy; no_kind.slab_x1 = hsx + 1; no_kind.slab_y1 = hsy;
    e = extseat_check_verb(U, P, &no_kind, NULL);
    CHECK_TRUE("an invalid order dry-runs to the same error a real submit would give", e && strcmp(e, "UNKNOWN_KIND") == 0);

    // A real gesture is now running; a dry run for something else must not be blocked by it, and must not disturb it.
    e = extseat_submit_verb(U, P, &valid, NULL);
    CHECK_TRUE("the real submit is accepted", e == NULL);
    CHECK_TRUE("the seat is now busy", !extseat_idle(U));
    struct ExtSeatVerb other = room_verb(hsx + 4, hsy - 1, hsx + 5, hsy);
    steps = -1;
    e = extseat_check_verb(U, P, &other, &steps);
    CHECK_TRUE("a dry run works even while the seat is busy (unlike a real, non-queued submit)", e == NULL && steps > 0);
    CHECK_TRUE("the busy gesture is untouched by the dry run", !extseat_idle(U));

    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " dry-run check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: dry_run validates like a real submit but never queues, tracks, or is blocked by a running gesture");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
