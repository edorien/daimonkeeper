// kfx_config: config_creature.c has ~40 public functions but no
// existing worked example to mirror directly -- this file prioritizes
// breadth (every reachable public function gets at least one real
// assertion) over exhaustively covering every struct field, following
// this library's established "document what's skipped and why" pattern
// where full coverage isn't practical.
//
// load_creaturetypes_config_file() (keeper_creaturetp_file_data.load_func)
// is NOT exercised against a fixture here: it parses model/jobs/instances/
// angerjobs/attacktypes/experience/sounds/sprite/annoyance/senses/
// appearance blocks in one hand-rolled pass (config_creature.c is 2627
// lines), and every accessor this file actually tests is exercised more
// directly and more legibly by writing straight into
// kfx_config_state.conf.crtr_conf/creature_desc/breed_activities instead
// -- the same "skip the big loader, test the accessors directly" choice
// config_magic_test.cpp made for its player-power-availability functions.
// Only the loader's missing-file/hooks behavior is checked here.
//
// get_job_for_room()/get_job_which_qualify_for_room()/
// get_jobs_enemies_may_do_in_room() are thin RoomKind->RoomRole wrappers
// around the _role siblings this file already tests directly; they're
// given their own real (non-trivial) coverage below by writing a room
// role into kfx_config_state.conf.slab_conf.room_cfgstats[], the same
// struct config_terrain.c's get_room_roles()/get_room_kind_stats() read.
//
// The Thing-taking functions (creature_stats_get_from_thing,
// get_creature_model_flags, creature_own_name, get_job_for_subtile) moved
// to kfx_sim in refactor pass 2 (S05); their tests moved with them.
#include <catch2/catch_test_macros.hpp>

#include "config_creature.h"
#include "config_terrain.h" // RoRoF_*, RoomRole
#include "config_strings.h" // TRANSLATION_STRINGS_START
#include "kfx_config_state.h"

#include <cstring>

namespace {
struct ResetAll {
    ResetAll() {
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        std::memset(creature_desc, 0, sizeof(struct NamedCommand) * CREATURE_TYPES_MAX);
        std::memset(creaturejob_desc, 0, sizeof(struct NamedCommand) * INSTANCE_TYPES_MAX);
        std::memset(instance_desc, 0, sizeof(struct NamedCommand) * INSTANCE_TYPES_MAX);
        std::memset(breed_activities, 0, sizeof(breed_activities));
    }
};
}

TEST_CASE_METHOD(ResetAll, "creature_stats_get falls back to slot 0 for index 0 or any out-of-range index", "[kfx_config][config_creature]") {
    kfx_config_state.conf.crtr_conf.model[1].health = 42;
    CHECK(creature_stats_get(1)->health == 42);
    CHECK(creature_stats_get(0) == creature_stats_get(-1));
    CHECK(creature_stats_get(0) == creature_stats_get(CREATURE_TYPES_MAX + 1));
}

TEST_CASE_METHOD(ResetAll, "creature_stats_invalid rejects slot 0 and NULL, accepting anything past it", "[kfx_config][config_creature]") {
    CHECK(creature_stats_invalid(creature_stats_get(0)));
    CHECK(creature_stats_invalid(nullptr));
    CHECK_FALSE(creature_stats_invalid(creature_stats_get(0) + 1));
}

TEST_CASE_METHOD(ResetAll, "init_creature_model_stats resets a model to its documented defaults", "[kfx_config][config_creature]") {
    struct CreatureModelConfig *crconf = creature_stats_get(1);
    crconf->health = 999;
    init_creature_model_stats(1);
    CHECK(crconf->health == 100);
    CHECK(crconf->base_speed == 32);
    CHECK(crconf->bleeds == true);
    CHECK(crconf->humanoid_creature == true);
    CHECK(crconf->flying == false);
    CHECK(crconf->natural_death_kind == Death_Normal);
}

TEST_CASE_METHOD(ResetAll, "init_all_creature_model_stats resets every model slot", "[kfx_config][config_creature]") {
    init_all_creature_model_stats();
    CHECK(creature_stats_get(1)->health == 100);
    CHECK(creature_stats_get(CREATURE_TYPES_MAX - 1)->health == 100);
}

