// Custody for an External seat (docs/refactor/AI/omissions/05): enemies held in the seat's prison and torture chamber
// are listed in own.custody (where, health, hunger, what they would become if they died there, time on the rack),
// chickens in its hatchery are listed for the hand, and the hand verbs take prisoners and food: chickens carried to the
// prison in one order (thing_ids + to_room), a prisoner moved to the torture chamber, one released on purpose
// (release: true) -- and refused without it (WOULD_RELEASE_PRISONER); an enemy not in custody is NOT_YOURS; Heal works
// on a prisoner. Decision reasons: new_prisoner, and prisoner_gone for the released one.
#include "ftest_ai_seat_custody.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>
#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api_seat_decision.h"
#include "api_seat_view.h"
#include "config_creature.h"
#include "config_keeperfx.h"
#include "config_magic.h"
#include "creature_control.h"
#include "creature_states.h"
#include "creature_states_prisn.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_config_state.h"
#include "net_game.h"
#include "player_availability.h"
#include "player_data.h"
#include "power_hand.h"
#include "room_data.h"
#include "slab_data.h"
#include "thing_creature.h"
#include "thing_list.h"
#include "thing_objects.h"

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
static RoomIndex s_prison = 0, s_torture = 0;
static ThingIndex s_cap[2], s_free_enemy = 0, s_chick[3];
static int64_t s_ids[4];
static HitPoints s_hurt_health = 0;

static const char* submit(struct ExtSeatVerb v, int64_t* id)
{
    struct ExtSeatSubmitInfo info; memset(&info, 0, sizeof(info));
    const char* e = extseat_submit_verb_ex(U, P, &v, true, &info);
    if (id) *id = info.id;
    return e;
}
static void expect(const char* what, struct ExtSeatVerb v, const char* code)
{
    const char* e = submit(v, NULL);
    if (e == NULL || strcmp(e, code) != 0) SOFT_FAIL("%s: expected %s, got %s", what, code, e ? e : "success");
}
static struct ExtSeatVerb carry(ThingIndex t, RoomIndex room)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_PickUpAndDrop; v.has_thing = true; v.thing_id = t;
    if (room) { v.has_room = true; v.room_id = room; }
    return v;
}
static const struct ExtSeatResult* result_for(int64_t id)
{
    static struct ExtSeatResult res[EXTSEAT_RESULT_RING];
    const int64_t n = extseat_results(U, res, EXTSEAT_RESULT_RING);
    for (int64_t k = 0; k < n; k++) if (res[k].id == id) return &res[k];
    return NULL;
}
static TbBool settled(int64_t id, const struct FTestActionArgs* args, int64_t budget)
{
    if (extseat_idle(U) && result_for(id) != NULL) return true;
    return (args->intended_start_at_game_turn > 0) && ((int64_t)get_gameturn() > args->intended_start_at_game_turn + budget);
}
static int64_t decision(char* reasons, size_t cap)
{
    VALUE v; api_seat_build_view(&v, P);
    VALUE* dec = value_dict_get(value_dict_get(&v, "seat"), "decision");
    const int64_t seq = value_int64(value_dict_get(dec, "seq"));
    reasons[0] = 0;
    VALUE* arr = value_dict_get(dec, "reasons");
    for (size_t i = 0; arr && i < value_array_size(arr); i++) {
        if (i) strncat(reasons, ",", cap - strlen(reasons) - 1);
        strncat(reasons, value_string(value_array_get(arr, i)), cap - strlen(reasons) - 1);
    }
    value_fini(&v);
    return seq;
}
// A prisoner's entry in own.custody.prisoners: its `where`, `on_death` (or "" / "-" when absent) and the torture fields.
struct Seen { TbBool listed; char where[16]; char on_death[32]; int64_t turns_in, break_time; };
static struct Seen seen(ThingIndex id)
{
    struct Seen r; memset(&r, 0, sizeof(r)); r.turns_in = -1;
    VALUE v; api_seat_build_view(&v, P);
    VALUE* pr = value_dict_get(value_dict_get(value_dict_get(&v, "own"), "custody"), "prisoners");
    for (size_t i = 0; pr && i < value_array_size(pr); i++) {
        VALUE* e = value_array_get(pr, i);
        if (value_int32(value_dict_get(e, "id")) != id) continue;
        r.listed = true;
        snprintf(r.where, sizeof(r.where), "%s", value_string(value_dict_get(e, "where")));
        VALUE* od = value_dict_get(e, "on_death");
        snprintf(r.on_death, sizeof(r.on_death), "%s", od ? value_string(od) : "-");
        VALUE* t = value_dict_get(e, "torture");
        if (t) { r.turns_in = value_int64(value_dict_get(t, "turns_in")); r.break_time = value_int64(value_dict_get(t, "break_time")); }
    }
    value_fini(&v);
    return r;
}
static int64_t chickens_listed(void)
{
    VALUE v; api_seat_build_view(&v, P);
    VALUE* food = value_dict_get(value_dict_get(value_dict_get(&v, "own"), "custody"), "food");
    int64_t n = 0;
    const VALUE* keys[16];
    const size_t nk = food ? value_dict_keys_sorted(food, keys, 16) : 0;
    for (size_t i = 0; i < nk; i++) n += value_int64(value_dict_get(value_dict_get(food, value_string(keys[i])), "chickens"));
    value_fini(&v);
    return n;
}

