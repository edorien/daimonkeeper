#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

TbBool ftest_spectator_handoff_init();
void ftest_spectator_handoff_pre_start();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
