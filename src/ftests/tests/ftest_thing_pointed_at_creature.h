#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * @brief The pointer over a creature picks it (kfx_render_state.thing_pointed_at, which shows its thought bubble)
 * whichever way its sprite faces, and doesn't pick it when it's elsewhere.
 */
TbBool ftest_thing_pointed_at_creature_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
