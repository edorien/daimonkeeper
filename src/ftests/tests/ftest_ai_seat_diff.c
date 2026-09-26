// M6 diff view (api_seat_finish_view) against the real engine. A baseline full view is taken with only part of the map known;
// then the world changes in four ways the diff must report: a region is revealed (map.revealed and map.changes), a room's
// slabs change (map.changes), a creature arrives (own.creatures.added) and an event marker is raised (own.events.added).
// The revealed-slab count must equal the engine's own count of slabs that went from unknown to known, and the diff must be
// a small fraction of the full view. An unchanged world must give a diff with nothing but the clock.
#include "ftest_ai_seat_diff.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>
#include <stdlib.h>
#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api_seat_diff.h"
#include "api_seat_view.h"
#include "config_creature.h"
#include "config_keeperfx.h"
#include "dungeon_data.h"
#include "frontend.h"
#include "game_legacy.h"
#include "map_data.h"
#include "map_events.h"
#include "net_game.h"
#include "player_data.h"
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
static int64_t baseline_id = 0, baseline_bytes = 0;
static int64_t revealed_expected = 0;
static MapSlabCoord rx0, ry0, rx1, ry1;   // the region revealed later
static ThingIndex new_creature = 0;

static int dump_len(const char* s, size_t n, void* u) { (void)s; *(size_t*)u += n; return 0; }
static size_t view_bytes(const VALUE* v) { size_t n = 0; json_dom_dump(v, dump_len, &n, 0, JSON_DOM_DUMP_MINIMIZE); return n; }

static TbBool slab_known(MapSlabCoord x, MapSlabCoord y) { return subtile_revealed(slab_subtile_center(x), slab_subtile_center(y), P); }
static int64_t count_known(void)
{
    int64_t n = 0;
    for (MapSlabCoord y = 0; y < kfx_sim_state.map_tiles_y; y++) for (MapSlabCoord x = 0; x < kfx_sim_state.map_tiles_x; x++) if (slab_known(x, y)) n++;
    return n;
}
static int64_t known_before = 0;

static int64_t sum_runs(VALUE* runs, const char* field)
{
    int64_t n = 0;
    for (size_t i = 0; runs && i < value_array_size(runs); i++) {
        VALUE* r = value_array_get(runs, i);
        n += (field[0] == 'c') ? (int64_t)(value_string_length(value_dict_get(r, "cells")) / 2) : value_int64(value_dict_get(r, "len"));
    }
    return n;
}

FTestActionResult f01_setup_baseline(struct FTestActionArgs* const args);
FTestActionResult f02_quiet_diff_then_change(struct FTestActionArgs* const args);
FTestActionResult f03_check_diff(struct FTestActionArgs* const args);

void ftest_ai_seat_diff_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_diff_init()
{
    s_failures = 0;
    ftest_append_action(f01_setup_baseline, 0, NULL);
    ftest_append_action(f02_quiet_diff_then_change, 3, NULL);
    ftest_append_action(f03_check_diff, 2, NULL);
    return true;
}

