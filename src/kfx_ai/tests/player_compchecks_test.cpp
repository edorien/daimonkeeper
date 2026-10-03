// kfx_sim coverage: a first pass over player_compchecks.c's computer-AI
// "check" functions -- the multi-branch decision logic that periodically
// re-evaluates a computer player's dungeon and either leaves it alone
// (CTaskRet_Unk4/no-op) or kicks off a task (CTaskRet_Unk1). These are
// genuinely complicated: computer_checks_hates alone loops over every
// player, applying four independent "reasons to hate" against a rival
// dungeon's creature/digger/room/entrance counts while skipping
// self/neutral/inactive/allied players; computer_check_prison_tendency
// branches on both room capacity vs. unit count *and* a tri-state script-
// override flag.
//
// player_compchecks.c is one of exactly four files in kfx_sim/src (with
// player_compevents.c/player_comptask.c/player_compprocs.c) that has no
// matching public header -- confirmed by checking every other .c file in
// this library has one. Its functions are reached only through
// computer_check_func_list's function-pointer table, never called
// directly from another translation unit, so this is deliberate, not an
// oversight worth "fixing" the way a same-file-only forward declaration
// normally would be. The declarations below are local to this test file
// rather than added to any production header.
//
// calculate_number_of_creatures_to_move: the fixture's creatures keep
// the zeroed thing->model 0, and creature_stats_get_from_thing() maps
// model 0 (below 1) to model[0] -- so the test configures job_primary/
// job_secondary on model[0] rather than per-model.
//
// Deliberately deferred: computer_check_move_creatures_to_best_room/
// _to_room (need computer_able_to_use_power/is_task_in_progress_using_hand/
// create_task_move_creatures_to_room's full task-queue machinery on top of
// what's tested here), computer_check_no_imps/_for_pretty/_for_quick_attack/
// _for_accelerate/_for_flight/_for_vision/_slap_imps/_enemy_entrances/
// _for_place_door/_neutral_places/_for_place_trap/_for_expand_room/
// _for_money (each its own room/task/power-hand fixture), and the static
// any_digger_is_digging_indestructible_valuables/
// count_faces_of_indestructible_valuables_marked_for_dig helpers (no
// external linkage to test directly).
#include <catch2/catch_test_macros.hpp>

#include "player_computer.h"
#include "dungeon_data.h"
#include "room_data.h"
#include "config_creature.h"
#include "config_terrain.h"
#include "kfx_ai_test_fixtures.h"
#include "thing_stats.h"

using namespace kfx_test;

extern "C" {
int64_t calculate_number_of_creatures_to_move(struct Dungeon *dungeon, int64_t percent_to_reassign);
int64_t computer_checks_hates(struct Computer2 *comp, struct ComputerCheck *check);
int64_t computer_check_prison_tendency(struct Computer2 *comp, struct ComputerCheck *check);
int64_t count_slabs_around_of_kind(MapSlabCoord slb_x, MapSlabCoord slb_y, SlabKind slbkind, PlayerNumber owner);
}

TEST_CASE_METHOD(ResetSimAndConfig, "calculate_number_of_creatures_to_move returns 0 for an empty creature list", "[kfx_sim][player_compchecks]") {
    struct Dungeon *dungeon = get_dungeon(0);
    CHECK(calculate_number_of_creatures_to_move(dungeon, 50) == 0);
}

TEST_CASE_METHOD(ResetSimAndConfig, "calculate_number_of_creatures_to_move scales by how far off the current other-jobs percentage is from the target", "[kfx_sim][player_compchecks]") {
    struct Dungeon *dungeon = get_dungeon(0);
    kfx_config_state.conf.crtr_conf.model[0].job_primary = 1;
    kfx_config_state.conf.crtr_conf.model[0].job_secondary = 2;

    make_creature(1, 1, 0); // job_assigned defaults to 0: counts as primary/secondary
    link_creature_into_player_list(&dungeon->creatr_list_start, 1);

    make_creature(2, 2, 0);
    creature_control_get(2)->job_assigned = 1; // matches job_primary
    link_creature_into_player_list(&dungeon->creatr_list_start, 2);

    make_creature(3, 3, 0);
    creature_control_get(3)->job_assigned = 2; // matches job_secondary
    link_creature_into_player_list(&dungeon->creatr_list_start, 3);

    make_creature(4, 4, 0);
    creature_control_get(4)->job_assigned = 4; // matches neither: "other jobs"
    link_creature_into_player_list(&dungeon->creatr_list_start, 4);

    // 1 of 4 creatures (25%) is doing "other" work.
    CHECK(calculate_number_of_creatures_to_move(dungeon, 25) == 0);  // target == current: nothing to move
    CHECK(calculate_number_of_creatures_to_move(dungeon, 50) == 1);  // 4 * (50-25) / 100 == 1
    CHECK(calculate_number_of_creatures_to_move(dungeon, 0) == 0);   // negative delta clamped to 0
}

