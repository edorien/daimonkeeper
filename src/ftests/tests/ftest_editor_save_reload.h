#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * docs/refactor/editor/phase3/00-slice1-native-save.md -- proves
 * editor_save_map() round-trips through the real engine, not just through
 * KfxNativeMapContentWriter/Reader in isolation (that's
 * map_content_roundtrip_test.cpp's job, in kfx_sim_utest). Places a
 * creature and a trap via the same explicit-position Redo verbs the other
 * editor ftests already established (no ambient-position dependency),
 * calls editor_save_map() directly to a fresh, otherwise-unused level
 * number in the real staged campaign directory (never overwriting
 * "keeporig" itself -- other editor ftests depend on that staying
 * pristine), reloads that saved level via the real production
 * load_map_file(), and asserts both things reappear with the right
 * model/owner/position. This exercises the whole pipeline together:
 * kfx_editor's snapshot step, KfxNativeMapContentWriter, the loader's
 * regenerate_derived_map_data() fallback (the saved map never writes
 * .dat/.clm), and KfxNativeMapContentWriter's own .lof (still loadable by
 * lvnum even without a campaign .lof rescan).
 *
 * docs/refactor/editor/phase3/01-slice2-classic-save.md extends this same
 * test (actions 009-011) to Force-Classic-save the just-reloaded state to
 * a second scratch level number and reload *that*, proving
 * ClassicMapContentWriter/Reader round-trip through the real engine too.
 */
TbBool ftest_editor_save_reload_init();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
