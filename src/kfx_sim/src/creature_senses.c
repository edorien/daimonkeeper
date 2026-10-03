/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file creature_senses.c
 *     Functions to check vision, hearing and other senses of creatures.
 * @par Purpose:
 *     Creature senses checks and handling.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     27 Nov 2011 - 22 Jan 2013
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "creature_senses.h"
#include "globals.h"

#include "bflib_math.h"
#include "bflib_planar.h"
#include "creature_states.h"
#include "thing_list.h"
#include "thing_navigate.h"
#include "thing_stats.h"
#include "creature_control.h"
#include "player_instances.h"
#include "config_creature.h"
#include "config_rules.h"
#include "config_settings.h"
#include "map_blocks.h"
#include "map_data.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "post_inc.h"

// Use values of 21 and below, otherwise you may need more rays to explore the entire distance
const int64_t CREATURE_EXPLORE_DISTANCE = 7;
const int64_t CREATURE_EXPLORE_DISTANCE_POSSESSED = 10;

/******************************************************************************/
/******************************************************************************/
/*
 * Refactor pass 3, S02: the 3D line-of-sight variants share one stepper,
 * los_walk(), and one diagonal-corner check, sibling_walk(). Each public
 * function below is its variant's rules: which points count as solid, when
 * a door at the target slab blocks, and nowibble_line_of_sight_3d's own
 * start and end. The line's height goes straight to the target's and stops
 * there; with the CROOKED_SIGHT_LINES classic bug it steps as it used to
 * (pass 3 finding F3): line_of_sight_3d kept the first step's height until
 * one more step would pass the target's, the others could pass it.
 */
struct LosContext {
    const struct Thing *door; /**< the door a variant ignores, or INVALID_THING */
    PlayerNumber plyr_idx;    /**< the player whose own doors a variant ignores */
};

typedef TbBool (*LosPointBlocked)(const struct Coord3d *pos, const struct LosContext *ctx);

enum SiblingDoorRule {
    SibDoor_UnlessDoorGiven = 0, /**< a door at the target slab blocks unless a door is given (thing_is_invalid) */
    SibDoor_UnlessDoorExists,    /**< the same, testing thing_exists() */
    SibDoor_Always,              /**< a door at the target slab always blocks */
};

struct SiblingRules {
    unsigned char door_rule; /**< enum SiblingDoorRule */
    unsigned char y_first;   /**< test the corner beside y before the one beside x */
    unsigned char log;       /**< log which corner blocked */
    LosPointBlocked blocked;
};

struct LosRules {
    LosPointBlocked blocked;             /**< is the next point on the line solid */
    const struct SiblingRules *sibling;  /**< the diagonal-corner check between two points */
    unsigned char z_clamp;               /**< with CROOKED_SIGHT_LINES, line_of_sight_3d's stepping: z stops at the
                                              target's height, and past the first step only moves when it gets there */
    unsigned char nowibble;              /**< start one unit towards the target, take one step fewer */
};

static TbBool los_solid(const struct Coord3d *pos, const struct LosContext *ctx)
{
    return point_in_map_is_solid(pos);
}

static TbBool los_solid_ignoring_door(const struct Coord3d *pos, const struct LosContext *ctx)
{
    return point_in_map_is_solid_ignoring_door(pos, ctx->door);
}

static TbBool los_solid_flags_ignoring_door(const struct Coord3d *pos, const struct LosContext *ctx)
{
    return (get_point_in_map_solid_flags_ignoring_door(pos, ctx->door) & 0x01) != 0;
}

static TbBool los_solid_flags_ignoring_own_door(const struct Coord3d *pos, const struct LosContext *ctx)
{
    return (get_point_in_map_solid_flags_ignoring_own_door(pos, ctx->plyr_idx) & 0x01) != 0;
}

/**
 * Whether the line may pass from prevpos to the neighbouring nextpos: a diagonal
 * step is blocked when either subtile beside it is solid.
 */
