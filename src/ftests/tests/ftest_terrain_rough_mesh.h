#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * @brief Terrain mesh audit (docs/newFeatures/terrain-v2/03-verification-and-phases.md
 * sections A and B): parks the camera at a set of stops in each overhead view
 * setting, then at first-person stops while possessing an imp, and checks the
 * terrain audit of a frame drawn there -- no lattice
 * vertex drawn at two positions, no full audit table. Logs one "TRNMESH:" line
 * per stop with the frame's polygon hash, so two binaries can be compared.
 */
TbBool ftest_terrain_rough_mesh_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
