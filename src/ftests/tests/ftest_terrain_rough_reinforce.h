#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * @brief Rough terrain's reinforcement easing (docs/newFeatures/terrain-v2/, phase 7): stamps a
 * pocket of claimed floor in earth beside the heart and drops imps in it. While they reinforce its
 * walls, each wall's drawn progress has to climb with their hits, at least one wall has to end up
 * fortified, and the terrain audit of every frame drawn on the way has to stay clean (no vertex
 * drawn at two positions, no T-junction, no sub-triangle turned over). Then the imps are removed
 * mid-wall, and that wall's progress has to relax back to nothing, never climbing.
 */
TbBool ftest_terrain_rough_reinforce_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
