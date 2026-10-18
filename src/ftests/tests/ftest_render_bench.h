#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * @brief A render-cost measurement (docs/plans/sprite-atlas-thrash.md, Phase 0), not a regression test: on original
 * level 6, KFX_FTEST_RB_CREATURES (default 60) creatures of every model are placed round player 0's heart, the camera
 * is pinned over them at the default overhead zoom, and every frame is drawn. After a warm-up it logs one "RBENCH:"
 * line -- turns a second over KFX_FTEST_RB_TURNS (default 600) turns (paced at most 20 a second, so a frame time is
 * the finer measure) -- and, with KFX_RPROF=1, the renderer's RPROF lines (frame stages, shadows, atlas uploads,
 * evictions). Run it alone with the renderer, asset pack and shadow style under test in the run's daimonkeeper.cfg:
 *     KFX_RPROF=1 daimonkeeper -ftests render_bench -includelongtests -exitonfailedtest -headless
 * (-headless with SDL_VIDEO_DRIVER=offscreen keeps a Vulkan device with no window shown).
 * It fails only when the creatures can't be placed.
 */
TbBool ftest_render_bench_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
