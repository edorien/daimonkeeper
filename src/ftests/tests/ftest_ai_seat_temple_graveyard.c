// Graveyard and temple for an External seat (docs/refactor/AI/omissions/06). Temple: a 5x5 temple reports a pool, a 2x2
// one none; the active recipes appear in rules.sacrifices only once the seat owns a temple; to_room temple puts a
// creature beside the pool (it lives), a pool drop without sacrifice: true is WOULD_SACRIFICE, and two FLYs sacrificed
// fire the FLY+FLY recipe (sacrifice_result, reported in own.sacrifices.outcomes, offered counts reset); a prisoner can
// be sacrificed too and counts toward the seat's offerings. Graveyard: progress toward the next vampire, and a body
// rotting away that completes it raises a vampire for the seat (vampire_raised).
#include "ftest_ai_seat_temple_graveyard.h"

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
#include "config_rules.h"
#include "creature_control.h"
#include "creature_states.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_config_state.h"
#include "net_game.h"
#include "player_availability.h"
#include "player_data.h"
#include "room_data.h"
#include "room_graveyard.h"
#include "slab_data.h"
#include "thing_corpses.h"
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
static RoomIndex s_big = 0, s_small = 0;
static ThingIndex s_fly[3], s_captive = 0;
static char s_recipe[160];
static int64_t s_ids[4];
static int64_t s_vamps_before = 0;
static TbBool s_saw_sacrifice = false, s_saw_vampire = false;
static int64_t s_pool_x = -1, s_pool_y = -1;

static const char* submit(struct ExtSeatVerb v, int64_t* id)
{
    struct ExtSeatSubmitInfo info; memset(&info, 0, sizeof(info));
    const char* e = extseat_submit_verb_ex(U, P, &v, true, &info);
    if (id) *id = info.id;
    return e;
}
static struct ExtSeatVerb carry(ThingIndex t, RoomIndex room, TbBool sacrifice)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_PickUpAndDrop; v.has_thing = true; v.thing_id = t; v.has_room = true; v.room_id = room; v.sacrifice = sacrifice;
    return v;
}
static const struct ExtSeatResult* result_for(int64_t id)
{
    static struct ExtSeatResult res[EXTSEAT_RESULT_RING];
    const int64_t n = extseat_results(U, res, EXTSEAT_RESULT_RING);
    for (int64_t k = 0; k < n; k++) if (res[k].id == id) return &res[k];
    return NULL;
}
static TbBool settled(int64_t id) { return extseat_idle(U) && result_for(id) != NULL; }
static TbBool timed_out(const struct FTestActionArgs* args, int64_t budget)
{
    return (args->intended_start_at_game_turn > 0) && ((int64_t)get_gameturn() > args->intended_start_at_game_turn + budget);
}
static void watch_reasons(void)
{
    VALUE v; api_seat_build_view(&v, P);
    VALUE* arr = value_dict_get(value_dict_get(value_dict_get(&v, "seat"), "decision"), "reasons");
    for (size_t i = 0; arr && i < value_array_size(arr); i++) {
        const char* r = value_string(value_array_get(arr, i));
        if (strcmp(r, "sacrifice_result") == 0) s_saw_sacrifice = true;
        if (strcmp(r, "vampire_raised") == 0) s_saw_vampire = true;
    }
    value_fini(&v);
}
static int64_t count_owned(const char* kind)
{
    const ThingModel m = (ThingModel)creature_model_id(kind);
    int64_t n = 0;
    int64_t idx = kfx_sim_state.thing_lists[TngList_Creatures].index;
    for (int64_t g = 0; (idx > 0) && (g < 4000); g++) {
        const struct Thing* t = thing_get(idx); idx = t->next_of_class;
        if (thing_is_creature(t) && (t->owner == P) && (t->model == m)) n++;
    }
    return n;
}
static TbBool recipes_listed(TbBool* has_expected)
{
    VALUE v; api_seat_build_view(&v, P);
    VALUE* list = value_dict_get(value_dict_get(&v, "rules"), "sacrifices");
    *has_expected = false;
    for (size_t i = 0; list && i < value_array_size(list); i++) if (strcmp(value_string(value_array_get(list, i)), s_recipe) == 0) *has_expected = true;
    const TbBool listed = list != NULL;
    value_fini(&v);
    return listed;
}

