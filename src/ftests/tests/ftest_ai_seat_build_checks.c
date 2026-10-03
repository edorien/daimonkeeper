// Placement checks for an External seat's build verbs: build_room, place_trap and place_door are checked with the
// engine's own rules when submitted, instead of a click that silently does nothing. Bridges: the engine builds a
// dragged bridge one slab per turn in drag order, each needing the player's land beside it by its turn (measured: a
// 1x3 bridge dragged from the far side builds 1 slab of 3), so the seat drags from the corner that lets every slab reach
// land -- two bridge lines given far-side-first are both built whole; one touching no land is refused CANNOT_BUILD_HERE,
// one wider than a slab BRIDGE_NOT_A_LINE (a bridge drag is a path, detect_bridge_shape). The second line and the treasure
// room also cover a fixed engine bug: a drag pressed after packets without map coordinates inherited the previous drag's
// start slab. A treasure room half on water: exactly the water half is reported (extseat_room_build_check, the
// PARTLY_UNBUILDABLE warning's source) and only the floor half built; wholly on earth, refused. Traps: on water, or on a
// slab already trapped, CANNOT_PLACE_HERE; on the seat's floor, placed. Doors: on open floor CANNOT_PLACE_HERE; in a
// one-slab corridor between two walls, placed.
#include "ftest_ai_seat_build_checks.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "config_keeperfx.h"
#include "config_trapdoor.h"
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
#include "thing_traps.h"

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

static struct ExtSeatVerb build(const char* kind, int64_t x0, int64_t y0, int64_t x1, int64_t y1)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_BuildRoom; snprintf(v.name, sizeof(v.name), "%s", kind); v.has_rect = true;
    v.slab_x0 = x0; v.slab_y0 = y0; v.slab_x1 = x1; v.slab_y1 = y1;
    return v;
}
static struct ExtSeatVerb place(TbBool trap, const char* kind, MapSlabCoord sx, MapSlabCoord sy)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = trap ? ESV_PlaceTrap : ESV_PlaceDoor; snprintf(v.name, sizeof(v.name), "%s", kind);
    v.has_pos = true; v.stl_x = slab_subtile_center(sx); v.stl_y = slab_subtile_center(sy);
    return v;
}
static const char* submit(struct ExtSeatVerb v) { return extseat_submit_verb_ex(U, P, &v, true, NULL); }
static void expect(const char* what, struct ExtSeatVerb v, const char* code)
{
    const char* e = submit(v);
    if (e == NULL || strcmp(e, code) != 0) SOFT_FAIL("%s: expected %s, got %s", what, code, e ? e : "success");
}
static int count_kind(SlabKind kind, MapSlabCoord x0, MapSlabCoord y0, MapSlabCoord x1, MapSlabCoord y1)
{
    int n = 0;
    for (MapSlabCoord y = y0; y <= y1; y++)
        for (MapSlabCoord x = x0; x <= x1; x++) { const struct SlabMap* s = get_slabmap_block(x, y); if (s->kind == kind && slabmap_owner(s) == P) n++; }
    return n;
}

FTestActionResult bc01_setup_and_submit(struct FTestActionArgs* const args);
FTestActionResult bc02_check(struct FTestActionArgs* const args);

void ftest_ai_seat_build_checks_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_build_checks_init()
{
    s_failures = 0;
    ftest_append_action(bc01_setup_and_submit, 2, NULL);
    ftest_append_action(bc02_check, 300, NULL);
    return true;
}

