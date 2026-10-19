// A room taken over next to a room of the same kind its new owner has becomes part of that room (upstream #5467):
// it used to stay a room of its own, beside the other, with its own flag and efficiency.
//
// Setup: in a block of solid rock, PLAYER0's 3x2 library with a neutral 3x2 library beside it, and below them
// PLAYER0's 3x2 library with a neutral 3x2 temple beside it (the control: another kind). The neutral rooms are
// taken over by PLAYER0 (take_over_room(), as a script's CHANGE_SLAB_OWNER or Lua's room.owner do; an imp's claim
// goes through the same merge). After it the two libraries are one room of 12 slabs, PLAYER0's; the temple is a room
// of its own.
#include "ftest_room_merge_on_claim.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "config_keeperfx.h"
#include "../ftest.h"
#include "../ftest_util.h"

#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "room_data.h"
#include "slab_data.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLOCK_MARGIN 2

static MapSlabCoord bx, by;

/** A block of rock w x h slabs (plus the margin) to build in: found by scanning the map. */
static TbBool find_rock_block(MapSlabCoord w, MapSlabCoord h, MapSlabCoord *out_x, MapSlabCoord *out_y)
{
    for (MapSlabCoord y = 1; y + h + 2 * BLOCK_MARGIN < kfx_sim_state.map_tiles_y - 1; y++)
        for (MapSlabCoord x = 1; x + w + 2 * BLOCK_MARGIN < kfx_sim_state.map_tiles_x - 1; x++)
        {
            TbBool all_rock = true;
            for (MapSlabCoord j = y; (j < y + h + 2 * BLOCK_MARGIN) && all_rock; j++)
                for (MapSlabCoord i = x; (i < x + w + 2 * BLOCK_MARGIN) && all_rock; i++)
                    all_rock = (get_slabmap_block(i, j)->kind == SlbT_ROCK);
            if (all_rock)
            {
                *out_x = x + BLOCK_MARGIN;
                *out_y = y + BLOCK_MARGIN;
                return true;
            }
        }
    return false;
}

static struct Room *room_at(MapSlabCoord slb_x, MapSlabCoord slb_y)
{
    return slab_room_get(slb_x, slb_y);
}

static FTestActionResult room_merge_a01_build(struct FTestActionArgs* const args)
{
    (void)args;
    if (!find_rock_block(6, 5, &bx, &by))
    {
        FTEST_FAIL_TEST("No block of rock 6x5 slabs to build in");
        return FTRs_Go_To_Next_Action;
    }
    // row 0: PLAYER0's library | neutral library; row 3: PLAYER0's library | neutral temple (a rock row between)
    if (!ftest_util_replace_slabs(bx, by, bx + 2, by + 1, SlbT_LIBRARY, PLAYER0)
     || !ftest_util_replace_slabs(bx + 3, by, bx + 5, by + 1, SlbT_LIBRARY, PLAYER_NEUTRAL)
     || !ftest_util_replace_slabs(bx, by + 3, bx + 2, by + 4, SlbT_LIBRARY, PLAYER0)
     || !ftest_util_replace_slabs(bx + 3, by + 3, bx + 5, by + 4, SlbT_TEMPLE, PLAYER_NEUTRAL))
    {
        FTEST_FAIL_TEST("Could not build the rooms");
        return FTRs_Go_To_Next_Action;
    }
    struct Room *own = room_at(bx, by), *neutral = room_at(bx + 3, by);
    if (room_is_invalid(own) || room_is_invalid(neutral) || (own == neutral) || (neutral->owner != PLAYER_NEUTRAL))
    {
        FTEST_FAIL_TEST("Setup: expected two libraries, PLAYER0's and a neutral one");
        return FTRs_Go_To_Next_Action;
    }
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult room_merge_a02_take_over_and_check(struct FTestActionArgs* const args)
{
    (void)args;
    struct Room *neutral_library = room_at(bx + 3, by);
    struct Room *neutral_temple = room_at(bx + 3, by + 3);
    if (room_is_invalid(neutral_library) || room_is_invalid(neutral_temple))
    {
        FTEST_FAIL_TEST("Setup rooms are gone");
        return FTRs_Go_To_Next_Action;
    }
    take_over_room(neutral_library, PLAYER0);
    take_over_room(neutral_temple, PLAYER0);

    struct Room *left = room_at(bx, by), *right = room_at(bx + 3, by);
    if (room_is_invalid(left) || (left != right))
    {
        FTEST_FAIL_TEST("The libraries are still two rooms (%" PRId64 " and %" PRId64 ")", (int64_t)left->index, (int64_t)right->index);
        return FTRs_Go_To_Next_Action;
    }
    if ((left->owner != PLAYER0) || (left->slabs_count != 12))
    {
        FTEST_FAIL_TEST("The merged library: owner %" PRId64 ", %" PRId64 " slabs; expected PLAYER0's, 12", (int64_t)left->owner, (int64_t)left->slabs_count);
        return FTRs_Go_To_Next_Action;
    }
    struct Room *library = room_at(bx, by + 3), *temple = room_at(bx + 3, by + 3);
    if (room_is_invalid(temple) || (temple == library) || (temple->owner != PLAYER0) || (temple->slabs_count != 6))
    {
        FTEST_FAIL_TEST("The temple should stay a room of its own, PLAYER0's, of 6 slabs");
        return FTRs_Go_To_Next_Action;
    }
    FTESTLOG("libraries merged into room %" PRId64 " of %" PRId64 " slabs; the temple stays room %" PRId64, (int64_t)left->index, (int64_t)left->slabs_count, (int64_t)temple->index);
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_room_merge_on_claim_init()
{
    ftest_append_action(room_merge_a01_build, 20, NULL);
    ftest_append_action(room_merge_a02_take_over_and_check, 10, NULL);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
