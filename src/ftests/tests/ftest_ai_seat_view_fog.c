// Phase M3 (O2) of docs/refactor/AI/LLM/01-integration-plan.md section 7: get_player_view must show a seat only
// what a human in that seat could see (docs/refactor/AI/LLM/03-observation-api.md section 3). Builds the view
// straight from the engine for two players of a real level and checks it against the visibility rules the UI uses:
// tile reveal, an invisible enemy creature, a hidden trap, a secret door, partially seen rooms, the slab map,
// ally-shared vision, and that a player's own state is never filtered. Also measures a fully revealed map's
// payload, which sizes the response buffer (02 section 2).
#include "ftest_ai_seat_view_fog.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <stdlib.h>
#include <string.h>
#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api_seat_view.h"
#include "config_creature.h"
#include "config_keeperfx.h"
#include "config_magic.h"
#include "config_rules.h"
#include "config_terrain.h"
#include "config_trapdoor.h"
#include "creature_control.h"
#include "dungeon_data.h"
#include "frontend.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "map_data.h"
#include "player_data.h"
#include "player_instances.h"
#include "thing_doors.h"
#include "thing_list.h"
#include "thing_traps.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK(what, cond) do { if (!(cond)) FAIL("%s", what); } while (0)

#define ME 0
#define RIVAL 1

struct vars_t {
    ThingIndex crtr;          /* the rival's creature */
    MapSubtlCoord cx, cy;     /* where it stands */
    MapSubtlCoord trap_x, trap_y;
    MapSubtlCoord door_x, door_y;
    ThingModel hidden_trap, plain_trap, secret_door, plain_door;
    ThingIndex hidden_trap_idx, secret_door_idx;
    MapSubtlCoord heart_x, heart_y;
    MapSubtlCoord far_x, far_y;
};
static struct vars_t vars;

void ftest_ai_seat_view_fog_pre_start() { fe_computer_players = 1; }

// ---- reading a view --------------------------------------------------------------------------------------
static VALUE* dict(VALUE* v, const char* k) { return value_dict_get(v, k); }

static int64_t list_size(VALUE* view, const char* section, const char* list)
{
    VALUE* l = dict(dict(view, section), list);
    return l ? (int64_t)value_array_size(l) : -1;
}

static VALUE* find_id(VALUE* view, const char* section, const char* list, int64_t id)
{
    VALUE* l = dict(dict(view, section), list);
    for (size_t i = 0; l && i < value_array_size(l); i++)
    {
        VALUE* e = value_array_get(l, i);
        if (value_type(dict(e, "id")) == VALUE_INT32 && value_int32(dict(e, "id")) == id) return e;
    }
    return NULL;
}

static const char* map_cell(VALUE* view, MapSlabCoord x, MapSlabCoord y, char out[3])
{
    VALUE* rows = dict(dict(view, "map"), "rows");
    const char* row = rows ? value_string(value_array_get(rows, y)) : NULL;
    if (row == NULL) return NULL;
    out[0] = row[x * 2]; out[1] = row[x * 2 + 1]; out[2] = 0;
    return out;
}

struct Counter { size_t bytes; };
static int count_writer(const char* s, size_t n, void* ud) { (void)s; ((struct Counter*)ud)->bytes += n; return 0; }

static size_t view_bytes(VALUE* view)
{
    struct Counter c = {0};
    json_dom_dump(view, count_writer, &c, 0, JSON_DOM_DUMP_MINIMIZE);
    return c.bytes;
}

static void with_view(PlayerNumber p, void (*fn)(VALUE*))
{
    VALUE view;
    api_seat_build_view(&view, p);
    fn(&view);
    value_fini(&view);
}

// ---- the checks -------------------------------------------------------------------------------------------
static void check_unseen(VALUE* v)
{
    CHECK("an unrevealed enemy creature is not listed", find_id(v, "visible", "creatures", vars.crtr) == NULL);
    char c[3];
    const char* cell = map_cell(v, subtile_slab(vars.cx), subtile_slab(vars.cy), c);
    CHECK("an unrevealed slab is '..' in the map", cell && strcmp(cell, "..") == 0);
    // Rooms elsewhere on the level may be known; none inside the concealed neighbourhood of the rival's heart may be.
    VALUE* rl = dict(dict(v, "visible"), "rooms");
    int64_t near_heart = 0;
    for (size_t i = 0; rl && i < value_array_size(rl); i++)
    {
        VALUE* pos = dict(value_array_get(rl, i), "pos");
        const int dx = value_int32(value_array_get(pos, 0)) - vars.heart_x, dy = value_int32(value_array_get(pos, 1)) - vars.heart_y;
        if (dx >= -14 && dx <= 14 && dy >= -14 && dy <= 14) near_heart++;
    }
    CHECK("no room in the concealed neighbourhood of the rival's heart is listed while unseen", near_heart == 0);
    CHECK("the map has one row per slab row", value_array_size(dict(dict(v, "map"), "rows")) == (size_t)kfx_sim_state.map_tiles_y);
}

