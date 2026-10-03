// What the agent is told about its own dungeon beyond raw lists: what research has unlocked (rooms, traps, doors; research is
// the creatures' job, the agent only needs the result), the army per creature kind (count, levels, health) with a profile of each
// kind it owns (what it is good at, its pay and hunger, abilities), and the imprison/flee tendencies with the set_tendency verb.
#include "ftest_ai_seat_intel.h"

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
#include "config_terrain.h"
#include "config_trapdoor.h"
#include "creature_control.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "net_game.h"
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
static NetUserId U = -1;
static ThingIndex s_angry_orc = 0, s_fleeing_troll = 0;

static VALUE* creature_by_id(VALUE* creatures, int64_t id)
{
    for (size_t i = 0; creatures && i < value_array_size(creatures); i++) {
        VALUE* e = value_array_get(creatures, i);
        if (value_int64(value_dict_get(e, "id")) == id) return e;
    }
    return NULL;
}

static TbBool array_has(VALUE* arr, const char* s)
{
    for (size_t i = 0; arr && i < value_array_size(arr); i++) if (strcmp(value_string(value_array_get(arr, i)), s) == 0) return true;
    return false;
}
static struct ExtSeatVerb tend(const char* name, int en)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_SetTendency; snprintf(v.name, sizeof(v.name), "%s", name);
    if (en >= 0) { v.has_enabled = true; v.enabled = en != 0; }
    return v;
}

FTestActionResult i01_setup(struct FTestActionArgs* const args);
FTestActionResult i02_check_view_toggle(struct FTestActionArgs* const args);
FTestActionResult i03_check_toggled(struct FTestActionArgs* const args);

void ftest_ai_seat_intel_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_intel_init()
{
    s_failures = 0;
    ftest_append_action(i01_setup, 0, NULL);
    ftest_append_action(i02_check_view_toggle, 2, NULL);
    ftest_append_action(i03_check_toggled, 4, NULL);
    return true;
}

