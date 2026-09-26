// M6 action set: casting through the seat. cast_power on a creature (with and without overcharge), on a subtile (lightning),
// and the level-wide powers with no target (obey, hold audience), plus the view fields an agent needs to budget them
// (own.power_costs, own.events). The observable is what the engine charges: the gold the seat loses for a cast must equal
// the price the view promised for the charge level reached, so an overcharge that did not charge, or a power that did
// not fire, both show.
#include "ftest_ai_seat_powers.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>
#include <stdlib.h>
#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api_seat_view.h"
#include "config_creature.h"
#include "config_keeperfx.h"
#include "config_magic.h"
#include "config_players.h"
#include "creature_control.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "magic_powers.h"
#include "net_game.h"
#include "player_data.h"
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
static ThingIndex own_crtr = 0, enemy_crtr = 0;
static GoldAmount gold_before = 0;
static int64_t enemy_hp_before = 0, own_hp_before = 0;

static struct Dungeon* dg(void) { return get_players_dungeon(get_player(P)); }
static struct ExtSeatVerb cast(const char* power)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_CastPower; snprintf(v.name, sizeof(v.name), "%s", power);
    return v;
}
static struct ExtSeatVerb on_thing(const char* power, ThingIndex t, int64_t oc) { struct ExtSeatVerb v = cast(power); v.has_thing = true; v.thing_id = t; v.overcharge_turns = oc; return v; }
static struct ExtSeatVerb at_stl(const char* power, int64_t x, int64_t y) { struct ExtSeatVerb v = cast(power); v.has_pos = true; v.stl_x = x; v.stl_y = y; return v; }
static void submit_ok(const char* what, struct ExtSeatVerb v)
{
    const char* e = extseat_submit_verb(U, P, &v, NULL);
    if (e) SOFT_FAIL("%s: rejected with %s", what, e);
}
static int64_t s_idle_since = -1;
static FTestActionResult wait_settled(int64_t settle)
{
    if (!extseat_idle(U)) { s_idle_since = -1; return FTRs_Repeat_Current_Action; }
    if (s_idle_since < 0) s_idle_since = (int64_t)get_gameturn();
    if ((int64_t)get_gameturn() < s_idle_since + settle) return FTRs_Repeat_Current_Action;
    s_idle_since = -1;
    return FTRs_Go_To_Next_Action;
}
static GoldAmount price(const char* power, int64_t level)
{
    return compute_power_price(P, (PowerKind)power_model_id(power), (KeepPwrLevel)level);
}
static int64_t health(ThingIndex t) { const struct Thing* th = thing_get(t); return thing_is_invalid(th) ? -1 : th->health; }

FTestActionResult w01_setup(struct FTestActionArgs* const args);
FTestActionResult w02_view_fields_cast_heal(struct FTestActionArgs* const args);
FTestActionResult w03_heal_wait(struct FTestActionArgs* const args);
FTestActionResult w04_check_heal_overcharge(struct FTestActionArgs* const args);
FTestActionResult w05_overcharge_wait(struct FTestActionArgs* const args);
FTestActionResult w06_check_oc_lightning(struct FTestActionArgs* const args);
FTestActionResult w07_lightning_wait(struct FTestActionArgs* const args);
FTestActionResult w08_check_lightning_obey(struct FTestActionArgs* const args);
FTestActionResult w09_obey_wait(struct FTestActionArgs* const args);
FTestActionResult w10_check_obey_hold_validation(struct FTestActionArgs* const args);

void ftest_ai_seat_powers_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_powers_init()
{
    s_failures = 0; s_idle_since = -1;
    ftest_append_action(w01_setup, 0, NULL);
    ftest_append_action(w02_view_fields_cast_heal, 2, NULL);
    ftest_append_action(w03_heal_wait, 1, NULL);
    ftest_append_action(w04_check_heal_overcharge, 1, NULL);
    ftest_append_action(w05_overcharge_wait, 1, NULL);
    ftest_append_action(w06_check_oc_lightning, 1, NULL);
    ftest_append_action(w07_lightning_wait, 1, NULL);
    ftest_append_action(w08_check_lightning_obey, 1, NULL);
    ftest_append_action(w09_obey_wait, 1, NULL);
    ftest_append_action(w10_check_obey_hold_validation, 1, NULL);
    return true;
}

