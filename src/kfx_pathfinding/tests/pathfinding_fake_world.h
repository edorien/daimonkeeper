// kfx_pathfinding: shared reusable test fake for PathfindingWorldPort
// (src/kfx_config/include/ports/pathfinding_world_port.h), built out for
// ariadne_wallhug.c (previously zero coverage -- see ariadne_wallhug_test.cpp)
// and reused by ariadne_update_test.cpp for the triangulation entry points.
//
// Technique: since refactor pass 2's S08, Ariadne reads struct Thing,
// struct Map and struct SlabMap fields directly (their layouts are in
// kfx_model), so the pointers this fake hands out point at real structs: a
// real struct Map/struct SlabMap per grid cell (kept in step with the Cell
// that describes it each time it's looked up), and FakeThing wraps a real
// struct Thing as its first member, so a FakeThing* and its struct Thing* are
// the same address (as_thing()/as_fake()). The map size lives in
// kfx_pathfinding_state (ariadne_set_map_dimensions()), which the fixtures
// below keep equal to the grid's.
//
// Grid model: a single flat 2D array of Cell, one per subtile, covering
// kGridDim x kGridDim subtiles (comfortably larger than any test's map, with
// out-of-bounds reads clamped to a permanent "blocked" sentinel cell so nav
// code that walks off the edge sees a wall rather than UB). Each Cell packs
// every piece of per-subtile state the callbacks below can report:
// SlbAtFlg_* map flags, SlabKind, owner, floor height, "unsafe" surface, and
// a single `walkable` bool that drives hug_can_move_on/is_valid_hug_subtile
// (inverted)/thing_in_wall_at consistently, so a test only has to poke one
// flag to build a wall a creature will actually collide with and hug along.
// Slab-level accessors (get_slabmap_block/get_slabmap_for_subtile/
// slabmap_block_kind/slabmap_owner) address the same per-subtile array via
// the slab's center subtile (slab_subtile_center) -- callers of set_slab()
// are expected to touch a whole 3x3 slab's worth of subtiles if they want
// internally-consistent data (the helper below does this).
//
// Every entry not overridden here is left at pathfinding_world_port's real
// default (a safe no-op/0/false/NULL/true-for-"is_invalid" -- see
// ports/pathfinding_world_port.def) via `fake = *pathfinding_world_port;`
// then overriding just the fields exercised, the same pattern already used
// by ariadne_test.cpp/ariadne_regions_test.cpp.
#ifndef KFX_PATHFINDING_TESTS_FAKE_WORLD_H
#define KFX_PATHFINDING_TESTS_FAKE_WORLD_H

#include "ports/pathfinding_world_port.h"
#include "ariadne_wallhug.h" // struct Navigation
#include "ariadne.h"         // struct Ariadne
#include "ariadne_update.h"  // ariadne_set_navigation_map_size/init_navigation, for TriangulatedWorldFixture
#include "kfx_pathfinding_state.h" // owner_player_navigating etc., reset per fixture
#include "thing_types.h"     // struct Thing
#include "map_types.h"       // struct Map
#include "slab_types.h"      // struct SlabMap
#include "bflib_math.h"      // LbArcTanAngle, ANGLE_MASK, DEGREES_45/90
#include "bflib_planar.h"    // chessboard_distance and friends (used by test bodies)
#include "config_terrain.h"  // SlbAtFlg_*

#include <cstdint>
#include <cstring>

namespace pf_fake {

constexpr int64_t kGridDim = 24; // subtiles per axis -- generous headroom for every test below

struct Cell {
    unsigned char map_flags = 0;       // SlbAtFlg_* bitmask -> map_block_flags
    SlabKind slab_kind = SlbT_PATH;    // -> slabmap_block_kind
    PlayerNumber owner = 0;            // -> slabmap_owner
    int64_t floor_filled_subtiles = 1;    // -> get_floor_filled_subtiles_at
    bool unsafe = false;               // -> subtile_is_unsafe
    bool walkable = true;              // drives hug_can_move_on / thing_in_wall_at(inverted) / is_valid_hug_subtile(inverted)
};

struct Grid {
    int64_t size_x = kGridDim;
    int64_t size_y = kGridDim;
    Cell cells[kGridDim][kGridDim];
    Cell oob_sentinel; // returned (by reference) for any out-of-bounds lookup
    // The real structs Ariadne dereferences, filled from cells[] on lookup.
    struct Map map_blocks[kGridDim][kGridDim];
    struct SlabMap slabs[kGridDim][kGridDim];
    // Off-map lookups get these, like kfx_sim's INVALID_MAP_BLOCK/
    // INVALID_SLABMAP_BLOCK: real structs, reported invalid, with the values
    // the old field callbacks returned for them (flags 0; kind SlbT_ROCK,
    // owner 0).
    struct Map oob_map_block;
    struct SlabMap oob_slab;