static inline TbBool sibling_walk(const struct Coord3d *prevpos, const struct Coord3d *nextpos,
    const struct SiblingRules *rules, const struct LosContext *ctx)
{
    TbBool check_door;
    switch (rules->door_rule)
    {
    case SibDoor_UnlessDoorGiven:
        check_door = thing_is_invalid(ctx->door);
        break;
    case SibDoor_UnlessDoorExists:
        check_door = !thing_exists(ctx->door);
        break;
    default:
        check_door = true;
        break;
    }
    // Check for door at central subtile
    if (check_door && subtile_is_door(stl_slab_center_subtile(nextpos->x.stl.num), stl_slab_center_subtile(nextpos->y.stl.num))) {
        return false;
    }
    // If only one dimensions changed, allow the pass
    // (in that case the outcome has been decided before this call)
    if ((nextpos->x.stl.num == prevpos->x.stl.num) ||
        (nextpos->y.stl.num == prevpos->y.stl.num)) {
        return true;
    }
    MapSubtlDelta subdelta_x = (nextpos->x.stl.num - (MapSubtlDelta)prevpos->x.stl.num);
    MapSubtlDelta subdelta_y = (nextpos->y.stl.num - (MapSubtlDelta)prevpos->y.stl.num);
    MapCoordDelta side_x;
    MapCoordDelta side_y;
    switch (subdelta_x + 2 * subdelta_y)
    {
    case -3: // change is (-1,-1)
        side_x = -COORD_PER_STL;
        side_y = -COORD_PER_STL;
        break;
    case -1: // change is (1,-1) as (-1,0) was eliminated earlier
        side_x = COORD_PER_STL;
        side_y = -COORD_PER_STL;
        break;
    case 1: // change is (-1,1) as (1,0) was eliminated earlier
        side_x = -COORD_PER_STL;
        side_y = COORD_PER_STL;
        break;
    case 3: // change is (1,1)
        side_x = COORD_PER_STL;
        side_y = COORD_PER_STL;
        break;
    default:
        ERRORDBG(8,"Invalid use of sibling function, delta (%" PRId64 ",%" PRId64 ")",(int64_t)subdelta_x,(int64_t)subdelta_y);
        return true;
    }
    struct Coord3d posmvx;
    posmvx.x.val = prevpos->x.val + side_x;
    posmvx.y.val = prevpos->y.val;
    posmvx.z.val = prevpos->z.val;
    struct Coord3d posmvy;
    posmvy.x.val = prevpos->x.val;
    posmvy.y.val = prevpos->y.val + side_y;
    posmvy.z.val = prevpos->z.val;
    const struct Coord3d *first = rules->y_first ? &posmvy : &posmvx;
    const struct Coord3d *second = rules->y_first ? &posmvx : &posmvy;
    if (rules->blocked(first, ctx)) {
        if (rules->log)
            SYNCDBG(17, "Cannot see through (%" PRId64 ",%" PRId64 ") with delta (%" PRId64 ",%" PRId64 ") %s",(int64_t)first->x.stl.num,(int64_t)first->y.stl.num,(int64_t)subdelta_x,(int64_t)subdelta_y, rules->y_first ? "Y" : "X");
        return false;
    }
    if (rules->blocked(second, ctx)) {
        if (rules->log)
            SYNCDBG(17, "Cannot see through (%" PRId64 ",%" PRId64 ") with delta (%" PRId64 ",%" PRId64 ") %s",(int64_t)second->x.stl.num,(int64_t)second->y.stl.num,(int64_t)subdelta_x,(int64_t)subdelta_y, rules->y_first ? "X" : "Y");
        return false;
    }
    return true;
}

/** The line's height one step on: increase_z further, but not past the target's height. */
static inline MapCoord los_step_z(MapCoord z, MapCoord increase_z, MapCoord target_z)
{
    const MapCoord next = z + increase_z;
    if (((increase_z > 0) && (next > target_z)) || ((increase_z < 0) && (next < target_z)))
        return target_z;
    return next;
}

