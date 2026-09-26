#ifndef DK_API_SEAT_VIEW_H
#define DK_API_SEAT_VIEW_H

#include "globals.h"
#include <json-dom.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Fills `out` (an uninitialised VALUE) with the seat's view for get_player_view. Caller value_fini()s it. */
void api_seat_build_view(VALUE *out, PlayerNumber plyr_idx);

#ifdef __cplusplus
}
#endif

#endif
