// attack(thing_id, pos) through an External seat: a held creature does not fight and the hand cannot drop on another
// keeper's land, so an agent had no way to set one of its creatures on an enemy room, door or heart (the live report).
// Each order walks its creature there held and, on arrival, hands it back fighting what is there, as a creature at a
// Call to Arms banner does: an enemy treasure room (attacks the room), an enemy door across a corridor (breaks it down),
// the enemy heart (a creature held beside it, which does not fight it, sets about it). A spot with nothing of the enemy's is refused up front (NOTHING_TO_ATTACK), and a plain
// move_creature still holds its creature on arrival rather than setting it on anything.
#include "ftest_ai_seat_attack.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "config_creature.h"
#include "config_crtrstates.h"
#include "config_keeperfx.h"
#include "config_trapdoor.h"
#include "creature_control.h"
#include "creature_states.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "net_game.h"
#include "player_data.h"
#include "player_instances.h"
#include "room_data.h"
#include "slab_data.h"
#include "thing_creature.h"
#include "thing_doors.h"
#include "thing_list.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK_TRUE(what, cond) do { if (!(cond)) SOFT_FAIL("%s", what); } while (0)

static PlayerNumber P = -1, E = -1;
static NetUserId U = -1;
static MapSlabCoord hsx, hsy;
static ThingIndex s_raider = 0, s_breaker = 0, s_heartbreaker = 0, s_mover = 0, s_door = 0, s_heart = 0;
static RoomIndex s_room = 0;
static int64_t s_phase_start = 0;
static TbBool s_saw_room = false, s_saw_door = false, s_saw_heart = false;
static HitPoints s_door_hp = 0, s_heart_hp = 0;

// North of the seat's heart: on this map it sits near the south edge.
#define ROOM_X (hsx + 4)
#define ROOM_Y (hsy - 6)
#define COR_Y (hsy - 11)
#define DOOR_X (hsx + 2)

static int64_t now(void) { return (int64_t)get_gameturn(); }
static TbBool phase_over(int64_t turns) { return now() - s_phase_start >= turns; }
static struct Thing* creature(ThingIndex i) { struct Thing* t = thing_get(i); return (thing_is_invalid(t) || !thing_is_creature(t)) ? NULL : t; }
static struct ExtSeatVerb order(enum ExtSeatVerbKind k, ThingIndex t, MapSubtlCoord x, MapSubtlCoord y)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = k; v.has_thing = true; v.thing_id = t; v.has_pos = true; v.stl_x = x; v.stl_y = y;
    return v;
}
static void submit_ok(const char* what, struct ExtSeatVerb v)
{
    const char* e = extseat_submit_verb_ex(U, P, &v, true, NULL);
    if (e) SOFT_FAIL("%s: rejected with %s", what, e);
}
static TbBool said(ThingIndex t, const char* reason)
{
    struct ExtSeatAutoRelease ar[EXTSEAT_AUTO_RELEASE_RING];
    const int64_t n = extseat_auto_releases(U, ar, EXTSEAT_AUTO_RELEASE_RING);
    for (int64_t i = 0; i < n; i++) if ((ar[i].thing_id == t) && (strcmp(ar[i].reason, reason) == 0)) return true;
    return false;
}
static TbBool said_attacking(ThingIndex t) { return said(t, "attacking"); }
static TbBool in_state(ThingIndex t, CrtrStateId st)
{
    const struct Thing* c = creature(t);
    return c && ((get_creature_state_besides_interruptions(c) == st) || (c->continue_state == st));
}
static TbBool held(ThingIndex t) { const struct Thing* c = creature(t); return c && ((c->active_state == CrSt_ManualControl) || (c->continue_state == CrSt_ManualControl)); }

FTestActionResult at01_setup(struct FTestActionArgs* const args);
FTestActionResult at02_orders(struct FTestActionArgs* const args);
FTestActionResult at03_fights(struct FTestActionArgs* const args);

void ftest_ai_seat_attack_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_attack_init()
{
    s_failures = 0;
    ftest_append_action(at01_setup, 0, NULL);
    ftest_append_action(at02_orders, 1, NULL);
    ftest_append_action(at03_fights, 1, NULL);
    return true;
}