/** True if nothing blocks the line from frpos to topos, stepping one subtile at a time along its longer axis. */
static inline TbBool los_walk(const struct Coord3d *frpos, const struct Coord3d *topos,
    const struct LosRules *rules, const struct LosContext *ctx)
{
    MapCoordDelta dx = topos->x.val - (MapCoordDelta)frpos->x.val;
    MapCoordDelta dy = topos->y.val - (MapCoordDelta)frpos->y.val;
    MapCoordDelta dz = topos->z.val - (MapCoordDelta)frpos->z.val;
    // Allow the travel to the same subtile
    if ((topos->x.stl.num == frpos->x.stl.num) &&
        (topos->y.stl.num == frpos->y.stl.num)) {
        return true;
    }
    // Initialize increases and do abs() of dx,dy and dz
    MapCoord increase_x;
    MapCoord increase_y;
    MapCoord increase_z;
    MapSubtlCoord distance;
    if (dx >= 0) {
        increase_x = COORD_PER_STL;
    } else {
        dx = -dx;
        increase_x = -COORD_PER_STL;
    }
    if (dy >= 0) {
        increase_y = COORD_PER_STL;
    } else {
        dy = -dy;
        increase_y = -COORD_PER_STL;
    }
    if (dz >= 0) {
        increase_z = COORD_PER_STL;
    } else {
        dz = -dz;
        increase_z = -COORD_PER_STL;
    }
    { // Compute amount of steps for the loop
        int64_t maxdim1;
        int64_t maxdim2;
        if (dy == dx)
        {
            increase_z = increase_z * dz / dx;
            maxdim1 = frpos->x.stl.num;
            maxdim2 = topos->x.stl.num;
        }
        else
        if (dy > dx)
        {
            increase_x = dx * increase_x / dy;
            increase_z = increase_z * dz / dy;
            maxdim1 = frpos->y.stl.num;
            maxdim2 = topos->y.stl.num;
        } else
        {
            increase_y = increase_y * dy / dx;
            increase_z = increase_z * dz / dx;
            maxdim1 = frpos->x.stl.num;
            maxdim2 = topos->x.stl.num;
        }
        distance = llabs(maxdim2 - maxdim1);
        if (rules->nowibble) {
            // One step fewer, to avoid floor/wall wibble lumps that block the explosion
            // happening "within" them
            distance--;
        }
    }
    // Go through the distance with given increases
    struct Coord3d prevpos;
    prevpos.x.val = frpos->x.val;
    prevpos.y.val = frpos->y.val;
    prevpos.z.val = frpos->z.val;
    if (rules->nowibble)
    {
        // Start one unit towards the target on each axis that moves
        if (increase_x != 0)
            prevpos.x.val += (increase_x / llabs(increase_x));
        if (increase_y != 0)
            prevpos.y.val += (increase_y / llabs(increase_y));
        if (increase_z != 0)
            prevpos.z.val += (increase_z / llabs(increase_z));
    }
    // CROOKED_SIGHT_LINES: each variant's old height stepping (pass 3 finding F3); otherwise a straight line
    const TbBool crooked = flag_is_set(kfx_config_state.conf.rules[0].gameplay.classic_bugs_flags, ClscBug_CrookedSightLines);
    const TbBool z_clamp = crooked && rules->z_clamp;
    struct Coord3d nextpos;
    nextpos.x.val = prevpos.x.val + increase_x;
    nextpos.y.val = prevpos.y.val + increase_y;
    if (!crooked)
    {
        nextpos.z.val = los_step_z(prevpos.z.val, increase_z, topos->z.val);
    }
    else
    if (z_clamp && ((increase_z >= 0 && ((prevpos.z.val + increase_z) >= topos->z.val)) ||
        (increase_z < 0 && ((prevpos.z.val + increase_z) < topos->z.val))))
    {
        // Z position overshoots, which returns incorrect results. Workaround until a proper fix is made
        nextpos.z.val = topos->z.val;
        increase_z = 0;
    }
    else
    {
        nextpos.z.val = prevpos.z.val + increase_z;
    }
    while (distance > 0)
    {
        if (rules->blocked(&nextpos, ctx)) {
            SYNCDBG(17, "Cannot see through (%" PRId64 ",%" PRId64 ") due to linear path solid flags (downcount %" PRId64 ")",
                (int64_t)nextpos.x.stl.num,(int64_t)nextpos.y.stl.num,(int64_t)distance);
            return false;
        }
        if (!sibling_walk(&prevpos, &nextpos, rules->sibling, ctx)) {
            SYNCDBG(17, "Cannot see through (%" PRId64 ",%" PRId64 ") due to 3D line of sight (downcount %" PRId64 ")",
                (int64_t)nextpos.x.stl.num,(int64_t)nextpos.y.stl.num,(int64_t)distance);
            return false;
        }
        // Go to next sibling subtile
        prevpos.x.val = nextpos.x.val;
        prevpos.y.val = nextpos.y.val;
        prevpos.z.val = nextpos.z.val;
        nextpos.x.val += increase_x;
        nextpos.y.val += increase_y;
        if (!crooked)
        {
            nextpos.z.val = los_step_z(nextpos.z.val, increase_z, topos->z.val);
        }
        else
        if (z_clamp)
        {
            // Z doesn't advance here: it only moves to the target's height once one more step would overshoot it
            if ((increase_z >= 0 && ((nextpos.z.val + increase_z) >= topos->z.val)) ||
                (increase_z < 0 && ((nextpos.z.val + increase_z) < topos->z.val)))
            {
                nextpos.z.val = topos->z.val;
                increase_z = 0;
            }
        } else
        {
            nextpos.z.val += increase_z;
        }
        distance--;
    }
    return true;
}

