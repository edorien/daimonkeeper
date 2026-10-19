// kfx_sim coverage: a first pass over player_comptask.c's computer-AI
// task-list bookkeeping -- get_task_in_progress/is_task_in_progress(_using_hand)
// and remove_task walk/mutate a Computer2's singly-linked task list
// (kfx_sim_state.computer_task[], threaded through
// ComputerTask::next_task, gated by the ComTsk_Unkn0001 "task is enabled"
// flag). remove_task in particular has real branching: front-of-list vs.
// middle-of-list removal are different code paths, and a task not found
// in the list at all returns false.
//
// Like player_compchecks.c, player_comptask.c is one of the 4 kfx_sim/src
// files with no matching header (reached only through the
// computer_process_func_list/task_function dispatch tables) -- the
// functions tested here are forward-declared locally rather than added to
// a production header.
//
// Deliberately deferred: this is far and away the largest single file in
// kfx_sim (4000+ lines) and almost entirely task-execution logic
// (task_dig_room/_place_room/_dig_to_entrance/etc.) built on top of
// pathfinding, room-building, and power-hand simulation -- genuinely the
// hard tail, left for a later increment.
#include <catch2/catch_test_macros.hpp>

#include "player_computer.h"
#include "kfx_ai_test_fixtures.h"

using namespace kfx_test;

extern "C" {
struct ComputerTask *get_task_in_progress(struct Computer2 *comp, ComputerTaskType ttype);
TbBool is_task_in_progress(struct Computer2 *comp, ComputerTaskType ttype);
TbBool is_task_in_progress_using_hand(struct Computer2 *comp);
TbBool remove_task(struct Computer2 *comp, struct ComputerTask *ctask);
}

namespace {
// Links task_idx onto the front of comp's task list, marked enabled
// (ComTsk_Unkn0001), with the given type (and, for CTT_WaitForBridge,
// sub-task original type).
struct ComputerTask *add_enabled_task(struct Computer2 *comp, int64_t task_idx, ComputerTaskType ttype, ComputerTaskType ottype = CTT_None)
{
    struct ComputerTask *ctask = get_computer_task(task_idx);
    ctask->flags |= ComTsk_Unkn0001;
    ctask->ttype = ttype;
    ctask->ottype = ottype;
    ctask->next_task = comp->task_idx;
    comp->task_idx = task_idx;
    return ctask;
}
}

TEST_CASE_METHOD(ResetSimAndConfig, "is_task_in_progress finds an enabled task of the given type and ignores disabled ones", "[kfx_sim][player_comptask]") {
    struct Computer2 *comp = make_computer_player(0);
    CHECK_FALSE(is_task_in_progress(comp, CTT_MoveCreatureToRoom));

    add_enabled_task(comp, 1, CTT_MoveCreatureToRoom);
    CHECK(is_task_in_progress(comp, CTT_MoveCreatureToRoom));
    CHECK_FALSE(is_task_in_progress(comp, CTT_MoveCreatureToPos));

    // A task that exists but isn't flagged enabled doesn't count.
    struct ComputerTask *disabled = get_computer_task(2);
    disabled->ttype = CTT_MoveCreatureToPos;
    disabled->next_task = comp->task_idx;
    comp->task_idx = 2;
    CHECK_FALSE(is_task_in_progress(comp, CTT_MoveCreatureToPos));
}

TEST_CASE_METHOD(ResetSimAndConfig, "is_task_in_progress resolves a CTT_WaitForBridge sub-task via its original task type", "[kfx_sim][player_comptask]") {
    struct Computer2 *comp = make_computer_player(0);
    add_enabled_task(comp, 1, CTT_WaitForBridge, CTT_MoveCreatureToRoom);

    CHECK(is_task_in_progress(comp, CTT_MoveCreatureToRoom)); // matched via ottype, not ttype
    CHECK_FALSE(is_task_in_progress(comp, CTT_WaitForBridge)); // ttype itself is never matched directly
}

TEST_CASE_METHOD(ResetSimAndConfig, "is_task_in_progress_using_hand is true for any of the five hand-using task types", "[kfx_sim][player_comptask]") {
    struct Computer2 *comp = make_computer_player(0);
    CHECK_FALSE(is_task_in_progress_using_hand(comp));

    add_enabled_task(comp, 1, CTT_MoveGoldToTreasury);
    CHECK(is_task_in_progress_using_hand(comp));
}

TEST_CASE_METHOD(ResetSimAndConfig, "remove_task unlinks the first task in the list and clears its enabled flag", "[kfx_sim][player_comptask]") {
    struct Computer2 *comp = make_computer_player(0);
    add_enabled_task(comp, 2, CTT_MoveCreatureToPos);
    struct ComputerTask *task1 = add_enabled_task(comp, 1, CTT_MoveCreatureToRoom); // now the list head

    CHECK(remove_task(comp, task1));
    CHECK(comp->task_idx == 2); // task2 is now first
    CHECK(task1->next_task == 0);
    CHECK_FALSE((task1->flags & ComTsk_Unkn0001) != 0);
    CHECK(is_task_in_progress(comp, CTT_MoveCreatureToPos)); // task2 still there
}