FTestActionResult tg01_setup(struct FTestActionArgs* const args);
FTestActionResult tg02_check_rooms_then_pray(struct FTestActionArgs* const args);
FTestActionResult tg03_check_prayed_then_first_fly(struct FTestActionArgs* const args);
FTestActionResult tg04_second_fly(struct FTestActionArgs* const args);
FTestActionResult tg05_check_recipe_then_prisoner(struct FTestActionArgs* const args);
FTestActionResult tg06_check_prisoner_and_vampire(struct FTestActionArgs* const args);

void ftest_ai_seat_temple_graveyard_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_temple_graveyard_init()
{
    s_failures = 0;
    ftest_append_action(tg01_setup, 0, NULL);
    ftest_append_action(tg02_check_rooms_then_pray, 3, NULL);
    ftest_append_action(tg03_check_prayed_then_first_fly, 1, NULL);
    ftest_append_action(tg04_second_fly, 1, NULL);
    ftest_append_action(tg05_check_recipe_then_prisoner, 1, NULL);
    ftest_append_action(tg06_check_prisoner_and_vampire, 1, NULL);
    return true;
}

FTestActionResult tg01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || (U = net_add_external_seat(P)) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    api_seat_decision_set_min_interval(10);
    const struct Thing* heart = find_players_dungeon_heart(P);
    hsx = subtile_slab(heart->mappos.x.stl.num); hsy = subtile_slab(heart->mappos.y.stl.num);
    // The recipe two FLYs fire, as rules.sacrifices will name it.
    s_recipe[0] = 0;
    const ThingModel fly = (ThingModel)creature_model_id("FLY");
    for (int r = 0; r < MAX_SACRIFICE_RECIPES && !s_recipe[0]; r++) {
        const struct SacrificeRecipe* sac = &kfx_config_state.conf.rules[0].sacrifices.sacrifice_recipes[r];
        int flies = 0, others = 0;
        for (int i = 0; i < MAX_SACRIFICE_VICTIMS; i++) { if (sac->victims[i] == fly) flies++; else if (sac->victims[i] > 0) others++; }
        if (sac->action != 0 && flies == 2 && others == 0) api_seat_recipe_text(sac, s_recipe, sizeof(s_recipe));
    }
    FTESTLOG("the FLY+FLY recipe: '%s'", s_recipe);
    if (!s_recipe[0]) SOFT_FAIL("no FLY+FLY recipe in the loaded rules");
    TbBool expected;
    CHECK_TRUE("no temple, no recipes in the view", !recipes_listed(&expected));
    ftest_util_replace_slabs(hsx - 4, hsy + 3, hsx + 9, hsy + 9, SlbT_CLAIMED, P);
    set_room_available(P, RoK_TEMPLE, 1, 1); set_room_available(P, RoK_GRAVEYARD, 1, 1); set_room_available(P, RoK_PRISON, 1, 1);
    ftest_util_replace_slabs(hsx, hsy + 3, hsx + 4, hsy + 7, SlbT_TEMPLE, P);
    ftest_util_replace_slabs(hsx - 4, hsy + 8, hsx - 3, hsy + 9, SlbT_TEMPLE, P);
    ftest_util_replace_slabs(hsx + 6, hsy + 3, hsx + 8, hsy + 5, SlbT_GRAVEYARD, P);
    ftest_util_replace_slabs(hsx - 4, hsy + 3, hsx - 2, hsy + 5, SlbT_PRISON, P);
    const struct Room* big = slab_room_get(hsx + 2, hsy + 5);
    const struct Room* small = slab_room_get(hsx - 4, hsy + 8);
    struct Room* grave = slab_room_get(hsx + 7, hsy + 4);
    if (room_is_invalid(big) || room_is_invalid(small) || room_is_invalid(grave)) { FTEST_FAIL_TEST("rooms not built"); return FTRs_Go_To_Next_Action; }
    s_big = big->index; s_small = small->index;
    for (int i = 0; i < 3; i++) {
        struct Thing* f = ftest_util_create_creature(subtile_coord_center(slab_subtile_center(hsx + 7 + i)), subtile_coord_center(slab_subtile_center(hsy + 8)), P, 1, fly);
        s_fly[i] = thing_is_invalid(f) ? 0 : f->index;
    }
    // A hero knocked out and carried into the prison, for the prisoner sacrifice.
    struct Coord3d centre;
    set_coords_to_slab_center(&centre, hsx - 3, hsy + 4);
    struct Thing* guard = ftest_util_create_creature(centre.x.val, centre.y.val, P, 1, (ThingModel)creature_model_id("ORC"));
    struct Thing* h = ftest_util_create_creature(centre.x.val, centre.y.val, PLAYER_GOOD, 2, (ThingModel)creature_model_id("ARCHER"));
    if (thing_is_invalid(guard) || thing_is_invalid(h)) { FTEST_FAIL_TEST("no guard/captive"); return FTRs_Go_To_Next_Action; }
    make_creature_unconscious(h);
    controlled_creature_drop_thing(guard, h, P);
    s_captive = h->index;
    // A body laid to rest in the graveyard, about to finish rotting, with the counter one short of a vampire.
    struct Coord3d gpos;
    set_coords_to_slab_center(&gpos, hsx + 7, hsy + 4);
    struct Thing* body = create_dead_creature(&gpos, (ThingModel)creature_model_id("ARCHER"), 1, PLAYER_GOOD, 0);
    if (thing_is_invalid(body) || !add_body_to_graveyard(body, grave)) { FTEST_FAIL_TEST("no body in the graveyard"); return FTRs_Go_To_Next_Action; }
    body->health = 30;
    struct Dungeon* d = get_players_dungeon(get_player(P));
    d->bodies_rotten_for_vampire = kfx_config_state.conf.rules[P].rooms.bodies_for_vampire - 1;
    s_vamps_before = count_owned("VAMPIRE");
    return FTRs_Go_To_Next_Action;
}

