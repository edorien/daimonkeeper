// An External seat is told at once when its ground or rooms are taken (api_seat_decision.c's note_losses), from its own
// map rather than the engine's events. Room slabs claimed by another keeper raise room_captured, a room slab smashed
// room_destroyed, floor neutralised or claimed ground_lost; the decision's losses say what, how many slabs, where and by
// whom. Not losses: a room slab the seat turned into its own floor, a slab its own imps were marked to dig, a bridge
// back to lava. The wake-up skips a long minimum interval, then holds further losses back for LOSS_URGENT_GAP turns.
// The view also carries own.territory and the creature limit for the pay day report. Then intruders: a hero knight
// stepping onto the seat's floor raises intruder and is listed in own.intruders, while a neutral creature on its floor
// and a knight on another keeper's ground are not intruders; the same knight stepping off and back on is no news. Last, a
// fight (enemy_fight) is urgent like a loss: it is told at once, not after the minimum interval, with the turn it was raised,
// its severity (1, combat) and where it is. Inside the gap after that wake-up a new kind of urgent reason (a fresh
// intruder) still goes at once, while the same kind again (another intruder) waits out the gap.
#include "ftest_ai_seat_losses.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>
#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api_seat_decision.h"
#include "api_seat_view.h"
#include "config_keeperfx.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_config_state.h"
#include "map_events.h"
#include "net_game.h"
#include "player_availability.h"
#include "player_data.h"
#include "room_data.h"
#include "slab_data.h"
#include "spdigger_stack.h"
#include "tasks_list.h"
#include "thing_list.h"
#include "thing_navigate.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK_TRUE(what, cond) do { if (!(cond)) SOFT_FAIL("%s", what); } while (0)

#define Y(n) (hsy - (n))
static PlayerNumber P = -1, E = -1;
static MapSlabCoord hsx, hsy;
static int64_t s_seq = 0;
static ThingIndex s_knight = 0, s_neutral = 0, s_far_knight = 0;

static void fill(MapSlabCoord x0, MapSlabCoord y0, MapSlabCoord x1, MapSlabCoord y1, SlabKind kind, PlayerNumber owner)
{
    ftest_util_replace_slabs(min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1), kind, owner);
}

struct Seen { int64_t seq; int64_t pending; char reasons[200]; char pending_reasons[200]; char losses[400]; };
static struct Seen seen(void)
{
    struct Seen r; memset(&r, 0, sizeof(r));
    VALUE v; api_seat_build_view(&v, P);
    VALUE* dec = value_dict_get(value_dict_get(&v, "seat"), "decision");
    r.seq = value_int64(value_dict_get(dec, "seq"));
    r.pending = value_int64(value_dict_get(dec, "pending"));
    VALUE* arr = value_dict_get(dec, "reasons");
    for (size_t i = 0; arr && i < value_array_size(arr); i++) {
        if (i) strncat(r.reasons, ",", sizeof(r.reasons) - strlen(r.reasons) - 1);
        strncat(r.reasons, value_string(value_array_get(arr, i)), sizeof(r.reasons) - strlen(r.reasons) - 1);
    }
    VALUE* pr = value_dict_get(dec, "pending_reasons");
    for (size_t i = 0; pr && i < value_array_size(pr); i++) {
        if (i) strncat(r.pending_reasons, ",", sizeof(r.pending_reasons) - strlen(r.pending_reasons) - 1);
        strncat(r.pending_reasons, value_string(value_array_get(pr, i)), sizeof(r.pending_reasons) - strlen(r.pending_reasons) - 1);
    }
    VALUE* ls = value_dict_get(dec, "losses");
    for (size_t i = 0; ls && i < value_array_size(ls); i++) {
        VALUE* e = value_array_get(ls, i);
        VALUE* room = value_dict_get(e, "room");
        VALUE* by = value_dict_get(e, "by");
        char one[64];
        snprintf(one, sizeof(one), "%s%s%s x%d by %d;", value_string(value_dict_get(e, "what")), room ? " " : "",
            room ? value_string(room) : "", (int)value_int64(value_dict_get(e, "slabs")), by ? (int)value_int32(by) : -1);
        strncat(r.losses, one, sizeof(r.losses) - strlen(r.losses) - 1);
    }
    value_fini(&v);
    return r;
}

