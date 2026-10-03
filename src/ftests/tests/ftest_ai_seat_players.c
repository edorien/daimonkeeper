// docs/refactor/AI/omissions/09-persistent-memory.md section 6.8: the seat's view names every player in the level
// (players[]), so an agent can keep lessons about an opponent across games. A local game's human takes the keeper
// name (the network screen's name, or -nick) as their player name, as a network game already did; a computer keeper
// is known by its built-in AI outside campaign levels (skirmish setup shows it; a campaign's script picks it unseen);
// an External seat by its own name.
#include "ftest_ai_seat_players.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>
#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api_seat_view.h"
#include "config_keeperfx.h"
#include "config_strings.h"
#include "frontend.h"
#include "game_legacy.h"
#include "net_game.h"
#include "net_main.h"
#include "player_data.h"
#include "thing_list.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define CHECK_TRUE(what, cond) do { if (!(cond)) { s_failures++; FTESTLOG("CHECK FAILED: %s", what); } } while (0)

static PlayerNumber s_rival = -1;

FTestActionResult pl01_before_the_seat(struct FTestActionArgs* const args);
FTestActionResult pl02_as_a_seat(struct FTestActionArgs* const args);

// The keeper name is set before the level starts, as the network screen or -nick would have set it.
void ftest_ai_seat_players_pre_start()
{
    fe_computer_players = 1;
    snprintf(net_player_name, sizeof(net_player_name), "%s", "Robin");
}

TbBool ftest_ai_seat_players_init()
{
    s_failures = 0;
    ftest_append_action(pl01_before_the_seat, 0, NULL);
    ftest_append_action(pl02_as_a_seat, 2, NULL);
    return true;
}

// The players[] entry for player p in a view built for `seat` (NULL if absent); the view must outlive it.
static VALUE *player_entry(VALUE *view, PlayerNumber p)
{
    VALUE *pls = value_dict_get(view, "players");
    for (size_t i = 0; i < value_array_size(pls); i++) {
        VALUE *e = value_array_get(pls, i);
        if (value_int32(value_dict_get(e, "player")) == p)
            return e;
    }
    return NULL;
}

static TbBool str_is(VALUE *e, const char *key, const char *want)
{
    const char *got = (e != NULL) ? value_string(value_dict_get(e, key)) : NULL;
    if (want == NULL)
        return (e != NULL) && (value_dict_get(e, key) == NULL);
    return (got != NULL) && (strcmp(got, want) == 0);
}

FTestActionResult pl01_before_the_seat(struct FTestActionArgs* const args)
{
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++) {
        if ((p != my_player_number) && (p != PLAYER_GOOD) && (p != PLAYER_NEUTRAL) && player_exists(get_player(p))
         && !thing_is_invalid(find_players_dungeon_heart(p))) {
            s_rival = p;
            break;
        }
    }
    if (s_rival < 0) {
        FTEST_FAIL_TEST("no second keeper on this level");
        return FTRs_Go_To_Next_Action;
    }
    CHECK_TRUE("the local human took the keeper name at level start", strcmp(get_player(my_player_number)->player_name, "Robin") == 0);
    // The view is built for a seat: the rival becomes one to read it, then goes back to the built-in AI.
    const NetUserId user = net_add_external_seat(s_rival);
    CHECK_TRUE("the rival became a seat", user > 0);
    VALUE v;
    api_seat_build_view(&v, s_rival);
    VALUE *me = player_entry(&v, my_player_number);
    CHECK_TRUE("players[] lists the human by kind and keeper name", str_is(me, "kind", "human") && str_is(me, "name", "Robin"));
    CHECK_TRUE("a human has no AI type", str_is(me, "ai_type", NULL));
    CHECK_TRUE("a keeper with a heart is alive", (me != NULL) && value_bool(value_dict_get(me, "alive")));
    VALUE *rival = player_entry(&v, s_rival);
    CHECK_TRUE("the seat itself is listed as external, with its default name", str_is(rival, "kind", "external") && str_is(rival, "name", "External 1"));
    CHECK_TRUE("the neutral player is not listed", player_entry(&v, PLAYER_NEUTRAL) == NULL);
    value_fini(&v);
    net_release_external_seat(s_rival);
    if (s_failures > 0)
        FTEST_FAIL_TEST("%" PRId64 " check(s) failed", s_failures);
    return FTRs_Go_To_Next_Action;
}

// Seen from the human's own slot (claimed as a seat, M10): the rival, released back to the built-in AI, is a computer
// keeper known by its AI type on this multiplayer (non-campaign) map.
FTestActionResult pl02_as_a_seat(struct FTestActionArgs* const args)
{
    const NetUserId user = net_add_external_seat(my_player_number);
    CHECK_TRUE("the human's own slot can be viewed as a seat", user > 0);
    VALUE v;
    api_seat_build_view(&v, my_player_number);
    VALUE *rival = player_entry(&v, s_rival);
    CHECK_TRUE("the rival is a computer keeper again", str_is(rival, "kind", "computer"));
    const char *ai = (rival != NULL) ? value_string(value_dict_get(rival, "ai_type")) : NULL;
    CHECK_TRUE("outside a campaign a computer keeper is known by its AI type", (ai != NULL) && (ai[0] != 0));
    FTESTLOG("rival %d AI type: %s", (int)s_rival, ai ? ai : "(none)");
    value_fini(&v);
    net_release_external_seat(my_player_number);
    if (s_failures > 0) {
        FTEST_FAIL_TEST("%" PRId64 " check(s) failed", s_failures);
        return FTRs_Go_To_Next_Action;
    }
    FTESTLOG("Test passed: the view names every player, the human by their keeper name");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