static const struct SiblingRules sibling_ignoring_door_rules = { SibDoor_UnlessDoorGiven, 0, 1, los_solid_ignoring_door };
static const struct SiblingRules sibling_lava_ignoring_door_rules = { SibDoor_UnlessDoorExists, 0, 0, los_solid_flags_ignoring_door };
static const struct SiblingRules sibling_lava_ignoring_own_door_rules = { SibDoor_Always, 1, 0, los_solid_flags_ignoring_own_door };

TbBool sibling_line_of_sight_ignoring_door(const struct Coord3d *prevpos,
    const struct Coord3d *nextpos, const struct Thing *doortng)
{
    const struct LosContext ctx = { doortng, 0 };
    return sibling_walk(prevpos, nextpos, &sibling_ignoring_door_rules, &ctx);
}


TbBool line_of_sight_3d_ignoring_specific_door(const struct Coord3d *frpos,
    const struct Coord3d *topos, const struct Thing *doortng)
{
    static const struct LosRules rules = { los_solid_ignoring_door, &sibling_ignoring_door_rules, 0, 0 };
    const struct LosContext ctx = { doortng, 0 };
    return los_walk(frpos, topos, &rules, &ctx);
}

TbBool sibling_line_of_sight_3d_including_lava_check_ignoring_door(const struct Coord3d *prevpos,
    const struct Coord3d *nextpos, const struct Thing *doortng)
{
    const struct LosContext ctx = { doortng, 0 };
    return sibling_walk(prevpos, nextpos, &sibling_lava_ignoring_door_rules, &ctx);
}

TbBool jonty_line_of_sight_3d_including_lava_check_ignoring_specific_door(const struct Coord3d *frpos,
    const struct Coord3d *topos, const struct Thing *doortng)
{
    static const struct LosRules rules = { los_solid_flags_ignoring_door, &sibling_lava_ignoring_door_rules, 0, 0 };
    const struct LosContext ctx = { doortng, 0 };
    return los_walk(frpos, topos, &rules, &ctx);
}

TbBool sibling_line_of_sight_3d_including_lava_check_ignoring_own_door(const struct Coord3d *prevpos,
    const struct Coord3d *nextpos, PlayerNumber plyr_idx)
{
    const struct LosContext ctx = { INVALID_THING, plyr_idx };
    return sibling_walk(prevpos, nextpos, &sibling_lava_ignoring_own_door_rules, &ctx);
}

TbBool jonty_line_of_sight_3d_including_lava_check_ignoring_own_door(const struct Coord3d *frpos,
    const struct Coord3d *topos, PlayerNumber plyr_idx)
{
    static const struct LosRules rules = { los_solid_flags_ignoring_own_door, &sibling_lava_ignoring_own_door_rules, 0, 0 };
    const struct LosContext ctx = { INVALID_THING, plyr_idx };
    return los_walk(frpos, topos, &rules, &ctx);
}

TbBool creature_can_see_thing(struct Thing *creatng, struct Thing *thing)
{
    struct Coord3d thing_pos;
    struct Coord3d creat_pos;

    creat_pos.x.val = creatng->mappos.x.val;
    creat_pos.y.val = creatng->mappos.y.val;
    creat_pos.z.val = creatng->mappos.z.val;

    thing_pos.x.val = thing->mappos.x.val;
    thing_pos.y.val = thing->mappos.y.val;
    thing_pos.z.val = thing->mappos.z.val;

    creat_pos.z.val += get_creature_eye_height(creatng);

    if (line_of_sight_3d(&creat_pos, &thing_pos))
        return 1;
    thing_pos.z.val += thing->clipbox_size_z;
    return line_of_sight_3d(&creat_pos, &thing_pos) != 0;
}

