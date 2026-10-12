// Refactor pass 5, P5-F32: a level's first packet doesn't take its map position from the level before it.
// The level is run twice in one process (repeat_n_times); a few turns in, each run reads the local user's cursor
// light, which turn 0's packet placed; the second run must find it where the first did. What the pointer points at
// (kfx_render_state's pick, set when a frame is drawn) once outlived the level, so turn 0 of every level after a
// process's first read the last level's last frame -- which only a second level in one process shows.
#include "ftest_level_change_pointer.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"

#include "config_keeperfx.h"
#include "kfx_sim_state.h"
#include "packet_data.h"
#include "player_data.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_runs;
static struct Coord3d s_first_pos;
static TbBool s_first_on;

FTestActionResult ftest_level_change_pointer_action001(struct FTestActionArgs* const args)
{
    const int64_t idx = get_user_state(get_local_user())->cursor_light_idx;
    if ((idx <= 0) || (idx >= LIGHTS_COUNT))
    {
        FTEST_FAIL_TEST("the local user has no cursor light (%" PRId64 ")", idx);
        return FTRs_Go_To_Next_Action;
    }
    const struct Light *lgt = &kfx_sim_state.light_registry.lights[idx];
    // light_turn_light_on() sets LgtF_CanTurnOff: the light is on
    const TbBool on = ((lgt->flags & LgtF_CanTurnOff) != 0);
    s_runs++;
    FTESTLOG("run %" PRId64 " of the level: cursor light %" PRId64 " at (%" PRId64 ",%" PRId64 "), %s", s_runs, idx,
        (int64_t)lgt->mappos.x.val, (int64_t)lgt->mappos.y.val, on ? "on" : "off");
    if (s_runs == 1)
    {
        s_first_pos = lgt->mappos;
        s_first_on = on;
    }
    else if ((lgt->mappos.x.val != s_first_pos.x.val) || (lgt->mappos.y.val != s_first_pos.y.val) || (on != s_first_on))
    {
        FTEST_FAIL_TEST("run %" PRId64 " of the level put the cursor light at (%" PRId64 ",%" PRId64 "), run 1 at (%" PRId64 ",%" PRId64 ")",
            s_runs, (int64_t)lgt->mappos.x.val, (int64_t)lgt->mappos.y.val,
            (int64_t)s_first_pos.x.val, (int64_t)s_first_pos.y.val);
    }
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_level_change_pointer_init()
{
    ftest_append_action(ftest_level_change_pointer_action001, 3, NULL);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
