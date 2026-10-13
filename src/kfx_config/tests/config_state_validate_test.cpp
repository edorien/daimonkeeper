// kfx_config: config_state_validate() -- whether a configuration that came from a save or a resync names only
// things that exist (security pass S06b).
#include <catch2/catch_test_macros.hpp>

#include "config_state_validate.h"
#include "config_funcnames.h"
#include "kfx_config_state.h"

#include <cstring>
#include <string>

namespace {
struct Zeroed {
    Zeroed() { std::memset(&kfx_config_state, 0, sizeof(kfx_config_state)); }
    static std::string why_not()
    {
        char why[256] = "";
        return config_state_validate(&kfx_config_state, why, sizeof(why)) ? std::string() : std::string(why);
    }
    static bool refused_for(const char *field) { return why_not().find(field) != std::string::npos; }
};
}

TEST_CASE_METHOD(Zeroed, "a configuration with nothing in it is acceptable", "[kfx_config][config_state_validate]") {
    CHECK(why_not().empty());
}

TEST_CASE_METHOD(Zeroed, "a count of entries stays within its table", "[kfx_config][config_state_validate]") {
    kfx_config_state.conf.crtr_conf.model_count = 128;
    CHECK(why_not().empty());
    kfx_config_state.conf.crtr_conf.model_count = 129;
    CHECK(refused_for("model_count"));
    kfx_config_state.conf.crtr_conf.model_count = -1;
    CHECK(refused_for("model_count"));
}

TEST_CASE_METHOD(Zeroed, "a function index stays within the list it selects from", "[kfx_config][config_state_validate]") {
    // found by the corruption fuzzer's reading of the code: these numbers index a list of function pointers and the
    // game calls what it finds there
    auto &state = kfx_config_state.conf.crtr_conf.states[3];
    state.process_state = PROCESS_FUNC_COMMANDS_SLOTS - 1;
    CHECK(why_not().empty());
    state.process_state = PROCESS_FUNC_COMMANDS_SLOTS;
    CHECK(refused_for("process_state"));
    state.process_state = -1;
    CHECK(refused_for("process_state"));
    state.process_state = 0;

    kfx_config_state.conf.crtr_conf.jobs[5].func_cord_assign_idx = CREATURE_JOB_COORDS_ASSIGN_FUNC_TYPE_SLOTS;
    CHECK(refused_for("func_cord_assign_idx"));
    kfx_config_state.conf.crtr_conf.jobs[5].func_cord_assign_idx = 0;

    kfx_config_state.conf.magic_conf.power_cfgstats[2].overcharge_check_idx = POWERMODEL_EXPAND_CHECK_FUNC_SLOTS;
    CHECK(refused_for("overcharge_check_idx"));
    kfx_config_state.conf.magic_conf.power_cfgstats[2].overcharge_check_idx = 0;

    kfx_config_state.conf.slab_conf.room_cfgstats[7].update_total_capacity_idx = ROOM_TOTAL_CAPACITY_FUNC_SLOTS;
    CHECK(refused_for("update_total_capacity_idx"));
    kfx_config_state.conf.slab_conf.room_cfgstats[7].update_total_capacity_idx = 0;

    kfx_config_state.conf.magic_conf.instance_info[9].search_func = CREATURE_INSTANCES_SEARCH_TARGETS_FUNC_TYPE_SLOTS;
    CHECK(refused_for("search_func"));
    kfx_config_state.conf.magic_conf.instance_info[9].search_func = 0;
    CHECK(why_not().empty());
}

TEST_CASE_METHOD(Zeroed, "an index into the script's functions is a negative number within their table", "[kfx_config][config_state_validate]") {
    auto &object = kfx_config_state.conf.object_conf.object_cfgstats[4];
    object.updatefn_idx = -(LUA_FUNCS_MAX - 1);
    CHECK(why_not().empty());
    object.updatefn_idx = -LUA_FUNCS_MAX;
    CHECK(refused_for("object_cfgstats.updatefn_idx"));
    object.updatefn_idx = OBJECT_UPDATE_FUNC_SLOTS - 1;
    CHECK(why_not().empty());
    object.updatefn_idx = OBJECT_UPDATE_FUNC_SLOTS;
    CHECK(refused_for("object_cfgstats.updatefn_idx"));
    object.updatefn_idx = 0;
    kfx_config_state.conf.trapdoor_conf.door_cfgstats[1].updatefn_idx = 5; // a door has only the script's functions
    CHECK(refused_for("door_cfgstats.updatefn_idx"));
}

TEST_CASE_METHOD(Zeroed, "the names of the script's functions end", "[kfx_config][config_state_validate]") {
    std::memset(kfx_config_state.conf.lua.lua_funcs[3], 'a', sizeof(kfx_config_state.conf.lua.lua_funcs[3]));
    CHECK(refused_for("lua_funcs"));
    kfx_config_state.conf.lua.lua_funcs[3][5] = '\0';
    CHECK(why_not().empty());
}

