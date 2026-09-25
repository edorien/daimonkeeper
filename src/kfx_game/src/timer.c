/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file timer.c
 *     Level-load timing support functions. See timer.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "timer.h"

#include "globals.h"
#include "bflib_datetm.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
static TbClockMSec level_load_times[LevelLoadTime_Count];
static TbClockMSec level_load_total_start;
static TbClockMSec level_load_phase_start;
static enum LevelLoadTimeKind level_load_phase;
static TbBool level_load_time_active;
/******************************************************************************/

void level_load_time_phase(enum LevelLoadTimeKind kind)
{
    TbClockMSec now = LbTimerClock();
    if (kind == LevelLoadTime_EngineStartup && !level_load_time_active) {
        memset(level_load_times, 0, sizeof(level_load_times));
        level_load_total_start = now;
        level_load_phase_start = now;
        level_load_phase = LevelLoadTime_EngineStartup;
        level_load_time_active = true;
        return;
    }
    if (!level_load_time_active)
        return;
    level_load_times[level_load_phase] += now - level_load_phase_start;
    if (kind == LevelLoadTime_Total) {
        level_load_times[LevelLoadTime_Total] = now - level_load_total_start;
        JUSTLOG("Level load timing: Engine startup: %" PRId64 " ms, Custom sprites: %" PRId64 " ms, Config files: %" PRId64 " ms, Level data: %" PRId64 " ms, Navigation: %" PRId64 " ms, Game setup: %" PRId64 " ms, Total: %" PRId64 " ms", (int64_t)(level_load_times[LevelLoadTime_EngineStartup]), (int64_t)(level_load_times[LevelLoadTime_Sprites]), (int64_t)(level_load_times[LevelLoadTime_Configs]), (int64_t)(level_load_times[LevelLoadTime_Data]), (int64_t)(level_load_times[LevelLoadTime_Navigation]), (int64_t)(level_load_times[LevelLoadTime_GameSetup]), (int64_t)(level_load_times[LevelLoadTime_Total]));
        level_load_time_active = false;
        return;
    }
    level_load_phase = kind;
    level_load_phase_start = now;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
