#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * @brief HD sprite packs, Phase 8 (docs/refactor/sprites/03-verification-and-phases.md): the sprite options switched
 * live at a stop change only what they should, and switching back restores the frame exactly. The test writes its
 * own pack (fxdata/asset_packs/ftest_sprite_switch/sprites/: the frames the level's things show at x2, colours inverted, so
 * HD shows) with a druid (a zip sprite) and a wizard placed in view, both again at each stop; then at a first-person stop (possessing an imp) and at the
 * closest overhead zoom it draws: the baseline; ASSET_PACK = the pack (must change the frame); the baseline;
 * SPRITE_TRUE_COLOUR on (must change the overhead frame, where the druid is); the baseline; the pack with
 * TEXTURE_FILTER Bilinear (Vulkan: must differ from the pack alone; Software: must not); the baseline again. Every
 * baseline must equal the first, frame for frame. The simulation is suspended while a stop's states are drawn, and
 * -framehash on, so a view draws the same frame each time; a state's frame is taken once its pack is all decoded.
 */
TbBool ftest_sprite_hd_switch_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
