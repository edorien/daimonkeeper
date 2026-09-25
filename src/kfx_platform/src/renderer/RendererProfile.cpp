// gpu-v2 Phase C.5: heavy-log-only renderer frame-time measurement. See
// renderer/RendererProfile.h. Empty in the standard build.
#include "pre_inc.h"
#include "renderer/RendererProfile.h"
#include "bflib_basics.h"
#include "globals.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include "post_inc.h"

#if (BFDEBUG_LEVEL > 0)

#define RPROF_REPORT_FRAMES 120

static const char *const stage_names[RPS_COUNT] = {
    "drawlist", "submit", "build", "encode", "gpuwait", "readback", "present"
};
static const char *const counter_names[RPC_COUNT] = {
    "ops", "polys", "sprites", "shadows", "verts", "draws", "blk_up", "spr_quads", "atlas_B", "lookups"
};

struct StageAcc { uint64_t start; double frame_ms; double sum_ms; double max_ms; int64_t hits; };
static StageAcc s_stage[RPS_COUNT];
static int64_t s_counter_frame[RPC_COUNT];
static double s_counter_sum[RPC_COUNT];
static int64_t s_frames = 0;
static uint64_t s_last_frame_tick = 0;
static double s_period_sum_ms = 0, s_period_max_ms = 0;

static double ticks_to_ms(uint64_t ticks)
{
    return (double)ticks * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

void RendererProfileBegin(int stage)
{
    if (stage >= 0 && stage < RPS_COUNT)
        s_stage[stage].start = SDL_GetPerformanceCounter();
}

void RendererProfileEnd(int stage)
{
    if (stage < 0 || stage >= RPS_COUNT || s_stage[stage].start == 0)
        return;
    s_stage[stage].frame_ms += ticks_to_ms(SDL_GetPerformanceCounter() - s_stage[stage].start);
    s_stage[stage].hits++;
    s_stage[stage].start = 0;
}

void RendererProfileCount(int counter, int64_t amount)
{
    if (counter >= 0 && counter < RPC_COUNT)
        s_counter_frame[counter] += amount;
}

int RendererProfileSyncGpu(void)
{
    static int cached = -1;
    if (cached < 0)
    {
        const char *v = getenv("KFX_GPU_PROF_SYNC");
        cached = (v != NULL && v[0] == '1') ? 1 : 0;
    }
    return cached;
}

void RendererProfileFrame(const char *renderer_name)
{
    const uint64_t now = SDL_GetPerformanceCounter();
    if (s_last_frame_tick != 0)
    {
        const double period = ticks_to_ms(now - s_last_frame_tick);
        s_period_sum_ms += period;
        if (period > s_period_max_ms) s_period_max_ms = period;
    }
    s_last_frame_tick = now;
    for (int i = 0; i < RPS_COUNT; i++)
    {
        s_stage[i].sum_ms += s_stage[i].frame_ms;
        if (s_stage[i].frame_ms > s_stage[i].max_ms) s_stage[i].max_ms = s_stage[i].frame_ms;
        s_stage[i].frame_ms = 0;
    }
    for (int i = 0; i < RPC_COUNT; i++)
    {
        s_counter_sum[i] += (double)s_counter_frame[i];
        s_counter_frame[i] = 0;
    }
    if (++s_frames < RPROF_REPORT_FRAMES)
        return;

    char line[768];
    int n = snprintf(line, sizeof(line), "RPROF [%s] %d frames: period %.2f/%.2f ms (avg/max, %.0f fps) |",
        renderer_name != NULL ? renderer_name : "?", (int)s_frames, s_period_sum_ms / s_frames, s_period_max_ms,
        s_period_sum_ms > 0 ? 1000.0 * s_frames / s_period_sum_ms : 0.0);
    for (int i = 0; i < RPS_COUNT && n > 0 && n < (int)sizeof(line); i++)
    {
        if (s_stage[i].sum_ms <= 0)
            continue;
        n += snprintf(line + n, sizeof(line) - (size_t)n, " %s %.2f/%.2f", stage_names[i], s_stage[i].sum_ms / s_frames, s_stage[i].max_ms);
    }
    n += snprintf(line + n, sizeof(line) - (size_t)n, " |");
    for (int i = 0; i < RPC_COUNT && n > 0 && n < (int)sizeof(line); i++)
    {
        if (s_counter_sum[i] <= 0)
            continue;
        n += snprintf(line + n, sizeof(line) - (size_t)n, " %s %.0f", counter_names[i], s_counter_sum[i] / s_frames);
    }
    JUSTMSG("%s", line);

    s_frames = 0;
    s_period_sum_ms = s_period_max_ms = 0;
    for (int i = 0; i < RPS_COUNT; i++) { s_stage[i].sum_ms = 0; s_stage[i].max_ms = 0; s_stage[i].hits = 0; }
    for (int i = 0; i < RPC_COUNT; i++) s_counter_sum[i] = 0;
}

#endif
