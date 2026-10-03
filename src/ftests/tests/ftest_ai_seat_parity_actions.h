#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

TbBool ftest_ai_seat_parity_actions_init();
void ftest_ai_seat_parity_actions_pre_start();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