FTestActionResult cu01_setup(struct FTestActionArgs* const args);
FTestActionResult cu02_capture(struct FTestActionArgs* const args);
FTestActionResult cu03_check_prisoners_and_refusals(struct FTestActionArgs* const args);
FTestActionResult cu04_check_chickens_then_torture(struct FTestActionArgs* const args);
FTestActionResult cu05_check_torture_then_heal(struct FTestActionArgs* const args);
FTestActionResult cu06_check_heal_then_release(struct FTestActionArgs* const args);
FTestActionResult cu07_check_release(struct FTestActionArgs* const args);

void ftest_ai_seat_custody_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_custody_init()
{
    s_failures = 0;
    ftest_append_action(cu01_setup, 0, NULL);
    ftest_append_action(cu02_capture, 2, NULL);
    ftest_append_action(cu03_check_prisoners_and_refusals, 12, NULL);
    ftest_append_action(cu04_check_chickens_then_torture, 1, NULL);
    ftest_append_action(cu05_check_torture_then_heal, 1, NULL);
    ftest_append_action(cu06_check_heal_then_release, 1, NULL);
    ftest_append_action(cu07_check_release, 1, NULL);
    return true;
}

FTestActionResult cu01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || (U = net_add_external_seat(P)) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    api_seat_decision_set_min_interval(10);
    const struct Thing* heart = find_players_dungeon_heart(P);
    hsx = subtile_slab(heart->mappos.x.stl.num); hsy = subtile_slab(heart->mappos.y.stl.num);
    // A claimed strip, then prison / torture chamber / hatchery side by side on it, and a corridor of floor beyond.
    ftest_util_replace_slabs(hsx - 4, hsy + 3, hsx + 9, hsy + 6, SlbT_CLAIMED, P);
    set_room_available(P, RoK_PRISON, 1, 1); set_room_available(P, RoK_TORTURE, 1, 1); set_room_available(P, RoK_GARDEN, 1, 1);
    ftest_util_replace_slabs(hsx - 4, hsy + 3, hsx - 2, hsy + 5, SlbT_PRISON, P);
    ftest_util_replace_slabs(hsx, hsy + 3, hsx + 2, hsy + 5, SlbT_TORTURE, P);
    ftest_util_replace_slabs(hsx + 4, hsy + 3, hsx + 6, hsy + 5, SlbT_GARDEN, P);
    const struct Room* prison = slab_room_get(hsx - 3, hsy + 4);
    const struct Room* torture = slab_room_get(hsx + 1, hsy + 4);
    if (room_is_invalid(prison) || room_is_invalid(torture)) { FTEST_FAIL_TEST("rooms not built"); return FTRs_Go_To_Next_Action; }
    s_prison = prison->index; s_torture = torture->index;
    set_power_available(P, PwrK_HEALCRTR, 1, 1);
    // Three mature chickens in the hatchery.
    for (int i = 0; i < 3; i++) {
        struct Coord3d pos;
        set_coords_to_slab_center(&pos, hsx + 4 + i, hsy + 4);
        struct Thing* c = create_object(&pos, ObjMdl_ChickenMature, P, -1);
        s_chick[i] = thing_is_invalid(c) ? 0 : c->index;
    }
    // Two heroes knocked out in the prison, a guard to carry them in, and a hero at large.
    struct Coord3d centre;
    set_coords_to_slab_center(&centre, hsx - 3, hsy + 4);
    struct Thing* guard = ftest_util_create_creature(centre.x.val, centre.y.val, P, 1, (ThingModel)creature_model_id("ORC"));
    for (int i = 0; i < 2; i++) {
        struct Thing* h = ftest_util_create_creature(centre.x.val, centre.y.val, PLAYER_GOOD, 3, (ThingModel)creature_model_id(i == 0 ? "BARBARIAN" : "ARCHER"));
        if (thing_is_invalid(h)) { FTEST_FAIL_TEST("no captive"); return FTRs_Go_To_Next_Action; }
        make_creature_unconscious(h);
        s_cap[i] = h->index;
    }
    struct Thing* f = ftest_util_create_creature(subtile_coord_center(heart->mappos.x.stl.num + 30), subtile_coord_center(heart->mappos.y.stl.num + 30), PLAYER_GOOD, 1, (ThingModel)creature_model_id("KNIGHT"));
    s_free_enemy = thing_is_invalid(f) ? 0 : f->index;
    if (thing_is_invalid(guard) || !s_chick[0] || !s_chick[1] || !s_chick[2]) { FTEST_FAIL_TEST("setup things"); return FTRs_Go_To_Next_Action; }
    return FTRs_Go_To_Next_Action;
}

