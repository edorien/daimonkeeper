// Sight of Evil cast through an External seat shows the seat what it uncovers, as it does a human: the engine keeps the
// area the sight has opened (soe_explored_flags) and uncovers it on the map only while a frame is drawn, so before the
// fix the seat's view never saw any of it. An enemy creature on ground the seat has not seen: absent from the view
// before the cast, in it (with its slab on the map) once the sight has opened over it, the map's real reveal flags left
// as they were, and gone again once the sight is switched off. Once the sight stops widening the agent is called at
// once (reason sight_open), and own.active_powers.POWER_SIGHT reports what it uncovered: its area, the enemy creature,
// the enemy's treasure room under it.
#include "ftest_ai_seat_sight.h"

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
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_sim_state.h"
#include "magic_powers.h"
#include "map_data.h"
#include "net_game.h"
#include "player_data.h"
#include "power_process.h"
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
static ThingIndex s_enemy = 0;
static int64_t s_phase_start = 0;

static int64_t now(void) { return (int64_t)get_gameturn(); }
static TbBool phase_over(int64_t turns) { return now() - s_phase_start >= turns; }
static struct Dungeon* dg(void) { return get_players_dungeon(get_player(P)); }
static const struct Thing* enemy(void) { const struct Thing* t = thing_get(s_enemy); return (thing_is_invalid(t) || !thing_is_creature(t)) ? NULL : t; }

/** Whether the sight has opened over a subtile (magic_powers.c's power_sight_explored, which no header declares). */
static TbBool sight_open_at(MapSubtlCoord x, MapSubtlCoord y)
{
    const struct Dungeon* d = dg();
    if (d->sight_casted_thing_idx <= 0) return false;
    const struct Thing* eye = thing_get(d->sight_casted_thing_idx);
    if (thing_is_invalid(eye)) return false;
    const int64_t sx = x - eye->mappos.x.stl.num + MAX_SOE_RADIUS, sy = y - eye->mappos.y.stl.num + MAX_SOE_RADIUS;
    if ((sx < 0) || (sx >= 2 * MAX_SOE_RADIUS) || (sy < 0) || (sy >= 2 * MAX_SOE_RADIUS)) return false;
    return d->soe_explored_flags[sy][sx] != 0;
}

/** What the seat's view says about the enemy: listed in visible.creatures, and its slab on the map. */
static void view_of_enemy(TbBool* listed, TbBool* on_map, TbBool* sight_reported)
{
    *listed = false; *on_map = false; *sight_reported = false;
    const struct Thing* e = enemy();
    VALUE v; api_seat_build_view(&v, P);
    VALUE* crs = value_dict_get(value_dict_get(&v, "visible"), "creatures");
    for (size_t i = 0; crs && i < value_array_size(crs); i++)
        if (value_int64(value_dict_get(value_array_get(crs, i), "id")) == (int64_t)s_enemy) *listed = true;
    VALUE* rows = value_dict_get(value_dict_get(&v, "map"), "rows");
    if (e && rows) {
        const char* row = value_string(value_array_get(rows, subtile_slab(e->mappos.y.stl.num)));
        if (row) *on_map = row[2 * subtile_slab(e->mappos.x.stl.num)] != '.';
    }
    VALUE* sight = value_dict_get(value_dict_get(value_dict_get(&v, "own"), "active_powers"), "POWER_SIGHT");
    *sight_reported = sight && value_dict_get(sight, "pos") && value_dict_get(sight, "until_turn");
    value_fini(&v);
}

FTestActionResult soe01_setup(struct FTestActionArgs* const args);
FTestActionResult soe02_cast(struct FTestActionArgs* const args);
FTestActionResult soe03_seen_while_open(struct FTestActionArgs* const args);
FTestActionResult soe04_gone_once_closed(struct FTestActionArgs* const args);
FTestActionResult soe03b_called_and_reported(struct FTestActionArgs* const args);