static void check_seen(VALUE* v)
{
    VALUE* e = find_id(v, "visible", "creatures", vars.crtr);
    CHECK("a creature on a revealed subtile is listed", e != NULL);
    if (e)
    {
        CHECK("... with its owner", value_int32(dict(e, "owner")) == RIVAL);
        CHECK("... not marked as an ally", value_type(dict(e, "ally")) == VALUE_BOOL && !value_bool(dict(e, "ally")));
        CHECK("... with its kind", strcmp(value_string(dict(e, "kind")), "ORC") == 0);
    }
    char c[3];
    const char* cell = map_cell(v, subtile_slab(vars.cx), subtile_slab(vars.cy), c);
    CHECK("a revealed slab shows its kind and owner", cell && strcmp(cell, "..") != 0);
}

static void check_invisible_hidden(VALUE* v) { CHECK("an invisible enemy creature is not listed even on a revealed tile", find_id(v, "visible", "creatures", vars.crtr) == NULL); }
static void check_invisible_own(VALUE* v) { CHECK("an invisible creature is still in its owner's own list", find_id(v, "own", "creatures", vars.crtr) != NULL); }

static void check_traps_unrevealed(VALUE* v)
{
    CHECK("a hidden, unrevealed enemy trap is not listed", find_id(v, "visible", "traps", vars.hidden_trap_idx) == NULL);
}
static void check_traps_revealed(VALUE* v)
{
    CHECK("a hidden trap becomes visible once revealed", find_id(v, "visible", "traps", vars.hidden_trap_idx) != NULL);
}
static void check_own_trap(VALUE* v) { CHECK("the owner always sees its own hidden trap", find_id(v, "own", "traps", vars.hidden_trap_idx) != NULL); }

static void check_door_hidden(VALUE* v)
{
    if (vars.secret_door_idx) CHECK("a secret door is not listed to a player who has not found it", find_id(v, "visible", "doors", vars.secret_door_idx) == NULL);
}
static void check_door_found(VALUE* v)
{
    if (vars.secret_door_idx) CHECK("a secret door is listed once found", find_id(v, "visible", "doors", vars.secret_door_idx) != NULL);
}

static void check_room_seen(VALUE* v)
{
    CHECK("a room with a seen slab is listed", list_size(v, "visible", "rooms") >= 1);
    VALUE* l = dict(dict(v, "visible"), "rooms");
    int64_t seen = -1;
    for (size_t i = 0; l && i < value_array_size(l); i++)
        if (value_int32(dict(value_array_get(l, i), "owner")) == RIVAL) seen = value_int32(dict(value_array_get(l, i), "slabs_seen"));
    CHECK("... reporting only the slabs seen", seen >= 1 && seen < 9);
}

static void check_ally_shares(VALUE* v)
{
    char c[3];
    const char* cell = map_cell(v, subtile_slab(vars.far_x), subtile_slab(vars.far_y), c);
    CHECK("an ally's revealed slab shows when the rules share vision", cell && strcmp(cell, "..") != 0);
    VALUE* e = find_id(v, "visible", "creatures", vars.crtr);
    CHECK("an ally's creature is flagged as an ally", e && value_type(dict(e, "ally")) == VALUE_BOOL && value_bool(dict(e, "ally")));
}

static void check_own_never_filtered(VALUE* v)
{
    CHECK("a player's own creature is in own.creatures", find_id(v, "own", "creatures", vars.crtr) != NULL);
    CHECK("... and never in visible.creatures", find_id(v, "visible", "creatures", vars.crtr) == NULL);
}

static void measure(VALUE* v)
{
    FTESTLOG("payload: %" PRId64 " bytes for a fully revealed %" PRId64 "x%" PRId64 " slab map (%" PRId64 " visible creature(s), %" PRId64 " visible room(s))",
        (int64_t)view_bytes(v), (int64_t)kfx_sim_state.map_tiles_x, (int64_t)kfx_sim_state.map_tiles_y,
        list_size(v, "visible", "creatures"), list_size(v, "visible", "rooms"));
    CHECK("a fully revealed view fits far inside the 1 MiB response buffer", view_bytes(v) < 200 * 1024);
}

