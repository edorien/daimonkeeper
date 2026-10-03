// kfx_game: sounds.c. Almost every function here reaches into real
// OpenAL/S3D audio emitters, Thing/Room world state, or player camera
// state, and isn't attempted here. reset_ambient_sound_thing_idx() is a
// bare setter, and ambient_sound_stop()'s invalid-thing early return is
// reachable with the sentinel index reset_ambient_sound_thing_idx()
// itself sets (index 0, thing_data.c's reserved "invalid" slot), so no
// real sound emitter is ever touched.
#include <catch2/catch_test_macros.hpp>

#include "sounds.h"
#include "kfx_game_state.h"
#include "kfx_sim_state.h"

#include <cstring>

namespace {
struct ResetState {
    ResetState() {
        std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
    }
};
}

TEST_CASE_METHOD(ResetState, "reset_ambient_sound_thing_idx clears the stored thing index", "[kfx_game][sounds]") {
    kfx_game_state.ambient_sound_thing_idx = 42;
    reset_ambient_sound_thing_idx();
    CHECK(kfx_game_state.ambient_sound_thing_idx == 0);
}

TEST_CASE_METHOD(ResetState, "ambient_sound_stop is false when the stored thing index is the reserved invalid sentinel", "[kfx_game][sounds]") {
    reset_ambient_sound_thing_idx(); // -> index 0, thing_data.c's reserved slot
    CHECK_FALSE(ambient_sound_stop());
}

TEST_CASE_METHOD(ResetState, "reset_ambient_sound_thing_idx also restarts the footstep cycle, with no creature of the previous level", "[kfx_game][sounds]") {
    kfx_game_local.footstep_timeslice = 2;
    kfx_game_local.footstep_near_creatures[1] = 42;
    reset_ambient_sound_thing_idx();
    CHECK(kfx_game_local.footstep_timeslice == 0);
    CHECK(kfx_game_local.footstep_near_creatures[1] == 0);
}