void ftest_ai_seat_sight_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_sight_init()
{
    s_failures = 0;
    ftest_append_action(soe01_setup, 0, NULL);
    ftest_append_action(soe02_cast, 1, NULL);
    ftest_append_action(soe03_seen_while_open, 1, NULL);
    ftest_append_action(soe03b_called_and_reported, 1, NULL);
    ftest_append_action(soe04_gone_once_closed, 1, NULL);
    return true;
}

FTestActionResult soe01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0) { FTEST_FAIL_TEST("no second keeper"); return FTRs_Go_To_Next_Action; }
    U = net_add_external_seat(P);
    if (U < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    api_seat_decision_set_min_interval(5);
    struct Dungeon* d = dg();
    d->total_money_owned = 1000000;
    d->magic_level[PwrK_SIGHT] = 8;
    // The other keeper's ground below its heart, which the seat has never seen, with a treasure room of that keeper's; an enemy
    // creature on it.
    const struct Thing* heart = find_players_dungeon_heart(my_player_number);
    const MapSubtlCoord hx = heart->mappos.x.stl.num, hy = heart->mappos.y.stl.num;
    ftest_util_replace_slabs(subtile_slab(hx) - 2, subtile_slab(hy) + 3, subtile_slab(hx) + 2, subtile_slab(hy) + 4, SlbT_CLAIMED, my_player_number);
    ftest_util_replace_slabs(subtile_slab(hx) - 1, subtile_slab(hy) + 3, subtile_slab(hx) + 1, subtile_slab(hy) + 4, SlbT_TREASURE, my_player_number);
    struct Thing* e = ftest_util_create_creature(subtile_coord_center(hx), subtile_coord_center(hy + 10), my_player_number, 4, (ThingModel)creature_model_id("ORC"));
    if (thing_is_invalid(e)) { FTEST_FAIL_TEST("could not create the enemy"); return FTRs_Go_To_Next_Action; }
    s_enemy = e->index;
    s_phase_start = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult soe02_cast(struct FTestActionArgs* const args)
{
    const struct Thing* e = enemy();
    if (!e) { FTEST_FAIL_TEST("the enemy is gone"); return FTRs_Go_To_Next_Action; }
    if (subtile_revealed(e->mappos.x.stl.num, e->mappos.y.stl.num, P)) { FTEST_FAIL_TEST("the seat already sees the enemy's ground"); return FTRs_Go_To_Next_Action; }
    TbBool listed, on_map, reported;
    view_of_enemy(&listed, &on_map, &reported);
    CHECK_TRUE("before the cast the enemy is not in the view", !listed && !on_map && !reported);
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_CastPower; snprintf(v.name, sizeof(v.name), "POWER_SIGHT");
    v.has_pos = true; v.stl_x = e->mappos.x.stl.num; v.stl_y = e->mappos.y.stl.num;
    const char* err = extseat_submit_verb(U, P, &v, NULL);
    if (err) { FTEST_FAIL_TEST("POWER_SIGHT refused: %s", err); return FTRs_Go_To_Next_Action; }
    s_phase_start = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult soe03_seen_while_open(struct FTestActionArgs* const args)
{
    const struct Thing* e = enemy();
    // Open over the enemy, and over its slab's centre (what the map's slab test looks at): the sight widens for a while.
    if (!e || !player_uses_power_sight(P) || !sight_open_at(e->mappos.x.stl.num, e->mappos.y.stl.num)
        || !sight_open_at(slab_subtile_center(subtile_slab(e->mappos.x.stl.num)), slab_subtile_center(subtile_slab(e->mappos.y.stl.num)))) {
        if (phase_over(300)) { FTEST_FAIL_TEST("the sight never opened over the enemy (sight %d)", (int)player_uses_power_sight(P)); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    FTESTLOG("the sight is open over the enemy %" PRId64 " turns after the cast", now() - s_phase_start);
    TbBool listed, on_map, reported;
    view_of_enemy(&listed, &on_map, &reported);
    CHECK_TRUE("while the sight is open the view lists the enemy", listed);
    CHECK_TRUE("and shows its ground on the map", on_map);
    CHECK_TRUE("own.active_powers.POWER_SIGHT gives pos and until_turn", reported);
    CHECK_TRUE("the map's own reveal flags are left as they were", !subtile_revealed(e->mappos.x.stl.num, e->mappos.y.stl.num, P));
    s_phase_start = now();
    return FTRs_Go_To_Next_Action;
}

static TbBool reasons_have(const char* what)
{
    VALUE v; api_seat_build_view(&v, P);
    VALUE* arr = value_dict_get(value_dict_get(value_dict_get(&v, "seat"), "decision"), "reasons");
    TbBool found = false;
    for (size_t i = 0; arr && i < value_array_size(arr); i++) if (strcmp(value_string(value_array_get(arr, i)), what) == 0) found = true;
    value_fini(&v);
    return found;
}

FTestActionResult soe03b_called_and_reported(struct FTestActionArgs* const args)
{
    if (!reasons_have("sight_open")) {
        if (phase_over(200)) { SOFT_FAIL("no sight_open decision"); }
        else return FTRs_Repeat_Current_Action;
    } else {
        FTESTLOG("sight_open decision %" PRId64 " turns after the sight reached the enemy", now() - s_phase_start);
    }
    VALUE vw; api_seat_build_view(&vw, P);
    VALUE* sight = value_dict_get(value_dict_get(value_dict_get(&vw, "own"), "active_powers"), "POWER_SIGHT");
    VALUE* rect = sight ? value_dict_get(sight, "slab_rect") : NULL;
    VALUE* crs = sight ? value_dict_get(sight, "creatures") : NULL;
    VALUE* rooms = sight ? value_dict_get(sight, "rooms") : NULL;
    TbBool listed = false, room = false;
    for (size_t i = 0; crs && i < value_array_size(crs); i++) if (value_int64(value_dict_get(value_array_get(crs, i), "id")) == (int64_t)s_enemy) listed = true;
    for (size_t i = 0; rooms && i < value_array_size(rooms); i++) if (value_int64(value_dict_get(value_array_get(rooms, i), "owner")) == my_player_number) room = true;
    const struct Thing* e = enemy();
    TbBool inside = false;
    if (rect && e && value_array_size(rect) == 4) {
        const int64_t sx = subtile_slab(e->mappos.x.stl.num), sy = subtile_slab(e->mappos.y.stl.num);
        inside = (sx >= value_int64(value_array_get(rect, 0))) && (sy >= value_int64(value_array_get(rect, 1)))
              && (sx <= value_int64(value_array_get(rect, 2))) && (sy <= value_int64(value_array_get(rect, 3)));
    }
    FTESTLOG("the report: %d subtiles open, %d creature(s), %d room(s)", sight ? (int)value_int64(value_dict_get(sight, "open_subtiles")) : -1,
             crs ? (int)value_array_size(crs) : -1, rooms ? (int)value_array_size(rooms) : -1);
    // The orc may have wandered off since the sight reached it: it must be listed while it stands where the sight is open.
    const TbBool still_there = e && sight_open_at(e->mappos.x.stl.num, e->mappos.y.stl.num);
    CHECK_TRUE("the report's area holds the enemy", !still_there || inside);
    CHECK_TRUE("it lists the enemy creature", !still_there || listed);
    if (!still_there) FTESTLOG("the orc walked out of the sight before the report was read");
    CHECK_TRUE("and the enemy's room it opened over", room);
    value_fini(&vw);
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_PowerOff; snprintf(v.name, sizeof(v.name), "POWER_SIGHT");
    const char* err = extseat_submit_verb(U, P, &v, NULL);
    if (err) SOFT_FAIL("power_off POWER_SIGHT refused: %s", err);
    s_phase_start = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult soe04_gone_once_closed(struct FTestActionArgs* const args)
{
    if (player_uses_power_sight(P)) {
        if (phase_over(100)) { FTEST_FAIL_TEST("power_off never ended the sight"); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    TbBool listed, on_map, reported;
    view_of_enemy(&listed, &on_map, &reported);
    CHECK_TRUE("once the sight is gone so is the enemy", !listed && !on_map && !reported);
    api_seat_decision_set_min_interval(100);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " sight check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: Sight of Evil shows the seat what it uncovers while it is open, and no longer");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
