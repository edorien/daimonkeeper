// kfx_pathfinding: shared reusable test fake for PathfindingWorldCallbacks
// (src/kfx_config/include/pathfinding_world.h), built out for
// ariadne_wallhug.c (previously zero coverage -- see ariadne_wallhug_test.cpp)
// and reused by ariadne_update_test.cpp for the triangulation entry points.
//
// Technique: struct Map/struct SlabMap/struct Navigation/struct Ariadne are
// forward-declared opaque in pathfinding_world.h -- ariadne only ever passes
// their pointers back through the callback table, never dereferences them.
// That means this fake is free to invent its own backing storage and encode
// a grid index straight into the "pointer" value (see encode_map/decode_map,
// encode_slab/decode_slab below) rather than allocate anything -- the
// pointer is never dereferenced by production code, only round-tripped
// through our own callbacks.
//
// struct Thing stays opaque the same way; FakeThing below is this test's own
// definition (never needs to match kfx_sim's real struct Thing layout) and
// every pointer of type "struct Thing *" handed to production code is really
// a `reinterpret_cast<struct Thing *>(&some_fake_thing)`, unwound the same
// way in the thing_* callbacks.
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
// Every callback not overridden here is left at pathfinding_world's real
// default (a safe no-op/0/false/NULL/true-for-"is_invalid" -- see
// src/kfx_config/src/pathfinding_world.c) via `fake = *pathfinding_world;`
// then overriding just the fields exercised, the same pattern already used
// by ariadne_test.cpp/ariadne_regions_test.cpp.
#ifndef KFX_PATHFINDING_TESTS_FAKE_WORLD_H
#define KFX_PATHFINDING_TESTS_FAKE_WORLD_H

#include "pathfinding_world.h"
#include "ariadne_wallhug.h" // struct Navigation
#include "ariadne.h"         // struct Ariadne
#include "ariadne_update.h"  // ariadne_set_navigation_map_size/init_navigation, for TriangulatedWorldFixture
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
    struct Coord3d pos{};
    int64_t move_angle = 0;
    int64_t index = 1;
    int64_t clipbox_size = 0; // clamps thing_nav_sizexy/thing_nav_block_sizexy to table entry 0
    PlayerNumber owner = 0;
    int64_t max_speed = 32;
    struct Navigation navi{};
    struct Ariadne arid{};
};

// Global knobs a test can poke before calling into production code -- kept
// separate from Cell so "how tall is the wall the creature just hit" can be
// controlled independently of the grid's per-subtile floor height.
inline MapCoord g_thing_height_at_reply = 0;
inline MapSubtlCoord g_map_size_z = 8;

inline struct Thing *as_thing(FakeThing &t) { return reinterpret_cast<struct Thing *>(&t); }
inline FakeThing &as_fake(struct Thing *t) { return *reinterpret_cast<FakeThing *>(t); }
inline const FakeThing &as_fake(const struct Thing *t) { return *reinterpret_cast<const FakeThing *>(t); }

// --- map/terrain -------------------------------------------------------
inline MapSubtlCoord fake_get_map_size_x(void) { return grid.size_x; }
inline MapSubtlCoord fake_get_map_size_y(void) { return grid.size_y; }
inline MapSubtlCoord fake_get_map_size_z(void) { return g_map_size_z; }

inline struct Map *fake_get_map_block_at(MapSubtlCoord stl_x, MapSubtlCoord stl_y) {
    if (!grid.in_bounds(stl_x, stl_y)) {
        return nullptr;
    }
    return reinterpret_cast<struct Map *>(static_cast<intptr_t>(stl_y * grid.size_x + stl_x + 1));
}
inline bool decode_map(const struct Map *m, int64_t &x, int64_t &y) {
    if (!m) {
        return false;
    }
    intptr_t idx = reinterpret_cast<intptr_t>(m) - 1;
    x = static_cast<int64_t>(idx % grid.size_x);
    y = static_cast<int64_t>(idx / grid.size_x);
    return true;
}
inline unsigned char fake_map_block_flags(const struct Map *mapblk) {
    int64_t x, y;
    if (!decode_map(mapblk, x, y)) {
        return 0;
    }
    return grid.at(x, y).map_flags;
}
inline TbBool fake_map_block_is_invalid(const struct Map *mapblk) { return mapblk == nullptr; }

