// kfx_sim "room" cluster depth increment, per docs/refactor/testing/
// comprehensive/stage-08b-kfx-sim-clusters.md's "still open" list:
// room_lair.c's creature_model_is_lair_enemy/_is_hostile_towards (pure
// array-membership checks, no state at all) and calculate_free_lair_space,
// which -- like creature_states_rsrch.c's get_next_research_item -- reaches
// through kfx_sim_state's room/thing/creature-control linked lists but
// takes its Dungeon by pointer, so a local zeroed Dungeon works fine (no
// need for a real kfx_sim_state.dungeon[] slot). The things keep the
// zeroed thing->model 0, which creature_stats_get_from_thing resolves to
// model[0].
#include <catch2/catch_test_macros.hpp>

#include "room_lair.h"
#include "room_data.h"
#include "dungeon_data.h"
#include "thing_data.h"
#include "creature_control.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "thing_stats.h"

#include <cstring>

namespace {
struct ResetState {
    struct Dungeon dungeon{};

    ResetState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        std::memset(&dungeon, 0, sizeof(dungeon));
    }
};
}

TEST_CASE("creature_model_is_lair_enemy finds a matching model, or not", "[kfx_sim][room_lair]") {
    ThingModel lair_enemy[CREATURE_TYPES_MAX] = {0};
    lair_enemy[2] = 7;

    CHECK(creature_model_is_lair_enemy(lair_enemy, 7));
    CHECK_FALSE(creature_model_is_lair_enemy(lair_enemy, 8));
}

TEST_CASE("creature_model_is_hostile_towards finds a matching model, or not", "[kfx_sim][room_lair]") {
    ThingModel hostile_towards[CREATURE_TYPES_MAX] = {0};
    hostile_towards[5] = 3;

    CHECK(creature_model_is_hostile_towards(hostile_towards, 3));
    CHECK_FALSE(creature_model_is_hostile_towards(hostile_towards, 4));
}

TEST_CASE_METHOD(ResetState, "calculate_free_lair_space subtracts used room capacity and pending creature lair needs from total capacity", "[kfx_sim][room_lair]") {
    kfx_config_state.conf.slab_conf.room_types_count = RoK_LAIR + 1;
    kfx_config_state.conf.slab_conf.room_cfgstats[RoK_LAIR].roles = RoRoF_LairStorage;
    kfx_config_state.conf.crtr_conf.model_count = 1;
    kfx_config_state.conf.crtr_conf.model[0].lair_size = 20;

    struct Room *room = room_get(1);
    room->total_capacity = 100;
    room->used_capacity = 30;
    room->next_of_owner = 0; // end of the per-kind room list
    dungeon.room_list_start[RoK_LAIR] = 1;

    struct Thing *creatng = thing_get(2);
    creatng->ccontrol_idx = 1;
    struct CreatureControl *cctrl = creature_control_get(1);
    cctrl->lair_room_id = 0; // needs a lair -- counts toward cap_required
    cctrl->players_next_creature_idx = 0; // end of the creature list
    dungeon.creatr_list_start = 2;

    CHECK(calculate_free_lair_space(&dungeon) == 50); // 100 - 30 - 20
}

TEST_CASE_METHOD(ResetState, "calculate_free_lair_space doesn't count a creature that already has a lair", "[kfx_sim][room_lair]") {
    kfx_config_state.conf.slab_conf.room_types_count = RoK_LAIR + 1;
    kfx_config_state.conf.slab_conf.room_cfgstats[RoK_LAIR].roles = RoRoF_LairStorage;
    kfx_config_state.conf.crtr_conf.model_count = 1;
    kfx_config_state.conf.crtr_conf.model[0].lair_size = 20;

    struct Room *room = room_get(1);
    room->total_capacity = 100;
    room->used_capacity = 30;
    room->next_of_owner = 0;
    dungeon.room_list_start[RoK_LAIR] = 1;

    struct Thing *creatng = thing_get(2);
    creatng->ccontrol_idx = 1;
    struct CreatureControl *cctrl = creature_control_get(1);
    cctrl->lair_room_id = 5; // already has a lair -- excluded from cap_required
    cctrl->players_next_creature_idx = 0;
    dungeon.creatr_list_start = 2;

    CHECK(calculate_free_lair_space(&dungeon) == 70); // 100 - 30 - 0
}