// ---- the sequence ------------------------------------------------------------------------------------------
FTestActionResult f01_setup(struct FTestActionArgs* const args);
FTestActionResult f02_unseen(struct FTestActionArgs* const args);
FTestActionResult f03_reveal_creature(struct FTestActionArgs* const args);
FTestActionResult f04_invisible(struct FTestActionArgs* const args);
FTestActionResult f05_traps(struct FTestActionArgs* const args);
FTestActionResult f06_doors_rooms(struct FTestActionArgs* const args);
FTestActionResult f07_alliance(struct FTestActionArgs* const args);
FTestActionResult f08_measure_and_end(struct FTestActionArgs* const args);

TbBool ftest_ai_seat_view_fog_init()
{
    s_failures = 0;
    memset(&vars, 0, sizeof(vars));
    ftest_append_action(f01_setup, 0, NULL);
    ftest_append_action(f02_unseen, 1, NULL);
    ftest_append_action(f03_reveal_creature, 1, NULL);
    ftest_append_action(f04_invisible, 1, NULL);
    ftest_append_action(f05_traps, 1, NULL);
    ftest_append_action(f06_doors_rooms, 1, NULL);
    ftest_append_action(f07_alliance, 1, NULL);
    ftest_append_action(f08_measure_and_end, 1, NULL);
    return true;
}

static void reveal_stl(MapSubtlCoord x, MapSubtlCoord y, PlayerNumber p) { reveal_map_block(get_map_block_at(x, y), p); }
static void conceal_stl(MapSubtlCoord x, MapSubtlCoord y, PlayerNumber p) { conceal_map_block(get_map_block_at(x, y), p); }

