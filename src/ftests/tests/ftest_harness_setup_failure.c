#include "ftest_harness_setup_failure.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "game_legacy.h"
#include "config_keeperfx.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// Reproduces the skirmish_setup_override hang (docs/refactor/skirmish/): a pre_start_func can legitimately
// bail out early via FTEST_FAIL_TEST (e.g. the setup it needs isn't available), and the harness must treat
// that exactly like any other test failure -- fail fast under -exitonfailedtest, or move on to the next
// test otherwise -- instead of going on to wait for FTF_LevelLoaded and run init_func/actions. Deliberately
// uses a level (keeporig 1) that always loads cleanly: the point isn't to reproduce a level that hangs on
// load, it's to prove the harness never even gets that far once pre_start_func has already failed the test.
void ftest_harness_setup_failure_pre_start()
{
    FTEST_FAIL_TEST("Deliberate pre_start failure -- proves the harness aborts/moves on instead of waiting "
        "for level load and running init_func/actions anyway");
}

FTestActionResult ftest_harness_setup_failure_action001__must_not_run(struct FTestActionArgs* const args)
{
    // If this ever executes, ftest_update()'s FTSt_TestIsProcessingActions handling has regressed back to
    // ignoring a pre_start_func failure (src/ftests/ftest.c): it waited for the level to load and ran
    // init_func/actions despite the test already being marked failed.
    FTEST_FAIL_TEST("Harness regression: action ran despite pre_start_func already failing this test");
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_harness_setup_failure_init()
{
    // Never actually reached while the harness is behaving correctly -- see the action's own comment.
    ftest_append_action(ftest_harness_setup_failure_action001__must_not_run, 0, NULL);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
