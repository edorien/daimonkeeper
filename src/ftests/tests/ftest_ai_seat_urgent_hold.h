#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

TbBool ftest_ai_seat_urgent_hold_init();
void ftest_ai_seat_urgent_hold_pre_start();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
