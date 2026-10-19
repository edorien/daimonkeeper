#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool; //redefine rather than include extraneus header info

TbBool ftest_bug_teleport_locked_door__locked_init();
TbBool ftest_bug_teleport_locked_door__unlocked_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
