// Workforce vs army (docs/refactor/AI/omissions/01): imps and tunnellers dig, claim and carry but barely fight, so the
// view counts them as workers, not fighters. own.force and vis.force_by_owner give fighters, workers and a fighter score
// (the engine's own per-kind, per-level creature score, scaled by health left); visible creatures carry digger and level.
// Everything is measured as a change from a baseline, so whatever the map already holds does not matter.
#include "ftest_ai_seat_force.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>
#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api_seat_view.h"
#include "config_creature.h"
#include "config_keeperfx.h"
#include "creature_control.h"
#include "dungeon_data.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_config_state.h"
#include "net_game.h"
#include "player_data.h"
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
struct Force { int64_t fighters, workers, score; };
static struct Force s_own0, s_heroes0;
static ThingIndex s_orc[2], s_knight = 0, s_tunneller = 0;

static struct Force read_force(VALUE* f)
{
    struct Force r = { 0, 0, 0 };
    if (f == NULL) return r;
    r.fighters = value_int64(value_dict_get(f, "fighters"));
    r.workers = value_int64(value_dict_get(f, "workers"));
    r.score = value_int64(value_dict_get(f, "fighter_score"));
    return r;
}
static void forces(struct Force* own, struct Force* heroes)
{
    VALUE v; api_seat_build_view(&v, P);
    *own = read_force(value_dict_get(value_dict_get(&v, "own"), "force"));
    char key[8]; snprintf(key, sizeof(key), "%d", (int)PLAYER_GOOD);
    *heroes = read_force(value_dict_get(value_dict_get(value_dict_get(&v, "visible"), "force_by_owner"), key));
    value_fini(&v);
}
// A visible creature's digger flag and level, by id (-1 if not listed).
static int visible_flag(ThingIndex id, const char* field)
{
    VALUE v; api_seat_build_view(&v, P);
    VALUE* crs = value_dict_get(value_dict_get(&v, "visible"), "creatures");
    int r = -1;
    for (size_t i = 0; crs && i < value_array_size(crs); i++) {
        VALUE* e = value_array_get(crs, i);
        if (value_int32(value_dict_get(e, "id")) != id) continue;
        VALUE* f = value_dict_get(e, field);
        r = (value_type(f) == VALUE_BOOL) ? (int)value_bool(f) : (int)value_int32(f);
    }
    value_fini(&v);
    return r;
}

FTestActionResult fo01_setup_baseline_and_add(struct FTestActionArgs* const args);
FTestActionResult fo02_check_counts_then_wound(struct FTestActionArgs* const args);

void ftest_ai_seat_force_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_force_init()
{
    s_failures = 0;
    ftest_append_action(fo01_setup_baseline_and_add, 0, NULL);
    ftest_append_action(fo02_check_counts_then_wound, 1, NULL);
    return true;
}

FTestActionResult fo01_setup_baseline_and_add(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || net_add_external_seat(P) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    const struct Thing* heart = find_players_dungeon_heart(P);
    const MapSubtlCoord hx = heart->mappos.x.stl.num, hy = heart->mappos.y.stl.num;
    ftest_util_replace_slabs(subtile_slab(hx) - 4, subtile_slab(hy) + 3, subtile_slab(hx) + 9, subtile_slab(hy) + 5, SlbT_CLAIMED, P);
    forces(&s_own0, &s_heroes0);
    FTESTLOG("baseline: own %" PRId64 "/%" PRId64 "/%" PRId64 ", heroes %" PRId64 "/%" PRId64 "/%" PRId64,
        s_own0.fighters, s_own0.workers, s_own0.score, s_heroes0.fighters, s_heroes0.workers, s_heroes0.score);
    for (int i = 0; i < 3; i++)
        ftest_util_create_creature(subtile_coord_center(hx - 6 + 2 * i), subtile_coord_center(hy + 10), P, 1, (ThingModel)creature_model_id("IMP"));
    for (int i = 0; i < 2; i++) {
        struct Thing* o = ftest_util_create_creature(subtile_coord_center(hx + 2 + 2 * i), subtile_coord_center(hy + 10), P, 4, (ThingModel)creature_model_id("ORC"));
        s_orc[i] = thing_is_invalid(o) ? 0 : o->index;
    }
    // A hero party the seat can see (the map is revealed), well away from the seat's creatures.
    struct Thing* k = ftest_util_create_creature(subtile_coord_center(hx + 20), subtile_coord_center(hy + 14), PLAYER_GOOD, 1, (ThingModel)creature_model_id("KNIGHT"));
    struct Thing* t = ftest_util_create_creature(subtile_coord_center(hx + 22), subtile_coord_center(hy + 14), PLAYER_GOOD, 1, (ThingModel)creature_model_id("TUNNELLER"));
    s_knight = thing_is_invalid(k) ? 0 : k->index;
    s_tunneller = thing_is_invalid(t) ? 0 : t->index;
    if (!s_orc[0] || !s_orc[1] || !s_knight || !s_tunneller) FTEST_FAIL_TEST("could not create the creatures");
    return FTRs_Go_To_Next_Action;
}

FTestActionResult fo02_check_counts_then_wound(struct FTestActionArgs* const args)
{
    struct Force own, heroes;
    forces(&own, &heroes);
    const int64_t orc_score = get_creature_thing_score(thing_get(s_orc[0]));
    FTESTLOG("after: own %" PRId64 "/%" PRId64 "/%" PRId64 ", heroes %" PRId64 "/%" PRId64 "/%" PRId64 "; one orc scores %" PRId64,
        own.fighters, own.workers, own.score, heroes.fighters, heroes.workers, heroes.score, orc_score);
    CHECK_TRUE("the two orcs are fighters, the three imps workers", (own.fighters - s_own0.fighters == 2) && (own.workers - s_own0.workers == 3));
    CHECK_TRUE("the fighter score grows by the two orcs' engine scores, and the imps add nothing",
        orc_score > 0 && own.score - s_own0.score == 2 * orc_score);
    CHECK_TRUE("a visible knight is a hero fighter, a visible tunneller a hero worker",
        (heroes.fighters - s_heroes0.fighters == 1) && (heroes.workers - s_heroes0.workers == 1));
    CHECK_TRUE("the tunneller is marked a digger", visible_flag(s_tunneller, "digger") == 1);
    CHECK_TRUE("the knight is not", visible_flag(s_knight, "digger") == 0);
    CHECK_TRUE("visible creatures carry their level", visible_flag(s_knight, "level") == 1);
    // Half the health, half the worth.
    struct Thing* o = thing_get(s_orc[1]);
    const struct CreatureControl* cctrl = creature_control_get_from_thing(o);
    o->health = cctrl->max_health / 2;
    const int64_t expect = orc_score * (cctrl->max_health / 2) / cctrl->max_health;
    struct Force wounded, h2;
    forces(&wounded, &h2);
    CHECK_TRUE("a half-dead orc counts for half its score", wounded.score - s_own0.score == orc_score + expect);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " force check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: workers are not counted as fighters, and the fighter score follows the engine's own scores and health");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
