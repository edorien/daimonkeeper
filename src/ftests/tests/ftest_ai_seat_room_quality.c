// Room quality in the External-seat view (docs/refactor/AI/omissions/03): efficiency, health, capacity, gold and
// open_sides -- the room edges room_slab_side_score gives nothing, which walling in or a door would improve. A 3x3
// treasure room in solid earth (every edge scores 1, none open); one side opened to a corridor (three open sides, all
// north, lower efficiency); the other sides replaced by the seat's own reinforced walls (efficiency up again, the three
// open sides unchanged). Neighbour changes are followed by do_slab_efficiency_alteration, as the engine's own claim and
// reinforce paths do.
#include "ftest_ai_seat_room_quality.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>
#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api_seat_view.h"
#include "config_keeperfx.h"
#include "config_terrain.h"
#include "dungeon_data.h"
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

static PlayerNumber P = -1;
static MapSlabCoord hsx, hsy;
static RoomIndex s_room = 0;
static int64_t s_eff_earth = -1, s_eff_open = -1;

struct Quality { TbBool found; int64_t eff, health, max_health, cap_total, open, open_north, gold_present; int64_t first_open_y; };

static struct Quality quality(void)
{
    struct Quality q; memset(&q, 0, sizeof(q)); q.first_open_y = -1;
    VALUE v; api_seat_build_view(&v, P);
    VALUE* rooms = value_dict_get(value_dict_get(&v, "own"), "rooms");
    for (size_t i = 0; rooms && i < value_array_size(rooms); i++) {
        VALUE* r = value_array_get(rooms, i);
        if (value_int32(value_dict_get(r, "id")) != s_room) continue;
        q.found = true;
        q.eff = value_int32(value_dict_get(r, "efficiency"));
        q.health = value_int64(value_dict_get(r, "health"));
        q.max_health = value_int64(value_dict_get(r, "max_health"));
        VALUE* cap = value_dict_get(r, "capacity");
        q.cap_total = cap ? value_int64(value_dict_get(cap, "total")) : 0;
        q.gold_present = value_dict_get(r, "gold") != NULL;
        VALUE* os = value_dict_get(r, "open_sides");
        q.open = value_int64(value_dict_get(os, "count"));
        VALUE* ex = value_dict_get(os, "slabs");
        for (size_t k = 0; ex && k < value_array_size(ex); k++) {
            VALUE* e = value_array_get(ex, k);
            if (strcmp(value_string(value_array_get(e, 2)), "N") == 0) q.open_north++;
            if (q.first_open_y < 0) q.first_open_y = value_int32(value_array_get(e, 1));
        }
    }
    value_fini(&v);
    FTESTLOG("room %d: found %d efficiency %" PRId64 "%% hp %" PRId64 "/%" PRId64 " capacity %" PRId64 " open %" PRId64 " (north %" PRId64 ")",
        (int)s_room, (int)q.found, q.eff, q.health, q.max_health, q.cap_total, q.open, q.open_north);
    return q;
}
static void reshape(MapSlabCoord x0, MapSlabCoord y0, MapSlabCoord x1, MapSlabCoord y1, SlabKind kind, PlayerNumber owner)
{
    ftest_util_replace_slabs(x0, y0, x1, y1, kind, owner);
    for (MapSlabCoord y = y0; y <= y1; y++)
        for (MapSlabCoord x = x0; x <= x1; x++)
            do_slab_efficiency_alteration(x, y);
}

FTestActionResult rq01_room_in_earth(struct FTestActionArgs* const args);
FTestActionResult rq02_open_one_side(struct FTestActionArgs* const args);
FTestActionResult rq03_wall_the_rest(struct FTestActionArgs* const args);
FTestActionResult rq04_check_walled(struct FTestActionArgs* const args);

void ftest_ai_seat_room_quality_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_room_quality_init()
{
    s_failures = 0;
    ftest_append_action(rq01_room_in_earth, 0, NULL);
    ftest_append_action(rq02_open_one_side, 2, NULL);
    ftest_append_action(rq03_wall_the_rest, 2, NULL);
    ftest_append_action(rq04_check_walled, 2, NULL);
    return true;
}

FTestActionResult rq01_room_in_earth(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || net_add_external_seat(P) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    const struct Thing* heart = find_players_dungeon_heart(P);
    hsx = subtile_slab(heart->mappos.x.stl.num); hsy = subtile_slab(heart->mappos.y.stl.num);
    // Solid earth, then a 3x3 treasure room in the middle of it (rows hsy+6..hsy+8).
    reshape(hsx - 3, hsy + 4, hsx + 3, hsy + 10, SlbT_EARTH, kfx_config_state.neutral_player_num);
    set_room_available(P, RoK_TREASURE, 1, 1);
    ftest_util_replace_slabs(hsx - 1, hsy + 6, hsx + 1, hsy + 8, SlbT_TREASURE, P);
    const struct Room* room = slab_room_get(hsx, hsy + 7);
    if (room_is_invalid(room)) { FTEST_FAIL_TEST("no treasure room built"); return FTRs_Go_To_Next_Action; }
    s_room = room->index;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult rq02_open_one_side(struct FTestActionArgs* const args)
{
    const struct Quality q = quality();
    CHECK_TRUE("the room is in the view with its quality", q.found && q.max_health > 0 && q.cap_total > 0 && q.gold_present);
    CHECK_TRUE("walled in by earth, no side is open", q.open == 0);
    CHECK_TRUE("and it has some efficiency", q.eff > 0);
    s_eff_earth = q.eff;
    // A corridor of the seat's claimed floor along the north side.
    reshape(hsx - 1, hsy + 5, hsx + 1, hsy + 5, SlbT_CLAIMED, P);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult rq03_wall_the_rest(struct FTestActionArgs* const args)
{
    const struct Quality q = quality();
    CHECK_TRUE("opening the north side makes exactly those three sides open", q.open == 3 && q.open_north == 3 && q.first_open_y == hsy + 6);
    CHECK_TRUE("and costs efficiency", q.eff < s_eff_earth);
    s_eff_open = q.eff;
    // The seat's own reinforced walls on the west, east and south sides (and the corners).
    reshape(hsx - 2, hsy + 5, hsx - 2, hsy + 9, SlbT_WALLDRAPE, P);
    reshape(hsx + 2, hsy + 5, hsx + 2, hsy + 9, SlbT_WALLDRAPE, P);
    reshape(hsx - 1, hsy + 9, hsx + 1, hsy + 9, SlbT_WALLDRAPE, P);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult rq04_check_walled(struct FTestActionArgs* const args)
{
    const struct Quality q = quality();
    CHECK_TRUE("reinforced walls score better than earth: efficiency up", q.eff > s_eff_open);
    CHECK_TRUE("the open north side is still reported", q.open == 3 && q.open_north == 3);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " room-quality check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: efficiency, capacity and open sides follow the room's surroundings as the engine scores them");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
