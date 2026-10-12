// Refactor pass 5, S12: a level's navigation mesh doesn't depend on the levels played before it in the process.
// The level is run twice in one process (repeat_n_times); a few turns in, each run hashes the mesh's saved state
// (ariadne_saved_state, what a save holds); the second run must hash the same as the first. The level reset
// (ariadne_reset_for_level()) once left the incremental build's guards set, so every level after a process's first
// built on an empty mesh -- which only a second level in one process shows.
#include "ftest_level_change_mesh.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <stdlib.h>
#include <string.h>

#include "../ftest.h"

#include "ariadne_saved_state.h"
#include "config_keeperfx.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_runs;
static uint64_t s_first_hash;

static uint64_t mesh_hash(void)
{
    const size_t n = ariadne_saved_state_size();
    unsigned char *buf = (unsigned char *)malloc(n);
    if (buf == NULL)
        return 0;
    ariadne_saved_state_write(buf);
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++)
        h = (h ^ buf[i]) * 0x100000001b3ULL;
    free(buf);
    return h;
}

FTestActionResult ftest_level_change_mesh_action001(struct FTestActionArgs* const args)
{
    const uint64_t h = mesh_hash();
    s_runs++;
    FTESTLOG("run %" PRId64 " of the level: mesh hash %016" PRIx64, s_runs, h);
    if (s_runs == 1)
    {
        s_first_hash = h;
    }
    else if (h != s_first_hash)
    {
        FTEST_FAIL_TEST("run %" PRId64 " of the level built another mesh than run 1 (%016" PRIx64 " vs %016" PRIx64 ")",
            s_runs, h, s_first_hash);
    }
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_level_change_mesh_init()
{
    ftest_append_action(ftest_level_change_mesh_action001, 3, NULL);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