TEST_CASE_METHOD(ResetAll, "check_and_auto_fix_stats zeroes heal_requirement for a creature with no lair and no toking recovery", "[kfx_config][config_creature]") {
    kfx_config_state.conf.crtr_conf.model_count = 2;
    struct CreatureModelConfig *crconf = creature_stats_get(1);
    crconf->lair_size = 0;
    crconf->toking_recovery = 0;
    crconf->heal_requirement = 5;
    crconf->heal_threshold = 5; // kept equal so the heal_threshold check below doesn't also fire

    check_and_auto_fix_stats();
    CHECK(crconf->heal_requirement == 0);
}

TEST_CASE_METHOD(ResetAll, "check_and_auto_fix_stats raises heal_threshold to match a higher heal_requirement", "[kfx_config][config_creature]") {
    kfx_config_state.conf.crtr_conf.model_count = 2;
    struct CreatureModelConfig *crconf = creature_stats_get(1);
    crconf->lair_size = 1; // has a lair, so heal_requirement itself isn't touched first
    crconf->heal_requirement = 5;
    crconf->heal_threshold = 3;

    check_and_auto_fix_stats();
    CHECK(crconf->heal_requirement == 5);
    CHECK(crconf->heal_threshold == 5);
}

TEST_CASE_METHOD(ResetAll, "check_and_auto_fix_stats fixes hunger_fill/grow_up/rebirth violations", "[kfx_config][config_creature]") {
    kfx_config_state.conf.crtr_conf.model_count = 2;
    struct CreatureModelConfig *crconf = creature_stats_get(1);

    crconf->hunger_rate = 1;
    crconf->hunger_fill = 0;      // hunger without any fill -> fixed to 1
    crconf->grow_up = 50;         // >= model_count and not CREATURE_NOT_A_DIGGER -> fixed to 0
    crconf->grow_up_level = 0;
    crconf->rebirth = CREATURE_MAX_LEVEL + 1; // fixed to 0

    check_and_auto_fix_stats();

    CHECK(crconf->hunger_fill == 1);
    CHECK(crconf->grow_up == 0);
    CHECK(crconf->rebirth == 0);
}

TEST_CASE_METHOD(ResetAll, "check_and_auto_fix_stats fixes an out-of-range grow_up_level and learned_instance_level violations", "[kfx_config][config_creature]") {
    kfx_config_state.conf.crtr_conf.model_count = 2;
    struct CreatureModelConfig *crconf = creature_stats_get(1);

    crconf->grow_up = 1; // > 0, so grow_up_level is checked
    crconf->grow_up_level = CREATURE_MAX_LEVEL + 5;

    crconf->learned_instance_id[0] = 7;  // a real instance with an invalid level -> fixed to 1
    crconf->learned_instance_level[0] = 0;
    crconf->learned_instance_id[1] = 0;  // an empty slot with a stray level -> fixed to 0
    crconf->learned_instance_level[1] = 3;

    check_and_auto_fix_stats();

    CHECK(crconf->grow_up_level == CREATURE_MAX_LEVEL);
    CHECK(crconf->learned_instance_level[0] == 1);
    CHECK(crconf->learned_instance_level[1] == 0);
}

TEST_CASE_METHOD(ResetAll, "init_creature_model_graphics sets every graphics slot to -1", "[kfx_config][config_creature]") {
    init_creature_model_graphics();
    CHECK(kfx_config_state.conf.crtr_conf.creature_graphics[0][0] == -1);
    CHECK(kfx_config_state.conf.crtr_conf.creature_graphics[3][7] == -1);
}

TEST_CASE("is_creature_model_wildcard is true for CREATURE_ANY/CREATURE_NOT_A_DIGGER/CREATURE_DIGGER only", "[kfx_config][config_creature]") {
    CHECK(is_creature_model_wildcard(CREATURE_ANY));
    CHECK(is_creature_model_wildcard(CREATURE_NOT_A_DIGGER));
    CHECK(is_creature_model_wildcard(CREATURE_DIGGER));
    CHECK_FALSE(is_creature_model_wildcard(5));
}

