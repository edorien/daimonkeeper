#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

TbBool ftest_keeper_assist_money_init();
void ftest_keeper_assist_money_pre_start();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
