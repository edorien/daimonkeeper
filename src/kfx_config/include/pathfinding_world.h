/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file pathfinding_world.h
 *     Header file for pathfinding_world.c.
 * @par Purpose:
 *     Callback-registration interface letting ariadne (bundled inside
 *     kfx_sim today, see docs/refactor/stage-06a-ariadne-pathfinding-
 *     interface.md) query map/door/creature state without a direct
 *     #include of the kfx_sim headers that own it. Covers Track 2 (map/door
 *     predicates, the per-creature CreatureControl-embedded pathfinding
 *     slot, and single-purpose struct-Thing queries that are mechanical
 *     function-call wraps regardless of call count) and Track 3 (direct
 *     struct-Thing position/move-angle/index/clipbox field access, read
 *     *and written* throughout ariadne's wall-hug collision code -- the
 *     large, higher-risk piece deliberately deferred out of Track 2).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_PATHFINDING_WORLD_H
#define DK_PATHFINDING_WORLD_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct Thing;
struct Coord3d;
struct Map;
struct SlabMap;
struct Navigation; /* defined in kfx_sim's ariadne_wallhug.h -- opaque here */
struct Ariadne;    /* defined in kfx_sim's ariadne.h -- opaque here */

struct PathfindingWorldCallbacks {
    /* map/terrain -- struct Map/struct SlabMap stay opaque; ariadne never
       sees their layout, only passes the pointer back through the
       accessors below (same shape as every other opaque-handle callback
       in this codebase, e.g. SimFeedbackCallbacks::get_local_camera). */
    MapSubtlCoord   (*get_map_size_x)(void);
    MapSubtlCoord   (*get_map_size_y)(void);
    struct Map     *(*get_map_block_at)(MapSubtlCoord stl_x, MapSubtlCoord stl_y);
    struct Map     *(*get_map_block_at_pos)(SubtlCodedCoords stl_num);
    unsigned char   (*map_block_flags)(const struct Map *mapblk);
    TbBool          (*map_block_is_invalid)(const struct Map *mapblk);
    int64_t            (*get_floor_filled_subtiles_at)(MapSubtlCoord stl_x, MapSubtlCoord stl_y);
    TbBool          (*subtile_is_unsafe)(MapSubtlCoord stl_x, MapSubtlCoord stl_y);
    struct SlabMap *(*get_slabmap_block)(MapSlabCoord slb_x, MapSlabCoord slb_y);
    SlabKind        (*slabmap_block_kind)(const struct SlabMap *slb);
    TbBool          (*slabmap_block_is_invalid)(const struct SlabMap *slb);
    PlayerNumber    (*slabmap_owner)(const struct SlabMap *slb);
    TbBool          (*is_valid_hug_subtile)(MapSubtlCoord stl_x, MapSubtlCoord stl_y, PlayerNumber plyr_idx);
    TbBool          (*subtile_is_door)(MapSubtlCoord stl_x, MapSubtlCoord stl_y);
    TbBool          (*thing_in_wall_at)(const struct Thing *thing, const struct Coord3d *pos);

    /* doors */
    struct Thing *(*get_door_for_position)(MapSubtlCoord stl_x, MapSubtlCoord stl_y);
    TbBool        (*door_is_hidden_to_player)(struct Thing *doortng, PlayerNumber plyr_idx);
    TbBool        (*door_will_open_for_thing)(const struct Thing *doortng, const struct Thing *creatng);
    TbBool        (*door_is_locked)(const struct Thing *doortng);
    TbBool        (*players_are_mutual_allies)(PlayerNumber plyr1_idx, PlayerNumber plyr2_idx);

    /* single-purpose struct-Thing queries -- mechanical 1:1 call-site
       substitutions no matter the call count, unlike the position/angle
       field access deferred to Track 3 */
    int64_t          (*thing_is_invalid)(const struct Thing *thing);
    PlayerNumber   (*thing_get_owner)(const struct Thing *thing);
    int64_t           (*get_thing_height_at)(const struct Thing *thing, const struct Coord3d *pos);
    int64_t           (*get_floor_height_under_thing_at)(const struct Thing *thing, const struct Coord3d *pos);
    TbBool         (*creature_can_travel_over_lava)(const struct Thing *creatng);
    TbBool         (*thing_is_flying)(const struct Thing *thing); /* movement_flags & TMvF_Flying */
    const char    *(*thing_model_name)(const struct Thing *thing); /* debug logging only */

