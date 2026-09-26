#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

TbBool ftest_ai_bridge_smoke_init();
void ftest_ai_bridge_smoke_pre_start();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
