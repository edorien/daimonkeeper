// kfx_sim coverage: a first pass over player_compprocs.c's computer-AI
// room-capacity and task-list-query helpers -- count_no_room_build_tasks/
// get_room_build_task_nearest_to reuse the same ComputerTask linked-list
// shape introduced in player_comptask_test.cpp (via
// kfx_sim_test_fixtures.h's add-task pattern, reimplemented locally here
// since it's specific to each task's ttype/new_room_pos rather than just
// its enabled flag); computer_get_room_role_total_capacity/
// computer_get_room_kind_free_capacity and there_is_virgin_entrance_for_computer
// reuse the room-list/entrance-list fixture helpers from
// kfx_sim_test_fixtures.h.
//
// player_compprocs.c is the last of the 4 kfx_sim/src files with no
// matching header (see player_compchecks_test.cpp's note) -- reached only
// through computer_process_func_type/computer_check_func_type's dispatch
// tables. count_no_room_build_tasks and get_room_build_task_nearest_to are
// forward-declared locally in this test file for that reason;
// computer_get_room_role_total_capacity/computer_get_room_kind_free_capacity/
// there_is_virgin_entrance_for_computer are already public via
// player_computer.h.
//
// Deliberately deferred: computer_get_room_kind_free_capacity's
// RoRoF_LairStorage branch (needs calculate_free_lair_space's own
// per-creature-lair-size fixture) and everything else in this file
// (computer_setup_*/computer_check_any_room/_dig_to_entrance/_dig_to_gold,
// move_imp_to_dig_here/_mine_here) -- task-queue and pathfinding-simulation
// heavy, genuinely the hard tail. simulate_dig_to has cases at the end, on
// small hand-built maps (upstream #189).
#include <catch2/catch_test_macros.hpp>

#include "player_computer.h"
#include "dungeon_data.h"
#include "room_data.h"
#include "config_terrain.h"
#include "kfx_ai_test_fixtures.h"

using namespace kfx_test;

extern "C" {
int64_t count_no_room_build_tasks(const struct Computer2 *comp);
struct ComputerTask *get_room_build_task_nearest_to(const struct Computer2 *comp, MapSubtlCoord stl_x, MapSubtlCoord stl_y, int64_t *retdist);
int64_t computer_get_room_kind_free_capacity(struct Computer2 *comp, RoomKind room_kind);
TbBool there_is_virgin_entrance_for_computer(const struct Computer2 *comp);
TbBool simulate_dig_to(struct Computer2 *comp, struct Coord3d *startpos, const struct Coord3d *endpos, uint64_t *dig_distance, DigFlags digflags);
}

namespace {
struct ComputerTask *add_task(struct Computer2 *comp, int64_t task_idx, ComputerTaskType ttype, unsigned char flags = ComTsk_Unkn0001)
{
    struct ComputerTask *ctask = get_computer_task(task_idx);
    ctask->flags = flags;
    ctask->ttype = ttype;
    ctask->next_task = comp->task_idx;
    comp->task_idx = task_idx;
    return ctask;
}
}

TEST_CASE_METHOD(ResetSimAndConfig, "count_no_room_build_tasks counts only enabled tasks of the four room-building types", "[kfx_sim][player_compprocs]") {
    struct Computer2 *comp = make_computer_player(0);
    CHECK(count_no_room_build_tasks(comp) == 0);

    add_task(comp, 1, CTT_DigRoomPassage);
    add_task(comp, 2, CTT_DigRoom);
    add_task(comp, 3, CTT_CheckRoomDug);
    add_task(comp, 4, CTT_PlaceRoom);
    add_task(comp, 5, CTT_MoveCreatureToRoom); // not a room-building type
    add_task(comp, 6, CTT_DigRoom, /*flags=*/0); // right type, but disabled

    CHECK(count_no_room_build_tasks(comp) == 4);
}

TEST_CASE_METHOD(ResetSimAndConfig, "get_room_build_task_nearest_to finds the closest matching task by Manhattan distance", "[kfx_sim][player_compprocs]") {
    struct Computer2 *comp = make_computer_player(0);

    struct ComputerTask *far = add_task(comp, 1, CTT_DigRoom, ComTsk_Unkn0001 | ComTsk_Unkn0002);
    far->new_room_pos.x.stl.num = 0;
    far->new_room_pos.y.stl.num = 0;

    struct ComputerTask *near = add_task(comp, 2, CTT_DigRoom, ComTsk_Unkn0001 | ComTsk_Unkn0002);
    near->new_room_pos.x.stl.num = 5;
    near->new_room_pos.y.stl.num = 5;

    int64_t dist = -1;
    struct ComputerTask *found = get_room_build_task_nearest_to(comp, 6, 6, &dist);

    CHECK(found == near);
    CHECK(dist == 2); // |6-5| + |6-5|
}