TEST_CASE_METHOD(ResetSimAndConfig, "count_slabs_around_of_kind counts only the 8 neighbors matching both kind and owner", "[kfx_sim][player_compchecks]") {
    get_slabmap_block(2, 1)->kind = SlbT_CLAIMED;  // {0,-1}
    get_slabmap_block(3, 2)->kind = SlbT_CLAIMED;  // {1,0}
    get_slabmap_block(1, 1)->kind = SlbT_CLAIMED;  // {-1,-1}, different owner
    get_slabmap_block(1, 1)->owner = 1;

    CHECK(count_slabs_around_of_kind(2, 2, SlbT_CLAIMED, 0) == 2); // owner 0's two matching neighbors
    CHECK(count_slabs_around_of_kind(2, 2, SlbT_CLAIMED, 1) == 1); // owner 1's one matching neighbor
    CHECK(count_slabs_around_of_kind(2, 2, SlbT_ROCK, 0) == 5);    // the 5 untouched (default-kind) neighbors
}

TEST_CASE_METHOD(ResetSimAndConfig, "computer_checks_hates increments hate_amount only for active, non-allied, non-neutral rivals with a real reason to be hated", "[kfx_sim][player_compchecks]") {
    struct Computer2 *comp = make_computer_player(0); // "us"

    // Player 1: a genuine rival, ahead on creatures, diggers, rooms, and entrances.
    make_player_active(1);
    struct Dungeon *dungeon1 = get_dungeon(1);
    make_creature(10, 10, 1)->model = 1;
    link_creature_into_player_list(&dungeon1->creatr_list_start, 10);
    make_creature(11, 11, 1)->model = 1;
    link_creature_into_player_list(&dungeon1->creatr_list_start, 11);
    make_creature(12, 12, 1)->model = 1;
    link_creature_into_player_list(&dungeon1->digger_list_start, 12);
    dungeon1->total_rooms = 1;
    make_room_at_slab(10, 0, 0, RoK_ENTRANCE, 1);
    link_room_into_kind_list(&kfx_sim_state.entrance_room_id, 10);

    // Player 2: an ally with the exact same excess stats -- must still be skipped.
    make_player_active(2);
    get_player(0)->allied_players |= to_flag(2);
    get_player(2)->allied_players |= to_flag(0);
    struct Dungeon *dungeon2 = get_dungeon(2);
    make_creature(20, 20, 2)->model = 1;
    link_creature_into_player_list(&dungeon2->creatr_list_start, 20);
    make_creature(21, 21, 2)->model = 1;
    link_creature_into_player_list(&dungeon2->creatr_list_start, 21);
    dungeon2->total_rooms = 1;

    // Player 3: same excess stats, but not a keeper -- must be skipped.
    struct PlayerInfo *player3 = make_player_active(3);
    player3->player_type = PT_Roaming;
    struct Dungeon *dungeon3 = get_dungeon(3);
    make_creature(30, 30, 3)->model = 1;
    link_creature_into_player_list(&dungeon3->creatr_list_start, 30);
    make_creature(31, 31, 3)->model = 1;
    link_creature_into_player_list(&dungeon3->creatr_list_start, 31);

    // The neutral player, with the same excess stats -- must be skipped.
    make_player_active(PLAYER_NEUTRAL);
    struct Dungeon *dungeonN = get_dungeon(PLAYER_NEUTRAL);
    make_creature(50, 50, PLAYER_NEUTRAL)->model = 1;
    link_creature_into_player_list(&dungeonN->creatr_list_start, 50);
    make_creature(51, 51, PLAYER_NEUTRAL)->model = 1;
    link_creature_into_player_list(&dungeonN->creatr_list_start, 51);

    struct ComputerCheck check = {};
    check.primary_parameter = 0; // 0 < get_gameturn()==0 is false: the "hate for surviving" random branch never fires

    CHECK(computer_checks_hates(comp, &check) == CTaskRet_Unk4);

    CHECK(comp->opponent_relations[1].hate_amount == 8); // 1(creatures) + 1(diggers) + 1(rooms) + 5(entrances)
    CHECK(comp->opponent_relations[2].hate_amount == 0); // ally: skipped
    CHECK(comp->opponent_relations[3].hate_amount == 0); // inactive: skipped
    CHECK(comp->opponent_relations[PLAYER_NEUTRAL].hate_amount == 0); // neutral: skipped
}

