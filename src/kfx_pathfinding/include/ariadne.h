/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file ariadne.h
 *     Header file for ariadne.c.
 * @par Purpose:
 *     Dungeon routing and path finding system.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     10 Jan 2010 - 20 Feb 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_ARIADNE_H
#define DK_ARIADNE_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// TREE_ROUTE_LEN <= 5000 results in crash on big maze map
// ARID_PATH_WAYPOINTS_COUNT <= 1300 results in crash on big maze map
#define TREE_ROUTE_LEN 6000
#define BORDER_LENGTH 100
#define ROUTE_LENGTH 12000
#define ARID_WAYPOINTS_COUNT 10
#define ARID_PATH_WAYPOINTS_COUNT 1400

/******************************************************************************/
#pragma pack(1)

struct Thing;

typedef unsigned char AriadneReturn;
typedef unsigned char AriadneRouteFlags;

enum AriadneReturnValues {
    AridRet_OK    = 0,
    AridRet_FinalOK,
    AridRet_Failed,
    AridRet_PartOK,
};

// Current wall-hugging activity state - "What wall-hugging am I currently doing?"
enum WallhugCurrentState {
    WallhugCurrentState_None = 0,        // Not wall-hugging
    WallhugCurrentState_Right = 1,       // Keep wall on creature's right side
    WallhugCurrentState_Left = 2         // Keep wall on creature's left side
};

// Wall-hugging side preference configuration - "Which side should I prefer to hug?"
enum WallhugPreference {
    WallhugPreference_None = 0,       // No wall-hugging preference
    WallhugPreference_Right = 1,      // Prefer to keep wall on right side
    WallhugPreference_Left = 2        // Prefer to keep wall on left side
};

// Pathfinding direction states for gates
enum PathfindingDirection {
    PathDir_Reverse = -1,           // Direction from end to start
    PathDir_StartToEnd = 0,         // Direction from start to end
    PathDir_EndToStart = 1,         // Direction from end to start
    PathDir_BestPoint = 2           // Direction to best point
};

// Wall-hugging activity state
enum WallhugActive {
    WallhugActive_Off = 0,          // Wall-hugging disabled
    WallhugActive_On = 1            // Wall-hugging enabled
};

// Triangle navigation corner flags for pathfinding
enum TriangleNavigationFlags {
    TriangleFlag_TopLeft = 0x01,         // Top-left corner flag
    TriangleFlag_TopRight = 0x02,        // Top-right corner flag
    TriangleFlag_BottomLeft = 0x04,      // Bottom-left corner flag
    TriangleFlag_BottomRight = 0x08,     // Bottom-right corner flag
    TriangleFlag_All = 0x0F              // All corners flag
};

// Field of view region test results
enum FieldOfViewRegion {
    FieldOfViewRegion_OutsideLeft = -1,  // Point is outside FOV on left side
    FieldOfViewRegion_WithinBounds = 0,  // Point is within FOV bounds
    FieldOfViewRegion_OutsideRight = 1   // Point is outside FOV on right side
};

// Navigation rule results for pathfinding
enum NavigationRule {
    NavigationRule_Blocked = 0,          // Cannot navigate through this area
    NavigationRule_Normal = 1,           // Normal navigation allowed
    NavigationRule_Special = 2           // Special navigation (higher cost)
};

// Creature navigation radius sizes for pathfinding
enum CreatureNavigationRadius {
    CreatureRadius_Small = 1,            // Small creature navigation radius
    CreatureRadius_Medium = 2,           // Medium creature navigation radius
    CreatureRadius_Large = 3             // Large creature navigation radius
};

enum AriadneRouteFlagValues {
    AridRtF_Default   = 0x00,
    AridRtF_NoOwner   = 0x01,
};

enum AriadneUpdateStateValues {
    AridUpSt_Unset   = 0,
    AridUpSt_OnLine,
    AridUpSt_Wallhug,
    AridUpSt_Manoeuvre,
};

enum AriadneUpdateSubStateManoeuvreValues {
    AridUpSStM_Unset   = 0,
    AridUpSStM_StartWallhug,
    AridUpSStM_ContinueWallhug,
};

enum NavigationStateValues {
    NavS_NavigationDisabled   = 0,
    NavS_WallhugInProgress,
    NavS_InitialWallhugSetup,
    NavS_WallhugDirectionCheck,
    NavS_WallhugPositionAdjust,
    NavS_WallhugAngleCorrection,
    NavS_WallhugGapDetected,
    NavS_WallhugRestartSetup,
};

