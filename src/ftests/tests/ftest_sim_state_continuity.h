#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

TbBool ftest_sim_state_continuity_init();
TbBool ftest_sim_state_continuity_restart_init();
void ftest_sim_state_continuity_pre_start();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