TEST_CASE_METHOD(ResetSimAndConfig, "computer_check_prison_tendency defers to the script when status is 0", "[kfx_sim][player_compchecks]") {
    struct Computer2 *comp = make_computer_player(0);
    struct ComputerCheck check = {};
    check.primary_parameter = 0;

    CHECK(computer_check_prison_tendency(comp, &check) == CTaskRet_Unk1);
    CHECK(computer_dungeon(comp)->creature_tendencies == 0); // untouched
}

TEST_CASE_METHOD(ResetSimAndConfig, "computer_check_prison_tendency enables imprisonment once prison capacity and unit count allow it", "[kfx_sim][player_compchecks]") {
    struct Computer2 *comp = make_computer_player(0);
    configure_job(Job_CAPTIVITY, RoRoF_Prison, 0);
    configure_room_role(RoK_PRISON, RoRoF_Prison);

    struct Room *prison = make_room_at_slab(1, 0, 0, RoK_PRISON, 0);
    prison->total_capacity = 10;
    link_room_into_owner_list(&computer_dungeon(comp)->room_list_start[RoK_PRISON], 1);
    computer_dungeon(comp)->num_active_creatrs = 5;

    struct ComputerCheck check = {};
    check.primary_parameter = 1;   // normal (non-manual) status
    check.secondary_parameter = 5; // min_capacity <= total_capacity (10)
    check.tertiary_parameter = 20; // max_units > num_active_creatrs (5)

    CHECK(computer_check_prison_tendency(comp, &check) == CTaskRet_Unk1);
    CHECK((computer_dungeon(comp)->creature_tendencies & CrTend_Imprison) != 0);

    // Already enabled: re-running is a no-op that still reports success.
    CHECK(computer_check_prison_tendency(comp, &check) == CTaskRet_Unk1);
    CHECK((computer_dungeon(comp)->creature_tendencies & CrTend_Imprison) != 0);
}

TEST_CASE_METHOD(ResetSimAndConfig, "computer_check_prison_tendency disables imprisonment once capacity/unit conditions fail, unless the script owns it", "[kfx_sim][player_compchecks]") {
    struct Computer2 *comp = make_computer_player(0);
    configure_job(Job_CAPTIVITY, RoRoF_Prison, 0);
    configure_room_role(RoK_PRISON, RoRoF_Prison);
    computer_dungeon(comp)->creature_tendencies = CrTend_Imprison; // currently enabled

    struct ComputerCheck check = {};
    check.primary_parameter = 1;   // normal status
    check.secondary_parameter = 5; // min_capacity: no prison rooms exist, so total_capacity (0) < 5
    check.tertiary_parameter = 20;

    CHECK(computer_check_prison_tendency(comp, &check) == CTaskRet_Unk1);
    CHECK((computer_dungeon(comp)->creature_tendencies & CrTend_Imprison) == 0); // disabled

    // status == 2: disabling is handled manually by script -- left untouched.
    computer_dungeon(comp)->creature_tendencies = CrTend_Imprison;
    check.primary_parameter = 2;
    CHECK(computer_check_prison_tendency(comp, &check) == CTaskRet_Unk1);
    CHECK((computer_dungeon(comp)->creature_tendencies & CrTend_Imprison) != 0); // untouched
}