FTestActionResult tg02_check_rooms_then_pray(struct FTestActionArgs* const args)
{
    VALUE v; api_seat_build_view(&v, P);
    VALUE* own = value_dict_get(&v, "own");
    VALUE* temples = value_dict_get(own, "temples");
    TbBool big_pool = false, small_no_pool = false;
    for (size_t i = 0; temples && i < value_array_size(temples); i++) {
        VALUE* t = value_array_get(temples, i);
        VALUE* pool = value_dict_get(t, "pool");
        if (value_int32(value_dict_get(t, "id")) == s_big && pool) { big_pool = true; s_pool_x = value_int32(value_array_get(pool, 0)); s_pool_y = value_int32(value_array_get(pool, 1)); }
        if (value_int32(value_dict_get(t, "id")) == s_small && !pool) small_no_pool = true;
    }
    VALUE* gy = value_dict_get(own, "graveyard");
    const int64_t toward = gy ? value_int64(value_dict_get(gy, "toward_vampire")) : -1;
    const int64_t per = gy ? value_int64(value_dict_get(gy, "per_vampire")) : -1;
    value_fini(&v);
    CHECK_TRUE("a 5x5 temple has a pool", big_pool);
    CHECK_TRUE("a 2x2 temple has none", small_no_pool);
    TbBool expected;
    CHECK_TRUE("with a temple the active recipes are listed, the FLY+FLY one among them", recipes_listed(&expected) && expected);
    CHECK_TRUE("the graveyard reports progress toward the next vampire", per > 0 && toward == per - 1);
    CHECK_TRUE("to_room temple (no sacrifice) is accepted", submit(carry(s_fly[0], s_big, false), &s_ids[0]) == NULL);
    struct ExtSeatVerb pool = carry(s_fly[1], 0, false);
    pool.has_room = false; pool.has_pos = true; pool.stl_x = s_pool_x; pool.stl_y = s_pool_y;
    const char* e = submit(pool, NULL);
    CHECK_TRUE("a drop on the pool without sacrifice: true is refused", e && strcmp(e, "WOULD_SACRIFICE") == 0);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult tg03_check_prayed_then_first_fly(struct FTestActionArgs* const args)
{
    watch_reasons();
    if (!settled(s_ids[0])) { if (timed_out(args, 200)) SOFT_FAIL("the temple drop never finished"); else return FTRs_Repeat_Current_Action; }
    const struct Thing* f = thing_get(s_fly[0]);
    CHECK_TRUE("the fly dropped beside the pool lives on, still the seat's", thing_exists(f) && thing_is_creature(f) && f->owner == P && !creature_is_being_sacrificed(f));
    CHECK_TRUE("sacrificing a fly is accepted", submit(carry(s_fly[1], s_big, true), &s_ids[1]) == NULL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult tg04_second_fly(struct FTestActionArgs* const args)
{
    watch_reasons();
    const struct Dungeon* d = get_players_dungeon(get_player(P));
    if (d->creature_sacrifice[creature_model_id("FLY")] < 1) {
        if (timed_out(args, 300)) { SOFT_FAIL("the first fly was never offered"); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    if (!extseat_idle(U)) return FTRs_Repeat_Current_Action;
    CHECK_TRUE("sacrificing the second fly is accepted", submit(carry(s_fly[2], s_big, true), &s_ids[2]) == NULL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult tg05_check_recipe_then_prisoner(struct FTestActionArgs* const args)
{
    watch_reasons();
    VALUE v; api_seat_build_view(&v, P);
    VALUE* sac = value_dict_get(value_dict_get(&v, "own"), "sacrifices");
    VALUE* outcomes = value_dict_get(sac, "outcomes");
    TbBool fired = false;
    for (size_t i = 0; outcomes && i < value_array_size(outcomes); i++)
        if (strcmp(value_string(value_dict_get(value_array_get(outcomes, i), "recipe")), s_recipe) == 0) fired = true;
    const TbBool flies_left = value_dict_get(value_dict_get(sac, "offered"), "FLY") != NULL;
    value_fini(&v);
    if (!fired || !s_saw_sacrifice) {
        if (timed_out(args, 300)) { SOFT_FAIL("the FLY+FLY recipe was not reported (fired %d, reason %d)", (int)fired, (int)s_saw_sacrifice); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    CHECK_TRUE("the recipe used the flies up: none left on offer", !flies_left);
    // The prisoner, from the prison to the pool.
    if (!creature_is_kept_in_prison(thing_get(s_captive))) {
        if (timed_out(args, 400)) { SOFT_FAIL("the captive never reached the prison"); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    const char* e = submit(carry(s_captive, s_big, true), &s_ids[3]);
    CHECK_TRUE("sacrificing a prisoner is accepted", e == NULL);
    if (e) FTESTLOG("refused: %s", e);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult tg06_check_prisoner_and_vampire(struct FTestActionArgs* const args)
{
    watch_reasons();
    const struct Dungeon* d = get_players_dungeon(get_player(P));
    const TbBool offered = d->creature_sacrifice[creature_model_id("ARCHER")] > 0;
    const TbBool vampire = count_owned("VAMPIRE") > s_vamps_before;
    if (!offered || !vampire || !s_saw_vampire) {
        if (!timed_out(args, 400)) return FTRs_Repeat_Current_Action;
    }
    CHECK_TRUE("a sacrificed prisoner counts toward the seat's own offerings", offered);
    CHECK_TRUE("the body finishing its rot raised a vampire for the seat", vampire);
    CHECK_TRUE("and that raised vampire_raised", s_saw_vampire);
    api_seat_decision_set_min_interval(100);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " temple/graveyard check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: pools, recipes, sacrifices (a prisoner's too) and the graveyard's vampire reach the seat");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
