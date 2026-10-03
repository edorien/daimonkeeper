// kfx_config: config_funcnames.c's name tables (refactor pass 2, S03).
// Each table names the slots of a function-pointer table in kfx_sim. The
// kfx_sim side has a _Static_assert that its table covers *_SLOTS; this
// checks the names side: exactly *_COUNT named entries, every index below
// *_SLOTS, no index used twice, and a {NULL, 0} terminator.
#include <catch2/catch_test_macros.hpp>

#include "config_funcnames.h"

#include <cstring>
#include <set>
#include <string>

namespace {
struct TableSpec {
    const char *name;
    const struct NamedCommand *table;
    int64_t count;
    int64_t slots;
};

#define KFX_FUNCNAMES_SPEC(n, U) { #n, n, U##_COUNT, U##_SLOTS }
const TableSpec specs[] = {
    KFX_FUNCNAMES_SPEC(computer_process_func_type, COMPUTER_PROCESS_FUNC_TYPE),
    KFX_FUNCNAMES_SPEC(computer_check_func_type, COMPUTER_CHECK_FUNC_TYPE),
    KFX_FUNCNAMES_SPEC(computer_event_func_type, COMPUTER_EVENT_FUNC_TYPE),
    KFX_FUNCNAMES_SPEC(computer_event_test_func_type, COMPUTER_EVENT_TEST_FUNC_TYPE),
    KFX_FUNCNAMES_SPEC(creature_instances_func_type, CREATURE_INSTANCES_FUNC_TYPE),
    KFX_FUNCNAMES_SPEC(creature_instances_validate_func_type, CREATURE_INSTANCES_VALIDATE_FUNC_TYPE),
    KFX_FUNCNAMES_SPEC(creature_instances_search_targets_func_type, CREATURE_INSTANCES_SEARCH_TARGETS_FUNC_TYPE),
    KFX_FUNCNAMES_SPEC(creature_job_player_assign_func_type, CREATURE_JOB_PLAYER_ASSIGN_FUNC_TYPE),
    KFX_FUNCNAMES_SPEC(creature_job_player_check_func_type, CREATURE_JOB_PLAYER_CHECK_FUNC_TYPE),
    KFX_FUNCNAMES_SPEC(creature_job_coords_check_func_type, CREATURE_JOB_COORDS_CHECK_FUNC_TYPE),
    KFX_FUNCNAMES_SPEC(creature_job_coords_assign_func_type, CREATURE_JOB_COORDS_ASSIGN_FUNC_TYPE),
    KFX_FUNCNAMES_SPEC(process_func_commands, PROCESS_FUNC_COMMANDS),
    KFX_FUNCNAMES_SPEC(cleanup_func_commands, CLEANUP_FUNC_COMMANDS),
    KFX_FUNCNAMES_SPEC(move_from_slab_func_commands, MOVE_FROM_SLAB_FUNC_COMMANDS),
    KFX_FUNCNAMES_SPEC(move_check_func_commands, MOVE_CHECK_FUNC_COMMANDS),
};
#undef KFX_FUNCNAMES_SPEC
}

TEST_CASE("every function-name table matches its _COUNT/_SLOTS and has unique indices", "[kfx_config][config_funcnames]") {
    for (const TableSpec &spec : specs) {
        CAPTURE(spec.name);
        std::set<int64_t> indices;
        std::set<std::string> names;
        int64_t n = 0;
        while (spec.table[n].name != nullptr) {
            const struct NamedCommand &e = spec.table[n];
            CHECK(e.num >= 0);
            CHECK(e.num < spec.slots);
            CHECK(indices.insert(e.num).second);  // each slot named once
            CHECK(names.insert(e.name).second);   // each name used once
            n++;
        }
        CHECK(n == spec.count);
        CHECK(spec.table[n].num == 0); // {NULL, 0} terminator
    }
}
