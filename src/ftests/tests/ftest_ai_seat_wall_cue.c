// Open ground behind a seat's walls: a fortified wall is built with a face toward any neighbour that is not plain
// earth or rock (map_blocks.c get_against), whether or not the keeper has seen that neighbour, so a human watching
// the wall knows when somebody tunnels past it. The seat view shows such a slab as '.o' (still unseen: no kind) and
// lists it under own.open_behind_walls, and the decision tick raises wall_cue when one appears. Layout, in slabs from
// the seat's heart (hsx, hsy): claimed floor on row hsy+4, the seat's wall on row hsy+5, earth below; one earth slab
// below the wall is hidden from the seat and later dug out (as a tunneller would), a second stays hidden earth.
#include "ftest_ai_seat_wall_cue.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>
#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api_seat_decision.h"
#include "api_seat_view.h"
#include "config_keeperfx.h"
#include "config_players.h"
#include "dungeon_data.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "map_data.h"
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
static MapSlabCoord hsx, hsy;
static GameTurn s_wait_until;
#define TUNNEL_X (hsx)
#define TUNNEL_Y (hsy + 6)
#define EARTH_X (hsx + 1)
#define EARTH_Y (hsy + 6)

FTestActionResult wc01_setup(struct FTestActionArgs* const args);
FTestActionResult wc02_hidden_earth_then_dig(struct FTestActionArgs* const args);
FTestActionResult wc03_sensed(struct FTestActionArgs* const args);
FTestActionResult wc04_revealed(struct FTestActionArgs* const args);

void ftest_ai_seat_wall_cue_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_wall_cue_init()
{
    s_failures = 0;
    ftest_append_action(wc01_setup, 0, NULL);
    ftest_append_action(wc02_hidden_earth_then_dig, 2, NULL);
    ftest_append_action(wc03_sensed, 1, NULL);
    ftest_append_action(wc04_revealed, 1, NULL);
    return true;
}

static void conceal_slab(MapSlabCoord sx, MapSlabCoord sy)
{
    for (int dx = 0; dx < STL_PER_SLB; dx++)
        for (int dy = 0; dy < STL_PER_SLB; dy++)
            conceal_map_block(get_map_block_at(slab_subtile(sx, dx), slab_subtile(sy, dy)), P);
}

static void reveal_slab(MapSlabCoord sx, MapSlabCoord sy)
{
    for (int dx = 0; dx < STL_PER_SLB; dx++)
        for (int dy = 0; dy < STL_PER_SLB; dy++)
            reveal_map_block(get_map_block_at(slab_subtile(sx, dx), slab_subtile(sy, dy)), P);
}

static void map_cell(MapSlabCoord x, MapSlabCoord y, char out[3])
{
    VALUE v; api_seat_build_view(&v, P);
    const char* row = value_string(value_array_get(value_dict_get(value_dict_get(&v, "map"), "rows"), (size_t)y));
    out[0] = row ? row[2 * x] : '?';
    out[1] = row ? row[2 * x + 1] : '?';
    out[2] = 0;
    value_fini(&v);
}

static TbBool listed_open(MapSlabCoord x, MapSlabCoord y, int64_t* count)
{
    int64_t xy[2 * 64];
    const int64_t n = api_seat_open_behind_walls(P, xy, 64);
    if (count) *count = n;
    for (int64_t i = 0; (i < n) && (i < 64); i++) if ((xy[2 * i] == x) && (xy[2 * i + 1] == y)) return true;
    return false;
}

static TbBool decision_says(const char* reason)
{
    VALUE v; api_seat_build_view(&v, P);
    VALUE* reasons = value_dict_get(value_dict_get(value_dict_get(&v, "seat"), "decision"), "reasons");
    TbBool found = false;
    for (size_t i = 0; reasons && i < value_array_size(reasons); i++)
        if (strcmp(value_string(value_array_get(reasons, i)), reason) == 0) found = true;
    value_fini(&v);
    return found;
}

FTestActionResult wc01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || net_add_external_seat(P) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    api_seat_decision_set_min_interval(0);
    ftest_util_reveal_map(P);
    const struct Thing* heart = find_players_dungeon_heart(P);
    hsx = subtile_slab(heart->mappos.x.stl.num); hsy = subtile_slab(heart->mappos.y.stl.num);
    ftest_util_replace_slabs(hsx - 3, hsy + 4, hsx + 3, hsy + 8, SlbT_EARTH, kfx_config_state.neutral_player_num);
    ftest_util_replace_slabs(hsx - 3, hsy + 4, hsx + 3, hsy + 4, SlbT_CLAIMED, P);
    ftest_util_replace_slabs(hsx - 3, hsy + 5, hsx + 3, hsy + 5, SlbT_WALLDRAPE, P);
    conceal_slab(TUNNEL_X, TUNNEL_Y);
    conceal_slab(EARTH_X, EARTH_Y);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult wc02_hidden_earth_then_dig(struct FTestActionArgs* const args)
{
    char c[3];
    int64_t n = -1;
    map_cell(TUNNEL_X, TUNNEL_Y, c);
    CHECK_TRUE("hidden earth behind the wall is plain unseen '..'", strcmp(c, "..") == 0);
    CHECK_TRUE("and not listed as open ground", !listed_open(TUNNEL_X, TUNNEL_Y, &n));
    CHECK_TRUE("nothing on the map is open ground behind the seat's walls yet", n == 0);
    // Somebody digs the slab out, unseen by the seat (a tunneller's dig).
    ftest_util_replace_slabs(TUNNEL_X, TUNNEL_Y, TUNNEL_X, TUNNEL_Y, SlbT_PATH, kfx_config_state.neutral_player_num);
    conceal_slab(TUNNEL_X, TUNNEL_Y);
    s_wait_until = get_gameturn() + 5;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult wc03_sensed(struct FTestActionArgs* const args)
{
    if (get_gameturn() < s_wait_until) return FTRs_Repeat_Current_Action;
    char c[3];
    int64_t n = -1;
    map_cell(TUNNEL_X, TUNNEL_Y, c);
    CHECK_TRUE("the dug slab is still unseen, and shows as open ground behind a wall: '.o'", strcmp(c, ".o") == 0);
    CHECK_TRUE("it is listed in own.open_behind_walls", listed_open(TUNNEL_X, TUNNEL_Y, &n));
    CHECK_TRUE("and it is the only one", n == 1);
    map_cell(EARTH_X, EARTH_Y, c);
    CHECK_TRUE("the hidden earth next to it tells nothing: still '..'", strcmp(c, "..") == 0);
    CHECK_TRUE("the decision raised wall_cue", decision_says("wall_cue"));
    reveal_slab(TUNNEL_X, TUNNEL_Y);
    s_wait_until = get_gameturn() + 2;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult wc04_revealed(struct FTestActionArgs* const args)
{
    if (get_gameturn() < s_wait_until) return FTRs_Repeat_Current_Action;
    char c[3];
    int64_t n = -1;
    map_cell(TUNNEL_X, TUNNEL_Y, c);
    CHECK_TRUE("once seen, the slab shows its kind (no longer '.o')", c[0] != '.');
    CHECK_TRUE("and leaves the list", !listed_open(TUNNEL_X, TUNNEL_Y, &n) && (n == 0));
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " wall-cue check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: open ground behind a seat's wall is shown as '.o', listed, and raises wall_cue until seen");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
