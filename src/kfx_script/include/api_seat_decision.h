/**
 * @file api_seat_decision.h
 * @brief "A decision is due" for External seats (docs/refactor/AI/LLM/02 section 4b).
 *
 * A real-time agent should think at natural moments, not on a fixed timer: at each quarter of a pay day, and when
 * something important happens (a fight with a rival, an attack on the heart or a room, a breach, a room lost, the first
 * creature of a new kind arriving). Once per turn the seat's situation is compared with the last look; when a reason has
 * appeared and the minimum interval since the last decision has passed, the decision counter goes up and the client that
 * subscribed to the DECISION_DUE event is told ({player, seq, turn, reasons, quarter}). The latest decision is also in
 * the view as seat.decision, for clients that poll. Reasons that arrive inside the minimum interval are held and
 * delivered together when it ends, so a long battle does not become a stream of calls.
 */
#ifndef DK_API_SEAT_DECISION_H
#define DK_API_SEAT_DECISION_H

#include "globals.h"
#include <json-dom.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Once per game turn from api_update_server(). Cheap; does nothing without an External seat. */
void api_seat_decision_tick(void);
/** Adds seat.decision to a view being built for `plyr_idx`. */
void api_seat_decision_add_to_view(VALUE *seat, PlayerNumber plyr_idx);
/** Minimum game turns between two decisions (default 100). */
void api_seat_decision_set_min_interval(int64_t turns);
/** Forgets every seat's baseline (a new game). */
void api_seat_decision_reset(void);

#ifdef __cplusplus
}
#endif

#endif