FTestActionResult w01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0) { FTEST_FAIL_TEST("no second keeper"); return FTRs_Go_To_Next_Action; }
    U = net_add_external_seat(P);
    if (U < 1) { FTEST_FAIL_TEST("net_add_external_seat failed"); return FTRs_Go_To_Next_Action; }
    ftest_util_reveal_map(P);
    const struct Thing* heart = find_players_dungeon_heart(P);
    const MapSubtlCoord hx = heart->mappos.x.stl.num, hy = heart->mappos.y.stl.num;
    struct Dungeon* d = dg();
    d->total_money_owned = 1000000;
    const char* names[] = { "POWER_HEAL_CREATURE", "POWER_LIGHTNING", "POWER_OBEY", "POWER_HOLD_AUDIENCE" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        d->magic_level[power_model_id(names[i])] = 8; // available, at max charge
    ftest_util_replace_slabs(subtile_slab(hx) - 5, subtile_slab(hy) + 3, subtile_slab(hx) + 5, subtile_slab(hy) + 6, SlbT_CLAIMED, P);
    struct Thing* a = ftest_util_create_creature(subtile_coord_center(hx - 3), subtile_coord_center(hy + 4), P, 9, (ThingModel)creature_model_id("ORC"));
    struct Thing* e = ftest_util_create_creature(subtile_coord_center(hx + 3), subtile_coord_center(hy + 4), my_player_number, 9, (ThingModel)creature_model_id("ORC"));
    if (thing_is_invalid(a) || thing_is_invalid(e)) { FTEST_FAIL_TEST("could not create the two creatures"); return FTRs_Go_To_Next_Action; }
    own_crtr = a->index; enemy_crtr = e->index;
    a->health = 5; // wounded, so a heal has room to show
    return FTRs_Go_To_Next_Action;
}

FTestActionResult w02_view_fields_cast_heal(struct FTestActionArgs* const args)
{
    VALUE view; api_seat_build_view(&view, P);
    VALUE* own = value_dict_get(&view, "own");
    VALUE* costs = own ? value_dict_get(own, "power_costs") : NULL;
    VALUE* heal = costs ? value_dict_get(costs, "POWER_HEAL_CREATURE") : NULL;
    CHECK_TRUE("the view lists the cost of each available power at each level", heal && value_array_size(heal) == MAGIC_OVERCHARGE_LEVELS);
    if (heal) {
        CHECK_TRUE("level 0 cost matches the engine's price", value_int64(value_array_get(heal, 0)) == price("POWER_HEAL_CREATURE", 0));
        CHECK_TRUE("level 2 cost matches the engine's price", value_int64(value_array_get(heal, 2)) == price("POWER_HEAL_CREATURE", 2));
    }
    CHECK_TRUE("the view has own.events", own && value_dict_get(own, "events") != NULL);
    value_fini(&view);

    gold_before = dg()->total_money_owned;
    own_hp_before = health(own_crtr);
    submit_ok("heal, no overcharge", on_thing("POWER_HEAL_CREATURE", own_crtr, 0));
    return FTRs_Go_To_Next_Action;
}
FTestActionResult w03_heal_wait(struct FTestActionArgs* const args) { return wait_settled(3); }

FTestActionResult w04_check_heal_overcharge(struct FTestActionArgs* const args)
{
    const int64_t spent = gold_before - dg()->total_money_owned;
    FTESTLOG("heal level 0: spent %" PRId64 " (price %" PRId64 "), health %" PRId64 " -> %" PRId64, spent, (int64_t)price("POWER_HEAL_CREATURE", 0), own_hp_before, health(own_crtr));
    CHECK_TRUE("the heal cast charged exactly the level-0 price", spent == price("POWER_HEAL_CREATURE", 0));
    CHECK_TRUE("the creature was healed", health(own_crtr) > own_hp_before);
    gold_before = dg()->total_money_owned;
    own_hp_before = health(own_crtr);
    struct Thing* a = thing_get(own_crtr);
    if (!thing_is_invalid(a)) a->health = 5;
    submit_ok("heal, 8 held turns", on_thing("POWER_HEAL_CREATURE", own_crtr, 8));
    return FTRs_Go_To_Next_Action;
}
FTestActionResult w05_overcharge_wait(struct FTestActionArgs* const args) { return wait_settled(3); }

