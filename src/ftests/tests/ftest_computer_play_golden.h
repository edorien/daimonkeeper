#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * @brief Rename this init function to reflect the name of your test, eg: ftest_myexampletest_init
 * 
 */
TbBool ftest_computer_play_golden_init();
void ftest_computer_play_golden_pre_start();
TbBool ftest_computer_arena_golden_init();


#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
