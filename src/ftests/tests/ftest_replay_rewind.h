#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

TbBool ftest_replay_rewind_init();
TbBool ftest_replay_rewind_resync_init();
void ftest_replay_rewind_pre_start();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