FTestActionResult sl01_setup(struct FTestActionArgs* const args);
FTestActionResult sl02_take_ground(struct FTestActionArgs* const args);
FTestActionResult sl03_told_at_once(struct FTestActionArgs* const args);
FTestActionResult sl04_held_within_the_gap(struct FTestActionArgs* const args);
FTestActionResult sl05_told_after_the_gap(struct FTestActionArgs* const args);
FTestActionResult sl06_intruders_arrive(struct FTestActionArgs* const args);
FTestActionResult sl07_intruder_told(struct FTestActionArgs* const args);
FTestActionResult sl08_knight_steps_off(struct FTestActionArgs* const args);
FTestActionResult sl09_and_back_is_no_news(struct FTestActionArgs* const args);
FTestActionResult sl10_check_no_news(struct FTestActionArgs* const args);
FTestActionResult sl11_a_fight(struct FTestActionArgs* const args);
FTestActionResult sl12_fight_told_at_once(struct FTestActionArgs* const args);
FTestActionResult sl13_a_new_kind_inside_the_gap(struct FTestActionArgs* const args);
FTestActionResult sl14_new_kind_told_at_once(struct FTestActionArgs* const args);
FTestActionResult sl15_the_same_kind_again_waits(struct FTestActionArgs* const args);

void ftest_ai_seat_losses_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_losses_init()
{
    s_failures = 0;
    ftest_append_action(sl01_setup, 0, NULL);
    ftest_append_action(sl02_take_ground, 3, NULL);
    ftest_append_action(sl03_told_at_once, 2, NULL);
    ftest_append_action(sl04_held_within_the_gap, 2, NULL);
    ftest_append_action(sl05_told_after_the_gap, 70, NULL);
    ftest_append_action(sl06_intruders_arrive, 70, NULL);
    ftest_append_action(sl07_intruder_told, 2, NULL);
    ftest_append_action(sl08_knight_steps_off, 2, NULL);
    ftest_append_action(sl09_and_back_is_no_news, 2, NULL);
    ftest_append_action(sl10_check_no_news, 2, NULL);
    ftest_append_action(sl11_a_fight, 70, NULL);
    ftest_append_action(sl12_fight_told_at_once, 2, NULL);
    ftest_append_action(sl13_a_new_kind_inside_the_gap, 2, NULL);
    ftest_append_action(sl14_new_kind_told_at_once, 2, NULL);
    ftest_append_action(sl15_the_same_kind_again_waits, 2, NULL);
    return true;
}