FTestActionResult f01_setup_baseline(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || net_add_external_seat(P) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    api_seat_diff_reset();
    // Baseline: whatever the seat knows at the start (its home area only).
    VALUE v; api_seat_build_view(&v, P);
    api_seat_finish_view(&v, P, false, 0);
    baseline_id = value_int64(value_dict_get(&v, "view_id"));
    baseline_bytes = (int64_t)view_bytes(&v);
    CHECK_TRUE("the first view is full", strcmp(value_string(value_dict_get(&v, "mode")), "full") == 0);
    value_fini(&v);
    known_before = count_known();
    FTESTLOG("baseline: view %" PRId64 " is %" PRId64 " bytes, the seat knows %" PRId64 " slabs", baseline_id, baseline_bytes, known_before);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult f02_quiet_diff_then_change(struct FTestActionArgs* const args)
{
    // Nothing happened in the world since the baseline except time: the diff is the clock and little else.
    VALUE q; api_seat_build_view(&q, P);
    api_seat_finish_view(&q, P, true, baseline_id);
    CHECK_TRUE("a quiet diff is a diff", strcmp(value_string(value_dict_get(&q, "mode")), "diff") == 0);
    VALUE* map = value_dict_get(&q, "map");
    CHECK_TRUE("a quiet world has no map changes", map == NULL);
    FTESTLOG("quiet diff: %zu bytes", view_bytes(&q));
    CHECK_TRUE("a quiet diff is tiny", view_bytes(&q) * 20 < (size_t)baseline_bytes);
    baseline_id = value_int64(value_dict_get(&q, "view_id"));
    value_fini(&q);

    // Change the world. Reveal a 12 x 8 slab region far from the seat's home, turn part of it into claimed floor with a
    // room-like owner change, add a creature and raise an event marker.
    const struct Thing* heart = find_players_dungeon_heart(P);
    const MapSlabCoord hsx = subtile_slab(heart->mappos.x.stl.num), hsy = subtile_slab(heart->mappos.y.stl.num);
    rx0 = (hsx > 30) ? hsx - 24 : hsx + 14; ry0 = (hsy > 20) ? hsy - 12 : hsy + 8; rx1 = rx0 + 11; ry1 = ry0 + 7;
    known_before = count_known();
    reveal_map_area(P, slab_subtile_center(rx0) - 1, slab_subtile_center(rx1) + 1, slab_subtile_center(ry0) - 1, slab_subtile_center(ry1) + 1);
    revealed_expected = count_known() - known_before;
    ftest_util_replace_slabs(rx0 + 2, ry0 + 2, rx0 + 4, ry0 + 4, SlbT_CLAIMED, P);
    struct Thing* c = ftest_util_create_creature(subtile_coord_center(slab_subtile_center(rx0 + 3)), subtile_coord_center(slab_subtile_center(ry0 + 3)), P, 2, (ThingModel)creature_model_id("ORC"));
    new_creature = thing_is_invalid(c) ? 0 : c->index;
    event_create_event_or_update_nearby_existing_event(subtile_coord_center(slab_subtile_center(rx0 + 3)), subtile_coord_center(slab_subtile_center(ry0 + 3)), EvKind_NewCreature, P, new_creature);
    FTESTLOG("changed the world: %" PRId64 " slabs newly revealed", revealed_expected);
    CHECK_TRUE("the region really was unknown before", revealed_expected > 20);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult f03_check_diff(struct FTestActionArgs* const args)
{
    VALUE d; api_seat_build_view(&d, P);
    api_seat_finish_view(&d, P, true, baseline_id);
    CHECK_TRUE("the view after changes is a diff", strcmp(value_string(value_dict_get(&d, "mode")), "diff") == 0);
    const size_t bytes = view_bytes(&d);
    FTESTLOG("diff after changes: %zu bytes (full baseline %" PRId64 ")", bytes, baseline_bytes);
    CHECK_TRUE("the diff is far smaller than the full view", bytes * 3 < (size_t)baseline_bytes);

    VALUE* map = value_dict_get(&d, "map");
    VALUE* rev = map ? value_dict_get(map, "revealed") : NULL;
    VALUE* chg = map ? value_dict_get(map, "changes") : NULL;
    CHECK_TRUE("the diff reports newly revealed slabs", rev != NULL);
    const int64_t reported = sum_runs(rev, "len");
    FTESTLOG("revealed: engine %" PRId64 ", diff %" PRId64, revealed_expected, reported);
    CHECK_TRUE("the revealed count equals the engine's own count", reported == revealed_expected);
    CHECK_TRUE("every revealed slab is also a changed cell (it went from '..' to something)", sum_runs(chg, "cells") >= reported);

    // The claimed-floor patch must be visible as changes with the seat's owner digit.
    TbBool saw_owner = false;
    for (size_t i = 0; chg && i < value_array_size(chg); i++) {
        const char* cells = value_string(value_dict_get(value_array_get(chg, i), "cells"));
        for (size_t k = 1; cells[k]; k += 2) if (cells[k] == (char)('0' + P)) saw_owner = true;
    }
    CHECK_TRUE("the seat's newly owned slabs show with its owner digit", saw_owner);

    VALUE* own = value_dict_get(&d, "own");
    VALUE* crs = own ? value_dict_get(own, "creatures") : NULL;
    VALUE* added = crs ? value_dict_get(crs, "added") : NULL;
    TbBool found = false;
    for (size_t i = 0; added && i < value_array_size(added); i++)
        if (value_int64(value_dict_get(value_array_get(added, i), "id")) == (int64_t)new_creature) found = true;
    CHECK_TRUE("the new creature is in own.creatures.added", new_creature != 0 && found);

    VALUE* evs = own ? value_dict_get(own, "events") : NULL;
    VALUE* eadded = evs ? value_dict_get(evs, "added") : NULL;
    TbBool ev_found = false;
    for (size_t i = 0; eadded && i < value_array_size(eadded); i++)
        if (strcmp(value_string(value_dict_get(value_array_get(eadded, i), "kind")), "new_creature") == 0) ev_found = true;
    CHECK_TRUE("the event marker is in own.events.added", ev_found);
    value_fini(&d);

    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " diff check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: the diff reports exactly what changed, in a fraction of the full view");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