FTestActionResult i01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || (U = net_add_external_seat(P)) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    const struct Thing* heart = find_players_dungeon_heart(P);
    const MapSubtlCoord hx = heart->mappos.x.stl.num, hy = heart->mappos.y.stl.num;
    struct Dungeon* d = get_players_dungeon(get_player(P));
    ftest_util_replace_slabs(subtile_slab(hx) - 4, subtile_slab(hy) + 3, subtile_slab(hx) + 4, subtile_slab(hy) + 5, SlbT_CLAIMED, P);
    // Unlocked: treasure room yes, lair no; boulder trap yes, alarm trap no; wooden door yes.
    const RoomKind treasure = (RoomKind)get_rid(room_desc, "TREASURE"), lair = (RoomKind)get_rid(room_desc, "LAIR");
    d->room_buildable[treasure] |= 1; d->room_buildable[lair] = 0; d->room_resrchable[lair] = 0;
    set_trap_buildable_and_add_to_amount(P, (ThingModel)trap_model_id("BOULDER"), 1, 1);
    set_door_buildable_and_add_to_amount(P, (ThingModel)door_model_id("WOOD"), 1, 1);
    // Army: two orcs (levels differ) and a troll.
    struct Thing* a = ftest_util_create_creature(subtile_coord_center(hx - 3), subtile_coord_center(hy + 10), P, 1, (ThingModel)creature_model_id("ORC"));
    struct Thing* b = ftest_util_create_creature(subtile_coord_center(hx - 1), subtile_coord_center(hy + 10), P, 1, (ThingModel)creature_model_id("ORC"));
    struct Thing* c = ftest_util_create_creature(subtile_coord_center(hx + 1), subtile_coord_center(hy + 10), P, 1, (ThingModel)creature_model_id("TROLL"));
    if (thing_is_invalid(a) || thing_is_invalid(b) || thing_is_invalid(c)) { FTEST_FAIL_TEST("no creatures"); return FTRs_Go_To_Next_Action; }
    creature_control_get_from_thing(b)->exp_level = 2; // level 3
    // Make the first orc angry (hungry, the real check: mood_flags & CCMoo_Angry, dominant reason from
    // annoyance_level[] against the model's own annoy_level) and put the troll to flight, exactly what the anger and
    // combat-flee subsystems themselves produce over time.
    struct CreatureControl* actrl = creature_control_get_from_thing(a);
    const struct CreatureModelConfig* acf = creature_stats_get_from_thing(a);
    CHECK_TRUE("the test orc can get angry (annoy_level is nonzero)", acf->annoy_level > 0);
    actrl->mood_flags |= CCMoo_Angry;
    actrl->annoyance_level[AngR_Hungry] = (int64_t)acf->annoy_level + 100;
    CHECK_TRUE("the troll was put to flight", external_set_thing_state(c, CrSt_CreatureCombatFlee));
    s_angry_orc = a->index;
    s_fleeing_troll = c->index;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult i02_check_view_toggle(struct FTestActionArgs* const args)
{
    VALUE v; api_seat_build_view(&v, P);
    VALUE* own = value_dict_get(&v, "own");
    VALUE* un = value_dict_get(own, "unlocked");
    CHECK_TRUE("unlocked lists a buildable room", array_has(value_dict_get(un, "rooms"), "TREASURE"));
    CHECK_TRUE("and does not list a room that is not", !array_has(value_dict_get(un, "rooms"), "LAIR"));
    CHECK_TRUE("unlocked lists a buildable trap", array_has(value_dict_get(un, "traps"), "BOULDER"));
    CHECK_TRUE("unlocked lists a buildable door", array_has(value_dict_get(un, "doors"), "WOOD"));
    VALUE* sum = value_dict_get(own, "creature_summary");
    VALUE* orc = sum ? value_dict_get(sum, "ORC") : NULL;
    CHECK_TRUE("the summary counts two orcs", orc && value_int64(value_dict_get(orc, "count")) == 2);
    CHECK_TRUE("with their levels (max 3)", orc && value_int64(value_dict_get(orc, "max_level")) == 3);
    VALUE* bl = orc ? value_dict_get(orc, "by_level") : NULL;
    CHECK_TRUE("by level: one at 1, one at 3", bl && value_int64(value_dict_get(bl, "1")) == 1 && value_int64(value_dict_get(bl, "3")) == 1);
    CHECK_TRUE("and one troll", sum && value_dict_get(sum, "TROLL") && value_int64(value_dict_get(value_dict_get(sum, "TROLL"), "count")) == 1);
    VALUE* info = value_dict_get(own, "creature_info");
    VALUE* oi = info ? value_dict_get(info, "ORC") : NULL;
    const struct CreatureModelConfig* cf = creature_stats_get((ThingModel)creature_model_id("ORC"));
    CHECK_TRUE("each owned kind has a profile", oi && value_dict_get(info, "TROLL"));
    CHECK_TRUE("the profile carries the configured health", oi && value_int64(value_dict_get(oi, "health")) == cf->health);
    CHECK_TRUE("and pay", oi && value_int64(value_dict_get(oi, "pay")) == cf->pay);
    CHECK_TRUE("and a combat class", oi && strlen(value_string(value_dict_get(oi, "combat"))) > 3);
    CHECK_TRUE("and the orc's primary job (it trains in the training room)", oi && array_has(value_dict_get(oi, "primary_jobs"), "TRAIN"));
    VALUE* ti = info ? value_dict_get(info, "TROLL") : NULL;
    CHECK_TRUE("and the troll's (it works in the workshop)", ti && array_has(value_dict_get(ti, "primary_jobs"), "MANUFACTURE"));
    FTESTLOG("orc: %s; troll: %s", oi ? value_string(value_dict_get(oi, "combat")) : "?", ti ? value_string(value_dict_get(ti, "combat")) : "?");
    VALUE* rc = value_dict_get(own, "room_costs");
    const RoomKind treasure_rk = (RoomKind)get_rid(room_desc, "TREASURE");
    CHECK_TRUE("room_costs prices an unlocked room at its configured per-slab cost",
        rc && value_dict_get(rc, "TREASURE") && value_int64(value_dict_get(rc, "TREASURE")) == get_room_kind_stats(treasure_rk)->cost);
    CHECK_TRUE("and does not price a room that is not unlocked", rc && value_dict_get(rc, "LAIR") == NULL);

    VALUE* creatures = value_dict_get(own, "creatures");
    VALUE* angry_e = creature_by_id(creatures, s_angry_orc);
    VALUE* fleeing_e = creature_by_id(creatures, s_fleeing_troll);
    CHECK_TRUE("the angry orc is reported angry, for being hungry", angry_e && value_bool(value_dict_get(angry_e, "angry"))
        && strcmp(value_string(value_dict_get(angry_e, "angry_reason")), "hungry") == 0);
    CHECK_TRUE("the fleeing troll is reported fleeing", fleeing_e && value_bool(value_dict_get(fleeing_e, "fleeing")));
    CHECK_TRUE("the angry orc is not also reported fleeing", angry_e && !value_bool(value_dict_get(angry_e, "fleeing")));
    CHECK_TRUE("the fleeing troll is not also reported angry", fleeing_e && !value_bool(value_dict_get(fleeing_e, "angry")));

    VALUE* td = value_dict_get(own, "tendencies");
    CHECK_TRUE("the view shows the tendencies", td != NULL);
    const TbBool imprison_before = td && value_bool(value_dict_get(td, "imprison"));
    value_fini(&v);

    struct ExtSeatVerb x;
    x = tend("imprison", -1);
    { const char* e = extseat_submit_verb(U, P, &x, NULL); CHECK_TRUE("set_tendency needs enabled", e && strcmp(e, "MISSING_ENABLED") == 0); }
    x = tend("nonsense", 1);
    { const char* e = extseat_submit_verb(U, P, &x, NULL); CHECK_TRUE("an unknown tendency is refused", e && strcmp(e, "UNKNOWN_KIND") == 0); }
    x = tend("imprison", imprison_before ? 1 : 0);
    { const char* e = extseat_submit_verb(U, P, &x, NULL); CHECK_TRUE("setting what is already set is refused", e && strcmp(e, "ALREADY_SET") == 0); }
    x = tend("imprison", imprison_before ? 0 : 1);
    { const char* e = extseat_submit_verb(U, P, &x, NULL); CHECK_TRUE("flipping it is accepted", e == NULL); }
    return FTRs_Go_To_Next_Action;
}

FTestActionResult i03_check_toggled(struct FTestActionArgs* const args)
{
    VALUE v; api_seat_build_view(&v, P);
    VALUE* td = value_dict_get(value_dict_get(&v, "own"), "tendencies");
    const TbBool now = td && value_bool(value_dict_get(td, "imprison"));
    const struct Dungeon* d = get_players_dungeon(get_player(P));
    CHECK_TRUE("the toggle reached the dungeon and the view", now == ((d->creature_tendencies & CrTend_Imprison) != 0));
    value_fini(&v);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " intel check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: unlocked lists, army summary, per-kind profiles and tendencies");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
