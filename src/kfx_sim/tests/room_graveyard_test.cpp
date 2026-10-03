// kfx_sim "room" cluster depth increment, per docs/refactor/testing/
// comprehensive/stage-08b-kfx-sim-clusters.md's "still open" list:
// room_graveyard.c's add_body_to_graveyard(), the one function in this file
// that's a self-contained pattern-A candidate. Its guard,
// corpse_laid_to_rest(), reads get_creature_model_flags() -- and a corpse
// with the zeroed thing->model 0 resolves to "no CMF_NoCorpseRotting
// flag" (get_creature_model_flags returns 0 for any model below 1). The rest of the file (reposition/count_bodies_*)
// needs a real map+thing-list fixture and is left for a later increment.
//
// deadtng must be a real kfx_sim_state.things_data[] slot, not a stack
// local: thing_is_invalid() (which corpse_laid_to_rest -> thing_exists
// goes through) range-checks the pointer against the real things_data
// array, so a stack Thing is unconditionally "invalid" -- caught by an
// empirical run failing exactly the "already laid to rest" case before
// switching to thing_get(1), the same discipline this whole plan uses.
#include <catch2/catch_test_macros.hpp>

#include "room_graveyard.h"
#include "room_data.h"
#include "thing_data.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "thing_stats.h"

#include <cstring>

namespace {
struct ResetSimState {
    ResetSimState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
    }
};
}

TEST_CASE_METHOD(ResetSimState, "add_body_to_graveyard refuses a corpse when the room is already at capacity", "[kfx_sim][room_graveyard]") {
    struct Room room{};
    room.kind = RoK_GRAVEYARD;
    room.total_capacity = 2;
    room.used_capacity = 2;
    struct Thing *deadtng = thing_get(1);

    CHECK_FALSE(add_body_to_graveyard(deadtng, &room));
    CHECK(room.used_capacity == 2);
    CHECK(deadtng->corpse.laid_to_rest == 0);
}

TEST_CASE_METHOD(ResetSimState, "add_body_to_graveyard refuses a corpse that's already laid to rest", "[kfx_sim][room_graveyard]") {
    struct Room room{};
    room.kind = RoK_GRAVEYARD;
    room.total_capacity = 2;
    room.used_capacity = 0;
    struct Thing *deadtng = thing_get(1);
    deadtng->alloc_flags = TAlF_Exists;
    deadtng->class_id = TCls_DeadCreature;
    deadtng->corpse.laid_to_rest = 1;

    CHECK_FALSE(add_body_to_graveyard(deadtng, &room));
    CHECK(room.used_capacity == 0); // rejected before the increment
}

TEST_CASE_METHOD(ResetSimState, "add_body_to_graveyard admits a fresh corpse and starts its configured decay timer", "[kfx_sim][room_graveyard]") {
    kfx_config_state.conf.rules[0].rooms.graveyard_convert_time = 500;
    struct Room room{};
    room.kind = RoK_GRAVEYARD;
    room.owner = 0;
    room.total_capacity = 2;
    room.used_capacity = 0;
    // Not yet existing/laid to rest -- corpse_laid_to_rest() is false.
    struct Thing *deadtng = thing_get(1);

    CHECK(add_body_to_graveyard(deadtng, &room));
    CHECK(room.used_capacity == 1);
    CHECK(deadtng->corpse.laid_to_rest == 1);
    CHECK(deadtng->health == 500);
}
