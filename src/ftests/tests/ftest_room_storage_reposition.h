#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * @brief Storage rooms putting their contents back after changing shape
 * (refactor pass 3, S03): a garden, graveyard, workshop and two libraries are
 * filled, then have slabs sold, rebuilt, and sold again until the room is
 * smaller than its contents. Logs every room's capacities and every object and
 * corpse in the area after each step ("RST:" lines) for a before/after
 * comparison.
 */
TbBool ftest_room_storage_reposition_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