inline int64_t fake_get_floor_filled_subtiles_at(MapSubtlCoord stl_x, MapSubtlCoord stl_y) {
    return grid.at(stl_x, stl_y).floor_filled_subtiles;
}
inline TbBool fake_subtile_is_unsafe(MapSubtlCoord stl_x, MapSubtlCoord stl_y) {
    return grid.at(stl_x, stl_y).unsafe;
}

inline struct SlabMap *encode_slab(int64_t slb_x, int64_t slb_y) {
    if (slb_x < 0 || slb_y < 0 || slb_x * STL_PER_SLB >= grid.size_x || slb_y * STL_PER_SLB >= grid.size_y) {
        return nullptr;
    }
    // Distinct index space from encode_map's -- never compared to a Map*.
    return reinterpret_cast<struct SlabMap *>(static_cast<intptr_t>(slb_y * 1000 + slb_x + 1));
}
inline bool decode_slab(const struct SlabMap *s, int64_t &slb_x, int64_t &slb_y) {
    if (!s) {
        return false;
    }
    intptr_t idx = reinterpret_cast<intptr_t>(s) - 1;
    slb_x = static_cast<int64_t>(idx % 1000);
    slb_y = static_cast<int64_t>(idx / 1000);
    return true;
}
inline struct SlabMap *fake_get_slabmap_block(MapSlabCoord slb_x, MapSlabCoord slb_y) { return encode_slab(slb_x, slb_y); }
inline struct SlabMap *fake_get_slabmap_for_subtile(MapSubtlCoord stl_x, MapSubtlCoord stl_y) {
    return encode_slab(subtile_slab(stl_x), subtile_slab(stl_y));
}
inline SlabKind fake_slabmap_block_kind(const struct SlabMap *slb) {
    int64_t slb_x, slb_y;
    if (!decode_slab(slb, slb_x, slb_y)) {
        return SlbT_ROCK;
    }
    return grid.at(slab_subtile_center(slb_x), slab_subtile_center(slb_y)).slab_kind;
}
inline TbBool fake_slabmap_block_is_invalid(const struct SlabMap *slb) { return slb == nullptr; }
inline PlayerNumber fake_slabmap_owner(const struct SlabMap *slb) {
    int64_t slb_x, slb_y;
    if (!decode_slab(slb, slb_x, slb_y)) {
        return 0;
    }
    return grid.at(slab_subtile_center(slb_x), slab_subtile_center(slb_y)).owner;
}

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

// --- doors -- left at pathfinding_world's real no-op defaults by every
// fixture below (no test here exercises door subtiles) ------------------

// --- single-purpose struct-Thing queries --------------------------------
inline int64_t fake_thing_is_invalid(const struct Thing *thing) { return thing == nullptr; }
inline PlayerNumber fake_thing_get_owner(const struct Thing *thing) { return as_fake(thing).owner; }
inline int64_t fake_get_thing_height_at(const struct Thing *, const struct Coord3d *) { return g_thing_height_at_reply; }
inline int64_t fake_get_floor_height_under_thing_at(const struct Thing *, const struct Coord3d *) { return 0; }

// --- Track 3: struct Thing field access ---------------------------------
inline struct Coord3d fake_thing_get_position(const struct Thing *thing) { return as_fake(thing).pos; }
inline void fake_thing_set_position(struct Thing *thing, const struct Coord3d *pos) { as_fake(thing).pos = *pos; }
inline int64_t fake_thing_get_move_angle(const struct Thing *thing) { return as_fake(thing).move_angle; }
inline void fake_thing_set_move_angle(struct Thing *thing, int64_t angle) { as_fake(thing).move_angle = angle; }
inline int64_t fake_thing_get_index(const struct Thing *thing) { return as_fake(thing).index; }
inline int64_t fake_thing_get_clipbox_size(const struct Thing *thing) { return as_fake(thing).clipbox_size; }

