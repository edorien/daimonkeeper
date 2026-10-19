#include "ftest_bug_spiral_ap_range.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "actionpt.h"
#include "config_keeperfx.h"
#include "dungeon_data.h"
#include "game_legacy.h"
#include "thing_creature.h"
#include "thing_list.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// An AT_ACTION_POINT creature search on an action point whose range is wider than the spiral_step[] table reaches:
// the spiral length is (2 * range_in_subtiles + 1)^2, past SPIRAL_STEPS_COUNT from about 25 subtiles. The search
// must stay inside the table. The read past it is only visible under AddressSanitizer (scripts/build-fuzz.sh asan-ft),
// where it aborts; in a normal build this checks the search still finds the creature next to the point.
#define TEST_AP_NUMBER 250
#define TEST_AP_RANGE_STL 40

struct ftest_bug_spiral_ap_range__variables
{
    struct Thing *imp;
    ActionPointId apt_idx;
};
struct ftest_bug_spiral_ap_range__variables ftest_bug_spiral_ap_range__vars = {
    .imp = NULL,
    .apt_idx = 0,
};

FTestActionResult ftest_bug_spiral_ap_range_action001__setup(struct FTestActionArgs* const args);
FTestActionResult ftest_bug_spiral_ap_range_action002__search(struct FTestActionArgs* const args);

TbBool ftest_bug_spiral_ap_range_init()
{
    ftest_append_action(ftest_bug_spiral_ap_range_action001__setup, 20, &ftest_bug_spiral_ap_range__vars);
    ftest_append_action(ftest_bug_spiral_ap_range_action002__search, 10, &ftest_bug_spiral_ap_range__vars);
    return true;
}

FTestActionResult ftest_bug_spiral_ap_range_action001__setup(struct FTestActionArgs* const args)
{
    struct ftest_bug_spiral_ap_range__variables* const vars = args->data;
    struct Thing *heart = get_player_soul_container(PLAYER0);
    if (thing_is_invalid(heart))
    {
        FTEST_FAIL_TEST("Player 0 has no dungeon heart");
        return FTRs_Go_To_Next_Action;
    }
    vars->imp = create_owned_special_digger(heart->mappos.x.val + subtile_coord(3, 0), heart->mappos.y.val, PLAYER0);
    if (thing_is_invalid(vars->imp))
    {
        FTEST_FAIL_TEST("Failed to create imp");
        return FTRs_Go_To_Next_Action;
    }
    struct ActionPoint *apt = allocate_free_action_point_structure_with_number(TEST_AP_NUMBER);
    if (action_point_is_invalid(apt))
    {
        FTEST_FAIL_TEST("Failed to create action point %d", TEST_AP_NUMBER);
        return FTRs_Go_To_Next_Action;
    }
    apt->mappos.x.val = heart->mappos.x.val;
    apt->mappos.y.val = heart->mappos.y.val;
    apt->range = subtile_coord(TEST_AP_RANGE_STL, 0);
    vars->apt_idx = action_point_number_to_index(TEST_AP_NUMBER);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_bug_spiral_ap_range_action002__search(struct FTestActionArgs* const args)
{
    struct ftest_bug_spiral_ap_range__variables* const vars = args->data;
    struct Thing *found = script_get_creature_by_criteria(PLAYER0, vars->imp->model, CSelCrit_NearAP | (vars->apt_idx << 4));
    // Level 1 has more imps around the heart; the search picks one of them at random
    struct ActionPoint *apt = action_point_get(vars->apt_idx);
    struct Coord3d apt_pos = {0};
    apt_pos.x.val = apt->mappos.x.val;
    apt_pos.y.val = apt->mappos.y.val;
    if (thing_is_invalid(found) || (found->owner != PLAYER0) || (found->model != vars->imp->model)
      || (get_2d_distance(&found->mappos, &apt_pos) > apt->range))
    {
        FTEST_FAIL_TEST("AT_ACTION_POINT search on a %d-subtile action point found %s, not one of player 0's imps in range",
            TEST_AP_RANGE_STL, thing_is_invalid(found) ? "nothing" : thing_model_name(found));
        return FTRs_Go_To_Next_Action;
    }
    if (count_player_creatures_of_model_in_action_point(PLAYER0, vars->imp->model, vars->apt_idx) < 1)
    {
        FTEST_FAIL_TEST("count_player_creatures_of_model_in_action_point doesn't see the imp");
        return FTRs_Go_To_Next_Action;
    }
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