    /* Track 3 -- struct Thing position/angle/index/clipbox field access.
       Value get/set pairs (stage-06a §6.3 Option 1), not a live pointer
       into kfx_sim's struct Thing: every write goes through a real setter,
       even for what was originally a single-field partial write
       (creatng->mappos.z.val = h -> get, mutate the local copy, set). */
    struct Coord3d (*thing_get_position)(const struct Thing *thing);
    void           (*thing_set_position)(struct Thing *thing, const struct Coord3d *pos);
    int64_t          (*thing_get_move_angle)(const struct Thing *thing);
    void           (*thing_set_move_angle)(struct Thing *thing, int64_t angle);
    int64_t (*thing_get_index)(const struct Thing *thing);
    int64_t (*thing_get_clipbox_size)(const struct Thing *thing);

    /* the CreatureControl-embedded pathfinding slot (see stage-06a §5) */
    struct Navigation *(*creature_get_navigation)(struct Thing *creatng);      /* &cctrl->navi */
    struct Ariadne     *(*creature_get_ariadne_state)(struct Thing *creatng);  /* &cctrl->arid */
    int64_t               (*creature_get_max_speed)(const struct Thing *creatng);
    void                (*creature_clear_state_flags_for_wallhug_override)(struct Thing *creatng);
        /* wraps the two direct writes: cctrl->creature_state_flags = 0; cctrl->combat_flags = 0; */

    /* Physical-split follow-ups: real (non-pure, kfx_sim-state-reading)
       dependencies surfaced only once ariadne.c/ariadne_update.c lost the
       transitive #include umbrella that kfx_sim_state.h used to pull in.
       Pure/stateless helpers (coordinate math macros, the SlabBlockedFlags
       enum) were relocated to kfx_platform/globals.h instead of wrapped
       here -- see stage-06a §14 as-built notes. */
    SubtlCodedCoords (*get_subtile_number)(MapSubtlCoord stl_x, MapSubtlCoord stl_y);
    MapSubtlCoord    (*stl_num_decode_x)(SubtlCodedCoords stl_num);
    MapSubtlCoord    (*stl_num_decode_y)(SubtlCodedCoords stl_num);
    MapSubtlCoord    (*stl_slab_center_subtile)(MapSubtlCoord stl_v);
    struct SlabMap  *(*get_slabmap_for_subtile)(MapSubtlCoord stl_x, MapSubtlCoord stl_y);
    TbBool           (*hug_can_move_on)(struct Thing *creatng, MapSubtlCoord stl_x, MapSubtlCoord stl_y);
    TbBool           (*cross_x_boundary_first)(const struct Coord3d *pos1, const struct Coord3d *pos2);
    TbBool           (*cross_y_boundary_first)(const struct Coord3d *pos1, const struct Coord3d *pos2);
    struct Around    (*get_small_around)(SmallAroundIndex n); /* small_around[n], map_utils.c */
    SmallAroundIndex (*get_small_around_length)(void); /* SMALL_AROUND_LENGTH, map_utils.h */
    SmallAroundIndex (*small_around_index_in_direction)(int64_t srcpos_x, int64_t srcpos_y, int64_t dstpos_x, int64_t dstpos_y);
    MapSubtlCoord    (*get_map_size_z)(void); /* map_subtiles_z, map_data.c */
    TbBool           (*creature_cannot_move_directly_to)(struct Thing *thing, struct Coord3d *pos);

    /* owner_player_navigating / nav_thing_can_travel_over_lava: plain
       globals (thing_navigate.c) read+written by BOTH kfx_sim
       (thing_navigate.c, creature_states.c) and ariadne -- shared mutable
       scratch state, not something either side owns exclusively, so it
       stays put and is reached through a getter/setter pair like every
       other cross-layer write in this interface. */
    int64_t  (*get_owner_player_navigating)(void);
    void  (*set_owner_player_navigating)(int64_t plyr_idx);
    int64_t  (*get_nav_thing_can_travel_over_lava)(void);
    void  (*set_nav_thing_can_travel_over_lava)(int64_t can_travel);
    int64_t  (*get_nav_thing_is_flying)(void);
    void  (*set_nav_thing_is_flying)(int64_t is_flying);

    /* map_columns.h -- Abyss dungeons (stage: Dungeons may now reach into
       the Abyss, #5169): ariadne_update.c needs to know whether a subtile
       has abyss on top of it to mark it NAVMAP_ABYSS in the nav tree. */
    TbBool (*subtile_has_abyss_on_top)(MapSubtlCoord stl_x, MapSubtlCoord stl_y);

    /* thing_navigate.h -- #5310 (abyss/lava wallhug): true when an uncontrolled creature
       standing on safe terrain would step onto terrain that is toxic for it at `pos`.
       ariadne_creature_blocked_by_wall_at() treats that as a wall. */
    TbBool (*creature_steps_into_toxic_terrain)(struct Thing *thing, const struct Coord3d *pos);
};
void set_pathfinding_world_callbacks(const struct PathfindingWorldCallbacks *callbacks);
extern const struct PathfindingWorldCallbacks *pathfinding_world;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
