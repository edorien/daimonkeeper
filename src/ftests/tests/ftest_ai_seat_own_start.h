#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

TbBool ftest_ai_seat_own_start_init();
TbBool ftest_ai_seat_own_start_midzoom_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
