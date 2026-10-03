// Characterisation tests for refactor pass 3, S02 (sight and reveal).
//
// S02 folds creature_senses.c's line-of-sight variants into one stepper and
// map_blocks.c's clear_dig_and_set_explored_can_see_x/_y into one
// axis-generic function, keeping every variant's behaviour. These tests pin
// that behaviour down before the change: they build a synthetic map (floors
// and walls of several heights, low ceilings, lava, two doors of different
// owners, one locked) from a fixed pseudo-random sequence, evaluate every
// variant over many point pairs, hash the answers, and compare the hash
// with the one recorded on the pre-S02 tree. A different hash means a
// variant's behaviour changed.
//
// Checked by mutation before S02: flipping y's allow_next_dir2 (README F2),
// x's or y's allow_next_dir1/2, removing line_of_sight_3d's z clamps (F3),
// dropping nowibble's shortened walk, or changing the own-door or lava
// solidity flags each changes a hash.
#include <catch2/catch_test_macros.hpp>

#include "creature_senses.h"
#include "map_blocks.h"
#include "map_data.h"
#include "map_columns.h"
#include "slab_data.h"
#include "thing_data.h"
#include "dungeon_data.h"
#include "config_cubes.h"
#include "config_terrain.h"
#include "config_slabsets.h"
#include "kfx_sim_test_fixtures.h"

#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
TbBool sibling_line_of_sight_3d_including_lava_check_ignoring_door(const struct Coord3d *prevpos,
    const struct Coord3d *nextpos, const struct Thing *doortng);
TbBool jonty_line_of_sight_3d_including_lava_check_ignoring_specific_door(const struct Coord3d *frpos,
    const struct Coord3d *topos, const struct Thing *doortng);
TbBool sibling_line_of_sight_3d_including_lava_check_ignoring_own_door(const struct Coord3d *prevpos,
    const struct Coord3d *nextpos, PlayerNumber plyr_idx);
TbBool jonty_line_of_sight_3d_including_lava_check_ignoring_own_door(const struct Coord3d *frpos,
    const struct Coord3d *topos, PlayerNumber plyr_idx);
void clear_dig_and_set_explored_can_see_x(MapSlabCoord slb_x, MapSlabCoord slb_y, PlayerNumber plyr_idx, int64_t can_see_slabs);
void clear_dig_and_set_explored_can_see_y(MapSlabCoord slb_x, MapSlabCoord slb_y, PlayerNumber plyr_idx, int64_t can_see_slabs);
}

using namespace kfx_test;

namespace {

const int kSlabs = 10;
const int kSubtiles = kSlabs * STL_PER_SLB;
const int64_t kLavaCube = 40;
const int64_t kLavaTexture = 3;

struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed) : s(seed) {}
    uint32_t next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return (uint32_t)(s >> 33); }
    uint32_t below(uint32_t n) { return next() % n; }
};

struct Hash {
    uint64_t h = 1469598103934665603ULL;
    void add(uint64_t v) { h ^= v; h *= 1099511628211ULL; }
};

void set_column(int64_t idx, int floor, int ceiling, int64_t floor_texture, int64_t top_cube)
{
    struct Column *col = &kfx_sim_state.columns_data[idx];
    col->bitfields = (unsigned char)(((floor & 0x0F) << 4) | ((ceiling & 0x07) << 1));
    col->floor_texture = floor_texture;
    if (floor > 0)
        col->cubes[floor - 1] = top_cube;
}

void place_door(ThingIndex idx, MapSlabCoord slb_x, MapSlabCoord slb_y, PlayerNumber owner, TbBool locked)
{
    struct Thing *door = thing_get(idx);
    door->index = idx;
    door->class_id = TCls_Door;
    door->model = 1;
    door->owner = owner;
    door->alloc_flags = TAlF_Exists;
    door->door.is_locked = locked;
    door->mappos.x.val = subtile_coord_center(slab_subtile_center(slb_x));
    door->mappos.y.val = subtile_coord_center(slab_subtile_center(slb_y));
    struct Map *mapblk = get_map_block_at(slab_subtile_center(slb_x), slab_subtile_center(slb_y));
    door->next_on_mapblk = mapblk->mapwho;
    mapblk->mapwho = idx;
}

