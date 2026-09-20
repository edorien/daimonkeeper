#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * The Skirmish Setup tab's script override in a real running game (docs/refactor/skirmish/): before the
 * level loads, the tab's state module edits the level's setup exactly as the UI would and installs the
 * one-shot override (skirmish_setup_install_for_play); the actions then check the *live* sim -- money,
 * limits, pool, availability, win conditions, AI model -- against the edited values, and the values the
 * tab did not touch against the level's own.
 *
 * Runs on the original-pack multiplayer map 50 ("Multiplayer 1"): a LEVEL_VERSION-less (v0) script, so it
 * also proves the v0 -> v1 translation (CREATURE_AVAILABLE) and the forced-v1 prelude end to end.
 */
TbBool ftest_skirmish_setup_init();
void ftest_skirmish_setup_pre_start();

/**
 * Second scenario, a LEVEL_VERSION(1) map with runtime-controlled rows (dk2maps map 220): edits to the
 * locked rows are ignored, edits elsewhere apply, and the level's own runtime conditions are still loaded.
 */
TbBool ftest_skirmish_setup_locks_init();
void ftest_skirmish_setup_locks_pre_start();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