#define NAVMAP_FLOORHEIGHT_BIT  0
#define NAVMAP_FLOORHEIGHT_MAX  0x0f
#define NAVMAP_FLOORHEIGHT_MASK 0x0f
#define NAVMAP_UNSAFE_SURFACE   0x10
#define NAVMAP_OWNERSELECT_BIT  5
#define NAVMAP_OWNERSELECT_MASK 0x3FE0
#define NAVMAP_ABYSS            0x4000

struct Ariadne { // sizeof = 102
    /** Position where the journey stated. */
    struct Coord3d startpos;
    /** Final position where we're heading. */
    struct Coord3d endpos;
    /** Position of the last reached waypoint. */
    struct Coord3d current_waypoint_pos;
  struct Coord3d next_position;
  struct Coord3d previous_position;
  unsigned char route_flags;
  unsigned char hug_side;
  unsigned char update_state;
  unsigned char wallhug_active;
  unsigned char may_need_reroute;
  int64_t wallhug_stored_angle;
  int64_t move_speed;
    /** Index of the current waypoint in list of nearest waypoints stored. */
    unsigned char current_waypoint;
    /** List of nearest waypoints in the way towards destination, stored in an array. */
    struct Coord2d waypoints[ARID_WAYPOINTS_COUNT];
    /** Amount of nearest waypoints stored in the array. */
    unsigned char stored_waypoints; // offs = 0x51
    /** Total amount of waypoints planned on the way towards endpos. */
    uint64_t total_waypoints;
  struct Coord3d manoeuvre_fixed_position;
  struct Coord3d manoeuvre_requested_position;
  unsigned char manoeuvre_state;
  int64_t wallhug_angle;
  int64_t straight_dist_to_next_waypoint;
};

struct PathWayPoint { // sizeof = 8
    int64_t x;
    int64_t y;
};

struct Path { // sizeof = 2068
    struct PathWayPoint start;
    struct PathWayPoint finish;
    int64_t waypoints_num;
    struct PathWayPoint waypoints[ARID_PATH_WAYPOINTS_COUNT];
};

struct HugStart {
    int64_t wh_angle;
    unsigned char wh_side;
};

/******************************************************************************/

extern const struct HugStart blocked_x_hug_start[][2];
extern const struct HugStart blocked_y_hug_start[][2];
extern const struct HugStart blocked_xy_hug_start[][2][2];
extern TbBool nav_map_initialised;

extern NavColour *LastTriangulatedMap;
extern int64_t ix_Border;
extern int64_t Border[BORDER_LENGTH];

/******************************************************************************/


#pragma pack()
/******************************************************************************/

void set_nav_rule_default(void);

AriadneReturn ariadne_initialise_creature_route_f(struct Thing *thing, const struct Coord3d *pos, int64_t speed, AriadneRouteFlags flags, const char *func_name);
#define ariadne_initialise_creature_route(thing, pos, speed, flags) ariadne_initialise_creature_route_f(thing, pos, speed, flags, __func__)
AriadneReturn creature_follow_route_to_using_gates(struct Thing *thing, struct Coord3d *finalpos, struct Coord3d *nextpos, int64_t speed, AriadneRouteFlags flags);

int64_t ariadne_count_waypoints_on_creature_route_to_target_f(const struct Thing *thing,
    const struct Coord3d *srcpos, const struct Coord3d *dstpos, AriadneRouteFlags flags, const char *func_name);
AriadneReturn ariadne_invalidate_creature_route(struct Thing *thing);

TbBool navigation_points_connected(struct Coord3d *pt1, struct Coord3d *pt2);
void path_init8_wide_f(struct Path *path, int64_t start_x, int64_t start_y, int64_t end_x, int64_t end_y, int64_t subroute, unsigned char nav_size, const char *func_name);
void nearest_search_f(int64_t sizexy, int64_t srcx, int64_t srcy, int64_t dstx, int64_t dsty, int64_t *px, int64_t *py, const char *func_name);
#define nearest_search(sizexy, srcx, srcy, dstx, dsty, px, py) nearest_search_f(sizexy, srcx, srcy, dstx, dsty, px, py, __func__)


int64_t pointed_at8(int64_t pos_x, int64_t pos_y, int64_t *ret_tri, int64_t *ret_pt);
int64_t angle_to_quadrant(int64_t angle);

int64_t thing_nav_block_sizexy(const struct Thing *thing);
int64_t thing_nav_sizexy(const struct Thing *thing);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
