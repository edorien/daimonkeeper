#ifndef RENDERER_RENDERERPROFILE_H
#define RENDERER_RENDERERPROFILE_H

#include <stdint.h>

/* gpu-v2 Phase C.5: frame-time measurement for the renderer backends.
 *
 * Compiled in ONLY for the heavy-log build (keeperfx_hvlog, BFDEBUG_LEVEL > 0);
 * in the standard build every RPROF_* macro is `((void)0)` and
 * RendererProfile.cpp is empty, so the release binary carries no timing code.
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
    RPC_COUNT
};

#if (BFDEBUG_LEVEL > 0)
void RendererProfileBegin(int stage);
void RendererProfileEnd(int stage);
void RendererProfileCount(int counter, int64_t amount);
/* Closes a frame; `renderer_name` labels the report. */
void RendererProfileFrame(const char *renderer_name);
int  RendererProfileSyncGpu(void); /* KFX_GPU_PROF_SYNC=1 */
#define RPROF_BEGIN(stage)         RendererProfileBegin(stage)
#define RPROF_END(stage)           RendererProfileEnd(stage)
#define RPROF_COUNT(counter, n)    RendererProfileCount((counter), (int64_t)(n))
#define RPROF_FRAME(name)          RendererProfileFrame(name)
#define RPROF_SYNC_GPU()           RendererProfileSyncGpu()
#else
#define RPROF_BEGIN(stage)         ((void)0)
#define RPROF_END(stage)           ((void)0)
#define RPROF_COUNT(counter, n)    ((void)0)
#define RPROF_FRAME(name)          ((void)0)
#define RPROF_SYNC_GPU()           (0)
#endif

#ifdef __cplusplus
}
#if (BFDEBUG_LEVEL > 0)
/* RAII stage timer for functions with several return paths. */
struct RendererProfileScope {
    int stage;
    explicit RendererProfileScope(int s) : stage(s) { RendererProfileBegin(s); }
    ~RendererProfileScope() { RendererProfileEnd(stage); }
};
#define RPROF_SCOPE(stage) RendererProfileScope rprof_scope_##stage(stage)
#else
#define RPROF_SCOPE(stage) ((void)0)
#endif
#endif
#endif
