// build_room larger than one engine drag: the engine clips a drag to MAX_USER_ROOMSPACE_WIDTH (9) slabs a side
// (create_box_roomspace_from_drag), which once stopped an agent's bridges at "10 slabs out" with no error. The seat now
// makes a larger rectangle as several drags from the start corner, each after the last has been built: a 19-slab
// bridge over lava from one slab of land (three drags, each piece starting from the end of the one before), and a 13x4
// treasure room (two drags), are built in full.
#include "ftest_ai_seat_long_builds.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "config_keeperfx.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_config_state.h"
#include "net_game.h"
#include "player_availability.h"
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

#define Y(n) (hsy - (n))
#define BRIDGE_FROM (hsx + 10)
#define BRIDGE_TO (hsx - 8)
static PlayerNumber P = -1;
static NetUserId U = -1;
static MapSlabCoord hsx, hsy;

static void fill(MapSlabCoord x0, MapSlabCoord y0, MapSlabCoord x1, MapSlabCoord y1, SlabKind kind, PlayerNumber owner)
{
    ftest_util_replace_slabs(min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1), kind, owner);
}
static int count_kind(SlabKind kind, MapSlabCoord x0, MapSlabCoord y0, MapSlabCoord x1, MapSlabCoord y1)
{
    int n = 0;
    for (MapSlabCoord y = min(y0, y1); y <= max(y0, y1); y++)
        for (MapSlabCoord x = min(x0, x1); x <= max(x0, x1); x++) {
            const struct SlabMap* s = get_slabmap_block(x, y);
            if ((s->kind == kind) && (slabmap_owner(s) == P)) n++;
        }
    return n;
}
static const char* build(const char* kind, MapSlabCoord x0, MapSlabCoord y0, MapSlabCoord x1, MapSlabCoord y1)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_BuildRoom; snprintf(v.name, sizeof(v.name), "%s", kind); v.has_rect = true;
    v.slab_x0 = x0; v.slab_y0 = y0; v.slab_x1 = x1; v.slab_y1 = y1;
    return extseat_submit_verb_ex(U, P, &v, true, NULL);
}

FTestActionResult lb01_setup_and_build(struct FTestActionArgs* const args);
FTestActionResult lb02_check(struct FTestActionArgs* const args);

void ftest_ai_seat_long_builds_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_long_builds_init()
{
    s_failures = 0;
    ftest_append_action(lb01_setup_and_build, 2, NULL);
    ftest_append_action(lb02_check, 1, NULL);
    return true;
}

FTestActionResult lb01_setup_and_build(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || (U = net_add_external_seat(P)) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    const struct Thing* heart = find_players_dungeon_heart(P);
    hsx = subtile_slab(heart->mappos.x.stl.num); hsy = subtile_slab(heart->mappos.y.stl.num);
    if ((BRIDGE_TO < 1) || (hsx + 11 >= kfx_sim_state.map_tiles_x - 1) || (Y(10) < 1)) { FTEST_FAIL_TEST("the heart is too near the map edge"); return FTRs_Go_To_Next_Action; }
    // North of the heart: claimed floor for a wide room, then a lava lake with one slab of land at its east end.
    fill(hsx - 8, Y(3), hsx + 8, Y(7), SlbT_CLAIMED, P);
    fill(hsx - 8, Y(8), hsx + 10, Y(10), SlbT_LAVA, kfx_config_state.neutral_player_num);
    fill(hsx + 11, Y(9), hsx + 11, Y(9), SlbT_CLAIMED, P);
    ftest_util_reveal_map(P);
    set_room_available(P, RoK_BRIDGE, 1, 1);
    set_room_available(P, RoK_TREASURE, 1, 1);
    get_players_dungeon(get_player(P))->offmap_money_owned += 100000;
    get_players_dungeon(get_player(P))->total_money_owned += 100000;
    const char* e = build("BRIDGE", BRIDGE_TO, Y(9), BRIDGE_FROM, Y(9)); // 19 slabs, given far end first
    CHECK_TRUE("a 19-slab bridge is accepted", e == NULL);
    if (e) FTESTLOG("bridge refused: %s", e);
    e = build("TREASURE", hsx - 7, Y(3), hsx + 5, Y(6)); // 13 x 4
    CHECK_TRUE("a 13x4 treasure room is accepted", e == NULL);
    if (e) FTESTLOG("treasure refused: %s", e);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult lb02_check(struct FTestActionArgs* const args)
{
    const int bridge = count_kind(SlbT_BRIDGE, BRIDGE_TO, Y(9), BRIDGE_FROM, Y(9));
    const int treasure = count_kind(SlbT_TREASURE, hsx - 7, Y(3), hsx + 5, Y(6));
    const TbBool waited = (args->intended_start_at_game_turn > 0) && ((int64_t)get_gameturn() > args->intended_start_at_game_turn + 600);
    if (((bridge < 19) || (treasure < 52)) && !waited) return FTRs_Repeat_Current_Action;
    FTESTLOG("built: bridge %d of 19 slabs, treasure room %d of 52 (turn %d)", bridge, treasure, (int)get_gameturn());
    CHECK_TRUE("the whole bridge was built, past one drag's 9 slabs", bridge == 19);
    CHECK_TRUE("the whole treasure room was built, past one drag's 9 slabs", treasure == 52);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " long build check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: rooms and bridges longer than one engine drag are built in full");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