TEST_CASE_METHOD(ResetAll, "creature_code_name round-trips a name set in creature_desc, falling back to INVALID", "[kfx_config][config_creature]") {
    // parse_creaturetypes_common_blocks() (the real loader) always writes
    // creature_desc[model_index - 1] for a creature stored at
    // model[model_index] -- creature_desc is offset by one relative to
    // model[]/creature_stats_get()'s own 1-based indexing (model[0] is
    // the reserved "NOCREATURE" sentinel). .num still carries the real
    // model index, so creature_code_name(1) looks up creature_desc[0]
    // via get_conf_parameter_text(), not creature_desc[1].
    std::strcpy(creature_stats_get(1)->name, "TEST_CREATURE");
    creature_desc[0].name = creature_stats_get(1)->name;
    creature_desc[0].num = 1;

    CHECK(std::strcmp(creature_code_name(1), "TEST_CREATURE") == 0);
    CHECK(std::strcmp(creature_code_name(5), "INVALID") == 0);
}

TEST_CASE_METHOD(ResetAll, "creature_model_id returns the model[] index a name was found at, matching creature_stats_get()'s own 1-based convention", "[kfx_config][config_creature]") {
    kfx_config_state.conf.crtr_conf.model_count = 2;
    std::strcpy(creature_stats_get(1)->name, "TEST_CREATURE");

    // Previously returned the loop index `i` plus one (a name found at
    // model[1] returned 2), one past the model[] slot it actually found --
    // a real, then-dormant bug (nothing in this codebase called
    // creature_model_id at the time). src/ftests/tests/
    // ftest_creature_temple_prayer.c became the first real caller and hit
    // it directly: creature_model_id("ORC") resolved to FLOATING_SPIRIT's
    // model index instead of ORC's. Fixed to return the bare loop index,
    // consistent with every other 1-based accessor here
    // (creature_stats_get(), creature_desc's .num field) treating model[1]
    // as "creature 1".
    CHECK(creature_model_id("TEST_CREATURE") == 1);
    CHECK(creature_model_id("NOT_A_REAL_CREATURE") == -1);
}

TEST_CASE_METHOD(ResetAll, "parse_creature_name resolves a real creature_desc name, or ANY_CREATURE to CREATURE_NOT_A_DIGGER, or -1 for unknown", "[kfx_config][config_creature]") {
    std::strcpy(creature_stats_get(1)->name, "TEST_CREATURE");
    creature_desc[0].name = creature_stats_get(1)->name;
    creature_desc[0].num = 1;

    CHECK(parse_creature_name("TEST_CREATURE") == 1);
    CHECK(parse_creature_name("ANY_CREATURE") == CREATURE_NOT_A_DIGGER);
    CHECK(parse_creature_name("NOT_A_REAL_CREATURE") == -1);
}

TEST_CASE_METHOD(ResetAll, "set_creature_model_graphics writes a slot, refusing an invalid sequence index or model", "[kfx_config][config_creature]") {
    kfx_config_state.conf.crtr_conf.model_count = 2;

    set_creature_model_graphics(1, 3, 777);
    CHECK(kfx_config_state.conf.crtr_conf.creature_graphics[1][3] == 777);

    set_creature_model_graphics(1, CREATURE_GRAPHICS_INSTANCES, 111); // out-of-range seq_idx -> no-op
    CHECK(kfx_config_state.conf.crtr_conf.creature_graphics[1][3] == 777);

    set_creature_model_graphics(5, 0, 111); // out-of-range model -> no-op
    CHECK(kfx_config_state.conf.crtr_conf.creature_graphics[5][0] == 0);
}

TEST_CASE_METHOD(ResetAll, "get_creature_model_with_model_flags finds the first model with all the needed flags set, or 0 if none match", "[kfx_config][config_creature]") {
    kfx_config_state.conf.crtr_conf.model_count = 3;
    creature_stats_get(2)->model_flags = CMF_IsEvil | CMF_Insect;

    CHECK(get_creature_model_with_model_flags(CMF_IsEvil) == 2);
    CHECK(get_creature_model_with_model_flags(CMF_IsArachnid) == 0);
}