FTestActionResult w06_check_oc_lightning(struct FTestActionArgs* const args)
{
    const int64_t spent = gold_before - dg()->total_money_owned;
    FTESTLOG("heal, 8 held turns (level 2): spent %" PRId64 " (price %" PRId64 ")", spent, (int64_t)price("POWER_HEAL_CREATURE", 2));
    CHECK_TRUE("the overcharged heal charged the level-2 price", spent == price("POWER_HEAL_CREATURE", 2));
    CHECK_TRUE("the overcharged heal healed", health(own_crtr) > 5);

    gold_before = dg()->total_money_owned;
    const struct Thing* e = thing_get(enemy_crtr);
    enemy_hp_before = health(enemy_crtr);
    submit_ok("lightning at the enemy's subtile", at_stl("POWER_LIGHTNING", e->mappos.x.stl.num, e->mappos.y.stl.num));
    return FTRs_Go_To_Next_Action;
}
FTestActionResult w07_lightning_wait(struct FTestActionArgs* const args) { return wait_settled(3); }

FTestActionResult w08_check_lightning_obey(struct FTestActionArgs* const args)
{
    const int64_t spent = gold_before - dg()->total_money_owned;
    FTESTLOG("lightning: spent %" PRId64 " (price %" PRId64 "), enemy health %" PRId64 " -> %" PRId64, spent, (int64_t)price("POWER_LIGHTNING", 0), enemy_hp_before, health(enemy_crtr));
    CHECK_TRUE("lightning charged the level-0 price", spent == price("POWER_LIGHTNING", 0));
    CHECK_TRUE("lightning hurt the enemy creature", health(enemy_crtr) < enemy_hp_before);

    gold_before = dg()->total_money_owned;
    submit_ok("obey (no target)", cast("POWER_OBEY"));
    return FTRs_Go_To_Next_Action;
}
FTestActionResult w09_obey_wait(struct FTestActionArgs* const args) { return wait_settled(3); }

FTestActionResult w10_check_obey_hold_validation(struct FTestActionArgs* const args)
{
    const int64_t spent = gold_before - dg()->total_money_owned;
    FTESTLOG("obey: spent %" PRId64 " (price %" PRId64 "), must_obey_turn %" PRId64, spent, (int64_t)price("POWER_OBEY", 0), (int64_t)dg()->must_obey_turn);
    CHECK_TRUE("obey took effect (must_obey_turn set)", dg()->must_obey_turn > 0);

    struct ExtSeatVerb v = at_stl("POWER_OBEY", 40, 40);
    const char* e = extseat_submit_verb(U, P, &v, NULL);
    CHECK_TRUE("a level power refuses a target", e && strcmp(e, "UNSUPPORTED_POWER") == 0);
    v = on_thing("POWER_HEAL_CREATURE", own_crtr, EXTSEAT_MAX_OVERCHARGE_TURNS + 1);
    e = extseat_submit_verb(U, P, &v, NULL);
    CHECK_TRUE("an overcharge beyond the cap is refused for a creature cast too", e && strcmp(e, "BAD_OVERCHARGE") == 0);
    v = cast("POWER_HOLD_AUDIENCE");
    e = extseat_submit_verb(U, P, &v, NULL);
    CHECK_TRUE("hold audience is accepted as a level power", e == NULL);
    dg()->magic_level[power_model_id("POWER_ARMAGEDDON")] = 0;
    v = cast("POWER_ARMAGEDDON");
    e = extseat_submit_verb_ex(U, P, &v, true, NULL); // queue=true: the hold-audience cast above is still being written out
    CHECK_TRUE("a level power the seat does not have is refused", e && strcmp(e, "NOT_AVAILABLE") == 0);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " power check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: creature, subtile and level powers cast through the seat, charged at the level the seat held");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