FTestActionResult bc01_setup_and_submit(struct FTestActionArgs* const args)
{
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || (U = net_add_external_seat(P)) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    const struct Thing* heart = find_players_dungeon_heart(P);
    hsx = subtile_slab(heart->mappos.x.stl.num); hsy = subtile_slab(heart->mappos.y.stl.num);
    // Shore (claimed row hsy+3), water rows hsy+4..hsy+6, earth beyond; a corridor block for the door further on.
    ftest_util_replace_slabs(hsx - 4, hsy + 3, hsx + 8, hsy + 3, SlbT_CLAIMED, P);
    ftest_util_replace_slabs(hsx - 4, hsy + 4, hsx + 8, hsy + 6, SlbT_WATER, kfx_config_state.neutral_player_num);
    ftest_util_replace_slabs(hsx - 4, hsy + 7, hsx + 8, hsy + 11, SlbT_EARTH, kfx_config_state.neutral_player_num);
    ftest_util_replace_slabs(hsx + 7, hsy + 8, hsx + 7, hsy + 10, SlbT_CLAIMED, P);
    ftest_util_reveal_map(P);
    set_room_available(P, RoK_BRIDGE, 1, 1);
    set_room_available(P, RoK_TREASURE, 1, 1);
    set_trap_buildable_and_add_to_amount(P, (ThingModel)trap_model_id("BOULDER"), 1, 3);
    set_door_buildable_and_add_to_amount(P, (ThingModel)door_model_id("WOOD"), 1, 3);
    // Gold the engine can actually spend: off-map money counts toward the total and is paid from first.
    get_players_dungeon(get_player(P))->offmap_money_owned += 100000;
    get_players_dungeon(get_player(P))->total_money_owned += 100000;

    // Bridges.
    CHECK_TRUE("a 1x3 bridge given far side first is accepted", submit(build("BRIDGE", hsx - 3, hsy + 6, hsx - 3, hsy + 4)) == NULL);
    expect("a bridge wider than one slab (a drag is a path, not a box)", build("BRIDGE", hsx, hsy + 4, hsx + 2, hsy + 6), "BRIDGE_NOT_A_LINE");
    CHECK_TRUE("a second line, also given far side first, is accepted", submit(build("BRIDGE", hsx + 1, hsy + 6, hsx + 1, hsy + 4)) == NULL);
    // Only row hsy+6, two away from the shore and with nothing of the seat's beside it.
    expect("a bridge touching none of the seat's land", build("BRIDGE", hsx + 5, hsy + 6, hsx + 6, hsy + 6), "CANNOT_BUILD_HERE");
    // A treasure room half on the shore, half on water.
    int64_t bad[2 * 8], nbad = 0;
    const int64_t ok = extseat_room_build_check(P, RoK_TREASURE, hsx + 4, hsy + 3, hsx + 6, hsy + 4, NULL, bad, 8, &nbad);
    CHECK_TRUE("half on water: the floor half builds, exactly the water half is reported", ok == 3 && nbad == 3 && bad[1] == hsy + 4 && bad[3] == hsy + 4 && bad[5] == hsy + 4);
    CHECK_TRUE("a partly buildable room is accepted (with its warning)", submit(build("TREASURE", hsx + 4, hsy + 3, hsx + 6, hsy + 4)) == NULL);
    expect("a room wholly on earth", build("TREASURE", hsx - 2, hsy + 9, hsx - 1, hsy + 10), "CANNOT_BUILD_HERE");
    // Traps.
    expect("a trap on water", place(true, "BOULDER", hsx + 8, hsy + 5), "CANNOT_PLACE_HERE");
    CHECK_TRUE("a trap on the seat's floor is accepted", submit(place(true, "BOULDER", hsx + 8, hsy + 3)) == NULL);
    // Doors.
    expect("a door on open floor (no wall pair)", place(false, "WOOD", hsx - 4, hsy + 3), "CANNOT_PLACE_HERE");
    CHECK_TRUE("a door in a one-slab corridor between walls is accepted", submit(place(false, "WOOD", hsx + 7, hsy + 9)) == NULL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult bc02_check(struct FTestActionArgs* const args)
{
    const int far_first = count_kind(SlbT_BRIDGE, hsx - 3, hsy + 4, hsx - 3, hsy + 6);
    const int second = count_kind(SlbT_BRIDGE, hsx + 1, hsy + 4, hsx + 1, hsy + 6);
    const int treasure = count_kind(SlbT_TREASURE, hsx + 4, hsy + 3, hsx + 6, hsy + 4);
    FTESTLOG("built: far-first bridge %d/3, second line %d/3, treasure %d (of 3 buildable)", far_first, second, treasure);
    CHECK_TRUE("the far-side-first bridge was built whole (the seat dragged from the shore)", far_first == 3);
    CHECK_TRUE("the second line was built whole too (its drag starts where it should, not where the last one did)", second == 3);
    CHECK_TRUE("only the floor half of the treasure room was built", treasure == 3);
    CHECK_TRUE("the boulder trap is on the shore", slab_has_trap_on(hsx + 8, hsy + 3));
    expect("a second trap on a slab already trapped", place(true, "BOULDER", hsx + 8, hsy + 3), "CANNOT_PLACE_HERE");
    CHECK_TRUE("the door is in the corridor", slab_is_door(hsx + 7, hsy + 9));
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " build check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: rooms, bridges, traps and doors are checked with the engine's own rules and bridges built whole");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