TEST_CASE_METHOD(ResetAll, "get_config_for_instance falls back to slot 0 for an out-of-range instance; creature_instance_code_name round-trips a set name", "[kfx_config][config_creature]") {
    kfx_config_state.conf.crtr_conf.instances_count = 2;
    std::strcpy(kfx_config_state.conf.crtr_conf.instances[1].name, "TEST_INSTANCE");

    CHECK(get_config_for_instance(1) == &kfx_config_state.conf.crtr_conf.instances[1]);
    CHECK(get_config_for_instance(-1) == get_config_for_instance(0));
    CHECK(get_config_for_instance(5) == get_config_for_instance(0));
    CHECK(std::strcmp(creature_instance_code_name(1), "TEST_INSTANCE") == 0);
    CHECK(std::strcmp(creature_instance_code_name(0), "INVALID") == 0);
}

namespace {
// jobs[i] (i >= 1) maps to bit (i-1) of CreatureJob -- jobs[1] is
// Job_TUNNEL (bit 0). Shared by every job-lookup test below.
void setup_test_job() {
    kfx_config_state.conf.crtr_conf.jobs_count = 2;
    struct CreatureJobConfig *jobcfg = &kfx_config_state.conf.crtr_conf.jobs[1];
    std::strcpy(jobcfg->name, "TEST_JOB");
    jobcfg->room_role = RoRoF_LairStorage;
    jobcfg->event_kind = 3;
    jobcfg->initial_crstate = CrSt_CreatureSleep;
    jobcfg->continue_crstate = CrSt_CreatureWantsAHome;
    jobcfg->job_flags = JoKF_OwnedCreatures;
}
}

TEST_CASE_METHOD(ResetAll, "get_config_for_job maps a single job bit to jobs[bit_index+1], falling back to slot 0 out of range", "[kfx_config][config_creature]") {
    setup_test_job();
    CHECK(get_config_for_job(Job_TUNNEL) == &kfx_config_state.conf.crtr_conf.jobs[1]);
    CHECK(get_config_for_job(Job_NULL) == &kfx_config_state.conf.crtr_conf.jobs[0]);
    CHECK(get_config_for_job(Job_MAD_PSYCHO) == &kfx_config_state.conf.crtr_conf.jobs[0]); // far past jobs_count
}

TEST_CASE_METHOD(ResetAll, "get_room_role_for_job/get_event_for_job/get_initial_state_for_job/get_arrive_at_state_for_job/get_continue_state_for_job/get_flags_for_job/creature_job_code_name read the matching job's fields", "[kfx_config][config_creature]") {
    setup_test_job();
    CHECK(get_room_role_for_job(Job_TUNNEL) == RoRoF_LairStorage);
    CHECK(get_event_for_job(Job_TUNNEL) == 3);
    CHECK(get_initial_state_for_job(Job_TUNNEL) == CrSt_CreatureSleep);
    CHECK(get_arrive_at_state_for_job(Job_TUNNEL) == CrSt_CreatureSleep);
    CHECK(get_continue_state_for_job(Job_TUNNEL) == CrSt_CreatureWantsAHome);
    CHECK(get_flags_for_job(Job_TUNNEL) == JoKF_OwnedCreatures);
    CHECK(std::strcmp(creature_job_code_name(Job_TUNNEL), "TEST_JOB") == 0);
    CHECK(std::strcmp(creature_job_code_name(Job_NULL), "INVALID") == 0);
}

TEST_CASE_METHOD(ResetAll, "get_required_room_capacity_for_job returns lair_size for lair/heal-sleep roles, 0 for RoRoF_None, and 0/1 by JoKF_NeedsCapacity otherwise", "[kfx_config][config_creature]") {
    setup_test_job(); // room_role = RoRoF_LairStorage
    kfx_config_state.conf.crtr_conf.model_count = 2;
    creature_stats_get(1)->lair_size = 6;
    CHECK(get_required_room_capacity_for_job(Job_TUNNEL, 1) == 6);

    struct CreatureJobConfig *jobcfg = &kfx_config_state.conf.crtr_conf.jobs[1];
    jobcfg->room_role = RoRoF_None;
    CHECK(get_required_room_capacity_for_job(Job_TUNNEL, 1) == 0);

    jobcfg->room_role = RoRoF_CratesStorage;
    jobcfg->job_flags = 0;
    CHECK(get_required_room_capacity_for_job(Job_TUNNEL, 1) == 0);
    jobcfg->job_flags = JoKF_NeedsCapacity;
    CHECK(get_required_room_capacity_for_job(Job_TUNNEL, 1) == 1);
}

