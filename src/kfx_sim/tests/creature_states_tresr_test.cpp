// kfx_sim "creature" cluster, a further depth increment: the first
// individual creature_states_*.c file (not creature_states.c's central
// dispatcher, tested in creature_states_test.cpp) --
// creature_states_tresr.c, the smallest of the per-state files (57
// lines), picked for exactly that reason, the same "cheapest candidate
// first" discipline every stage in this whole plan has followed.
//
// creature_able_to_get_salary() calls creature_stats_get_from_thing(),
// which reads thing->model (since refactor pass 2's S05 moved it into
// kfx_sim; before that it went through a ConfigReloadCallbacks fake).
#include <catch2/catch_test_macros.hpp>

#include "creature_states_tresr.h"
#include "config_creature.h"
#include "kfx_config_state.h"
#include "thing_data.h"
#include "thing_stats.h"

#include <cstring>

namespace {
struct CreatureStatsFixture {
    struct Thing thing{};

    CreatureStatsFixture() {
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        kfx_config_state.conf.crtr_conf.model_count = CREATURE_TYPES_MAX;
    }
};
}

TEST_CASE_METHOD(CreatureStatsFixture, "creature_able_to_get_salary is true when the resolved model's pay is nonzero", "[kfx_sim][creature_states_tresr]") {
    kfx_config_state.conf.crtr_conf.model[5].pay = 100;
    thing.model = 5;
    CHECK(creature_able_to_get_salary(&thing));
}

TEST_CASE_METHOD(CreatureStatsFixture, "creature_able_to_get_salary is false when the resolved model's pay is zero", "[kfx_sim][creature_states_tresr]") {
    kfx_config_state.conf.crtr_conf.model[5].pay = 0;
    thing.model = 5;
    CHECK_FALSE(creature_able_to_get_salary(&thing));
}

TEST_CASE_METHOD(CreatureStatsFixture, "creature_able_to_get_salary is false for the reserved model-0 sentinel, regardless of its pay field", "[kfx_sim][creature_states_tresr]") {
    kfx_config_state.conf.crtr_conf.model[0].pay = 100; // would look eligible if read directly
    thing.model = 0;
    CHECK_FALSE(creature_able_to_get_salary(&thing)); // creature_stats_invalid() catches model 0 first
}