FTestActionResult f01_setup(struct FTestActionArgs* const args)
{
    const struct Thing* heart = find_players_dungeon_heart(RIVAL);
    if (thing_is_invalid(heart) || !player_exists(get_player(RIVAL))) { FTEST_FAIL_TEST("no rival keeper with a heart"); return FTRs_Go_To_Next_Action; }
    vars.heart_x = heart->mappos.x.stl.num; vars.heart_y = heart->mappos.y.stl.num;
    vars.cx = vars.heart_x - 3; vars.cy = vars.heart_y;
    vars.trap_x = vars.heart_x; vars.trap_y = vars.heart_y + 6;
    vars.door_x = vars.heart_x + 12; vars.door_y = vars.heart_y;
    vars.far_x = vars.heart_x; vars.far_y = vars.heart_y - 12;

    struct Thing* c = ftest_util_create_creature(subtile_coord_center(vars.cx), subtile_coord_center(vars.cy), RIVAL, 9, (ThingModel)creature_model_id("ORC"));
    if (thing_is_invalid(c)) { FTEST_FAIL_TEST("no creature"); return FTRs_Go_To_Next_Action; }
    vars.crtr = c->index;

    // The whole neighbourhood is unrevealed to the human, whatever the level started with.
    for (int dx = -14; dx <= 14; dx++) for (int dy = -14; dy <= 14; dy++)
    {
        const MapSubtlCoord x = vars.heart_x + dx, y = vars.heart_y + dy;
        if (x >= 0 && y >= 0 && x < kfx_sim_state.map_subtiles_x && y < kfx_sim_state.map_subtiles_y) conceal_stl(x, y, ME);
    }
    reveal_stl(vars.far_x, vars.far_y, RIVAL);

    // Trap and door models: the first hidden / non-hidden trap and secret / plain door the config has.
    for (int64_t m = 1; m < kfx_config_state.conf.trapdoor_conf.trap_types_count; m++)
    {
        if (get_trap_model_stats(m)->hidden) { if (!vars.hidden_trap) vars.hidden_trap = m; } else if (!vars.plain_trap) vars.plain_trap = m;
    }
    for (int64_t m = 1; m < kfx_config_state.conf.trapdoor_conf.door_types_count; m++)
    {
        if (flag_is_set(get_door_model_stats(m)->model_flags, DoMF_Secret)) { if (!vars.secret_door) vars.secret_door = m; } else if (!vars.plain_door) vars.plain_door = m;
    }
    FTESTLOG("models: hidden trap %" PRId64 ", secret door %" PRId64 " (0 = none in this config)", (int64_t)vars.hidden_trap, (int64_t)vars.secret_door);
    if (!vars.hidden_trap) FTEST_FAIL_TEST("this config has no hidden trap to test");

    // The rival's claimed floor for the trap, and a walled corridor for the door.
    ftest_util_replace_slabs(subtile_slab(vars.trap_x), subtile_slab(vars.trap_y), subtile_slab(vars.trap_x), subtile_slab(vars.trap_y), SlbT_CLAIMED, RIVAL);
    const MapSlabCoord dsx = subtile_slab(vars.door_x), dsy = subtile_slab(vars.door_y);
    ftest_util_replace_slabs(dsx - 1, dsy, dsx + 1, dsy, SlbT_CLAIMED, RIVAL);
    ftest_util_replace_slabs(dsx - 1, dsy - 1, dsx + 1, dsy - 1, SlbT_WALLDRAPE, PLAYER_NEUTRAL);
    ftest_util_replace_slabs(dsx - 1, dsy + 1, dsx + 1, dsy + 1, SlbT_WALLDRAPE, PLAYER_NEUTRAL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult f02_unseen(struct FTestActionArgs* const args)
{
    with_view(ME, check_unseen);
    with_view(RIVAL, check_own_never_filtered);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult f03_reveal_creature(struct FTestActionArgs* const args)
{
    reveal_stl(vars.cx, vars.cy, ME);
    with_view(ME, check_seen);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult f04_invisible(struct FTestActionArgs* const args)
{
    struct Thing* c = thing_get(vars.crtr);
    struct CreatureControl* cctrl = creature_control_get_from_thing(c);
    cctrl->spell_flags |= CSAfF_Invisibility;
    cctrl->force_visible = 0;
    with_view(ME, check_invisible_hidden);
    with_view(RIVAL, check_invisible_own);
    cctrl->spell_flags &= ~CSAfF_Invisibility;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult f05_traps(struct FTestActionArgs* const args)
{
    // A trap on a tile the human has revealed.
    for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) reveal_stl(vars.trap_x + dx, vars.trap_y + dy, ME);
    if (!player_place_trap_at_subtile_without_check(vars.trap_x, vars.trap_y, RIVAL, vars.hidden_trap, true)) { FTEST_FAIL_TEST("could not place the hidden trap"); return FTRs_Go_To_Next_Action; }
    struct Thing* t = get_trap_for_position(slab_subtile_center(subtile_slab(vars.trap_x)), slab_subtile_center(subtile_slab(vars.trap_y)));
    if (thing_is_invalid(t)) { FTEST_FAIL_TEST("the trap is not there"); return FTRs_Go_To_Next_Action; }
    vars.hidden_trap_idx = t->index;
    t->trap.revealed = 0;
    with_view(ME, check_traps_unrevealed);
    with_view(RIVAL, check_own_trap);
    t->trap.revealed = 1;
    with_view(ME, check_traps_revealed);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult f06_doors_rooms(struct FTestActionArgs* const args)
{
    if (vars.secret_door)
    {
        const MapSlabCoord dsx = subtile_slab(vars.door_x), dsy = subtile_slab(vars.door_y);
        for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) reveal_stl(vars.door_x + dx, vars.door_y + dy, ME);
        struct Coord3d pos; set_coords_to_slab_center(&pos, dsx, dsy);
        struct Thing* d = create_door(&pos, vars.secret_door, 0, RIVAL, 0);
        if (thing_is_invalid(d)) { FTEST_FAIL_TEST("could not create the secret door"); return FTRs_Go_To_Next_Action; }
        vars.secret_door_idx = d->index;
        with_view(ME, check_door_hidden);
        reveal_secret_door_to_player(d, ME);
        with_view(ME, check_door_found);
    }
    // Rooms: the human has now revealed only one slab of the rival's heart room; conceal the rest first.
    for (int dx = -5; dx <= 5; dx++) for (int dy = -5; dy <= 5; dy++) conceal_stl(vars.heart_x + dx, vars.heart_y + dy, ME);
    reveal_stl(vars.heart_x + 3, vars.heart_y, ME); /* one slab of the 3x3 heart room */
    with_view(ME, check_room_seen);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult f07_alliance(struct FTestActionArgs* const args)
{
    const TbBool old_rule = kfx_config_state.conf.rules[ME].gameplay.allies_share_vision;
    kfx_config_state.conf.rules[ME].gameplay.allies_share_vision = true;
    set_ally_with_player(ME, RIVAL, true);
    set_ally_with_player(RIVAL, ME, true);
    reveal_stl(vars.cx, vars.cy, ME); /* the creature is on a tile the human sees again */
    with_view(ME, check_ally_shares);
    set_ally_with_player(ME, RIVAL, false);
    set_ally_with_player(RIVAL, ME, false);
    kfx_config_state.conf.rules[ME].gameplay.allies_share_vision = old_rule;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult f08_measure_and_end(struct FTestActionArgs* const args)
{
    ftest_util_reveal_map(ME);
    with_view(ME, measure);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " visibility check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: the view shows a seat only what it could see");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