TEST_CASE_METHOD(ResetAll, "get_job_for_room_role matches a configured job's room_role and required flags, honoring JoKF_NeedsHaveJob", "[kfx_config][config_creature]") {
    setup_test_job(); // job_flags = JoKF_OwnedCreatures, room_role = RoRoF_LairStorage

    CHECK(get_job_for_room_role(RoRoF_LairStorage, JoKF_OwnedCreatures, Job_NULL) == Job_TUNNEL);
    CHECK(get_job_for_room_role(RoRoF_CratesStorage, JoKF_OwnedCreatures, Job_NULL) == Job_NULL); // role doesn't match
    CHECK(get_job_for_room_role(RoRoF_None, JoKF_OwnedCreatures, Job_NULL) == Job_NULL); // rrole == 0 short-circuits

    kfx_config_state.conf.crtr_conf.jobs[1].job_flags |= JoKF_NeedsHaveJob;
    CHECK(get_job_for_room_role(RoRoF_LairStorage, JoKF_OwnedCreatures, Job_NULL) == Job_NULL); // has_jobs doesn't include it
    CHECK(get_job_for_room_role(RoRoF_LairStorage, JoKF_OwnedCreatures, Job_TUNNEL) == Job_TUNNEL);
}

TEST_CASE_METHOD(ResetAll, "get_job_which_qualify_for_room_role matches by qualify_flags/prevent_flags/room_role, returning Job_NULL for RoRoF_None", "[kfx_config][config_creature]") {
    setup_test_job(); // job_flags = JoKF_OwnedCreatures, room_role = RoRoF_LairStorage

    CHECK(get_job_which_qualify_for_room_role(RoRoF_LairStorage, JoKF_OwnedCreatures, 0) == Job_TUNNEL);
    CHECK(get_job_which_qualify_for_room_role(RoRoF_LairStorage, JoKF_OwnedCreatures, JoKF_OwnedCreatures) == Job_NULL); // prevented
    CHECK(get_job_which_qualify_for_room_role(RoRoF_None, JoKF_OwnedCreatures, 0) == Job_NULL);
}

TEST_CASE_METHOD(ResetAll, "get_jobs_enemies_may_do_in_room_role ORs in every job whose room_role matches and which allows enemy creatures/diggers", "[kfx_config][config_creature]") {
    setup_test_job(); // room_role = RoRoF_LairStorage, job_flags = JoKF_OwnedCreatures (no enemy flag)
    CHECK(get_jobs_enemies_may_do_in_room_role(RoRoF_LairStorage) == Job_NULL);

    kfx_config_state.conf.crtr_conf.jobs[1].job_flags = JoKF_EnemyCreatures;
    CHECK(get_jobs_enemies_may_do_in_room_role(RoRoF_LairStorage) == Job_TUNNEL);
}

namespace {
// get_job_for_room()/get_job_which_qualify_for_room()/
// get_jobs_enemies_may_do_in_room() are RoomKind->RoomRole wrappers via
// config_terrain.c's get_room_roles(), which reads
// kfx_config_state.conf.slab_conf.room_cfgstats[rkind].roles.
void setup_test_room(RoomKind rkind, RoomRole roles) {
    kfx_config_state.conf.slab_conf.room_types_count = rkind + 1;
    kfx_config_state.conf.slab_conf.room_cfgstats[rkind].roles = roles;
}
}

TEST_CASE_METHOD(ResetAll, "get_job_for_room resolves the room kind's role via get_room_roles() and matches a job the same way get_job_for_room_role does", "[kfx_config][config_creature]") {
    setup_test_job(); // room_role = RoRoF_LairStorage, job_flags = JoKF_OwnedCreatures
    setup_test_room(2, RoRoF_LairStorage);

    CHECK(get_job_for_room(2, JoKF_OwnedCreatures, Job_NULL) == Job_TUNNEL);
    CHECK(get_job_for_room(0, JoKF_OwnedCreatures, Job_NULL) == Job_NULL); // room 0's role defaults to RoRoF_None
}

