// kfx_sim "creature" cluster, hard-tail depth increment: creature_jobs.c
// was at 0% coverage. Most of the file is the anger-job/state-machine
// dispatch that needs a full Room+navigation fixture, but the job-
// eligibility predicates are pattern A/B: creature_has_job/_dislikes_job
// reach through creature_stats_get_from_thing (default model-0 provider,
// the same reliance used throughout this plan), and
// is_correct_owner_to_perform_job reaches through
// creature_kind_is_for_dungeon_diggers_list (direct by-model config, like
// thing_list_test.cpp's creature_model_matches_model) and get_flags_for_job
// (get_config_for_job's bit-position index into kfx_config_state.conf.crtr_conf.jobs[]
// -- job value 1 resolves to index 1, confirmed by tracing the
// while(k){k>>=1;i++;} loop by hand: k=1 -> one shift -> i=1).
// is_correct_position_to_perform_job is covered only on its
// no-room-role-required branch (RoRoF_None, the default zeroed job_role);
// the room-role-matching branch needs the map+slabmap+room fixture
// room_garden_test.cpp introduced and is left for a later increment.
#include <catch2/catch_test_macros.hpp>

#include "creature_jobs.h"
#include "creature_control.h"
#include "thing_data.h"
#include "player_data.h"
#include "dungeon_data.h"
#include "slab_data.h"
#include "map_events.h"
#include "config_creature.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "thing_stats.h"

#include <cstring>

namespace {
struct ResetState {
    ResetState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        kfx_sim_state.map_tiles_x = 4;
        kfx_sim_state.map_tiles_y = 4;
        kfx_sim_state.map_subtiles_x = 10;
        kfx_sim_state.map_subtiles_y = 10;
    }
};

// CreatureJob value 1 (bit 0) resolves to kfx_config_state.conf.crtr_conf.jobs[1]
// via get_config_for_job's bit-position index -- not a real named Job_*
// constant, just a minimal bitmask that lands on a predictable, easy
// index.
constexpr CreatureJob kTestJob = 1;
}

TEST_CASE_METHOD(ResetState, "creature_can_do_job_always_for_player is unconditionally true", "[kfx_sim][creature_jobs]") {
    struct Thing *thing = thing_get(1);
    CHECK(creature_can_do_job_always_for_player(thing, 0, kTestJob));
}

TEST_CASE_METHOD(ResetState, "creature_has_job checks both job_primary and job_secondary bitmasks", "[kfx_sim][creature_jobs]") {
    kfx_config_state.conf.crtr_conf.model[0].job_primary = 0x02;
    kfx_config_state.conf.crtr_conf.model[0].job_secondary = 0x04;
    struct Thing *thing = thing_get(1);

    CHECK(creature_has_job(thing, 0x02));
    CHECK(creature_has_job(thing, 0x04));
    CHECK_FALSE(creature_has_job(thing, 0x08));
}

TEST_CASE_METHOD(ResetState, "creature_dislikes_job reads jobs_not_do", "[kfx_sim][creature_jobs]") {
    kfx_config_state.conf.crtr_conf.model[0].jobs_not_do = 0x02;
    struct Thing *thing = thing_get(1);

    CHECK(creature_dislikes_job(thing, 0x02));
    CHECK_FALSE(creature_dislikes_job(thing, 0x04));
}

TEST_CASE_METHOD(ResetState, "creature_will_reject_job is overridden by an obeying player unless ClscBug_MustObeyKeepsNotDoJobs is set", "[kfx_sim][creature_jobs]") {
    kfx_config_state.conf.crtr_conf.model[0].jobs_not_do = 0x02;
    struct Thing *thing = thing_get(1);
    thing->owner = 0;

    CHECK(creature_will_reject_job(thing, 0x02)); // no obey-power in effect -- normal rejection

    struct PlayerInfo *player = get_player(0);
    player->allocflags = PlaF_Allocated;
    get_dungeon(0)->must_obey_turn = 5; // player_uses_power_obey() now true

    CHECK_FALSE(creature_will_reject_job(thing, 0x02)); // obeying overrides the dislike

    kfx_config_state.conf.rules[0].gameplay.classic_bugs_flags = ClscBug_MustObeyKeepsNotDoJobs;
    CHECK(creature_will_reject_job(thing, 0x02)); // classic-bugs flag restores the rejection
}

TEST_CASE_METHOD(ResetState, "set_creature_assigned_job writes job_assigned via the thing's CreatureControl", "[kfx_sim][creature_jobs]") {
    struct Thing *thing = thing_get(1);
    thing->ccontrol_idx = 0; // no CreatureControl assigned
    CHECK_FALSE(set_creature_assigned_job(thing, kTestJob));

    thing->ccontrol_idx = 1;
    CHECK(set_creature_assigned_job(thing, kTestJob));
    CHECK(creature_control_get(1)->job_assigned == kTestJob);
}