FTestActionResult at01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0) { FTEST_FAIL_TEST("no second keeper"); return FTRs_Go_To_Next_Action; }
    E = my_player_number;
    U = net_add_external_seat(P);
    if (U < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    kfx_config_state.pay_day_progress[P] = 0;
    const struct Thing* heart = find_players_dungeon_heart(P);
    hsx = subtile_slab(heart->mappos.x.stl.num); hsy = subtile_slab(heart->mappos.y.stl.num);
    // Open floor with the enemy's treasure room in it; beyond that a corridor between rock with the enemy's door across it.
    if ((hsy < 14) || (hsx < 8) || (hsx + 8 >= kfx_sim_state.map_tiles_x)) { FTEST_FAIL_TEST("the heart is too near the map's edge for the layout"); return FTRs_Go_To_Next_Action; }
    ftest_util_replace_slabs(hsx - 6, hsy - 8, hsx + 6, hsy - 4, SlbT_CLAIMED, P);
    ftest_util_replace_slabs(ROOM_X - 1, ROOM_Y - 1, ROOM_X + 1, ROOM_Y + 1, SlbT_TREASURE, E);
    ftest_util_replace_slabs(hsx - 6, COR_Y - 1, hsx + 6, COR_Y + 1, SlbT_ROCK, PLAYER_NEUTRAL);
    ftest_util_replace_slabs(hsx - 6, COR_Y, hsx + 6, COR_Y, SlbT_CLAIMED, P);
    ftest_util_replace_slabs(DOOR_X, COR_Y, DOOR_X, COR_Y, SlbT_CLAIMED, E);
    if (!player_place_door_without_check_at(slab_subtile_center(DOOR_X), slab_subtile_center(COR_Y), E, (ThingModel)door_model_id("WOOD"), true)) {
        FTEST_FAIL_TEST("could not place the enemy door"); return FTRs_Go_To_Next_Action;
    }
    const struct Thing* door = get_door_for_position(slab_subtile_center(DOOR_X), slab_subtile_center(COR_Y));
    const struct Room* room = subtile_room_get(slab_subtile_center(ROOM_X), slab_subtile_center(ROOM_Y));
    if (thing_is_invalid(door) || room_is_invalid(room)) { FTEST_FAIL_TEST("no enemy door or room"); return FTRs_Go_To_Next_Action; }
    s_door = door->index; s_room = room->index;
    // Below the enemy's own heart, open ground of its own. The bile demon starts there held by an earlier move_creature
    // (the live report: a held creature beside a heart does not attack it); left free it would go for the heart itself,
    // and so it would if the enemy's creatures set upon it (handed back to fight them), so none of them are left.
    {
        int64_t idx = kfx_sim_state.thing_lists[TngList_Creatures].index;
        for (int64_t g = 0; (idx > 0) && (g < THINGS_COUNT); g++) {
            struct Thing* t = thing_get(idx); idx = t->next_of_class;
            if (t->owner == E) kill_creature(t, INVALID_THING, P, CrDed_NoEffects);
        }
    }
    const struct Thing* eh = find_players_dungeon_heart(E);
    s_heart = eh->index;
    const MapSubtlCoord ex = eh->mappos.x.stl.num, ey = eh->mappos.y.stl.num;
    ftest_util_replace_slabs(subtile_slab(ex) - 2, subtile_slab(ey) + 3, subtile_slab(ex) + 2, subtile_slab(ey) + 4, SlbT_CLAIMED, E);
    struct Thing* a = ftest_util_create_creature(subtile_coord_center(slab_subtile_center(hsx - 4)), subtile_coord_center(slab_subtile_center(ROOM_Y)), P, 6, (ThingModel)creature_model_id("TROLL"));
    struct Thing* b = ftest_util_create_creature(subtile_coord_center(slab_subtile_center(hsx - 4)), subtile_coord_center(slab_subtile_center(COR_Y)), P, 6, (ThingModel)creature_model_id("TROLL"));
    struct Thing* c = ftest_util_create_creature(subtile_coord_center(ex), subtile_coord_center(ey + 12), P, 9, (ThingModel)creature_model_id("BILE_DEMON"));
    struct Thing* d = ftest_util_create_creature(subtile_coord_center(slab_subtile_center(hsx - 5)), subtile_coord_center(slab_subtile_center(ROOM_Y - 1)), P, 3, (ThingModel)creature_model_id("ORC"));
    if (thing_is_invalid(a) || thing_is_invalid(b) || thing_is_invalid(c) || thing_is_invalid(d)) { FTEST_FAIL_TEST("could not create the creatures"); return FTRs_Go_To_Next_Action; }
    s_raider = a->index; s_breaker = b->index; s_heartbreaker = c->index; s_mover = d->index;
    s_door_hp = door->health; s_heart_hp = eh->health;
    submit_ok("hold the bile demon", order(ESV_MoveCreature, s_heartbreaker, ex, ey + 13));
    s_phase_start = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult at02_orders(struct FTestActionArgs* const args)
{
    if (!extseat_idle(U) || !held(s_heartbreaker) || (creature(s_heartbreaker)->active_state != CrSt_ManualControl)) {
        if (phase_over(300)) { FTEST_FAIL_TEST("the bile demon was never held"); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    CHECK_TRUE("held, it leaves the heart alone", thing_get(s_heart)->health == s_heart_hp);
    struct ExtSeatVerb v = order(ESV_Attack, s_raider, slab_subtile_center(hsx - 2), slab_subtile_center(ROOM_Y - 2));
    const char* e = extseat_submit_verb_ex(U, P, &v, true, NULL);
    if ((e == NULL) || (strcmp(e, "NOTHING_TO_ATTACK") != 0)) SOFT_FAIL("attack on plain floor: expected NOTHING_TO_ATTACK, got %s", e ? e : "success");
    const struct Thing* eh = thing_get(s_heart);
    submit_ok("attack the enemy room", order(ESV_Attack, s_raider, slab_subtile_center(ROOM_X), slab_subtile_center(ROOM_Y)));
    submit_ok("attack the enemy door", order(ESV_Attack, s_breaker, slab_subtile_center(DOOR_X - 1), slab_subtile_center(COR_Y)));
    submit_ok("attack the enemy heart", order(ESV_Attack, s_heartbreaker, eh->mappos.x.stl.num, eh->mappos.y.stl.num + 8));
    submit_ok("a plain move beside the room", order(ESV_MoveCreature, s_mover, slab_subtile_center(ROOM_X - 2), slab_subtile_center(ROOM_Y)));
    s_phase_start = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult at03_fights(struct FTestActionArgs* const args)
{
    if (in_state(s_raider, CrSt_CreatureAttackRooms) || (creature(s_raider) && creature_control_get_from_thing(creature(s_raider))->target_room_id == s_room)) s_saw_room = true;
    if (in_state(s_breaker, CrSt_CreatureDoorCombat)) s_saw_door = true;
    if (in_state(s_heartbreaker, CrSt_CreatureObjectCombat) && (creature_control_get_from_thing(creature(s_heartbreaker))->combat.battle_enemy_idx == s_heart)) s_saw_heart = true;
    const struct Thing* door = thing_get(s_door);
    const struct Thing* eh = thing_get(s_heart);
    const TbBool door_hurt = thing_is_invalid(door) || (door->class_id != TCls_Door) || (door->health < s_door_hp);
    const TbBool heart_hurt = !thing_is_invalid(eh) && (eh->health < s_heart_hp);
    const TbBool done = s_saw_room && s_saw_door && door_hurt && s_saw_heart && heart_hurt && extseat_idle(U);
    if (!done && !phase_over(1500)) return FTRs_Repeat_Current_Action;
    FTESTLOG("after %" PRId64 " turns: room %d, door %d (hurt %d), heart %d (hurt %d); arrivals said: %d %d %d",
             now() - s_phase_start, (int)s_saw_room, (int)s_saw_door, (int)door_hurt, (int)s_saw_heart, (int)heart_hurt,
             (int)said_attacking(s_raider), (int)said_attacking(s_breaker), (int)said_attacking(s_heartbreaker));
    CHECK_TRUE("the raider attacked the enemy room", s_saw_room);
    CHECK_TRUE("the breaker went for the enemy door", s_saw_door);
    CHECK_TRUE("and damaged it", door_hurt);
    CHECK_TRUE("the bile demon fought the enemy heart", s_saw_heart);
    CHECK_TRUE("and damaged it", heart_hurt);
    CHECK_TRUE("each arrival is reported (auto_released: attacking)", said_attacking(s_raider) && said_attacking(s_breaker));
    CHECK_TRUE("so is the bile demon's", said_attacking(s_heartbreaker));
    CHECK_TRUE("a plain move_creature still holds its creature", held(s_mover) && !said_attacking(s_mover));
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " attack check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: attack sets the seat's creatures on an enemy room, door and heart");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