/* Builds the synthetic map: per-slab column kinds with per-subtile noise. */
void build_map(uint64_t seed)
{
    kfx_sim_state.map_subtiles_x = kSubtiles;
    kfx_sim_state.map_subtiles_y = kSubtiles;
    kfx_sim_state.map_tiles_x = kSlabs;
    kfx_sim_state.map_tiles_y = kSlabs;
    // Columns: 1 floor, 2 wall, 3 lava floor (no cubes, lava by texture), 4 lava on a raised floor,
    // 5 floor under a low ceiling, 6 raised step, 7 pit, 8 floor under a ceiling cube.
    set_column(1, 1, 0, 0, 1);
    set_column(2, 5, 0, 0, 2);
    set_column(3, 0, 0, kLavaTexture, 0);
    set_column(4, 1, 0, 0, kLavaCube);
    set_column(5, 1, 2, 0, 1);
    set_column(6, 3, 0, 0, 1);
    set_column(7, 0, 0, 0, 0);
    set_column(8, 1, 1, 0, 1);
    kfx_sim_state.top_cube[kLavaTexture] = kLavaCube;
    kfx_config_state.conf.cube_conf.cube_cfgstats[kLavaCube].properties_flags = CPF_IsLava;
    Lcg rng(seed);
    for (int slb_y = 0; slb_y < kSlabs; slb_y++) {
        for (int slb_x = 0; slb_x < kSlabs; slb_x++) {
            const int64_t slab_col = 1 + rng.below(8);
            const unsigned char slab_filled = (unsigned char)rng.below(16);
            const unsigned char slab_flags = (unsigned char)rng.next();
            for (int dy = 0; dy < STL_PER_SLB; dy++) {
                for (int dx = 0; dx < STL_PER_SLB; dx++) {
                    struct Map *mapblk = get_map_block_at(slab_subtile(slb_x, dx), slab_subtile(slb_y, dy));
                    const TbBool noisy = (rng.below(6) == 0);
                    mapblk->col_idx = noisy ? (ColumnIndex)(1 + rng.below(8)) : (ColumnIndex)slab_col;
                    mapblk->filled_subtiles = noisy ? (unsigned char)rng.below(16) : slab_filled;
                    mapblk->flags = noisy ? (unsigned char)rng.next() : slab_flags;
                }
            }
        }
    }
    place_door(100, 3, 3, 0, false);
    place_door(101, 6, 5, 1, true);
}

std::vector<struct Coord3d> sample_points(uint64_t seed)
{
    Lcg rng(seed);
    std::vector<struct Coord3d> pts;
    for (int i = 0; i < 140; i++) {
        struct Coord3d p;
        std::memset(&p, 0, sizeof(p));
        p.x.val = (MapCoord)(COORD_PER_STL + rng.below((kSubtiles - 2) * COORD_PER_STL));
        p.y.val = (MapCoord)(COORD_PER_STL + rng.below((kSubtiles - 2) * COORD_PER_STL));
        p.z.val = (MapCoord)rng.below(7 * COORD_PER_STL); // any height, so z steps overshoot their target
        pts.push_back(p);
    }
    return pts;
}

}