FTestActionResult cu02_capture(struct FTestActionArgs* const args)
{
    // The guard is the only orc of the seat standing in the prison.
    struct Coord3d centre;
    set_coords_to_slab_center(&centre, hsx - 3, hsy + 4);
    struct Thing* guard = NULL;
    int64_t idx = kfx_sim_state.thing_lists[TngList_Creatures].index;
    for (int64_t g = 0; (idx > 0) && (g < 4000) && (guard == NULL); g++) {
        struct Thing* t = thing_get(idx); idx = t->next_of_class;
        if (thing_is_creature(t) && (t->owner == P) && (t->model == creature_model_id("ORC")) && (subtile_slab(t->mappos.x.stl.num) == hsx - 3)) guard = t;
    }
    if (guard == NULL) { FTEST_FAIL_TEST("no guard"); return FTRs_Go_To_Next_Action; }
    for (int i = 0; i < 2; i++) controlled_creature_drop_thing(guard, thing_get(s_cap[i]), P);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult cu03_check_prisoners_and_refusals(struct FTestActionArgs* const args)
{
    const struct Seen a = seen(s_cap[0]), b = seen(s_cap[1]);
    FTESTLOG("captives: a listed %d where %s on_death %s; b listed %d where %s on_death %s", (int)a.listed, a.where, a.on_death, (int)b.listed, b.where, b.on_death);
    CHECK_TRUE("both captives are listed as prisoners, in the prison", a.listed && b.listed && strcmp(a.where, "prison") == 0 && strcmp(b.where, "prison") == 0);
    const struct Room* prison = room_get(s_prison);
    const ThingModel rises = prison_death_kind(thing_get(s_cap[0]), prison);
    CHECK_TRUE("a humanoid prisoner would rise as the prison's kind if it starved", rises > 0 && strcmp(a.on_death, creature_code_name(rises)) == 0);
    char r[300];
    decision(r, sizeof(r));
    FTESTLOG("reasons: %s", r);
    CHECK_TRUE("new prisoners raise a decision", strstr(r, "new_prisoner") != NULL);
    CHECK_TRUE("the hatchery's chickens are listed for the hand", chickens_listed() >= 3);
    // Refusals.
    if (s_free_enemy) expect("an enemy not in custody", carry(s_free_enemy, s_torture), "NOT_YOURS");
    struct ExtSeatVerb out = carry(s_cap[0], 0);
    out.has_pos = true; out.stl_x = slab_subtile_center(hsx + 8); out.stl_y = slab_subtile_center(hsy + 4);
    expect("a prisoner dropped on open floor would go free", out, "WOULD_RELEASE_PRISONER");
    struct ExtSeatVerb nine; memset(&nine, 0, sizeof(nine));
    nine.kind = ESV_PickUpAndDrop; nine.has_room = true; nine.room_id = s_prison;
    for (int i = 0; i < 9; i++) nine.thing_ids[nine.thing_count++] = s_chick[i % 3];
    expect("more things than the hand holds", nine, "TOO_MANY");
    CHECK_TRUE("refusals queued nothing", extseat_idle(U) && power_hand_is_empty(get_player(P)));
    // Three chickens to the prison in one order.
    struct ExtSeatVerb feed; memset(&feed, 0, sizeof(feed));
    feed.kind = ESV_PickUpAndDrop; feed.has_room = true; feed.room_id = s_prison;
    for (int i = 0; i < 3; i++) feed.thing_ids[feed.thing_count++] = s_chick[i];
    CHECK_TRUE("carrying three chickens to the prison is accepted", submit(feed, &s_ids[0]) == NULL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult cu04_check_chickens_then_torture(struct FTestActionArgs* const args)
{
    if (!settled(s_ids[0], args, 300)) return FTRs_Repeat_Current_Action;
    const struct ExtSeatResult* r = result_for(s_ids[0]);
    CHECK_TRUE("the feeding order completed", r && !r->rejected);
    CHECK_TRUE("the hand is empty afterwards", power_hand_is_empty(get_player(P)));
    int in_prison = 0;
    for (int i = 0; i < 3; i++) {
        const struct Thing* c = thing_get(s_chick[i]);
        // Eaten already, or lying in the prison.
        if (thing_is_invalid(c) || !thing_exists(c)) { in_prison++; continue; }
        const struct Room* room = get_room_thing_is_on(c);
        if (!room_is_invalid(room) && room->index == s_prison) in_prison++;
    }
    CHECK_TRUE("all three chickens reached the prison", in_prison == 3);
    CHECK_TRUE("moving a prisoner to the torture chamber is accepted", submit(carry(s_cap[0], s_torture), &s_ids[1]) == NULL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult cu05_check_torture_then_heal(struct FTestActionArgs* const args)
{
    if (!settled(s_ids[1], args, 300)) return FTRs_Repeat_Current_Action;
    const struct Seen a = seen(s_cap[0]);
    if (strcmp(a.where, "torture") != 0 && args->intended_start_at_game_turn > 0 && (int64_t)get_gameturn() < args->intended_start_at_game_turn + 200)
        return FTRs_Repeat_Current_Action; // walking to its device
    FTESTLOG("in torture: where %s on_death %s turns_in %" PRId64 " break_time %" PRId64, a.where, a.on_death, a.turns_in, a.break_time);
    const struct ExtSeatResult* r = result_for(s_ids[1]);
    CHECK_TRUE("the move to the torture chamber completed", r && !r->rejected);
    CHECK_TRUE("the prisoner is now listed in the torture chamber, still in custody", a.listed && strcmp(a.where, "torture") == 0);
    CHECK_TRUE("with its time on the rack and its kind's break time", a.turns_in >= 0 && a.break_time > 0);
    // Heal the other prisoner.
    struct Thing* b = thing_get(s_cap[1]);
    b->health = creature_control_get_from_thing(b)->max_health / 3;
    s_hurt_health = b->health;
    struct ExtSeatVerb heal; memset(&heal, 0, sizeof(heal));
    heal.kind = ESV_CastPower; snprintf(heal.name, sizeof(heal.name), "POWER_HEAL_CREATURE"); heal.has_thing = true; heal.thing_id = s_cap[1];
    const char* e = submit(heal, &s_ids[2]);
    CHECK_TRUE("Heal on a prisoner is accepted", e == NULL);
    if (e) FTESTLOG("heal refused: %s", e);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult cu06_check_heal_then_release(struct FTestActionArgs* const args)
{
    if (!settled(s_ids[2], args, 100)) return FTRs_Repeat_Current_Action;
    const struct Thing* b = thing_get(s_cap[1]);
    FTESTLOG("healed prisoner: %" PRId64 " -> %" PRId64, (int64_t)s_hurt_health, (int64_t)b->health);
    CHECK_TRUE("Heal raised the prisoner's health", b->health > s_hurt_health);
    struct ExtSeatVerb out = carry(s_cap[1], 0);
    out.has_pos = true; out.stl_x = slab_subtile_center(hsx + 8); out.stl_y = slab_subtile_center(hsy + 4); out.release = true;
    CHECK_TRUE("releasing a prisoner on purpose is accepted", submit(out, &s_ids[3]) == NULL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult cu07_check_release(struct FTestActionArgs* const args)
{
    if (!settled(s_ids[3], args, 300)) return FTRs_Repeat_Current_Action;
    const struct Thing* b = thing_get(s_cap[1]);
    const TbBool freed = !creature_is_kept_in_custody_by_player(b, P);
    if (!freed && args->intended_start_at_game_turn > 0 && (int64_t)get_gameturn() < args->intended_start_at_game_turn + 100) return FTRs_Repeat_Current_Action;
    CHECK_TRUE("the released prisoner is out of custody and its owner's again", freed && b->owner == PLAYER_GOOD && !seen(s_cap[1]).listed);
    char r[300];
    decision(r, sizeof(r));
    FTESTLOG("reasons after release: %s", r);
    if (strstr(r, "prisoner_gone") == NULL && args->intended_start_at_game_turn > 0 && (int64_t)get_gameturn() < args->intended_start_at_game_turn + 150)
        return FTRs_Repeat_Current_Action; // held by the minimum interval
    CHECK_TRUE("a prisoner leaving custody raises prisoner_gone", strstr(r, "prisoner_gone") != NULL);
    api_seat_decision_set_min_interval(100);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " custody check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: prisoners and food are listed, carried, healed and released only as ordered");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
