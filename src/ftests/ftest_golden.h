/******************************************************************************/
/** @file ftest_golden.h
 *     Golden hashes of what a script line (or anything else) does to a saved level.
 * @par Purpose:
 *     A level is saved once; each case loads that save, does its thing, and hashes the bytes of kfx_sim_state,
 *     kfx_game_state, kfx_config_state, the intralevel data and the pathfinding state a save holds
 *     (kfx_pathfinding_state and Ariadne's mesh) that differ from the state right after loading
 *     (bytes that differ between two loads of the save are left out: pointers, wall-clock times; so are the level
 *     statistics' end_time and gameplay_time, real times that winning or losing writes). The hashes are
 *     compared with a table, or printed (KFX_FTEST_GOLDEN_PRINT=1, lines prefixed "GOLDEN:") to make one.
 *     The hashes depend on the data and base config the game runs on (daimonkeeper.cfg's FLEE_BUTTON_DEFAULT
 *     alone changes some), so print them where StageFtestData.cmake staged (out/coverage-ftest/, as
 *     build-coverage-core.sh runs the tests), never in an install or another run directory.
 *     KFX_FTEST_GOLDEN_ONLY=<text> runs only the cases whose name contains the text; KFX_FTEST_GOLDEN_DUMP=<text>
 *     logs each byte the matching cases change. KFX_FTEST_GOLDEN_CUT=<block>:<start>:<len>:<stride>:<count>[,...]
 *     hashes as if those byte ranges weren't in the block (a layout change's check: the commit before it, with the
 *     removed fields cut, must print the hashes the new layout prints). KFX_FTEST_GOLDEN_MASK, the same format, leaves
 *     the bytes out without moving the others (two builds that differ only there print the same hashes).
 */
/******************************************************************************/
#ifndef FTEST_GOLDEN_H
#define FTEST_GOLDEN_H

#ifdef FUNCTESTING

#include "bflib_basics.h"

#ifdef __cplusplus
extern "C" {
#endif

struct FTestGoldenExpected { const char *name; uint64_t hash; };

struct FTestGolden {
    int64_t slot;
    const struct FTestGoldenExpected *expected; /**< ends with {NULL, 0} */
    TbBool print;
    TbBool saved;
    int64_t checked;
    int64_t failures;
};

/** Saves the level to the golden's slot and takes the reference; false (after failing the test) if it can't. */
TbBool ftest_golden_begin(struct FTestGolden *g, int64_t slot, const struct FTestGoldenExpected *expected);
/** Loads the save again, unpaused, with no script condition open. */
TbBool ftest_golden_load(const struct FTestGolden *g);
/** Whether a case runs (KFX_FTEST_GOLDEN_ONLY). */
TbBool ftest_golden_selected(const char *name);
/** Prints or checks the hash of what changed since the load, under the given name. */
void ftest_golden_check(struct FTestGolden *g, const char *name);
/** The same, with a result the case produced (a return value, an error) hashed in too; printed after the hash. */
void ftest_golden_check_result(struct FTestGolden *g, const char *name, const char *result);
/** Loads the save one last time and reports: fails the test if any hash differed. */
void ftest_golden_finish(struct FTestGolden *g, const char *what);

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
#endif
