#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

// Regression coverage for the ftest harness itself (not gameplay): a pre_start_func that fails via
// FTEST_FAIL_TEST must be treated as a hard test failure straight away -- the harness must not go on to
// wait for the level to load and run init_func/actions regardless. See ftest_harness_setup_failure.c.
void ftest_harness_setup_failure_pre_start();
TbBool ftest_harness_setup_failure_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