TbBool creature_can_see_thing_ignoring_specific_door(struct Thing *creatng, struct Thing *thing,struct Thing *doortng)
{
    struct Coord3d thing_pos;
    struct Coord3d creat_pos;

    creat_pos.x.val = creatng->mappos.x.val;
    creat_pos.y.val = creatng->mappos.y.val;
    creat_pos.z.val = creatng->mappos.z.val;

    thing_pos.x.val = thing->mappos.x.val;
    thing_pos.y.val = thing->mappos.y.val;
    thing_pos.z.val = thing->mappos.z.val;

    creat_pos.z.val += get_creature_eye_height(creatng);

    if (line_of_sight_3d(&creat_pos, &thing_pos))
        return 1;
    thing_pos.z.val += thing->clipbox_size_z;
    return line_of_sight_3d_ignoring_specific_door(&creat_pos, &thing_pos,doortng) != 0;
}

TbBool jonty_creature_can_see_thing_including_lava_check(const struct Thing *creatng, const struct Thing *thing)
{
    const struct Coord3d* srcpos = &creatng->mappos;
    struct Coord3d eyepos;
    eyepos.x.val = srcpos->x.val;
    eyepos.y.val = srcpos->y.val;
    eyepos.z.val = srcpos->z.val;
    struct Coord3d tgtpos;
    tgtpos.x.val = thing->mappos.x.val;
    tgtpos.y.val = thing->mappos.y.val;
    tgtpos.z.val = thing->mappos.z.val;
    eyepos.z.val += get_creature_eye_height(creatng);
    if (thing->class_id == TCls_Door)
    {
        // If we're immune to lava, or we're already on it - don't care, travel over it
        if (lava_at_position(srcpos) || creature_can_travel_over_lava(creatng))
        {
            SYNCDBG(17, "The %s index %" PRId64 " owned by player %" PRId64 " checks w/o lava %s index %" PRId64,
                thing_model_name(creatng),(int64_t)creatng->index,(int64_t)creatng->owner,thing_model_name(thing),(int64_t)thing->index);
            // Check bottom of the thing
            if (line_of_sight_3d_ignoring_specific_door(&eyepos, &tgtpos, thing))
                return true;
            // Check top of the thing
            tgtpos.z.val += thing->clipbox_size_z;
            if (line_of_sight_3d_ignoring_specific_door(&eyepos, &tgtpos, thing))
                return true;
            return false;
        } else
        {
            SYNCDBG(17, "The %s index %" PRId64 " owned by player %" PRId64 " checks with lava %s index %" PRId64,
                thing_model_name(creatng),(int64_t)creatng->index,(int64_t)creatng->owner,thing_model_name(thing),(int64_t)thing->index);
            // Check bottom of the thing
            if (jonty_line_of_sight_3d_including_lava_check_ignoring_specific_door(&eyepos, &tgtpos, thing))
                return true;
            // Check top of the thing
            tgtpos.z.val += thing->clipbox_size_z;
            if (jonty_line_of_sight_3d_including_lava_check_ignoring_specific_door(&eyepos, &tgtpos, thing))
                return true;
            return false;
        }
    } else
    {
        // If we're immune to lava, or we're already on it - don't care, travel over it
        if (lava_at_position(srcpos) || creature_can_travel_over_lava(creatng))
        {
            SYNCDBG(17, "The %s index %" PRId64 " owned by player %" PRId64 " checks w/o lava %s index %" PRId64,
                thing_model_name(creatng),(int64_t)creatng->index,(int64_t)creatng->owner,thing_model_name(thing),(int64_t)thing->index);
            // Check bottom of the thing
            if (line_of_sight_3d(&eyepos, &tgtpos))
                return true;
            // Check top of the thing
            tgtpos.z.val += thing->clipbox_size_z;
            if (line_of_sight_3d(&eyepos, &tgtpos))
                return true;
            int64_t angle = get_angle_xy_to(&tgtpos, &eyepos);
            // Check left side
            // We're checking point at 60 degrees left; could use 90 deg, but making even slim edge visible might not be a good idea
            // Also 60 deg will shorten distance to the check point, which may better describe real visibility
            tgtpos.x.val = thing->mappos.x.val + distance_with_angle_to_coord_x(thing->clipbox_size_xy / 2, angle + DEGREES_60);
            tgtpos.y.val = thing->mappos.y.val + distance_with_angle_to_coord_y(thing->clipbox_size_xy / 2, angle + DEGREES_60);
            if (jonty_line_of_sight_3d_including_lava_check_ignoring_own_door(&eyepos, &tgtpos, creatng->owner))
                return true;
            // Check right side
            tgtpos.x.val = thing->mappos.x.val + distance_with_angle_to_coord_x(thing->clipbox_size_xy / 2, angle - DEGREES_60);
            tgtpos.y.val = thing->mappos.y.val + distance_with_angle_to_coord_y(thing->clipbox_size_xy / 2, angle - DEGREES_60);
            if (jonty_line_of_sight_3d_including_lava_check_ignoring_own_door(&eyepos, &tgtpos, creatng->owner))
                return true;
            return false;
        } else
        {
            SYNCDBG(17, "The %s index %" PRId64 " owned by player %" PRId64 " checks with lava %s index %" PRId64,
                thing_model_name(creatng),(int64_t)creatng->index,(int64_t)creatng->owner,thing_model_name(thing),(int64_t)thing->index);
            // Check bottom of the thing
            if (jonty_line_of_sight_3d_including_lava_check_ignoring_own_door(&eyepos, &tgtpos, creatng->owner))
                return true;
            // Check top of the thing
            tgtpos.z.val += thing->clipbox_size_z;
            if (jonty_line_of_sight_3d_including_lava_check_ignoring_own_door(&eyepos, &tgtpos, creatng->owner))
                return true;
            // Check both sides at middle of thing height
            tgtpos.z.val -= thing->clipbox_size_z / 2;
            int64_t angle = get_angle_xy_to(&tgtpos, &eyepos);
            // Check left side
            // We're checking point at 60 degrees left; could use 90 deg, but making even slim edge visible might not be a good idea
            // Also 60 deg will shorten distance to the check point, which may better describe real visibility
            tgtpos.x.val = thing->mappos.x.val + distance_with_angle_to_coord_x(thing->clipbox_size_xy/2, angle + DEGREES_60);
            tgtpos.y.val = thing->mappos.y.val + distance_with_angle_to_coord_y(thing->clipbox_size_xy/2, angle + DEGREES_60);
            if (jonty_line_of_sight_3d_including_lava_check_ignoring_own_door(&eyepos, &tgtpos, creatng->owner))
                return true;
            // Check right side
            tgtpos.x.val = thing->mappos.x.val + distance_with_angle_to_coord_x(thing->clipbox_size_xy/2, angle - DEGREES_60);
            tgtpos.y.val = thing->mappos.y.val + distance_with_angle_to_coord_y(thing->clipbox_size_xy/2, angle - DEGREES_60);
            if (jonty_line_of_sight_3d_including_lava_check_ignoring_own_door(&eyepos, &tgtpos, creatng->owner))
                return true;
            return false;
        }
    }
}