// --- CreatureControl-embedded pathfinding slot --------------------------
inline struct Navigation *fake_creature_get_navigation(struct Thing *thing) { return &as_fake(thing).navi; }
inline struct Ariadne *fake_creature_get_ariadne_state(struct Thing *thing) { return &as_fake(thing).arid; }
inline int64_t fake_creature_get_max_speed(const struct Thing *thing) { return as_fake(thing).max_speed; }
inline void fake_creature_clear_state_flags_for_wallhug_override(struct Thing *) {}

// --- physical-split follow-ups ------------------------------------------
inline SubtlCodedCoords fake_get_subtile_number(MapSubtlCoord stl_x, MapSubtlCoord stl_y) {
    if (stl_x > grid.size_x + 1) stl_x = grid.size_x + 1;
    if (stl_y > grid.size_y + 1) stl_y = grid.size_y + 1;
    if (stl_x < 0) stl_x = 0;
    if (stl_y < 0) stl_y = 0;
    return stl_y * (grid.size_x + 1) + stl_x;
}
inline MapSubtlCoord fake_stl_num_decode_x(SubtlCodedCoords stl_num) { return stl_num % (grid.size_x + 1); }
inline MapSubtlCoord fake_stl_num_decode_y(SubtlCodedCoords stl_num) { return (stl_num / (grid.size_x + 1)) % grid.size_y; }
inline MapSubtlCoord fake_stl_slab_center_subtile(MapSubtlCoord stl_v) { return slab_subtile_center(subtile_slab(stl_v)); }
inline struct Map *fake_get_map_block_at_pos(SubtlCodedCoords stl_num) {
    return fake_get_map_block_at(fake_stl_num_decode_x(stl_num), fake_stl_num_decode_y(stl_num));
}

inline TbBool fake_cross_x_boundary_first(const struct Coord3d *, const struct Coord3d *) { return false; }
inline TbBool fake_cross_y_boundary_first(const struct Coord3d *, const struct Coord3d *) { return false; }

inline const struct Around kSmallAroundTable[4] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
inline struct Around fake_get_small_around(SmallAroundIndex n) { return kSmallAroundTable[n & 3]; }
inline SmallAroundIndex fake_get_small_around_length(void) { return 4; }
inline SmallAroundIndex fake_small_around_index_in_direction(int64_t srcpos_x, int64_t srcpos_y, int64_t dstpos_x, int64_t dstpos_y) {
    // Same formula as kfx_sim's real small_around_index_in_direction
    // (src/kfx_sim/src/map_utils.c) -- reproduced here rather than faked
    // away, since it's pure math over LbArcTanAngle (kfx_platform, real,
    // already linked), not a piece of world state to stub out.
    int64_t i = ((LbArcTanAngle(dstpos_x - srcpos_x, dstpos_y - srcpos_y) & ANGLE_MASK) + DEGREES_45);
    return static_cast<SmallAroundIndex>((i / DEGREES_90) & 3);
}

inline TbBool fake_creature_cannot_move_directly_to(struct Thing *, struct Coord3d *) { return false; }

inline int64_t g_owner_player_navigating = -1;
inline int64_t fake_get_owner_player_navigating(void) { return g_owner_player_navigating; }
inline void fake_set_owner_player_navigating(int64_t plyr_idx) { g_owner_player_navigating = plyr_idx; }
inline int64_t g_nav_thing_can_travel_over_lava = 0;
inline int64_t fake_get_nav_thing_can_travel_over_lava(void) { return g_nav_thing_can_travel_over_lava; }
inline void fake_set_nav_thing_can_travel_over_lava(int64_t can_travel) { g_nav_thing_can_travel_over_lava = can_travel; }

