#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * @brief Texture resolution, Phase 0 measurements (docs/newFeatures/texture-resolution/
 * 03-verification-and-phases.md section A). Not a regression test: it fails only when a
 * stop draws nothing. It logs:
 * - one "TRNTEX:" line per stop, the drawn terrain's texel density (texel size on screen and
 *   texels per pixel, area-weighted percentiles), at the closest, default and farthest zoom of
 *   the overhead views (rough terrain off and on), first-person stops and the front view;
 * - "GPUTEX:" lines, the Vulkan renderer's terrain texture cache per turn, through a pain fade,
 *   the freeze palette and a lightning flash ("GPUTEX: n/a" under the Software renderer);
 * - one "TRNMEM:" line, the process's resident memory after the level loaded (Linux).
 */
TbBool ftest_texture_density_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
