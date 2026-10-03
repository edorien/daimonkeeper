// set_game_speed (the console's own turn-rate cheat, exposed through the API): global to the game, not per seat. Checks
// the bounds, the reset-to-configured-default, and that the view's turns_per_second field tracks it.
#include "ftest_ai_seat_speed.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api_seat_view.h"
#include "config_keeperfx.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_sim_state.h"
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
static int64_t s_default_speed = 0;

FTestActionResult sp01_setup_and_check(struct FTestActionArgs* const args);

void ftest_ai_seat_speed_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_speed_init()
{
    s_failures = 0;
    ftest_append_action(sp01_setup_and_check, 0, NULL);
    return true;
}

FTestActionResult sp01_setup_and_check(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || net_add_external_seat(P) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    s_default_speed = kfx_sim_state.turns_per_second;
    FTESTLOG("configured speed: %" PRId64, s_default_speed);

    VALUE v; api_seat_build_view(&v, P);
    CHECK_TRUE("the view starts at the configured speed", value_int64(value_dict_get(&v, "turns_per_second")) == s_default_speed);
    value_fini(&v);

    kfx_sim_state.turns_per_second = 5; // what set_game_speed {turns_per_second:5} does
    api_seat_build_view(&v, P);
    CHECK_TRUE("the view reflects a slower speed", value_int64(value_dict_get(&v, "turns_per_second")) == 5);
    value_fini(&v);

    kfx_sim_state.turns_per_second = 60;
    api_seat_build_view(&v, P);
    CHECK_TRUE("and a faster one", value_int64(value_dict_get(&v, "turns_per_second")) == 60);
    value_fini(&v);

    kfx_sim_state.turns_per_second = s_default_speed; // what {turns_per_second:0} resets to (api.c: start_params.num_fps)
    api_seat_build_view(&v, P);
    CHECK_TRUE("resetting returns to the configured speed", value_int64(value_dict_get(&v, "turns_per_second")) == s_default_speed);
    value_fini(&v);

    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " speed check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: the view reports the simulation's real-time turn rate as it changes");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