TEST_CASE_METHOD(ResetSimAndConfig, "every line-of-sight variant answers as recorded before S02 on a synthetic map", "[kfx_sim][creature_senses][characterisation]") {
    build_map(0x5202);
    // The pre-S02 height stepping, which the CROOKED_SIGHT_LINES classic bug keeps (pass 3 F3).
    set_flag(kfx_config_state.conf.rules[0].gameplay.classic_bugs_flags, ClscBug_CrookedSightLines);
    const std::vector<struct Coord3d> pts = sample_points(0x5203);
    const struct Thing *doors[] = { INVALID_THING, thing_get(100), thing_get(101) };
    Hash h;
    int64_t trues[16] = {0};
    int64_t total = 0;
    for (const struct Coord3d &a : pts) {
        for (const struct Coord3d &b : pts) {
            int f = 0;
            auto rec = [&](TbBool r) { h.add((uint64_t)(r != 0) + 2 * (uint64_t)f); trues[f] += (r != 0); f++; };
            rec(line_of_sight_3d(&a, &b));
            rec(nowibble_line_of_sight_3d(&a, &b));
            rec(line_of_sight_2d(&a, &b));
            for (const struct Thing *d : doors) {
                rec(line_of_sight_3d_ignoring_specific_door(&a, &b, d));
                rec(jonty_line_of_sight_3d_including_lava_check_ignoring_specific_door(&a, &b, d));
            }
            rec(jonty_line_of_sight_3d_including_lava_check_ignoring_own_door(&a, &b, 0));
            rec(jonty_line_of_sight_3d_including_lava_check_ignoring_own_door(&a, &b, 1));
            total++;
        }
    }
    // Sibling checks take two neighbouring points: every point against its 26 neighbours.
    for (const struct Coord3d &a : pts) {
        for (int dz = -1; dz <= 1; dz++) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dy && !dz) continue;
            struct Coord3d b = a;
            b.x.val += dx * COORD_PER_STL;
            b.y.val += dy * COORD_PER_STL;
            b.z.val += dz * COORD_PER_STL;
            int f = 11;
            auto rec = [&](TbBool r) { h.add((uint64_t)(r != 0) + 2 * (uint64_t)f); trues[f] += (r != 0); f++; };
            rec(sibling_line_of_sight_ignoring_door(&a, &b, doors[1]));
            rec(sibling_line_of_sight_3d_including_lava_check_ignoring_door(&a, &b, doors[2]));
            rec(sibling_line_of_sight_3d_including_lava_check_ignoring_own_door(&a, &b, 0));
            rec(sibling_line_of_sight_3d_including_lava_check_ignoring_own_door(&a, &b, 1));
        }
    }
    // The map must exercise both answers of every variant, or the hash proves little.
    for (int f = 0; f < 15; f++) {
        INFO("variant " << f << " true " << trues[f] << " of " << total);
        CHECK(trues[f] > 0);
        CHECK(trues[f] < (f < 11 ? total : (int64_t)pts.size() * 26));
    }
    CHECK(h.h == 9882139545660253548ULL); // recorded on the pre-S02 tree (c8929677a)
}