    void reset_open() {
        for (auto &row : cells) {
            for (auto &c : row) {
                c = Cell{};
            }
        }
        oob_sentinel = Cell{};
        oob_sentinel.walkable = false;
        size_x = kGridDim;
        size_y = kGridDim;
    }

    bool in_bounds(int64_t x, int64_t y) const {
        return x >= 0 && y >= 0 && x < size_x && y < size_y;
    }

    Cell &at(int64_t x, int64_t y) {
        if (!in_bounds(x, y)) {
            return oob_sentinel;
        }
        return cells[y][x];
    }

    // Sets every subtile in slab (slb_x,slb_y) consistently -- the shape
    // real map data always has (a whole slab shares kind/owner/blocking).
    void set_slab(int64_t slb_x, int64_t slb_y, SlabKind kind, PlayerNumber owner, bool walkable, unsigned char map_flags) {
        for (int64_t dy = 0; dy < STL_PER_SLB; dy++) {
            for (int64_t dx = 0; dx < STL_PER_SLB; dx++) {
                Cell &c = at(slb_x * STL_PER_SLB + dx, slb_y * STL_PER_SLB + dy);
                c.slab_kind = kind;
                c.owner = owner;
                c.walkable = walkable;
                c.map_flags = map_flags;
            }
        }
    }
};

inline Grid grid;

struct FakeThing {
    struct Thing tng; // must stay first: as_thing()/as_fake() cast between the two
    int64_t max_speed = 32;
    struct Navigation navi{};
    struct Ariadne arid{};
    FakeThing() {
        std::memset(&tng, 0, sizeof(tng));
        tng.index = 1; // clipbox_size_xy 0 picks nav-size table entry 0
    }
};

// Global knobs a test can poke before calling into production code -- kept
// separate from Cell so "how tall is the wall the creature just hit" can be
// controlled independently of the grid's per-subtile floor height.
inline MapCoord g_thing_height_at_reply = 0;
inline MapSubtlCoord g_map_size_z = 8;

inline struct Thing *as_thing(FakeThing &t) { return &t.tng; }
inline FakeThing &as_fake(struct Thing *t) { return *reinterpret_cast<FakeThing *>(t); }
inline const FakeThing &as_fake(const struct Thing *t) { return *reinterpret_cast<const FakeThing *>(t); }

// --- map/terrain -------------------------------------------------------
inline struct Map *fake_get_map_block_at(MapSubtlCoord stl_x, MapSubtlCoord stl_y) {
    if (!grid.in_bounds(stl_x, stl_y)) {
        std::memset(&grid.oob_map_block, 0, sizeof(grid.oob_map_block));
        return &grid.oob_map_block;
    }
    struct Map *mapblk = &grid.map_blocks[stl_y][stl_x];
    mapblk->flags = grid.at(stl_x, stl_y).map_flags;
    return mapblk;
}
inline TbBool fake_map_block_is_invalid(const struct Map *mapblk) { return (mapblk == nullptr) || (mapblk == &grid.oob_map_block); }

inline int64_t fake_get_floor_filled_subtiles_at(MapSubtlCoord stl_x, MapSubtlCoord stl_y) {
    return grid.at(stl_x, stl_y).floor_filled_subtiles;
}
inline TbBool fake_subtile_is_unsafe(MapSubtlCoord stl_x, MapSubtlCoord stl_y) {
    return grid.at(stl_x, stl_y).unsafe;
}

inline struct SlabMap *fake_get_slabmap_block(MapSlabCoord slb_x, MapSlabCoord slb_y) {
    if (slb_x < 0 || slb_y < 0 || slb_x * STL_PER_SLB >= grid.size_x || slb_y * STL_PER_SLB >= grid.size_y) {
        std::memset(&grid.oob_slab, 0, sizeof(grid.oob_slab));
        grid.oob_slab.kind = SlbT_ROCK;
        return &grid.oob_slab;
    }
    const Cell &c = grid.at(slab_subtile_center(slb_x), slab_subtile_center(slb_y));
    struct SlabMap *slb = &grid.slabs[slb_y][slb_x];
    slb->kind = c.slab_kind;
    slb->owner = c.owner;
    return slb;
}
inline struct SlabMap *fake_get_slabmap_for_subtile(MapSubtlCoord stl_x, MapSubtlCoord stl_y) {
    return fake_get_slabmap_block(subtile_slab(stl_x), subtile_slab(stl_y));
}
inline TbBool fake_slabmap_block_is_invalid(const struct SlabMap *slb) { return (slb == nullptr) || (slb == &grid.oob_slab); }
inline PlayerNumber fake_slabmap_owner(const struct SlabMap *slb) { return (slb == nullptr) ? 0 : slb->owner; }

inline TbBool fake_is_valid_hug_subtile(MapSubtlCoord stl_x, MapSubtlCoord stl_y, PlayerNumber) {
    // "valid hug subtile" == a wall the creature can hug along, i.e. NOT walkable.
    return !grid.at(stl_x, stl_y).walkable;
}
inline TbBool fake_hug_can_move_on(struct Thing *, MapSubtlCoord stl_x, MapSubtlCoord stl_y) {
    return grid.at(stl_x, stl_y).walkable;
}
inline TbBool fake_thing_in_wall_at(const struct Thing *, const struct Coord3d *pos) {
    return !grid.at(coord_subtile(pos->x.val), coord_subtile(pos->y.val)).walkable;
}

// --- doors -- left at pathfinding_world_port's real no-op defaults by every
// fixture below (no test here exercises door subtiles) ------------------

// --- single-purpose struct-Thing queries --------------------------------
inline int64_t fake_thing_is_invalid(const struct Thing *thing) { return thing == nullptr; }
inline int64_t fake_get_thing_height_at(const struct Thing *, const struct Coord3d *) { return g_thing_height_at_reply; }
inline int64_t fake_get_floor_height_under_thing_at(const struct Thing *, const struct Coord3d *) { return 0; }

// --- CreatureControl-embedded pathfinding slot --------------------------
inline struct Navigation *fake_creature_get_navigation(struct Thing *thing) { return &as_fake(thing).navi; }
inline struct Ariadne *fake_creature_get_ariadne_state(struct Thing *thing) { return &as_fake(thing).arid; }
inline int64_t fake_creature_get_max_speed(const struct Thing *thing) { return as_fake(thing).max_speed; }
inline void fake_creature_clear_state_flags_for_wallhug_override(struct Thing *) {}

// --- physical-split follow-ups ------------------------------------------
inline struct Map *fake_get_map_block_at_pos(SubtlCodedCoords stl_num) {
    return fake_get_map_block_at(ariadne_stl_num_decode_x(stl_num), ariadne_stl_num_decode_y(stl_num));
}

inline TbBool fake_creature_cannot_move_directly_to(struct Thing *, struct Coord3d *) { return false; }


// Builds a PathfindingWorldPort table with every grid/thing-backed
// field above wired in, starting from the real safe-default table for
// everything else (doors, players_are_mutual_allies, ...).
inline struct PathfindingWorldPort make_fake_callbacks() {
    struct PathfindingWorldPort fake = *pathfinding_world_port;
    fake.get_map_block_at = fake_get_map_block_at;
    fake.get_map_block_at_pos = fake_get_map_block_at_pos;
    fake.map_block_is_invalid = fake_map_block_is_invalid;
    fake.get_floor_filled_subtiles_at = fake_get_floor_filled_subtiles_at;
    fake.subtile_is_unsafe = fake_subtile_is_unsafe;
    fake.get_slabmap_block = fake_get_slabmap_block;
    fake.get_slabmap_for_subtile = fake_get_slabmap_for_subtile;
    fake.slabmap_block_is_invalid = fake_slabmap_block_is_invalid;
    fake.slabmap_owner = fake_slabmap_owner;
    fake.is_valid_hug_subtile = fake_is_valid_hug_subtile;
    fake.hug_can_move_on = fake_hug_can_move_on;
    fake.thing_in_wall_at = fake_thing_in_wall_at;
    fake.thing_is_invalid = fake_thing_is_invalid;
    fake.get_thing_height_at = fake_get_thing_height_at;
    fake.get_floor_height_under_thing_at = fake_get_floor_height_under_thing_at;
    fake.creature_get_navigation = fake_creature_get_navigation;
    fake.creature_get_ariadne_state = fake_creature_get_ariadne_state;
    fake.creature_get_max_speed = fake_creature_get_max_speed;
    fake.creature_clear_state_flags_for_wallhug_override = fake_creature_clear_state_flags_for_wallhug_override;
    fake.creature_cannot_move_directly_to = fake_creature_cannot_move_directly_to;
    return fake;
}

// Shared Catch2 fixture: resets the grid to an all-open, all-walkable
// floor, installs the fake callback table, restores the real one on
// teardown. Individual tests carve walls/doors/etc. out of the grid via
// grid.at()/grid.set_slab() before calling into production code.
struct GridWorldFixture {
    struct PathfindingWorldPort fake;
    GridWorldFixture() {
        grid.reset_open();
        g_thing_height_at_reply = 0;
        g_map_size_z = 8;
        ariadne_set_map_dimensions(grid.size_x, grid.size_y, g_map_size_z);
        kfx_pathfinding_state.owner_player_navigating = -1;
        kfx_pathfinding_state.nav_thing_can_travel_over_lava = 0;
        kfx_pathfinding_state.nav_thing_is_flying = 0;
        fake = make_fake_callbacks();
        set_pathfinding_world_port(&fake);
    }
    ~GridWorldFixture() {
        set_pathfinding_world_port(nullptr);
    }
};

// Extends GridWorldFixture with a small, uniform, all-open-floor map that
// has actually been through init_navigation()'s real triangulation --
// shared by ariadne_update_test.cpp (which drives init_navigation/
// update_navigation_triangulation directly) and ariadne_test.cpp (which
// needs a genuinely-triangulated Triangles[]/ari_Points[] to exercise
// ariadne.c's route-finding entry points, rather than the hand-poked
// 2-triangle fixtures ariadne_regions_test.cpp/ariadne_tringls_test.cpp
// use for their own narrower purposes).
//
// init_navigation()'s own triangulate_area() call always forces "whole
// map" mode (its start_x/start_y are always 0, which is always < 1,
// unconditionally re-deriving [0, get_map_size()+1) bounds regardless of
// what kfx_pathfinding_state.navigation_map_size_x/y were set to) -- so
// this sets the fake grid's logical map size *and* navigation_map_size to
// size+1 (matching that self-correction) purely so navmap_tile_number()'s
// row stride lines up with what the algorithm actually iterates, avoiding
// a silent index mismatch rather than an outright crash
// (kfx_pathfinding_state.navigation_map is a fixed 511*511 array either
// way).
struct TriangulatedWorldFixture : GridWorldFixture {
    static constexpr int64_t kLogicalSize = 8;
    TriangulatedWorldFixture() {
        grid.size_x = kLogicalSize;
        grid.size_y = kLogicalSize;
        for (int64_t y = 0; y < kLogicalSize; y++) {
            for (int64_t x = 0; x < kLogicalSize; x++) {
                grid.at(x, y).floor_filled_subtiles = 1;
                grid.at(x, y).unsafe = false;
                grid.at(x, y).map_flags = 0;
                grid.at(x, y).walkable = true;
            }
        }
        ariadne_set_navigation_map_size(kLogicalSize + 1, kLogicalSize + 1);
        ariadne_set_map_dimensions(kLogicalSize, kLogicalSize, g_map_size_z);
    }
};

} // namespace pf_fake

#endif
