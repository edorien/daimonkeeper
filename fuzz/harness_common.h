/** @file harness_common.h
 *     What the harnesses share: a runtime directory to run in, and a scratch directory per input (security pass S08).
 *
 *     The game finds its files below keeper_runtime_directory. The harnesses use a staged one in /dev/shm: the repo's
 *     config/ files (fxdata/, creatrs/, ...) linked in, so the base configuration loads as the game's does, plus
 *     whatever an input writes. KFX_FUZZ_DATA names another directory to link instead of the repo's config/.
 */
#ifndef KFX_FUZZ_HARNESS_COMMON_H
#define KFX_FUZZ_HARNESS_COMMON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Installs the callback tables (the ports) as the game's startup does. Called by fuzz_runtime_dir(). */
void fuzz_wire_ports(void);

/** The staged runtime directory (created and set as keeper_runtime_directory on first call). Never NULL. */
const char *fuzz_runtime_dir(void);

/** A directory of this process's own below the runtime directory, empty, named `name`-<pid>; returns its path relative
 *  to the runtime directory (what a campaign location holds). Removed by fuzz_scratch_clean(). */
const char *fuzz_scratch_dir(char *rel, size_t rel_size, const char *name);

/** Writes `size` bytes to <runtime>/<rel>/<file>. @return 0 on success. */
int fuzz_write_file(const char *rel, const char *file, const uint8_t *data, size_t size);

/** Removes the scratch directory fuzz_scratch_dir() made (its files and itself). */
void fuzz_scratch_clean(const char *rel);

#ifdef __cplusplus
}
#endif
#endif
