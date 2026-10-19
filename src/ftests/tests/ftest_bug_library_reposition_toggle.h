#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool; //redefine rather than include extraneus header info

TbBool ftest_bug_library_reposition_toggle__expand_init();
TbBool ftest_bug_library_reposition_toggle__moved_init();
TbBool ftest_bug_library_reposition_toggle__dropped_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
