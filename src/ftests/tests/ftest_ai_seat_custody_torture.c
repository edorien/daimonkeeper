// What torture yields, as the External seat hears of it (docs/refactor/AI/omissions/05): three prisoners carried to
// the torture chamber in one order (thing_ids + to_room), then, with the rules' chances set to 0 or 100 so the outcome
// is certain: one broken with convert chance 0 is interrogated (its points fall back: prisoner_interrogated), one broken
// with convert chance 100 joins the seat (prisoner_converted), and one tortured past zero health becomes the torture
// chamber's kind for the seat (the view's on_death said which), leaving custody (prisoner_gone). Also: three prisoners
// are refused ROOM_FULL for a chamber with room for fewer (the engine would turn the extra ones away, free).
#include "ftest_ai_seat_custody_torture.h"

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
#include "creature_control.h"
#include "creature_states.h"
#include "creature_states_tortr.h"
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
#include "thing_creature.h"
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
static RoomIndex s_torture = 0, s_small = 0;
static ThingIndex s_v[3];
static int64_t s_move_id = 0;
static unsigned char s_saved_convert, s_saved_death, s_saved_ghost;
static ThingModel s_ghost_kind = 0;
static int64_t s_saved_loss_turns = -1;
static int64_t s_ghosts_before = 0;

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
static TbBool timed_out(const struct FTestActionArgs* args, int64_t budget)
{
    return (args->intended_start_at_game_turn > 0) && ((int64_t)get_gameturn() > args->intended_start_at_game_turn + budget);
}
// Push a victim's accumulated torture points far past its break time, so it breaks on (about) the next turn.
static void break_soon(ThingIndex id)
{
    struct Thing* t = thing_get(id);
    struct CreatureControl* cctrl = creature_control_get_from_thing(t);
    const int64_t over = creature_stats_get_from_thing(t)->torture_break_time + 7000;
    cctrl->tortured.accumulated_torture_points = over * TORTURE_ACCUM_FAC * ROOM_EFFICIENCY_MAX;
}
static void calm(ThingIndex id)
{
    creature_control_get_from_thing(thing_get(id))->tortured.accumulated_torture_points = 0;
}
static int64_t count_owned(ThingModel model)
{
    int64_t n = 0;
    int64_t idx = kfx_sim_state.thing_lists[TngList_Creatures].index;
    for (int64_t g = 0; (idx > 0) && (g < 4000); g++) {
        const struct Thing* t = thing_get(idx); idx = t->next_of_class;
        if (thing_is_creature(t) && (t->owner == P) && (t->model == model)) n++;
    }
    return n;
}

FTestActionResult ct01_setup(struct FTestActionArgs* const args);
FTestActionResult ct02_capture(struct FTestActionArgs* const args);
FTestActionResult ct03_carry_to_torture(struct FTestActionArgs* const args);
FTestActionResult ct04_wait_on_devices_then_interrogate(struct FTestActionArgs* const args);
FTestActionResult ct05_check_interrogated_then_convert(struct FTestActionArgs* const args);
FTestActionResult ct06_check_converted_then_kill(struct FTestActionArgs* const args);
FTestActionResult ct07_check_death(struct FTestActionArgs* const args);

void ftest_ai_seat_custody_torture_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_custody_torture_init()
{
    s_failures = 0;
    ftest_append_action(ct01_setup, 0, NULL);
    ftest_append_action(ct02_capture, 2, NULL);
    ftest_append_action(ct03_carry_to_torture, 12, NULL);
    ftest_append_action(ct04_wait_on_devices_then_interrogate, 1, NULL);
    ftest_append_action(ct05_check_interrogated_then_convert, 1, NULL);
    ftest_append_action(ct06_check_converted_then_kill, 1, NULL);
    ftest_append_action(ct07_check_death, 1, NULL);
    return true;
}