TEST_CASE_METHOD(ResetSimAndConfig, "the x and y reveal sweeps reveal and untag as recorded before S02", "[kfx_sim][map_blocks][characterisation]") {
    build_map(0x5204);
    // Slab kinds with varied block flags; map slabs get kinds and owners from the same sequence.
    Lcg rng(0x5205);
    // Kind 0 is SlbT_ROCK: filled, and clear_slab_dig() untags rock; kinds 6 and 7 are filled and
    // blocking, so a sweep that reaches them past a wall reveals them.
    const unsigned long kinds[] = { SlbAtFlg_Filled | SlbAtFlg_Blocking, SlbAtFlg_Filled, SlbAtFlg_Digable, SlbAtFlg_Valuable,
        SlbAtFlg_IsDoor, 0, SlbAtFlg_Filled | SlbAtFlg_Blocking, SlbAtFlg_Filled | SlbAtFlg_Blocking };
    kfx_config_state.conf.slab_conf.slab_types_count = 8; // or get_slab_stats() resolves every slab to kind 0
    for (int k = 0; k < 8; k++)
        kfx_config_state.conf.slab_conf.slab_cfgstats[k].block_flags = kinds[k];
    for (int slb_y = 0; slb_y < kSlabs; slb_y++) {
        for (int slb_x = 0; slb_x < kSlabs; slb_x++) {
            struct SlabMap *slb = get_slabmap_block(slb_x, slb_y);
            slb->kind = (SlabKind)rng.below(8);
        }
    }
    // The pre-S02 behaviour, which the CORNER_WALL_UNREVEALED classic bug keeps (pass 3 F2).
    set_flag(kfx_config_state.conf.rules[0].gameplay.classic_bugs_flags, ClscBug_CornerWallUnrevealed);
    // Player 0 (the local player in this fixture) has about half the slabs marked for digging:
    // a task in its dungeon's task list and the map blocks' tagged/unexplored bits (build_map's
    // random flags). clear_slab_dig() removes both, so the hash covers them as well as the reveals.
    struct Dungeon *dungeon = get_dungeon(0);
    int64_t ntasks = 0;
    for (int slb_y = 0; slb_y < kSlabs; slb_y++) {
        for (int slb_x = 0; slb_x < kSlabs; slb_x++) {
            if (rng.below(2) == 0) {
                dungeon->task_list[ntasks].kind = 1;
                dungeon->task_list[ntasks].coords = get_subtile_number(slab_subtile(slb_x, 0) + 1, slab_subtile(slb_y, 0) + 1);
                ntasks++;
            }
        }
    }
    dungeon->highest_task_number = ntasks;
    const int nblocks = (kSubtiles + 1) * (kSubtiles + 1);
    std::vector<unsigned char> flags0(nblocks);
    for (int n = 0; n < nblocks; n++)
        flags0[n] = kfx_sim_state.map[n].flags;
    std::vector<struct MapTask> tasks0(dungeon->task_list, dungeon->task_list + ntasks);
    Hash h;
    int64_t revealed_total = 0;
    int64_t untagged_total = 0;
    for (int slb_y = 0; slb_y < kSlabs; slb_y++) {
        for (int slb_x = 0; slb_x < kSlabs; slb_x++) {
            for (int64_t can_see = 2; can_see <= 7; can_see += 5) {
                for (int n = 0; n < nblocks; n++) {
                    kfx_sim_state.map[n].revealed = 0;
                    kfx_sim_state.map[n].flags = flags0[n];
                }
                std::memcpy(dungeon->task_list, tasks0.data(), sizeof(struct MapTask) * ntasks);
                dungeon->highest_task_number = ntasks;
                // Hashed after each sweep, so the y sweep can't hide what the x sweep did.
                for (int sweep = 0; sweep < 2; sweep++) {
                    if (sweep == 0)
                        clear_dig_and_set_explored_can_see_x(slb_x, slb_y, 0, can_see);
                    else
                        clear_dig_and_set_explored_can_see_y(slb_x, slb_y, 0, can_see);
                    for (int n = 0; n < nblocks; n++) {
                        h.add((uint64_t)kfx_sim_state.map[n].revealed);
                        h.add((uint64_t)kfx_sim_state.map[n].flags);
                        revealed_total += (kfx_sim_state.map[n].revealed != 0);
                        untagged_total += (kfx_sim_state.map[n].flags != flags0[n]);
                    }
                    h.add((uint64_t)dungeon->highest_task_number);
                    for (int64_t t = 0; t < ntasks; t++) {
                        h.add((uint64_t)dungeon->task_list[t].kind);
                        h.add((uint64_t)dungeon->task_list[t].coords);
                    }
                }
            }
        }
    }
    CHECK(revealed_total > 0);
    CHECK(untagged_total > 0);
    CHECK(h.h == 8731813000040209498ULL); // recorded on the pre-S02 tree (c8929677a)
}

