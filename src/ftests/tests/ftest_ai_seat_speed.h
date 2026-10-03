#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

TbBool ftest_ai_seat_speed_init();
void ftest_ai_seat_speed_pre_start();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
