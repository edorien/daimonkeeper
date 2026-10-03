#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * @brief The local player's view transitions (refactor pass 2, S15): direct
 * and passenger possession and leaving them, the controlled creature dying,
 * and the parchment map (the live no-fade path and the fade path). Logs the
 * local presentation state every turn ("LVT:" lines) and checks the map's
 * UI hold puts the status menu and tooltips back.
 */
TbBool ftest_local_view_transitions_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