TEST_CASE_METHOD(ResetAll, "get_job_which_qualify_for_room resolves the room kind's role via get_room_roles() and matches a job the same way get_job_which_qualify_for_room_role does", "[kfx_config][config_creature]") {
    setup_test_job(); // room_role = RoRoF_LairStorage, job_flags = JoKF_OwnedCreatures
    setup_test_room(2, RoRoF_LairStorage);

    CHECK(get_job_which_qualify_for_room(2, JoKF_OwnedCreatures, 0) == Job_TUNNEL);
    CHECK(get_job_which_qualify_for_room(2, JoKF_OwnedCreatures, JoKF_OwnedCreatures) == Job_NULL); // prevented
}

TEST_CASE_METHOD(ResetAll, "get_jobs_enemies_may_do_in_room resolves the room kind's role via get_room_roles() the same way get_jobs_enemies_may_do_in_room_role does", "[kfx_config][config_creature]") {
    setup_test_job(); // room_role = RoRoF_LairStorage
    kfx_config_state.conf.crtr_conf.jobs[1].job_flags = JoKF_EnemyDiggers;
    setup_test_room(2, RoRoF_LairStorage);

    CHECK(get_jobs_enemies_may_do_in_room(2) == Job_TUNNEL);
    CHECK(get_jobs_enemies_may_do_in_room(0) == Job_NULL); // room 0's role defaults to RoRoF_None
}

TEST_CASE_METHOD(ResetAll, "get_job_for_creature_state matches a configured job's initial/continue state, falling back to the hardcoded eat/salary/sleep hacks", "[kfx_config][config_creature]") {
    setup_test_job(); // initial_crstate = CrSt_CreatureSleep, continue_crstate = CrSt_CreatureWantsAHome
    CHECK(get_job_for_creature_state(CrSt_CreatureSleep) == Job_TUNNEL);
    CHECK(get_job_for_creature_state(CrSt_Unused) == Job_NULL);

    kfx_config_state.conf.crtr_conf.jobs_count = 0; // no configured jobs -> only the hardcoded fallback applies
    CHECK(get_job_for_creature_state(CrSt_CreatureWantsSalary) == Job_TAKE_SALARY);
    CHECK(get_job_for_creature_state(CrSt_CreatureEat) == Job_TAKE_FEED);
}

TEST_CASE_METHOD(ResetAll, "get_first_room_kind_for_job returns ROOM_KIND_NONE (0) when no room kind's role matches", "[kfx_config][config_creature]") {
    setup_test_job();
    // room_types_count defaults to 0 after reset -- the search loop never runs.
    CHECK(get_first_room_kind_for_job(Job_TUNNEL) == 0);
}

TEST_CASE_METHOD(ResetAll, "get_creature_job_causing_stress/get_creature_job_causing_going_postal return Job_NULL when no room role is configured for the room kind", "[kfx_config][config_creature]") {
    setup_test_job();
    // get_room_roles(0) resolves to RoRoF_None on a freshly-reset terrain
    // config, which short-circuits get_job_which_qualify_for_room_role().
    CHECK(get_creature_job_causing_stress(Job_TUNNEL, 0) == Job_NULL);
    CHECK(get_creature_job_causing_going_postal(Job_TUNNEL, 0) == Job_NULL);
}

TEST_CASE("load_creaturetypes_config_file returns false for a missing file", "[kfx_config][config_creature]") {
    CHECK_FALSE(keeper_creaturetp_file_data.load_func("/does/not/exist.cfg", CnfLd_IgnoreErrors));
}

TEST_CASE("keeper_creaturetp_file_data has no pre/post-load hooks", "[kfx_config][config_creature]") {
    CHECK(keeper_creaturetp_file_data.pre_load_func == nullptr);
    CHECK(keeper_creaturetp_file_data.post_load_func == nullptr);
    CHECK(std::strcmp(keeper_creaturetp_file_data.filename, "creature.cfg") == 0);
}
