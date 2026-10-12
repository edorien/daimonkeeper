#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * @brief Texture resolution, Phase 6 (docs/newFeatures/texture-resolution/03-verification-and-phases.md): the
 * texture options switched live at a stop change only what they should, and switching back restores the frame
 * exactly. The test writes its own HD pack (fxdata/asset_packs/ftest_hd_switch/textures/: the level's own tmapa file at 64
 * texels, every block's colours inverted, so HD shows), then at the closest and farthest overhead zoom draws:
 * the baseline; ASSET_PACK = the pack (the closest stop must change; the farthest -- the original 32x32 art
 * there -- must not); TEXTURE_MIPMAPS on (the farthest must change); TEXTURE_FILTER Bilinear (no change with the
 * Software renderer); each followed by the baseline again, which must equal the first frame for frame.
 * Turns -framehash on for its own frames and suspends the simulation, so a view draws the same frame each time.
 */
TbBool ftest_texture_hd_switch_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