FTestActionResult ct01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || (U = net_add_external_seat(P)) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    api_seat_decision_set_min_interval(10);
    const struct Thing* heart = find_players_dungeon_heart(P);
    hsx = subtile_slab(heart->mappos.x.stl.num); hsy = subtile_slab(heart->mappos.y.stl.num);
    ftest_util_replace_slabs(hsx - 4, hsy + 3, hsx + 9, hsy + 8, SlbT_CLAIMED, P);
    set_room_available(P, RoK_PRISON, 1, 1); set_room_available(P, RoK_TORTURE, 1, 1);
    ftest_util_replace_slabs(hsx - 4, hsy + 3, hsx - 2, hsy + 5, SlbT_PRISON, P);
    // A small chamber (room for about one) and a big one.
    ftest_util_replace_slabs(hsx - 4, hsy + 7, hsx - 3, hsy + 8, SlbT_TORTURE, P);
    ftest_util_replace_slabs(hsx, hsy + 3, hsx + 4, hsy + 7, SlbT_TORTURE, P);
    const struct Room* torture = slab_room_get(hsx + 2, hsy + 5);
    const struct Room* small = slab_room_get(hsx - 4, hsy + 7);
    if (room_is_invalid(torture) || room_is_invalid(small)) { FTEST_FAIL_TEST("no torture chambers"); return FTRs_Go_To_Next_Action; }
    s_torture = torture->index;
    s_small = small->index;
    struct RoomRulesConfig* rr = &kfx_config_state.conf.rules[P].rooms;
    s_saved_convert = rr->torture_convert_chance; s_saved_death = rr->torture_death_chance; s_saved_ghost = rr->ghost_convert_chance;
    rr->torture_convert_chance = 0; rr->torture_death_chance = 0; rr->ghost_convert_chance = 100;
    struct Coord3d centre;
    set_coords_to_slab_center(&centre, hsx - 3, hsy + 4);
    ftest_util_create_creature(centre.x.val, centre.y.val, P, 1, (ThingModel)creature_model_id("ORC"));
    static const char* kinds[3] = { "BARBARIAN", "ARCHER", "MONK" };
    for (int i = 0; i < 3; i++) {
        struct Thing* h = ftest_util_create_creature(centre.x.val, centre.y.val, PLAYER_GOOD, 3, (ThingModel)creature_model_id(kinds[i]));
        if (thing_is_invalid(h)) { FTEST_FAIL_TEST("no captive"); return FTRs_Go_To_Next_Action; }
        make_creature_unconscious(h);
        s_v[i] = h->index;
    }
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ct02_capture(struct FTestActionArgs* const args)
{
    struct Thing* guard = NULL;
    int64_t idx = kfx_sim_state.thing_lists[TngList_Creatures].index;
    for (int64_t g = 0; (idx > 0) && (g < 4000) && (guard == NULL); g++) {
        struct Thing* t = thing_get(idx); idx = t->next_of_class;
        if (thing_is_creature(t) && (t->owner == P) && (t->model == creature_model_id("ORC")) && (subtile_slab(t->mappos.x.stl.num) == hsx - 3)) guard = t;
    }
    if (guard == NULL) { FTEST_FAIL_TEST("no guard"); return FTRs_Go_To_Next_Action; }
    for (int i = 0; i < 3; i++) controlled_creature_drop_thing(guard, thing_get(s_v[i]), P);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ct03_carry_to_torture(struct FTestActionArgs* const args)
{
    for (int i = 0; i < 3; i++) if (!creature_is_kept_in_prison(thing_get(s_v[i]))) {
        if (timed_out(args, 100)) { SOFT_FAIL("captive %d never reached the prison", i); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    {
        const struct Room* tr = room_get(s_torture);
        FTESTLOG("torture chamber: %d slabs, efficiency %d, capacity used %d / total %d", (int)tr->slabs_count, (int)tr->efficiency, (int)tr->used_capacity, (int)tr->total_capacity);
    }
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_PickUpAndDrop; v.has_room = true; v.room_id = s_small;
    for (int i = 0; i < 3; i++) v.thing_ids[v.thing_count++] = s_v[i];
    struct ExtSeatSubmitInfo info; memset(&info, 0, sizeof(info));
    // Three into a chamber with room for fewer: the engine would turn the extra ones away on arrival, free.
    const char* e = extseat_submit_verb_ex(U, P, &v, true, &info);
    CHECK_TRUE("more prisoners than a torture chamber holds is refused ROOM_FULL", e && strcmp(e, "ROOM_FULL") == 0);
    v.room_id = s_torture;
    e = extseat_submit_verb_ex(U, P, &v, true, &info);
    CHECK_TRUE("three prisoners carried to the torture chamber in one order", e == NULL);
    if (e) FTESTLOG("refused: %s", e);
    s_move_id = info.id;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ct04_wait_on_devices_then_interrogate(struct FTestActionArgs* const args)
{
    int on = 0;
    for (int i = 0; i < 3; i++) {
        const struct Thing* t = thing_get(s_v[i]);
        if (creature_is_being_tortured(t) && (creature_control_get_from_thing(t)->tortured.assigned_torturer != 0)) on++;
    }
    if (on < 3) {
        if (timed_out(args, 600)) { SOFT_FAIL("only %d of 3 victims reached a torture device", on); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    for (int i = 0; i < 3; i++) calm(s_v[i]);
    break_soon(s_v[1]); // convert chance 0, death 0: breaking can only be interrogation
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ct05_check_interrogated_then_convert(struct FTestActionArgs* const args)
{
    char r[300];
    decision(r, sizeof(r));
    if (strstr(r, "prisoner_interrogated") == NULL) {
        if (timed_out(args, 150)) { SOFT_FAIL("no prisoner_interrogated (reasons '%s')", r); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    CHECK_TRUE("the interrogated victim is still a prisoner", thing_get(s_v[1])->owner == PLAYER_GOOD && creature_is_kept_in_custody_by_player(thing_get(s_v[1]), P));
    calm(s_v[1]);
    kfx_config_state.conf.rules[P].rooms.torture_convert_chance = 100;
    break_soon(s_v[0]);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ct06_check_converted_then_kill(struct FTestActionArgs* const args)
{
    char r[300];
    decision(r, sizeof(r));
    if (strstr(r, "prisoner_converted") == NULL) {
        if (timed_out(args, 150)) { SOFT_FAIL("no prisoner_converted (reasons '%s', owner %d)", r, (int)thing_get(s_v[0])->owner); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    CHECK_TRUE("the converted victim now belongs to the seat", thing_get(s_v[0])->owner == P);
    kfx_config_state.conf.rules[P].rooms.torture_convert_chance = 0;
    calm(s_v[1]);
    // Tortured past zero health, with ghost chance 100: becomes the chamber's kind for the seat.
    struct Thing* c = thing_get(s_v[2]);
    s_ghost_kind = torture_death_kind(c, room_get(s_torture));
    s_ghosts_before = count_owned(s_ghost_kind);
    // As the rack does it: one health left and torture damage every turn (GameTurnsPerTortureHealthLoss 1, restored
    // below), so the damage and the ghost check happen together in process_torture_function -- setting health below
    // zero directly would let the ordinary death handling kill it first.
    c->health = 1;
    s_saved_loss_turns = kfx_config_state.conf.rules[P].health.turns_per_torture_health_loss;
    kfx_config_state.conf.rules[P].health.turns_per_torture_health_loss = 1;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ct07_check_death(struct FTestActionArgs* const args)
{
    char r[300];
    decision(r, sizeof(r));
    if ((strstr(r, "prisoner_gone") == NULL) || (count_owned(s_ghost_kind) <= s_ghosts_before)) {
        if (timed_out(args, 150)) {
            SOFT_FAIL("the dying victim did not become a %s for the seat (reasons '%s', %s owned %" PRId64 " -> %" PRId64 ")",
                creature_code_name(s_ghost_kind), r, creature_code_name(s_ghost_kind), s_ghosts_before, count_owned(s_ghost_kind));
            goto done;
        }
        return FTRs_Repeat_Current_Action;
    }
    FTESTLOG("the victim rose as a %s for the seat", creature_code_name(s_ghost_kind));
done:
    {
        struct RoomRulesConfig* rr = &kfx_config_state.conf.rules[P].rooms;
        rr->torture_convert_chance = s_saved_convert; rr->torture_death_chance = s_saved_death; rr->ghost_convert_chance = s_saved_ghost;
        if (s_saved_loss_turns >= 0) kfx_config_state.conf.rules[P].health.turns_per_torture_health_loss = s_saved_loss_turns;
    }
    api_seat_decision_set_min_interval(100);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " torture check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: interrogation, conversion and death on the rack reach the seat as decision reasons");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