// Builds a PathfindingWorldCallbacks table with every grid/thing-backed
// field above wired in, starting from the real safe-default table for
// everything else (doors, players_are_mutual_allies, ...).
inline struct PathfindingWorldCallbacks make_fake_callbacks() {
    struct PathfindingWorldCallbacks fake = *pathfinding_world;
    fake.get_map_size_x = fake_get_map_size_x;
    fake.get_map_size_y = fake_get_map_size_y;
    fake.get_map_size_z = fake_get_map_size_z;
    fake.get_map_block_at = fake_get_map_block_at;
    fake.get_map_block_at_pos = fake_get_map_block_at_pos;
    fake.map_block_flags = fake_map_block_flags;
    fake.map_block_is_invalid = fake_map_block_is_invalid;
    fake.get_floor_filled_subtiles_at = fake_get_floor_filled_subtiles_at;
    fake.subtile_is_unsafe = fake_subtile_is_unsafe;
    fake.get_slabmap_block = fake_get_slabmap_block;
    fake.get_slabmap_for_subtile = fake_get_slabmap_for_subtile;
    fake.slabmap_block_kind = fake_slabmap_block_kind;
    fake.slabmap_block_is_invalid = fake_slabmap_block_is_invalid;
    fake.slabmap_owner = fake_slabmap_owner;
    fake.is_valid_hug_subtile = fake_is_valid_hug_subtile;
    fake.hug_can_move_on = fake_hug_can_move_on;
    fake.thing_in_wall_at = fake_thing_in_wall_at;
    fake.thing_is_invalid = fake_thing_is_invalid;
    fake.thing_get_owner = fake_thing_get_owner;
    fake.get_thing_height_at = fake_get_thing_height_at;
    fake.get_floor_height_under_thing_at = fake_get_floor_height_under_thing_at;
    fake.thing_get_position = fake_thing_get_position;
    fake.thing_set_position = fake_thing_set_position;
    fake.thing_get_move_angle = fake_thing_get_move_angle;
    fake.thing_set_move_angle = fake_thing_set_move_angle;
    fake.thing_get_index = fake_thing_get_index;
    fake.thing_get_clipbox_size = fake_thing_get_clipbox_size;
    fake.creature_get_navigation = fake_creature_get_navigation;
    fake.creature_get_ariadne_state = fake_creature_get_ariadne_state;
    fake.creature_get_max_speed = fake_creature_get_max_speed;
    fake.creature_clear_state_flags_for_wallhug_override = fake_creature_clear_state_flags_for_wallhug_override;
    fake.get_subtile_number = fake_get_subtile_number;
    fake.stl_num_decode_x = fake_stl_num_decode_x;
    fake.stl_num_decode_y = fake_stl_num_decode_y;
    fake.stl_slab_center_subtile = fake_stl_slab_center_subtile;
    fake.cross_x_boundary_first = fake_cross_x_boundary_first;
    fake.cross_y_boundary_first = fake_cross_y_boundary_first;
    fake.get_small_around = fake_get_small_around;
    fake.get_small_around_length = fake_get_small_around_length;
    fake.small_around_index_in_direction = fake_small_around_index_in_direction;
    fake.creature_cannot_move_directly_to = fake_creature_cannot_move_directly_to;
    fake.get_owner_player_navigating = fake_get_owner_player_navigating;
    fake.set_owner_player_navigating = fake_set_owner_player_navigating;
    fake.get_nav_thing_can_travel_over_lava = fake_get_nav_thing_can_travel_over_lava;
    fake.set_nav_thing_can_travel_over_lava = fake_set_nav_thing_can_travel_over_lava;
    return fake;
}

// Shared Catch2 fixture: resets the grid to an all-open, all-walkable
// floor, installs the fake callback table, restores the real one on
// teardown. Individual tests carve walls/doors/etc. out of the grid via
// grid.at()/grid.set_slab() before calling into production code.
struct GridWorldFixture {
    struct PathfindingWorldCallbacks fake;
    GridWorldFixture() {
        grid.reset_open();
        g_thing_height_at_reply = 0;
        g_map_size_z = 8;
        g_owner_player_navigating = -1;
        g_nav_thing_can_travel_over_lava = 0;
        fake = make_fake_callbacks();
        set_pathfinding_world_callbacks(&fake);
    }
    ~GridWorldFixture() {
        set_pathfinding_world_callbacks(nullptr);
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
    }
};

} // namespace pf_fake

#endif