FTestActionResult sl01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    E = my_player_number;
    if (P < 0 || net_add_external_seat(P) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    const struct Thing* heart = find_players_dungeon_heart(P);
    hsx = subtile_slab(heart->mappos.x.stl.num); hsy = subtile_slab(heart->mappos.y.stl.num);
    if ((hsx - 4 < 1) || (Y(8) < 1)) { FTEST_FAIL_TEST("the heart is too near the map edge"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    set_room_available(P, RoK_TREASURE, 1, 1); set_room_available(P, RoK_LAIR, 1, 1); set_room_available(P, RoK_BRIDGE, 1, 1);
    set_room_available(E, RoK_TREASURE, 1, 1);
    // North of the heart: claimed ground with a treasure room and a lair, then lava with a two-slab bridge.
    fill(hsx - 4, Y(3), hsx + 6, Y(6), SlbT_CLAIMED, P);
    fill(hsx - 4, Y(3), hsx - 2, Y(5), SlbT_TREASURE, P);
    fill(hsx, Y(3), hsx + 2, Y(5), SlbT_LAIR, P);
    fill(hsx - 4, Y(7), hsx + 6, Y(8), SlbT_LAVA, kfx_config_state.neutral_player_num);
    fill(hsx + 4, Y(7), hsx + 4, Y(8), SlbT_BRIDGE, P);
    // One of the seat's own slabs its imps are to dig.
    add_task_list_entry(P, SDDigTask_DigEarth, get_subtile_number_at_slab_center(hsx + 5, Y(6)));
    api_seat_decision_set_min_interval(2000); // only an urgent reason gets through
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sl02_take_ground(struct FTestActionArgs* const args)
{
    s_seq = seen().seq;
    // Losses: three treasure slabs claimed by another keeper, a lair slab smashed, two floor slabs neutralised, one
    // claimed.
    fill(hsx - 4, Y(3), hsx - 2, Y(3), SlbT_TREASURE, E);
    fill(hsx, Y(4), hsx, Y(4), SlbT_PATH, kfx_config_state.neutral_player_num);
    fill(hsx + 3, Y(6), hsx + 4, Y(6), SlbT_PATH, kfx_config_state.neutral_player_num);
    fill(hsx + 6, Y(6), hsx + 6, Y(6), SlbT_CLAIMED, E);
    // Not losses: a lair slab the seat made its own floor, its own marked slab dug out, a bridge slab back to lava.
    fill(hsx + 2, Y(5), hsx + 2, Y(5), SlbT_CLAIMED, P);
    fill(hsx + 5, Y(6), hsx + 5, Y(6), SlbT_PATH, kfx_config_state.neutral_player_num);
    fill(hsx + 4, Y(8), hsx + 4, Y(8), SlbT_LAVA, kfx_config_state.neutral_player_num);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sl03_told_at_once(struct FTestActionArgs* const args)
{
    const struct Seen s = seen();
    FTESTLOG("decision %d (was %d): %s | losses: %s", (int)s.seq, (int)s_seq, s.reasons, s.losses);
    CHECK_TRUE("the losses raised a decision at once, despite a minimum interval of 2000 turns", s.seq > s_seq);
    CHECK_TRUE("room_captured", strstr(s.reasons, "room_captured") != NULL);
    CHECK_TRUE("room_destroyed", strstr(s.reasons, "room_destroyed") != NULL);
    CHECK_TRUE("ground_lost", strstr(s.reasons, "ground_lost") != NULL);
    char want[64];
    snprintf(want, sizeof(want), "room_captured TREASURE x3 by %d;", (int)E);
    CHECK_TRUE("three treasure slabs captured, and by whom", strstr(s.losses, want) != NULL);
    CHECK_TRUE("one lair slab destroyed, nobody holding it -- not the one the seat floored itself",
        strstr(s.losses, "room_destroyed LAIR x1 by -1;") != NULL);
    CHECK_TRUE("two floor slabs neutralised -- not the one the seat's imps were marked to dig",
        strstr(s.losses, "ground_lost x2 by -1;") != NULL);
    snprintf(want, sizeof(want), "ground_captured x1 by %d;", (int)E);
    CHECK_TRUE("one floor slab claimed by the other keeper", strstr(s.losses, want) != NULL);
    CHECK_TRUE("a bridge back to lava is not a loss", strstr(s.losses, "BRIDGE") == NULL);
    {
        VALUE v; api_seat_build_view(&v, P);
        VALUE* dec = value_dict_get(value_dict_get(&v, "seat"), "decision");
        VALUE* wh = value_dict_get(dec, "where");
        const int64_t sev = value_int64(value_dict_get(dec, "severity"));
        const int64_t wx = wh ? value_int64(value_array_get(wh, 0)) : -1, wy = wh ? value_int64(value_array_get(wh, 1)) : -1;
        value_fini(&v);
        FTESTLOG("losses: severity %d, where (%d,%d)", (int)sev, (int)wx, (int)wy);
        CHECK_TRUE("losses are severity 1 (combat)", sev == 1);
        CHECK_TRUE("and say where: one of the lost slabs", wh != NULL && subtile_slab(wx) >= hsx - 4 && subtile_slab(wx) <= hsx + 6
            && subtile_slab(wy) >= Y(6) && subtile_slab(wy) <= Y(3));
    }
    s_seq = s.seq;
    // The raid goes on: another slab, inside the gap.
    fill(hsx - 4, Y(6), hsx - 4, Y(6), SlbT_PATH, kfx_config_state.neutral_player_num);
    // The pay day report's numbers.
    VALUE v; api_seat_build_view(&v, P);
    VALUE* own = value_dict_get(&v, "own");
    FTESTLOG("territory %d, creatures %d of %d", (int)value_int64(value_dict_get(own, "territory")),
        (int)value_int64(value_dict_get(own, "creatures_active")), (int)value_int64(value_dict_get(own, "creature_limit")));
    CHECK_TRUE("own.territory counts the seat's slabs", value_int64(value_dict_get(own, "territory")) > 20);
    CHECK_TRUE("own.creature_limit is the portal's", value_dict_get(own, "creature_limit") != NULL
        && value_int64(value_dict_get(own, "creature_limit")) == get_players_num_dungeon(P)->max_creatures_attracted);
    value_fini(&v);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sl04_held_within_the_gap(struct FTestActionArgs* const args)
{
    const struct Seen s = seen();
    FTESTLOG("inside the gap: decision %d, pending %d", (int)s.seq, (int)s.pending);
    CHECK_TRUE("a loss inside the gap waits, pending", (s.seq == s_seq) && (s.pending > 0));
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sl05_told_after_the_gap(struct FTestActionArgs* const args)
{
    const struct Seen s = seen();
    FTESTLOG("after the gap: decision %d: %s | losses: %s", (int)s.seq, s.reasons, s.losses);
    CHECK_TRUE("once the gap has passed it is told", (s.seq > s_seq) && strstr(s.reasons, "ground_lost") != NULL
        && strstr(s.losses, "ground_lost x1 by -1;") != NULL);
    s_seq = s.seq;
    return FTRs_Go_To_Next_Action;
}

static struct Thing* creature_on_slab(const char* kind, PlayerNumber owner, MapSlabCoord sx, MapSlabCoord sy)
{
    struct Coord3d pos;
    set_coords_to_slab_center(&pos, sx, sy);
    return ftest_util_create_creature(pos.x.val, pos.y.val, owner, 1, (ThingModel)creature_model_id(kind));
}

/** own.intruders: the count, and whether `id` is listed. */
static int64_t intruders(ThingIndex id, TbBool* listed)
{
    VALUE v; api_seat_build_view(&v, P);
    VALUE* in = value_dict_get(value_dict_get(&v, "own"), "intruders");
    const int64_t n = value_int64(value_dict_get(in, "count"));
    VALUE* arr = value_dict_get(in, "creatures");
    *listed = false;
    for (size_t i = 0; arr && i < value_array_size(arr); i++)
        if (value_int32(value_dict_get(value_array_get(arr, i), "id")) == id) *listed = true;
    value_fini(&v);
    return n;
}

FTestActionResult sl06_intruders_arrive(struct FTestActionArgs* const args)
{
    s_seq = seen().seq;
    // A hero on the seat's claimed floor; a neutral creature on it too; a hero on the other keeper's claimed slab.
    struct Thing* k = creature_on_slab("KNIGHT", PLAYER_GOOD, hsx + 1, Y(6));
    struct Thing* n = creature_on_slab("FLY", kfx_config_state.neutral_player_num, hsx - 3, Y(6));
    struct Thing* f = creature_on_slab("KNIGHT", PLAYER_GOOD, hsx + 6, Y(6));
    if (thing_is_invalid(k) || thing_is_invalid(n) || thing_is_invalid(f)) { FTEST_FAIL_TEST("intruder setup"); return FTRs_Go_To_Next_Action; }
    s_knight = k->index; s_neutral = n->index; s_far_knight = f->index;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sl07_intruder_told(struct FTestActionArgs* const args)
{
    const struct Seen s = seen();
    TbBool knight = false, neutral = false, far = false;
    const int64_t n = intruders(s_knight, &knight);
    intruders(s_neutral, &neutral);
    intruders(s_far_knight, &far);
    FTESTLOG("intruders: decision %d (was %d): %s | %d on the seat's ground, knight %d, neutral %d, far knight %d",
        (int)s.seq, (int)s_seq, s.reasons, (int)n, (int)knight, (int)neutral, (int)far);
    // The fight the knight starts is a new kind of urgent reason, so it may already have followed the intruder decision
    // (it jumps the gap): then the latest decision is the fight's and the intruder's came just before it.
    CHECK_TRUE("a hero on the seat's floor raises intruder at once", (s.seq > s_seq)
        && ((strstr(s.reasons, "intruder") != NULL) || ((s.seq > s_seq + 1) && (strstr(s.reasons, "enemy_fight") != NULL))));
    CHECK_TRUE("and is listed", knight && (n == 1));
    CHECK_TRUE("a neutral creature is no intruder", !neutral);
    CHECK_TRUE("nor a hero on another keeper's ground", !far);
    s_seq = s.seq;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sl08_knight_steps_off(struct FTestActionArgs* const args)
{
    struct Thing* k = thing_get(s_knight);
    struct Coord3d pos;
    set_coords_to_slab_center(&pos, hsx + 6, Y(6)); // the other keeper's slab
    move_thing_in_map(k, &pos);
    TbBool listed = false;
    const int64_t n = intruders(s_knight, &listed);
    // The list is the last scan's: it changes on the next turn.
    FTESTLOG("knight stepped off (listed until the next scan: %d, count %d)", (int)listed, (int)n);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sl09_and_back_is_no_news(struct FTestActionArgs* const args)
{
    TbBool listed = true;
    intruders(s_knight, &listed);
    CHECK_TRUE("off the seat's ground, the knight is no longer listed", !listed);
    // Back on the seat's floor: a creature seen there lately is not news again (a new one would be pending at once,
    // even inside the gap).
    struct Coord3d pos;
    set_coords_to_slab_center(&pos, hsx + 1, Y(6));
    move_thing_in_map(thing_get(s_knight), &pos);
    s_seq = seen().seq;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sl10_check_no_news(struct FTestActionArgs* const args)
{
    const struct Seen s = seen();
    TbBool listed = false;
    intruders(s_knight, &listed);
    FTESTLOG("knight back: listed %d, decision %d (was %d): %s, pending %s", (int)listed, (int)s.seq, (int)s_seq, s.reasons, s.pending_reasons);
    CHECK_TRUE("the knight is listed again", listed);
    CHECK_TRUE("but raises no new intruder (inside the gap it would be pending)", (s.seq == s_seq) && strstr(s.pending_reasons, "intruder") == NULL);
    return FTRs_Go_To_Next_Action;
}

static int64_t s_fight_turn = 0;

static int64_t latest_decision_turn(void)
{
    VALUE v; api_seat_build_view(&v, P);
    const int64_t t = value_int64(value_dict_get(value_dict_get(value_dict_get(&v, "seat"), "decision"), "turn"));
    value_fini(&v);
    return t;
}

FTestActionResult sl11_a_fight(struct FTestActionArgs* const args)
{
    // The knights above are fighting by now, and their fight's own wake-up starts a LOSS_URGENT_GAP: wait it out, so
    // that only the minimum interval (still 2000 turns) could hold a new fight back.
    if ((int64_t)get_gameturn() - latest_decision_turn() < 62) return FTRs_Repeat_Current_Action;
    s_seq = seen().seq;
    s_fight_turn = (int64_t)get_gameturn();
    // At the heart: far enough (over 5 subtiles) from the knights' fight to be a new event rather than an update of theirs.
    event_create_event_or_update_nearby_existing_event(subtile_coord_center(slab_subtile_center(hsx)),
        subtile_coord_center(slab_subtile_center(hsy)), EvKind_EnemyFight, P, 0);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sl12_fight_told_at_once(struct FTestActionArgs* const args)
{
    const struct Seen s = seen();
    VALUE v; api_seat_build_view(&v, P);
    VALUE* dec = value_dict_get(value_dict_get(&v, "seat"), "decision");
    VALUE* raised = value_dict_get(dec, "raised");
    VALUE* reasons = value_dict_get(dec, "reasons");
    int64_t fight_raised = -1;
    for (size_t i = 0; raised && reasons && i < value_array_size(reasons) && i < value_array_size(raised); i++)
        if (strcmp(value_string(value_array_get(reasons, i)), "enemy_fight") == 0) fight_raised = value_int64(value_array_get(raised, i));
    const int64_t told = value_int64(value_dict_get(dec, "turn"));
    value_fini(&v);
    FTESTLOG("a fight at turn %d: decision %d (was %d) at turn %d: %s, enemy_fight raised at %d",
        (int)s_fight_turn, (int)s.seq, (int)s_seq, (int)told, s.reasons, (int)fight_raised);
    CHECK_TRUE("a fight raises a decision at once, despite a minimum interval of 2000 turns",
        (s.seq > s_seq) && strstr(s.reasons, "enemy_fight") != NULL);
    CHECK_TRUE("told within 2 turns of the fight", (told >= s_fight_turn) && (told - s_fight_turn <= 2));
    CHECK_TRUE("the decision says when the fight was raised", (fight_raised >= s_fight_turn) && (fight_raised <= told));
    {
        VALUE v2; api_seat_build_view(&v2, P);
        VALUE* d2 = value_dict_get(value_dict_get(&v2, "seat"), "decision");
        VALUE* wh = value_dict_get(d2, "where");
        const int64_t sev = value_int64(value_dict_get(d2, "severity"));
        const int64_t wx = wh ? value_int64(value_array_get(wh, 0)) : -1, wy = wh ? value_int64(value_array_get(wh, 1)) : -1;
        value_fini(&v2);
        FTESTLOG("the fight: severity %d, where (%d,%d) (heart slab (%d,%d))", (int)sev, (int)wx, (int)wy, (int)hsx, (int)hsy);
        CHECK_TRUE("a fight is severity 1 (combat)", sev == 1);
        CHECK_TRUE("and says where: at the heart", wh != NULL && subtile_slab(wx) == hsx && subtile_slab(wy) == hsy);
    }
    s_seq = s.seq;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sl13_a_new_kind_inside_the_gap(struct FTestActionArgs* const args)
{
    // Two turns after the fight's wake-up, well inside LOSS_URGENT_GAP: a new hero on the seat's floor.
    s_seq = seen().seq;
    if (thing_is_invalid(creature_on_slab("KNIGHT", PLAYER_GOOD, hsx - 3, Y(6)))) { FTEST_FAIL_TEST("intruder setup"); return FTRs_Go_To_Next_Action; }
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sl14_new_kind_told_at_once(struct FTestActionArgs* const args)
{
    const struct Seen s = seen();
    FTESTLOG("an intruder inside the gap after a fight: decision %d (was %d): %s, pending %s", (int)s.seq, (int)s_seq, s.reasons, s.pending_reasons);
    CHECK_TRUE("a new kind of urgent reason inside the gap is told at once", (s.seq > s_seq) && strstr(s.reasons, "intruder") != NULL);
    s_seq = s.seq;
    // Another fresh intruder: the same kind as the wake-up just now.
    if (thing_is_invalid(creature_on_slab("KNIGHT", PLAYER_GOOD, hsx - 2, Y(6)))) { FTEST_FAIL_TEST("intruder setup"); return FTRs_Go_To_Next_Action; }
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sl15_the_same_kind_again_waits(struct FTestActionArgs* const args)
{
    const struct Seen s = seen();
    FTESTLOG("another intruder inside the gap: decision %d (was %d), pending %s", (int)s.seq, (int)s_seq, s.pending_reasons);
    CHECK_TRUE("the same kind again waits out the gap, pending", (s.seq == s_seq) && strstr(s.pending_reasons, "intruder") != NULL);
    api_seat_decision_set_min_interval(100);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " loss check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: ground and rooms taken from the seat, enemies setting foot on its ground, and fights wake it at once");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
