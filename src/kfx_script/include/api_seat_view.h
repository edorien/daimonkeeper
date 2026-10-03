#ifndef DK_API_SEAT_VIEW_H
#define DK_API_SEAT_VIEW_H

#include "globals.h"
#include <json-dom.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Fills `out` (an uninitialised VALUE) with the seat's view for get_player_view. Caller value_fini()s it. */
void api_seat_build_view(VALUE *out, PlayerNumber plyr_idx);

/** Dig marks no imp of the seat could get to: a slab-level flood fill from the seat's own walkable ground (where it can
 *  drop imps) through walkable slabs and through marked slabs (they become floor once dug). A mark the fill never
 *  reaches is unreachable. Marked means tagged already, or inside a mark_dig the seat has submitted but not finished
 *  tagging, or inside one of the `n_assume` rectangles in `assume4` (slab quads x0,y0,x1,y1, any corner order: earlier
 *  orders of a batch being checked). With `report4` (one such quad, NULL = none), the slabs of that rectangle that a
 *  mark_dig would tag count as marked too, and only those are reported -- the check for a mark_dig not yet placed.
 *  Writes up to `max` slab (x, y) pairs to `out_xy` (may be NULL) and returns how many there are in all. */
int64_t api_seat_unreachable_dig_slabs(PlayerNumber plyr_idx, const int64_t *assume4, int64_t n_assume, const int64_t *report4,
    int64_t *out_xy, int64_t max);
/** Slabs the seat has not seen that one of its own fortified walls is built facing (wall_slab_shows_face_toward): open
 *  ground -- dug earth, water or lava, a room, a door -- or another keeper's wall, known from the wall as a keeper
 *  watching it knows it, e.g. a tunnel being dug around the dungeon. Writes up to `max` slab (x, y) pairs to `out_xy`
 *  (may be NULL), in map order, and returns how many there are in all. */
int64_t api_seat_open_behind_walls(PlayerNumber plyr_idx, int64_t *out_xy, int64_t max);

#ifdef __cplusplus
}
#endif

#endif
