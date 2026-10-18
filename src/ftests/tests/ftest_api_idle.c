// A level just started, left running for an outside client to play through the local API (scripts/llm_bridge/): the
// human's own keeper, heart zoom running, nothing else done. Ends when the client creates the file named by the
// environment variable KFX_API_IDLE_STOP, or after a time limit, and logs what the world looks like then. A development
// aid for reproducing what an agent met live; not part of the regular list of tests that pass or fail.
#include "ftest_api_idle.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <stdio.h>
#include <stdlib.h>

#include "../ftest.h"

#include "bflib_datetm.h"
#include "config_keeperfx.h"
#include "dungeon_data.h"
#include "game_legacy.h"
#include "kfx_sim_state.h"
#include "player_data.h"
#include "player_instances.h"
#include "tasks_list.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define API_IDLE_LIMIT_MS 900000

static TbClockMSec s_started = 0;

FTestActionResult idle_01_start(struct FTestActionArgs* const args);
FTestActionResult idle_02_wait(struct FTestActionArgs* const args);

TbBool ftest_api_idle_init()
{
    ftest_append_action(idle_01_start, 0, NULL);
    ftest_append_action(idle_02_wait, 1, NULL);
    return true;
}

FTestActionResult idle_01_start(struct FTestActionArgs* const args)
{
    set_player_instance(get_my_player(), PI_HeartZoom, 0);
    s_started = LbTimerClock();
    FTESTLOG("APIIDLE: ready at turn %" PRId64, (int64_t)get_gameturn());
    return FTRs_Go_To_Next_Action;
}

FTestActionResult idle_02_wait(struct FTestActionArgs* const args)
{
    const char *stop = getenv("KFX_API_IDLE_STOP");
    FILE *f = (stop != NULL) ? fopen(stop, "r") : NULL;
    if (f == NULL && (LbTimerClock() - s_started) < API_IDLE_LIMIT_MS)
        return FTRs_Repeat_Current_Action;
    if (f != NULL) fclose(f);
    const struct Dungeon *d = get_players_dungeon(get_my_player());
    int64_t marks = 0;
    for (int64_t i = 0; i < MAPTASKS_COUNT; i++) if (d->task_list[i].kind != 0) marks++;
    FTESTLOG("APIIDLE: done at turn %" PRId64 ": dig marks %" PRId64 ", gold %" PRId64 ", diggers %" PRId64,
        (int64_t)get_gameturn(), marks, (int64_t)d->total_money_owned, (int64_t)d->num_active_diggers);
    FTESTLOG("Test passed: left running for an outside client");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
