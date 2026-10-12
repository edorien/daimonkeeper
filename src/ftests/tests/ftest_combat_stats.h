#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

TbBool ftest_combat_stats_init();
void ftest_combat_stats_pre_start();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