TbBool line_of_sight_2d(const struct Coord3d *frpos, const struct Coord3d *topos)
{

    MapCoordDelta pos_delta_x;
    MapCoordDelta pos_delta_y;
    MapCoordDelta ray_point_delta_x;
    MapCoordDelta ray_point_delta_y;

    int64_t ray_end_point;
    int64_t ray_current_point;
    struct Coord3d ray_point_pos;
    static const int64_t RAY_RESOLUTION = 80;
        
    pos_delta_x = llabs(topos->x.val - frpos->x.val);
    pos_delta_y = llabs(topos->y.val - frpos->y.val);

    if ( frpos->x.val > topos->x.val )
    {
        ray_point_delta_x = -RAY_RESOLUTION;
    }
    else
    {
        ray_point_delta_x = RAY_RESOLUTION;
    }
    
    if ( frpos->y.val > topos->y.val )
    {
        ray_point_delta_y = -RAY_RESOLUTION;
    }
    else
    {
        ray_point_delta_y = RAY_RESOLUTION;
    }
    
    if ( pos_delta_y == pos_delta_x )
    {
      ray_end_point = (pos_delta_x + 1) / RAY_RESOLUTION;
    }
    else if ( pos_delta_y > pos_delta_x )
    {
      ray_point_delta_x = (pos_delta_x + 1) * ray_point_delta_x / (pos_delta_y + 1);
      ray_end_point = (pos_delta_y + 1) / RAY_RESOLUTION;
    }
    else
    {
      ray_point_delta_y = (pos_delta_y + 1) * ray_point_delta_y / (pos_delta_x + 1);
      ray_end_point = (pos_delta_x + 1) / RAY_RESOLUTION;
    }
    
    ray_current_point = ray_end_point;
    ray_point_pos.x = frpos->x;
    ray_point_pos.y = frpos->y;
    ray_point_pos.z = frpos->z;

    if ( ray_end_point == 0 )
      return true;

    while ( !point_in_map_is_solid(&ray_point_pos) )
    {
      ray_point_pos.x.val += ray_point_delta_x;
      ray_point_pos.y.val += ray_point_delta_y;
      ray_current_point--;
      if ( ray_current_point == 0 )
        return true;
    }
    return false;
}

