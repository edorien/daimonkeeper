#ifndef RENDERER_RENDERERPROFILE_H
#define RENDERER_RENDERERPROFILE_H

#include <stdint.h>
#include "globals.h" // KFX_DEBUG_ON

/* gpu-v2 Phase C.5: frame-time measurement for the renderer backends.
 *
 * Active only at the Debug log level and above (the LOG_LEVEL option,
 * docs/refactor-pass2/stage-02-logging-option.md), or with the environment
 * variable KFX_RPROF=1 at any level (measurements that the Debug level's own
 * logging would slow down): otherwise every RPROF_* macro is a single
 * predictable branch, and no timing or report happens.
 *
 * Stages are wall-clock CPU intervals (SDL_GetPerformanceCounter). Every
 * RPROF_FRAME() (one per game-frame present) closes the current frame; every
 * RPROF_REPORT_FRAMES frames one `RPROF` line is written to keeperfx.log with,
 * per stage, average/max milliseconds per frame, and per counter the
 * per-frame average -- the same line format for the Software and Vulkan
 * backends, so their runs compare directly.
 *
 * Nested stages (build/encode inside submit) are reported separately, not
 * subtracted. `gpuwait` (Vulkan only) is the time to fence-wait for the GPU to
 * finish the frame's world pass; it is only measured when the environment
 * variable KFX_GPU_PROF_SYNC=1 is set, because forcing that wait serialises
 * CPU and GPU and so changes the frame time it is measuring. `present`
 * includes any VSYNC wait -- run with VSYNC=OFF to measure cost, not refresh rate. */
#ifdef __cplusplus
extern "C" {
#endif

enum RendererProfileStage {
    RPS_DRAWLIST = 0, /* engine_render.c display_drawlist(): the whole world walk (CPU raster or recording) */
    RPS_SUBMIT,       /* RendererGpu3D::SubmitWorldFrame, total */
    RPS_BUILD,        /*   op walk: block-cache resolve, vertex + sprite-quad build */
    RPS_ENCODE,       /*   copy pass + render pass encode + command-buffer submit */
    RPS_GPUWAIT,      /*   fence wait for the world pass (KFX_GPU_PROF_SYNC=1 only) */
    RPS_READBACK,     /* eye-lens GPU->CPU read-back (RendererCopyFrameRect) */
    RPS_PRESENT,      /* RendererPresentGameFrame(): compositing + ImGui + present (+ vsync) */
    RPS_SHADOWS,      /*   inside drawlist: creature shadows (mask drawing, soft coverage, rasterising or recording) */
    RPS_COUNT
};

enum RendererProfileCounter {
    RPC_OPS = 0,        /* recorded ops (poly + sprite + shadow) */
    RPC_POLYS,
    RPC_SPRITES,
    RPC_SHADOWS,
    RPC_VERTICES,       /* terrain vertices */
    RPC_DRAW_RUNS,      /* GPU draw calls */
    RPC_BLOCK_UPLOADS,  /* 32x32 terrain block layers uploaded */
    RPC_SPRITE_QUADS,   /* sprite/shadow vertices uploaded */
    RPC_ATLAS_BYTES,    /* sprite index-atlas bytes uploaded */
    RPC_LOOKUP_ENTRIES, /* scaling-lookup entries uploaded */
    RPC_MASK_BYTES,     /* shadow-mask bytes uploaded (to the index atlas) */
    RPC_HD_BYTES,       /* HD sprite atlas bytes uploaded */
    RPC_ATLAS_CLEARS,   /* sprite atlas clears or page evictions -- reported as a total over the report's frames */
    RPC_SKIPPED,        /* sprite/shadow ops not drawn (atlas or upload budget full) -- a total, like clears */
    RPC_COUNT
};

void RendererProfileBegin(int stage);
void RendererProfileEnd(int stage);
void RendererProfileCount(int counter, int64_t amount);
/* Closes a frame; `renderer_name` labels the report. */
void RendererProfileFrame(const char *renderer_name);
int  RendererProfileSyncGpu(void); /* KFX_GPU_PROF_SYNC=1 */
/* KFX_RPROF=1: the profiler runs whatever the log level. */
extern int rprof_forced;
/* True while the profiler runs (Debug log level and above, or KFX_RPROF=1). */
#define RPROF_ACTIVE()             (KFX_DEBUG_ON(0) || rprof_forced)
#define RPROF_BEGIN(stage)         do { if (RPROF_ACTIVE()) RendererProfileBegin(stage); } while (0)
#define RPROF_END(stage)           do { if (RPROF_ACTIVE()) RendererProfileEnd(stage); } while (0)
#define RPROF_COUNT(counter, n)    do { if (RPROF_ACTIVE()) RendererProfileCount((counter), (int64_t)(n)); } while (0)
#define RPROF_FRAME(name)          do { if (RPROF_ACTIVE()) RendererProfileFrame(name); } while (0)
#define RPROF_SYNC_GPU()           (RPROF_ACTIVE() && RendererProfileSyncGpu())

#ifdef __cplusplus
}
/* RAII stage timer for functions with several return paths. Ends only a
 * stage it began, so a level change in between is harmless. */
struct RendererProfileScope {
    int stage;
    bool active;
    explicit RendererProfileScope(int s) : stage(s), active(RPROF_ACTIVE()) { if (active) RendererProfileBegin(s); }
    ~RendererProfileScope() { if (active) RendererProfileEnd(stage); }
};
#define RPROF_SCOPE(stage) RendererProfileScope rprof_scope_##stage(stage)
#endif
#endif
