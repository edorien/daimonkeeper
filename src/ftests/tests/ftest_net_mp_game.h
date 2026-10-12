#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

TbBool ftest_net_mp_game_host_init();
TbBool ftest_net_mp_game_join_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
