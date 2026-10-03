#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

TbBool ftest_campaign_external_seat_checkbox_init();
void ftest_campaign_external_seat_checkbox_pre_start();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