TEST_CASE_METHOD(ResetSimAndConfig, "get_room_build_task_nearest_to ignores tasks missing either required flag", "[kfx_sim][player_compprocs]") {
    struct Computer2 *comp = make_computer_player(0);
    add_task(comp, 1, CTT_DigRoom, ComTsk_Unkn0001); // missing Unkn0002

    int64_t dist = -1;
    CHECK(computer_task_invalid(get_room_build_task_nearest_to(comp, 0, 0, &dist)));
}

TEST_CASE_METHOD(ResetSimAndConfig, "computer_get_room_role_total_capacity sums total_capacity across every room kind matching the role", "[kfx_sim][player_compprocs]") {
    struct Computer2 *comp = make_computer_player(0);
    configure_room_role(RoK_PRISON, RoRoF_Prison);

    CHECK(computer_get_room_role_total_capacity(comp, RoRoF_Prison) == 0);

    struct Room *prison = make_room_at_slab(1, 0, 0, RoK_PRISON, 0);
    prison->total_capacity = 10;
    link_room_into_owner_list(&computer_dungeon(comp)->room_list_start[RoK_PRISON], 1);

    struct Room *prison2 = make_room_at_slab(2, 1, 0, RoK_PRISON, 0);
    prison2->total_capacity = 5;
    link_room_into_owner_list(&computer_dungeon(comp)->room_list_start[RoK_PRISON], 2);

    CHECK(computer_get_room_role_total_capacity(comp, RoRoF_Prison) == 15);
}

TEST_CASE_METHOD(ResetSimAndConfig, "computer_get_room_kind_free_capacity reports 9999 for food storage and for a kind the player has no capacity in", "[kfx_sim][player_compprocs]") {
    struct Computer2 *comp = make_computer_player(0);
    configure_room_role(RoK_GARDEN, RoRoF_FoodStorage);
    CHECK(computer_get_room_kind_free_capacity(comp, RoK_GARDEN) == 9999);

    configure_room_role(RoK_PRISON, RoRoF_Prison); // a role with no special-cased handling
    CHECK(computer_get_room_kind_free_capacity(comp, RoK_PRISON) == 9999); // no prison rooms yet
}

TEST_CASE_METHOD(ResetSimAndConfig, "computer_get_room_kind_free_capacity is total minus used once the dungeon actually has the room", "[kfx_sim][player_compprocs]") {
    struct Computer2 *comp = make_computer_player(0);
    configure_room_role(RoK_PRISON, RoRoF_Prison);
    struct Room *prison = make_room_at_slab(1, 0, 0, RoK_PRISON, 0);
    prison->total_capacity = 10;
    prison->used_capacity = 4;
    link_room_into_owner_list(&computer_dungeon(comp)->room_list_start[RoK_PRISON], 1);

    CHECK(computer_get_room_kind_free_capacity(comp, RoK_PRISON) == 6);
}

TEST_CASE_METHOD(ResetSimAndConfig, "there_is_virgin_entrance_for_computer is true only for a foreign-owned entrance this player is interested in", "[kfx_sim][player_compprocs]") {
    struct Computer2 *comp = make_computer_player(0);
    CHECK_FALSE(there_is_virgin_entrance_for_computer(comp));

    struct Room *entrance = make_room_at_slab(1, 0, 0, RoK_ENTRANCE, 1); // owned by player 1
    link_room_into_kind_list(&kfx_sim_state.entrance_room_id, 1);
    CHECK_FALSE(there_is_virgin_entrance_for_computer(comp)); // not marked interested yet

    entrance->player_interested[0] |= 0x01;
    CHECK(there_is_virgin_entrance_for_computer(comp));

    entrance->owner = 0; // now owned by the computer's own player
    CHECK_FALSE(there_is_virgin_entrance_for_computer(comp));
}