TEST_CASE_METHOD(ResetState, "is_correct_owner_to_perform_job picks the owned/enemy x digger/creature job_flags gate", "[kfx_sim][creature_jobs]") {
    kfx_config_state.conf.crtr_conf.jobs_count = 2;
    struct Thing *creatng = thing_get(1);
    creatng->owner = 0;
    creatng->model = 3; // not CREATURE_DIGGER, no CMF_IsSpecDigger flag -- a regular creature

    kfx_config_state.conf.crtr_conf.jobs[1].job_flags = JoKF_OwnedCreatures;
    CHECK(is_correct_owner_to_perform_job(creatng, 0, kTestJob)); // own creature, matching flag
    kfx_config_state.conf.crtr_conf.jobs[1].job_flags = JoKF_OwnedDiggers; // wrong flag for a non-digger
    CHECK_FALSE(is_correct_owner_to_perform_job(creatng, 0, kTestJob));

    kfx_config_state.conf.crtr_conf.jobs[1].job_flags = JoKF_EnemyCreatures;
    CHECK(is_correct_owner_to_perform_job(creatng, 3, kTestJob)); // different plyr_idx -- enemy branch

    creatng->model = CREATURE_DIGGER;
    kfx_config_state.conf.crtr_conf.jobs[1].job_flags = JoKF_OwnedDiggers;
    CHECK(is_correct_owner_to_perform_job(creatng, 0, kTestJob)); // own digger, matching flag
}

TEST_CASE_METHOD(ResetState, "is_correct_position_to_perform_job skips the room-role check for RoRoF_None jobs and delegates to the owner check", "[kfx_sim][creature_jobs]") {
    kfx_config_state.conf.crtr_conf.jobs_count = 2; // jobs[1].room_role defaults to RoRoF_None
    kfx_config_state.conf.crtr_conf.jobs[1].job_flags = JoKF_OwnedCreatures;
    struct Thing *creatng = thing_get(1);
    creatng->owner = 0;
    creatng->model = 3;

    struct SlabMap *slb = get_slabmap_block(0, 0);
    slb->owner = 0; // matches creatng's owner

    CHECK(is_correct_position_to_perform_job(creatng, 0, 0, kTestJob));

    slb->owner = 3; // now an "enemy" slab, but job_flags only allows JoKF_OwnedCreatures
    CHECK_FALSE(is_correct_position_to_perform_job(creatng, 0, 0, kTestJob));
}

// The "_for_player" wrapper cluster: each is a thin composition over an
// already-tested "can this creature do X at all" predicate (or, for
// barracking/join-fight, a direct dungeon/event-array check). None of
// these dereference a CreatureControl, so a bare thing_get() slot
// (this file's existing convention) is enough -- no make_creature needed.

TEST_CASE_METHOD(ResetState, "creature_can_do_research_for_player delegates to creature_can_do_research", "[kfx_sim][creature_jobs]") {
    // ResetState (unlike kfx_sim_test_fixtures.h's ResetSimAndConfig) leaves
    // neutral_player_num at its zeroed default, which collides with owner 0
    // and would make is_neutral_thing() true for the test creature -- set it
    // explicitly so creature_can_do_research's real research_value/
    // current_research_idx checks are what's actually being exercised.
    kfx_config_state.neutral_player_num = PLAYER_NEUTRAL;
    struct Thing *creatng = thing_get(1);
    creatng->owner = 0;
    CHECK_FALSE(creature_can_do_research_for_player(creatng, 0, kTestJob)); // research_value defaults to 0

    kfx_config_state.conf.crtr_conf.model[0].research_value = 1; // creature_stats_get_from_thing() always resolves to model[0]
    CHECK(creature_can_do_research_for_player(creatng, 0, kTestJob)); // current_research_idx defaults to 0 (>= 0)
}

TEST_CASE_METHOD(ResetState, "creature_can_do_manufacturing_for_player delegates to creature_can_do_manufacturing", "[kfx_sim][creature_jobs]") {
    kfx_config_state.neutral_player_num = PLAYER_NEUTRAL; // see the note in the research_for_player test above
    struct Thing *creatng = thing_get(1);
    creatng->owner = 0;
    CHECK_FALSE(creature_can_do_manufacturing_for_player(creatng, 0, kTestJob)); // manufacture_value defaults to 0
}

TEST_CASE_METHOD(ResetState, "creature_can_join_fight_for_player checks for an EnemyFight or HeartAttacked event owned by the player", "[kfx_sim][creature_jobs]") {
    struct Thing *creatng = thing_get(1);
    creatng->owner = 0;
    CHECK_FALSE(creature_can_join_fight_for_player(creatng, 0, kTestJob));

    kfx_sim_state.event[1].flags |= EvF_Exists;
    kfx_sim_state.event[1].owner = 0;
    kfx_sim_state.event[1].kind = EvKind_HeartAttacked;
    CHECK(creature_can_join_fight_for_player(creatng, 0, kTestJob));

    kfx_sim_state.event[1].owner = 1; // wrong owner
    CHECK_FALSE(creature_can_join_fight_for_player(creatng, 0, kTestJob));

    kfx_sim_state.event[1].owner = 0;
    kfx_sim_state.event[1].kind = EvKind_EnemyFight;
    CHECK(creature_can_join_fight_for_player(creatng, 0, kTestJob));
}

TEST_CASE_METHOD(ResetState, "creature_can_do_barracking_for_player requires more than one active creature in the player's dungeon", "[kfx_sim][creature_jobs]") {
    struct Thing *creatng = thing_get(1);
    get_dungeon(0)->num_active_creatrs = 1;
    CHECK_FALSE(creature_can_do_barracking_for_player(creatng, 0, kTestJob));

    get_dungeon(0)->num_active_creatrs = 2;
    CHECK(creature_can_do_barracking_for_player(creatng, 0, kTestJob));
}
