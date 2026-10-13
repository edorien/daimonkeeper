#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

TbBool ftest_state_corruption_fuzz_init();
void ftest_state_corruption_fuzz_pre_start();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