// --- simulate_dig_to past a liquid slab (upstream #189) ---
// A 6x3-slab map whose middle row is: claimed start (0,1), water_width water
// slabs from (1,1), a far-bank slab, then the gold destination. Every other
// slab is undiggable rock (kind 0), so the gold is reachable only through the
// far-bank slab.
namespace {
MapSlabCoord setup_gold_across_water(struct Computer2 *comp, SlabKind far_bank, MapSlabCoord water_width = 1)
{
    kfx_sim_state.map_tiles_x = 6;
    kfx_sim_state.map_tiles_y = 3;
    kfx_sim_state.map_subtiles_x = 6 * STL_PER_SLB;
    kfx_sim_state.map_subtiles_y = 3 * STL_PER_SLB;
    struct SlabsConfig *slab_conf = &kfx_config_state.conf.slab_conf;
    slab_conf->slab_types_count = SlbT_WATER + 1;
    struct SlabConfigStats *rock = &slab_conf->slab_cfgstats[SlbT_ROCK];
    rock->block_flags = SlbAtFlg_Filled;
    rock->indestructible = 1;
    struct SlabConfigStats *gold = &slab_conf->slab_cfgstats[SlbT_GOLD];
    gold->block_flags = SlbAtFlg_Filled | SlbAtFlg_Valuable;
    gold->is_diggable = 1;
    struct SlabConfigStats *earth = &slab_conf->slab_cfgstats[SlbT_EARTH];
    earth->block_flags = SlbAtFlg_Filled | SlbAtFlg_Digable;
    earth->is_diggable = 1;
    slab_conf->slab_cfgstats[SlbT_WATER].wlb_type = WlbT_Water;
    configure_room_role(RoK_BRIDGE, RoRoF_PassWater | RoRoF_PassLava);
    struct Dungeon *dungeon = computer_dungeon(comp);
    dungeon->room_resrchable[RoK_BRIDGE] = 1;
    dungeon->room_buildable[RoK_BRIDGE] = 1;
    struct SlabMap *start = get_slabmap_block(0, 1);
    start->kind = SlbT_CLAIMED;
    start->owner = dungeon->owner;
    for (MapSlabCoord slb_x = 1; slb_x <= water_width; slb_x++)
        get_slabmap_block(slb_x, 1)->kind = SlbT_WATER;
    get_slabmap_block(water_width + 1, 1)->kind = far_bank;
    get_slabmap_block(water_width + 2, 1)->kind = SlbT_GOLD;
    return water_width + 2;
}

TbBool simulate_dig_to_gold(struct Computer2 *comp, MapSlabCoord gold_slb_x)
{
    struct Coord3d startpos = {};
    struct Coord3d endpos = {};
    startpos.x.val = subtile_coord_center(slab_subtile_center(0));
    startpos.y.val = subtile_coord_center(slab_subtile_center(1));
    endpos.x.val = subtile_coord_center(slab_subtile_center(gold_slb_x));
    endpos.y.val = subtile_coord_center(slab_subtile_center(1));
    uint64_t dig_distance = 0;
    return simulate_dig_to(comp, &startpos, &endpos, &dig_distance, ToolDig_AllowValuable);
}
}

TEST_CASE_METHOD(ResetSimAndConfig, "simulate_dig_to rejects gold behind water and then rock (upstream #189)", "[kfx_ai][player_compprocs]") {
    struct Computer2 *comp = make_computer_player(0);
    MapSlabCoord gold_slb_x = setup_gold_across_water(comp, SlbT_ROCK);
    // The bridge would only lead to rock: needing a bridge is not reaching the gold.
    CHECK_FALSE(simulate_dig_to_gold(comp, gold_slb_x));
}

TEST_CASE_METHOD(ResetSimAndConfig, "simulate_dig_to reaches gold behind water and then earth", "[kfx_ai][player_compprocs]") {
    struct Computer2 *comp = make_computer_player(0);
    MapSlabCoord gold_slb_x = setup_gold_across_water(comp, SlbT_EARTH);
    CHECK(simulate_dig_to_gold(comp, gold_slb_x));
}

TEST_CASE_METHOD(ResetSimAndConfig, "simulate_dig_to crosses a two-slab-wide water strip", "[kfx_ai][player_compprocs]") {
    struct Computer2 *comp = make_computer_player(0);
    MapSlabCoord gold_slb_x = setup_gold_across_water(comp, SlbT_EARTH, 2);
    CHECK(simulate_dig_to_gold(comp, gold_slb_x));
}

TEST_CASE_METHOD(ResetSimAndConfig, "simulate_dig_to rejects gold behind a two-slab-wide water strip and then rock", "[kfx_ai][player_compprocs]") {
    struct Computer2 *comp = make_computer_player(0);
    MapSlabCoord gold_slb_x = setup_gold_across_water(comp, SlbT_ROCK, 2);
    CHECK_FALSE(simulate_dig_to_gold(comp, gold_slb_x));
}