TEST_CASE_METHOD(Zeroed, "textures, cubes and columns stay in their tables", "[kfx_config][config_state_validate]") {
    // found by the corruption fuzzer: a cube whose texture number was 0xffff<<48
    kfx_config_state.conf.cube_conf.cube_cfgstats[5].texture_id[4] = 0xffffLL << 48;
    CHECK(refused_for("cube_cfgstats.texture_id"));
    kfx_config_state.conf.cube_conf.cube_cfgstats[5].texture_id[4] = TEXTURE_BLOCKS_COUNT - 1;
    CHECK(why_not().empty());
    kfx_config_state.conf.cube_conf.cube_bits[3][2] = CUBE_ITEMS_MAX;
    CHECK(refused_for("cube_bits"));
    kfx_config_state.conf.cube_conf.cube_bits[3][2] = 0;
    kfx_config_state.conf.column_conf.cols[100].cubes[3] = -1;
    CHECK(refused_for("column_conf.cubes"));
    kfx_config_state.conf.column_conf.cols[100].cubes[3] = 0;
    kfx_config_state.col_static_entries[4] = -480; // a column number, negated
    CHECK(why_not().empty());
    kfx_config_state.col_static_entries[4] = 480;
    CHECK(refused_for("col_static_entries"));
    kfx_config_state.col_static_entries[4] = 0;
    kfx_config_state.conf.slab_conf.slab_cfgstats[8].block_health_index = 10;
    CHECK(refused_for("block_health_index"));
}

TEST_CASE_METHOD(Zeroed, "how many samples a creature's sound has is a loop bound, and stays small", "[kfx_config][config_state_validate]") {
    // found by the corruption fuzzer (config blob): a count of 0x7fff... made playing_creature_sound() loop for ever
    kfx_config_state.conf.crtr_conf.creature_sounds[23].hit.count = 12;
    CHECK(why_not().empty());
    kfx_config_state.conf.crtr_conf.creature_sounds[23].hit.count = INT64_MAX;
    CHECK(refused_for("creature_sounds.count"));
    kfx_config_state.conf.crtr_conf.creature_sounds[23].hit.count = -3;
    CHECK(refused_for("creature_sounds.count"));
}

TEST_CASE_METHOD(Zeroed, "the survey lists every kind of violation and finds none in a good state", "[kfx_config][config_state_validate]") {
    char report[4096] = "";
    CHECK(config_state_survey(&kfx_config_state, report, sizeof(report)) == 0);
    kfx_config_state.conf.crtr_conf.model_count = 500;
    kfx_config_state.conf.cube_conf.cube_cfgstats[1].texture_id[0] = -5;
    CHECK(config_state_survey(&kfx_config_state, report, sizeof(report)) == 2);
}

TEST_CASE_METHOD(Zeroed, "a configuration loaded from files has its out-of-table values reset, and nothing else touched", "[kfx_config][config_state_validate]") {
    // a configuration file or a level's script can set any number; the level goes on with the value reset to "none"
    kfx_config_state.conf.crtr_conf.states[2].process_state = 1 << 20;
    kfx_config_state.conf.cube_conf.cube_cfgstats[5].texture_id[4] = 0xffffLL << 48;
    kfx_config_state.conf.crtr_conf.jobs[3].func_plyr_check_idx = 200; // an unsigned char
    kfx_config_state.conf.crtr_conf.states[2].move_check = 4;           // a valid one
    kfx_config_state.conf.magic_conf.power_cfgstats[1].overcharge_check_idx = 3;
    CHECK(config_state_repair(&kfx_config_state) == 3);
    CHECK(kfx_config_state.conf.crtr_conf.states[2].process_state == 0);
    CHECK(kfx_config_state.conf.cube_conf.cube_cfgstats[5].texture_id[4] == 0);
    CHECK(kfx_config_state.conf.crtr_conf.jobs[3].func_plyr_check_idx == 0);
    CHECK(kfx_config_state.conf.crtr_conf.states[2].move_check == 4);
    CHECK(kfx_config_state.conf.magic_conf.power_cfgstats[1].overcharge_check_idx == 3);
    CHECK(why_not().empty());
    CHECK(config_state_repair(&kfx_config_state) == 0);
}

#include "config_slabsets.h"
#include "config_strings.h"
#include "config_campaigns.h"

// Security pass S08: a map's column file holds 16-bit numbers that index the texture and cube tables.
TEST_CASE("a column read from a map file keeps its texture and cubes inside the tables", "[kfx_config][config_state_validate][security]") {
    struct LegacyColumn legacy;
    std::memset(&legacy, 0, sizeof(legacy));
    legacy.floor_texture = 0xFFFF;
    for (int i = 0; i < COLUMN_STACK_HEIGHT; i++)
        legacy.cubes[i] = (uint16_t)(0xFF00 + i);
    legacy.cubes[2] = 1023;
    struct Column col;
    column_from_legacy(&col, &legacy);
    CHECK(col.floor_texture == 0);
    CHECK(col.cubes[0] == 0);
    CHECK(col.cubes[2] == 1023);
    legacy.floor_texture = 1543;
    column_from_legacy(&col, &legacy);
    CHECK(col.floor_texture == 1543);
}

TEST_CASE("a campaign string asked for before the campaign's strings are set up is the default one", "[kfx_config][config_state_validate][security]") {
    std::memset(&campaign, 0, sizeof(campaign)); // every campaign.strings[] entry NULL
    CHECK(cmpgn_string(5) != nullptr);
}