TEST_CASE_METHOD(ResetSimAndConfig, "the y reveal sweep is the x sweep transposed, without the CORNER_WALL_UNREVEALED classic bug (pass 3 F2)", "[kfx_sim][map_blocks]") {
    build_map(0x5206);
    const unsigned long kinds[] = { SlbAtFlg_Filled | SlbAtFlg_Blocking, SlbAtFlg_Filled, SlbAtFlg_Digable, SlbAtFlg_Valuable,
        SlbAtFlg_IsDoor, 0, 0, 0 };
    kfx_config_state.conf.slab_conf.slab_types_count = 8;
    for (int k = 0; k < 8; k++)
        kfx_config_state.conf.slab_conf.slab_cfgstats[k].block_flags = kinds[k];
    Lcg rng(0x5207);
    std::vector<SlabKind> map_kinds(kSlabs * kSlabs);
    for (auto &k : map_kinds)
        k = (SlabKind)rng.below(8);
    const int nblocks = (kSubtiles + 1) * (kSubtiles + 1);
    // Which slabs player 0 sees (by their centre subtile) after one sweep from (x, y), on the map or its transpose.
    auto sweep = [&](bool transposed, bool y_sweep, int x, int y, int64_t can_see) {
        for (int slb_y = 0; slb_y < kSlabs; slb_y++)
            for (int slb_x = 0; slb_x < kSlabs; slb_x++)
                get_slabmap_block(slb_x, slb_y)->kind = transposed ? map_kinds[slb_x * kSlabs + slb_y] : map_kinds[slb_y * kSlabs + slb_x];
        for (int n = 0; n < nblocks; n++)
            kfx_sim_state.map[n].revealed = 0;
        if (y_sweep)
            clear_dig_and_set_explored_can_see_y(x, y, 0, can_see);
        else
            clear_dig_and_set_explored_can_see_x(x, y, 0, can_see);
        std::vector<int> seen(kSlabs * kSlabs);
        for (int slb_y = 0; slb_y < kSlabs; slb_y++)
            for (int slb_x = 0; slb_x < kSlabs; slb_x++)
                seen[slb_y * kSlabs + slb_x] = (get_map_block_at(slab_subtile_center(slb_x), slab_subtile_center(slb_y))->revealed & 1) != 0;
        return seen;
    };
    auto count_asymmetric = [&]() {
        int64_t bad = 0;
        for (int y = 0; y < kSlabs; y++)
            for (int x = 0; x < kSlabs; x++)
                for (int64_t can_see = 2; can_see <= 7; can_see += 5) {
                    const std::vector<int> a = sweep(false, false, x, y, can_see);
                    const std::vector<int> b = sweep(true, true, y, x, can_see);
                    for (int sy = 0; sy < kSlabs; sy++)
                        for (int sx = 0; sx < kSlabs; sx++)
                            bad += (a[sy * kSlabs + sx] != b[sx * kSlabs + sy]);
                }
        return bad;
    };
    CHECK(count_asymmetric() == 0);
    // The classic bug: the y sweep misses corner walls the x sweep reveals.
    set_flag(kfx_config_state.conf.rules[0].gameplay.classic_bugs_flags, ClscBug_CornerWallUnrevealed);
    CHECK(count_asymmetric() > 0);
}

TEST_CASE_METHOD(ResetSimAndConfig, "line of sight follows a straight line in height, without the CROOKED_SIGHT_LINES classic bug (pass 3 F3)", "[kfx_sim][creature_senses]") {
    build_map(0x5202);
    const std::vector<struct Coord3d> pts = sample_points(0x5203);
    // line_of_sight_3d and line_of_sight_3d_ignoring_specific_door (no door) have the same rules
    // except the old height stepping, so with straight lines they agree on every pair.
    auto disagreements = [&]() {
        int64_t n = 0;
        for (const struct Coord3d &a : pts)
            for (const struct Coord3d &b : pts)
                n += ((line_of_sight_3d(&a, &b) != 0) != (line_of_sight_3d_ignoring_specific_door(&a, &b, INVALID_THING) != 0));
        return n;
    };
    CHECK(disagreements() == 0);
    // Between points at one height, the stepping never mattered.
    set_flag(kfx_config_state.conf.rules[0].gameplay.classic_bugs_flags, ClscBug_CrookedSightLines);
    int64_t flat = 0;
    for (const struct Coord3d &a : pts)
        for (const struct Coord3d &b : pts)
        {
            if (a.z.val != b.z.val)
                continue;
            const TbBool crooked = line_of_sight_3d(&a, &b);
            clear_flag(kfx_config_state.conf.rules[0].gameplay.classic_bugs_flags, ClscBug_CrookedSightLines);
            const TbBool straight = line_of_sight_3d(&a, &b);
            set_flag(kfx_config_state.conf.rules[0].gameplay.classic_bugs_flags, ClscBug_CrookedSightLines);
            CHECK(crooked == straight);
            flat++;
        }
    CHECK(flat > 0);
    // With the classic bug the two disagree where heights differ.
    CHECK(disagreements() > 0);
}