TEST_CASE_METHOD(ResetSimAndConfig, "remove_task unlinks a task from the middle of the list, relinking its neighbor", "[kfx_sim][player_comptask]") {
    struct Computer2 *comp = make_computer_player(0);
    struct ComputerTask *task3 = add_enabled_task(comp, 3, CTT_MoveCreaturesToDefend);
    struct ComputerTask *task2 = add_enabled_task(comp, 2, CTT_MoveCreatureToPos);
    add_enabled_task(comp, 1, CTT_MoveCreatureToRoom); // list: 1 -> 2 -> 3

    CHECK(remove_task(comp, task2));
    CHECK(comp->task_idx == 1); // head unchanged
    CHECK(get_computer_task(1)->next_task == 3); // task1 now points past task2, straight to task3
    CHECK(task2->next_task == 0);
    CHECK(is_task_in_progress(comp, CTT_MoveCreatureToRoom));
    CHECK(is_task_in_progress(comp, CTT_MoveCreaturesToDefend));
    CHECK_FALSE(is_task_in_progress(comp, CTT_MoveCreatureToPos));
    (void)task3;
}

TEST_CASE_METHOD(ResetSimAndConfig, "remove_task returns false for a task that isn't actually in the list", "[kfx_sim][player_comptask]") {
    struct Computer2 *comp = make_computer_player(0);
    add_enabled_task(comp, 1, CTT_MoveCreatureToRoom);

    struct ComputerTask *stray = get_computer_task(5); // never linked in
    CHECK_FALSE(remove_task(comp, stray));
}

// --- tool_dig_to_pos2 and liquid slabs (upstream #786) ---
// A 5x3-slab map whose middle row is: claimed start (0,1), one liquid slab
// (1,1), path destination (2,1). Every other slab is kind 0 with no flags.
namespace {
void setup_liquid_strip(struct Computer2 *comp, SlabKind liquid, TbBool bridge_buildable)
{
    kfx_sim_state.map_tiles_x = 5;
    kfx_sim_state.map_tiles_y = 3;
    kfx_sim_state.map_subtiles_x = 5 * STL_PER_SLB;
    kfx_sim_state.map_subtiles_y = 3 * STL_PER_SLB;
    struct SlabsConfig *slab_conf = &kfx_config_state.conf.slab_conf;
    slab_conf->slab_types_count = SlbT_WATER + 1;
    slab_conf->slab_cfgstats[SlbT_WATER].wlb_type = WlbT_Water;
    slab_conf->slab_cfgstats[SlbT_LAVA].wlb_type = WlbT_Lava;
    configure_room_role(RoK_BRIDGE, RoRoF_PassWater | RoRoF_PassLava);
    struct Dungeon *dungeon = computer_dungeon(comp);
    dungeon->room_resrchable[RoK_BRIDGE] = 1;
    dungeon->room_buildable[RoK_BRIDGE] = bridge_buildable ? 1 : 0;
    struct SlabMap *start = get_slabmap_block(0, 1);
    start->kind = SlbT_CLAIMED;
    start->owner = dungeon->owner;
    get_slabmap_block(1, 1)->kind = liquid;
    get_slabmap_block(2, 1)->kind = SlbT_PATH;
}

ToolDigResult dig_across_strip(struct Computer2 *comp, struct ComputerDig *cdig)
{
    struct Coord3d startpos = {};
    struct Coord3d endpos = {};
    startpos.x.val = subtile_coord_center(slab_subtile_center(0));
    startpos.y.val = subtile_coord_center(slab_subtile_center(1));
    endpos.x.val = subtile_coord_center(slab_subtile_center(2));
    endpos.y.val = subtile_coord_center(slab_subtile_center(1));
    setup_dig_to(cdig, startpos, endpos);
    return tool_dig_to_pos2(comp, cdig, true, ToolDig_BasicOnly);
}
}

TEST_CASE_METHOD(ResetSimAndConfig, "tool_dig_to_pos2 asks for a bridge on a water slab when a bridge is available (upstream #786)", "[kfx_ai][player_comptask]") {
    struct Computer2 *comp = make_computer_player(0);
    setup_liquid_strip(comp, SlbT_WATER, true);
    struct ComputerDig cdig;
    CHECK((int)dig_across_strip(comp, &cdig) == TDR_BuildBridgeOnSlab);
    CHECK(subtile_slab(cdig.pos_next.x.stl.num) == 1);
    CHECK(subtile_slab(cdig.pos_next.y.stl.num) == 1);
}

TEST_CASE_METHOD(ResetSimAndConfig, "tool_dig_to_pos2 asks for a bridge on a lava slab when a bridge is available", "[kfx_ai][player_comptask]") {
    struct Computer2 *comp = make_computer_player(0);
    setup_liquid_strip(comp, SlbT_LAVA, true);
    struct ComputerDig cdig;
    CHECK((int)dig_across_strip(comp, &cdig) == TDR_BuildBridgeOnSlab);
    CHECK(subtile_slab(cdig.pos_next.x.stl.num) == 1);
    CHECK(subtile_slab(cdig.pos_next.y.stl.num) == 1);
}

TEST_CASE_METHOD(ResetSimAndConfig, "tool_dig_to_pos2 treats water as blocking when no bridge is available", "[kfx_ai][player_comptask]") {
    struct Computer2 *comp = make_computer_player(0);
    setup_liquid_strip(comp, SlbT_WATER, false);
    struct ComputerDig cdig;
    int result = dig_across_strip(comp, &cdig);
    CHECK(result != TDR_BuildBridgeOnSlab);
    CHECK(result != TDR_ReachedDestination);
}
