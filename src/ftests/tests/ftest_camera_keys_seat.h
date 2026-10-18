#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The camera follows the movement keys in a plain game, with an External seat on the local player's keeper
 * (an agent playing it), and with the game paused in each of those. Holds a movement key in each case and checks the
 * local camera moved.
 */
TbBool ftest_camera_keys_seat_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