TbBool line_of_sight_3d(const struct Coord3d *frpos, const struct Coord3d *topos)
{
    static const struct LosRules rules = { los_solid, &sibling_ignoring_door_rules, 1, 0 };
    const struct LosContext ctx = { INVALID_THING, 0 };
    return los_walk(frpos, topos, &rules, &ctx);
}

TbBool nowibble_line_of_sight_3d(const struct Coord3d *frpos, const struct Coord3d *topos)
{
    static const struct LosRules rules = { los_solid, &sibling_ignoring_door_rules, 0, 1 };
    const struct LosContext ctx = { INVALID_THING, 0 };
    return los_walk(frpos, topos, &rules, &ctx);
}

TbBool line_of_room_move_2d(const struct Coord3d *frpos, const struct Coord3d *topos, struct Room *room)
{
    MapCoordDelta delta_x;
    MapCoordDelta delta_y;
    int64_t distance_per_step_x;
    int64_t distance_per_step_y;
    int64_t ray_end_point;
    int64_t ray_current_point;
    struct Coord3d ray_point_pos;
    static const int64_t RAY_RESOLUTION = 80;

    distance_per_step_x = RAY_RESOLUTION;
    delta_x = topos->x.val - frpos->x.val;
    delta_y = topos->y.val - frpos->y.val;
    if ( delta_x < 0 )
    {
        delta_x = frpos->x.val - topos->x.val;
        distance_per_step_x = -RAY_RESOLUTION;
    }
    distance_per_step_y = RAY_RESOLUTION;
    if ( delta_y < 0 )
    {
        delta_y = frpos->y.val - topos->y.val;
        distance_per_step_y = -RAY_RESOLUTION;
    }

    if ( delta_y == delta_x )
    {
        ray_end_point = (delta_x + 1) / RAY_RESOLUTION;
    }
    else if ( delta_y >= delta_x )
    {
        distance_per_step_x = (delta_x + 1) * distance_per_step_x / (delta_y + 1);
        ray_end_point = (delta_y + 1) / RAY_RESOLUTION;
    }
    else
    {
        distance_per_step_y = (delta_y + 1) * distance_per_step_y / (delta_x + 1);
        ray_end_point = (delta_x + 1) / RAY_RESOLUTION;
    }
    ray_current_point = ray_end_point;
    ray_point_pos.x.val = frpos->x.val;
    ray_point_pos.y.val = frpos->y.val;
    ray_point_pos.z.val = frpos->z.val;


    if ( !ray_end_point )
        return true;
    while ( get_room_at_pos(&ray_point_pos) == room )
    {
        ray_point_pos.x.val += distance_per_step_x;
        ray_point_pos.y.val += distance_per_step_y;
        ray_current_point--;
        if ( ray_current_point == 0 )
            return true;
    }
    return false;
}

int64_t get_explore_sight_distance_in_slabs(const struct Thing *thing)
{
    if (!thing_exists(thing))
    {
        WARNLOG("The %s index %" PRId64 " exploring dug slabs no longer exists", thing_model_name(thing), (int64_t)thing->index);
        return 0;
    }
    int64_t dist;
    if (!is_thing_some_way_controlled(thing)) {
        dist = CREATURE_EXPLORE_DISTANCE;
    } else {
        dist = CREATURE_EXPLORE_DISTANCE_POSSESSED;
    }
    return dist;
}
/******************************************************************************/
