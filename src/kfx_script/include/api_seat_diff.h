/**
 * @file api_seat_diff.h
 * @brief "What changed since the view you last had" for get_player_view (docs/refactor/AI/LLM/03, M6).
 *
 * Every view sent to a seat is remembered as that seat's baseline and carries a `view_id`. A request with
 * `since: <view_id>` that matches the baseline is answered with only the differences; anything else gets the full view
 * (with a `reason`), so a client that lost track simply resynchronises. The diff is generic over the view's JSON tree:
 *   - a changed scalar appears under the same path with its new value; a key that vanished is listed in `_removed`;
 *   - an array becomes {added, removed, changed}: elements are matched by their `id` when they have one (creatures,
 *     rooms, traps, events...), otherwise by content (dig marks, powers); `changed` carries the element's `id` plus the
 *     fields that changed;
 *   - the map's `rows` become `changes` (runs of changed cells: {y, x, cells}) and `revealed` (runs of slabs that were
 *     hidden and now are: {y, x, len}), which is the confirmation of a build and the news of newly seen ground.
 */
#ifndef DK_API_SEAT_DIFF_H
#define DK_API_SEAT_DIFF_H

#include "globals.h"
#include <json-dom.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Finishes a freshly built full view `view` for `plyr_idx`: numbers it (`view_id`, `mode`), stores it as the new
 * baseline, and if `want_diff` and `since` names the previous baseline, replaces `*view` with the diff.
 * `*view` is owned by the caller either way.
 */
void api_seat_finish_view(VALUE *view, PlayerNumber plyr_idx, TbBool want_diff, int64_t since);

/** Diff of two arbitrary view trees, as described above. `out` must be an initialised dict. Exposed for tests. */
void api_seat_diff_values(VALUE *out, const VALUE *prev, const VALUE *cur);
/** Forgets every baseline (a new game). */
void api_seat_diff_reset(void);

#ifdef __cplusplus
}
#endif

#endif
