#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * @brief HD sprite packs, Phase 0 measurements (docs/refactor/sprites/03-verification-and-phases.md
 * section C). Not a regression test: it fails when a stop draws nothing, or when a kind of sprite draw
 * the colour gates rely on (remapped, ghost, alpha ramp, mirrored) never shows. It logs:
 * - one "SPRDEN:" line per stop, how big thing sprites are drawn (destination pixels per source texel,
 *   area-weighted percentiles, and the draws by kind), at the closest, default and farthest overhead zoom,
 *   with frozen, invisible, chicken, flashing creatures and an alpha-ramp flame, through each palette
 *   effect, in the front view, in first person and in the power hand;
 * - with KFX_FTEST_SD_KEYS=<file>, "SPRKEYS:" and the engine's content key of every sprite frame in that
 *   file (scripts/spritepack.py check-keys compares it); with KFX_FTEST_SD_SHOTS=1, a screenshot per stop.
 */
TbBool ftest_sprite_density_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
