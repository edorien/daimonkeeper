/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_render.c
 *     Rendering the 3D view functions.
 * @par Purpose:
 *     Functions for displaying drawlist elements on screen.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     20 Mar 2009 - 20 May 2011
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "renderer/RendererProfile.h"
#include <stddef.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "engine_render.h"
#include "globals.h"

#include "bflib_basics.h"
#include "bflib_fileio.h"
#include "bflib_math.h"
#include "bflib_planar.h"
#include "bflib_render.h"
#include "bflib_sprite.h"
#include "bflib_video.h"
#include "bflib_vidraw.h"
#include "config_creature.h"
#include "config_players.h"
#include "config_settings.h"
#include "config_spritecolors.h"
#include "config_terrain.h"
#include "config_keeperfx.h"
#include "creature_graphics.h"
#include "creature_states.h"
#include "creature_states_combt.h"
#include "creature_states_gardn.h"
#include "creature_states_lair.h"
#include "creature_states_mood.h"
#include "custom_sprites.h"
#include "engine_arrays.h"
#include "engine_camera.h"
#include "engine_lenses.h"
#include "engine_redraw.h"
#include "engine_textures.h"
#include "local_camera.h"
#include "map_data.h"
#include "map_columns.h"
#include "map_utils.h"
#include "sim_scratch.h"
#include "player_instances.h"
#include "roomspace_prediction.h"
#include "slab_data.h"
#include "sprites.h"
#include "thing_objects.h"
#include "thing_stats.h"
#include "thing_traps.h"
#include "vidmode.h"

#include "kfx_render_state.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "light_data.h"
#include "bflib_mouse.h"
#include "ports/ui_port.h"
#include "ports/game_port.h"
#include "ports/session_loop_port.h"
#include "ports/editor_port.h"
#include "local_state.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TO_FIXED(x)    ((x) << 16)
#define FROM_FIXED(x)    ((x) >> 16)
#define ABYSS_WALL_RENDER_HEIGHT 6
#define ABYSS_WALL_TOP_BRIGHTNESS 70
#define ABYSS_WALL_BOTTOM_BRIGHTNESS 0
#define ABYSS_LAVA_SCROLL_SPEED 0.75
#define ABYSS_WATER_SCROLL_SPEED 2.25
#define ABYSS_LIQUID_SCROLL_CYCLE 128.0

enum QKinds {
    QK_PolygonStandard = 0,
    QK_PolygonNearFP,
    QK_JontySprite,
    QK_CreatureShadow,
    QK_SlabSelector,
    QK_CreatureStatus,
    QK_TextureQuad,
    QK_FloatingGoldText,
    QK_RoomFlagBottomPole,
    QK_JontyISOSprite,
    QK_RoomFlagStatusBox,
    QK_ListEnd,
};

struct MinMax;
struct Camera;
struct PlayerInfo;

typedef unsigned char QKind;

struct BasicQ { // sizeof = 5
  struct BasicQ *next;
  QKind kind;
};

struct BucketKindPolygonStandard {
    struct BasicQ b;
    int64_t block;
    struct PolyPoint vertex_first;
    struct PolyPoint vertex_second;
    struct PolyPoint vertex_third;
};

struct BucketKindPolygonNearFP {
    struct BasicQ b;
    unsigned char subtype;
    int64_t block;
    struct PolyPoint vertex_first;
    struct PolyPoint vertex_second;
    struct PolyPoint vertex_third;
    struct XYZ coordinate_first;
    struct XYZ coordinate_second;
    struct XYZ coordinate_third;
};

struct BucketKindJontySprite {  // BasicQ type 11,18
    struct BasicQ b;
    struct Thing *thing;
    int64_t scr_x;
    int64_t scr_y;
    int64_t depth_fade;
};

struct BucketKindCreatureShadow {
    struct BasicQ b;
    int64_t color_value;
    struct PolyPoint vertex_first;
    struct PolyPoint vertex_second;
    struct PolyPoint vertex_third;
    struct PolyPoint vertex_fourth;
    int64_t angle;
    int64_t anim_sprite;
    unsigned char current_frame;
};

struct BucketKindSlabSelector {
    struct BasicQ b;
    int64_t color_value;
    struct PolyPoint p;
};

struct BucketKindCreatureStatus { // sizeof = 24
    struct BasicQ b;
    unsigned char padding[3];
    struct Thing *thing;
    int64_t x;
    int64_t y;
    int64_t z;
};

#define SHADOW_SOURCES_MAX_COUNT 4
struct NearestLights {
    struct Coord3d coord[SHADOW_SOURCES_MAX_COUNT];
};
struct BucketKindTexturedQuad { // sizeof = 54
    struct BasicQ b;
    unsigned char orient;
    int64_t texture_idx;
    int64_t texture_x;
    int64_t texture_y;
    struct Coord2d texture_scroll;
    int64_t zoom_x;
    int64_t zoom_y;
    int64_t shade_intensity0;
    int64_t shade_intensity1;
    int64_t shade_intensity2;
    int64_t shade_intensity3;
    int64_t marked_mode;
};

struct BucketKindFloatingGoldText { // BasicQ type 16
    struct BasicQ b;
    int64_t x;
    int64_t y;
    int64_t lvl;
};

struct BucketKindRoomFlag { // BasicQ type 17,19
    struct BasicQ b;
    int64_t lvl;
    int64_t x;
    int64_t y;
};



/* Corner slot holding the ceiling vertex. Slots 0..COLUMN_STACK_HEIGHT belong to the
   cubes of a column and the abyss walls own the slots above them, so the ceiling needs
   a slot of its own past both. Sharing slot COLUMN_STACK_HEIGHT with the cubes made a
   column with every cube filled draw its top face and topmost side at ceiling height. */
#define ENGINE_COL_CEILING_CORNER (COLUMN_STACK_HEIGHT + ABYSS_WALL_RENDER_HEIGHT + 3)

struct EngineCol {
    struct EngineCoord cors[ENGINE_COL_CEILING_CORNER + 1];
};

struct SideOri {
    unsigned char back_texture_index;
    unsigned char top_texture_index;
    unsigned char front_texture_index;
    unsigned char bottom_texture_index;
};

/******************************************************************************/
static const struct SideOri sideoris[] = {
    { 0,  1,  2,  3},
    { 0,  0,  3,  2},
    { 1,128,  2,  1},
    { 0,  3,128,  2},
    { 3,  0,  1,  0},
    { 3,  2,  1,  0},
    {128, 3,  0,  1},
    { 2,  0,  1,  2},
    { 3,  0,  0,  1},
    { 0,  3,  2,128},
};

int64_t const x_offs[] =  { 0, 1, 1, 0};
int64_t const y_offs[] =  { 0, 0, 1, 1};
int64_t const x_step1[] = { 0,-1, 0, 1};
int64_t const y_step1[] = { 1, 0,-1, 0};
int64_t const x_step2[] = { 1, 0,-1, 0};
int64_t const y_step2[] = { 0, 1, 0,-1};
int64_t const orient_table_xflip[] =  {0, 0, 1, 1};
int64_t const orient_table_yflip[] =  {0, 1, 1, 0};
int64_t const orient_table_rotate[] = {0, 1, 0, 1};
int64_t const orient_to_mapU1[] = { 0x00, 0x1F0000, 0x1F0000, 0x00 };
int64_t const orient_to_mapU2[] = { 0x1F0000, 0x1F0000, 0x00, 0x00 };
int64_t const orient_to_mapU3[] = { 0x1F0000, 0x00, 0x00, 0x1F0000 };
int64_t const orient_to_mapU4[] = { 0x00, 0x00, 0x1F0000, 0x1F0000 };
int64_t const orient_to_mapV1[] = { 0x00, 0x00, 0x1F0000, 0x1F0000 };
int64_t const orient_to_mapV2[] = { 0x00, 0x1F0000, 0x1F0000, 0x00 };
int64_t const orient_to_mapV3[] = { 0x1F0000, 0x1F0000, 0x00, 0x00 };
int64_t const orient_to_mapV4[] = { 0x1F0000, 0x00, 0x00, 0x1F0000 };

unsigned char const height_masks[] = {
  0, 1, 2, 2, 3, 3, 3, 3,
  4, 4, 4, 4, 4, 4, 4, 4,
  5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5,
  6, 6, 6, 6, 6, 6, 6, 6,
  6, 6, 6, 6, 6, 6, 6, 6,
  6, 6, 6, 6, 6, 6, 6, 6,
  6, 6, 6, 6, 6, 6, 6, 6,
  7, 7, 7, 7, 7, 7, 7, 7,
  7, 7, 7, 7, 7, 7, 7, 7,
  7, 7, 7, 7, 7, 7, 7, 7,
  7, 7, 7, 7, 7, 7, 7, 7,
  7, 7, 7, 7, 7, 7, 7, 7,
  7, 7, 7, 7, 7, 7, 7, 7,
  7, 7, 7, 7, 7, 7, 7, 7,
  7, 7, 7, 7, 7, 7, 7, 7,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
};

// View distance related
struct MinMax minmaxs[MINMAX_LENGTH];
unsigned char *getpoly;
unsigned char poly_pool[POLY_POOL_SIZE];
unsigned char *poly_pool_end;
struct BasicQ *buckets[BUCKETS_COUNT];
int64_t cells_away;
int64_t max_i_can_see;
const int64_t MAX_I_CAN_SEE_OVERHEAD = (MINMAX_LENGTH/2)-2;
struct EngineCol ecs1[MINMAX_LENGTH-1];
struct EngineCol ecs2[MINMAX_LENGTH-1];
struct EngineCol *front_ec;
struct EngineCol *back_ec;
double hud_scale;

int64_t line_box_size = 150; // Default value, overwritten by cfg setting
int64_t creature_status_size = 16; // Default value, overwritten by cfg setting
static int64_t water_wibble_angle = 0;
static double render_water_wibble = 0; // Rendering float
static double render_abyss_lava_scroll;
static double render_abyss_water_scroll;
static struct Coord2d texture_scroll;
static uint64_t render_problems;
static int64_t render_prob_kind;

Offset vert_offset[3];
Offset hori_offset[3];
Offset high_offset[3];
int64_t x_init_off;
int64_t y_init_off;
int64_t floor_pointed_at_x;
int64_t floor_pointed_at_y;
int64_t box_lag_compensation_x;
int64_t box_lag_compensation_y;

static int64_t fade_scaler;
static int64_t fade_way_out;
static int64_t map_roll;
static int64_t map_tilt;
static int64_t view_alt;
static int64_t fade_min;
static int64_t fade_range;
static int64_t depth_init_off;
static int64_t normal_shade_left;
static int64_t normal_shade_right;
static int64_t apos;
static int64_t bpos;
static int64_t split1at;
static int64_t split2at;
static int64_t map_x_pos;
static int64_t map_y_pos;
static int64_t map_z_pos;
static int64_t normal_shade_front;
static int64_t normal_shade_back;
static int64_t me_distance;
static int64_t thelens;
static int64_t fade_mmm;
static int64_t spr_map_angle;
static int64_t lfade_max;
static int64_t lfade_min;
static unsigned char thing_being_displayed_is_creature;
static int64_t global_scaler;
static int64_t water_source_cutoff;
static int64_t water_y_offset;
static int64_t cam_map_angle;

static struct M33 camera_matrix;
struct EngineCoord object_origin;

int64_t mx;
int64_t my;
int64_t mz;
unsigned char temp_cluedo_mode; // This is true(1) if the "short wall" have been enabled in the graphics options
struct Thing *thing_being_displayed;

TbSpriteData *keepsprite[KEEPSPRITE_LENGTH];
TbSpriteData sprite_heap_handle[KEEPSPRITE_LENGTH];
struct HeapMgrHeader *graphics_heap;
TbFileHandle jty_file_handle;

struct MapVolumeBox map_volume_box;
int64_t view_height_over_2;
int64_t view_width_over_2;
int64_t z_threshold_near;
int64_t split_2;
int64_t fade_max;

static const char splittypes[64] = {
    0, 0, 0, 0, 0, 1, 1, 1, 0, 1, 5, 5, 0, 1, 5, 5,
    0, 2, 2, 2, 3, 4, 4, 4, 3, 4, 8, 8, 3, 4, 8, 8,
    0, 2, 6, 6, 3, 4, 9, 9, 7, 10, 11, 11, 7, 10, 11, 11,
    0, 2, 6, 6, 3, 4, 9, 9, 7, 10, 11, 11, 7, 10, 11, 11
};

/******************************************************************************/
#ifdef __cplusplus
}
#endif
/******************************************************************************/
static void do_map_who(int64_t tnglist_idx);
static void (*render_sprite_debug_fn) (struct Thing*, int64_t scrpos_x, int64_t scrpos_y) = NULL;
static int64_t render_sprite_debug_level = 0;
static void draw_keepsprite_unscaled_in_buffer(int64_t kspr_n, int64_t angle, unsigned char current_frame, unsigned char *outbuf);
static void draw_jonty_mapwho(struct BucketKindJontySprite *jspr);

static TbBool animation_sprite_id_invalid(int64_t animation_sprite)
{
    return ((animation_sprite >= CREATURE_FRAMELIST_LENGTH) && (animation_sprite < KEEPERSPRITE_ADD_OFFSET))
        || (animation_sprite >= KEEPERSPRITE_ADD_OFFSET + KEEPERSPRITE_ADD_NUM);
}
/******************************************************************************/

static void calculate_hud_scale(struct Camera *cam) {
    // hud_scale is the current camera zoom converted to a percentage that ranges between base level zoom and fully zoomed out.
    // HUD items: creature status flowers, room flags, popup gold numbers. They scale with the zoom.
    double range_input = cam->zoom;
    double range_min;
    double range_max;
    switch (cam->view_mode) {
        case PVM_IsoWibbleView:
        case PVM_IsoStraightView:
            range_min = CAMERA_ZOOM_MIN; // Fully zoomed out
            range_max = 4100; // Base zoom level
            break;
        case PVM_FrontView:
            range_min = FRONTVIEW_CAMERA_ZOOM_MIN; // Fully zoomed out
            range_max = 32768; // Base zoom level
            break;
        default:
            hud_scale = 0;
            return;
    }
    if (range_input < range_min) {
        range_input = range_min;
    } else if (range_input > range_max) {
        range_input = range_max;
    }
    hud_scale = ((range_input - range_min)) / (range_max - range_min);
}

// interpolate_time (kfx_apploop's game_session_loop.cpp) is reached
// through SessionLoopPort instead of a same-file bare-extern
// forward-declaration. See docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md.

double interpolate(double previous, double current)
{
    if (! is_feature_on(Ft_DeltaTime))
        return current;

    return LbLerp(previous, current, loop_get_interpolate_time());
}

double interpolate_angle(double previous, double current)
{
    if (! is_feature_on(Ft_DeltaTime))
        return current;

    return lerp_angle(previous, current, loop_get_interpolate_time());
}

// For things that stop moving when the game is paused.
double interpolate_synced(double previous, double current)
{
    if (flag_is_set(kfx_sim_state.operation_flags, GOF_Paused))
        return current;

    return interpolate(previous, current);
}

struct ThingInterpolateResult interpolate_thing(struct Thing *thing)
{
    struct ThingInterpolateResult result;

    if (get_gameturn() - thing->creation_turn <= 1)
    {
        // Set initial interp position when Thing has just been created
        thing->previous_mappos = thing->mappos;
        thing->previous_floor_height = thing->floor_height;
    }

    // Interpolate position every frame
    result.mappos.x.val = interpolate_synced(thing->previous_mappos.x.val, thing->mappos.x.val);
    result.mappos.y.val = interpolate_synced(thing->previous_mappos.y.val, thing->mappos.y.val);
    result.mappos.z.val = interpolate_synced(thing->previous_mappos.z.val, thing->mappos.z.val);
    result.floor_height = interpolate_synced(thing->previous_floor_height, thing->floor_height);

    // Cancel interpolation if distance to interpolate is too far. This is a
    // catch-all to solve any remaining interpolation bugs.
    if ((llabs(thing->previous_mappos.x.val - thing->mappos.x.val) >= 10000) ||
        (llabs(thing->previous_mappos.y.val - thing->mappos.y.val) >= 10000) ||
        (llabs(thing->previous_mappos.z.val - thing->mappos.z.val) >= 10000))
    {
        ERRORLOG("The %s index %" PRId64 " owned by player %" PRId64 " moved an unrealistic distance((%" PRId64 ",%" PRId64 ",%" PRId64 ") to (%" PRId64 ",%" PRId64 ",%" PRId64 ")), refusing interpolation.",
                 thing_model_name(thing), (int64_t)thing->index, (int64_t)thing->owner,
                 (int64_t)(thing->previous_mappos.x.stl.num), (int64_t)(thing->previous_mappos.y.stl.num), (int64_t)(thing->previous_mappos.z.stl.num),
                 (int64_t)(thing->mappos.x.stl.num), (int64_t)(thing->mappos.y.stl.num), (int64_t)(thing->mappos.z.stl.num));
        result.mappos = thing->mappos;
        result.floor_height = thing->floor_height;
    }

    return result;
}

static void get_floor_pointed_at(int64_t x, int64_t y, int64_t *floor_x, int64_t *floor_y)
{
    long long ofs_x;
    long long ofs_y;
    long long sor_hp;
    long long sor_hn;
    long long sor_vp;
    long long sor_vn;
    long long der_hp;
    long long der_hn;
    long long der_vp;
    long long der_vn;
    long long div_v;
    long long div_h;
    if ( (vert_offset[1] == 0) && (hori_offset[1] == 0) )
    {
        *floor_x = 0;
        *floor_y = 0;
        return;
    }
    ofs_x = (long long)x - (long long)x_init_off;
    ofs_y = (long long)y - (long long)y_init_off;
    sor_vp = (((long long)vert_offset[1] * ofs_x) / 2LL);
    sor_vn = (((long long)vert_offset[0] * ofs_y) / 2LL);
    der_vp = ((long long)hori_offset[0] * (long long)vert_offset[1]) / 8LL;
    der_vn = ((long long)vert_offset[0] * (long long)hori_offset[1]) / 8LL;
    sor_hp = (((long long)hori_offset[1] * ofs_x) / 2LL);
    sor_hn = (((long long)hori_offset[0] * ofs_y) / 2LL);
    der_hp = ((long long)vert_offset[0] * (long long)hori_offset[1]) / 8LL;
    der_hn = ((long long)hori_offset[0] * (long long)vert_offset[1]) / 8LL;
    div_v = (der_vp - der_vn) >> 8;
    div_h = (der_hp - der_hn) >> 8;
    if (div_v == 0 || div_h == 0)
    {
        ERRORLOG("Invalid floor value from %" PRId64 ",%" PRId64, (int64_t)(x), (int64_t)(y));
        *floor_x = 0;
        *floor_y = 0;
        return;
    }
    *floor_y = ((sor_vp - sor_vn) / div_v) >> 2;
    *floor_x = ((sor_hp - sor_hn) / div_h) >> 2;
}

static int64_t compute_cells_away(void) // For overhead view, not for 1st person view
{
    int64_t half_width;
    int64_t half_height;
    int64_t xmin;
    int64_t ymin;
    int64_t xmax;
    int64_t ymax;
    int64_t xcell;
    int64_t ycell;
    int64_t ncells_a;
    half_width = (local_state.engine_window_width >> 1);
    half_height = (local_state.engine_window_height >> 1);
    xcell = ((half_width<<1) + (half_width>>4))/pixel_size - local_state.engine_window_x/pixel_size;
    ycell = ((8 * high_offset[1]) >> 8) - (half_width>>4)/pixel_size - local_state.engine_window_y/pixel_size;
    get_floor_pointed_at(xcell, ycell, &xmax, &ymax);
    xcell = (half_width)/pixel_size - local_state.engine_window_x/pixel_size;
    ycell = (half_height)/pixel_size - local_state.engine_window_y/pixel_size;
    get_floor_pointed_at(xcell, ycell, &xmin, &ymin);
    xcell = llabs(ymax - ymin);
    ycell = llabs(xmax - xmin);
    if (ycell >= xcell) {
        ncells_a = ycell + (xcell >> 1);
    } else {
        ncells_a = xcell + (ycell >> 1);
    }
    ncells_a += 2;
    // docs/refactor/editor/phase4/05-live-test-fixes.md -- confirmed live
    // this clamp is NOT the zoom-out dropout's cause (no WARNLOG fired in a
    // session where the dropout reproduced). Kept as a new-peak tracker
    // rather than removed outright -- cheap, harmless, editor-only, and
    // still worth knowing if some other map/camera combination ever does
    // reach it. New-peak-triggered (not edge-triggered on the clamp
    // itself) so a session's log shows the actual growth curve toward
    // whatever ncells_a really reaches, not just a single crossing.
    if (editorport_is_active())
    {
        static int64_t s_peak_ncells_a = 0;
        if (ncells_a > s_peak_ncells_a)
        {
            s_peak_ncells_a = ncells_a;
            WARNLOG("Editor camera horizon-scan new peak: wants %" PRId64 " cells (clamp %" PRId64 ", MINMAX_LENGTH=%" PRId64 ")", (int64_t)(ncells_a), (int64_t)(MAX_I_CAN_SEE_OVERHEAD), (int64_t)(MINMAX_LENGTH));
        }
    }
    if (ncells_a > MAX_I_CAN_SEE_OVERHEAD) {
        ncells_a = MAX_I_CAN_SEE_OVERHEAD;
    }
    return ncells_a;
}

static void init_coords_and_rotation(struct EngineCoord *origin,struct M33 *matx)
{
    origin->x = 0;
    origin->y = 0;
    origin->z = 0;
    matx->r[0].v[0] = 0x4000u;
    matx->r[0].v[1] = 0;
    matx->r[0].v[2] = 0;
    matx->r[1].v[0] = 0;
    matx->r[1].v[1] = 0x4000u;
    matx->r[1].v[2] = 0;
    matx->r[2].v[0] = 0;
    matx->r[2].v[1] = 0;
    matx->r[2].v[2] = 0x4000u;
}

static void update_fade_limits(int64_t ncells_a)
{
    fade_max = (ncells_a << 8);
    fade_scaler = (ncells_a << 8);
    fade_way_out = (ncells_a + 1) << 8;
    // Terrain starts to fade toward darkness at half the view range (was three quarters, a short
    // band that read as a hard edge); linear from there to fade_max.
    fade_min = (128 * ncells_a);
    z_threshold_near = (split1at << 8);
    split_2 = (split2at << 8);
}

static void update_normal_shade(struct M33 *matx)
{
    normal_shade_left = matx->r[2].v[0];
    normal_shade_right = -matx->r[2].v[0];
    normal_shade_back = -matx->r[2].v[2];
    normal_shade_front = matx->r[2].v[2];
    if (normal_shade_front < 0)
      normal_shade_front = 0;
    if (normal_shade_back < 0)
      normal_shade_back = 0;
    if (normal_shade_left < 0)
      normal_shade_left = 0;
    if (normal_shade_right < 0)
      normal_shade_right = 0;
}

void update_engine_settings(struct PlayerInfo *player)
{
    switch (settings.video_detail_level)
    {
    case 0:
        split1at = 4;
        split2at = 3;
        break;
    case 1:
        split1at = 3;
        split2at = 2;
        break;
    case 2:
        split1at = 2;
        split2at = 1;
        break;
    case 3:
    default:
        split1at = 0;
        split2at = 0;
        break;
    }
    kfx_render_state.me_pointed_at = NULL;
    me_distance = 100000000;
    max_i_can_see = get_max_i_can_see_from_settings();
    if (lens_mode != 0)
      temp_cluedo_mode = 0;
    else
      temp_cluedo_mode = settings.video_cluedo_mode;
    kfx_render_state.thing_pointed_at = NULL;
}

/**
 * Sets the reserved amount of poly pool entries.
 * Entries which are reserved won't be filled by standard rendering items, even if the queue is full.
 * @param nitems
 */
static void poly_pool_end_reserve(int64_t nitems)
{
    poly_pool_end = &poly_pool[sizeof(poly_pool)-(nitems*sizeof(struct BucketKindSlabSelector))];
}

static TbBool is_free_space_in_poly_pool(int64_t nitems)
{
    return (getpoly+(nitems*sizeof(struct BucketKindSlabSelector)) <= poly_pool_end);
}

static void rotpers_parallel_3(struct EngineCoord *epos, struct M33 *matx, int64_t zoom)
{
    int64_t factor_w;
    int64_t factor_h;
    int64_t inp_x;
    int64_t inp_y;
    int64_t inp_z;
    long long out_x;
    long long out_y;
    inp_x = epos->x;
    inp_y = epos->y;
    inp_z = epos->z;
    out_x = ((long long)(inp_z * matx->r[0].v[2]) + ((long long)(inp_y + matx->r[0].v[0]) * (long long)(inp_x + matx->r[0].v[1])) - (long long)matx->r[0].v[3] - (long long)(inp_x * inp_y)) >> 14;
    epos->x = out_x;
    out_y = ((long long)(inp_z * matx->r[1].v[2]) + ((long long)(inp_y + matx->r[1].v[0]) * (long long)(inp_x + matx->r[1].v[1])) - (long long)matx->r[1].v[3] - (long long)(inp_x * inp_y)) >> 14;
    epos->y = out_y;
    epos->z = ((long long)(inp_z * matx->r[2].v[2]) + ((long long)(inp_y + matx->r[2].v[0]) * (long long)(inp_x + matx->r[2].v[1])) - (long long)matx->r[2].v[3] - (long long)(inp_x * inp_y)) >> 14;
    factor_w = (int64_t)view_width_over_2 + (zoom * out_x >> 16);
    epos->view_width = factor_w;
    factor_h = (int64_t)view_height_over_2 - (zoom * out_y >> 16);
    epos->view_height = factor_h;
    if (factor_w < 0)
    {
        epos->clip_flags |= 0x0008;
    } else
    if (vec_window_width <= factor_w)
    {
        epos->clip_flags |= 0x0010;
    }
    if (factor_h < 0)
    {
        epos->clip_flags |= 0x0020;
    } else
    if (factor_h >= vec_window_height)
    {
        epos->clip_flags |= 0x0040;
    }
    epos->clip_flags |= 0x0400;
}

static void base_vec_normalisation(struct M33 *matx, unsigned char a2)
{
    struct M31 *vec;
    vec = &matx->r[a2];
    int64_t rv0;
    int64_t rv1;
    int64_t rv2;
    int64_t rvlen;
    rv0 = vec->v[0];
    rv1 = vec->v[1];
    rv2 = vec->v[2];
    rvlen = LbSqrL(rv0 * rv0 + rv1 * rv1 + rv2 * rv2);
    vec->v[0] = (rv0 << 14) / rvlen;
    vec->v[1] = (rv1 << 14) / rvlen;
    vec->v[2] = (rv2 << 14) / rvlen;
}

static void vec_cross_prod(struct M31 *outvec, const struct M31 *vec2, const struct M31 *vec3)
{
    outvec->v[0] = vec3->v[2] * vec2->v[1] - vec3->v[1] * vec2->v[2];
    outvec->v[1] = vec3->v[0] * vec2->v[2] - vec3->v[2] * vec2->v[0];
    outvec->v[2] = vec3->v[1] * vec2->v[0] - vec3->v[0] * vec2->v[1];
}

static void matrix_transform(struct M31 *outvec, const struct M33 *matx, const struct M31 *vec2)
{
    outvec->v[0] = matx->r[0].v[2] * vec2->v[2] + matx->r[0].v[0] * vec2->v[0] + matx->r[0].v[1] * vec2->v[1];
    outvec->v[1] = matx->r[1].v[2] * vec2->v[2] + matx->r[1].v[0] * vec2->v[0] + matx->r[1].v[1] * vec2->v[1];
    outvec->v[2] = matx->r[2].v[2] * vec2->v[2] + matx->r[2].v[1] * vec2->v[1] + matx->r[2].v[0] * vec2->v[0];
}

static void rotate_base_axis(struct M33 *matx, int64_t angle, unsigned char axis)
{
    unsigned char scor0;
    unsigned char scor1;
    unsigned char scor2;
    switch (axis)
    {
    case 1:
        scor0 = 1;
        scor1 = 2;
        scor2 = 0;
        break;
    case 2:
        scor0 = 0;
        scor1 = 2;
        scor2 = 1;
        break;
    case 3:
        scor0 = 0;
        scor1 = 1;
        scor2 = 2;
        break;
    default:
        ERRORLOG("Bad axis");
        scor0 = 0;
        scor1 = 1;
        scor2 = 2;
        break;
    }

    struct M33 matt;
    {
#define TRIG_LIMIT (1 << (LbFPMath_TrigmBits - 2))
        int64_t angle_sin;
        int64_t angle_cos;
        angle_sin = LbSinL(angle) >> 2;
        angle_cos = LbCosL(angle) >> 2;
        int64_t matrix_x_component;
        int64_t matrix_y_component;
        int64_t matrix_z_component;
        matrix_x_component = matx->r[scor2].v[0];
        matrix_z_component = matx->r[scor2].v[2];
        matrix_y_component = matx->r[scor2].v[1];
        int64_t shf0;
        int64_t shf1;
        int64_t shf2;
        int64_t mag0;
        int64_t mag1;
        int64_t mag2;
        matt.r[0].v[0] = (matrix_x_component * matrix_x_component >> 14) + (angle_cos * (TRIG_LIMIT - (matrix_x_component * matrix_x_component >> 14)) >> 14);
        matt.r[1].v[1] = (matrix_y_component * matrix_y_component >> 14) + (angle_cos * (TRIG_LIMIT - (matrix_y_component * matrix_y_component >> 14)) >> 14);
        matt.r[2].v[2] = (matrix_z_component * matrix_z_component >> 14) + (angle_cos * (TRIG_LIMIT - (matrix_z_component * matrix_z_component >> 14)) >> 14);
        mag2 = (TRIG_LIMIT - angle_cos) * (matrix_y_component * matrix_x_component >> 14) >> 14;
        shf2 = angle_sin * matrix_z_component >> 14;
        mag1 = (TRIG_LIMIT - angle_cos) * (matrix_x_component * matrix_z_component >> 14) >> 14;
        shf1 = angle_sin * matrix_y_component >> 14;
        mag0 = (TRIG_LIMIT - angle_cos) * (matrix_y_component * matrix_z_component >> 14) >> 14;
        shf0 = angle_sin * matrix_x_component >> 14;
        matt.r[0].v[1] = mag2 - shf2;
        matt.r[0].v[2] = mag1 + shf1;
        matt.r[1].v[2] = mag0 - shf0;
        matt.r[1].v[0] = mag2 + shf2;
        matt.r[2].v[0] = mag1 - shf1;
        matt.r[2].v[1] = mag0 + shf0;
#undef TRIG_LIMIT
    }

    struct M31 locvec;
    matrix_transform(&locvec, &matt, &matx->r[scor0]);
    matx->r[scor0].v[0] = locvec.v[0] >> 14;
    matx->r[scor0].v[1] = locvec.v[1] >> 14;
    matx->r[scor0].v[2] = locvec.v[2] >> 14;
    matrix_transform(&locvec, &matt, &matx->r[scor1]);
    matx->r[scor1].v[0] = locvec.v[0] >> 14;
    matx->r[scor1].v[1] = locvec.v[1] >> 14;
    matx->r[scor1].v[2] = locvec.v[2] >> 14;
    base_vec_normalisation(matx, 2);

    vec_cross_prod(&locvec, &matx->r[2], &matx->r[0]);
    matx->r[1].v[0] = locvec.v[0] >> 14;
    matx->r[1].v[1] = locvec.v[1] >> 14;
    matx->r[1].v[2] = locvec.v[2] >> 14;
    base_vec_normalisation(matx, 1);

    vec_cross_prod(&locvec, &matx->r[1], &matx->r[2]);
    matx->r[0].v[0] = locvec.v[0] >> 14;
    matx->r[0].v[1] = locvec.v[1] >> 14;
    matx->r[0].v[2] = locvec.v[2] >> 14;
    base_vec_normalisation(matx, 0);

    matx->r[0].v[3] = matx->r[0].v[0] * matx->r[0].v[1];
    matx->r[1].v[3] = matx->r[1].v[0] * matx->r[1].v[1];
    matx->r[2].v[3] = matx->r[2].v[0] * matx->r[2].v[1];
}

struct WibbleTable *get_wibble_from_table(struct Camera *cam, int64_t table_index, MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{
    if (table_index < 0 || table_index >= WIBBLE_TABLE_SIZE) {
        ERRORLOG("Invalid wibble table index %" PRId64, (int64_t)(table_index));
        return &blank_wibble_table[0];
    }
    if (cam->view_mode == PVM_IsoWibbleView || cam->view_mode == PVM_CreatureView)
    {
        return &wibble_table[table_index];
    }
    else if (cam->view_mode == PVM_IsoStraightView)
    {
        struct SlabMap *slb = get_slabmap_for_subtile(stl_slab_center_subtile(stl_x), stl_slab_center_subtile(stl_y));
         // additional checks needed to keep straight edges around liquid with liquid wibble mode
        struct SlabMap *slb2 = get_slabmap_for_subtile(stl_slab_center_subtile(stl_x), stl_slab_center_subtile(stl_y+1));
        struct SlabMap *slb3 = get_slabmap_for_subtile(stl_slab_center_subtile(stl_x-1), stl_slab_center_subtile(stl_y));
        if (slab_kind_is_liquid(slb3->kind) && slab_kind_is_liquid(slb2->kind) && slab_kind_is_liquid(slb->kind))
        {
            return &wibble_table[table_index];
        }
    }
    return &blank_wibble_table[table_index];
}

/******************************************************************************/
// The first-person (creature) view displaces every wall/floor vertex by the wibble table's small
// 3-D offsets, which makes walls bulge irregularly. With the eye right next to a wall that lets a
// bulge cross the camera, so triangles flip back-facing or pass the near plane and holes open up.
// The offsets are therefore faded out for vertices close to the eye (full strength from
// WIBBLE_FADE_FAR away), per vertex -- so shared vertices stay shared and no cracks appear.
#define WIBBLE_FADE_NEAR 256
#define WIBBLE_FADE_FAR 512

static int64_t wibble_near_scale(const struct Camera *cam, int64_t rx, int64_t ry, int64_t rz, int64_t offset)
{
    if (cam->view_mode != PVM_CreatureView || offset == 0)
        return offset;
    const double d = sqrt((double)rx * (double)rx + (double)ry * (double)ry + (double)rz * (double)rz);
    if (d >= WIBBLE_FADE_FAR)
        return offset;
    if (d <= WIBBLE_FADE_NEAR)
        return 0;
    return (int64_t)((double)offset * (d - WIBBLE_FADE_NEAR) / (double)(WIBBLE_FADE_FAR - WIBBLE_FADE_NEAR));
}
/******************************************************************************/

/******************************************************************************/
// Overhead views have no distance fade in the engine (fade_min is set out of reach), so terrain is
// drawn at full brightness right to the edge of the screen. This adds a very subtle vignette: shade
// is scaled down with the (screen) distance of a vertex from the centre of the view, measured in
// tiles at the current zoom, easing (smoothstep) from no change at OVERHEAD_FADE_START_TILES to
// (100 - the OVERHEAD_FADE setting)% of the brightness at OVERHEAD_FADE_END_TILES and beyond.
#define OVERHEAD_FADE_START_TILES 3
#define OVERHEAD_FADE_END_TILES 10

static int64_t overhead_fade_shade(int64_t shade, int64_t view_w, int64_t view_h)
{
    const int64_t zoom = camera_zoom / pixel_size;
    if (zoom <= 0)
        return shade;
    const double tile_px = 768.0 * (double)zoom / 65536.0;
    if (tile_px < 1.0)
        return shade;
    const double dx = (double)(view_w - view_width_over_2);
    const double dy = (double)(view_h - view_height_over_2);
    const double tiles = sqrt(dx * dx + dy * dy) / tile_px;
    double t = (tiles - OVERHEAD_FADE_START_TILES) / (double)(OVERHEAD_FADE_END_TILES - OVERHEAD_FADE_START_TILES);
    if (t <= 0.0)
        return shade;
    if (t > 1.0)
        t = 1.0;
    t = t * t * (3.0 - 2.0 * t);
    const int64_t strength = keeperfx_ui_config.overhead_fade; // percent dimming at full effect
    if (strength <= 0)
        return shade;
    const double scale = 1.0 - t * (double)(strength > 100 ? 100 : strength) / 100.0;
    return (int64_t)((double)shade * scale);
}
/******************************************************************************/

static struct BasicQ *get_bucket_item(int64_t min_cor_z, enum QKinds kind, size_t size)
{
    if (getpoly >= poly_pool_end)
    {
        return NULL;
    }

    int64_t bckt_idx = min_cor_z / BUCKETS_STEP;
    if (bckt_idx < 0)
    {
        bckt_idx = 0;
    }
    else if (bckt_idx > BUCKETS_COUNT-2)
    {
        bckt_idx = BUCKETS_COUNT-2;
    }
    struct BasicQ * kspr;
    kspr = (struct BasicQ *)getpoly;
    getpoly += size;
    kspr->next = buckets[bckt_idx];
    kspr->kind = kind;
    buckets[bckt_idx] = (struct BasicQ *)kspr;
    return kspr;
}

static int64_t ABYSS_SHADE(int64_t lightness, int64_t depth)
{
    if (depth > ABYSS_WALL_RENDER_HEIGHT) {
        return 0;
    }
    return lightness * (ABYSS_WALL_TOP_BRIGHTNESS + (ABYSS_WALL_BOTTOM_BRIGHTNESS - ABYSS_WALL_TOP_BRIGHTNESS) * depth / ABYSS_WALL_RENDER_HEIGHT) / 100;
}

static const struct Column *get_abyss_wall_column(const struct Column *colmn, const struct Map *mapblk, MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{
    if ((mapblk->flags & SlbAtFlg_IsRoom) == 0) {
        return colmn;
    }
    struct SlabMap *slb = get_slabmap_for_subtile(stl_x, stl_y);
    if (get_slab_stats(slb)->wlb_type != WlbT_Bridge) {
        return colmn;
    }
    int64_t slbkind = slab_kind_from_wlb_type(slabmap_wlb(slb));
    if (slbkind < 0) {
        return colmn;
    }
    return get_column(-kfx_sim_state.slabset[SLABSETS_PER_SLAB * slbkind].col_idx[(stl_y % STL_PER_SLB) * STL_PER_SLB + stl_x % STL_PER_SLB]);
}

static int64_t get_column_top_cube(const struct Column *colmn)
{
    if (colmn->cubes[0] != 0) {
        return colmn->cubes[0];
    }
    return kfx_sim_state.top_cube[colmn->floor_texture];
}

static TbBool map_block_has_rendered_abyss(const struct Map *mapblk, MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{
    return cube_is_abyss(kfx_sim_state.top_cube[get_abyss_wall_column(get_map_column(mapblk), mapblk, stl_x, stl_y)->floor_texture]);
}

static void fill_in_abyss_points_parallel(struct WibbleTable *wibl, struct EngineCol *ecol, int64_t eview_w, int64_t hview_y, int64_t hview_z, int64_t dview_h, int64_t dview_z, int64_t lightness)
{
    ecol->cors[COLUMN_STACK_HEIGHT + 1] = ecol->cors[0];
    ecol->cors[COLUMN_STACK_HEIGHT + 1].shade_intensity = ABYSS_SHADE(ecol->cors[0].shade_intensity, 0);
    int64_t idxh;
    for (idxh = 1; idxh <= ABYSS_WALL_RENDER_HEIGHT + 1; idxh++) {
        int64_t depth = idxh;
        if (depth > ABYSS_WALL_RENDER_HEIGHT) {
            depth = ABYSS_DEPTH;
        }
        struct EngineCoord *ecord = &ecol->cors[COLUMN_STACK_HEIGHT + idxh + 1];
        struct WibbleTable *abyss_wibl = &wibl[2 * ((depth - 1) % COLUMN_STACK_HEIGHT)];
        ecord->view_width = (eview_w + abyss_wibl->view_width_offset) >> 8;
        ecord->view_height = (hview_y - dview_h * depth + abyss_wibl->view_height_offset) >> 8;
        ecord->z = clamp(hview_z - dview_z * depth, 0, Z_DRAW_DISTANCE_MAX);
        ecord->clip_flags = 0;
        ecord->shade_intensity = ABYSS_SHADE(lightness, depth);
    }
}

static void fill_in_points_perspective(struct Camera *cam, int64_t bstl_x, int64_t bstl_y, struct MinMax *mm)
{
    if ((bstl_y < 0) || (bstl_y > kfx_sim_state.map_subtiles_y-1)) {
        return;
    }
    int64_t mmin;
    int64_t mmax;
    mmin = min(mm[0].min,mm[1].min);
    mmax = max(mm[0].max,mm[1].max);
    if (mmin + bstl_x < 1)
      mmin = 1 - bstl_x;
    if (mmax + bstl_x > kfx_sim_state.map_subtiles_x)
      mmax = kfx_sim_state.map_subtiles_x - bstl_x;
    MapSubtlCoord stl_x;
    MapSubtlCoord stl_y;
    stl_y = bstl_y;
    stl_x = mmin + bstl_x;
    apos += subtile_coord(mmin,0);
    struct EngineCol *ecol;
    ecol = &front_ec[mmin + MINMAX_ALMOST_HALF];
    uint64_t mask_unrev;
    {
        struct Column *col;
        col = get_column(kfx_sim_state.unrevealed_column_idx);
        mask_unrev = col->solidmask + 65536;
    }
    struct Map *mapblk;
    struct Column *col;
    uint64_t pfulmask_or;
    uint64_t pfulmask_and;
    int64_t abyss_mask = 0;
    {
        uint64_t mask_cur;
        uint64_t mask_yp;
        mask_cur = mask_unrev;
        mask_yp = mask_unrev;
        mapblk = get_map_block_at(stl_x-1, stl_y+1);
        if (map_block_revealed(mapblk, my_player_number)) {
            col = get_map_column(mapblk);
            mask_cur = col->solidmask;
            abyss_mask |= map_block_has_rendered_abyss(mapblk, stl_x - 1, stl_y + 1) << 1;
        }
        mapblk = get_map_block_at(stl_x-1, stl_y);
        if (map_block_revealed(mapblk, my_player_number)) {
            col = get_map_column(mapblk);
            mask_yp = col->solidmask;
            abyss_mask |= map_block_has_rendered_abyss(mapblk, stl_x - 1, stl_y);
        }
        pfulmask_or = mask_cur | mask_yp;
        pfulmask_and = mask_cur & mask_yp;
    }

    int64_t wib_x;
    int64_t wib_y;
    int64_t wib_v;
    wib_y = (stl_y + 1) & 3;
    int64_t idxx;
    for (idxx=mmax-mmin+1; idxx > 0; idxx--)
    {
        uint64_t mask_cur;
        uint64_t mask_yp;
        mask_cur = mask_unrev;
        mask_yp = mask_unrev;
        mapblk = get_map_block_at(stl_x, stl_y+1);
        wib_v = get_mapblk_wibble_value(mapblk);
        if (map_block_revealed(mapblk, my_player_number)) {
            col = get_map_column(mapblk);
            mask_cur = col->solidmask;
            abyss_mask |= map_block_has_rendered_abyss(mapblk, stl_x, stl_y + 1) << 3;
        }
        mapblk = get_map_block_at(stl_x, stl_y);
        if (map_block_revealed(mapblk, my_player_number)) {
            col = get_map_column(mapblk);
            mask_yp = col->solidmask;
            abyss_mask |= map_block_has_rendered_abyss(mapblk, stl_x, stl_y) << 2;
        }
        uint64_t nfulmask_or;
        uint64_t nfulmask_and;
        nfulmask_or = mask_cur | mask_yp;
        nfulmask_and = mask_cur & mask_yp;
        uint64_t fulmask_or;
        uint64_t fulmask_and;
        fulmask_or = nfulmask_or | pfulmask_or;
        fulmask_and = nfulmask_and & pfulmask_and;
        pfulmask_or = nfulmask_or;
        pfulmask_and = nfulmask_and;
        int64_t lightness;
        lightness = 0;
        if ((fulmask_or & 0x10000) == 0)
            lightness = get_subtile_lightness(&lish,stl_x,stl_y+1);
        int64_t hmin;
        int64_t hmax;
        hmax = height_masks[fulmask_or & 0xff];
        hmin = floor_height_table[fulmask_and & 0xff];
        if ((hmin > 0) && (abyss_mask != 0)) {
            hmin = 0;
        }
        struct EngineCoord *ecord;
        ecord = &ecol->cors[hmin];
        int64_t hpos;
        hpos = subtile_coord(hmin,0) - view_alt;
        wib_x = stl_x & 3;
        struct WibbleTable *wibl;
        wibl = get_wibble_from_table(cam, 32 * wib_v + wib_x + (wib_y << 2), stl_x, stl_y);
        int64_t idxh;
        for (idxh = hmax-hmin+1; idxh > 0; idxh--)
        {
            ecord->x = apos + wibble_near_scale(cam, apos, hpos, bpos, wibl->offset_x);
            ecord->y = hpos + wibble_near_scale(cam, apos, hpos, bpos, wibl->offset_y);
            ecord->z = bpos + wibble_near_scale(cam, apos, hpos, bpos, wibl->offset_z);
            ecord->clip_flags = 0;
            lightness += wibl->lightness_offset;
            if (lightness < 0)
                lightness = 0;
            if (lightness > 16128)
                lightness = 16128;
            ecord->shade_intensity = lightness;
            wibl += 2;
            hpos += COORD_PER_STL;
            rotpers(ecord, &camera_matrix);
            ecord++;
        }
        wibl -= 2;
        // Set ceiling
        mapblk = get_map_block_at(stl_x, stl_y+1);
        wib_v = get_mapblk_wibble_value(mapblk);
        hpos = subtile_coord(get_mapblk_filled_subtiles(mapblk),0) - view_alt;
        if (wib_v == 2)
        {
            wibl = get_wibble_from_table(cam, wib_x + 2 * (hmax + 2 * wib_y - hmin) + 32, stl_x, stl_y);
        }
        ecord = &ecol->cors[ENGINE_COL_CEILING_CORNER];
        {
            ecord->x = apos + wibble_near_scale(cam, apos, hpos, bpos, wibl->offset_x);
            ecord->y = hpos + wibble_near_scale(cam, apos, hpos, bpos, wibl->offset_y);
            ecord->z = bpos + wibble_near_scale(cam, apos, hpos, bpos, wibl->offset_z);
            ecord->clip_flags = 0;
            // Use lightness from last cube
            ecord->shade_intensity = lightness;
            rotpers(ecord, &camera_matrix);
        }
        if (abyss_mask != 0) {
            wibl = get_wibble_from_table(cam, 32 * wib_v + wib_x + (wib_y << 2), stl_x, stl_y);
            ecol->cors[COLUMN_STACK_HEIGHT + 1] = ecol->cors[0];
            ecol->cors[COLUMN_STACK_HEIGHT + 1].shade_intensity = ABYSS_SHADE(ecol->cors[0].shade_intensity, 0);
            for (idxh = 1; idxh <= ABYSS_WALL_RENDER_HEIGHT + 1; idxh++) {
                int64_t depth = idxh;
                if (depth > ABYSS_WALL_RENDER_HEIGHT) {
                    depth = ABYSS_DEPTH;
                }
                ecord = &ecol->cors[COLUMN_STACK_HEIGHT + idxh + 1];
                struct WibbleTable *abyss_wibl = &wibl[2 * ((depth - 1) % COLUMN_STACK_HEIGHT)];
                ecord->x = apos + abyss_wibl->offset_x;
                ecord->y = -depth * COORD_PER_STL - view_alt + abyss_wibl->offset_y;
                ecord->z = bpos + abyss_wibl->offset_z;
                ecord->clip_flags = 0;
                ecord->shade_intensity = ABYSS_SHADE(lightness, depth);
                rotpers(ecord, &camera_matrix);
            }
        }
        abyss_mask >>= 2;
        stl_x++;
        ecol++;
        apos += COORD_PER_STL;
    }
}

static void fill_in_points_cluedo(struct Camera *cam, int64_t bstl_x, int64_t bstl_y, struct MinMax *mm)
{
    if ((bstl_y < 0) || (bstl_y > kfx_sim_state.map_subtiles_y-1)) {
        return;
    }
    int64_t mmin;
    int64_t mmax;
    mmin = min(mm[0].min,mm[1].min);
    mmax = max(mm[0].max,mm[1].max);
    if (mmin + bstl_x < 1) {
        mmin = 1 - bstl_x;
    }
    if (mmax + bstl_x > kfx_sim_state.map_subtiles_x) {
        mmax = kfx_sim_state.map_subtiles_x - bstl_x;
    }
    if (mmax < mmin) {
        return;
    }
    MapSubtlCoord stl_x;
    MapSubtlCoord stl_y;
    stl_y = bstl_y;
    stl_x = mmin + bstl_x;
    apos += (mmin << 8);
    struct EngineCol *ecol;
    ecol = &front_ec[mmin + MINMAX_ALMOST_HALF];
    uint64_t mask_unrev;
    {
        struct Column *col;
        col = get_column(kfx_sim_state.unrevealed_column_idx);
        mask_unrev = (col->solidmask & 3) + 65536;
    }
    struct Map *mapblk;
    struct Column *col;
    uint64_t pfulmask_or;
    uint64_t pfulmask_and;
    int64_t abyss_mask = 0;
    {
        uint64_t mask_cur;
        uint64_t mask_yp;
        mask_cur = mask_unrev;
        mask_yp = mask_unrev;
        mapblk = get_map_block_at(stl_x-1, stl_y+1);
        if (map_block_revealed(mapblk, my_player_number)) {
            col = get_map_column(mapblk);
            mask_cur = col->solidmask;
            abyss_mask |= map_block_has_rendered_abyss(mapblk, stl_x - 1, stl_y + 1) << 1;
            if ((mask_cur >= 8) && ((mapblk->flags & (SlbAtFlg_IsDoor|SlbAtFlg_IsRoom)) == 0) && ((col->bitfields & 0xE) == 0)) {
                mask_cur &= 3;
            }
        }
        mapblk = get_map_block_at(stl_x-1, stl_y);
        if (map_block_revealed(mapblk, my_player_number)) {
            col = get_map_column(mapblk);
            mask_yp = col->solidmask;
            abyss_mask |= map_block_has_rendered_abyss(mapblk, stl_x - 1, stl_y);
            if ((mask_yp >= 8) && ((mapblk->flags & (SlbAtFlg_IsDoor|SlbAtFlg_IsRoom)) == 0) && ((col->bitfields & 0xE) == 0)) {
                mask_yp &= 3;
            }
        }
        pfulmask_or = mask_cur | mask_yp;
        pfulmask_and = mask_cur & mask_yp;
    }
    int64_t view_z;
    int64_t zoom;
    int64_t eview_w;
    int64_t eview_h;
    int64_t eview_z;
    int64_t hview_y;
    int64_t hview_z;
    zoom = camera_zoom / pixel_size;
    view_z = object_origin.z + (cells_away << 8)
        + ((bpos * camera_matrix.r[2].v[2]
         + (apos + camera_matrix.r[2].v[1]) * (camera_matrix.r[2].v[0] - view_alt)
          - camera_matrix.r[2].v[3]
          - apos * -view_alt) >> 14);
    eview_w = (view_width_over_2 + (zoom
          * (object_origin.x
           + ((bpos * camera_matrix.r[0].v[2]
            + (apos + camera_matrix.r[0].v[1]) * (camera_matrix.r[0].v[0] - view_alt)
             - camera_matrix.r[0].v[3]
             - apos * -view_alt) >> 14)) >> 16)) << 8;
    hview_y = (view_height_over_2 - (zoom
          * (object_origin.y
           + ((bpos * camera_matrix.r[1].v[2]
            + (apos + camera_matrix.r[1].v[1]) * (camera_matrix.r[1].v[0] - view_alt)
             - camera_matrix.r[1].v[3]
             - apos * -view_alt) >> 14)) >> 16)) << 8;
    hview_z = (llabs(view_z) >> 1);
    if (hview_z < 32) {
        hview_z = 0;
    } else
    if (hview_z >= Z_DRAW_DISTANCE_MAX) {
        hview_z = Z_DRAW_DISTANCE_MAX;
    }
    int64_t dview_w;
    int64_t dview_h;
    int64_t dview_z;
    int64_t dhview_y;
    int64_t dhview_z;

    dview_w = zoom * camera_matrix.r[0].v[0] >> 14;
    dhview_y = -(zoom * camera_matrix.r[1].v[0]) >> 14;
    dhview_z = camera_matrix.r[2].v[0] >> 7;
    dview_h = -(zoom * camera_matrix.r[1].v[1]) >> 14;
    dview_z = camera_matrix.r[2].v[1] >> 7;
    int64_t wib_x;
    int64_t wib_y;
    int64_t wib_v;
    wib_y = (stl_y + 1) & 3;
    int64_t idxx;
    for (idxx=mmax-mmin+1; idxx > 0; idxx--)
    {
        uint64_t mask_cur;
        uint64_t mask_yp;
        mask_cur = mask_unrev;
        mask_yp = mask_unrev;
        mapblk = get_map_block_at(stl_x, stl_y+1);
        wib_v = get_mapblk_wibble_value(mapblk);
        if (map_block_revealed(mapblk, my_player_number)) {
            col = get_map_column(mapblk);
            mask_cur = col->solidmask;
            abyss_mask |= map_block_has_rendered_abyss(mapblk, stl_x, stl_y + 1) << 3;
            if ((mask_cur >= 8) && ((mapblk->flags & (SlbAtFlg_IsDoor|SlbAtFlg_IsRoom)) == 0) && ((col->bitfields & 0xE) == 0)) {
                mask_cur &= 3;
            }
        }
        mapblk = get_map_block_at(stl_x, stl_y);
        if (map_block_revealed(mapblk, my_player_number)) {
            col = get_map_column(mapblk);
            mask_yp = col->solidmask;
            abyss_mask |= map_block_has_rendered_abyss(mapblk, stl_x, stl_y) << 2;
            if ((mask_yp >= 8) && ((mapblk->flags & (SlbAtFlg_IsDoor|SlbAtFlg_IsRoom)) == 0) && ((col->bitfields & 0xE) == 0)) {
                mask_yp &= 3;
            }
        }
        uint64_t nfulmask_or;
        uint64_t nfulmask_and;
        nfulmask_or = mask_cur | mask_yp;
        nfulmask_and = mask_cur & mask_yp;
        uint64_t fulmask_or;
        uint64_t fulmask_and;
        fulmask_or = nfulmask_or | pfulmask_or;
        fulmask_and = nfulmask_and & pfulmask_and;
        pfulmask_or = nfulmask_or;
        pfulmask_and = nfulmask_and;
        int64_t lightness;
        lightness = 0;
        if ((fulmask_or & 0x10000) == 0)
            lightness = get_subtile_lightness(&lish,stl_x,stl_y+1);

        int64_t hmin;
        int64_t hmax;
        hmax = height_masks[fulmask_or & 0xff];
        hmin = floor_height_table[fulmask_and & 0xff];
        if ((hmin > 0) && (abyss_mask != 0)) {
            hmin = 0;
        }
        struct EngineCoord *ecord;
        ecord = &ecol->cors[hmin];
        wib_x = stl_x & 3;
        struct WibbleTable *wibl;
        wibl = get_wibble_from_table(cam, 32 * wib_v + wib_x + (wib_y << 2), stl_x, stl_y);
        int64_t *randmis;
        randmis = &randomisors[(stl_x + 17 * (stl_y + 1)) & 0xff];
        eview_h = dview_h * hmin + hview_y;
        eview_z = dview_z * hmin + hview_z;
        int64_t idxh;
        for (idxh = hmax-hmin+1; idxh > 0; idxh--)
        {
            ecord->view_width = (eview_w + wibl->view_width_offset) >> 8;
            ecord->view_height = (eview_h + wibl->view_height_offset) >> 8;
            ecord->z = eview_z;
            ecord->clip_flags = 0;
            lightness += *randmis;
            if (lightness < 0)
                lightness = 0;
            if (lightness > 16128)
                lightness = 16128;
            ecord->shade_intensity = overhead_fade_shade(lightness, ecord->view_width, ecord->view_height);
            if (ecord->z < 32) {
                ecord->z = 0;
            } else
            if (ecord->z >= Z_DRAW_DISTANCE_MAX) {
                ecord->z = Z_DRAW_DISTANCE_MAX;
            }
            if (ecord->view_width < 0) {
                ecord->clip_flags |= 0x08;
            } else
            if (ecord->view_width >= vec_window_width) {
                ecord->clip_flags |= 0x10;
            }
            if (ecord->view_height < 0) {
                ecord->clip_flags |= 0x20;
            } else
            if (ecord->view_height >= vec_window_height) {
                ecord->clip_flags |= 0x40;
            }

            wibl += 2;
            ecord++;
            randmis++;
            eview_h += dview_h;
            eview_z += dview_z;
        }
        if (abyss_mask != 0) {
            fill_in_abyss_points_parallel(wibl - 2 * (hmax - hmin + 1), ecol, eview_w, hview_y, hview_z, dview_h, dview_z, lightness);
        }
        abyss_mask >>= 2;
        stl_x++;
        ecol++;
        apos += 256;
        eview_w += dview_w;
        hview_y += dhview_y;
        hview_z += dhview_z;
    }
}

static void fill_in_points_isometric(struct Camera *cam, int64_t bstl_x, int64_t bstl_y, struct MinMax *mm)
{
    if ((bstl_y < 0) || (bstl_y > kfx_sim_state.map_subtiles_y-1)) {
        return;
    }
    int64_t mmin;
    int64_t mmax;
    TbBool clip_min;
    TbBool clip_max;
    mmin = min(mm[0].min,mm[1].min);
    mmax = max(mm[0].max,mm[1].max);
    clip_min = false;
    clip_max = false;
    if (mmin + bstl_x <= 1) {
        clip_min = true;
        mmin = 1 - bstl_x;
    }
    if (mmax + bstl_x >= kfx_sim_state.map_subtiles_x) {
        clip_max = true;
        mmax = kfx_sim_state.map_subtiles_x - bstl_x;
    }
    if (mmax < mmin) {
        return;
    }
    MapSubtlCoord stl_x;
    MapSubtlCoord stl_y;
    stl_y = bstl_y;
    stl_x = mmin + bstl_x;
    TbBool lim_min;
    TbBool lim_max;
    lim_min = (stl_y <= 0);
    lim_max = (stl_y >= kfx_sim_state.map_subtiles_y-1);
    TbBool clip;
    clip = clip_min | clip_max | lim_max | lim_min;
    apos += (mmin << 8);
    struct EngineCol *ecol;
    ecol = &front_ec[mmin + MINMAX_ALMOST_HALF];
    uint64_t mask_unrev;
    {
        struct Column *col;
        col = get_column(kfx_sim_state.unrevealed_column_idx);
        mask_unrev = col->solidmask + 65536;
    }
    struct Map *mapblk;
    struct Column *col;
    uint64_t pfulmask_or;
    uint64_t pfulmask_and;
    int64_t abyss_mask = 0;
    {
        uint64_t mask_cur;
        uint64_t mask_yp;
        mask_cur = mask_unrev;
        mask_yp = mask_unrev;
        mapblk = get_map_block_at(stl_x-1, stl_y+1);
        if (map_block_revealed(mapblk, my_player_number)) {
            col = get_map_column(mapblk);
            mask_cur = col->solidmask;
            abyss_mask |= map_block_has_rendered_abyss(mapblk, stl_x - 1, stl_y + 1) << 1;
        }
        mapblk = get_map_block_at(stl_x-1, stl_y);
        if (map_block_revealed(mapblk, my_player_number)) {
            col = get_map_column(mapblk);
            mask_yp = col->solidmask;
            abyss_mask |= map_block_has_rendered_abyss(mapblk, stl_x - 1, stl_y);
        }
        if (clip)
        {
            if (clip_min || lim_max)
                mask_cur = 0;
            if (clip_min || lim_min)
                mask_yp = 0;
        }
        pfulmask_or = mask_cur | mask_yp;
        pfulmask_and = mask_cur & mask_yp;
    }

    int64_t hpos;
    int64_t view_x;
    int64_t view_y;
    int64_t view_z;
    int64_t zoom;
    int64_t hview_z;

    zoom = camera_zoom / pixel_size;
    hpos = -view_alt * apos;
    view_x = view_width_over_2 + (zoom
         * (object_origin.x
          + ((bpos * camera_matrix.r[0].v[2]
           + (apos + camera_matrix.r[0].v[1]) * (camera_matrix.r[0].v[0] - view_alt)
            - hpos - camera_matrix.r[0].v[3]) >> 14)) >> 16);
    view_y = view_height_over_2 - (zoom
         * (object_origin.y
          + ((bpos * camera_matrix.r[1].v[2]
           + (apos + camera_matrix.r[1].v[1]) * (camera_matrix.r[1].v[0] - view_alt)
            - hpos - camera_matrix.r[1].v[3]) >> 14)) >> 16);
    view_z = object_origin.z + (cells_away << 8)
        + ((bpos * camera_matrix.r[2].v[2]
         + (apos + camera_matrix.r[2].v[1]) * (camera_matrix.r[2].v[0] - view_alt)
          - hpos - camera_matrix.r[2].v[3]) >> 14);
    hview_z = (llabs(view_z) >> 1);
    if (hview_z < 32) {
        hview_z = 0;
    } else
    if (hview_z >= Z_DRAW_DISTANCE_MAX) {
        hview_z = Z_DRAW_DISTANCE_MAX;
    }
    int64_t eview_w;
    int64_t eview_h;
    int64_t eview_z;
    int64_t hview_y;
    int64_t *randmis;
    int64_t dview_w;
    int64_t dview_h;
    int64_t dview_z;
    int64_t dhview_y;
    int64_t dhview_z;

    eview_w = view_x << 8;
    hview_y = view_y << 8;
    dview_w = zoom * camera_matrix.r[0].v[0] >> 14;
    dhview_y = -(zoom * camera_matrix.r[1].v[0]) >> 14;
    dhview_z = camera_matrix.r[2].v[0] >> 7;
    dview_h = -(zoom * camera_matrix.r[1].v[1]) >> 14;
    dview_z = camera_matrix.r[2].v[1] >> 7;
    int64_t wib_x;
    int64_t wib_y;
    int64_t wib_v;
    wib_y = (stl_y + 1) & 3;
    int64_t idxx;
    for (idxx=mmax-mmin+1; idxx > 0; idxx--)
    {
        uint64_t mask_cur;
        uint64_t mask_yp;
        mask_cur = mask_unrev;
        mask_yp = mask_unrev;
        mapblk = get_map_block_at(stl_x, stl_y+1);
        wib_v = get_mapblk_wibble_value(mapblk);
        if (map_block_revealed(mapblk, my_player_number)) {
            col = get_map_column(mapblk);
            mask_cur = col->solidmask;
            abyss_mask |= map_block_has_rendered_abyss(mapblk, stl_x, stl_y + 1) << 3;
        }
        mapblk = get_map_block_at(stl_x, stl_y);
        if (map_block_revealed(mapblk, my_player_number)) {
            col = get_map_column(mapblk);
            mask_yp = col->solidmask;
            abyss_mask |= map_block_has_rendered_abyss(mapblk, stl_x, stl_y) << 2;
        }
        if (clip)
        {
            if (clip_max && (idxx == 1)) {
                mask_cur = 0;
                mask_yp = 0;
            }
            if (lim_max)
                mask_cur = 0;
            if (lim_min)
                mask_yp = 0;
        }
        uint64_t nfulmask_or;
        uint64_t nfulmask_and;
        nfulmask_or = mask_cur | mask_yp;
        nfulmask_and = mask_cur & mask_yp;
        uint64_t fulmask_or;
        uint64_t fulmask_and;
        fulmask_or = nfulmask_or | pfulmask_or;
        fulmask_and = nfulmask_and & pfulmask_and;
        pfulmask_or = nfulmask_or;
        pfulmask_and = nfulmask_and;
        int64_t lightness;
        lightness = 0;
        if ((fulmask_or & 0x10000) == 0)
            lightness = get_subtile_lightness(&lish,stl_x,stl_y+1);
        int64_t hmin;
        int64_t hmax;
        hmax = height_masks[fulmask_or & 0xff];
        hmin = floor_height_table[fulmask_and & 0xff];
        if ((hmin > 0) && (abyss_mask != 0)) {
            hmin = 0;
        }
        struct EngineCoord *ecord;
        ecord = &ecol->cors[hmin];
        wib_x = stl_x & 3;
        struct WibbleTable *wibl;
        wibl = get_wibble_from_table(cam, 32 * wib_v + wib_x + (wib_y << 2), stl_x, stl_y);
        eview_h = dview_h * hmin + hview_y;
        eview_z = dview_z * hmin + hview_z;
        randmis = &randomisors[(stl_x + 17 * (stl_y+1)) & 0xff] + hmin;
        int64_t idxh;
        for (idxh = hmax-hmin+1; idxh > 0; idxh--)
        {
            ecord->view_width = (eview_w + wibl->view_width_offset) >> 8;
            ecord->view_height = (eview_h + wibl->view_height_offset) >> 8;
            ecord->z = eview_z;
            ecord->clip_flags = 0;
            lightness += 4 * (*randmis & 0xff) - 512;
            if (lightness < 0)
                lightness = 0;
            if (lightness > 15872)
                lightness = 15872;
            ecord->shade_intensity = overhead_fade_shade(lightness, ecord->view_width, ecord->view_height);
            if (ecord->z < 32) {
                ecord->z = 0;
            } else
            if (ecord->z >= Z_DRAW_DISTANCE_MAX) {
                ecord->z = Z_DRAW_DISTANCE_MAX;
            }
            if (ecord->view_width < 0) {
                ecord->clip_flags |= 0x08;
            } else
            if (ecord->view_width >= vec_window_width) {
                ecord->clip_flags |= 0x10;
            }
            if (ecord->view_height < 0) {
                ecord->clip_flags |= 0x20;
            } else
            if (ecord->view_height >= vec_window_height) {
                ecord->clip_flags |= 0x40;
            }
            wibl += 2;
            ecord++;
            randmis++;
            eview_h += dview_h;
            eview_z += dview_z;
        }
        if (abyss_mask != 0) {
            fill_in_abyss_points_parallel(wibl - 2 * (hmax - hmin + 1), ecol, eview_w, hview_y, hview_z, dview_h, dview_z, lightness);
        }
        abyss_mask >>= 2;
        stl_x++;
        ecol++;
        apos += 256;
        hview_z += dhview_z;
        eview_w += dview_w;
        hview_y += dhview_y;
    }
}

void frame_wibble_generate(void)
{
    int64_t i;
    struct WibbleTable *wibl;
    wibl = &wibble_table[64];
    for (i = 0; i < 16; i++)
    {
        int64_t angle;
        int64_t osc;
        angle = water_wibble_angle + ((i & 0xFFFC) * ((i & 3) + 1) << 7);
        osc = LbSinL(angle);
        wibl->offset_y = osc >> 11;
        wibl->lightness_offset = osc >> 6;
        wibl++;
    }
    render_water_wibble += DEGREES_8_18 * kfx_render_state.delta_time;
    water_wibble_angle = (int64_t)render_water_wibble & ANGLE_MASK;
    render_abyss_lava_scroll += ABYSS_LAVA_SCROLL_SPEED * kfx_render_state.delta_time;
    if (render_abyss_lava_scroll >= ABYSS_LIQUID_SCROLL_CYCLE) {
        render_abyss_lava_scroll -= ABYSS_LIQUID_SCROLL_CYCLE;
    }
    render_abyss_water_scroll += ABYSS_WATER_SCROLL_SPEED * kfx_render_state.delta_time;
    if (render_abyss_water_scroll >= ABYSS_LIQUID_SCROLL_CYCLE) {
        render_abyss_water_scroll -= ABYSS_LIQUID_SCROLL_CYCLE;
    }
    int64_t zoom;
    {
        zoom = camera_zoom / pixel_size;
    }

    int64_t zm00;
    int64_t zm02;
    int64_t zm10;
    int64_t zm11;
    int64_t zm12;
    zm00 = zoom * camera_matrix.r[0].v[0] >> 14;
    zm02 = zoom * camera_matrix.r[0].v[2] >> 14;
    zm10 = zoom * camera_matrix.r[1].v[0] >> 14;
    zm12 = zoom * camera_matrix.r[1].v[2] >> 14;
    zm11 = zoom * camera_matrix.r[1].v[1] >> 14;

    wibl = &wibble_table[32];
    for (i=64; i > 0; i--)
    {
        wibl->view_width_offset =   ((zm00 * wibl->offset_x) >> 8)
                         + ((zm02 * wibl->offset_z) >> 8);
        wibl->view_height_offset = -(((zm12 * wibl->offset_z) >> 8)
                         + ((zm10 * wibl->offset_x) >> 8)
                         + ((zm11 * wibl->offset_y) >> 8));
        wibl++;
    }
}

void setup_rotate_stuff(int64_t x, int64_t y, int64_t z, int64_t rotate_fade_max, int64_t rotate_fade_min, int64_t zoom, int64_t map_angle, int64_t rotate_map_roll)
{
    view_width_over_2 = vec_window_width / 2;
    view_height_over_2 = vec_window_height / 2;
    map_x_pos = x;
    map_y_pos = y;
    map_z_pos = z;
    thelens = zoom;
    spr_map_angle = map_angle;
    lfade_min = rotate_fade_min;
    lfade_max = rotate_fade_max;
    fade_mmm = rotate_fade_max - rotate_fade_min;
}

static void create_box_coords(struct EngineCoord *coord, int64_t x, int64_t z, int64_t y)
{
    coord->x = x;
    coord->z = z;
    coord->clip_flags = 0;
    coord->y = y;
    rotpers(coord, &camera_matrix);
}

static void do_perspective_rotation(int64_t x, int64_t y, int64_t z)
{
    struct EngineCoord epos;
    int64_t zoom;
    int64_t engine_w;
    int64_t engine_h;
    zoom = camera_zoom / pixel_size;
    engine_w = local_state.engine_window_width/pixel_size;
    engine_h = local_state.engine_window_height/pixel_size;
    epos.x = -x;
    epos.y = 0;
    epos.z = y;
    rotpers_parallel_3(&epos, &camera_matrix, zoom);
    x_init_off = epos.view_width;
    y_init_off = epos.view_height;
    depth_init_off = epos.z;
    epos.x = 65536;
    epos.y = 0;
    epos.z = 0;
    rotpers_parallel_3(&epos, &camera_matrix, zoom);
    hori_offset[0] = epos.view_width - (engine_w >> 1);
    hori_offset[1] = epos.view_height - (engine_h >> 1);
    hori_offset[2] = epos.z;
    epos.x = 0;
    epos.y = 0;
    epos.z = -65536;
    rotpers_parallel_3(&epos, &camera_matrix, zoom);
    vert_offset[0] = epos.view_width - (engine_w >> 1);
    vert_offset[1] = epos.view_height - (engine_h >> 1);
    vert_offset[2] = epos.z;
    epos.x = 0;
    epos.y = 65536;
    epos.z = 0;
    rotpers_parallel_3(&epos, &camera_matrix, zoom);
    high_offset[0] = epos.view_width - (engine_w >> 1);
    high_offset[1] = epos.view_height - (engine_h >> 1);
    high_offset[2] = epos.z;
}

static void find_gamut(void)
{
    SYNCDBG(19,"Starting");
    {
        int64_t cell_cur;
        int64_t cell_lim;
        struct MinMax *mml;
        struct MinMax *mmr;
        cell_lim = cells_away + 1;
        mml = &minmaxs[MINMAX_ALMOST_HALF];
        mmr = &minmaxs[MINMAX_ALMOST_HALF];
        for (cell_cur = 0; cell_cur < cell_lim; cell_cur++)
        {
            int64_t dist;
            dist = LbSqrL(cell_lim * cell_lim - cell_cur * cell_cur);
            mmr->max = dist;
            mml->max = dist;
            dist = -mmr->max;
            mmr->min = dist;
            mml->min = dist;
            mmr++;
            mml--;
        }
    }
    if (lens_mode == 0) {
        return;
    }

    int64_t angle_sin;
    int64_t angle_cos;
    angle_sin = LbSinL(cam_map_angle);
    angle_cos = LbCosL(cam_map_angle);
    int64_t cells_w;
    int64_t cells_h;
    cells_h = 6 * angle_cos >> 16;
    cells_w = -6 * angle_sin >> 16;
    int64_t scr_w1;
    int64_t scr_h1;
    int64_t scr_w2;
    int64_t scr_h2;
    int64_t screen_dist;
    screen_dist = (lbDisplay.PhysicalScreenWidth << 7) / lens;
    scr_w1 = cells_w + ((screen_dist * angle_cos - (angle_sin << 8)) >> 16);
    scr_h1 = cells_h + (((angle_cos << 8) + screen_dist * angle_sin) >> 16);
    scr_w2 = cells_w + ((-screen_dist * angle_cos - (angle_sin << 8)) >> 16);
    scr_h2 = cells_h + (((angle_cos << 8) - screen_dist * angle_sin) >> 16);
    int64_t mbase;
    int64_t delta;
    struct MinMax *mm;
    int64_t cell_curr;
    if (scr_h1 < cells_h)
    {
        delta = ((scr_w1 - cells_w) << 8) / (scr_h1 - cells_h);
        mm = &minmaxs[-cells_away + MINMAX_ALMOST_HALF];
        mbase = delta * (-cells_away - cells_h);
        for (cell_curr = -cells_away; cell_curr <= cells_away; cell_curr++)
        {
            int64_t nlimit;
            nlimit = cells_w + (mbase >> 8);
            if (mm->max > nlimit)
                mm->max = nlimit;
            mm++;
            mbase += delta;
        }
    } else
    if (scr_h1 > cells_h)
    {
        delta = ((scr_w1 - cells_w) << 8) / (scr_h1 - cells_h);
        mm = &minmaxs[-cells_away + MINMAX_ALMOST_HALF];
        mbase = delta * (-cells_away - cells_h);
        for (cell_curr = -cells_away; cell_curr <= cells_away; cell_curr++)
        {
            int64_t nlimit;
            nlimit = cells_w + (mbase >> 8);
            if (mm->min < nlimit)
                mm->min = nlimit;
            mm++;
            mbase += delta;
        }
    } else
    {
        if (scr_w1 <= cells_w)
        {
            mm = &minmaxs[cells_h + MINMAX_ALMOST_HALF];
            for (cell_curr = cells_h; cell_curr >= -cells_away; cell_curr--)
            {
                mm->max = 0;
                mm->min = 0;
                mm--;
            }
        } else
        {
            mm = &minmaxs[cells_h + MINMAX_ALMOST_HALF];
            for (cell_curr = cells_h; cell_curr <= cells_away; cell_curr++)
            {
                mm->max = 0;
                mm->min = 0;
                mm++;
            }
        }
    }

    if (scr_h2 < cells_h)
    {
        delta = ((scr_w2 - cells_w) << 8) / (scr_h2 - cells_h);
        mm = &minmaxs[-cells_away + MINMAX_ALMOST_HALF];
        mbase = delta * (-cells_away - cells_h);
        for (cell_curr = -cells_away; cell_curr <= cells_away; cell_curr++)
        {
            int64_t nlimit;
            nlimit = cells_w + (mbase >> 8);
            if ( mm->min < nlimit )
              mm->min = nlimit;
            mm++;
            mbase += delta;
        }
    } else
    if (scr_h2 > cells_h)
    {
        delta = ((scr_w2 - cells_w) << 8) / (scr_h2 - cells_h);
        mm = &minmaxs[-cells_away + MINMAX_ALMOST_HALF];
        mbase = delta * (-cells_away - cells_h);
        for (cell_curr = -cells_away; cell_curr <= cells_away; cell_curr++)
        {
            int64_t nlimit;
            nlimit = cells_w + (mbase >> 8);
            if (mm->max > nlimit)
              mm->max = nlimit;
            mm++;
            mbase += delta;
        }
    } else
    {
        if (cells_w <= scr_w2)
        {
            mm = &minmaxs[cells_h + MINMAX_ALMOST_HALF];
            for ( ; cells_h >= -cells_away; cells_h--)
            {
                mm->max = 0;
                mm->min = 0;
                mm--;
            }
        } else
        {
            mm = &minmaxs[cells_h + MINMAX_ALMOST_HALF];
            for ( ; cells_away >= cells_h; cells_h++)
            {
                mm->max = 0;
                mm->min = 0;
                mm++;
            }
        }
    }
}

static void fiddle_half_gamut(int64_t start_stl_x, int64_t start_stl_y, int64_t step, int64_t a4)
{
    int64_t end_stl_x;
    int64_t stl_xc;
    int64_t stl_xp;
    int64_t stl_xn;

    end_stl_x = start_stl_x + minmaxs[(MINMAX_LENGTH/2)].min;
    for (stl_xc=start_stl_x; 1; stl_xc--)
    {
        if (stl_xc < end_stl_x) {
            stl_xc = -4000;
            break;
        }
        struct Map *mapblk;
        mapblk = get_map_block_at(stl_xc, start_stl_y);
        if  ((mapblk->flags & SlbAtFlg_Blocking) != 0) {
            break;
        }
    }
    for (stl_xp=start_stl_x; 1; stl_xp--)
    {
        if (stl_xp < end_stl_x) {
            stl_xp = -4000;
            break;
        }
        struct Map *mapblk;
        mapblk = get_map_block_at(stl_xp, start_stl_y-1);
        if  ((mapblk->flags & SlbAtFlg_Blocking) != 0) {
            break;
        }
    }
    for (stl_xn=start_stl_x; 1; stl_xn--)
    {
        if (stl_xn < end_stl_x) {
            stl_xn = -4000;
            break;
        }
        struct Map *mapblk;
        mapblk = get_map_block_at(stl_xn, start_stl_y-1);
        if  ((mapblk->flags & SlbAtFlg_Blocking) != 0) {
            break;
        }
    }
    int64_t stl_x_min;
    stl_x_min = 0;
    TbBool set_x_min;
    set_x_min = false;
    if ((stl_xc != -4000) && (stl_xp != -4000) && (stl_xn != -4000))
    {
        stl_x_min = min(min(stl_xn, stl_xp), stl_xc);
        set_x_min = true;
        minmaxs[(MINMAX_LENGTH/2)].min = stl_x_min - start_stl_x;
    }

    end_stl_x = start_stl_x + minmaxs[(MINMAX_LENGTH/2)].max;
    for (stl_xc=start_stl_x; 1; stl_xc++)
    {
        if (stl_xc > end_stl_x) {
            stl_xc = -4000;
            break;
        }
        struct Map *mapblk;
        mapblk = get_map_block_at(stl_xc, start_stl_y);
        if  ((mapblk->flags & SlbAtFlg_Blocking) != 0) {
            break;
        }
    }
    for (stl_xp=start_stl_x; 1; stl_xp++)
    {
        if (stl_xp > end_stl_x) {
            stl_xp = -4000;
            break;
        }
        struct Map *mapblk;
        mapblk = get_map_block_at(stl_xp, start_stl_y-1);
        if  ((mapblk->flags & SlbAtFlg_Blocking) != 0) {
            break;
        }
    }
    for (stl_xn=start_stl_x; 1; stl_xn++)
    {
        if (stl_xn > end_stl_x) {
            stl_xn = -4000;
            break;
        }
        struct Map *mapblk;
        mapblk = get_map_block_at(stl_xn, start_stl_y-1);
        if  ((mapblk->flags & SlbAtFlg_Blocking) != 0) {
            break;
        }
    }
    int64_t stl_x_max;
    stl_x_max = 0;
    TbBool set_x_max;
    set_x_max = false;
    if ((stl_xc != -4000) && (stl_xp != -4000) && (stl_xn != -4000))
    {
        stl_x_max = max(max(stl_xn, stl_xp), stl_xc);
        set_x_max = true;
        minmaxs[(MINMAX_LENGTH/2)].max = stl_x_max - start_stl_x + 1;
    }

    struct MinMax *mm;
    int64_t stl_y;
    stl_y = start_stl_y + step;
    mm = &minmaxs[step + (MINMAX_LENGTH/2)];
    int64_t n;
    for (n=1; n < a4; n++)
    {
        if (mm->max <= mm->min)
        {
            int64_t i;
            for (i=a4-n; i > 0; i--)
            {
                mm->min = 0;
                mm->max = 0;
                mm += step;
            }
            break;
        }
        int64_t stl_x_min_limit;
        stl_x_min_limit = start_stl_x + mm->min;
        if (!set_x_min || (stl_x_min < stl_x_min_limit)) {
            stl_x_min = stl_x_min_limit;
        }
        int64_t stl_x_max_limit;
        stl_x_max_limit = start_stl_x + mm->max;
        if (!set_x_max || (stl_x_max > stl_x_max_limit)) {
            stl_x_max = stl_x_max_limit;
        }

        /* The variable needs to be volatile to disallow changing it to float during optimisations.
         * Changing it to float would lead to conditions like "if (delta_y != 1)" not working.
         */
        volatile int64_t delta_y;
        delta_y = llabs(stl_y - start_stl_y);
        int64_t rect_factor;

        TbBool set_x_min_rect;
        if (delta_y != 1) {
            rect_factor = (stl_x_min - start_stl_x) / (delta_y - 1);
        } else {
            rect_factor = 1;
        }
        if (rect_factor - 1 <= 0) {
            set_x_min_rect = false;
        } else {
            set_x_min_rect = true;
            stl_x_min = rect_factor + stl_x_min - 1;
        }

        int64_t stl_x;
        int64_t stl_x_lc_min;

        for (stl_x=stl_x_min-1; stl_x < stl_x_max_limit; stl_x++)
        {
            struct Map *mapblk;
            mapblk = get_map_block_at(stl_x+1, stl_y);
            if ((mapblk->flags & SlbAtFlg_Blocking) == 0)
              break;
        }
        stl_x_lc_min = stl_x;

        if ( set_x_min_rect
          || stl_x_lc_min > stl_x_min
          || (get_map_block_at(stl_x_lc_min, stl_y)->flags & SlbAtFlg_Blocking) )
        {
            int64_t relative_x_offset;
            relative_x_offset = stl_x_min - start_stl_x;
            stl_x_min = stl_x_lc_min;
            set_x_min = true;
            mm->min = relative_x_offset - 1;
        }
        else
        {
          if (delta_y != 1) {
              rect_factor = (stl_x_lc_min - start_stl_x) / (delta_y - 1);
          } else {
              rect_factor = 1;
          }
          int64_t stl_x_min_sublim;
          if ((delta_y == 1) || (stl_x_min + rect_factor - 1 < stl_x_min_limit))
          {
              set_x_min = false;
              stl_x_min_sublim = stl_x_min_limit;
          } else
          {
              stl_x_min += rect_factor - 1;
              set_x_min = true;
              stl_x_min_sublim = stl_x_min;
              mm->min = stl_x_min - start_stl_x - 1;
          }
          for (stl_x=stl_x_min; stl_x >= stl_x_min_sublim; stl_x--)
          {
              struct Map *mapblk;
              mapblk = get_map_block_at(stl_x, stl_y);
              if ((mapblk->flags & SlbAtFlg_Blocking) != 0) {
                  stl_x_min = stl_x;
                  set_x_min = true;
                  mm->min = stl_x - start_stl_x - 1;
                  break;
              }
          }
        }

        TbBool set_x_max_rect;
        if (delta_y != 1) {
            rect_factor = (stl_x_max - start_stl_x) / (delta_y - 1);
        } else {
            rect_factor = 1;
        }
        if (rect_factor + 1 >= 0) {
            set_x_max_rect = false;
        } else {
            set_x_max_rect = true;
            stl_x_max += rect_factor + 1;
        }

        for (stl_x=stl_x_max+1; stl_x > stl_x_min_limit; stl_x--)
        {
            struct Map *mapblk;
            mapblk = get_map_block_at(stl_x-1, stl_y);
            if ((mapblk->flags & SlbAtFlg_Blocking) == 0)
              break;
        }
        stl_x_lc_min = stl_x;

        if ( set_x_max_rect
          || stl_x_lc_min < stl_x_max
          || (get_map_block_at(stl_x_lc_min, stl_y)->flags & SlbAtFlg_Blocking) )
        {
            int64_t stl_tmp;
            stl_tmp = stl_x_max - start_stl_x;
            stl_x_max = stl_x_lc_min;
            mm->max = stl_tmp + 2;
            set_x_max = true;
        }
        else
        {
          set_x_max = 0;
          if (delta_y != 1) {
              rect_factor = (stl_x_lc_min - start_stl_x) / (delta_y - 1);
          } else {
              rect_factor = 1;
          }

          int64_t stl_x_max_sublim;
          if ((delta_y == 1) || (stl_x_max + rect_factor + 1 > start_stl_x + mm->max))
          {
              stl_x_max_sublim = start_stl_x + mm->max;
          } else
          {
              stl_x_max += rect_factor + 1;
              set_x_max = true;
              stl_x_max_sublim = stl_x_max;
              mm->max = stl_x_max - start_stl_x + 2;
          }
          for (stl_x=stl_x_max; stl_x <= stl_x_max_sublim; stl_x++)
          {
              struct Map *mapblk;
              mapblk = get_map_block_at(stl_x, stl_y);
              if ((mapblk->flags & SlbAtFlg_Blocking) != 0) {
                  set_x_max = true;
                  stl_x_max = stl_x;
                  mm->max = stl_x - start_stl_x + 2;
                  break;
              }
          }
        }

        if (mm->min < -cells_away)
            mm->min = -cells_away;
        if (mm->max > cells_away)
            mm->max = cells_away;
        if (mm->min >= mm->max)
        {
            int64_t i;
            for (i=a4-n; i > 0; i--)
            {
                mm->min = 0;
                mm->max = 0;
                mm += step;
            }
            break;
        }
        mm += step;
        stl_y += step;
    }
}

static void fiddle_gamut_find_limits(int64_t *floor_x, int64_t *floor_y, int64_t ewwidth, int64_t ewheight, int64_t ewzoom)
{
    int64_t edge_length_01;
    int64_t edge_length_02;
    int64_t edge_length_13;
    int64_t edge_length_23;
    int64_t tmp_y;
    int64_t tmp_x;
    int64_t i;
    get_floor_pointed_at(ewwidth + ewzoom, -ewzoom, &floor_y[2], &floor_x[2]);
    get_floor_pointed_at(ewwidth + ewzoom, ewheight + ewzoom, &floor_y[1], &floor_x[1]);
    get_floor_pointed_at(-ewzoom, ewheight + ewzoom, &floor_y[0], &floor_x[0]);
    get_floor_pointed_at(-ewzoom, -ewzoom, &floor_y[3], &floor_x[3]);
    // Get the value with lowest X coord into [0]
    for (i=1; i < 4; i++)
    {
        tmp_y = floor_y[i];
        if (floor_y[0] > tmp_y)
        {
          tmp_x = floor_x[i];
          floor_x[i] = floor_x[0];
          floor_x[0] = tmp_x;
          floor_y[i] = floor_y[0];
          floor_y[0] = tmp_y;
        }
    }
    // Get the value with highest X coord into [3]
    for (i=0; i < 3; i++)
    {
        tmp_y = floor_y[i];
        if (floor_y[3] < tmp_y)
        {
          tmp_x = floor_x[i];
          floor_x[i] = floor_x[3];
          floor_x[3] = tmp_x;
          floor_y[i] = floor_y[3];
          floor_y[3] = tmp_y;
        }
    }
    // Between values with medicore X, place the lowest Y first
    if (floor_x[1] > floor_x[2])
    {
        tmp_x = floor_x[1];
        tmp_y = floor_y[1];
        floor_x[1] = floor_x[2];
        floor_x[2] = tmp_x;
        floor_y[1] = floor_y[2];
        floor_y[2] = tmp_y;
    }

    // Lengths of X vectors
    edge_length_01 = llabs(floor_y[1] - floor_y[0]);
    edge_length_13 = llabs(floor_y[3] - floor_y[1]);
    edge_length_02 = llabs(floor_y[2] - floor_y[0]);
    edge_length_23 = llabs(floor_y[3] - floor_y[2]);
    // Update points according to both coordinates
    if ( (floor_x[1] > floor_x[0]) && (edge_length_01 < edge_length_13) )
    {
        tmp_x = floor_x[1];
        floor_y[1] = floor_y[0];
        floor_x[1] = floor_x[0];
        floor_x[0] = tmp_x;
    }
    if ( (floor_x[1] > floor_x[3]) && (edge_length_13 < edge_length_01) )
    {
        tmp_x = floor_x[1];
        floor_y[1] = floor_y[3];
        floor_x[1] = floor_x[3];
        floor_x[3] = tmp_x;
    }
    if ( (floor_x[2] < floor_x[0]) && (edge_length_02 < edge_length_23) )
    {
        tmp_x = floor_x[2];
        floor_y[2] = floor_y[0];
        floor_x[2] = floor_x[0];
        floor_x[0] = tmp_x;
    }
    if ( (floor_x[2] < floor_x[3]) && (edge_length_23 < edge_length_02) )
    {
        tmp_x = floor_x[2];
        floor_x[2] = floor_x[3];
        floor_x[3] = tmp_x;
        floor_y[2] = floor_y[3];
    }
}

static void fiddle_gamut_set_base(int64_t *floor_x, int64_t *floor_y, int64_t pos_x, int64_t pos_y)
{
    floor_x[0] -= pos_x;
    floor_x[1] -= pos_x;
    floor_y[0] += (MINMAX_LENGTH/2) - pos_y;
    floor_x[2] -= pos_x;
    floor_y[1] += (MINMAX_LENGTH/2) - pos_y;
    floor_y[2] += (MINMAX_LENGTH/2) - pos_y;
    floor_x[3] -= pos_x;
    floor_y[3] += (MINMAX_LENGTH/2) - pos_y;
}

static void fiddle_gamut_set_minmaxes(int64_t *floor_x, int64_t *floor_y, int64_t max_tiles)
{
    struct MinMax *mm;
    int64_t mlimit;
    int64_t bormul;
    int64_t bormuh;
    int64_t borinc;
    int64_t bordec;
    int64_t midx;
    midx = 0;
    mlimit = floor_y[0];
    if (mlimit > MINMAX_LENGTH-1)
      mlimit = MINMAX_LENGTH-1;
    for (; midx < mlimit; midx++)
    {
        mm = &minmaxs[midx];
        mm->min = 0;
        mm->max = 0;
    }
    if (floor_y[1] <= floor_y[0])
        borinc = floor_x[0];
    else
        borinc = ((floor_x[1] - floor_x[0]) << 16) / (floor_y[1] - floor_y[0]);

    bormul = (floor_x[0] << 16);
    if (floor_y[0] < 0)
        bormul -= floor_y[0] * borinc;

    mlimit = floor_y[1];
    if (mlimit > MINMAX_LENGTH-1)
      mlimit = MINMAX_LENGTH-1;
    for (; midx < mlimit; midx++)
    {
        mm = &minmaxs[midx];
        bordec = (bormul >> 16);
        if (bordec < -max_tiles)
            mm->min = -max_tiles;
        else
            mm->min = bordec;
        bormul += borinc;
    }

    bormul = floor_x[1] << 16;
    if (floor_y[1] < floor_y[3])
      borinc = ((floor_x[3] - floor_x[1]) << 16) / (floor_y[3] - floor_y[1]);

    mlimit = floor_y[3];
    if (mlimit > MINMAX_LENGTH-1)
      mlimit = MINMAX_LENGTH-1;
    if (midx < 0) {
        bormul -= midx * borinc;
        midx = 0;
    }

    for (; midx < mlimit; midx++)
    {
        mm = &minmaxs[midx];
        bordec = (bormul >> 16);
        if (bordec < -max_tiles)
            mm->min = -max_tiles;
        else
            mm->min = bordec;
        bormul += borinc;
    }
    midx = floor_y[0];
    if (floor_y[2] > floor_y[0])
        borinc = ((floor_x[2] - floor_x[0]) << 16) / (floor_y[2] - floor_y[0]);
    mlimit = floor_y[2];
    if (mlimit > MINMAX_LENGTH-1)
        mlimit = MINMAX_LENGTH-1;
    bormuh = (floor_x[0] << 16);
    if (midx < 0) {
        bormuh -= floor_y[0] * borinc;
        midx = 0;
    }

    for (; midx < mlimit; midx++)
    {
        mm = &minmaxs[midx];
        bordec = (bormuh >> 16) + 1;
        if (bordec > max_tiles)
            mm->max = max_tiles;
        else
            mm->max = bordec;
        bormuh += borinc;
    }

    bormul = floor_x[2] << 16;
    if (floor_y[2] < floor_y[3])
      borinc = ((floor_x[3] - floor_x[2]) << 16) / (floor_y[3] - floor_y[2]);
    mlimit = floor_y[3];
    if (mlimit > MINMAX_LENGTH-1)
      mlimit = MINMAX_LENGTH-1;
    if ( midx < 0 ) {
        bormul -= midx * borinc;
        midx = 0;
    }

    for (; midx < mlimit; midx++)
    {
        mm = &minmaxs[midx];
        bordec = (bormul >> 16) + 1;
        if (bordec > max_tiles)
            mm->max = max_tiles;
        else
            mm->max = bordec;
        bormul += borinc;
    }
    for (; midx <= MINMAX_LENGTH-1; midx++)
    {
        mm = &minmaxs[midx];
        mm->min = 0;
        mm->max = 0;
    }
}

/** Prepares limits for tiles to be rendered.
 *
 * @param pos_x
 * @param pos_y
 */
static void fiddle_gamut(int64_t pos_x, int64_t pos_y)
{
    struct PlayerInfo *player = get_my_player();
    struct Camera *camera = get_local_active_camera(player);
    int64_t ewwidth;
    int64_t ewheight;
    int64_t ewzoom;
    int64_t floor_x[4];
    int64_t floor_y[4];
    switch (camera->view_mode)
    {
    case PVM_CreatureView:
        fiddle_half_gamut(pos_x, pos_y, 1, cells_away);
        fiddle_half_gamut(pos_x, pos_y, -1, cells_away + 2);
        break;
    case PVM_IsoWibbleView:
    case PVM_IsoStraightView:
        // Retrieve coordinates on limiting map points
        ewwidth = local_state.engine_window_width / pixel_size;
        ewheight = local_state.engine_window_height / pixel_size - ((8 * high_offset[1]) >> 8);
        ewzoom = (768 * (camera_zoom/pixel_size)) >> 17;
        fiddle_gamut_find_limits(floor_x, floor_y, ewwidth, ewheight, ewzoom);
        // Place the area at proper base coords
        fiddle_gamut_set_base(floor_x, floor_y, pos_x, pos_y);
        fiddle_gamut_set_minmaxes(floor_x, floor_y, MAX_I_CAN_SEE_OVERHEAD);
        break;
    }
}

int64_t floor_height_for_volume_box(PlayerNumber plyr_idx, MapSlabCoord slb_x, MapSlabCoord slb_y)
{
    struct SlabMap* slb = get_slabmap_block(slb_x, slb_y);
    struct SlabConfigStats* slabst = get_slab_stats(slb);
    if (!subtile_revealed(slab_subtile_center(slb_x), slab_subtile_center(slb_y), plyr_idx) || ((slabst->block_flags & (SlbAtFlg_Filled|SlbAtFlg_Digable|SlbAtFlg_Valuable)) != 0))
    {
        return (temp_cluedo_mode == 0) ? 5 : 2; // return a height of 5 for a wall, or if cluedo mode (low walls mode) is enabled, return a wall height of 2.
    }
    if (slab_kind_is_liquid(slb->kind))
    {
        return 0; // Water/Lava is at height 0
    }
    return 1; // Floor is at height 1
}

/* color here is an SLC_* line-colour-category index (see engine_render.h),
 * not a resolved TbPixel -- QK_SlabSelector items are only ever consumed by
 * draw_clipped_line() -> draw_stripey_line(), which indexes
 * colored_stripey_lines[] with it. Do not route this through
 * expand_indexed_pixel()/TbPixel_Pack(). */
static void create_line_element(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t bckt_idx, unsigned char color)
{
    struct BucketKindSlabSelector *poly;
    if (!is_free_space_in_poly_pool(1))
    {
        return;
    }
    if (bckt_idx >= BUCKETS_COUNT)
        bckt_idx = BUCKETS_COUNT-1;
    else
    if (bckt_idx < 0)
        bckt_idx = 0;
    poly = (struct BucketKindSlabSelector *)getpoly;
    getpoly += sizeof(struct BucketKindSlabSelector);
    poly->b.next = buckets[bckt_idx];
    poly->b.kind = QK_SlabSelector;
    buckets[bckt_idx] = (struct BasicQ *)poly;
    if (pixel_size > 0)
    {
        poly->p.X = a1 / pixel_size;
        poly->p.Y = a2 / pixel_size;
        poly->p.U = a3 / pixel_size;
        poly->p.V = a4 / pixel_size;
    }
    poly->p.S = color;
}

/* color here is an SLC_* line-colour-category index, same as
 * create_line_element() above -- not a resolved TbPixel. */
static void create_line_segment(struct EngineCoord *start, struct EngineCoord *end, unsigned char color)
{
    struct BucketKindSlabSelector *poly;
    int64_t bckt_idx;
    if (!is_free_space_in_poly_pool(1))
        return;

    // Reducing line_z will make the lines look cleaner, but the "fancy_map_volume_box" vertical lines become more visible.
    double line_z = 0.994;
    bckt_idx = (( (start->z*line_z) + (end->z*line_z) ) / 32) - 2;
    // Original calculation:  bckt_idx = (start->z+end->z)/2 / 16 - 2;

    if (bckt_idx >= BUCKETS_COUNT)
        bckt_idx = BUCKETS_COUNT-1;
    else
    if (bckt_idx < 0)
        bckt_idx = 0;
    // Add to bucket
    poly = (struct BucketKindSlabSelector *)getpoly;
    getpoly += sizeof(struct BucketKindSlabSelector);
    poly->b.next = buckets[bckt_idx];
    poly->b.kind = QK_SlabSelector;
    buckets[bckt_idx] = (struct BasicQ *)poly;
    // Fill parameters
    if (pixel_size > 0)
    {
        poly->p.X = start->view_width;
        poly->p.Z = worldframe_depth_from_view_z(start->z);
        poly->p.Y = start->view_height;
        poly->p.U = end->view_width;
        poly->p.V = end->view_height;
    }
    poly->p.S = color;
}

/**
* Adds a line with constant Z coord to the drawlist of perspective view.
* @param color Color index.
* @param pos_z The Z coord, constant for whole line.
* @param beg_x The X coord of start of the line.
* @param end_x The X coord of end of the line.
* @param beg_y The Y coord of start of the line.
* @param end_y The Y coord of end of the line.
*/
static void create_line_const_z(unsigned char color, int64_t pos_z, int64_t beg_x, int64_t end_x, int64_t beg_y, int64_t end_y)
{
    struct EngineCoord end;
    struct EngineCoord start;
    int64_t vec_x;
    int64_t vec_y;
    int64_t pos_x;
    int64_t pos_y;
    vec_x = end_x - beg_x;
    vec_y = end_y - beg_y;
    create_box_coords(&start, beg_x, beg_y, pos_z);

    if (llabs(vec_y) > llabs(vec_x))
    {
        if (vec_y < 0)
        {
            int64_t vec_tmp;
            vec_tmp = beg_x;
            beg_x = end_x;
            end_x = vec_tmp;
            vec_tmp = beg_y;
            beg_y = end_y;
            end_y = vec_tmp;
            vec_x = -vec_x;
            vec_y = -vec_y;
        }
        for (pos_y = beg_y + COORD_PER_STL; pos_y <= end_y; pos_y += COORD_PER_STL)
        {
            pos_x = beg_x + vec_x * llabs(pos_y - beg_y) / llabs(vec_y);
            create_box_coords(&end, pos_x, pos_y, pos_z);
            create_line_segment(&start, &end, color);
            memcpy(&start, &end, sizeof(struct EngineCoord));
        }
    }
    else
    {
        if (vec_x < 0)
        {
            int64_t vec_tmp;
            vec_tmp = beg_x;
            beg_x = end_x;
            end_x = vec_tmp;
            vec_tmp = beg_y;
            beg_y = end_y;
            end_y = vec_tmp;
            vec_x = -vec_x;
            vec_y = -vec_y;
        }
        for (pos_x = beg_x + COORD_PER_STL; pos_x <= end_x; pos_x += COORD_PER_STL)
        {
            pos_y = beg_y + vec_y * llabs(pos_x - beg_x) / llabs(vec_x);
            create_box_coords(&end, pos_x, pos_y, pos_z);
            create_line_segment(&start, &end, color);
            memcpy(&start, &end, sizeof(struct EngineCoord));
        }
    }
}

/**
* Adds a line with constant XZ coords to the drawlist of perspective view.
* @param pos_x The X coord, constant for whole line.
* @param pos_z The Z coord, constant for whole line.
* @param start_y The Y coord of start of the line.
* @param end_y The Y coord of end of the line.
*/
static void create_line_const_xz(int64_t pos_x, int64_t pos_z, int64_t start_y, int64_t end_y)
{
    struct EngineCoord end;
    struct EngineCoord start;
    int64_t pos_y;
    create_box_coords(&start, pos_x, start_y, pos_z);
    for (pos_y = start_y+256; pos_y <= end_y; pos_y+=256)
    {
        create_box_coords(&end, pos_x, pos_y, pos_z);
        create_line_segment(&start, &end, map_volume_box.color);
        memcpy(&start, &end, sizeof(struct EngineCoord));
    }
}

/**
* Adds a line with constant XY coords to the drawlist of perspective view.
* @param pos_x The X coord, constant for whole line.
* @param pos_y The Y coord, constant for whole line.
* @param start_z The Z coord of start of the line.
* @param end_z The Z coord of end of the line.
*/
static void create_line_const_xy(int64_t pos_x, int64_t pos_y, int64_t start_z, int64_t end_z)
{
    struct EngineCoord end;
    struct EngineCoord start;
    int64_t pos_z;
    create_box_coords(&start, pos_x, pos_y, start_z);
    for (pos_z = start_z+256; pos_z <= end_z; pos_z+=256)
    {
        create_box_coords(&end, pos_x, pos_y, pos_z);
        create_line_segment(&start, &end, map_volume_box.color);
        memcpy(&start, &end, sizeof(struct EngineCoord));
    }
}

/**
* Adds a line with constant YZ coords to the drawlist of perspective view.
* @param pos_y The Y coord, constant for whole line.
* @param pos_z The Z coord, constant for whole line.
* @param start_x The X coord of start of the line.
* @param end_x The X coord of end of the line.
*/
static void create_line_const_yz(int64_t pos_y, int64_t pos_z, int64_t start_x, int64_t end_x)
{
    struct EngineCoord end;
    struct EngineCoord start;
    int64_t pos_x;
    create_box_coords(&start, start_x, pos_y, pos_z);
    for (pos_x = start_x+256; pos_x <= end_x; pos_x+=256)
    {
        create_box_coords(&end, pos_x, pos_y, pos_z);
        create_line_segment(&start, &end, map_volume_box.color);
        memcpy(&start, &end, sizeof(struct EngineCoord));
    }
}

void create_map_volume_box(int64_t x, int64_t y, int64_t z, int64_t line_color)
{
    int64_t box_xs;
    int64_t box_xe;
    int64_t box_ys;
    int64_t box_ye;
    int64_t box_zs;
    int64_t box_ze;
    int64_t i;
    int64_t box_color = map_volume_box.color;
    map_volume_box.color = line_color;

    box_xs = map_volume_box.beg_x - x;
    box_ys = y - map_volume_box.beg_y;
    box_ye = y - map_volume_box.end_y;
    box_xe = map_volume_box.end_x - x;

    if ( temp_cluedo_mode )
    {
        box_ze = 2*COORD_PER_STL - z;
    }
    else
    {
        box_ze = 5*COORD_PER_STL - z;
    }

    box_zs = (map_volume_box.floor_height_z << 8) - z;
    if ( box_zs >= box_ze )
    {
      box_zs = box_ze;
    }

    if ( box_xe < box_xs )
    {
        i = map_volume_box.beg_x;
        box_xs = map_volume_box.end_x - x;
        box_xe = map_volume_box.beg_x - x;
        map_volume_box.beg_x = map_volume_box.end_x;
        map_volume_box.end_x = i;
    }

    if ( box_ye < box_ys )
    {
        i = map_volume_box.beg_y;
        box_ys = y - map_volume_box.end_y;
        box_ye = y - map_volume_box.beg_y;
        map_volume_box.beg_y = map_volume_box.end_y;
        map_volume_box.end_y = i;
    }

    // Draw top rectangle
    create_line_const_yz(box_ye, box_zs, box_xs, box_xe);
    create_line_const_yz(box_ys, box_zs, box_xs, box_xe);
    create_line_const_xz(box_xs, box_zs, box_ys, box_ye);
    create_line_const_xz(box_xe, box_zs, box_ys, box_ye);
    // Vertical lines which connect the rectangles
    create_line_const_xy(box_xs, box_ys, box_zs, box_ze);
    create_line_const_xy(box_xe, box_ys, box_zs, box_ze);
    create_line_const_xy(box_xe, box_ye, box_zs, box_ze);
    create_line_const_xy(box_xs, box_ye, box_zs, box_ze);
    // Bottom rectangle
    create_line_const_yz(box_ye, box_ze, box_xs, box_xe);
    create_line_const_yz(box_ys, box_ze, box_xs, box_xe);
    create_line_const_xz(box_xs, box_ze, box_ys, box_ye);
    create_line_const_xz(box_xe, box_ze, box_ys, box_ye);

    map_volume_box.color = box_color;
}

void create_fancy_map_volume_box(struct RoomSpace roomspace, int64_t x, int64_t y, int64_t z, int64_t color, TbBool show_outer_box)
{
    int64_t line_color = color;
    if (show_outer_box)
    {
        line_color = map_volume_box.color; //  set the "inner" box color to the default colour (usually red/green)
    }
    int64_t box_xs;
    int64_t box_xe;
    int64_t box_ys;
    int64_t box_ye;
    int64_t box_zs;
    int64_t box_ze;
    int64_t i;
    int64_t box_color = map_volume_box.color;
    map_volume_box.color = line_color;
    struct MapVolumeBox valid_slabs = map_volume_box;
    // get the 'accurate' roomspace shape instead of the outer box
    valid_slabs.beg_x = subtile_coord((roomspace.left * 3), 0);
    valid_slabs.beg_y = subtile_coord((roomspace.top * 3), 0);
    valid_slabs.end_x = subtile_coord((3*1) + (roomspace.right * 3), 0);
    valid_slabs.end_y = subtile_coord(((3*1) + roomspace.bottom * 3), 0);

    box_xs = valid_slabs.beg_x - x;
    box_ys = y - valid_slabs.beg_y;
    box_ye = y - valid_slabs.end_y;
    box_xe = valid_slabs.end_x - x;

    if ( temp_cluedo_mode )
    {
        box_ze = 2*COORD_PER_STL - z;
    }
    else
    {
        box_ze = 5*COORD_PER_STL - z;
    }

    box_zs = 256 - z;
    if ( box_zs >= box_ze )
    {
      box_zs = box_ze;
    }

    if ( box_xe < box_xs )
    {
        i = valid_slabs.beg_x;
        box_xs = valid_slabs.end_x - x;
        box_xe = valid_slabs.beg_x - x;
        valid_slabs.beg_x = valid_slabs.end_x;
        valid_slabs.end_x = i;
    }

    if ( box_ye > box_ys )
    {
        i = valid_slabs.beg_y;
        box_ys = y - valid_slabs.end_y;
        box_ye = y - valid_slabs.beg_y;
        valid_slabs.beg_y = valid_slabs.end_y;
        valid_slabs.end_y = i;
    }
    for (int64_t roomspace_y = 0; roomspace_y < roomspace.height; roomspace_y++)
    {
        for (int64_t roomspace_x = 0; roomspace_x < roomspace.width; roomspace_x++)
        {
            TbBool is_in_roomspace = roomspace.slab_grid[roomspace_x][roomspace_y];
            int64_t slab_xstart = box_xs + (roomspace_x * 3 * COORD_PER_STL);
            int64_t slab_ystart = box_ys - (roomspace_y * 3 * COORD_PER_STL);
            int64_t slab_xend = box_xs + ((roomspace_x + 1) * 3 * COORD_PER_STL);
            int64_t slab_yend = box_ys - ((roomspace_y + 1) * 3 * COORD_PER_STL);
            if (is_in_roomspace)
            {
                TbBool air_left = (roomspace_x == 0) ? true : (roomspace.slab_grid[roomspace_x-1][roomspace_y] == false);
                TbBool air_right = (roomspace_x == roomspace.width) ? true : (roomspace.slab_grid[roomspace_x+1][roomspace_y] == false);
                TbBool air_above = (roomspace_y == 0) ? true : (roomspace.slab_grid[roomspace_x][roomspace_y-1] == false);
                TbBool air_below = (roomspace_y == roomspace.height) ? true : (roomspace.slab_grid[roomspace_x][roomspace_y+1] == false);
                if (air_left)
                {
                    // Draw top rectangle
                    create_line_const_xz(slab_xstart, box_zs, slab_yend, slab_ystart);
                    // Bottom rectangle
                    create_line_const_xz(slab_xstart, box_ze, slab_yend, slab_ystart);
                    // Vertical lines which connect the rectangles
                    if (air_above)
                    {
                        create_line_const_xy(slab_xstart, slab_ystart, box_zs, box_ze);
                    }
                    if (air_below)
                    {
                        create_line_const_xy(slab_xstart, slab_yend, box_zs, box_ze);
                    }
                }
                if (air_right)
                {
                    // Draw top rectangle
                    create_line_const_xz(slab_xend, box_zs, slab_yend, slab_ystart);
                    // Bottom rectangle
                    create_line_const_xz(slab_xend, box_ze, slab_yend, slab_ystart);
                    // Vertical lines which connect the rectangles
                    if (air_above)
                    {
                        create_line_const_xy(slab_xend, slab_ystart, box_zs, box_ze);
                    }
                    if (air_below)
                    {
                        create_line_const_xy(slab_xend, slab_yend, box_zs, box_ze);
                    }
                }
                if (air_above)
                {
                    // Draw top rectangle
                    create_line_const_yz(slab_ystart, box_zs, slab_xstart, slab_xend);
                    // Bottom rectangle
                    create_line_const_yz(slab_ystart, box_ze, slab_xstart, slab_xend);
                }
                if (air_below)
                {
                    // Draw top rectangle
                    create_line_const_yz(slab_yend, box_zs, slab_xstart, slab_xend);
                    // Bottom rectangle
                    create_line_const_yz(slab_yend, box_ze, slab_xstart, slab_xend);
                }
            }
            else if (!is_in_roomspace) //this handles "inside corners"
            {
                TbBool room_left = (roomspace_x == 0) ? false : roomspace.slab_grid[roomspace_x-1][roomspace_y];
                TbBool room_right = (roomspace_x == roomspace.width) ? false : roomspace.slab_grid[roomspace_x+1][roomspace_y];
                TbBool room_above = (roomspace_y == 0) ? false : roomspace.slab_grid[roomspace_x][roomspace_y-1];
                TbBool room_below = (roomspace_y == roomspace.height) ? false : roomspace.slab_grid[roomspace_x][roomspace_y+1];
                if (room_left)
                {
                    // Vertical lines which connect the rectangles
                    if (room_above)
                    {
                        create_line_const_xy(slab_xstart, slab_ystart, box_zs, box_ze);
                    }
                    if (room_below)
                    {
                        create_line_const_xy(slab_xstart, slab_yend, box_zs, box_ze);
                    }
                }
                if (room_right)
                {
                    // Vertical lines which connect the rectangles
                    if (room_above)
                    {
                        create_line_const_xy(slab_xend, slab_ystart, box_zs, box_ze);
                    }
                    if (room_below)
                    {
                        create_line_const_xy(slab_xend, slab_yend, box_zs, box_ze);
                    }
                }
                if (show_outer_box) // this handles the "outer line" (only when it is not in the roomspace)
                {
                    //draw 2nd line, i.e. the outer line - the one around the edge of the 5x5 cursor, not the valid slabs within the cursor
                    map_volume_box.color = color; // switch to the "secondary colour" (the one passed as a variable if show_outer_box is true)
                    TbBool left_edge   = (roomspace_x == 0)                    ? true : false;
                    TbBool right_edge  = (roomspace_x == roomspace.width - 1)  ? true : false;
                    TbBool top_edge    = (roomspace_y == 0)                    ? true : false;
                    TbBool bottom_edge = (roomspace_y == roomspace.height - 1) ? true : false;
                    if (left_edge)
                    {
                        create_line_const_xz(slab_xstart, box_zs, slab_yend, slab_ystart);
                        create_line_const_xz(slab_xstart, box_ze, slab_yend, slab_ystart);
                        if (top_edge)
                        {
                            create_line_const_xy(slab_xstart, slab_ystart, box_zs, box_ze);
                        }
                        if (bottom_edge)
                        {
                            create_line_const_xy(slab_xstart, slab_yend, box_zs, box_ze);
                        }
                    }
                    if (right_edge)
                    {
                        create_line_const_xz(slab_xend, box_zs, slab_yend, slab_ystart);
                        create_line_const_xz(slab_xend, box_ze, slab_yend, slab_ystart);
                        if (top_edge)
                        {
                            create_line_const_xy(slab_xend, slab_ystart, box_zs, box_ze);
                        }
                        if (bottom_edge)
                        {
                            create_line_const_xy(slab_xend, slab_yend, box_zs, box_ze);
                        }
                    }
                    if (top_edge)
                    {
                        create_line_const_yz(slab_ystart, box_zs, slab_xstart, slab_xend);
                        create_line_const_yz(slab_ystart, box_ze, slab_xstart, slab_xend);
                    }
                    if (bottom_edge)
                    {
                        create_line_const_yz(slab_yend, box_zs, slab_xstart, slab_xend);
                        create_line_const_yz(slab_yend, box_ze, slab_xstart, slab_xend);
                    }
                    map_volume_box.color = line_color; // switch back to default color (red/green) for the inner line
                }
            }
        }
    }

    map_volume_box.color = box_color;
}

static void process_isometric_map_volume_box(int64_t x, int64_t y, int64_t z, PlayerNumber plyr_idx)
{
    unsigned char default_color = map_volume_box.color;
    unsigned char line_color = default_color;
    struct PlayerInfo* current_player = get_player(plyr_idx);
    struct RoomSpace *render_roomspace = get_local_dig_prediction_render_roomspace(&current_player->render_roomspace);
    // Check if a roomspace is currently being built
    // and if so feed this back to the user
    if ((current_player->roomspace.is_active) && ((current_player->work_state == PSt_Sell) || (current_player->work_state == PSt_BuildRoom)))
    {
        line_color = SLC_REDYELLOW; // change the cursor color to indicate to the user that nothing else can be built or sold at the moment
    }
    if (render_roomspace->render_roomspace_as_box)
    {
        if (render_roomspace->is_roomspace_a_box)
        {
            // This is a basic square box
            create_map_volume_box(x + box_lag_compensation_x, y + box_lag_compensation_y, z, line_color);
        }
        else
        {
            // This is a "2-line" square box
            // i.e. an "accurate" box with an outer square box
            map_volume_box.color = line_color;
            create_fancy_map_volume_box(*render_roomspace, x + box_lag_compensation_x, y + box_lag_compensation_y, z, (render_roomspace->slab_count == 0) ? SLC_RED : SLC_BROWN, true);
        }
    }
    else
    {
        // This is an "accurate"/"automagic" box
        create_fancy_map_volume_box(*render_roomspace, x + box_lag_compensation_x, y + box_lag_compensation_y, z, line_color, false);
    }
    map_volume_box.color = default_color;
}

/******************************************************************************/
// Near-plane clipping of the standard-perspective terrain triangles. A vertex closer than the
// near plane projects to garbage, and flicker_fix() culls whole triangles whose vertices are all
// behind/beside the camera -- so a wall whose non-planar tris poke across a first-person camera
// lost pieces. A triangle with a vertex nearer than NEAR_CLIP_Z is instead clipped against that
// plane in view space (attributes interpolated), split finely (affine texturing is inaccurate
// on big near triangles) and emitted as ordinary standard polygons. Triangles wholly nearer
// than the plane are dropped.
#define NEAR_CLIP_Z 48
#define NEAR_CLIP_SPLITS 2

enum NearClipKind { NCK_TRIG_TR, NCK_TRIG_BL, NCK_GOURAD_TR, NCK_GOURAD_BL, NCK_UNLIT_TR, NCK_UNLIT_BL };

struct NearClipVertex {
    int64_t x, y, z;   // view space
    int64_t si;        // shade_intensity
    int64_t u, v;      // texture corner, 0..0x1FFFFF (+scroll added on emit)
    int64_t sx, sy;    // projected
};

static void near_clip_project(struct NearClipVertex *nv)
{
    const long long wx = nv->x * (lens << 16) / nv->z;
    const long long wy = nv->y * (lens << 16) / nv->z;
    nv->sx = view_width_over_2 + (wx >> 16);
    nv->sy = view_height_over_2 - (wy >> 16);
}

static void near_clip_lerp(struct NearClipVertex *out, const struct NearClipVertex *a, const struct NearClipVertex *b, int64_t num, int64_t den)
{
    out->x = a->x + (b->x - a->x) * num / den;
    out->y = a->y + (b->y - a->y) * num / den;
    out->z = a->z + (b->z - a->z) * num / den;
    out->si = a->si + (b->si - a->si) * num / den;
    out->u = a->u + (b->u - a->u) * num / den;
    out->v = a->v + (b->v - a->v) * num / den;
    if (out->z < NEAR_CLIP_Z)
        out->z = NEAR_CLIP_Z;
    near_clip_project(out);
}

static void near_clip_emit(enum NearClipKind kind, const struct NearClipVertex *a, const struct NearClipVertex *b, const struct NearClipVertex *c, int64_t textr_id, int64_t a5)
{
    if ((a->sx < 0 && b->sx < 0 && c->sx < 0) || (a->sx >= vec_window_width && b->sx >= vec_window_width && c->sx >= vec_window_width)
        || (a->sy < 0 && b->sy < 0 && c->sy < 0) || (a->sy >= vec_window_height && b->sy >= vec_window_height && c->sy >= vec_window_height))
        return;
    const int64_t area = (a->sy - b->sy) * (c->sx - b->sx) + (c->sy - b->sy) * (b->sx - a->sx);
    if (area == 0)
        return;
    if (area < 0)
    {
        // Back-facing pieces of a triangle that had to be clipped: the camera is right at the surface,
        // and culling would leave a hole, so draw it from behind (two vertices swapped so the winding is
        // the one the rasterizers expect; each keeps its own UV and shade).
        const struct NearClipVertex *t = b;
        b = c;
        c = t;
    }
    if (getpoly >= poly_pool_end)
        return;
    int64_t z = a->z;
    if (b->z > z) z = b->z;
    if (c->z > z) z = c->z;
    struct BucketKindPolygonStandard *poly = (struct BucketKindPolygonStandard *)getpoly;
    getpoly += sizeof(struct BucketKindPolygonStandard);
    const int64_t bucket_index = z / 16;
    poly->b.next = buckets[bucket_index];
    poly->b.kind = 0;
    buckets[bucket_index] = &poly->b;
    poly->block = textr_id;
    const struct NearClipVertex *vs[3] = { a, b, c };
    struct PolyPoint *pts[3] = { &poly->vertex_first, &poly->vertex_second, &poly->vertex_third };
    const TbBool unlit = (kind == NCK_UNLIT_TR) || (kind == NCK_UNLIT_BL);
    for (int i = 0; i < 3; i++)
    {
        int64_t si = vs[i]->si;
        int64_t shade;
        if (unlit)
            shade = (si + 3072) << 8;
        else if (kind == NCK_TRIG_TR || kind == NCK_TRIG_BL)
            shade = ((a5 >= 0) ? ((si * (3 * a5 + 81920)) >> 17) : si) << 8;
        else
            shade = ((a5 >= 0) ? ((4 * si * (a5 + 0x4000)) >> 17) : si) << 8;
        pts[i]->X = vs[i]->sx;
        pts[i]->Y = vs[i]->sy;
        pts[i]->Z = worldframe_depth_from_view_z(vs[i]->z);
        pts[i]->U = vs[i]->u + (unlit ? 0 : texture_scroll.x.val);
        pts[i]->V = vs[i]->v + (unlit ? 0 : texture_scroll.y.val);
        pts[i]->S = shade;
    }
}

static void near_clip_split(enum NearClipKind kind, const struct NearClipVertex *a, const struct NearClipVertex *b, const struct NearClipVertex *c, int64_t textr_id, int64_t a5, int depth)
{
    if (depth <= 0)
    {
        near_clip_emit(kind, a, b, c, textr_id, a5);
        return;
    }
    struct NearClipVertex ab, bc, ca;
    near_clip_lerp(&ab, a, b, 1, 2);
    near_clip_lerp(&bc, b, c, 1, 2);
    near_clip_lerp(&ca, c, a, 1, 2);
    near_clip_split(kind, a, &ab, &ca, textr_id, a5, depth - 1);
    near_clip_split(kind, &ab, b, &bc, textr_id, a5, depth - 1);
    near_clip_split(kind, &ca, &bc, c, textr_id, a5, depth - 1);
    near_clip_split(kind, &ab, &bc, &ca, textr_id, a5, depth - 1);
}

/** Returns true when the triangle was handled here (clipped, or dropped for lying wholly before the plane). */
static TbBool near_clip_triangle(enum NearClipKind kind, const struct EngineCoord *e1, const struct EngineCoord *e2, const struct EngineCoord *e3, int64_t textr_id, int64_t a5)
{
    if (rotpers != rotpers_standard)
        return false;
    if (e1->z >= NEAR_CLIP_Z && e2->z >= NEAR_CLIP_Z && e3->z >= NEAR_CLIP_Z)
        return false;
    if (e1->z < NEAR_CLIP_Z && e2->z < NEAR_CLIP_Z && e3->z < NEAR_CLIP_Z)
        return true;
    static const int64_t uv_tr[3][2] = { {0, 0}, {0x1FFFFF, 0}, {0x1FFFFF, 0x1FFFFF} };
    static const int64_t uv_bl[3][2] = { {0x1FFFFF, 0x1FFFFF}, {0, 0x1FFFFF}, {0, 0} };
    const TbBool bl = (kind == NCK_TRIG_BL) || (kind == NCK_GOURAD_BL) || (kind == NCK_UNLIT_BL);
    const struct EngineCoord *ec[3] = { e1, e2, e3 };
    struct NearClipVertex in[3];
    for (int i = 0; i < 3; i++)
    {
        in[i].x = ec[i]->x; in[i].y = ec[i]->y; in[i].z = ec[i]->z;
        in[i].si = ec[i]->shade_intensity;
        in[i].u = bl ? uv_bl[i][0] : uv_tr[i][0];
        in[i].v = bl ? uv_bl[i][1] : uv_tr[i][1];
    }
    // Sutherland-Hodgman against z = NEAR_CLIP_Z: at most 4 vertices out.
    struct NearClipVertex poly[4];
    int n = 0;
    for (int i = 0; i < 3; i++)
    {
        const struct NearClipVertex *cur = &in[i];
        const struct NearClipVertex *nxt = &in[(i + 1) % 3];
        const TbBool cur_in = cur->z >= NEAR_CLIP_Z;
        const TbBool nxt_in = nxt->z >= NEAR_CLIP_Z;
        if (cur_in)
        {
            poly[n] = *cur;
            near_clip_project(&poly[n]);
            n++;
        }
        if (cur_in != nxt_in)
        {
            near_clip_lerp(&poly[n], cur, nxt, NEAR_CLIP_Z - cur->z, nxt->z - cur->z);
            n++;
        }
    }
    for (int i = 1; i + 1 < n; i++)
        near_clip_split(kind, &poly[0], &poly[i], &poly[i + 1], textr_id, a5, NEAR_CLIP_SPLITS);
    return true;
}
/******************************************************************************/

static void do_a_trig_gourad_tr(struct EngineCoord *engine_coordinate_1, struct EngineCoord *engine_coordinate_2, struct EngineCoord *engine_coordinate_3, int64_t textr_idx, int64_t argument5)
{
    if (near_clip_triangle(NCK_TRIG_TR, engine_coordinate_1, engine_coordinate_2, engine_coordinate_3, textr_idx, argument5))
        return;
    struct BucketKindPolygonNearFP *triangle_bucket_near_1;
    struct BucketKindPolygonNearFP *triangle_bucket_near_2;
    struct BucketKindPolygonNearFP *triangle_bucket_near_3;
    struct BucketKindPolygonNearFP *triangle_bucket_near_4;
    struct BucketKindPolygonStandard *triangle_bucket_far;
    struct PolyPoint *polypoint1;
    struct PolyPoint *polypoint2;
    struct PolyPoint *polypoint3;
    struct XYZ *xyz1;
    struct XYZ *xyz2;
    struct XYZ *xyz3;
    struct XYZ *xyz4;
    struct XYZ *xyz5;
    struct XYZ *xyz6;
    int64_t coordinate_1_frustum = engine_coordinate_1->clip_flags;
    int64_t coordinate_2_frustum = engine_coordinate_2->clip_flags;
    int64_t coordinate_3_frustum = engine_coordinate_3->clip_flags;

    if (((int64_t)coordinate_1_frustum & (int64_t)(coordinate_2_frustum & coordinate_3_frustum) & 0x1F8) == 0 && (engine_coordinate_1->view_height - engine_coordinate_2->view_height) * (engine_coordinate_3->view_width - engine_coordinate_2->view_width) + (engine_coordinate_3->view_height - engine_coordinate_2->view_height) * (engine_coordinate_2->view_width - engine_coordinate_1->view_width) > 0)
    {
        int64_t choose_largest_z = engine_coordinate_1->z;
        if (engine_coordinate_2->z > choose_largest_z)
            choose_largest_z = engine_coordinate_2->z;
        if (engine_coordinate_3->z > choose_largest_z)
            choose_largest_z = engine_coordinate_3->z;
        int64_t divided_z = choose_largest_z / 16;
        if (getpoly < poly_pool_end)
        {
            if ((((uint8_t)coordinate_3_frustum | (uint8_t)(coordinate_2_frustum | coordinate_1_frustum)) & 3) != 0)
            {
                triangle_bucket_near_1 = (struct BucketKindPolygonNearFP *)getpoly;
                getpoly += sizeof(struct BucketKindPolygonNearFP);
                triangle_bucket_near_1->subtype = splittypes[16 * (engine_coordinate_3->clip_flags & 3) + 4 * (engine_coordinate_1->clip_flags & 3) + (engine_coordinate_2->clip_flags & 3)];
                triangle_bucket_near_1->b.next = buckets[divided_z];
                triangle_bucket_near_1->b.kind = QK_PolygonNearFP;
                buckets[divided_z] = &triangle_bucket_near_1->b;
                triangle_bucket_near_1->block = textr_idx;
                triangle_bucket_near_1->vertex_first.X = engine_coordinate_1->view_width;
                triangle_bucket_near_1->vertex_first.Z = worldframe_depth_from_view_z(engine_coordinate_1->z);
                triangle_bucket_near_1->vertex_first.Y = engine_coordinate_1->view_height;
                triangle_bucket_near_1->vertex_first.U = texture_scroll.x.val;
                triangle_bucket_near_1->vertex_first.V = texture_scroll.y.val;

                int64_t coordinate_1_lightness = engine_coordinate_1->shade_intensity;
                int64_t coordinate_1_distance = engine_coordinate_1->render_distance;

                if (argument5 >= 0)
                    coordinate_1_lightness = (coordinate_1_lightness * (3 * argument5 + 81920)) >> 17;

                int64_t apply_lighting_to_triangle_nearby_1;
                if (fade_min >= coordinate_1_distance)
                {
                    apply_lighting_to_triangle_nearby_1 = coordinate_1_lightness << 8;
                }
                else if (fade_max > coordinate_1_distance)
                {
                    apply_lighting_to_triangle_nearby_1 = coordinate_1_lightness * (fade_scaler - coordinate_1_distance) / fade_range + 0x8000;
                }
                else
                {
                    apply_lighting_to_triangle_nearby_1 = 0x8000;
                }

                triangle_bucket_near_1->vertex_first.S = apply_lighting_to_triangle_nearby_1;
                triangle_bucket_near_1->vertex_second.X = engine_coordinate_2->view_width;
                triangle_bucket_near_1->vertex_second.Z = worldframe_depth_from_view_z(engine_coordinate_2->z);
                triangle_bucket_near_1->vertex_second.Y = engine_coordinate_2->view_height;
                triangle_bucket_near_1->vertex_second.U = 0x1FFFFF + texture_scroll.x.val;
                triangle_bucket_near_1->vertex_second.V = texture_scroll.y.val;

                int64_t coordinate_2_lightness = engine_coordinate_2->shade_intensity;
                int64_t coordinate_2_distance = engine_coordinate_2->render_distance;

                if (argument5 >= 0)
                    coordinate_2_lightness = (coordinate_2_lightness * (3 * argument5 + 81920)) >> 17;

                int64_t apply_lighting_to_triangle_nearby_2;
                if (coordinate_2_distance <= fade_min)
                {
                    apply_lighting_to_triangle_nearby_2 = coordinate_2_lightness << 8;
                }
                else if (coordinate_2_distance < fade_max)
                {
                    apply_lighting_to_triangle_nearby_2 = coordinate_2_lightness * (fade_scaler - coordinate_2_distance) / fade_range + 0x8000;
                }
                else
                {
                    apply_lighting_to_triangle_nearby_2 = 0x8000;
                }

                triangle_bucket_near_1->vertex_second.S = apply_lighting_to_triangle_nearby_2;
                triangle_bucket_near_1->vertex_third.X = engine_coordinate_3->view_width;
                triangle_bucket_near_1->vertex_third.Z = worldframe_depth_from_view_z(engine_coordinate_3->z);
                triangle_bucket_near_1->vertex_third.Y = engine_coordinate_3->view_height;
                triangle_bucket_near_1->vertex_third.U = 0x1FFFFF + texture_scroll.x.val;
                triangle_bucket_near_1->vertex_third.V = 0x1FFFFF + texture_scroll.y.val;

                int64_t coordinate_3_lightness = engine_coordinate_3->shade_intensity;
                int64_t coordinate_3_distance = engine_coordinate_3->render_distance;

                if (argument5 >= 0)
                    coordinate_3_lightness = (coordinate_3_lightness * (3 * argument5 + 81920)) >> 17;

                int64_t apply_lighting_to_triangle_nearby_3;
                if (fade_min >= coordinate_3_distance)
                {
                    apply_lighting_to_triangle_nearby_3 = coordinate_3_lightness << 8;
                }
                else if (fade_max > coordinate_3_distance)
                {
                    apply_lighting_to_triangle_nearby_3 = coordinate_3_lightness * (fade_scaler - coordinate_3_distance) / fade_range + 0x8000;
                }
                else
                {
                    apply_lighting_to_triangle_nearby_3 = 0x8000;
                }

                triangle_bucket_near_1->vertex_third.S = apply_lighting_to_triangle_nearby_3;

                int64_t coordinate_1_z = engine_coordinate_1->z;
                if (coordinate_1_z >= 32)
                {
                    int64_t coordinate_2_z = engine_coordinate_2->z;
                    int64_t coordinate_3_z = engine_coordinate_3->z;
                    if (coordinate_2_z >= 32)
                    {
                        if (coordinate_3_z >= 32)
                        {
                            triangle_bucket_near_1->coordinate_first.x = engine_coordinate_1->x;
                            triangle_bucket_near_1->coordinate_first.y = engine_coordinate_1->y;
                            triangle_bucket_near_1->coordinate_first.z = engine_coordinate_1->z;
                            xyz5 = &triangle_bucket_near_1->coordinate_second;
                            triangle_bucket_near_1->coordinate_second.x = engine_coordinate_2->x;
                            xyz6 = &triangle_bucket_near_1->coordinate_third;
                            xyz5->y = engine_coordinate_2->y;
                            xyz5->z = engine_coordinate_2->z;
                            xyz6->x = engine_coordinate_3->x;
                            xyz6->y = engine_coordinate_3->y;
                            xyz6->z = engine_coordinate_3->z;
                        }
                        else
                        {
                            triangle_bucket_near_4 = (struct BucketKindPolygonNearFP *)getpoly;
                            getpoly += sizeof(struct BucketKindPolygonNearFP);
                            triangle_bucket_near_4->subtype = splittypes[16 * (engine_coordinate_3->clip_flags & 3) + 4 * (engine_coordinate_1->clip_flags & 3) + (engine_coordinate_2->clip_flags & 3)];
                            triangle_bucket_near_4->b.next = buckets[divided_z];
                            triangle_bucket_near_4->b.kind = QK_PolygonNearFP;
                            buckets[divided_z] = &triangle_bucket_near_4->b;
                            triangle_bucket_near_4->block = textr_idx;
                            triangle_bucket_near_1->coordinate_first.x = engine_coordinate_1->x;
                            triangle_bucket_near_1->coordinate_first.y = engine_coordinate_1->y;
                            triangle_bucket_near_1->coordinate_first.z = engine_coordinate_1->z;
                            triangle_bucket_near_1->coordinate_second.x = engine_coordinate_2->x;
                            triangle_bucket_near_1->coordinate_second.y = engine_coordinate_2->y;
                            triangle_bucket_near_1->coordinate_second.z = engine_coordinate_2->z;
                            memcpy(&triangle_bucket_near_4->vertex_third, &triangle_bucket_near_1->vertex_third, sizeof(triangle_bucket_near_4->vertex_third));
                            memcpy(&triangle_bucket_near_4->vertex_second, &triangle_bucket_near_1->vertex_second, sizeof(triangle_bucket_near_4->vertex_second));

                            int64_t z_ratio_1 = ((32 - engine_coordinate_3->z) << 8) / (engine_coordinate_1->z - engine_coordinate_3->z);

                            triangle_bucket_near_1->coordinate_third.x = engine_coordinate_3->x + ((z_ratio_1 * (engine_coordinate_1->x - engine_coordinate_3->x)) >> 8);
                            triangle_bucket_near_1->coordinate_third.y = engine_coordinate_3->y + ((z_ratio_1 * (engine_coordinate_1->y - engine_coordinate_3->y)) >> 8);
                            triangle_bucket_near_1->coordinate_third.z = 32;
                            perspective(&triangle_bucket_near_1->coordinate_third, &triangle_bucket_near_1->vertex_third);
                            triangle_bucket_near_1->vertex_third.U += (z_ratio_1 * (triangle_bucket_near_1->vertex_first.U - triangle_bucket_near_1->vertex_third.U)) >> 8;
                            triangle_bucket_near_1->vertex_third.V += (z_ratio_1 * (triangle_bucket_near_1->vertex_first.V - triangle_bucket_near_1->vertex_third.V)) >> 8;

                            int64_t light_factor_1 = triangle_bucket_near_1->vertex_third.S;
                            int64_t light_delta_1 = (z_ratio_1 * (triangle_bucket_near_1->vertex_first.S - light_factor_1)) >> 8;

                            polypoint3 = &triangle_bucket_near_1->vertex_third;
                            xyz4 = &triangle_bucket_near_1->coordinate_third;
                            xyz4[-3].z = light_factor_1 + light_delta_1;
                            memcpy(&triangle_bucket_near_4->vertex_first, polypoint3, sizeof(triangle_bucket_near_4->vertex_first));
                            triangle_bucket_near_4->coordinate_first.x = xyz4->x;
                            triangle_bucket_near_4->coordinate_first.y = xyz4->y;
                            triangle_bucket_near_4->coordinate_first.z = xyz4->z;
                            triangle_bucket_near_4->coordinate_second.x = engine_coordinate_2->x;
                            triangle_bucket_near_4->coordinate_second.y = engine_coordinate_2->y;
                            triangle_bucket_near_4->coordinate_second.z = engine_coordinate_2->z;

                            int64_t z_ratio_2 = ((32 - engine_coordinate_3->z) << 8) / (engine_coordinate_2->z - engine_coordinate_3->z);

                            triangle_bucket_near_4->coordinate_third.x = engine_coordinate_3->x + ((z_ratio_2 * (engine_coordinate_2->x - engine_coordinate_3->x)) >> 8);
                            triangle_bucket_near_4->coordinate_third.y = engine_coordinate_3->y + ((z_ratio_2 * (engine_coordinate_2->y - engine_coordinate_3->y)) >> 8);
                            triangle_bucket_near_4->coordinate_third.z = 32;
                            perspective(&triangle_bucket_near_4->coordinate_third, &triangle_bucket_near_4->vertex_third);
                            triangle_bucket_near_4->vertex_third.U += (z_ratio_2 * (triangle_bucket_near_4->vertex_second.U - triangle_bucket_near_4->vertex_third.U)) >> 8;
                            triangle_bucket_near_4->vertex_third.V += (z_ratio_2 * (triangle_bucket_near_4->vertex_second.V - triangle_bucket_near_4->vertex_third.V)) >> 8;
                            triangle_bucket_near_4->vertex_third.S += (z_ratio_2 * (triangle_bucket_near_4->vertex_second.S - triangle_bucket_near_4->vertex_third.S)) >> 8;
                        }
                    }
                    else if (coordinate_3_z >= 32)
                    {
                        triangle_bucket_near_3 = (struct BucketKindPolygonNearFP *)getpoly;
                        getpoly += sizeof(struct BucketKindPolygonNearFP);
                        triangle_bucket_near_3->subtype = splittypes[16 * (engine_coordinate_3->clip_flags & 3) + 4 * (engine_coordinate_1->clip_flags & 3) + (engine_coordinate_2->clip_flags & 3)];
                        triangle_bucket_near_3->b.next = buckets[divided_z];
                        triangle_bucket_near_3->b.kind = QK_PolygonNearFP;
                        buckets[divided_z] = &triangle_bucket_near_3->b;
                        triangle_bucket_near_3->block = textr_idx;
                        triangle_bucket_near_1->coordinate_first.x = engine_coordinate_1->x;
                        triangle_bucket_near_1->coordinate_first.y = engine_coordinate_1->y;
                        triangle_bucket_near_1->coordinate_first.z = engine_coordinate_1->z;
                        triangle_bucket_near_1->coordinate_third.x = engine_coordinate_3->x;
                        triangle_bucket_near_1->coordinate_third.y = engine_coordinate_3->y;
                        triangle_bucket_near_1->coordinate_third.z = engine_coordinate_3->z;
                        memcpy(&triangle_bucket_near_3->vertex_second, &triangle_bucket_near_1->vertex_second, sizeof(triangle_bucket_near_3->vertex_second));
                        memcpy(&triangle_bucket_near_3->vertex_third, &triangle_bucket_near_1->vertex_third, sizeof(triangle_bucket_near_3->vertex_third));

                        int64_t z_split_1 = ((32 - engine_coordinate_2->z) << 8) / (engine_coordinate_1->z - engine_coordinate_2->z);

                        triangle_bucket_near_1->coordinate_second.x = engine_coordinate_2->x + ((z_split_1 * (engine_coordinate_1->x - engine_coordinate_2->x)) >> 8);
                        triangle_bucket_near_1->coordinate_second.y = engine_coordinate_2->y + ((z_split_1 * (engine_coordinate_1->y - engine_coordinate_2->y)) >> 8);
                        triangle_bucket_near_1->coordinate_second.z = 32;
                        perspective(&triangle_bucket_near_1->coordinate_second, &triangle_bucket_near_1->vertex_second);
                        triangle_bucket_near_1->vertex_second.U += (z_split_1 * (triangle_bucket_near_1->vertex_first.U - triangle_bucket_near_1->vertex_second.U)) >> 8;
                        triangle_bucket_near_1->vertex_second.V += (z_split_1 * (triangle_bucket_near_1->vertex_first.V - triangle_bucket_near_1->vertex_second.V)) >> 8;

                        int64_t light_base_1 = triangle_bucket_near_1->vertex_second.S;
                        int64_t light_delta_2 = (z_split_1 * (triangle_bucket_near_1->vertex_first.S - light_base_1)) >> 8;

                        polypoint2 = &triangle_bucket_near_1->vertex_second;
                        xyz3 = &triangle_bucket_near_1->coordinate_second;
                        xyz3[-3].x = light_base_1 + light_delta_2;
                        memcpy(&triangle_bucket_near_3->vertex_first, polypoint2, sizeof(triangle_bucket_near_3->vertex_first));
                        triangle_bucket_near_3->coordinate_first.x = xyz3->x;
                        triangle_bucket_near_3->coordinate_first.y = xyz3->y;
                        triangle_bucket_near_3->coordinate_first.z = xyz3->z;
                        triangle_bucket_near_3->coordinate_third.x = engine_coordinate_3->x;
                        triangle_bucket_near_3->coordinate_third.y = engine_coordinate_3->y;
                        triangle_bucket_near_3->coordinate_third.z = engine_coordinate_3->z;

                        int64_t z_ratio_3 = ((32 - engine_coordinate_2->z) << 8) / (engine_coordinate_3->z - engine_coordinate_2->z);

                        triangle_bucket_near_3->coordinate_second.x = engine_coordinate_2->x + ((z_ratio_3 * (engine_coordinate_3->x - engine_coordinate_2->x)) >> 8);
                        triangle_bucket_near_3->coordinate_second.y = engine_coordinate_2->y + ((z_ratio_3 * (engine_coordinate_3->y - engine_coordinate_2->y)) >> 8);
                        triangle_bucket_near_3->coordinate_second.z = 32;
                        perspective(&triangle_bucket_near_3->coordinate_second, &triangle_bucket_near_3->vertex_second);
                        triangle_bucket_near_3->vertex_second.U += (z_ratio_3 * (triangle_bucket_near_3->vertex_third.U - triangle_bucket_near_3->vertex_second.U)) >> 8;
                        triangle_bucket_near_3->vertex_second.V += (z_ratio_3 * (triangle_bucket_near_3->vertex_third.V - triangle_bucket_near_3->vertex_second.V)) >> 8;
                        triangle_bucket_near_3->vertex_second.S += (z_ratio_3 * (triangle_bucket_near_3->vertex_third.S - triangle_bucket_near_3->vertex_second.S)) >> 8;
                    }
                    else
                    {
                        int64_t z_split_2 = ((32 - coordinate_2_z) << 8) / (coordinate_1_z - coordinate_2_z);

                        triangle_bucket_near_1->coordinate_second.x = engine_coordinate_2->x + ((z_split_2 * (engine_coordinate_1->x - engine_coordinate_2->x)) >> 8);
                        triangle_bucket_near_1->coordinate_second.y = engine_coordinate_2->y + ((z_split_2 * (engine_coordinate_1->y - engine_coordinate_2->y)) >> 8);
                        triangle_bucket_near_1->coordinate_second.z = 32;
                        perspective(&triangle_bucket_near_1->coordinate_second, &triangle_bucket_near_1->vertex_second);
                        triangle_bucket_near_1->vertex_second.U += (z_split_2 * (triangle_bucket_near_1->vertex_first.U - triangle_bucket_near_1->vertex_second.U)) >> 8;
                        triangle_bucket_near_1->vertex_second.V += (z_split_2 * (triangle_bucket_near_1->vertex_first.V - triangle_bucket_near_1->vertex_second.V)) >> 8;
                        triangle_bucket_near_1->vertex_second.S += (z_split_2 * (triangle_bucket_near_1->vertex_first.S - triangle_bucket_near_1->vertex_second.S)) >> 8;

                        int64_t z_ratio_4 = ((32 - engine_coordinate_3->z) << 8) / (engine_coordinate_1->z - engine_coordinate_3->z);

                        triangle_bucket_near_1->coordinate_third.x = engine_coordinate_3->x + ((z_ratio_4 * (engine_coordinate_1->x - engine_coordinate_3->x)) >> 8);
                        triangle_bucket_near_1->coordinate_third.y = engine_coordinate_3->y + ((z_ratio_4 * (engine_coordinate_1->y - engine_coordinate_3->y)) >> 8);
                        triangle_bucket_near_1->coordinate_third.z = 32;
                        perspective(&triangle_bucket_near_1->coordinate_third, &triangle_bucket_near_1->vertex_third);
                        triangle_bucket_near_1->vertex_third.U += (z_ratio_4 * (triangle_bucket_near_1->vertex_first.U - triangle_bucket_near_1->vertex_third.U)) >> 8;
                        triangle_bucket_near_1->vertex_third.V += (z_ratio_4 * (triangle_bucket_near_1->vertex_first.V - triangle_bucket_near_1->vertex_third.V)) >> 8;

                        int64_t light_base_2 = triangle_bucket_near_1->vertex_first.S;
                        int64_t light_base_3 = triangle_bucket_near_1->vertex_third.S;

                        xyz2 = &triangle_bucket_near_1->coordinate_first;
                        xyz2[-1].z = light_base_3 + ((z_ratio_4 * (light_base_2 - light_base_3)) >> 8);
                        xyz2->x = engine_coordinate_1->x;
                        xyz2->y = engine_coordinate_1->y;
                        xyz2->z = engine_coordinate_1->z;
                    }
                }
                else if (engine_coordinate_2->z >= 32)
                {
                    if (engine_coordinate_3->z >= 32)
                    {
                        triangle_bucket_near_2 = (struct BucketKindPolygonNearFP *)getpoly;
                        getpoly += sizeof(struct BucketKindPolygonNearFP);
                        triangle_bucket_near_2->subtype = splittypes[16 * (engine_coordinate_3->clip_flags & 3) + 4 * (engine_coordinate_1->clip_flags & 3) + (engine_coordinate_2->clip_flags & 3)];
                        triangle_bucket_near_2->b.next = buckets[divided_z];
                        triangle_bucket_near_2->b.kind = QK_PolygonNearFP;
                        buckets[divided_z] = &triangle_bucket_near_2->b;
                        triangle_bucket_near_2->block = textr_idx;
                        triangle_bucket_near_1->coordinate_second.x = engine_coordinate_2->x;
                        triangle_bucket_near_1->coordinate_second.y = engine_coordinate_2->y;
                        triangle_bucket_near_1->coordinate_second.z = engine_coordinate_2->z;
                        triangle_bucket_near_1->coordinate_third.x = engine_coordinate_3->x;
                        triangle_bucket_near_1->coordinate_third.y = engine_coordinate_3->y;
                        triangle_bucket_near_1->coordinate_third.z = engine_coordinate_3->z;
                        memcpy(&triangle_bucket_near_2->vertex_first, &triangle_bucket_near_1->vertex_first, sizeof(triangle_bucket_near_2->vertex_first));
                        memcpy(&triangle_bucket_near_2->vertex_third, &triangle_bucket_near_1->vertex_third, sizeof(triangle_bucket_near_2->vertex_third));

                        int64_t z_split_3 = ((32 - engine_coordinate_1->z) << 8) / (engine_coordinate_2->z - engine_coordinate_1->z);

                        triangle_bucket_near_1->coordinate_first.x = engine_coordinate_1->x + ((z_split_3 * (engine_coordinate_2->x - engine_coordinate_1->x)) >> 8);
                        triangle_bucket_near_1->coordinate_first.y = engine_coordinate_1->y + ((z_split_3 * (engine_coordinate_2->y - engine_coordinate_1->y)) >> 8);
                        triangle_bucket_near_1->coordinate_first.z = 32;
                        perspective(&triangle_bucket_near_1->coordinate_first, &triangle_bucket_near_1->vertex_first);
                        triangle_bucket_near_1->vertex_first.U += (z_split_3 * (triangle_bucket_near_1->vertex_second.U - triangle_bucket_near_1->vertex_first.U)) >> 8;
                        triangle_bucket_near_1->vertex_first.V += (z_split_3 * (triangle_bucket_near_1->vertex_second.V - triangle_bucket_near_1->vertex_first.V)) >> 8;

                        int64_t light_base_4 = triangle_bucket_near_1->vertex_first.S;
                        int64_t light_delta_3 = (z_split_3 * (triangle_bucket_near_1->vertex_second.S - light_base_4)) >> 8;

                        polypoint1 = &triangle_bucket_near_1->vertex_first;
                        xyz1 = &triangle_bucket_near_1->coordinate_first;
                        xyz1[-4].y = light_base_4 + light_delta_3;
                        memcpy(&triangle_bucket_near_2->vertex_second, polypoint1, sizeof(triangle_bucket_near_2->vertex_second));
                        triangle_bucket_near_2->coordinate_second.x = xyz1->x;
                        triangle_bucket_near_2->coordinate_second.y = xyz1->y;
                        triangle_bucket_near_2->coordinate_second.z = xyz1->z;
                        triangle_bucket_near_2->coordinate_third.x = engine_coordinate_3->x;
                        triangle_bucket_near_2->coordinate_third.y = engine_coordinate_3->y;
                        triangle_bucket_near_2->coordinate_third.z = engine_coordinate_3->z;

                        int64_t z_ratio_5 = ((32 - engine_coordinate_1->z) << 8) / (engine_coordinate_3->z - engine_coordinate_1->z);

                        triangle_bucket_near_2->coordinate_first.x = engine_coordinate_1->x + ((z_ratio_5 * (engine_coordinate_3->x - engine_coordinate_1->x)) >> 8);
                        triangle_bucket_near_2->coordinate_first.y = engine_coordinate_1->y + ((z_ratio_5 * (engine_coordinate_3->y - engine_coordinate_1->y)) >> 8);
                        triangle_bucket_near_2->coordinate_first.z = 32;
                        perspective(&triangle_bucket_near_2->coordinate_first, &triangle_bucket_near_2->vertex_first);
                        triangle_bucket_near_2->vertex_first.U += (z_ratio_5 * (triangle_bucket_near_2->vertex_third.U - triangle_bucket_near_2->vertex_first.U)) >> 8;
                        triangle_bucket_near_2->vertex_first.V += (z_ratio_5 * (triangle_bucket_near_2->vertex_third.V - triangle_bucket_near_2->vertex_first.V)) >> 8;
                        triangle_bucket_near_2->vertex_first.S += (z_ratio_5 * (triangle_bucket_near_2->vertex_third.S - triangle_bucket_near_2->vertex_first.S)) >> 8;
                    }
                    else
                    {
                        triangle_bucket_near_1->coordinate_second.x = engine_coordinate_2->x;
                        triangle_bucket_near_1->coordinate_second.y = engine_coordinate_2->y;
                        triangle_bucket_near_1->coordinate_second.z = engine_coordinate_2->z;

                        int64_t z_split_4 = ((32 - engine_coordinate_1->z) << 8) / (engine_coordinate_2->z - engine_coordinate_1->z);

                        triangle_bucket_near_1->coordinate_first.x = engine_coordinate_1->x + ((z_split_4 * (engine_coordinate_2->x - engine_coordinate_1->x)) >> 8);
                        triangle_bucket_near_1->coordinate_first.y = engine_coordinate_1->y + ((z_split_4 * (engine_coordinate_2->y - engine_coordinate_1->y)) >> 8);
                        triangle_bucket_near_1->coordinate_first.z = 32;
                        perspective(&triangle_bucket_near_1->coordinate_first, &triangle_bucket_near_1->vertex_first);
                        triangle_bucket_near_1->vertex_first.U += (z_split_4 * (triangle_bucket_near_1->vertex_second.U - triangle_bucket_near_1->vertex_first.U)) >> 8;
                        triangle_bucket_near_1->vertex_first.V += (z_split_4 * (triangle_bucket_near_1->vertex_second.V - triangle_bucket_near_1->vertex_first.V)) >> 8;
                        triangle_bucket_near_1->vertex_first.S += (z_split_4 * (triangle_bucket_near_1->vertex_second.S - triangle_bucket_near_1->vertex_first.S)) >> 8;

                        int64_t z_ratio_6 = ((32 - engine_coordinate_3->z) << 8) / (engine_coordinate_2->z - engine_coordinate_3->z);

                        triangle_bucket_near_1->coordinate_third.x = engine_coordinate_3->x + ((z_ratio_6 * (engine_coordinate_2->x - engine_coordinate_3->x)) >> 8);
                        triangle_bucket_near_1->coordinate_third.y = engine_coordinate_3->y + ((z_ratio_6 * (engine_coordinate_2->y - engine_coordinate_3->y)) >> 8);
                        triangle_bucket_near_1->coordinate_third.z = 32;
                        perspective(&triangle_bucket_near_1->coordinate_third, &triangle_bucket_near_1->vertex_third);
                        triangle_bucket_near_1->vertex_third.U += (z_ratio_6 * (triangle_bucket_near_1->vertex_second.U - triangle_bucket_near_1->vertex_third.U)) >> 8;
                        triangle_bucket_near_1->vertex_third.V += (z_ratio_6 * (triangle_bucket_near_1->vertex_second.V - triangle_bucket_near_1->vertex_third.V)) >> 8;
                        triangle_bucket_near_1->vertex_third.S += (z_ratio_6 * (triangle_bucket_near_1->vertex_second.S - triangle_bucket_near_1->vertex_third.S)) >> 8;
                    }
                }
                else
                {
                    triangle_bucket_near_1->coordinate_third.x = engine_coordinate_3->x;
                    triangle_bucket_near_1->coordinate_third.y = engine_coordinate_3->y;
                    triangle_bucket_near_1->coordinate_third.z = engine_coordinate_3->z;

                    int64_t z_ratio_7 = ((32 - engine_coordinate_1->z) << 8) / (engine_coordinate_3->z - engine_coordinate_1->z);

                    triangle_bucket_near_1->coordinate_first.x = engine_coordinate_1->x + ((z_ratio_7 * (engine_coordinate_3->x - engine_coordinate_1->x)) >> 8);
                    triangle_bucket_near_1->coordinate_first.y = engine_coordinate_1->y + ((z_ratio_7 * (engine_coordinate_3->y - engine_coordinate_1->y)) >> 8);
                    triangle_bucket_near_1->coordinate_first.z = 32;
                    perspective(&triangle_bucket_near_1->coordinate_first, &triangle_bucket_near_1->vertex_first);
                    triangle_bucket_near_1->vertex_first.U += (z_ratio_7 * (triangle_bucket_near_1->vertex_third.U - triangle_bucket_near_1->vertex_first.U)) >> 8;
                    triangle_bucket_near_1->vertex_first.V += (z_ratio_7 * (triangle_bucket_near_1->vertex_third.V - triangle_bucket_near_1->vertex_first.V)) >> 8;
                    triangle_bucket_near_1->vertex_first.S += (z_ratio_7 * (triangle_bucket_near_1->vertex_third.S - triangle_bucket_near_1->vertex_first.S)) >> 8;

                    int64_t z_ratio_8 = ((32 - engine_coordinate_2->z) << 8) / (engine_coordinate_3->z - engine_coordinate_2->z);

                    triangle_bucket_near_1->coordinate_second.x = engine_coordinate_2->x + ((z_ratio_8 * (engine_coordinate_3->x - engine_coordinate_2->x)) >> 8);
                    triangle_bucket_near_1->coordinate_second.y = engine_coordinate_2->y + ((z_ratio_8 * (engine_coordinate_3->y - engine_coordinate_2->y)) >> 8);
                    triangle_bucket_near_1->coordinate_second.z = 32;
                    perspective(&triangle_bucket_near_1->coordinate_second, &triangle_bucket_near_1->vertex_second);
                    triangle_bucket_near_1->vertex_second.U += (z_ratio_8 * (triangle_bucket_near_1->vertex_third.U - triangle_bucket_near_1->vertex_second.U)) >> 8;
                    triangle_bucket_near_1->vertex_second.V += (z_ratio_8 * (triangle_bucket_near_1->vertex_third.V - triangle_bucket_near_1->vertex_second.V)) >> 8;
                    triangle_bucket_near_1->vertex_second.S += (z_ratio_8 * (triangle_bucket_near_1->vertex_third.S - triangle_bucket_near_1->vertex_second.S)) >> 8;
                }
            }
            else
            {
                triangle_bucket_far = (struct BucketKindPolygonStandard *)getpoly;
                getpoly += sizeof(struct BucketKindPolygonStandard);
                triangle_bucket_far->b.next = buckets[divided_z];
                triangle_bucket_far->b.kind = QK_PolygonStandard;
                buckets[divided_z] = &triangle_bucket_far->b;

                triangle_bucket_far->block = textr_idx;
                triangle_bucket_far->vertex_first.X = engine_coordinate_1->view_width;
                triangle_bucket_far->vertex_first.Z = worldframe_depth_from_view_z(engine_coordinate_1->z);
                triangle_bucket_far->vertex_first.Y = engine_coordinate_1->view_height;
                triangle_bucket_far->vertex_first.U = texture_scroll.x.val;
                triangle_bucket_far->vertex_first.V = texture_scroll.y.val;

                int64_t coordinate_1_lightness = engine_coordinate_1->shade_intensity;
                int64_t coordinate_1_distance = engine_coordinate_1->render_distance;

                if (argument5 >= 0)
                    coordinate_1_lightness = (coordinate_1_lightness * (3 * argument5 + 81920)) >> 17;

                int64_t apply_lighting_to_triangle_far_1;
                if (coordinate_1_distance <= fade_min)
                {
                    apply_lighting_to_triangle_far_1 = coordinate_1_lightness << 8;
                }
                else if (coordinate_1_distance < fade_max)
                {
                    apply_lighting_to_triangle_far_1 = coordinate_1_lightness * (fade_scaler - coordinate_1_distance) / fade_range + 0x8000;
                }
                else
                {
                    apply_lighting_to_triangle_far_1 = 0x8000;
                }

                triangle_bucket_far->vertex_first.S = apply_lighting_to_triangle_far_1;
                triangle_bucket_far->vertex_second.X = engine_coordinate_2->view_width;
                triangle_bucket_far->vertex_second.Z = worldframe_depth_from_view_z(engine_coordinate_2->z);
                triangle_bucket_far->vertex_second.Y = engine_coordinate_2->view_height;
                triangle_bucket_far->vertex_second.U = 0x1FFFFF + texture_scroll.x.val;
                triangle_bucket_far->vertex_second.V = texture_scroll.y.val;

                int64_t coordinate_2_lightness = engine_coordinate_2->shade_intensity;
                int64_t coordinate_2_distance = engine_coordinate_2->render_distance;

                if (argument5 >= 0)
                    coordinate_2_lightness = (coordinate_2_lightness * (3 * argument5 + 81920)) >> 17;

                int64_t apply_lighting_to_triangle_far_2;
                if (coordinate_2_distance <= fade_min)
                {
                    apply_lighting_to_triangle_far_2 = coordinate_2_lightness << 8;
                }
                else if (coordinate_2_distance < fade_max)
                {
                    apply_lighting_to_triangle_far_2 = coordinate_2_lightness * (fade_scaler - coordinate_2_distance) / fade_range + 0x8000;
                }
                else
                {
                    apply_lighting_to_triangle_far_2 = 0x8000;
                }

                triangle_bucket_far->vertex_second.S = apply_lighting_to_triangle_far_2;
                triangle_bucket_far->vertex_third.X = engine_coordinate_3->view_width;
                triangle_bucket_far->vertex_third.Z = worldframe_depth_from_view_z(engine_coordinate_3->z);
                triangle_bucket_far->vertex_third.Y = engine_coordinate_3->view_height;
                triangle_bucket_far->vertex_third.U = 0x1FFFFF + texture_scroll.x.val;
                triangle_bucket_far->vertex_third.V = 0x1FFFFF + texture_scroll.y.val;

                int64_t coordinate_3_lightness = engine_coordinate_3->shade_intensity;
                int64_t coordinate_3_distance = engine_coordinate_3->render_distance;

                if (argument5 >= 0)
                    coordinate_3_lightness = (coordinate_3_lightness * (3 * argument5 + 81920)) >> 17;

                if (coordinate_3_distance <= fade_min)
                {
                    triangle_bucket_far->vertex_third.S = coordinate_3_lightness << 8;
                }
                else if (coordinate_3_distance < fade_max)
                {
                    triangle_bucket_far->vertex_third.S = coordinate_3_lightness * (fade_scaler - coordinate_3_distance) / fade_range + 0x8000;
                }
                else
                {
                    triangle_bucket_far->vertex_third.S = 0x8000;
                }
            }
        }
    }
}

static void do_a_trig_gourad_bl(struct EngineCoord *engine_coordinate_1, struct EngineCoord *engine_coordinate_2, struct EngineCoord *engine_coordinate_3, int64_t argument4, int64_t argument5)
{
    if (near_clip_triangle(NCK_TRIG_BL, engine_coordinate_1, engine_coordinate_2, engine_coordinate_3, argument4, argument5))
        return;
    struct BucketKindPolygonNearFP *triangle_bucket_near_1;
    struct BucketKindPolygonNearFP *triangle_bucket_near_2;
    struct BucketKindPolygonNearFP *triangle_bucket_near_3;
    struct BucketKindPolygonNearFP *triangle_bucket_near_4;
    struct BucketKindPolygonStandard *triangle_bucket_far;
    struct PolyPoint *polypoint1;
    struct PolyPoint *polypoint2;
    struct PolyPoint *polypoint3;
    struct XYZ *xyz1;
    struct XYZ *xyz2;
    struct XYZ *xyz3;
    struct XYZ *xyz4;
    struct XYZ *xyz5;
    struct XYZ *xyz6;
    int64_t coordinate_1_frustum = engine_coordinate_1->clip_flags;
    int64_t coordinate_2_frustum = engine_coordinate_2->clip_flags;
    int64_t coordinate_3_frustum = engine_coordinate_3->clip_flags;

    if (((int64_t)coordinate_2_frustum & (int64_t)(coordinate_3_frustum & coordinate_1_frustum) & 0x1F8) == 0 && (engine_coordinate_2->view_width - engine_coordinate_1->view_width) * (engine_coordinate_3->view_height - engine_coordinate_2->view_height) + (engine_coordinate_3->view_width - engine_coordinate_2->view_width) * (engine_coordinate_1->view_height - engine_coordinate_2->view_height) > 0)
    {
        int64_t choose_smallest_z = engine_coordinate_1->z;
        if (choose_smallest_z < engine_coordinate_2->z)
            choose_smallest_z = engine_coordinate_2->z;
        if (choose_smallest_z < engine_coordinate_3->z)
            choose_smallest_z = engine_coordinate_3->z;
        int64_t divided_z = choose_smallest_z / 16;
        if (getpoly < poly_pool_end)
        {
            if ((((uint8_t)coordinate_1_frustum | (uint8_t)(coordinate_3_frustum | coordinate_2_frustum)) & 3) != 0)
            {
                triangle_bucket_near_1 = (struct BucketKindPolygonNearFP *)getpoly;
                getpoly += sizeof(struct BucketKindPolygonNearFP);
                triangle_bucket_near_1->subtype = splittypes[16 * (engine_coordinate_3->clip_flags & 3) + 4 * (engine_coordinate_1->clip_flags & 3) + (engine_coordinate_2->clip_flags & 3)];
                triangle_bucket_near_1->b.next = buckets[divided_z];
                triangle_bucket_near_1->b.kind = QK_PolygonNearFP;
                buckets[divided_z] = &triangle_bucket_near_1->b;
                triangle_bucket_near_1->block = argument4;

                triangle_bucket_near_1->vertex_first.X = engine_coordinate_1->view_width;

                triangle_bucket_near_1->vertex_first.Z = worldframe_depth_from_view_z(engine_coordinate_1->z);
                triangle_bucket_near_1->vertex_first.Y = engine_coordinate_1->view_height;
                triangle_bucket_near_1->vertex_first.U = 0x1FFFFF + texture_scroll.x.val;
                triangle_bucket_near_1->vertex_first.V = 0x1FFFFF + texture_scroll.y.val;

                int64_t coordinate_1_lightness = engine_coordinate_1->shade_intensity;
                int64_t coordinate_1_distance = engine_coordinate_1->render_distance;

                if (argument5 >= 0)
                    coordinate_1_lightness = (coordinate_1_lightness * (3 * argument5 + 81920)) >> 17;

                int64_t apply_lighting_to_triangle_nearby_1;
                if (coordinate_1_distance <= fade_min)
                {
                    apply_lighting_to_triangle_nearby_1 = coordinate_1_lightness << 8;
                }
                else if (coordinate_1_distance < fade_max)
                {
                    apply_lighting_to_triangle_nearby_1 = coordinate_1_lightness * (fade_scaler - coordinate_1_distance) / fade_range + 0x8000;
                }
                else
                {
                    apply_lighting_to_triangle_nearby_1 = 0x8000;
                }

                triangle_bucket_near_1->vertex_first.S = apply_lighting_to_triangle_nearby_1;
                triangle_bucket_near_1->vertex_second.X = engine_coordinate_2->view_width;
                triangle_bucket_near_1->vertex_second.Z = worldframe_depth_from_view_z(engine_coordinate_2->z);
                triangle_bucket_near_1->vertex_second.Y = engine_coordinate_2->view_height;
                triangle_bucket_near_1->vertex_second.U = texture_scroll.x.val;
                triangle_bucket_near_1->vertex_second.V = 0x1FFFFF + texture_scroll.y.val;

                int64_t coordinate_2_lightness = engine_coordinate_2->shade_intensity;
                int64_t coordinate_2_distance = engine_coordinate_2->render_distance;

                if (argument5 >= 0)
                    coordinate_2_lightness = (coordinate_2_lightness * (3 * argument5 + 81920)) >> 17;

                int64_t apply_lighting_to_triangle_nearby_2;
                if (coordinate_2_distance <= fade_min)
                {
                    apply_lighting_to_triangle_nearby_2 = coordinate_2_lightness << 8;
                }
                else if (coordinate_2_distance < fade_max)
                {
                    apply_lighting_to_triangle_nearby_2 = coordinate_2_lightness * (fade_scaler - coordinate_2_distance) / fade_range + 0x8000;
                }
                else
                {
                    apply_lighting_to_triangle_nearby_2 = 0x8000;
                }

                triangle_bucket_near_1->vertex_second.S = apply_lighting_to_triangle_nearby_2;
                triangle_bucket_near_1->vertex_third.X = engine_coordinate_3->view_width;
                triangle_bucket_near_1->vertex_third.Z = worldframe_depth_from_view_z(engine_coordinate_3->z);
                triangle_bucket_near_1->vertex_third.Y = engine_coordinate_3->view_height;
                triangle_bucket_near_1->vertex_third.U = texture_scroll.x.val;
                triangle_bucket_near_1->vertex_third.V = texture_scroll.y.val;

                int64_t coordinate_3_lightness = engine_coordinate_3->shade_intensity;
                int64_t coordinate_3_distance = engine_coordinate_3->render_distance;

                if (argument5 >= 0)
                    coordinate_3_lightness = (coordinate_3_lightness * (3 * argument5 + 81920)) >> 17;

                int64_t apply_lighting_to_triangle_nearby_3;
                if (coordinate_3_distance <= fade_min)
                {
                    apply_lighting_to_triangle_nearby_3 = coordinate_3_lightness << 8;
                }
                else if (coordinate_3_distance < fade_max)
                {
                    apply_lighting_to_triangle_nearby_3 = coordinate_3_lightness * (fade_scaler - coordinate_3_distance) / fade_range + 0x8000;
                }
                else
                {
                    apply_lighting_to_triangle_nearby_3 = 0x8000;
                }

                triangle_bucket_near_1->vertex_third.S = apply_lighting_to_triangle_nearby_3;

                int64_t coordinate_1_z = engine_coordinate_1->z;
                if (coordinate_1_z >= 32)
                {
                    int64_t coordinate_2_z = engine_coordinate_2->z;
                    int64_t coordinate_3_z = engine_coordinate_3->z;
                    if (coordinate_2_z >= 32)
                    {
                        if (coordinate_3_z >= 32)
                        {
                            triangle_bucket_near_1->coordinate_first.x = engine_coordinate_1->x;
                            triangle_bucket_near_1->coordinate_first.y = engine_coordinate_1->y;
                            triangle_bucket_near_1->coordinate_first.z = engine_coordinate_1->z;
                            xyz5 = &triangle_bucket_near_1->coordinate_second;
                            triangle_bucket_near_1->coordinate_second.x = engine_coordinate_2->x;
                            xyz6 = &triangle_bucket_near_1->coordinate_third;
                            xyz5->y = engine_coordinate_2->y;
                            xyz5->z = engine_coordinate_2->z;
                            xyz6->x = engine_coordinate_3->x;
                            xyz6->y = engine_coordinate_3->y;
                            xyz6->z = engine_coordinate_3->z;
                        }
                        else
                        {
                            triangle_bucket_near_4 = (struct BucketKindPolygonNearFP *)getpoly;
                            getpoly += sizeof(struct BucketKindPolygonNearFP);
                            triangle_bucket_near_4->subtype = splittypes[16 * (engine_coordinate_3->clip_flags & 3) + 4 * (engine_coordinate_1->clip_flags & 3) + (engine_coordinate_2->clip_flags & 3)];
                            triangle_bucket_near_4->b.next = buckets[divided_z];
                            triangle_bucket_near_4->b.kind = QK_PolygonNearFP;
                            buckets[divided_z] = &triangle_bucket_near_4->b;
                            triangle_bucket_near_4->block = argument4;
                            triangle_bucket_near_1->coordinate_first.x = engine_coordinate_1->x;
                            triangle_bucket_near_1->coordinate_first.y = engine_coordinate_1->y;
                            triangle_bucket_near_1->coordinate_first.z = engine_coordinate_1->z;
                            triangle_bucket_near_1->coordinate_second.x = engine_coordinate_2->x;
                            triangle_bucket_near_1->coordinate_second.y = engine_coordinate_2->y;
                            triangle_bucket_near_1->coordinate_second.z = engine_coordinate_2->z;
                            memcpy(&triangle_bucket_near_4->vertex_third, &triangle_bucket_near_1->vertex_third, sizeof(triangle_bucket_near_4->vertex_third));
                            memcpy(&triangle_bucket_near_4->vertex_second, &triangle_bucket_near_1->vertex_second, sizeof(triangle_bucket_near_4->vertex_second));

                            int64_t z_ratio_1 = ((32 - engine_coordinate_3->z) << 8) / (engine_coordinate_1->z - engine_coordinate_3->z);

                            triangle_bucket_near_1->coordinate_third.x = engine_coordinate_3->x + ((z_ratio_1 * (engine_coordinate_1->x - engine_coordinate_3->x)) >> 8);
                            triangle_bucket_near_1->coordinate_third.y = engine_coordinate_3->y + ((z_ratio_1 * (engine_coordinate_1->y - engine_coordinate_3->y)) >> 8);
                            triangle_bucket_near_1->coordinate_third.z = 32;
                            perspective(&triangle_bucket_near_1->coordinate_third, &triangle_bucket_near_1->vertex_third);
                            triangle_bucket_near_1->vertex_third.U += (z_ratio_1 * (triangle_bucket_near_1->vertex_first.U - triangle_bucket_near_1->vertex_third.U)) >> 8;
                            triangle_bucket_near_1->vertex_third.V += (z_ratio_1 * (triangle_bucket_near_1->vertex_first.V - triangle_bucket_near_1->vertex_third.V)) >> 8;

                            int64_t light_factor_1 = triangle_bucket_near_1->vertex_third.S;
                            int64_t light_delta_1 = (z_ratio_1 * (triangle_bucket_near_1->vertex_first.S - light_factor_1)) >> 8;

                            polypoint3 = &triangle_bucket_near_1->vertex_third;
                            xyz4 = &triangle_bucket_near_1->coordinate_third;
                            xyz4[-3].z = light_factor_1 + light_delta_1;
                            memcpy(&triangle_bucket_near_4->vertex_first, polypoint3, sizeof(triangle_bucket_near_4->vertex_first));
                            triangle_bucket_near_4->coordinate_first.x = xyz4->x;
                            triangle_bucket_near_4->coordinate_first.y = xyz4->y;
                            triangle_bucket_near_4->coordinate_first.z = xyz4->z;
                            triangle_bucket_near_4->coordinate_second.x = engine_coordinate_2->x;
                            triangle_bucket_near_4->coordinate_second.y = engine_coordinate_2->y;
                            triangle_bucket_near_4->coordinate_second.z = engine_coordinate_2->z;

                            int64_t z_ratio_2 = ((32 - engine_coordinate_3->z) << 8) / (engine_coordinate_2->z - engine_coordinate_3->z);

                            triangle_bucket_near_4->coordinate_third.x = engine_coordinate_3->x + ((z_ratio_2 * (engine_coordinate_2->x - engine_coordinate_3->x)) >> 8);
                            triangle_bucket_near_4->coordinate_third.y = engine_coordinate_3->y + ((z_ratio_2 * (engine_coordinate_2->y - engine_coordinate_3->y)) >> 8);
                            triangle_bucket_near_4->coordinate_third.z = 32;
                            perspective(&triangle_bucket_near_4->coordinate_third, &triangle_bucket_near_4->vertex_third);
                            triangle_bucket_near_4->vertex_third.U += (z_ratio_2 * (triangle_bucket_near_4->vertex_second.U - triangle_bucket_near_4->vertex_third.U)) >> 8;
                            triangle_bucket_near_4->vertex_third.V += (z_ratio_2 * (triangle_bucket_near_4->vertex_second.V - triangle_bucket_near_4->vertex_third.V)) >> 8;
                            triangle_bucket_near_4->vertex_third.S += (z_ratio_2 * (triangle_bucket_near_4->vertex_second.S - triangle_bucket_near_4->vertex_third.S)) >> 8;
                        }
                    }
                    else if (coordinate_3_z >= 32)
                    {
                        triangle_bucket_near_3 = (struct BucketKindPolygonNearFP *)getpoly;
                        getpoly += sizeof(struct BucketKindPolygonNearFP);
                        triangle_bucket_near_3->subtype = splittypes[16 * (engine_coordinate_3->clip_flags & 3) + 4 * (engine_coordinate_1->clip_flags & 3) + (engine_coordinate_2->clip_flags & 3)];
                        triangle_bucket_near_3->b.next = buckets[divided_z];
                        triangle_bucket_near_3->b.kind = QK_PolygonNearFP;
                        buckets[divided_z] = &triangle_bucket_near_3->b;
                        triangle_bucket_near_3->block = argument4;
                        triangle_bucket_near_1->coordinate_first.x = engine_coordinate_1->x;
                        triangle_bucket_near_1->coordinate_first.y = engine_coordinate_1->y;
                        triangle_bucket_near_1->coordinate_first.z = engine_coordinate_1->z;
                        triangle_bucket_near_1->coordinate_third.x = engine_coordinate_3->x;
                        triangle_bucket_near_1->coordinate_third.y = engine_coordinate_3->y;
                        triangle_bucket_near_1->coordinate_third.z = engine_coordinate_3->z;
                        memcpy(&triangle_bucket_near_3->vertex_second, &triangle_bucket_near_1->vertex_second, sizeof(triangle_bucket_near_3->vertex_second));
                        memcpy(&triangle_bucket_near_3->vertex_third, &triangle_bucket_near_1->vertex_third, sizeof(triangle_bucket_near_3->vertex_third));

                        int64_t z_split_1 = ((32 - engine_coordinate_2->z) << 8) / (engine_coordinate_1->z - engine_coordinate_2->z);

                        triangle_bucket_near_1->coordinate_second.x = engine_coordinate_2->x + ((z_split_1 * (engine_coordinate_1->x - engine_coordinate_2->x)) >> 8);
                        triangle_bucket_near_1->coordinate_second.y = engine_coordinate_2->y + ((z_split_1 * (engine_coordinate_1->y - engine_coordinate_2->y)) >> 8);
                        triangle_bucket_near_1->coordinate_second.z = 32;
                        perspective(&triangle_bucket_near_1->coordinate_second, &triangle_bucket_near_1->vertex_second);
                        triangle_bucket_near_1->vertex_second.U += (z_split_1 * (triangle_bucket_near_1->vertex_first.U - triangle_bucket_near_1->vertex_second.U)) >> 8;
                        triangle_bucket_near_1->vertex_second.V += (z_split_1 * (triangle_bucket_near_1->vertex_first.V - triangle_bucket_near_1->vertex_second.V)) >> 8;

                        int64_t light_base_1 = triangle_bucket_near_1->vertex_second.S;
                        int64_t light_delta_2 = (z_split_1 * (triangle_bucket_near_1->vertex_first.S - light_base_1)) >> 8;

                        polypoint2 = &triangle_bucket_near_1->vertex_second;
                        xyz3 = &triangle_bucket_near_1->coordinate_second;
                        xyz3[-3].x = light_base_1 + light_delta_2;
                        memcpy(&triangle_bucket_near_3->vertex_first, polypoint2, sizeof(triangle_bucket_near_3->vertex_first));
                        triangle_bucket_near_3->coordinate_first.x = xyz3->x;
                        triangle_bucket_near_3->coordinate_first.y = xyz3->y;
                        triangle_bucket_near_3->coordinate_first.z = xyz3->z;
                        triangle_bucket_near_3->coordinate_third.x = engine_coordinate_3->x;
                        triangle_bucket_near_3->coordinate_third.y = engine_coordinate_3->y;
                        triangle_bucket_near_3->coordinate_third.z = engine_coordinate_3->z;

                        int64_t z_ratio_3 = ((32 - engine_coordinate_2->z) << 8) / (engine_coordinate_3->z - engine_coordinate_2->z);

                        triangle_bucket_near_3->coordinate_second.x = engine_coordinate_2->x + ((z_ratio_3 * (engine_coordinate_3->x - engine_coordinate_2->x)) >> 8);
                        triangle_bucket_near_3->coordinate_second.y = engine_coordinate_2->y + ((z_ratio_3 * (engine_coordinate_3->y - engine_coordinate_2->y)) >> 8);
                        triangle_bucket_near_3->coordinate_second.z = 32;
                        perspective(&triangle_bucket_near_3->coordinate_second, &triangle_bucket_near_3->vertex_second);
                        triangle_bucket_near_3->vertex_second.U += (z_ratio_3 * (triangle_bucket_near_3->vertex_third.U - triangle_bucket_near_3->vertex_second.U)) >> 8;
                        triangle_bucket_near_3->vertex_second.V += (z_ratio_3 * (triangle_bucket_near_3->vertex_third.V - triangle_bucket_near_3->vertex_second.V)) >> 8;
                        triangle_bucket_near_3->vertex_second.S += (z_ratio_3 * (triangle_bucket_near_3->vertex_third.S - triangle_bucket_near_3->vertex_second.S)) >> 8;
                    }
                    else
                    {
                        int64_t z_split_2 = ((32 - coordinate_2_z) << 8) / (coordinate_1_z - coordinate_2_z);

                        triangle_bucket_near_1->coordinate_second.x = engine_coordinate_2->x + ((z_split_2 * (engine_coordinate_1->x - engine_coordinate_2->x)) >> 8);
                        triangle_bucket_near_1->coordinate_second.y = engine_coordinate_2->y + ((z_split_2 * (engine_coordinate_1->y - engine_coordinate_2->y)) >> 8);
                        triangle_bucket_near_1->coordinate_second.z = 32;
                        perspective(&triangle_bucket_near_1->coordinate_second, &triangle_bucket_near_1->vertex_second);
                        triangle_bucket_near_1->vertex_second.U += (z_split_2 * (triangle_bucket_near_1->vertex_first.U - triangle_bucket_near_1->vertex_second.U)) >> 8;
                        triangle_bucket_near_1->vertex_second.V += (z_split_2 * (triangle_bucket_near_1->vertex_first.V - triangle_bucket_near_1->vertex_second.V)) >> 8;
                        triangle_bucket_near_1->vertex_second.S += (z_split_2 * (triangle_bucket_near_1->vertex_first.S - triangle_bucket_near_1->vertex_second.S)) >> 8;

                        int64_t z_ratio_4 = ((32 - engine_coordinate_3->z) << 8) / (engine_coordinate_1->z - engine_coordinate_3->z);

                        triangle_bucket_near_1->coordinate_third.x = engine_coordinate_3->x + ((z_ratio_4 * (engine_coordinate_1->x - engine_coordinate_3->x)) >> 8);
                        triangle_bucket_near_1->coordinate_third.y = engine_coordinate_3->y + ((z_ratio_4 * (engine_coordinate_1->y - engine_coordinate_3->y)) >> 8);
                        triangle_bucket_near_1->coordinate_third.z = 32;
                        perspective(&triangle_bucket_near_1->coordinate_third, &triangle_bucket_near_1->vertex_third);
                        triangle_bucket_near_1->vertex_third.U += (z_ratio_4 * (triangle_bucket_near_1->vertex_first.U - triangle_bucket_near_1->vertex_third.U)) >> 8;
                        triangle_bucket_near_1->vertex_third.V += (z_ratio_4 * (triangle_bucket_near_1->vertex_first.V - triangle_bucket_near_1->vertex_third.V)) >> 8;

                        int64_t light_base_2 = triangle_bucket_near_1->vertex_first.S;
                        int64_t light_base_3 = triangle_bucket_near_1->vertex_third.S;

                        xyz2 = &triangle_bucket_near_1->coordinate_first;
                        xyz2[-1].z = light_base_3 + ((z_ratio_4 * (light_base_2 - light_base_3)) >> 8);
                        xyz2->x = engine_coordinate_1->x;
                        xyz2->y = engine_coordinate_1->y;
                        xyz2->z = engine_coordinate_1->z;
                    }
                }
                else if (engine_coordinate_2->z >= 32)
                {
                    if (engine_coordinate_3->z >= 32)
                    {
                        triangle_bucket_near_2 = (struct BucketKindPolygonNearFP *)getpoly;
                        getpoly += sizeof(struct BucketKindPolygonNearFP);
                        triangle_bucket_near_2->subtype = splittypes[16 * (engine_coordinate_3->clip_flags & 3) + 4 * (engine_coordinate_1->clip_flags & 3) + (engine_coordinate_2->clip_flags & 3)];
                        triangle_bucket_near_2->b.next = buckets[divided_z];
                        triangle_bucket_near_2->b.kind = QK_PolygonNearFP;
                        buckets[divided_z] = &triangle_bucket_near_2->b;
                        triangle_bucket_near_2->block = argument4;
                        triangle_bucket_near_1->coordinate_second.x = engine_coordinate_2->x;
                        triangle_bucket_near_1->coordinate_second.y = engine_coordinate_2->y;
                        triangle_bucket_near_1->coordinate_second.z = engine_coordinate_2->z;
                        triangle_bucket_near_1->coordinate_third.x = engine_coordinate_3->x;
                        triangle_bucket_near_1->coordinate_third.y = engine_coordinate_3->y;
                        triangle_bucket_near_1->coordinate_third.z = engine_coordinate_3->z;
                        memcpy(&triangle_bucket_near_2->vertex_first, &triangle_bucket_near_1->vertex_first, sizeof(triangle_bucket_near_2->vertex_first));
                        memcpy(&triangle_bucket_near_2->vertex_third, &triangle_bucket_near_1->vertex_third, sizeof(triangle_bucket_near_2->vertex_third));

                        int64_t z_split_3 = ((32 - engine_coordinate_1->z) << 8) / (engine_coordinate_2->z - engine_coordinate_1->z);

                        triangle_bucket_near_1->coordinate_first.x = engine_coordinate_1->x + ((z_split_3 * (engine_coordinate_2->x - engine_coordinate_1->x)) >> 8);
                        triangle_bucket_near_1->coordinate_first.y = engine_coordinate_1->y + ((z_split_3 * (engine_coordinate_2->y - engine_coordinate_1->y)) >> 8);
                        triangle_bucket_near_1->coordinate_first.z = 32;
                        perspective(&triangle_bucket_near_1->coordinate_first, &triangle_bucket_near_1->vertex_first);
                        triangle_bucket_near_1->vertex_first.U += (z_split_3 * (triangle_bucket_near_1->vertex_second.U - triangle_bucket_near_1->vertex_first.U)) >> 8;
                        triangle_bucket_near_1->vertex_first.V += (z_split_3 * (triangle_bucket_near_1->vertex_second.V - triangle_bucket_near_1->vertex_first.V)) >> 8;

                        int64_t light_base_4 = triangle_bucket_near_1->vertex_first.S;
                        int64_t light_delta_3 = (z_split_3 * (triangle_bucket_near_1->vertex_second.S - light_base_4)) >> 8;

                        polypoint1 = &triangle_bucket_near_1->vertex_first;
                        xyz1 = &triangle_bucket_near_1->coordinate_first;
                        xyz1[-4].y = light_base_4 + light_delta_3;
                        memcpy(&triangle_bucket_near_2->vertex_second, polypoint1, sizeof(triangle_bucket_near_2->vertex_second));
                        triangle_bucket_near_2->coordinate_second.x = xyz1->x;
                        triangle_bucket_near_2->coordinate_second.y = xyz1->y;
                        triangle_bucket_near_2->coordinate_second.z = xyz1->z;
                        triangle_bucket_near_2->coordinate_third.x = engine_coordinate_3->x;
                        triangle_bucket_near_2->coordinate_third.y = engine_coordinate_3->y;
                        triangle_bucket_near_2->coordinate_third.z = engine_coordinate_3->z;

                        int64_t z_ratio_5 = ((32 - engine_coordinate_1->z) << 8) / (engine_coordinate_3->z - engine_coordinate_1->z);

                        triangle_bucket_near_2->coordinate_first.x = engine_coordinate_1->x + ((z_ratio_5 * (engine_coordinate_3->x - engine_coordinate_1->x)) >> 8);
                        triangle_bucket_near_2->coordinate_first.y = engine_coordinate_1->y + ((z_ratio_5 * (engine_coordinate_3->y - engine_coordinate_1->y)) >> 8);
                        triangle_bucket_near_2->coordinate_first.z = 32;
                        perspective(&triangle_bucket_near_2->coordinate_first, &triangle_bucket_near_2->vertex_first);
                        triangle_bucket_near_2->vertex_first.U += (z_ratio_5 * (triangle_bucket_near_2->vertex_third.U - triangle_bucket_near_2->vertex_first.U)) >> 8;
                        triangle_bucket_near_2->vertex_first.V += (z_ratio_5 * (triangle_bucket_near_2->vertex_third.V - triangle_bucket_near_2->vertex_first.V)) >> 8;
                        triangle_bucket_near_2->vertex_first.S += (z_ratio_5 * (triangle_bucket_near_2->vertex_third.S - triangle_bucket_near_2->vertex_first.S)) >> 8;
                    }
                    else
                    {
                        triangle_bucket_near_1->coordinate_second.x = engine_coordinate_2->x;
                        triangle_bucket_near_1->coordinate_second.y = engine_coordinate_2->y;
                        triangle_bucket_near_1->coordinate_second.z = engine_coordinate_2->z;

                        int64_t z_split_4 = ((32 - engine_coordinate_1->z) << 8) / (engine_coordinate_2->z - engine_coordinate_1->z);

                        triangle_bucket_near_1->coordinate_first.x = engine_coordinate_1->x + ((z_split_4 * (engine_coordinate_2->x - engine_coordinate_1->x)) >> 8);
                        triangle_bucket_near_1->coordinate_first.y = engine_coordinate_1->y + ((z_split_4 * (engine_coordinate_2->y - engine_coordinate_1->y)) >> 8);
                        triangle_bucket_near_1->coordinate_first.z = 32;
                        perspective(&triangle_bucket_near_1->coordinate_first, &triangle_bucket_near_1->vertex_first);
                        triangle_bucket_near_1->vertex_first.U += (z_split_4 * (triangle_bucket_near_1->vertex_second.U - triangle_bucket_near_1->vertex_first.U)) >> 8;
                        triangle_bucket_near_1->vertex_first.V += (z_split_4 * (triangle_bucket_near_1->vertex_second.V - triangle_bucket_near_1->vertex_first.V)) >> 8;
                        triangle_bucket_near_1->vertex_first.S += (z_split_4 * (triangle_bucket_near_1->vertex_second.S - triangle_bucket_near_1->vertex_first.S)) >> 8;

                        int64_t z_ratio_6 = ((32 - engine_coordinate_3->z) << 8) / (engine_coordinate_2->z - engine_coordinate_3->z);

                        triangle_bucket_near_1->coordinate_third.x = engine_coordinate_3->x + ((z_ratio_6 * (engine_coordinate_2->x - engine_coordinate_3->x)) >> 8);
                        triangle_bucket_near_1->coordinate_third.y = engine_coordinate_3->y + ((z_ratio_6 * (engine_coordinate_2->y - engine_coordinate_3->y)) >> 8);
                        triangle_bucket_near_1->coordinate_third.z = 32;
                        perspective(&triangle_bucket_near_1->coordinate_third, &triangle_bucket_near_1->vertex_third);
                        triangle_bucket_near_1->vertex_third.U += (z_ratio_6 * (triangle_bucket_near_1->vertex_second.U - triangle_bucket_near_1->vertex_third.U)) >> 8;
                        triangle_bucket_near_1->vertex_third.V += (z_ratio_6 * (triangle_bucket_near_1->vertex_second.V - triangle_bucket_near_1->vertex_third.V)) >> 8;
                        triangle_bucket_near_1->vertex_third.S += (z_ratio_6 * (triangle_bucket_near_1->vertex_second.S - triangle_bucket_near_1->vertex_third.S)) >> 8;
                    }
                }
                else
                {
                    triangle_bucket_near_1->coordinate_third.x = engine_coordinate_3->x;
                    triangle_bucket_near_1->coordinate_third.y = engine_coordinate_3->y;
                    triangle_bucket_near_1->coordinate_third.z = engine_coordinate_3->z;

                    int64_t z_ratio_7 = ((32 - engine_coordinate_1->z) << 8) / (engine_coordinate_3->z - engine_coordinate_1->z);

                    triangle_bucket_near_1->coordinate_first.x = engine_coordinate_1->x + ((z_ratio_7 * (engine_coordinate_3->x - engine_coordinate_1->x)) >> 8);
                    triangle_bucket_near_1->coordinate_first.y = engine_coordinate_1->y + ((z_ratio_7 * (engine_coordinate_3->y - engine_coordinate_1->y)) >> 8);
                    triangle_bucket_near_1->coordinate_first.z = 32;
                    perspective(&triangle_bucket_near_1->coordinate_first, &triangle_bucket_near_1->vertex_first);
                    triangle_bucket_near_1->vertex_first.U += (z_ratio_7 * (triangle_bucket_near_1->vertex_third.U - triangle_bucket_near_1->vertex_first.U)) >> 8;
                    triangle_bucket_near_1->vertex_first.V += (z_ratio_7 * (triangle_bucket_near_1->vertex_third.V - triangle_bucket_near_1->vertex_first.V)) >> 8;
                    triangle_bucket_near_1->vertex_first.S += (z_ratio_7 * (triangle_bucket_near_1->vertex_third.S - triangle_bucket_near_1->vertex_first.S)) >> 8;

                    int64_t z_ratio_8 = ((32 - engine_coordinate_2->z) << 8) / (engine_coordinate_3->z - engine_coordinate_2->z);

                    triangle_bucket_near_1->coordinate_second.x = engine_coordinate_2->x + ((z_ratio_8 * (engine_coordinate_3->x - engine_coordinate_2->x)) >> 8);
                    triangle_bucket_near_1->coordinate_second.y = engine_coordinate_2->y + ((z_ratio_8 * (engine_coordinate_3->y - engine_coordinate_2->y)) >> 8);
                    triangle_bucket_near_1->coordinate_second.z = 32;
                    perspective(&triangle_bucket_near_1->coordinate_second, &triangle_bucket_near_1->vertex_second);
                    triangle_bucket_near_1->vertex_second.U += (z_ratio_8 * (triangle_bucket_near_1->vertex_third.U - triangle_bucket_near_1->vertex_second.U)) >> 8;
                    triangle_bucket_near_1->vertex_second.V += (z_ratio_8 * (triangle_bucket_near_1->vertex_third.V - triangle_bucket_near_1->vertex_second.V)) >> 8;
                    triangle_bucket_near_1->vertex_second.S += (z_ratio_8 * (triangle_bucket_near_1->vertex_third.S - triangle_bucket_near_1->vertex_second.S)) >> 8;
                }
            }
            else
            {
                triangle_bucket_far = (struct BucketKindPolygonStandard *)getpoly;
                getpoly += sizeof(struct BucketKindPolygonStandard);
                triangle_bucket_far->b.next = buckets[divided_z];
                triangle_bucket_far->b.kind = QK_PolygonStandard;
                buckets[divided_z] = &triangle_bucket_far->b;
                triangle_bucket_far->block = argument4;

                triangle_bucket_far->vertex_first.X = engine_coordinate_1->view_width;

                triangle_bucket_far->vertex_first.Z = worldframe_depth_from_view_z(engine_coordinate_1->z);
                triangle_bucket_far->vertex_first.Y = engine_coordinate_1->view_height;
                triangle_bucket_far->vertex_first.U = 0x1FFFFF + texture_scroll.x.val;
                triangle_bucket_far->vertex_first.V = 0x1FFFFF + texture_scroll.y.val;

                int64_t coordinate_1_lightness = engine_coordinate_1->shade_intensity;
                int64_t coordinate_1_distance = engine_coordinate_1->render_distance;

                if (argument5 >= 0)
                    coordinate_1_lightness = (coordinate_1_lightness * (3 * argument5 + 81920)) >> 17;

                int64_t apply_lighting_to_triangle_far_1;
                if (coordinate_1_distance <= fade_min)
                {
                    apply_lighting_to_triangle_far_1 = coordinate_1_lightness << 8;
                }
                else if (coordinate_1_distance < fade_max)
                {
                    apply_lighting_to_triangle_far_1 = coordinate_1_lightness * (fade_scaler - coordinate_1_distance) / fade_range + 0x8000;
                }
                else
                {
                    apply_lighting_to_triangle_far_1 = 0x8000;
                }

                triangle_bucket_far->vertex_first.S = apply_lighting_to_triangle_far_1;
                triangle_bucket_far->vertex_second.X = engine_coordinate_2->view_width;
                triangle_bucket_far->vertex_second.Z = worldframe_depth_from_view_z(engine_coordinate_2->z);
                triangle_bucket_far->vertex_second.Y = engine_coordinate_2->view_height;
                triangle_bucket_far->vertex_second.U = texture_scroll.x.val;
                triangle_bucket_far->vertex_second.V = 0x1FFFFF + texture_scroll.y.val;

                int64_t coordinate_2_lightness = engine_coordinate_2->shade_intensity;
                int64_t coordinate_2_distance = engine_coordinate_2->render_distance;

                if (argument5 >= 0)
                    coordinate_2_lightness = (coordinate_2_lightness * (3 * argument5 + 81920)) >> 17;

                int64_t apply_lighting_to_triangle_far_2;
                if (coordinate_2_distance <= fade_min)
                {
                    apply_lighting_to_triangle_far_2 = coordinate_2_lightness << 8;
                }
                else if (coordinate_2_distance < fade_max)
                {
                    apply_lighting_to_triangle_far_2 = coordinate_2_lightness * (fade_scaler - coordinate_2_distance) / fade_range + 0x8000;
                }
                else
                {
                    apply_lighting_to_triangle_far_2 = 0x8000;
                }

                triangle_bucket_far->vertex_second.S = apply_lighting_to_triangle_far_2;
                triangle_bucket_far->vertex_third.X = engine_coordinate_3->view_width;
                triangle_bucket_far->vertex_third.Z = worldframe_depth_from_view_z(engine_coordinate_3->z);
                triangle_bucket_far->vertex_third.Y = engine_coordinate_3->view_height;
                triangle_bucket_far->vertex_third.U = texture_scroll.x.val;
                triangle_bucket_far->vertex_third.V = texture_scroll.y.val;

                int64_t coordinate_3_lightness = engine_coordinate_3->shade_intensity;
                int64_t coordinate_3_distance = engine_coordinate_3->render_distance;

                if (argument5 >= 0)
                    coordinate_3_lightness = (coordinate_3_lightness * (3 * argument5 + 81920)) >> 17;

                if (coordinate_3_distance <= fade_min)
                {
                    triangle_bucket_far->vertex_third.S = coordinate_3_lightness << 8;
                }
                else if (coordinate_3_distance < fade_max)
                {
                    triangle_bucket_far->vertex_third.S = coordinate_3_lightness * (fade_scaler - coordinate_3_distance) / fade_range + 0x8000;
                }
                else
                {
                    triangle_bucket_far->vertex_third.S = 0x8000;
                }
            }
        }
    }
}

static TbBool add_light_to_nearest_list(struct NearestLights* nlgt, int64_t * nlgt_dist, const struct Light* lgt, int64_t dist)
{
    int64_t i;
    for (i = settings.video_shadows-1; i > 0; i--)
    {
        nlgt_dist[i] = nlgt_dist[i-1];
        nlgt->coord[i] = nlgt->coord[i-1];
    }
    nlgt_dist[0] = dist;
    nlgt->coord[0] = lgt->mappos;
    return true;
}

static void find_closest_lights_on_list(struct NearestLights *nlgt, int64_t *nlgt_dist, const struct Coord3d *pos, ThingIndex list_start_idx)
{
    int64_t i;
    uint64_t k;
    if (settings.video_shadows < 1)
        return;
    i = list_start_idx;
    k = 0;
    while (i > 0)
    {
        struct Light *lgt;
        lgt = &kfx_sim_state.light_registry.lights[i];
        i = lgt->next_in_list;
        // Per-light code
        if ((lgt->flags & LgtF_Allocated) != 0)
        {
            int64_t dist;
            dist = get_chessboard_distance(pos, &lgt->mappos);
            if ((dist < 2560) && (nlgt_dist[settings.video_shadows-1] > dist)
                && (pos->x.val != lgt->mappos.x.val) && (pos->y.val != lgt->mappos.y.val))
            {
                add_light_to_nearest_list(nlgt, nlgt_dist, lgt, dist);
            }
        }
        // Per-light code ends
        k++;
        if (k > LIGHTS_COUNT)
        {
            ERRORLOG("Infinite loop detected when sweeping lights list");
            break;
        }
    }
}

static int64_t find_closest_lights(const struct Coord3d* pos, struct NearestLights* nlgt)
{
    int64_t count;
    int64_t nlgt_dist[SHADOW_SOURCES_MAX_COUNT];
    int64_t i;
    for (i = 0; i < SHADOW_SOURCES_MAX_COUNT; i++) {
        nlgt_dist[i] = INT32_MAX;
    }
    i = kfx_sim_state.thing_lists[TngList_StaticLights].index;
    find_closest_lights_on_list(nlgt, nlgt_dist, pos, i);
    i = kfx_sim_state.thing_lists[TngList_DynamLights].index;
    find_closest_lights_on_list(nlgt, nlgt_dist, pos, i);
    count = 0;
    for (i = 0; i < SHADOW_SOURCES_MAX_COUNT; i++) {
        if (nlgt_dist[i] == INT32_MAX)
            break;
        count++;
    }
    return count;
}

static int64_t find_fade_S(struct EngineCoord *ecor)
{
    if (ecor->render_distance <= fade_min) {
        return ecor->shade_intensity << 8;
    }
    else if (ecor->render_distance >= fade_max) {
        return 32768;
    } else {
        return ecor->shade_intensity * (fade_scaler - ecor->render_distance) / fade_range + 32768;
    }
}

static void create_shadows(struct Thing *thing, struct EngineCoord *ecor, struct Coord3d *pos)
{
    int64_t animation_sprite;
    unsigned char current_frame;
    int64_t mv_angle;
    int64_t sh_angle;
    int64_t sprite_angle;
    int64_t dist_sq;
    struct EngineCoord ecor1;
    struct EngineCoord ecor2;
    struct EngineCoord ecor3;
    struct EngineCoord ecor4;

    animation_sprite = get_render_animation_sprite(thing->anim_sprite);
    current_frame = thing->current_frame;
    struct KeeperSprite *spr = keepersprite_array(animation_sprite);
    if (spr == NULL) {
        return;
    }

    mv_angle = thing->move_angle_xy;
    sh_angle = get_angle_xy_to(pos, &thing->mappos);
    sprite_angle = (mv_angle - sh_angle) & ANGLE_MASK;
    dist_sq = (get_2d_distance_squared(&thing->mappos, pos) >> 17) + 16;
    if (dist_sq < 16) {
        dist_sq = 16;
    }
    else if (dist_sq > 31) {
        dist_sq = 31;
    }
    int64_t dim_ow;
    int64_t dim_oh;
    int64_t dim_th;
    int64_t dim_tw;
    get_keepsprite_unscaled_dimensions(animation_sprite, sprite_angle, current_frame, &dim_ow, &dim_oh, &dim_tw, &dim_th);
    if (dim_ow <= 0 || dim_oh <= 0 || dim_ow > 256 || dim_oh > 256)
    {
        WARNLOG("[md10 crash investigation] Invalid shadow dimensions dim_ow=%" PRId64 " dim_oh=%" PRId64 " for thing %" PRId64 " (anim=%" PRId64 " frame=%" PRId64 ")",
                (int64_t)(dim_ow), (int64_t)(dim_oh), (int64_t)(thing->index), (int64_t)(animation_sprite), (int64_t)(current_frame));
        return;
    }
    {
        int64_t sh_angle_sin = LbSinL(sh_angle);
        int64_t sh_angle_cos = LbCosL(sh_angle);

        int64_t base_y2 = 8 * (6 - dim_oh - dim_th + spr->shadow_offset);
        int64_t base_z2 = 8 * dim_tw;
        int64_t base_th = 8 * (dim_th - 4 * dist_sq) + 560;
        int64_t base_tw = 8 * (dim_tw + dim_ow);

        int64_t base_x = ecor->x;
        int64_t base_y = ecor->y;
        int64_t base_z = ecor->z;

        // near and far are measured from origin of thing
        int64_t near_x = base_y2 * sh_angle_sin;
        int64_t near_y = base_y2 * sh_angle_cos;

        int64_t left_x = base_z2 * sh_angle_cos;
        int64_t left_y = base_z2 * sh_angle_sin;
        int64_t far_x = base_th * sh_angle_sin;
        int64_t far_y = base_th * sh_angle_cos;
        int64_t right_x = base_tw * sh_angle_cos;
        int64_t right_y = base_tw * sh_angle_sin;

        // near/left
        ecor1.x = base_x + FROM_FIXED(left_x - near_x);
        ecor1.y = base_y;
        ecor1.z = base_z - FROM_FIXED(left_y + near_y);

        // far/left
        ecor2.x = base_x + FROM_FIXED(left_x - far_x);
        ecor2.y = base_y;
        ecor2.z = base_z - FROM_FIXED(left_y + far_y);

        // far/right
        ecor3.x = base_x + FROM_FIXED(right_x - far_x);
        ecor3.y = base_y;
        ecor3.z = base_z - FROM_FIXED(right_y + far_y);

        // near/right
        ecor4.x = base_x + FROM_FIXED(right_x - near_x);
        ecor4.y = base_y;
        ecor4.z = base_z - FROM_FIXED(right_y + near_y);
    }

    rotpers(&ecor1, &camera_matrix);
    rotpers(&ecor2, &camera_matrix);
    rotpers(&ecor3, &camera_matrix);
    rotpers(&ecor4, &camera_matrix);

    int64_t min_cor_z = min(min(ecor1.z,ecor2.z),min(ecor3.z,ecor4.z));
    struct BucketKindCreatureShadow *kspr = (struct BucketKindCreatureShadow *)get_bucket_item(min_cor_z, QK_CreatureShadow, sizeof(struct BucketKindCreatureShadow));
    if (kspr == NULL)
        return;

    // P1
    kspr->vertex_first.X = ecor1.view_width;
    kspr->vertex_first.Z = worldframe_depth_from_view_z(ecor1.z);
    kspr->vertex_first.Y = ecor1.view_height;
    kspr->vertex_first.U = 0;
    kspr->vertex_first.V = TO_FIXED(dim_oh - 1);
    kspr->vertex_first.S = find_fade_S(&ecor1);

    // P2
    kspr->vertex_second.X = ecor2.view_width;
    kspr->vertex_second.Z = worldframe_depth_from_view_z(ecor2.z);
    kspr->vertex_second.Y = ecor2.view_height;
    kspr->vertex_second.U = 0;
    kspr->vertex_second.V = 0;
    kspr->vertex_second.S = find_fade_S(&ecor2);

    // P3
    kspr->vertex_third.X = ecor3.view_width;
    kspr->vertex_third.Z = worldframe_depth_from_view_z(ecor3.z);
    kspr->vertex_third.Y = ecor3.view_height;
    kspr->vertex_third.U = TO_FIXED(dim_ow - 1);
    kspr->vertex_third.V = 0;
    kspr->vertex_third.S = find_fade_S(&ecor3);

    // P4
    kspr->vertex_fourth.X = ecor4.view_width;
    kspr->vertex_fourth.Z = worldframe_depth_from_view_z(ecor4.z);
    kspr->vertex_fourth.Y = ecor4.view_height;
    kspr->vertex_fourth.U = TO_FIXED(dim_ow - 1);
    kspr->vertex_fourth.V = TO_FIXED(dim_oh - 1);
    kspr->vertex_fourth.S = find_fade_S(&ecor4);

    // overall
    kspr->vertex_first.S = dist_sq;
    kspr->angle = sprite_angle;
    kspr->anim_sprite = animation_sprite;
    kspr->current_frame = current_frame;
}

// Creature status flower above head in isometric view
static void add_draw_status_box(struct Thing *thing, struct EngineCoord *ecor)
{
    struct EngineCoord coord = *ecor;
    const struct CreatureModelConfig* crconf = creature_stats_get_from_thing(thing);
    struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
    int64_t offset = thing->clipbox_size_z + crconf->status_offset;
    offset += (offset * kfx_config_state.conf.crtr_conf.exp.size_increase_on_exp * cctrl->exp_level) / 100;
    coord.y += offset;
    rotpers(&coord, &camera_matrix);

    int64_t z_val = coord.z;
    if (!lens_mode)
        z_val = BUCKETS_STEP; // should get into bucket 1

    struct BucketKindCreatureStatus* poly = (struct BucketKindCreatureStatus*)get_bucket_item(z_val, QK_CreatureStatus, sizeof(struct BucketKindCreatureStatus));
    if (poly == NULL)
        return;

    poly->thing = thing;
    poly->x = coord.view_width;
    poly->y = coord.view_height;
    poly->z = coord.z;
}

int64_t engine_remap_texture_blocks(int64_t stl_x, int64_t stl_y, int64_t tex_id)
{
    texture_scroll = (struct Coord2d){0};
    int64_t slb_x = subtile_slab(stl_x);
    int64_t slb_y = subtile_slab(stl_y);
    return tex_id + (kfx_config_state.slab_ext_data[get_slab_number(slb_x,slb_y)] & 0x1F) * TEXTURE_BLOCKS_COUNT;
}

static int64_t get_abyss_liquid_scroll(const struct CubeConfigStats *texturing)
{
    double scroll = 0;
    if (flag_is_set(texturing->properties_flags, CPF_IsLava)) {
        scroll += render_abyss_lava_scroll;
    }
    if (flag_is_set(texturing->properties_flags, CPF_IsWater)) {
        scroll += render_abyss_water_scroll;
    }
    return TO_FIXED((int64_t)scroll);
}

static int64_t engine_remap_top_texture_blocks(MapSubtlCoord stl_x, MapSubtlCoord stl_y, int64_t texture)
{
    const struct CubeConfigStats *texturing = get_cube_model_stats(kfx_sim_state.top_cube[texture]);
    texture = engine_remap_texture_blocks(stl_x, stl_y, texture);
    int64_t offset = get_abyss_liquid_scroll(texturing);
    if (offset == 0) {
        return texture;
    }
    // The whole slab scrolls one way, toward the abyss slabs around it, so its
    // subtiles stay joined. Diagonal abyss only counts when no side touches one.
    MapSlabCoord slb_x = subtile_slab(stl_x);
    MapSlabCoord slb_y = subtile_slab(stl_y);
    TbBool touches_abyss = false;
    int64_t flow_x = 0;
    int64_t flow_y = 0;
    for (int64_t side = 0; side < AROUND_EIGHT_LENGTH && (side < 4 || !touches_abyss); side++) {
        const struct Around *direction = &my_around_eight[(2 * side + side / 4) & 7];
        MapSlabCoord adjacent_slb_x = slb_x + direction->delta_x;
        MapSlabCoord adjacent_slb_y = slb_y + direction->delta_y;
        if (side >= 4 && (!slab_is_liquid(adjacent_slb_x, slb_y) || !slab_is_liquid(slb_x, adjacent_slb_y))) {
            continue;
        }
        MapSubtlCoord adjacent_x = slab_subtile_center(adjacent_slb_x);
        MapSubtlCoord adjacent_y = slab_subtile_center(adjacent_slb_y);
        struct Map *adjacent_map = get_map_block_at(adjacent_x, adjacent_y);
        if (!map_block_revealed(adjacent_map, my_player_number) || !map_block_has_rendered_abyss(adjacent_map, adjacent_x, adjacent_y)) {
            continue;
        }
        touches_abyss = true;
        flow_x += direction->delta_x;
        flow_y += direction->delta_y;
    }
    texture_scroll.x.val = -max(-1, min(1, flow_x)) * offset;
    texture_scroll.y.val = -max(-1, min(1, flow_y)) * offset;
    return texture;
}

static int64_t engine_remap_abyss_wall_texture_blocks(MapSubtlCoord stl_x, MapSubtlCoord stl_y, int64_t cube, int64_t side)
{
    const struct CubeConfigStats *texturing = get_cube_model_stats(cube);
    int64_t texture = floor_to_ceiling_map[0];
    if (any_flag_is_set(texturing->properties_flags, CPF_IsLava | CPF_IsWater)) {
        texture = texturing->texture_id[side];
    }
    texture = engine_remap_texture_blocks(stl_x, stl_y, texture);
    texture_scroll.y.val = -get_abyss_liquid_scroll(texturing);
    return texture;
}

static void draw_abyss(const struct Column *colmn, const struct Map *mapblk, struct EngineCol *bec, struct EngineCol *fec, MapSubtlCoord stl_x, MapSubtlCoord stl_y);

static void do_a_plane_of_engine_columns_perspective(int64_t stl_x, int64_t stl_y, int64_t plane_start, int64_t plane_end)
{
    struct Column *blank_colmn;
    struct Column *colmn;
    struct Map *mapblk;
    struct Map *sib_mapblk;
    struct Column *sib_colmn;
    int64_t textr_idx;
    int64_t height_bit;
    SubtlCodedCoords center_block_idx;
    int64_t fepos;
    int64_t bepos;
    int64_t ecpos;
    int64_t clip_start;
    int64_t clip_end;
    struct CubeConfigStats *texturing;
    int64_t *cubenum_ptr;
    int64_t i;
    int64_t n;
    if ((stl_y <= 0) || (stl_y >= kfx_sim_state.map_subtiles_y))
        return;
    clip_start = plane_start;
    if (stl_x + plane_start < 1)
        clip_start = 1 - stl_x;
    clip_end = plane_end;
    if (stl_x + plane_end > kfx_sim_state.map_subtiles_x)
        clip_end = kfx_sim_state.map_subtiles_x - stl_x;
    struct EngineCol *bec;
    struct EngineCol *fec;
    bec = &back_ec[clip_start + MINMAX_ALMOST_HALF];
    fec = &front_ec[clip_start + MINMAX_ALMOST_HALF];
    blank_colmn = get_column(kfx_sim_state.unrevealed_column_idx);
    center_block_idx = clip_start + stl_x + (stl_y * (kfx_sim_state.map_subtiles_x+1));
    MapSubtlCoord center_x = clip_start + stl_x;
    for (i = clip_end-clip_start; i > 0; i--)
    {
        mapblk = get_map_block_at_pos(center_block_idx);
        colmn = blank_colmn;
        if (map_block_revealed(mapblk, my_player_number))
        {
            n = get_mapwho_thing_index(mapblk);
            if (n != 0)
                do_map_who(n);
            colmn = get_map_column(mapblk);
        }
        // Retrieve solidmasks for surrounding area
        int64_t solidmsk_center;
        int64_t solidmsk_top;
        int64_t solidmsk_bottom;
        int64_t solidmsk_left;
        int64_t solidmsk_right;
        solidmsk_center = colmn->solidmask;
        solidmsk_top = blank_colmn->solidmask;
        solidmsk_right = blank_colmn->solidmask;
        solidmsk_bottom = blank_colmn->solidmask;
        solidmsk_left = blank_colmn->solidmask;
        sib_mapblk = get_map_block_at_pos(center_block_idx-kfx_sim_state.map_subtiles_x-1);
        if (map_block_revealed(sib_mapblk, my_player_number)) {
            sib_colmn = get_map_column(sib_mapblk);
            solidmsk_top = sib_colmn->solidmask;
        }
        sib_mapblk = get_map_block_at_pos(center_block_idx+kfx_sim_state.map_subtiles_x+1);
        if (map_block_revealed(sib_mapblk, my_player_number)) {
            sib_colmn = get_map_column(sib_mapblk);
            solidmsk_bottom = sib_colmn->solidmask;
        }
        sib_mapblk = get_map_block_at_pos(center_block_idx-1);
        if (map_block_revealed(sib_mapblk, my_player_number)) {
            sib_colmn = get_map_column(sib_mapblk);
            solidmsk_left = sib_colmn->solidmask;
        }
        sib_mapblk = get_map_block_at_pos(center_block_idx+1);
        if (map_block_revealed(sib_mapblk, my_player_number)) {
            sib_colmn = get_map_column(sib_mapblk);
            solidmsk_right = sib_colmn->solidmask;
        }
        bepos = 0;
        fepos = 0;
        cubenum_ptr = &colmn->cubes[0];
        height_bit = 1;
        while (height_bit <= solidmsk_center)
        {
            texturing = get_cube_model_stats(*cubenum_ptr);
            if ((solidmsk_center & height_bit) != 0)
            {
              if ((solidmsk_top & height_bit) == 0)
              {

                  textr_idx = engine_remap_texture_blocks(center_x, stl_y, texturing->texture_id[sideoris[0].back_texture_index]);
                  do_a_trig_gourad_tr(&bec[1].cors[bepos+1], &bec[0].cors[bepos+1], &bec[0].cors[bepos],   textr_idx, normal_shade_back);
                  do_a_trig_gourad_bl(&bec[0].cors[bepos],   &bec[1].cors[bepos],   &bec[1].cors[bepos+1], textr_idx, normal_shade_back);
              }
              if ((solidmsk_bottom & height_bit) == 0)
              {
                  textr_idx = engine_remap_texture_blocks(center_x, stl_y, texturing->texture_id[sideoris[0].front_texture_index]);
                  do_a_trig_gourad_tr(&fec[0].cors[fepos+1], &fec[1].cors[fepos+1], &fec[1].cors[fepos],   textr_idx, normal_shade_front);
                  do_a_trig_gourad_bl(&fec[1].cors[fepos],   &fec[0].cors[fepos],   &fec[0].cors[fepos+1], textr_idx, normal_shade_front);
              }
              if ((solidmsk_left & height_bit) == 0)
              {
                  textr_idx = engine_remap_texture_blocks(center_x, stl_y, texturing->texture_id[sideoris[0].bottom_texture_index]);
                  do_a_trig_gourad_tr(&bec[0].cors[bepos+1], &fec[0].cors[fepos+1], &fec[0].cors[fepos],   textr_idx, normal_shade_left);
                  do_a_trig_gourad_bl(&fec[0].cors[fepos],   &bec[0].cors[bepos],   &bec[0].cors[bepos+1], textr_idx, normal_shade_left);
              }
              if ((solidmsk_right & height_bit) == 0)
              {
                  textr_idx = engine_remap_texture_blocks(center_x, stl_y, texturing->texture_id[sideoris[0].top_texture_index]);
                  do_a_trig_gourad_tr(&fec[1].cors[fepos+1], &bec[1].cors[bepos+1], &bec[1].cors[bepos],   textr_idx, normal_shade_right);
                  do_a_trig_gourad_bl(&bec[1].cors[bepos],   &fec[1].cors[fepos],   &fec[1].cors[fepos+1], textr_idx, normal_shade_right);
              }
            }
            bepos++; fepos++;
            cubenum_ptr++;
            height_bit = height_bit << 1;
        }
        TbBool abyss = cube_is_abyss(kfx_sim_state.top_cube[colmn->floor_texture]);
        draw_abyss(colmn, mapblk, bec, fec, center_x, stl_y);

        ecpos = floor_height_table[solidmsk_center];
        if (ecpos > 0)
        {
            cubenum_ptr = &colmn->cubes[ecpos-1];
            texturing = get_cube_model_stats(*cubenum_ptr);
            textr_idx = engine_remap_top_texture_blocks(center_x, stl_y, texturing->texture_id[4]);
            do_a_trig_gourad_tr(&bec[0].cors[ecpos], &bec[1].cors[ecpos], &fec[1].cors[ecpos], textr_idx, -1);
            do_a_trig_gourad_bl(&fec[1].cors[ecpos], &fec[0].cors[ecpos], &bec[0].cors[ecpos], textr_idx, -1);
        } else if (!abyss) {
            textr_idx = engine_remap_top_texture_blocks(center_x, stl_y, colmn->floor_texture);
            do_a_trig_gourad_tr(&bec[0].cors[ecpos], &bec[1].cors[ecpos], &fec[1].cors[ecpos], textr_idx, -1);
            do_a_trig_gourad_bl(&fec[1].cors[ecpos], &fec[0].cors[ecpos], &bec[0].cors[ecpos], textr_idx, -1);
        }
        // For tiles which have solid columns at top, draw them
        ecpos = lintel_top_height[solidmsk_center];
        if (ecpos > 0)
        {
            cubenum_ptr = &colmn->cubes[ecpos-1];
            texturing = get_cube_model_stats(*cubenum_ptr);
            textr_idx = engine_remap_top_texture_blocks(center_x, stl_y, texturing->texture_id[4]);
            do_a_trig_gourad_tr(&bec[0].cors[ecpos], &bec[1].cors[ecpos], &fec[1].cors[ecpos], textr_idx, -1);
            do_a_trig_gourad_bl(&fec[1].cors[ecpos], &fec[0].cors[ecpos], &bec[0].cors[ecpos], textr_idx, -1);

            ecpos =  lintel_bottom_height[solidmsk_center];
            textr_idx = engine_remap_texture_blocks(center_x, stl_y, texturing->texture_id[5]);
            do_a_trig_gourad_tr(&fec[0].cors[ecpos], &fec[1].cors[ecpos], &bec[1].cors[ecpos], textr_idx, -1);
            do_a_trig_gourad_bl(&bec[1].cors[ecpos], &bec[0].cors[ecpos], &fec[0].cors[ecpos], textr_idx, -1);
        }
        // Draw the universal ceiling on top of the columns
        TbBool edge_abyss = abyss && ((center_x == 1) || (center_x == kfx_sim_state.map_subtiles_x - 1) || (stl_y == 1) || (stl_y == kfx_sim_state.map_subtiles_y - 1));
        if (!edge_abyss) {
            ecpos = ENGINE_COL_CEILING_CORNER;
            textr_idx = floor_to_ceiling_map[colmn->floor_texture * !abyss];
            textr_idx = engine_remap_texture_blocks(center_x, stl_y, textr_idx);
            do_a_trig_gourad_tr(&fec[0].cors[ecpos], &fec[1].cors[ecpos], &bec[1].cors[ecpos], textr_idx, -1);
            do_a_trig_gourad_bl(&bec[1].cors[ecpos], &bec[0].cors[ecpos], &fec[0].cors[ecpos], textr_idx, -1);
        }
        bec++;
        fec++;
        center_x++;
        center_block_idx++;
    }
}

static void do_a_gpoly_gourad_tr(struct EngineCoord *ec1, struct EngineCoord *ec2, struct EngineCoord *ec3, int64_t textr_id, int64_t a5)
{
    if (near_clip_triangle(NCK_GOURAD_TR, ec1, ec2, ec3, textr_id, a5))
        return;
    int64_t z;
    struct BucketKindPolygonStandard *current_polygon_bucket;
    int64_t bucket_index;
    struct BucketKindPolygonStandard *polygon_bucket_ptr;
    struct BasicQ *previous_bucket_item;
    struct PolyPoint *polypoint1;
    struct PolyPoint *polypoint2;
    struct PolyPoint *polypoint3;
    int64_t ec1_fieldA;
    int64_t ec2_fieldA;
    int64_t ec3_fieldA;

    if ( (ec1->clip_flags & (int64_t)(ec2->clip_flags & ec3->clip_flags) & 0x1F8) == 0
        && (ec2->view_width - ec1->view_width) * (ec3->view_height - ec2->view_height)
        + (ec1->view_height - ec2->view_height) * (ec3->view_width - ec2->view_width) > 0 )
    {
        z = ec1->z;
        if ( z < ec2->z )
        z = ec2->z;
        if ( z < ec3->z )
        z = ec3->z;
        current_polygon_bucket = (struct BucketKindPolygonStandard *)getpoly;
        bucket_index = z / 16;
        if ( getpoly < poly_pool_end )
        {
            polygon_bucket_ptr = (struct BucketKindPolygonStandard *)getpoly;
            previous_bucket_item = buckets[bucket_index];
            getpoly += sizeof(struct BucketKindPolygonStandard);
            current_polygon_bucket->b.next = previous_bucket_item;
            polypoint1 = &current_polygon_bucket->vertex_first;
            current_polygon_bucket->b.kind = 0;
            buckets[bucket_index] = &current_polygon_bucket->b;
            current_polygon_bucket->block = textr_id;
            ec1_fieldA = ec1->shade_intensity;
            ec2_fieldA = ec2->shade_intensity;
            ec3_fieldA = ec3->shade_intensity;
            if ( a5 >= 0 )
            {
                ec1_fieldA = (4 * ec1_fieldA * (a5 + 0x4000)) >> 17;
                ec2_fieldA = (4 * ec2_fieldA * (a5 + 0x4000)) >> 17;
                ec3_fieldA = (4 * (a5 + 0x4000) * ec3_fieldA) >> 17;
            }
            polypoint1->X = ec1->view_width;
            polypoint1->Z = worldframe_depth_from_view_z(ec1->z);
            polypoint1->Y = ec1->view_height;
            polypoint1->U = texture_scroll.x.val;
            polypoint1->V = texture_scroll.y.val;
            polypoint1->S = ec1_fieldA << 8;
            polypoint2 = &polygon_bucket_ptr->vertex_second;
            polygon_bucket_ptr->vertex_second.X = ec2->view_width;
            polygon_bucket_ptr->vertex_second.Z = worldframe_depth_from_view_z(ec2->z);
            polypoint3 = &polygon_bucket_ptr->vertex_third;
            polypoint2->Y = ec2->view_height;
            polypoint2->U = 0x1FFFFF + texture_scroll.x.val;
            polypoint2->V = texture_scroll.y.val;
            polypoint2->S = ec2_fieldA << 8;
            polypoint3->X = ec3->view_width;
            polypoint3->Z = worldframe_depth_from_view_z(ec3->z);
            polypoint3->Y = ec3->view_height;
            polypoint3->U = 0x1FFFFF + texture_scroll.x.val;
            polypoint3->V = 0x1FFFFF + texture_scroll.y.val;
            polypoint3->S = ec3_fieldA << 8;
        }
    }
}

static void do_a_gpoly_unlit_tr(struct EngineCoord *ec1, struct EngineCoord *ec2, struct EngineCoord *ec3, int64_t textr_id)
{
    if (near_clip_triangle(NCK_UNLIT_TR, ec1, ec2, ec3, textr_id, 0))
        return;
    int64_t z;
    struct BucketKindPolygonStandard *current_polygon_bucket;
    int64_t bucket_index;
    struct BucketKindPolygonStandard *polygon_bucket;
    struct BasicQ *previous_bucket_item;

    if ( (ec1->clip_flags & (int64_t)(ec2->clip_flags & ec3->clip_flags) & 0x1F8) == 0
        && (ec3->view_width - ec2->view_width) * (ec1->view_height - ec2->view_height)
        + (ec3->view_height - ec2->view_height) * (ec2->view_width - ec1->view_width) > 0 )
    {
        z = ec1->z;
        if ( z < ec2->z )
        z = ec2->z;
        if ( z < ec3->z )
        z = ec3->z;
        current_polygon_bucket = (struct BucketKindPolygonStandard *)getpoly;
        bucket_index = z / 16;
        if ( getpoly < poly_pool_end )
        {
            polygon_bucket = (struct BucketKindPolygonStandard *)getpoly;
            previous_bucket_item = buckets[bucket_index];
            getpoly += sizeof(struct BucketKindPolygonStandard);
            current_polygon_bucket->b.next = previous_bucket_item;
            current_polygon_bucket->b.kind = 0;
            buckets[bucket_index] = &current_polygon_bucket->b;
            current_polygon_bucket->block = textr_id;
            current_polygon_bucket->vertex_first.X = ec1->view_width;
            current_polygon_bucket->vertex_first.Z = worldframe_depth_from_view_z(ec1->z);
            current_polygon_bucket->vertex_first.Y = ec1->view_height;
            current_polygon_bucket->vertex_first.U = 0;
            current_polygon_bucket->vertex_first.V = 0;
            current_polygon_bucket->vertex_first.S = (ec1->shade_intensity + 3072) << 8;
            current_polygon_bucket->vertex_second.X = ec2->view_width;
            current_polygon_bucket->vertex_second.Z = worldframe_depth_from_view_z(ec2->z);
            current_polygon_bucket->vertex_second.Y = ec2->view_height;
            current_polygon_bucket->vertex_second.U = 0x1FFFFF;
            current_polygon_bucket->vertex_second.V = 0;
            current_polygon_bucket->vertex_second.S = (ec2->shade_intensity + 3072) << 8;
            polygon_bucket->vertex_third.X = ec3->view_width;
            polygon_bucket->vertex_third.Z = worldframe_depth_from_view_z(ec3->z);
            polygon_bucket->vertex_third.Y = ec3->view_height;
            polygon_bucket->vertex_third.U = 0x1FFFFF;
            polygon_bucket->vertex_third.V = 0x1FFFFF;
            polygon_bucket->vertex_third.S = (ec3->shade_intensity + 3072) << 8;
        }
    }
}

static void do_a_gpoly_unlit_bl(struct EngineCoord *ec1, struct EngineCoord *ec2, struct EngineCoord *ec3, int64_t textr_id)
{
    if (near_clip_triangle(NCK_UNLIT_BL, ec1, ec2, ec3, textr_id, 0))
        return;
    int64_t z;
    struct BucketKindPolygonStandard *current_polygon_bucket;
    int64_t bucket_index;
    struct BasicQ *next_bucket_item;

    if ( (ec1->clip_flags & (int64_t)(ec2->clip_flags & ec3->clip_flags) & 0x1F8) == 0
        && (ec3->view_width - ec2->view_width) * (ec1->view_height - ec2->view_height)
        + (ec3->view_height - ec2->view_height) * (ec2->view_width - ec1->view_width) > 0 )
    {
        z = ec1->z;
        if ( z < ec2->z )
        z = ec2->z;
        if ( z < ec3->z )
        z = ec3->z;
        current_polygon_bucket = (struct BucketKindPolygonStandard *)getpoly;
        bucket_index = z / 16;
        if ( getpoly < poly_pool_end )
        {
        next_bucket_item = buckets[bucket_index];
        getpoly += sizeof(struct BucketKindPolygonStandard);
        current_polygon_bucket->b.next = next_bucket_item;
        current_polygon_bucket->b.kind = 0;
        buckets[bucket_index] = &current_polygon_bucket->b;
        current_polygon_bucket->block = textr_id;
        current_polygon_bucket->vertex_first.X = ec1->view_width;
        current_polygon_bucket->vertex_first.Z = worldframe_depth_from_view_z(ec1->z);
        current_polygon_bucket->vertex_first.Y = ec1->view_height;
        current_polygon_bucket->vertex_first.U = 0x1FFFFF;
        current_polygon_bucket->vertex_first.V = 0x1FFFFF;
        current_polygon_bucket->vertex_first.S = (ec1->shade_intensity + 3072) << 8;
        current_polygon_bucket->vertex_second.X = ec2->view_width;
        current_polygon_bucket->vertex_second.Z = worldframe_depth_from_view_z(ec2->z);
        current_polygon_bucket->vertex_second.Y = ec2->view_height;
        current_polygon_bucket->vertex_second.U = 0;
        current_polygon_bucket->vertex_second.V = 0x1FFFFF;
        current_polygon_bucket->vertex_second.S = (ec2->shade_intensity + 3072) << 8;
        current_polygon_bucket->vertex_third.X = ec3->view_width;
        current_polygon_bucket->vertex_third.Z = worldframe_depth_from_view_z(ec3->z);
        current_polygon_bucket->vertex_third.Y = ec3->view_height;
        current_polygon_bucket->vertex_third.U = 0;
        current_polygon_bucket->vertex_third.V = 0;
        current_polygon_bucket->vertex_third.S = (ec3->shade_intensity + 3072) << 8;
        }
    }
}

static void do_a_gpoly_gourad_bl(struct EngineCoord *ec1, struct EngineCoord *ec2, struct EngineCoord *ec3, int64_t textr_id, int64_t a5)
{
    if (near_clip_triangle(NCK_GOURAD_BL, ec1, ec2, ec3, textr_id, a5))
        return;
    int64_t z;
    struct BucketKindPolygonStandard *current_polygon_bucket;
    int64_t zdiv16;
    struct BucketKindPolygonStandard *poly_ptr;
    struct BasicQ *previous_bucket_item;
    int64_t ec1_fieldA;
    int64_t ec2_fieldA;
    int64_t ec3_fieldA;
    struct PolyPoint *polypoint2;
    struct PolyPoint *polypoint3;
    struct PolyPoint *polypoint1;

    if ( (ec1->clip_flags & (int64_t)(ec2->clip_flags & ec3->clip_flags) & 0x1F8) == 0
        && (ec3->view_height - ec2->view_height) * (ec2->view_width - ec1->view_width)
        + (ec1->view_height - ec2->view_height) * (ec3->view_width - ec2->view_width) > 0 )
    {
        z = ec1->z;
        if ( z < ec2->z )
        z = ec2->z;
        if ( z < ec3->z )
        z = ec3->z;
        current_polygon_bucket = (struct BucketKindPolygonStandard *)getpoly;
        zdiv16 = z / 16;
        if ( getpoly < poly_pool_end )
        {
            poly_ptr = (struct BucketKindPolygonStandard *)getpoly;
            previous_bucket_item = buckets[zdiv16];
            getpoly += sizeof(struct BucketKindPolygonStandard);
            current_polygon_bucket->b.next = previous_bucket_item;
            polypoint1 = &current_polygon_bucket->vertex_first;
            current_polygon_bucket->b.kind = 0;
            buckets[zdiv16] = &current_polygon_bucket->b;
            current_polygon_bucket->block = textr_id;
            ec1_fieldA = ec1->shade_intensity;
            ec2_fieldA = ec2->shade_intensity;
            ec3_fieldA = ec3->shade_intensity;
            if ( a5 >= 0 )
            {
                ec1_fieldA = (4 * (a5 + 0x4000) * ec1_fieldA) >> 17;
                ec2_fieldA = (4 * (a5 + 0x4000) * ec2_fieldA) >> 17;
                ec3_fieldA = (4 * (a5 + 0x4000) * ec3_fieldA) >> 17;
            }
            polypoint1->X = ec1->view_width;
            polypoint1->Z = worldframe_depth_from_view_z(ec1->z);
            polypoint2 = &poly_ptr->vertex_second;
            polypoint1->Y = ec1->view_height;
            polypoint1->U = 0x1FFFFF + texture_scroll.x.val;
            polypoint1->V = 0x1FFFFF + texture_scroll.y.val;
            polypoint1->S = ec1_fieldA << 8;
            poly_ptr->vertex_second.X = ec2->view_width;
            poly_ptr->vertex_second.Z = worldframe_depth_from_view_z(ec2->z);
            polypoint3 = &poly_ptr->vertex_third;
            polypoint2->Y = ec2->view_height;
            polypoint2->U = texture_scroll.x.val;
            polypoint2->V = 0x1FFFFF + texture_scroll.y.val;
            polypoint2->S = ec2_fieldA << 8;
            polypoint3->X = ec3->view_width;
            polypoint3->Z = worldframe_depth_from_view_z(ec3->z);
            polypoint3->Y = ec3->view_height;
            polypoint3->U = texture_scroll.x.val;
            polypoint3->V = texture_scroll.y.val;
            polypoint3->S = ec3_fieldA << 8;
        }
    }
}

static void draw_abyss(const struct Column *colmn, const struct Map *mapblk, struct EngineCol *bec, struct EngineCol *fec, MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{
    colmn = get_abyss_wall_column(colmn, mapblk, stl_x, stl_y);
    if (cube_is_abyss(kfx_sim_state.top_cube[colmn->floor_texture])) {
        return;
    }
    int64_t side;
    int64_t depth;
    int64_t top;
    int64_t cube = get_column_top_cube(colmn);
    struct EngineCol *edges[] = {&fec[1], &fec[0], &bec[0], &bec[1], &fec[1]};
    const int64_t shades[] = {normal_shade_front, normal_shade_left, normal_shade_back, normal_shade_right};
    for (side = 0; side < 4; side++) {
        MapSubtlCoord adjacent_x = stl_x + x_step1[side];
        MapSubtlCoord adjacent_y = stl_y + y_step1[side];
        struct Map *adjacent_map = get_map_block_at(adjacent_x, adjacent_y);
        if (!map_block_revealed(adjacent_map, my_player_number) || !map_block_has_rendered_abyss(adjacent_map, adjacent_x, adjacent_y)) {
            continue;
        }
        int64_t textr_idx = engine_remap_abyss_wall_texture_blocks(stl_x, stl_y, cube, (side + 2) & 3);
        for (depth = COLUMN_STACK_HEIGHT + 2, top = COLUMN_STACK_HEIGHT + 1; depth <= COLUMN_STACK_HEIGHT + ABYSS_WALL_RENDER_HEIGHT + 2; top = depth++) {
            if (lens_mode != 0) {
                do_a_trig_gourad_tr(&edges[side + 1]->cors[top], &edges[side]->cors[top], &edges[side]->cors[depth], textr_idx, shades[side]);
                do_a_trig_gourad_bl(&edges[side]->cors[depth], &edges[side + 1]->cors[depth], &edges[side + 1]->cors[top], textr_idx, shades[side]);
            } else {
                do_a_gpoly_gourad_tr(&edges[side + 1]->cors[top], &edges[side]->cors[top], &edges[side]->cors[depth], textr_idx, shades[side]);
                do_a_gpoly_gourad_bl(&edges[side]->cors[depth], &edges[side + 1]->cors[depth], &edges[side + 1]->cors[top], textr_idx, shades[side]);
            }
        }
    }
}

static void do_a_plane_of_engine_columns_cluedo(int64_t stl_x, int64_t stl_y, int64_t plane_start, int64_t plane_end)
{
    if ((stl_y < 1) || (stl_y > (kfx_sim_state.map_subtiles_y - 1))) {
        return;
    }
    int64_t xaval;
    int64_t xbval;
    xaval = plane_start;
    if (stl_x + plane_start < 1) {
        xaval = 1 - stl_x;
    }
    xbval = plane_end;
    if (stl_x + plane_end > kfx_sim_state.map_subtiles_x) {
        xbval = kfx_sim_state.map_subtiles_x - stl_x;
    }
    int64_t xidx;
    int64_t xdelta;
    xdelta = xbval - xaval;
    const struct Column *unrev_colmn;
    unrev_colmn = get_column(kfx_sim_state.unrevealed_column_idx);
    for (xidx=0; xidx < xdelta; xidx++)
    {
        struct Map *cur_mapblk;
        cur_mapblk = get_map_block_at(stl_x + xaval + xidx, stl_y);
        unsigned char render_map_flags = get_local_dig_prediction_render_flags(stl_x + xaval + xidx, stl_y, cur_mapblk->flags);
        // Get solidmasks of sibling columns
        int64_t solidmsk_cur_raw;
        int64_t solidmsk_cur;
        int64_t solidmsk_back;
        int64_t solidmsk_front;
        int64_t solidmsk_left;
        int64_t solidmsk_right;
        solidmsk_cur_raw = unrev_colmn->solidmask;
        solidmsk_cur = unrev_colmn->solidmask & 3;
        solidmsk_back = unrev_colmn->solidmask & 3;
        solidmsk_right = unrev_colmn->solidmask & 3;
        solidmsk_front = unrev_colmn->solidmask & 3;
        solidmsk_left = unrev_colmn->solidmask & 3;
        // Get column to be drawn
        const struct Column *cur_colmn;
        cur_colmn = unrev_colmn;
        if (map_block_revealed(cur_mapblk, my_player_number))
        {
            int64_t i;
            i = get_mapwho_thing_index(cur_mapblk);
            if (i > 0) {
              do_map_who(i);
            }
            cur_colmn = get_map_column(cur_mapblk);
            solidmsk_cur_raw = cur_colmn->solidmask;
            solidmsk_cur = solidmsk_cur_raw;
            if (solidmsk_cur >= (1<<3))
            {
                if (((cur_mapblk->flags & (SlbAtFlg_IsDoor|SlbAtFlg_IsRoom)) == 0) && ((cur_colmn->bitfields & 0xE) == 0)) {
                    solidmsk_cur &= 3;
                }
            }
        }
        struct Map *sib_mapblk;
        sib_mapblk = get_map_block_at(stl_x + xaval + xidx, stl_y - 1);
        if (map_block_revealed(sib_mapblk, my_player_number)) {
            struct Column *colmn;
            colmn = get_map_column(sib_mapblk);
            solidmsk_back = colmn->solidmask;
            if (solidmsk_back >= (1<<3))
            {
                if (((sib_mapblk->flags & (SlbAtFlg_IsDoor|SlbAtFlg_IsRoom)) == 0) && ((colmn->bitfields & 0xE) == 0)) {
                    solidmsk_back &= 3;
                }
            }
        }
        sib_mapblk = get_map_block_at(stl_x + xaval + xidx, stl_y + 1);
        if (map_block_revealed(sib_mapblk, my_player_number)) {
            struct Column *colmn;
            colmn = get_map_column(sib_mapblk);
            solidmsk_front = colmn->solidmask;
            if (solidmsk_front >= (1<<3))
            {
                if (((sib_mapblk->flags & (SlbAtFlg_IsDoor|SlbAtFlg_IsRoom)) == 0) && ((colmn->bitfields & 0xE) == 0)) {
                    solidmsk_front &= 3;
                }
            }
        }
        sib_mapblk = get_map_block_at(stl_x + xaval + xidx - 1, stl_y);
        if (map_block_revealed(sib_mapblk, my_player_number)) {
            struct Column *colmn;
            colmn = get_map_column(sib_mapblk);
            solidmsk_left = colmn->solidmask;
            if (solidmsk_left >= (1<<3))
            {
                if (((sib_mapblk->flags & (SlbAtFlg_IsDoor|SlbAtFlg_IsRoom)) == 0) && ((colmn->bitfields & 0xE) == 0)) {
                    solidmsk_left &= 3;
                }
            }
        }
        sib_mapblk = get_map_block_at(stl_x + xaval + xidx + 1, stl_y);
        if (map_block_revealed(sib_mapblk, my_player_number)) {
            struct Column *colmn;
            colmn = get_map_column(sib_mapblk);
            solidmsk_right = colmn->solidmask;
            if (solidmsk_right >= (1<<3))
            {
                if (((sib_mapblk->flags & (SlbAtFlg_IsDoor|SlbAtFlg_IsRoom)) == 0) && ((colmn->bitfields & 0xE) == 0)) {
                    solidmsk_right &= 3;
                }
            }
        }

        struct EngineCol *bec;
        struct EngineCol *fec;
        bec = &back_ec[xaval + MINMAX_ALMOST_HALF + xidx];
        fec = &front_ec[xaval + MINMAX_ALMOST_HALF + xidx];
        int64_t mask;
        int64_t ncor;
        for (mask=1,ncor=0; mask <= solidmsk_cur; mask*=2,ncor++)
        {
            int64_t textr_id;
            struct CubeConfigStats *cubed;
            cubed = get_cube_model_stats(cur_colmn->cubes[ncor]);
            if ((mask & solidmsk_cur) == 0)
            {
                continue;
            }
            if ((mask & solidmsk_back) == 0)
            {
                textr_id = engine_remap_texture_blocks(stl_x + xaval + xidx, stl_y, cubed->texture_id[sideoris[0].back_texture_index]);
                do_a_gpoly_gourad_tr(&bec[1].cors[ncor+1], &bec[0].cors[ncor+1], &bec[0].cors[ncor],   textr_id, normal_shade_back);
                do_a_gpoly_gourad_bl(&bec[0].cors[ncor],   &bec[1].cors[ncor],   &bec[1].cors[ncor+1], textr_id, normal_shade_back);
            }
            if ((solidmsk_front & mask) == 0)
            {
                textr_id = engine_remap_texture_blocks(stl_x + xaval + xidx, stl_y, cubed->texture_id[sideoris[0].front_texture_index]);
                do_a_gpoly_gourad_tr(&fec[0].cors[ncor+1], &fec[1].cors[ncor+1], &fec[1].cors[ncor],   textr_id, normal_shade_front);
                do_a_gpoly_gourad_bl(&fec[1].cors[ncor],   &fec[0].cors[ncor],   &fec[0].cors[ncor+1], textr_id, normal_shade_front);
            }
            if ((solidmsk_left & mask) == 0)
            {
                textr_id = engine_remap_texture_blocks(stl_x + xaval + xidx, stl_y, cubed->texture_id[sideoris[0].bottom_texture_index]);
                do_a_gpoly_gourad_tr(&bec[0].cors[ncor+1], &fec[0].cors[ncor+1], &fec[0].cors[ncor],   textr_id, normal_shade_left);
                do_a_gpoly_gourad_bl(&fec[0].cors[ncor],   &bec[0].cors[ncor],   &bec[0].cors[ncor+1], textr_id, normal_shade_left);
            }
            if ((solidmsk_right & mask) == 0)
            {
                textr_id = engine_remap_texture_blocks(stl_x + xaval + xidx, stl_y, cubed->texture_id[sideoris[0].top_texture_index]);
                do_a_gpoly_gourad_tr(&fec[1].cors[ncor+1], &bec[1].cors[ncor+1], &bec[1].cors[ncor],   textr_id, normal_shade_right);
                do_a_gpoly_gourad_bl(&bec[1].cors[ncor],   &fec[1].cors[ncor],   &fec[1].cors[ncor+1], textr_id, normal_shade_right);
            }
        }
        draw_abyss(cur_colmn, cur_mapblk, bec, fec, stl_x + xaval + xidx, stl_y);

        ncor = floor_height_table[solidmsk_cur];
        if ((ncor > 0) && (ncor <= COLUMN_STACK_HEIGHT))
        {
            int64_t ncor_raw;
            ncor_raw = floor_height_table[solidmsk_cur_raw];
            if ( (render_map_flags & SlbAtFlg_Unexplored) != 0 )
            {
                int64_t textr_id = engine_remap_texture_blocks(stl_x + xaval + xidx, stl_y, TEXTURE_LAND_MARKED_LAND);
                do_a_gpoly_unlit_tr(&bec[0].cors[ncor], &bec[1].cors[ncor], &fec[1].cors[ncor], textr_id);
                do_a_gpoly_unlit_bl(&fec[1].cors[ncor], &fec[0].cors[ncor], &bec[0].cors[ncor], textr_id);
            } else
            if ((render_map_flags & SlbAtFlg_TaggedValuable) != 0)
            {
                int64_t textr_id = engine_remap_texture_blocks(stl_x + xaval + xidx, stl_y, TEXTURE_LAND_MARKED_GOLD);
                do_a_gpoly_unlit_tr(&bec[0].cors[ncor], &bec[1].cors[ncor], &fec[1].cors[ncor], textr_id);
                do_a_gpoly_unlit_bl(&fec[1].cors[ncor], &fec[0].cors[ncor], &bec[0].cors[ncor], textr_id);
            } else
             {
                if ((ncor_raw > 0) && (ncor_raw <= COLUMN_STACK_HEIGHT))
                {
                    struct CubeConfigStats * cubed = get_cube_model_stats(cur_colmn->cubes[ncor_raw-1]);
                    int64_t textr_id = engine_remap_top_texture_blocks(stl_x + xaval + xidx, stl_y, cubed->texture_id[4]);
                    // Top surface in cluedo mode
                    do_a_gpoly_gourad_tr(&bec[0].cors[ncor], &bec[1].cors[ncor], &fec[1].cors[ncor], textr_id, -1);
                    do_a_gpoly_gourad_bl(&fec[1].cors[ncor], &fec[0].cors[ncor], &bec[0].cors[ncor], textr_id, -1);
                }
            }
        } else if (!cube_is_abyss(kfx_sim_state.top_cube[cur_colmn->floor_texture])) {
            if ((render_map_flags & SlbAtFlg_Unexplored) == 0) {
                int64_t textr_id = engine_remap_top_texture_blocks(stl_x + xaval + xidx, stl_y, cur_colmn->floor_texture);
                do_a_gpoly_gourad_tr(&bec[0].cors[0], &bec[1].cors[0], &fec[1].cors[0], textr_id, -1);
                do_a_gpoly_gourad_bl(&fec[1].cors[0], &fec[0].cors[0], &bec[0].cors[0], textr_id, -1);
            } else {
                int64_t textr_id = engine_remap_texture_blocks(stl_x + xaval + xidx, stl_y, TEXTURE_LAND_MARKED_LAND);
                do_a_gpoly_unlit_tr(&bec[0].cors[0], &bec[1].cors[0], &fec[1].cors[0], textr_id);
                do_a_gpoly_unlit_bl(&fec[1].cors[0], &fec[0].cors[0], &bec[0].cors[0], textr_id);
            }
        }
        ncor = lintel_top_height[solidmsk_cur];
        if ((ncor > 0) && (ncor <= COLUMN_STACK_HEIGHT))
        {
            struct CubeConfigStats * cubed;
            cubed = get_cube_model_stats(cur_colmn->cubes[ncor-1]);
            int64_t textr_id = engine_remap_top_texture_blocks(stl_x + xaval + xidx, stl_y, cubed->texture_id[4]);
            do_a_gpoly_gourad_tr(&bec[0].cors[ncor], &bec[1].cors[ncor], &fec[1].cors[ncor], textr_id, -1);
            do_a_gpoly_gourad_bl(&fec[1].cors[ncor], &fec[0].cors[ncor], &bec[0].cors[ncor], textr_id, -1);
        }
    }
}

static void do_a_plane_of_engine_columns_isometric(int64_t stl_x, int64_t stl_y, int64_t plane_start, int64_t plane_end)
{
    if ((stl_y < 1) || (stl_y > kfx_sim_state.map_subtiles_y - 1)) {
        return;
    }

    int64_t xaval;
    int64_t xbval;
    TbBool xaclip;
    TbBool xbclip;
    xaval = plane_start;
    xaclip = 0;
    xbclip = 0;
    if (stl_x + plane_start <= 1) {
        xaclip = 1;
        xaval = 1 - stl_x;
    }
    xbval = plane_end;
    if (stl_x + plane_end >= kfx_sim_state.map_subtiles_x) {
        xbclip = 1;
        xbval = kfx_sim_state.map_subtiles_x - stl_x;
    }
    int64_t xidx;
    int64_t xdelta;
    xdelta = xbval - xaval;
    const struct Column *unrev_colmn;
    unrev_colmn = get_column(kfx_sim_state.unrevealed_column_idx);
    for (xidx=0; xidx < xdelta; xidx++)
    {
        struct Map *cur_mapblk;
        cur_mapblk = get_map_block_at(stl_x + xaval + xidx, stl_y);
        unsigned char render_map_flags = get_local_dig_prediction_render_flags(stl_x + xaval + xidx, stl_y, cur_mapblk->flags);
        // Get column to be drawn
        const struct Column *cur_colmn;
        cur_colmn = unrev_colmn;
        if (map_block_revealed(cur_mapblk, my_player_number))
        {
            int64_t i;
            i = get_mapwho_thing_index(cur_mapblk);
            if (i > 0) {
              do_map_who(i);
            }
            cur_colmn = get_map_column(cur_mapblk);
        }
        // Get solidmasks of sibling columns
        int64_t solidmsk_cur;
        int64_t solidmsk_back;
        int64_t solidmsk_front;
        int64_t solidmsk_left;
        int64_t solidmsk_right;
        solidmsk_cur = cur_colmn->solidmask;
        solidmsk_back = unrev_colmn->solidmask;
        solidmsk_right = unrev_colmn->solidmask;
        solidmsk_front = unrev_colmn->solidmask;
        solidmsk_left = unrev_colmn->solidmask;
        struct Map *sib_mapblk;
        sib_mapblk = get_map_block_at(stl_x + xaval + xidx, stl_y - 1);
        if (map_block_revealed(sib_mapblk, my_player_number)) {
            struct Column *colmn;
            colmn = get_map_column(sib_mapblk);
            solidmsk_back = colmn->solidmask;
        }
        sib_mapblk = get_map_block_at(stl_x + xaval + xidx, stl_y + 1);
        if (map_block_revealed(sib_mapblk, my_player_number)) {
            struct Column *colmn;
            colmn = get_map_column(sib_mapblk);
            solidmsk_front = colmn->solidmask;
        }
        sib_mapblk = get_map_block_at(stl_x + xaval + xidx - 1, stl_y);
        if (map_block_revealed(sib_mapblk, my_player_number)) {
            struct Column *colmn;
            colmn = get_map_column(sib_mapblk);
            solidmsk_left = colmn->solidmask;
        }
        sib_mapblk = get_map_block_at(stl_x + xaval + xidx + 1, stl_y);
        if (map_block_revealed(sib_mapblk, my_player_number)) {
            struct Column *colmn;
            colmn = get_map_column(sib_mapblk);
            solidmsk_right = colmn->solidmask;
        }
        if ( xaclip || xbclip || (stl_y <= 1) || (stl_y >= kfx_sim_state.map_subtiles_y - 1))
        {
            if (xaclip && (xidx == 0)) {
                solidmsk_left = 0;
            }
            if (xbclip && (xdelta - xidx == 1)) {
                solidmsk_right = 0;
            }
            if (stl_y <= 1) {
                solidmsk_back = 0;
            }
            if (stl_y >= kfx_sim_state.map_subtiles_y - 1) {
                solidmsk_front = 0;
            }
        }

        struct EngineCol *bec;
        struct EngineCol *fec;
        bec = &back_ec[xaval + MINMAX_ALMOST_HALF + xidx];
        fec = &front_ec[xaval + MINMAX_ALMOST_HALF + xidx];
        int64_t mask;
        int64_t ncor;
        for (mask=1,ncor=0; mask <= solidmsk_cur; mask*=2,ncor++)
        {
            int64_t textr_id;
            struct CubeConfigStats *cubed;
            cubed = get_cube_model_stats(cur_colmn->cubes[ncor]);
            if ((mask & solidmsk_cur) == 0)
            {
                continue;
            }
            if ((mask & solidmsk_back) == 0)
            {
                textr_id = engine_remap_texture_blocks(stl_x + xaval + xidx, stl_y, cubed->texture_id[sideoris[0].back_texture_index]);
                do_a_gpoly_gourad_tr(&bec[1].cors[ncor+1], &bec[0].cors[ncor+1], &bec[0].cors[ncor],   textr_id, normal_shade_back);
                do_a_gpoly_gourad_bl(&bec[0].cors[ncor],   &bec[1].cors[ncor],   &bec[1].cors[ncor+1], textr_id, normal_shade_back);
            }
            if ((solidmsk_front & mask) == 0)
            {
                textr_id = engine_remap_texture_blocks(stl_x + xaval + xidx, stl_y, cubed->texture_id[sideoris[0].front_texture_index]);
                do_a_gpoly_gourad_tr(&fec[0].cors[ncor+1], &fec[1].cors[ncor+1], &fec[1].cors[ncor],   textr_id, normal_shade_front);
                do_a_gpoly_gourad_bl(&fec[1].cors[ncor],   &fec[0].cors[ncor],   &fec[0].cors[ncor+1], textr_id, normal_shade_front);
            }
            if ((solidmsk_left & mask) == 0)
            {
                textr_id = engine_remap_texture_blocks(stl_x + xaval + xidx, stl_y, cubed->texture_id[sideoris[0].bottom_texture_index]);
                do_a_gpoly_gourad_tr(&bec[0].cors[ncor+1], &fec[0].cors[ncor+1], &fec[0].cors[ncor],   textr_id, normal_shade_left);
                do_a_gpoly_gourad_bl(&fec[0].cors[ncor],   &bec[0].cors[ncor],   &bec[0].cors[ncor+1], textr_id, normal_shade_left);
            }
            if ((solidmsk_right & mask) == 0)
            {
                textr_id = engine_remap_texture_blocks(stl_x + xaval + xidx, stl_y, cubed->texture_id[sideoris[0].top_texture_index]);
                do_a_gpoly_gourad_tr(&fec[1].cors[ncor+1], &bec[1].cors[ncor+1], &bec[1].cors[ncor],   textr_id, normal_shade_right);
                do_a_gpoly_gourad_bl(&bec[1].cors[ncor],   &fec[1].cors[ncor],   &fec[1].cors[ncor+1], textr_id, normal_shade_right);
            }
        }
        draw_abyss(cur_colmn, cur_mapblk, bec, fec, stl_x + xaval + xidx, stl_y);

        ncor = floor_height_table[solidmsk_cur];
        if (ncor > 0)
        {
            if (render_map_flags & SlbAtFlg_Unexplored)
            {
                int64_t textr_id = engine_remap_texture_blocks(stl_x + xaval + xidx, stl_y, TEXTURE_LAND_MARKED_LAND);
                do_a_gpoly_unlit_tr(&bec[0].cors[ncor], &bec[1].cors[ncor], &fec[1].cors[ncor], textr_id);
                do_a_gpoly_unlit_bl(&fec[1].cors[ncor], &fec[0].cors[ncor], &bec[0].cors[ncor], textr_id);
            }
            else if ((render_map_flags & (SlbAtFlg_TaggedValuable|SlbAtFlg_Unexplored)) == 0)
            {
                struct CubeConfigStats * cubed;
                cubed = get_cube_model_stats(cur_colmn->cubes[ncor - 1]);
                int64_t textr_id = engine_remap_top_texture_blocks(stl_x + xaval + xidx, stl_y, cubed->texture_id[4]);
                // Top surface on full iso mode
                do_a_gpoly_gourad_tr(&bec[0].cors[ncor], &bec[1].cors[ncor], &fec[1].cors[ncor], textr_id, -1);
                do_a_gpoly_gourad_bl(&fec[1].cors[ncor], &fec[0].cors[ncor], &bec[0].cors[ncor], textr_id, -1);
            } else
            if ((render_map_flags & SlbAtFlg_Valuable) != 0)
            {
                int64_t textr_id = engine_remap_texture_blocks(stl_x + xaval + xidx, stl_y, TEXTURE_LAND_MARKED_GOLD);
                do_a_gpoly_unlit_tr(&bec[0].cors[ncor], &bec[1].cors[ncor], &fec[1].cors[ncor], textr_id);
                do_a_gpoly_unlit_bl(&fec[1].cors[ncor], &fec[0].cors[ncor], &bec[0].cors[ncor], textr_id);
            }
        } else if (!cube_is_abyss(kfx_sim_state.top_cube[cur_colmn->floor_texture])) {
            if ((render_map_flags & SlbAtFlg_Unexplored) == 0) {
                int64_t textr_id = engine_remap_top_texture_blocks(stl_x + xaval + xidx, stl_y, cur_colmn->floor_texture);
                do_a_gpoly_gourad_tr(&bec[0].cors[0], &bec[1].cors[0], &fec[1].cors[0], textr_id, -1);
                do_a_gpoly_gourad_bl(&fec[1].cors[0], &fec[0].cors[0], &bec[0].cors[0], textr_id, -1);
            } else {
                int64_t textr_id = engine_remap_texture_blocks(stl_x + xaval + xidx, stl_y, TEXTURE_LAND_MARKED_LAND);
                do_a_gpoly_unlit_tr(&bec[0].cors[0], &bec[1].cors[0], &fec[1].cors[0], textr_id);
                do_a_gpoly_unlit_bl(&fec[1].cors[0], &fec[0].cors[0], &bec[0].cors[0], textr_id);
            }
        }
        ncor = lintel_top_height[solidmsk_cur];
        if (ncor > 0)
        {
            struct CubeConfigStats * cubed;
            cubed = get_cube_model_stats(cur_colmn->cubes[ncor - 1]);
            int64_t textr_id = engine_remap_top_texture_blocks(stl_x + xaval + xidx, stl_y, cubed->texture_id[4]);
            do_a_gpoly_gourad_tr(&bec[0].cors[ncor], &bec[1].cors[ncor], &fec[1].cors[ncor], textr_id, -1);
            do_a_gpoly_gourad_bl(&fec[1].cors[ncor], &fec[0].cors[ncor], &bec[0].cors[ncor], textr_id, -1);
        }
    }
}

void draw_map_volume_box(int64_t cor1_x, int64_t cor1_y, int64_t cor2_x, int64_t cor2_y, int64_t floor_height_z, unsigned char color)
{
    map_volume_box.visible = 1;
    map_volume_box.beg_x = cor1_x & ((int64_t)(int32_t)0xFFFFFF00);
    map_volume_box.beg_y = cor1_y & 0xFFFF00;
    map_volume_box.end_x = cor2_x & ((int64_t)(int32_t)0xFFFFFF00);
    map_volume_box.end_y = cor2_y & ((int64_t)(int32_t)0xFFFFFF00);
    map_volume_box.floor_height_z = floor_height_z;
    map_volume_box.color = color;
}

// docs/refactor/editor/04-views-camera-overlays.md -- same camera-relative
// offset + rotpers() recipe do_map_who_for_thing() already uses to place a
// thing's sprite on screen (map_x_pos/map_y_pos/map_z_pos and
// camera_matrix are this file's own private per-frame render state, hence
// this wrapper living here rather than exposing them directly).
TbBool project_world_position_to_screen(MapCoord x, MapCoord y, MapCoord z, int64_t *screen_x, int64_t *screen_y)
{
    struct EngineCoord ecor;
    ecor.clip_flags = 0;
    ecor.x = (x - map_x_pos);
    ecor.z = (map_y_pos - y);
    ecor.y = (z - map_z_pos);
    rotpers(&ecor, &camera_matrix);
    if (ecor.clip_flags != 0)
        return false;
    *screen_x = ecor.view_width;
    *screen_y = ecor.view_height;
    return true;
}

/**
 * Some objects have a secondary sprite drawn on top, that is positioned relative to the original sprite and scaled differently.
 * @param jspr the base sprite
 * @param angle the camera angle at which sprite is shown
 * @param base_sprite_size the size of the sprite on the screen after camera zoom
 * @note Renders both the primary and secondary sprite. So both the torch and the flame.
  */
static void process_keeper_flame_on_sprite(struct BucketKindJontySprite* jspr, int64_t angle, int64_t base_sprite_size)
{
    struct PlayerInfo* player = get_my_player();
    struct Thing* thing = jspr->thing;
    struct ObjectConfigStats* objst;
    struct TrapConfigStats* trapst;
    struct FlameProperties flame;
    int64_t animation_sprite;
    unsigned char current_frame;
    uint64_t nframe;
    int64_t add_x, add_y;
    int64_t scale = 0;
    if (thing_is_object(thing))
    {
        objst = get_object_model_stats(thing->model);
        flame = objst->flame;
    } else
    if (thing_is_deployed_trap(thing))
    {
        trapst = get_trap_model_stats(thing->model);
        flame = trapst->flame;
    }
    else
    {
        ERRORLOG("Thing %s is neither an object nor a flame.", thing_model_name(thing));
        return;
    }
    if (thing->sprite_size != 0)
    {
        scale = (flame.sprite_size * base_sprite_size / thing->sprite_size);
    }

    if (get_local_view_type(player) == PVT_DungeonTop)
    {
        add_x = (base_sprite_size * flame.td_add_x) >> 5;
        add_y = (base_sprite_size * flame.td_add_y) >> 5;
    }
    else
    {
        add_x = (base_sprite_size * flame.fp_add_x) >> 5;
        add_y = (base_sprite_size * flame.fp_add_y) >> 5;
    }

    //Object/Trap itself
    RendererClearDrawFlags(TRF_Transpar_Flags);
    EngineSpriteDrawUsingAlpha = 0;
    if (flag_is_set(thing->rendering_flags,TRF_Transpar_8))
        RendererAddDrawFlags(Lb_SPRITE_TRANSPAR8);
    if (flag_is_set(thing->rendering_flags, TRF_Transpar_4))
        RendererAddDrawFlags(Lb_SPRITE_TRANSPAR4);
    if (flag_is_set(thing->rendering_flags, TRF_Transpar_Alpha))
        EngineSpriteDrawUsingAlpha = 1;
    animation_sprite = get_render_animation_sprite(thing->anim_sprite);
    current_frame = thing->current_frame;
    process_keeper_sprite(jspr->scr_x, jspr->scr_y, animation_sprite, angle, current_frame, base_sprite_size);

    //Flame
    RendererSetDrawFlags(0);
    EngineSpriteDrawUsingAlpha = 0;
    if (flame.transparency_flags == TRF_Transpar_8)
    {
        RendererAddDrawFlags(Lb_SPRITE_TRANSPAR8);
    }
    else if (flame.transparency_flags == TRF_Transpar_4)
    {
        RendererAddDrawFlags(Lb_SPRITE_TRANSPAR4);
    }
    else if (flame.transparency_flags == TRF_Transpar_Alpha)
    {
        EngineSpriteDrawUsingAlpha = 1;
    }
    int64_t flame_sprite = get_render_animation_sprite(flame.animation_id);
    unsigned char flame_frames = keepersprite_frames(flame_sprite);
    if (flame_frames > 0) {
        nframe = (thing->index + get_gameturn() * flame.anim_speed / 256) % flame_frames;
        process_keeper_sprite(jspr->scr_x + add_x, jspr->scr_y + add_y, flame_sprite, angle, nframe, scale);
    }
}

static int64_t get_thing_shade(struct Thing* thing);
static void draw_fastview_mapwho(struct Camera *cam, struct BucketKindJontySprite *jspr)
{
    int64_t flg_mem;
    unsigned char alpha_mem;
    struct PlayerInfo *player = get_my_player();
    struct ObjectConfigStats* objst;
    struct Thing *thing = jspr->thing;
    int64_t animation_sprite;
    unsigned char current_frame;
    int64_t angle;
    flg_mem = RendererGetDrawFlags();
    alpha_mem = EngineSpriteDrawUsingAlpha;
    animation_sprite = get_render_animation_sprite(thing->anim_sprite);
    current_frame = thing->current_frame;
    if (keepersprite_rotable(animation_sprite))
    {
        angle = thing->move_angle_xy - cam->rotation_angle_x; // rotation_angle_x maybe short
    }
    else
    {
        angle = thing->move_angle_xy;
    }

    switch(thing->rendering_flags & TRF_Transpar_Alpha)
    {
        case TRF_Transpar_8:
            RendererAddDrawFlags(Lb_SPRITE_TRANSPAR8);
            break;
        case TRF_Transpar_4:
            RendererAddDrawFlags(Lb_SPRITE_TRANSPAR4);
            break;
        default:
            break;
    }
    int64_t shade_intensity = 0x2000;
    if ( !(thing->rendering_flags & TRF_Unshaded) )
        shade_intensity = get_thing_shade(thing);
    shade_intensity >>= 8;

    int64_t size_on_screen = thing->sprite_size * (int64_t)((((int64_t)camera_zoom << 13) / 0x10000) / pixel_size) / 0x10000;
    if ( thing->rendering_flags & TRF_Tint_Flags )
    {
        RendererAddDrawFlags(Lb_SPRITE_REMAP);
        SetupSpriteRemapGhost(thing->tint_colour,
            (thing->rendering_flags & TRF_Tint_2) ? SPRITE_TINT_STRONG : SPRITE_TINT_LEGACY);
    }
    else if ( shade_intensity == 0x2000 )
    {
        RendererClearDrawFlags(Lb_SPRITE_REMAP);
    }
    else
    {
        RendererAddDrawFlags(Lb_SPRITE_REMAP);
        SetupSpriteRemapShade(shade_intensity);
    }

    EngineSpriteDrawUsingAlpha = 0;
    switch (thing->rendering_flags & (TRF_Transpar_Flags))
    {
        case TRF_Transpar_8:
            RendererAddDrawFlags(Lb_SPRITE_TRANSPAR8);
            RendererClearDrawFlags(Lb_SPRITE_REMAP);
            break;
        case TRF_Transpar_4:
            RendererAddDrawFlags(Lb_SPRITE_TRANSPAR4);
            RendererClearDrawFlags(Lb_SPRITE_REMAP);
            break;
        case TRF_Transpar_Alpha:
            EngineSpriteDrawUsingAlpha = 1;
            break;
    }

    if ((thing->class_id == TCls_Creature)
        || (thing->class_id == TCls_Object)
        || (thing->class_id == TCls_DeadCreature)
        || (player->work_state == PSt_QueryAll))
    {
        if ((local_state.local_thing_under_hand == thing->index) && ((get_gameturn() % (4 * kfx_config_state.gui_blink_rate)) >= 2 * kfx_config_state.gui_blink_rate)) {
            RendererAddDrawFlags(Lb_SPRITE_REMAP);
            SetupSpriteRemapWhiteFlash();
        } else {
            if (thing->last_turn_damaged == kfx_sim_state.play_gameturn)
            {
                RendererAddDrawFlags(Lb_SPRITE_REMAP);
                SetupSpriteRemapRedFlash();
            }
        }
        thing_being_displayed_is_creature = 1;
        thing_being_displayed = thing;
    } else
    {
        thing_being_displayed_is_creature = 0;
        thing_being_displayed = NULL;
    }

    if (animation_sprite_id_invalid(animation_sprite))
    {
        ERRORLOG("Invalid graphic Id %" PRId64 " from model %" PRId64 ", class %" PRId64, (int64_t)animation_sprite, (int64_t)thing->model, (int64_t)thing->class_id);
        RendererSetDrawFlags(flg_mem);
        EngineSpriteDrawUsingAlpha = alpha_mem;
        return;
    }
    TbBool flame_on_sprite = false;
    TbBool is_shown = true;
    if (thing_is_object(thing))
    {
        objst = get_object_model_stats(thing->model);
        if (objst->flame.animation_id != 0)
        {
            flame_on_sprite = true;
            process_keeper_flame_on_sprite(jspr, angle, size_on_screen);
        }
    }
    else
    {
        if (thing->class_id == TCls_Trap)
        {
            is_shown = !kfx_config_state.conf.trapdoor_conf.trap_cfgstats[thing->model].hidden;
            if (is_shown || thing->trap.revealed)
            {
                struct TrapConfigStats* trapst = get_trap_model_stats(thing->model);
                if ((trapst->flame.animation_id != 0) && (thing->trap.num_shots != 0))
                {
                    flame_on_sprite = true;
                    process_keeper_flame_on_sprite(jspr, angle, size_on_screen);
                }
            }
        }
        else
        {
            is_shown = ((thing->rendering_flags & TRF_Invisible) == 0);
        }
    }
    if (!flame_on_sprite)
    {
        if (is_shown || get_my_player()->id_number == thing->owner || thing->trap.revealed)
        {
            process_keeper_sprite(jspr->scr_x, jspr->scr_y, animation_sprite, angle, current_frame, size_on_screen);
        }
    }
    RendererSetDrawFlags(flg_mem);
    EngineSpriteDrawUsingAlpha = alpha_mem;
}

static void draw_engine_number(struct BucketKindFloatingGoldText *num)
{
    struct PlayerInfo *player;
    int64_t flg_mem;
    const struct TbSprite *spr;
    int64_t remaining_digits;
    int64_t ndigits;
    int64_t w;
    int64_t h;
    int64_t pos_x;

    // 1st argument: the scale when fully zoomed out. 2nd argument: the scale at base level zoom
    double scale_by_zoom = LbLerp(0.15, 1.00, hud_scale);

    flg_mem = RendererGetDrawFlags();
    player = get_my_player();
    RendererClearDrawFlags(Lb_SPRITE_FLIP_HORIZ);
    spr = get_button_sprite(GBS_fontchars_number_dig0);
    w = scale_ui_value(spr->SWidth) * scale_by_zoom;
    h = scale_ui_value(spr->SHeight) * scale_by_zoom;
    struct Camera *active_cam = get_local_active_camera(player);
    if (active_cam != NULL && (active_cam->view_mode == PVM_IsoWibbleView || active_cam->view_mode == PVM_FrontView || active_cam->view_mode == PVM_IsoStraightView)) {
        // Count digits to be displayed
        ndigits=0;
        for (remaining_digits = num->lvl; remaining_digits > 0; remaining_digits /= 10)
            ndigits++;
        if (ndigits > 0)
        {
            // Show the digits
            pos_x = w*(ndigits-1)/2 + num->x;
            for (remaining_digits = num->lvl; remaining_digits > 0; remaining_digits /= 10)
            {
                spr = get_button_sprite((remaining_digits%10) + GBS_fontchars_number_dig0);
                LbSpriteDrawScaled(pos_x, num->y - h, spr, w, h);

                pos_x -= w;
            }
        }
    }
    RendererSetDrawFlags(flg_mem);
}

static void draw_engine_room_flagpole(struct BucketKindRoomFlag *rflg)
{
    RendererClearDrawFlags(Lb_SPRITE_FLIP_HORIZ);

    struct Room *room = room_get(rflg->lvl);
    if (!room_exists(room) || !room_can_have_ensign(room->kind)) {
        return;
    }
    struct PlayerInfo *player = get_my_player();
    const struct Camera *cam = get_local_active_camera(player);

    if (
        cam->view_mode == PVM_IsoWibbleView ||
        cam->view_mode == PVM_FrontView ||
        cam->view_mode == PVM_IsoStraightView
    ) {
        if (settings.roomflags_on)
        {
            int64_t deltay, height, zoom_factor;
            // 1st argument: the scale when fully zoomed out. 2nd argument: the scale at base level zoom
            double scale_by_zoom = LbLerp(0.15, 1.00, hud_scale);

            if (cam->view_mode == PVM_FrontView) {
                zoom_factor = 4094*scale_by_zoom;
                deltay = (zoom_factor << 7 >> 13) * units_per_pixel_ui / 16;
                height = ((2 * (71 * zoom_factor) >> 13) * units_per_pixel_ui + 8) / 16;
            } else {
                zoom_factor = camera_zoom;
                deltay = (zoom_factor << 7 >> 13);
                height = (2 * (71 * zoom_factor) >> 13) + 8;
            }

            LbDrawBox(rflg->x,
                      rflg->y - deltay,
                      ((4*scale_by_zoom) * units_per_pixel_ui + 8) / 16,
                      height,
                      expand_indexed_pixel(kfx_sim_state.colours[3][1][0], RendererGetActivePalette()));
            LbDrawBox(rflg->x + (2*scale_by_zoom) * (units_per_pixel_ui) / 16,
                      rflg->y - deltay,
                      ((2*scale_by_zoom) * units_per_pixel_ui + 8) / 16,
                      height,
                      expand_indexed_pixel(kfx_sim_state.colours[1][0][0], RendererGetActivePalette()));
        }
    }
}

/**
 * Selects index of a sprite used to show creature health flower.
 * @param thing
 */
static int64_t choose_health_sprite(struct Thing* thing)
{
    struct CreatureControl *cctrl;
    cctrl = creature_control_get_from_thing(thing);
    HitPoints health;
    HitPoints maxhealth;
    health = thing->health;
    maxhealth = cctrl->max_health;

    if ((maxhealth <= 0) || (health <= 0))
    {
        return get_player_colored_button_sprite_idx(GBS_creature_flower_health_r1,thing->owner);
    } else
    if (health >= maxhealth)
    {
        return get_player_colored_button_sprite_idx(GBS_creature_flower_health_r1,thing->owner) - 7;
    } else
    {
        return get_player_colored_button_sprite_idx(GBS_creature_flower_health_r1,thing->owner) - (8 * health / maxhealth);
    }
}

void fill_status_sprite_indexes(struct Thing *thing, struct CreatureControl *cctrl, int64_t *health_spridx,
                                int64_t *state_spridx, int64_t *anger_spridx)
{
    (*health_spridx) = choose_health_sprite(thing);
    if (is_my_player_number(thing->owner))
    {
        RendererAddDrawFlags(Lb_SPRITE_TRANSPAR4);
        if (get_gameturn() - cctrl->thought_bubble_last_turn_drawn == 1)
        {
            if (cctrl->thought_bubble_display_timer < 40) {
                cctrl->thought_bubble_display_timer++;
            }
        } else {
            if (get_gameturn() - cctrl->thought_bubble_last_turn_drawn > 1) {
                cctrl->thought_bubble_display_timer = 0;
            }
        }
        cctrl->thought_bubble_last_turn_drawn = get_gameturn();
        if (cctrl->thought_bubble_display_timer >= 40)
        {
            struct CreatureStateConfig *stati;
            stati = get_creature_state_with_task_completion(thing);
            if (!stati->blocks_all_state_changes)
            {
                if (creature_under_spell_effect(thing, CSAfF_MadKilling))
                {
                    stati = &kfx_config_state.conf.crtr_conf.states[CrSt_MadKillingPsycho];
                }
                else if (anger_is_creature_livid(thing))
                {
                    stati = &kfx_config_state.conf.crtr_conf.states[CrSt_CreatureLeavingDungeon];
                }
                else if (creature_is_called_to_arms(thing))
                {
                    stati = &kfx_config_state.conf.crtr_conf.states[CrSt_ArriveAtCallToArms];
                }
                else if (creature_is_at_alarm(thing))
                {
                    stati = &kfx_config_state.conf.crtr_conf.states[CrSt_ArriveAtAlarm];
                }
                else if (anger_is_creature_angry(thing))
                {
                    stati = &kfx_config_state.conf.crtr_conf.states[CrSt_PersonSulkAtLair];
                }
                else if (hunger_is_creature_hungry(thing))
                {
                    stati = &kfx_config_state.conf.crtr_conf.states[CrSt_CreatureArrivedAtGarden];
                }
                else if (creature_requires_healing(thing))
                {
                    stati = &kfx_config_state.conf.crtr_conf.states[CrSt_CreatureSleep];
                }
                else if (cctrl->paydays_owed)
                {
                    stati = &kfx_config_state.conf.crtr_conf.states[CrSt_CreatureWantsSalary];
                }
                else
                {
                    stati = get_creature_state_with_task_completion(thing);
                }
                if ((stati->display_thought_bubble == 1) || (kfx_render_state.thing_pointed_at == thing))
                {
                    (*state_spridx) = stati->sprite_idx;
                }
                switch (anger_get_creature_anger_type(thing))
                {
                case AngR_NotPaid:
                    if ((cctrl->paydays_owed <= 0) && (cctrl->paydays_advanced >= 0))
                    {
                        (*anger_spridx) = GBS_creature_states_angry;
                    }
                    else
                    {
                        (*anger_spridx) = GBS_creature_states_getgold;
                    }
                    break;
                case AngR_Hungry:
                    (*anger_spridx) = GBS_creature_states_hungry;
                    break;
                case AngR_NoLair:
                    (*anger_spridx) = GBS_creature_states_sleep;
                    break;
                case AngR_Other:
                    (*anger_spridx) = GBS_creature_states_angry;
                    break;
                default:
                    break;
                }
            }
        }
    }
}

void draw_status_sprites(int64_t scrpos_x, int64_t scrpos_y, struct Thing *thing)
{
    struct PlayerInfo *player = get_my_player();
    const struct Camera *cam = get_local_active_camera(player);
    if (cam == NULL)
    {
        return;
    }

    double scale_by_zoom;
    int64_t base_size = creature_status_size * 256;
    switch (cam->view_mode)
    {
    case PVM_IsoWibbleView:
    case PVM_IsoStraightView:
        // 1st argument: the scale when fully zoomed out. 2nd argument: the scale at base level zoom.
        scale_by_zoom = LbLerp(0.15, 1.00, hud_scale);
        break;
    case PVM_FrontView:
        scale_by_zoom = LbLerp(0.15, 1.00, hud_scale);
        break;
    case PVM_ParchmentView:
        scale_by_zoom = 1;
        break;
    default:
        return; // Do not draw if camera is 1st person.
    }

    int64_t flg_mem;

    flg_mem = RendererGetDrawFlags();
    RendererSetDrawFlags(0);

    struct CreatureControl *cctrl;
    cctrl = creature_control_get_from_thing(thing);
    if ((cctrl->force_health_flower_hidden == true) || flag_is_set(get_creature_model_flags(thing), CMF_NoHealthFlower)) {
        RendererSetDrawFlags(flg_mem);
        return;
    }
    if (flag_is_set(kfx_sim_state.mode_flags,MFlg_NoHeroHealthFlower))
    {
        if (local_state.local_thing_under_hand != thing->index) {
            cctrl->thought_bubble_last_turn_drawn = get_gameturn();
            if (cctrl->force_health_flower_displayed == false)
            {
                return;
            }
        }
        cctrl->thought_bubble_display_timer = 40;
    }

    int64_t health_spridx;
    int64_t state_spridx;
    int64_t anger_spridx;

    anger_spridx = 0;
    health_spridx = 0;
    state_spridx = 0;

    CrtrExpLevel exp_level = min(cctrl->exp_level, 9);
    if (cam->view_mode != PVM_ParchmentView)
    {
        fill_status_sprite_indexes(thing, cctrl, &health_spridx, &state_spridx, &anger_spridx);
    }

    int64_t h_add;
    h_add = 0;
    int64_t w;
    int64_t h;
    const struct TbSprite *spr;
    int64_t bs_units_per_px;
    spr = get_button_sprite(GBS_creature_states_cloud);
    bs_units_per_px = units_per_pixel_ui * 2 * scale_by_zoom;

    if (cam->view_mode == PVM_FrontView)
    {
        double flower_distance = 1280; // Higher number means flower is further away from creature.
        scrpos_y -= (int64_t)((flower_distance / spr->SHeight) * ((double)camera_zoom / FRONTVIEW_CAMERA_ZOOM_MAX));
    }

    if (state_spridx || anger_spridx)
    {
        spr = get_button_sprite(GBS_creature_states_cloud);
        w = (base_size * spr->SWidth * bs_units_per_px / 16) >> 13;
        h = (base_size * spr->SHeight * bs_units_per_px / 16) >> 13;
        LbSpriteDrawScaled(scrpos_x - w / 2, scrpos_y - h, spr, w, h);
    }

    RendererClearDrawFlags(Lb_SPRITE_TRANSPAR8);
    RendererClearDrawFlags(Lb_SPRITE_TRANSPAR4);
    if (((get_gameturn() % (8 * kfx_config_state.gui_blink_rate)) < 4 * kfx_config_state.gui_blink_rate) && (anger_spridx > 0))
    {
        spr = get_button_sprite(anger_spridx);
        w = (base_size * spr->SWidth * bs_units_per_px / 16) >> 13;
        h = (base_size * spr->SHeight * bs_units_per_px / 16) >> 13;
        LbSpriteDrawScaled(scrpos_x - w / 2, scrpos_y - h, spr, w, h);
        spr = get_button_sprite_for_player(state_spridx, thing->owner);
        h_add += spr->SHeight * bs_units_per_px / 16;
    }
    else if (state_spridx)
    {
        spr = get_button_sprite_for_player(state_spridx, thing->owner);
        w = (base_size * spr->SWidth * bs_units_per_px / 16) >> 13;
        h = (base_size * spr->SHeight * bs_units_per_px / 16) >> 13;
        LbSpriteDrawScaled(scrpos_x - w / 2, scrpos_y - h, spr, w, h);
        h_add += h;
    }

    if ((thing->lair.spr_size > 0) && (health_spridx > 0) && ((get_gameturn() % (2 * kfx_config_state.gui_blink_rate)) >= kfx_config_state.gui_blink_rate))
    {
        int64_t flash_color = get_player_color_idx(thing->owner);
        if (flash_color == PLAYER_NEUTRAL)
        {
            flash_color = (get_gameturn() % (4 * kfx_config_state.neutral_flash_rate)) / kfx_config_state.neutral_flash_rate;
        }
        spr = get_button_sprite_for_player(health_spridx, thing->owner);
        w = (base_size * spr->SWidth * bs_units_per_px / 16) >> 13;
        h = (base_size * spr->SHeight * bs_units_per_px / 16) >> 13;
        LbSpriteDrawScaledOneColour(scrpos_x - w / 2, scrpos_y - h - h_add, spr, w, h, player_flash_colours[flash_color]);
    }
    else
    {
        // Determine if the creature is under the player's hand (being hovered over).
        TbBool is_thing_under_hand = (local_state.local_thing_under_hand == thing->index);
        // Check if the creature is an enemy and is visible.
        TbBool is_enemy_and_visible = players_are_enemies(player->id_number, thing->owner) && !creature_is_invisible(thing);
        // Check if the creature belongs to the player, is hurt but not unconscious.
        TbBool is_owned_and_hurt = false;
        // Check if the creature belongs to an ally.
        TbBool is_allied = false;
        TbBool should_drag_to_lair = false;
        TbBool is_zombie_player = !flag_is_set(get_player(thing->owner)->allocflags, PlaF_Allocated);
        TbBool forced_visible = cctrl->force_health_flower_displayed;
        if (!is_enemy_and_visible)
        {
            is_owned_and_hurt = creature_would_benefit_from_healing(thing) && !creature_is_being_unconscious(thing) && (player->id_number == thing->owner);
            is_allied = players_are_mutual_allies(player->id_number, thing->owner) && (player->id_number != thing->owner);
            should_drag_to_lair = creature_is_being_unconscious(thing) && (player->id_number == thing->owner)
            // Check if the creature has a lair room or can heal in a lair.
            && ((kfx_config_state.conf.rules[thing->owner].workers.drag_to_lair == 1 && !room_is_invalid(get_creature_lair_room(thing)))
            // Or check if the creature can have lair and heal in it.
            || (kfx_config_state.conf.rules[thing->owner].workers.drag_to_lair == 2 && creature_can_do_healing_sleep(thing)));
        }
        // Check if the creature is in combat.
        TbBool is_in_combat = (cctrl->combat_flags != 0);
        // Check if the creature has a lair.
        TbBool has_lair = (thing->lair.spr_size > 0);
        // Determine if the current view is the schematic top-down map view.
        TbBool is_parchment_map_view = (cam->view_mode == PVM_ParchmentView);
        if ((forced_visible)
        || (is_thing_under_hand)
        || (is_enemy_and_visible)
        || (is_owned_and_hurt)
        || (is_allied)
        || (is_zombie_player)
        || (thing->owner == PLAYER_NEUTRAL)
        // If drag_to_lair rule is active.
        || (should_drag_to_lair)
        || (is_in_combat)
        || (has_lair)
        || (is_parchment_map_view))
        {
            if (health_spridx > 0)
            {
                spr = get_button_sprite_for_player(health_spridx, thing->owner);
                w = (base_size * spr->SWidth * bs_units_per_px / 16) >> 13;
                h = (base_size * spr->SHeight * bs_units_per_px / 16) >> 13;
                LbSpriteDrawScaled(scrpos_x - w / 2, scrpos_y - h - h_add, spr, w, h);
            }
            spr = get_button_sprite(GBS_creature_flower_level_01 + exp_level);
            w = (base_size * spr->SWidth * bs_units_per_px / 16) >> 13;
            h = (base_size * spr->SHeight * bs_units_per_px / 16) >> 13;
            LbSpriteDrawScaled(scrpos_x - w / 2, scrpos_y - h - h_add, spr, w, h);
        }
    }
    RendererSetDrawFlags(flg_mem);
}

static void draw_iso_only_fastview_mapwho(struct Camera *cam, struct BucketKindJontySprite *spr)
{
    if (cam->view_mode == PVM_FrontView)
      draw_fastview_mapwho(cam, spr);
}

#define ROOM_FLAG_PROGRESS_BAR_WIDTH 10
static void draw_room_flag_top(int64_t x, int64_t y, int64_t units_per_px, const struct Room *room)
{
    uint64_t flg_mem;
    flg_mem = RendererGetDrawFlags();
    int64_t bar_fill;
    int64_t bar_empty;
    const struct TbSprite *spr;
    int64_t ps_units_per_px;
    spr = get_panel_sprite(GPS_rpanel_room_ensign_filled);
    ps_units_per_px = 36*units_per_px/spr->SHeight;
    LbSpriteDrawScaled(x, y, spr, spr->SWidth * ps_units_per_px / 16, spr->SHeight * ps_units_per_px / 16);
    struct RoomConfigStats *roomst;
    roomst = get_room_kind_stats(room->kind);
    int64_t barpos_x;
    barpos_x = x + spr->SWidth * ps_units_per_px / 16 - (8 * units_per_px - 8) / 16;
    spr = get_panel_sprite(roomst->medsym_sprite_idx);
    LbSpriteDrawResized(x - 2*units_per_px/16, y - 4*units_per_px/16, ps_units_per_px, spr);
    bar_fill = ROOM_FLAG_PROGRESS_BAR_WIDTH;
    bar_empty = 0;
    if (room->slabs_count > 0)
    {
        bar_fill = ROOM_FLAG_PROGRESS_BAR_WIDTH * room->health / compute_room_max_health(room->slabs_count, room->efficiency);
        bar_empty = ROOM_FLAG_PROGRESS_BAR_WIDTH - bar_fill;
    }
    int64_t bar_width;
    int64_t bar_height;
    bar_width = (2 * bar_empty * units_per_px + 8) / 16;
    // Compute height in a way which will assure covering whole bar area
    bar_height = (5 * units_per_px - 8) / 16;
    LbDrawBox(barpos_x - bar_width, y +  (8 * units_per_px + 8) / 16, bar_width, bar_height, expand_indexed_pixel(kfx_sim_state.colours[0][0][0], RendererGetActivePalette()));
    bar_empty = 0;
    if (room->total_capacity > 0)
    {
        bar_fill = ROOM_FLAG_PROGRESS_BAR_WIDTH * room->used_capacity / room->total_capacity;
        bar_empty = ROOM_FLAG_PROGRESS_BAR_WIDTH - bar_fill;
    }
    bar_width = (2 * bar_empty * units_per_px + 8) / 16;
    LbDrawBox(barpos_x - bar_width, y + (16 * units_per_px + 8) / 16, bar_width, bar_height, expand_indexed_pixel(kfx_sim_state.colours[0][0][0], RendererGetActivePalette()));
    bar_empty = 0;
    {
        bar_fill = ROOM_FLAG_PROGRESS_BAR_WIDTH * room->efficiency / ROOM_EFFICIENCY_MAX;
        bar_empty = ROOM_FLAG_PROGRESS_BAR_WIDTH - bar_fill;
    }
    bar_width = (2 * bar_empty * units_per_px + 8) / 16;
    LbDrawBox(barpos_x - bar_width, y + (24 * units_per_px + 8) / 16, bar_width, bar_height, expand_indexed_pixel(kfx_sim_state.colours[0][0][0], RendererGetActivePalette()));
    RendererSetDrawFlags(flg_mem);
}
#undef ROOM_FLAG_PROGRESS_BAR_WIDTH

static void draw_engine_room_flag_top(struct BucketKindRoomFlag *rflg)
{
    RendererClearDrawFlags(Lb_SPRITE_FLIP_HORIZ);

    struct Room *room = room_get(rflg->lvl);
    if (!room_exists(room) || !room_can_have_ensign(room->kind)) {
        return;
    }
    struct PlayerInfo *player = get_my_player();
    const struct Camera *cam = get_local_active_camera(player);

    if (
        cam->view_mode == PVM_IsoWibbleView ||
        cam->view_mode == PVM_FrontView ||
        cam->view_mode == PVM_IsoStraightView
    ) {
        if (settings.roomflags_on)
        {
            int64_t top_of_pole_offset, zoom_factor;
            // 1st argument: the scale when fully zoomed out. 2nd argument: the scale at base level zoom
            double scale_by_zoom = LbLerp(0.15, 1.00, hud_scale);

            if (cam->view_mode == PVM_FrontView) {
                zoom_factor = (4094*scale_by_zoom);
                top_of_pole_offset = (zoom_factor << 7 >> 13) * (units_per_pixel_ui)/16;
            } else {
                zoom_factor = camera_zoom;
                top_of_pole_offset = (zoom_factor << 7 >> 13);
            }
            draw_room_flag_top(rflg->x, rflg->y - top_of_pole_offset, (units_per_pixel_ui *scale_by_zoom), room);
        }
    }
}

static void draw_stripey_line(int64_t x1,int64_t y1,int64_t x2,int64_t y2,unsigned char line_color)
{
    if ((x1 == x2) && (y1 == y2)) return; // todo if distance is 0, provide a red square

    // get the 4 least significant bits of get_gameturn(), to loop through the starting index of the color array, using numbers 0-15.
    unsigned char color_index = get_gameturn() & 0xf;

    // get engine window width and height
    int64_t relative_window_width = ((local_state.engine_window_width * 256) / (pixel_size * 256)) - 1;
    int64_t relative_window_height = ((local_state.engine_window_height * 256) / (pixel_size * 256)) - 1;

    // Bresenham’s Line Drawing Algorithm - handles all octants
    // A and B are relative, and are set to be either X (shallow curves) or Y (steep curves).
    // A1 and A2, and B1 and B2, are swapped when the line is directed towards -1 X/Y.
    // A and B are incremented, apart from when the slope of the lines goes from 0 to -1 in A, where B will decrement instead
    int64_t distance_a, distance_b, a, b, a1, b1, a2, b2, relative_window_a, relative_window_b, remainder, remainder_limit;
    int64_t *x_coord, *y_coord; // Maintain a reference to the actual X/Y coordinates, even after swapping A and B

    if (llabs(y2 - y1) < llabs(x2 - x1))
    {
        x_coord = &a;
        y_coord = &b;
        relative_window_a = relative_window_width;
        relative_window_b = relative_window_height;
        if (x1 < x2)
        {
            a1 = x1;
            b1 = y1;
            a2 = x2;
            b2 = y2;
        }
        else // Swap n1 with n2
        {
            a1 = x2;
            b1 = y2;
            a2 = x1;
            b2 = y1;
            color_index = 0xf - color_index; // invert the color index
        }
    }
    else // Reverse X and Y
    {
        x_coord = &b;
        y_coord = &a;
        relative_window_a = relative_window_height;
        relative_window_b = relative_window_width;
        if (y1 < y2)
        {
            a1 = y1;
            b1 = x1;
            a2 = y2;
            b2 = x2;
        }
        else // Swap n1 with n2
        {
            a1 = y2;
            b1 = x2;
            a2 = y1;
            b2 = x1;
            color_index = 0xf - color_index; // invert the color index
        }
    }

    distance_a = a2 - a1;
    distance_b = b2 - b1;

    if (distance_b == 0)
    {
        if ((b1 < 0) || (b1 > relative_window_b))
        {
            return; // line is off the screen
        }
    }
    if (distance_a == 0)
    {
        if ((a1 < 0) || (a1 > relative_window_a))
        {
            return; // line is off the screen
        }
    }

    int64_t start_b_dist_from_window = 0 - b1; // For window clipping
    int64_t end_b_dist_from_window = b2 - relative_window_b; // For window clipping

    // Handle going towards 0 in B (i.e. B counts down, not up)
    int64_t b_increment = 1;
    if (distance_b < 0)
    {
        b_increment = -1;
        distance_b = -distance_b;
        start_b_dist_from_window = b1 - relative_window_b;
        end_b_dist_from_window = 0 - b2;
    }

    // Clip line within engine_window
    remainder_limit = (distance_b+1)/2;
    // Find starting A coord
    if (distance_b == 0)
    {
        remainder = 0;
    }
    else
    {
        remainder = start_b_dist_from_window * distance_a % distance_b;
    }
    int64_t min_a_start = 0;
    if ((b1 < 0 || b1 > relative_window_b))
    {
        min_a_start = a1 + ( (start_b_dist_from_window) * distance_a / distance_b );
        if (remainder >= remainder_limit)
        {
            min_a_start++;
        }
        min_a_start = max(min_a_start, 0);
    }
    int64_t a_start = max(a1, min_a_start);
    // Find ending A coord
    if (distance_b == 0)
    {
        remainder = 0;
    }
    else
    {
        remainder = end_b_dist_from_window * distance_a % distance_b;
    }
    int64_t max_a_end = relative_window_a;
    if (b2 < 0 || b2 > relative_window_b)
    {
        max_a_end = a2 - ( (end_b_dist_from_window) * distance_a / distance_b );
        if (remainder >= remainder_limit)
        {
            max_a_end--;
        }
        max_a_end = min(max_a_end, relative_window_a);

    }
    int64_t a_end = min(a2, max_a_end);
    // Find starting B coord
    remainder_limit = (distance_a+1)/2;
    if (distance_a == 0)
    {
        remainder = 0;
    }
    else
    {
        remainder = (a_start - a1) * distance_b % distance_a; // initialise remainder for loop
    }
    int64_t b_start =  (distance_a == 0) ? b1 : b1 + ( b_increment * (a_start - a1) * distance_b / distance_a );
    if (remainder >= remainder_limit)
    {
        remainder -= distance_a;
        b_start += b_increment;
    }
    b = b_start;

    // A hack-fix to ensure that pixels are always drawn on screen. Otherwise when zoomed in, pixels have trouble being drawn in the bottom right corner
    relative_window_a = lbDisplay.GraphicsScreenWidth;
    relative_window_b = lbDisplay.GraphicsScreenHeight;

    // Set up parameters before starting the drawing loop
    double custom_line_box_size = line_box_size / 100.0;
    int64_t line_thickness = max(1, (custom_line_box_size * units_per_pixel_best / 16.0) );

    // Make the line slightly thinner when zoomed out
    line_thickness = LbLerp(line_thickness, 1, 1.0-hud_scale);

    int64_t put_pixels_left = line_thickness/2; // Allocate half of the thickness to the left
    int64_t put_pixels_right = line_thickness-put_pixels_left; // Remaining thickness is placed to the right

    TbBool isHorizontal = llabs(x2 - x1) >= llabs(y2 - y1); // Check if line is more horizontal than vertical, helps with the "pixel-art look".
    int64_t temp_x, temp_y;
    double color_animation_position = color_index;
    // Main loop to draw the line
    for (a = a_start; a <= a_end; a++) {

        //if ((a < 0) || (a > relative_window_a) || (b < 0) || (b > relative_window_b))
        //{
        //    Temporary Error message, this should never appear in the log, but if it does, then the line must have been clipped incorrectly
        //    WARNMSG("draw_stripey_line: Pixel rendered outside engine window. X: %d, Y: %d, window_width: %d, window_height %d, A1: %d, A2 %d, B1 %d, B2 %d, a_start: %d, a_end: %d, b_start: %d, rWA: %d", (int64_t)(*x_coord), (int64_t)(*y_coord), (int64_t)(relative_window_width), (int64_t)(relative_window_height), (int64_t)(a1), (int64_t)(a2), (int64_t)(b1), (int64_t)(b2), (int64_t)(a_start), (int64_t)(a_end), (int64_t)(b_start), (int64_t)(relative_window_a));
        //}
        color_animation_position += LbLerp(1.0, 4.0, 1.0-hud_scale) * (16.0/units_per_pixel_best);
        if (color_animation_position >= 16.0) {
            color_animation_position -= 16.0;
        }
        color_index = max(0, (int64_t)color_animation_position);

        // Nested loops to draw square pixels around each point for the specified thickness
        for (int64_t dx = -put_pixels_left; dx < put_pixels_right; dx++) {
            for (int64_t dy = -put_pixels_left; dy < put_pixels_right; dy++) {
                // Determine pixel coordinates based on line orientation
                if (isHorizontal) {
                    temp_x = *x_coord;
                    temp_y = *y_coord + dy;
                } else {
                    temp_x = *x_coord + dx;
                    temp_y = *y_coord;
                }

                // Draw the pixel if it's within the bounds of the window
                if ((temp_x >= 0) && (temp_x < relative_window_a) && (temp_y >= 0) && (temp_y < relative_window_b)) {
                    LbDrawPixel(temp_x, temp_y, colored_stripey_lines[line_color].stripey_line_color_array[color_index]);
                }
            }
        }

        if (remainder >= remainder_limit) {
            b += b_increment;
            remainder -= distance_a;
        }
        remainder += distance_b;
    }
}

/* color is an SLC_* line-colour-category index passed straight through to
 * draw_stripey_line() -- see create_line_element()'s comment above. Not a
 * resolved TbPixel. */
static void draw_clipped_line(int64_t x1, int64_t y1, int64_t x2, int64_t y2, unsigned char color)
{
    if ((x1 >= 0) || (x2 >= 0))
    {
      if ((y1 >= 0) || (y2 >= 0))
      {
        if ((x1 < local_state.engine_window_width) || (x2 < local_state.engine_window_width))
        {
          if ((y1 < local_state.engine_window_height) || (y2 < local_state.engine_window_height))
          {
            draw_stripey_line(x1, y1, x2, y2, color);
          }
        }
      }
    }
}

// gpu-v2 Phase C.1: while a GPU world frame is being recorded (see
// RendererWorldFrameBegin(), display_drawlist()), every textured world-view
// triangle -- isometric QK_PolygonStandard and the first-person
// QK_PolygonNearFP subdivision alike all funnel through here with vec_map
// already set to the block's texture -- is recorded in draw order instead of
// being rasterized on the CPU; RendererGpu3D draws them on the GPU. Sprites
// are recorded the same way, from the software sprite dispatchers.
/* gpu-v2 Phase C.5 lighting pass: hand the recorded world frame this frame's
 * per-pixel lighting inputs (see light_data.c, RendererWorldFrameSetLighting).
 * The GPU rebuilds each pixel's map position from its depth and screen
 * position, so the view->map transform is needed: the engine's world->view
 * transform is affine (rotpers_standard's matrix step), so it is measured by
 * projecting four probe points through the engine's own routine rather than
 * re-derived, then inverted here. */
static int light_pp_compare_distance(const void *a, const void *b)
{
    const float *la = (const float *)a, *lb = (const float *)b;
    const double dax = la[0] - map_x_pos, day = la[1] - map_y_pos, dbx = lb[0] - map_x_pos, dby = lb[1] - map_y_pos;
    const double da = dax * dax + day * day, db = dbx * dbx + dby * dby;
    return (da < db) ? -1 : ((da > db) ? 1 : 0);
}

static void submit_perpixel_lighting(void)
{
    const float *lights;
    int64_t light_count;
    const unsigned char *heights;
    int64_t grid_w, grid_h;
    if (!RendererWorldFrameCapturing() || !light_perpixel_active() || !light_perpixel_get(&lights, &light_count, &heights, &grid_w, &grid_h))
        return;

    // World->view is view = M * e + t, with e = (x - map_x_pos, z - map_z_pos, map_y_pos - y).
    const struct EngineCoord saved_origin = object_origin;
    object_origin.x = object_origin.y = object_origin.z = 0;
    const double step = 8192.0;
    double view[4][3];
    for (int p = 0; p < 4; p++)
    {
        struct EngineCoord ec;
        memset(&ec, 0, sizeof(ec));
        ec.x = (p == 1) ? (int64_t)step : 0;
        ec.y = (p == 2) ? (int64_t)step : 0;
        ec.z = (p == 3) ? (int64_t)step : 0;
        pers_set_transform_matrix(&ec, &camera_matrix);
        view[p][0] = (double)ec.x; view[p][1] = (double)ec.y; view[p][2] = (double)ec.z;
    }
    object_origin = saved_origin;
    double m[3][3];
    for (int col = 0; col < 3; col++)
        for (int row = 0; row < 3; row++)
            m[row][col] = (view[col + 1][row] - view[0][row]) / step;
    const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
                     - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                     + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (fabs(det) < 1e-9)
        return;
    double inv[3][3];
    inv[0][0] =  (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / det;
    inv[0][1] = -(m[0][1] * m[2][2] - m[0][2] * m[2][1]) / det;
    inv[0][2] =  (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det;
    inv[1][0] = -(m[1][0] * m[2][2] - m[1][2] * m[2][0]) / det;
    inv[1][1] =  (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det;
    inv[1][2] = -(m[0][0] * m[1][2] - m[0][2] * m[1][0]) / det;
    inv[2][0] =  (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / det;
    inv[2][1] = -(m[0][0] * m[2][1] - m[0][1] * m[2][0]) / det;
    inv[2][2] =  (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det;
    const double *t = view[0];
    // e = inv * (view - t); map x = map_x_pos + e.x; map y = map_y_pos - e.z.
    // e.y is height above map_z_pos: map z = map_z_pos + e.y.
    double map_x[4], map_y[4], map_z[4];
    for (int k = 0; k < 3; k++)
    {
        map_x[k] = inv[0][k];
        map_y[k] = -inv[2][k];
        map_z[k] = inv[1][k];
    }
    map_x[3] = (double)map_x_pos - (inv[0][0] * t[0] + inv[0][1] * t[1] + inv[0][2] * t[2]);
    map_y[3] = (double)map_y_pos + (inv[2][0] * t[0] + inv[2][1] * t[1] + inv[2][2] * t[2]);
    map_z[3] = (double)map_z_pos - (inv[1][0] * t[0] + inv[1][1] * t[1] + inv[1][2] * t[2]);

    // Nearest lights first: the GPU takes at most WORLDFRAME_MAX_LIGHTS.
    static float sorted[256 * 8];
    if (light_count > 256) light_count = 256;
    memcpy(sorted, lights, (size_t)light_count * 8 * sizeof(float));
    qsort(sorted, (size_t)light_count, 8 * sizeof(float), light_pp_compare_distance);

    const double fade[4] = { (double)fade_min, (double)fade_max, (double)fade_scaler, (double)fade_range };
    RendererWorldFrameSetLighting(map_x, map_y, map_z, (double)lens, (double)view_width_over_2, (double)view_height_over_2,
                                  fade, sorted, light_count, heights, grid_w, grid_h);
}

static void world_draw_gpoly(struct PolyPoint *point_a, struct PolyPoint *point_b, struct PolyPoint *point_c)
{
    if (RendererWorldFrameCapturing())
        RendererWorldFrameAddPoly(point_a, point_b, point_c, vec_map);
    else
        draw_gpoly(point_a, point_b, point_c);
}

static void draw_subdivided_near_polygon(struct BucketKindPolygonNearFP *polygon_data)
{
    struct XYZ coord_a;
    struct XYZ coord_b;
    struct XYZ coord_c;
    struct XYZ coord_d;
    struct XYZ coord_e;
    struct PolyPoint point_a;
    struct PolyPoint point_b;
    struct PolyPoint point_c;
    struct PolyPoint point_d;
    struct PolyPoint point_e;
    struct PolyPoint point_f;
    struct PolyPoint point_g;
    struct PolyPoint point_h;
    struct PolyPoint point_i;
    struct PolyPoint point_j;
    struct PolyPoint point_k;
    struct PolyPoint point_l;
    vec_map = block_ptrs[polygon_data->block];
    switch (polygon_data->subtype)
    {
    case 0:
        vec_mode = VM_QuadTextured;
        world_draw_gpoly(&polygon_data->vertex_first,&polygon_data->vertex_second,&polygon_data->vertex_third);
        break;
    case 1:
        vec_mode = VM_QuadTextured;
        coord_a.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_first.x) >> 1;
        coord_a.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_first.y) >> 1;
        coord_a.z = (polygon_data->coordinate_first.z + polygon_data->coordinate_second.z) >> 1;
        point_a.U = (polygon_data->vertex_first.U + polygon_data->vertex_second.U) >> 1;
        point_a.V = (polygon_data->vertex_second.V + polygon_data->vertex_first.V) >> 1;
        point_a.S = (polygon_data->vertex_second.S + polygon_data->vertex_first.S) >> 1;
        perspective(&coord_a, &point_a);
        world_draw_gpoly(&polygon_data->vertex_first, &point_a, &polygon_data->vertex_third);
        world_draw_gpoly(&point_a, &polygon_data->vertex_second, &polygon_data->vertex_third);
        break;
    case 2:
        vec_mode = VM_QuadTextured;
        coord_a.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_third.x) >> 1;
        coord_a.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_third.y) >> 1;
        coord_a.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_second.z) >> 1;
        point_a.U = (polygon_data->vertex_third.U + polygon_data->vertex_second.U) >> 1;
        point_a.V = (polygon_data->vertex_third.V + polygon_data->vertex_second.V) >> 1;
        point_a.S = (polygon_data->vertex_third.S + polygon_data->vertex_second.S) >> 1;
        perspective(&coord_a, &point_a);
        world_draw_gpoly(&polygon_data->vertex_first, &polygon_data->vertex_second, &point_a);
        world_draw_gpoly(&polygon_data->vertex_first, &point_a, &polygon_data->vertex_third);
        break;
    case 3:
        vec_mode = VM_QuadTextured;
        coord_a.x = (polygon_data->coordinate_first.x + polygon_data->coordinate_third.x) >> 1;
        coord_a.y = (polygon_data->coordinate_third.y + polygon_data->coordinate_first.y) >> 1;
        coord_a.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_first.z) >> 1;
        point_a.U = (polygon_data->vertex_first.U + polygon_data->vertex_third.U) >> 1;
        point_a.V = (polygon_data->vertex_third.V + polygon_data->vertex_first.V) >> 1;
        point_a.S = (polygon_data->vertex_third.S + polygon_data->vertex_first.S) >> 1;
        perspective(&coord_a, &point_a);
        world_draw_gpoly(&polygon_data->vertex_first, &polygon_data->vertex_second, &point_a);
        world_draw_gpoly(&point_a, &polygon_data->vertex_second, &polygon_data->vertex_third);
        break;
    case 4:
        vec_mode = VM_QuadTextured;
        coord_a.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_first.x) >> 1;
        coord_a.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_first.y) >> 1;
        coord_a.z = (polygon_data->coordinate_first.z + polygon_data->coordinate_second.z) >> 1;
        point_a.U = (polygon_data->vertex_first.U + polygon_data->vertex_second.U) >> 1;
        point_a.V = (polygon_data->vertex_second.V + polygon_data->vertex_first.V) >> 1;
        point_a.S = (polygon_data->vertex_second.S + polygon_data->vertex_first.S) >> 1;
        perspective(&coord_a, &point_a);
        coord_b.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_third.x) >> 1;
        coord_b.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_third.y) >> 1;
        coord_b.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_second.z) >> 1;
        point_b.U = (polygon_data->vertex_third.U + polygon_data->vertex_second.U) >> 1;
        point_b.V = (polygon_data->vertex_third.V + polygon_data->vertex_second.V) >> 1;
        point_b.S = (polygon_data->vertex_third.S + polygon_data->vertex_second.S) >> 1;
        perspective(&coord_b, &point_b);
        coord_c.x = (polygon_data->coordinate_first.x + polygon_data->coordinate_third.x) >> 1;
        coord_c.y = (polygon_data->coordinate_third.y + polygon_data->coordinate_first.y) >> 1;
        coord_c.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_first.z) >> 1;
        point_c.U = (polygon_data->vertex_first.U + polygon_data->vertex_third.U) >> 1;
        point_c.V = (polygon_data->vertex_third.V + polygon_data->vertex_first.V) >> 1;
        point_c.S = (polygon_data->vertex_third.S + polygon_data->vertex_first.S) >> 1;
        perspective(&coord_c, &point_c);
        world_draw_gpoly(&polygon_data->vertex_first, &point_a, &point_c);
        world_draw_gpoly(&point_a, &polygon_data->vertex_second, &point_b);
        world_draw_gpoly(&point_a, &point_b, &point_c);
        world_draw_gpoly(&point_c, &point_b, &polygon_data->vertex_third);
        break;
    case 5:
        vec_mode = VM_QuadTextured;
        coord_a.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_first.x) >> 1;
        coord_a.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_first.y) >> 1;
        coord_a.z = (polygon_data->coordinate_first.z + polygon_data->coordinate_second.z) >> 1;
        point_a.U = (polygon_data->vertex_first.U + polygon_data->vertex_second.U) >> 1;
        point_a.V = (polygon_data->vertex_second.V + polygon_data->vertex_first.V) >> 1;
        point_a.S = (polygon_data->vertex_second.S + polygon_data->vertex_first.S) >> 1;
        perspective(&coord_a, &point_a);
        coord_b.x = (coord_a.x + polygon_data->coordinate_first.x) >> 1;
        coord_b.y = (coord_a.y + polygon_data->coordinate_first.y) >> 1;
        coord_b.z = (coord_a.z + polygon_data->coordinate_first.z) >> 1;
        point_b.U = (point_a.U + polygon_data->vertex_first.U) >> 1;
        point_b.V = (point_a.V + polygon_data->vertex_first.V) >> 1;
        point_b.S = (point_a.S + polygon_data->vertex_first.S) >> 1;
        perspective(&coord_b, &point_b);
        coord_c.x = (coord_a.x + polygon_data->coordinate_second.x) >> 1;
        coord_c.y = (coord_a.y + polygon_data->coordinate_second.y) >> 1;
        coord_c.z = (coord_a.z + polygon_data->coordinate_second.z) >> 1;
        point_c.U = (point_a.U + polygon_data->vertex_second.U) >> 1;
        point_c.V = (point_a.V + polygon_data->vertex_second.V) >> 1;
        point_c.S = (point_a.S + polygon_data->vertex_second.S) >> 1;
        perspective(&coord_c, &point_c);
        world_draw_gpoly(&polygon_data->vertex_first, &point_b, &polygon_data->vertex_third);
        world_draw_gpoly(&point_b, &point_a, &polygon_data->vertex_third);
        world_draw_gpoly(&point_a, &point_c, &polygon_data->vertex_third);
        world_draw_gpoly(&point_c, &polygon_data->vertex_second, &polygon_data->vertex_third);
        break;
    case 6:
        vec_mode = VM_QuadTextured;
        coord_a.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_third.x) >> 1;
        coord_a.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_third.y) >> 1;
        coord_a.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_second.z) >> 1;
        point_a.U = (polygon_data->vertex_third.U + polygon_data->vertex_second.U) >> 1;
        point_a.V = (polygon_data->vertex_third.V + polygon_data->vertex_second.V) >> 1;
        point_a.S = (polygon_data->vertex_third.S + polygon_data->vertex_second.S) >> 1;
        perspective(&coord_a, &point_a);
        coord_b.x = (coord_a.x + polygon_data->coordinate_second.x) >> 1;
        coord_b.y = (coord_a.y + polygon_data->coordinate_second.y) >> 1;
        coord_b.z = (coord_a.z + polygon_data->coordinate_second.z) >> 1;
        point_b.U = (point_a.U + polygon_data->vertex_second.U) >> 1;
        point_b.V = (point_a.V + polygon_data->vertex_second.V) >> 1;
        point_b.S = (point_a.S + polygon_data->vertex_second.S) >> 1;
        perspective(&coord_b, &point_b);
        coord_c.x = (coord_a.x + polygon_data->coordinate_third.x) >> 1;
        coord_c.y = (coord_a.y + polygon_data->coordinate_third.y) >> 1;
        coord_c.z = (coord_a.z + polygon_data->coordinate_third.z) >> 1;
        point_c.U = (point_a.U + polygon_data->vertex_third.U) >> 1;
        point_c.V = (point_a.V + polygon_data->vertex_third.V) >> 1;
        point_c.S = (point_a.S + polygon_data->vertex_third.S) >> 1;
        perspective(&coord_c, &point_c);
        world_draw_gpoly(&polygon_data->vertex_first, &polygon_data->vertex_second, &point_b);
        world_draw_gpoly(&polygon_data->vertex_first, &point_b, &point_a);
        world_draw_gpoly(&polygon_data->vertex_first, &point_a, &point_c);
        world_draw_gpoly(&polygon_data->vertex_first, &point_c, &polygon_data->vertex_third);
        break;
    case 7:
        vec_mode = VM_QuadTextured;
        coord_a.x = (polygon_data->coordinate_first.x + polygon_data->coordinate_third.x) >> 1;
        coord_a.y = (polygon_data->coordinate_third.y + polygon_data->coordinate_first.y) >> 1;
        coord_a.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_first.z) >> 1;
        point_a.U = (polygon_data->vertex_first.U + polygon_data->vertex_third.U) >> 1;
        point_a.V = (polygon_data->vertex_third.V + polygon_data->vertex_first.V) >> 1;
        point_a.S = (polygon_data->vertex_third.S + polygon_data->vertex_first.S) >> 1;
        perspective(&coord_a, &point_a);
        coord_b.x = (coord_a.x + polygon_data->coordinate_third.x) >> 1;
        coord_b.y = (coord_a.y + polygon_data->coordinate_third.y) >> 1;
        coord_b.z = (coord_a.z + polygon_data->coordinate_third.z) >> 1;
        point_b.U = (point_a.U + polygon_data->vertex_third.U) >> 1;
        point_b.V = (point_a.V + polygon_data->vertex_third.V) >> 1;
        point_b.S = (point_a.S + polygon_data->vertex_third.S) >> 1;
        perspective(&coord_b, &point_b);
        coord_c.x = (coord_a.x + polygon_data->coordinate_first.x) >> 1;
        coord_c.y = (coord_a.y + polygon_data->coordinate_first.y) >> 1;
        coord_c.z = (coord_a.z + polygon_data->coordinate_first.z) >> 1;
        point_c.U = (point_a.U + polygon_data->vertex_first.U) >> 1;
        point_c.V = (point_a.V + polygon_data->vertex_first.V) >> 1;
        point_c.S = (point_a.S + polygon_data->vertex_first.S) >> 1;
        perspective(&coord_c, &point_c);
        world_draw_gpoly(&polygon_data->vertex_second, &polygon_data->vertex_third, &point_b);
        world_draw_gpoly(&polygon_data->vertex_second, &point_b, &point_a);
        world_draw_gpoly(&polygon_data->vertex_second, &point_a, &point_c);
        world_draw_gpoly(&polygon_data->vertex_second, &point_c, &polygon_data->vertex_first);
        break;
    case 8:
        vec_mode = VM_QuadTextured;
        coord_a.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_first.x) >> 1;
        coord_a.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_first.y) >> 1;
        coord_a.z = (polygon_data->coordinate_first.z + polygon_data->coordinate_second.z) >> 1;
        point_a.U = (polygon_data->vertex_first.U + polygon_data->vertex_second.U) >> 1;
        point_a.V = (polygon_data->vertex_second.V + polygon_data->vertex_first.V) >> 1;
        point_a.S = (polygon_data->vertex_second.S + polygon_data->vertex_first.S) >> 1;
        perspective(&coord_a, &point_a);
        coord_b.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_third.x) >> 1;
        coord_b.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_third.y) >> 1;
        coord_b.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_second.z) >> 1;
        point_b.U = (polygon_data->vertex_third.U + polygon_data->vertex_second.U) >> 1;
        point_b.V = (polygon_data->vertex_third.V + polygon_data->vertex_second.V) >> 1;
        point_b.S = (polygon_data->vertex_third.S + polygon_data->vertex_second.S) >> 1;
        perspective(&coord_b, &point_b);
        coord_c.x = (polygon_data->coordinate_first.x + polygon_data->coordinate_third.x) >> 1;
        coord_c.y = (polygon_data->coordinate_third.y + polygon_data->coordinate_first.y) >> 1;
        coord_c.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_first.z) >> 1;
        point_c.U = (polygon_data->vertex_first.U + polygon_data->vertex_third.U) >> 1;
        point_c.V = (polygon_data->vertex_third.V + polygon_data->vertex_first.V) >> 1;
        point_c.S = (polygon_data->vertex_third.S + polygon_data->vertex_first.S) >> 1;
        perspective(&coord_c, &point_c);
        coord_d.x = (coord_a.x + polygon_data->coordinate_first.x) >> 1;
        coord_d.y = (coord_a.y + polygon_data->coordinate_first.y) >> 1;
        coord_d.z = (coord_a.z + polygon_data->coordinate_first.z) >> 1;
        point_d.U = (point_a.U + polygon_data->vertex_first.U) >> 1;
        point_d.V = (point_a.V + polygon_data->vertex_first.V) >> 1;
        point_d.S = (point_a.S + polygon_data->vertex_first.S) >> 1;
        perspective(&coord_d, &point_d);
        coord_e.x = (coord_a.x + polygon_data->coordinate_second.x) >> 1;
        coord_e.y = (coord_a.y + polygon_data->coordinate_second.y) >> 1;
        coord_e.z = (coord_a.z + polygon_data->coordinate_second.z) >> 1;
        point_e.U = (point_a.U + polygon_data->vertex_second.U) >> 1;
        point_e.V = (point_a.V + polygon_data->vertex_second.V) >> 1;
        point_e.S = (point_a.S + polygon_data->vertex_second.S) >> 1;
        perspective(&coord_e, &point_e);
        world_draw_gpoly(&polygon_data->vertex_first, &point_d, &point_c);
        world_draw_gpoly(&point_d, &point_a, &point_c);
        world_draw_gpoly(&point_a, &point_e, &point_b);
        world_draw_gpoly(&point_e, &polygon_data->vertex_second, &point_b);
        world_draw_gpoly(&point_a, &point_b, &point_c);
        world_draw_gpoly(&point_c, &point_b, &polygon_data->vertex_third);
        break;
    case 9:
        vec_mode = VM_QuadTextured;
        coord_a.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_first.x) >> 1;
        coord_a.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_first.y) >> 1;
        coord_a.z = (polygon_data->coordinate_first.z + polygon_data->coordinate_second.z) >> 1;
        point_a.U = (polygon_data->vertex_first.U + polygon_data->vertex_second.U) >> 1;
        point_a.V = (polygon_data->vertex_second.V + polygon_data->vertex_first.V) >> 1;
        point_a.S = (polygon_data->vertex_second.S + polygon_data->vertex_first.S) >> 1;
        perspective(&coord_a, &point_a);
        coord_b.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_third.x) >> 1;
        coord_b.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_third.y) >> 1;
        coord_b.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_second.z) >> 1;
        point_b.U = (polygon_data->vertex_third.U + polygon_data->vertex_second.U) >> 1;
        point_b.V = (polygon_data->vertex_third.V + polygon_data->vertex_second.V) >> 1;
        point_b.S = (polygon_data->vertex_third.S + polygon_data->vertex_second.S) >> 1;
        perspective(&coord_b, &point_b);
        coord_c.x = (polygon_data->coordinate_first.x + polygon_data->coordinate_third.x) >> 1;
        coord_c.y = (polygon_data->coordinate_third.y + polygon_data->coordinate_first.y) >> 1;
        coord_c.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_first.z) >> 1;
        point_c.U = (polygon_data->vertex_first.U + polygon_data->vertex_third.U) >> 1;
        point_c.V = (polygon_data->vertex_third.V + polygon_data->vertex_first.V) >> 1;
        point_c.S = (polygon_data->vertex_third.S + polygon_data->vertex_first.S) >> 1;
        perspective(&coord_c, &point_c);
        coord_d.x = (coord_b.x + polygon_data->coordinate_second.x) >> 1;
        coord_d.y = (coord_b.y + polygon_data->coordinate_second.y) >> 1;
        coord_d.z = (coord_b.z + polygon_data->coordinate_second.z) >> 1;
        point_d.U = (point_b.U + polygon_data->vertex_second.U) >> 1;
        point_d.V = (point_b.V + polygon_data->vertex_second.V) >> 1;
        point_d.S = (point_b.S + polygon_data->vertex_second.S) >> 1;
        perspective(&coord_d, &point_d);
        coord_e.x = (coord_b.x + polygon_data->coordinate_third.x) >> 1;
        coord_e.y = (coord_b.y + polygon_data->coordinate_third.y) >> 1;
        coord_e.z = (coord_b.z + polygon_data->coordinate_third.z) >> 1;
        point_e.U = (point_b.U + polygon_data->vertex_third.U) >> 1;
        point_e.V = (point_b.V + polygon_data->vertex_third.V) >> 1;
        point_e.S = (point_b.S + polygon_data->vertex_third.S) >> 1;
        perspective(&coord_e, &point_e);
        world_draw_gpoly(&polygon_data->vertex_first, &point_a, &point_c);
        world_draw_gpoly(&point_a, &point_b, &point_c);
        world_draw_gpoly(&point_a, &polygon_data->vertex_second, &point_d);
        world_draw_gpoly(&point_a, &point_d, &point_b);
        world_draw_gpoly(&point_c, &point_b, &point_e);
        world_draw_gpoly(&point_c, &point_e, &polygon_data->vertex_third);
        break;
    case 10:
        vec_mode = VM_QuadTextured;
        coord_a.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_first.x) >> 1;
        coord_a.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_first.y) >> 1;
        coord_a.z = (polygon_data->coordinate_first.z + polygon_data->coordinate_second.z) >> 1;
        point_a.U = (polygon_data->vertex_first.U + polygon_data->vertex_second.U) >> 1;
        point_a.V = (polygon_data->vertex_second.V + polygon_data->vertex_first.V) >> 1;
        point_a.S = (polygon_data->vertex_second.S + polygon_data->vertex_first.S) >> 1;
        perspective(&coord_a, &point_a);
        coord_b.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_third.x) >> 1;
        coord_b.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_third.y) >> 1;
        coord_b.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_second.z) >> 1;
        point_b.U = (polygon_data->vertex_third.U + polygon_data->vertex_second.U) >> 1;
        point_b.V = (polygon_data->vertex_third.V + polygon_data->vertex_second.V) >> 1;
        point_b.S = (polygon_data->vertex_third.S + polygon_data->vertex_second.S) >> 1;
        perspective(&coord_b, &point_b);
        coord_c.x = (polygon_data->coordinate_first.x + polygon_data->coordinate_third.x) >> 1;
        coord_c.y = (polygon_data->coordinate_third.y + polygon_data->coordinate_first.y) >> 1;
        coord_c.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_first.z) >> 1;
        point_c.U = (polygon_data->vertex_first.U + polygon_data->vertex_third.U) >> 1;
        point_c.V = (polygon_data->vertex_third.V + polygon_data->vertex_first.V) >> 1;
        point_c.S = (polygon_data->vertex_third.S + polygon_data->vertex_first.S) >> 1;
        perspective(&coord_c, &point_c);
        coord_d.x = (coord_c.x + polygon_data->coordinate_third.x) >> 1;
        coord_d.y = (coord_c.y + polygon_data->coordinate_third.y) >> 1;
        coord_d.z = (coord_c.z + polygon_data->coordinate_third.z) >> 1;
        point_d.U = (point_c.U + polygon_data->vertex_third.U) >> 1;
        point_d.V = (point_c.V + polygon_data->vertex_third.V) >> 1;
        point_d.S = (point_c.S + polygon_data->vertex_third.S) >> 1;
        perspective(&coord_d, &point_d);
        coord_e.x = (coord_c.x + polygon_data->coordinate_first.x) >> 1;
        coord_e.y = (coord_c.y + polygon_data->coordinate_first.y) >> 1;
        coord_e.z = (coord_c.z + polygon_data->coordinate_first.z) >> 1;
        point_e.U = (point_c.U + polygon_data->vertex_first.U) >> 1;
        point_e.V = (point_c.V + polygon_data->vertex_first.V) >> 1;
        point_e.S = (point_c.S + polygon_data->vertex_first.S) >> 1;
        perspective(&coord_e, &point_e);
        world_draw_gpoly(&point_a, &polygon_data->vertex_second, &point_b);
        world_draw_gpoly(&point_a, &point_b, &point_c);
        world_draw_gpoly(&polygon_data->vertex_first, &point_a, &point_e);
        world_draw_gpoly(&point_e, &point_a, &point_c);
        world_draw_gpoly(&point_c, &point_b, &point_d);
        world_draw_gpoly(&point_d, &point_b, &polygon_data->vertex_third);
        break;
    case 11: // Flickers in 1st person (before flicker_fix() was applied)
        vec_mode = VM_QuadTextured;
        coord_a.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_first.x) >> 1;
        coord_a.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_first.y) >> 1;
        coord_a.z = (polygon_data->coordinate_first.z + polygon_data->coordinate_second.z) >> 1;
        point_a.U = (polygon_data->vertex_first.U + polygon_data->vertex_second.U) >> 1;
        point_a.V = (polygon_data->vertex_second.V + polygon_data->vertex_first.V) >> 1;
        point_a.S = (polygon_data->vertex_second.S + polygon_data->vertex_first.S) >> 1;
        perspective(&coord_a, &point_a);
        coord_b.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_third.x) >> 1;
        coord_b.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_third.y) >> 1;
        coord_b.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_second.z) >> 1;
        point_b.U = (polygon_data->vertex_third.U + polygon_data->vertex_second.U) >> 1;
        point_b.V = (polygon_data->vertex_third.V + polygon_data->vertex_second.V) >> 1;
        point_b.S = (polygon_data->vertex_third.S + polygon_data->vertex_second.S) >> 1;
        perspective(&coord_b, &point_b);
        coord_c.x = (polygon_data->coordinate_first.x + polygon_data->coordinate_third.x) >> 1;
        coord_c.y = (polygon_data->coordinate_third.y + polygon_data->coordinate_first.y) >> 1;
        coord_c.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_first.z) >> 1;
        point_c.U = (polygon_data->vertex_first.U + polygon_data->vertex_third.U) >> 1;
        point_c.V = (polygon_data->vertex_third.V + polygon_data->vertex_first.V) >> 1;
        point_c.S = (polygon_data->vertex_third.S + polygon_data->vertex_first.S) >> 1;
        perspective(&coord_c, &point_c);
        coord_d.x = (coord_a.x + polygon_data->coordinate_first.x) >> 1;
        coord_d.y = (coord_a.y + polygon_data->coordinate_first.y) >> 1;
        coord_d.z = (coord_a.z + polygon_data->coordinate_first.z) >> 1;
        point_d.U = (point_a.U + polygon_data->vertex_first.U) >> 1;
        point_d.V = (point_a.V + polygon_data->vertex_first.V) >> 1;
        point_d.S = (point_a.S + polygon_data->vertex_first.S) >> 1;
        perspective(&coord_d, &point_d);
        coord_e.x = (coord_a.x + polygon_data->coordinate_second.x) >> 1;
        coord_e.y = (coord_a.y + polygon_data->coordinate_second.y) >> 1;
        coord_e.z = (coord_a.z + polygon_data->coordinate_second.z) >> 1;
        point_e.U = (point_a.U + polygon_data->vertex_second.U) >> 1;
        point_e.V = (point_a.V + polygon_data->vertex_second.V) >> 1;
        point_e.S = (point_a.S + polygon_data->vertex_second.S) >> 1;
        perspective(&coord_e, &point_e);
        coord_d.x = (coord_b.x + polygon_data->coordinate_second.x) >> 1;
        coord_d.y = (coord_b.y + polygon_data->coordinate_second.y) >> 1;
        coord_d.z = (coord_b.z + polygon_data->coordinate_second.z) >> 1;
        point_f.U = (point_b.U + polygon_data->vertex_second.U) >> 1;
        point_f.V = (point_b.V + polygon_data->vertex_second.V) >> 1;
        point_f.S = (point_b.S + polygon_data->vertex_second.S) >> 1;
        perspective(&coord_d, &point_f);
        coord_e.x = (coord_b.x + polygon_data->coordinate_third.x) >> 1;
        coord_e.y = (coord_b.y + polygon_data->coordinate_third.y) >> 1;
        coord_e.z = (coord_b.z + polygon_data->coordinate_third.z) >> 1;
        point_g.U = (point_b.U + polygon_data->vertex_third.U) >> 1;
        point_g.V = (point_b.V + polygon_data->vertex_third.V) >> 1;
        point_g.S = (point_b.S + polygon_data->vertex_third.S) >> 1;
        perspective(&coord_e, &point_g);
        coord_d.x = (coord_c.x + polygon_data->coordinate_third.x) >> 1;
        coord_d.y = (coord_c.y + polygon_data->coordinate_third.y) >> 1;
        coord_d.z = (coord_c.z + polygon_data->coordinate_third.z) >> 1;
        point_h.U = (point_c.U + polygon_data->vertex_third.U) >> 1;
        point_h.V = (point_c.V + polygon_data->vertex_third.V) >> 1;
        point_h.S = (point_c.S + polygon_data->vertex_third.S) >> 1;
        perspective(&coord_d, &point_h);
        coord_e.x = (coord_c.x + polygon_data->coordinate_first.x) >> 1;
        coord_e.y = (coord_c.y + polygon_data->coordinate_first.y) >> 1;
        coord_e.z = (coord_c.z + polygon_data->coordinate_first.z) >> 1;
        point_i.U = (point_c.U + polygon_data->vertex_first.U) >> 1;
        point_i.V = (point_c.V + polygon_data->vertex_first.V) >> 1;
        point_i.S = (point_c.S + polygon_data->vertex_first.S) >> 1;
        perspective(&coord_e, &point_i);
        coord_d.x = (coord_a.x + coord_c.x) >> 1;
        coord_d.y = (coord_a.y + coord_c.y) >> 1;
        coord_d.z = (coord_a.z + coord_c.z) >> 1;
        point_j.U = (point_a.U + point_c.U) >> 1;
        point_j.V = (point_a.V + point_c.V) >> 1;
        point_j.S = (point_a.S + point_c.S) >> 1;
        perspective(&coord_d, &point_j);
        coord_e.x = (coord_a.x + coord_b.x) >> 1;
        coord_e.y = (coord_a.y + coord_b.y) >> 1;
        coord_e.z = (coord_a.z + coord_b.z) >> 1;
        point_k.U = (point_a.U + point_b.U) >> 1;
        point_k.V = (point_a.V + point_b.V) >> 1;
        point_k.S = (point_a.S + point_b.S) >> 1;
        perspective(&coord_e, &point_k);
        coord_d.x = (coord_b.x + coord_c.x) >> 1;
        coord_d.y = (coord_b.y + coord_c.y) >> 1;
        coord_d.z = (coord_b.z + coord_c.z) >> 1;
        point_l.U = (point_b.U + point_c.U) >> 1;
        point_l.V = (point_b.V + point_c.V) >> 1;
        point_l.S = (point_b.S + point_c.S) >> 1;
        perspective(&coord_d, &point_l);
        world_draw_gpoly(&polygon_data->vertex_first, &point_d, &point_i);
        world_draw_gpoly(&point_d, &point_a, &point_j);
        world_draw_gpoly(&point_a, &point_e, &point_k);
        world_draw_gpoly(&point_e, &polygon_data->vertex_second, &point_f);
        world_draw_gpoly(&point_d, &point_j, &point_i);
        world_draw_gpoly(&point_a, &point_k, &point_j);
        world_draw_gpoly(&point_e, &point_f, &point_k);
        world_draw_gpoly(&point_i, &point_j, &point_c);
        world_draw_gpoly(&point_j, &point_k, &point_l);
        world_draw_gpoly(&point_k, &point_f, &point_b);
        world_draw_gpoly(&point_j, &point_l, &point_c);
        world_draw_gpoly(&point_k, &point_b, &point_l);
        world_draw_gpoly(&point_c, &point_l, &point_h);
        world_draw_gpoly(&point_l, &point_b, &point_g);
        world_draw_gpoly(&point_l, &point_g, &point_h);
        world_draw_gpoly(&point_h, &point_g, &polygon_data->vertex_third);
        break;
    case 12:
        vec_mode = VM_SolidColor;
        vec_shade = (int64_t)clamp((polygon_data->vertex_third.S + polygon_data->vertex_second.S + polygon_data->vertex_first.S) / 3 >> 16, 0, 63);
        trig(&polygon_data->vertex_first, &polygon_data->vertex_second, &polygon_data->vertex_third);
        break;
    case 13:
        vec_mode = VM_SolidColor;
        vec_shade = (int64_t)clamp((polygon_data->vertex_third.S + polygon_data->vertex_second.S + polygon_data->vertex_first.S) / 3 >> 16, 0, 63);
        coord_a.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_first.x) >> 1;
        coord_a.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_first.y) >> 1;
        coord_a.z = (polygon_data->coordinate_first.z + polygon_data->coordinate_second.z) >> 1;
        point_a.U = (polygon_data->vertex_first.U + polygon_data->vertex_second.U) >> 1;
        point_a.V = (polygon_data->vertex_second.V + polygon_data->vertex_first.V) >> 1;
        perspective(&coord_a, &point_a);
        trig(&polygon_data->vertex_first, &point_a, &polygon_data->vertex_third);
        trig(&point_a, &polygon_data->vertex_second, &polygon_data->vertex_third);
        break;
    case 14:
        vec_mode = VM_SolidColor;
        vec_shade = (int64_t)clamp((polygon_data->vertex_third.S + polygon_data->vertex_second.S + polygon_data->vertex_first.S) / 3 >> 16, 0, 63);
        coord_a.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_third.x) >> 1;
        coord_a.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_third.y) >> 1;
        coord_a.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_second.z) >> 1;
        point_a.U = (polygon_data->vertex_third.U + polygon_data->vertex_second.U) >> 1;
        point_a.V = (polygon_data->vertex_third.V + polygon_data->vertex_second.V) >> 1;
        perspective(&coord_a, &point_a);
        trig(&polygon_data->vertex_first, &polygon_data->vertex_second, &point_a);
        trig(&polygon_data->vertex_first, &point_a, &polygon_data->vertex_third);
        break;
    case 15:
        vec_mode = VM_SolidColor;
        vec_shade = (int64_t)clamp((polygon_data->vertex_third.S + polygon_data->vertex_second.S + polygon_data->vertex_first.S) / 3 >> 16, 0, 63);
        coord_a.x = (polygon_data->coordinate_first.x + polygon_data->coordinate_third.x) >> 1;
        coord_a.y = (polygon_data->coordinate_third.y + polygon_data->coordinate_first.y) >> 1;
        coord_a.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_first.z) >> 1;
        point_a.U = (polygon_data->vertex_first.U + polygon_data->vertex_third.U) >> 1;
        point_a.V = (polygon_data->vertex_third.V + polygon_data->vertex_first.V) >> 1;
        perspective(&coord_a, &point_a);
        trig(&polygon_data->vertex_first, &polygon_data->vertex_second, &point_a);
        trig(&point_a, &polygon_data->vertex_second, &polygon_data->vertex_third);
        break;
    case 16:
        vec_mode = VM_SolidColor;
        vec_shade = (int64_t)clamp((polygon_data->vertex_third.S + polygon_data->vertex_second.S + polygon_data->vertex_first.S) / 3 >> 16, 0, 63);
        coord_a.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_first.x) >> 1;
        coord_a.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_first.y) >> 1;
        coord_a.z = (polygon_data->coordinate_first.z + polygon_data->coordinate_second.z) >> 1;
        point_a.U = (polygon_data->vertex_first.U + polygon_data->vertex_second.U) >> 1;
        point_a.V = (polygon_data->vertex_second.V + polygon_data->vertex_first.V) >> 1;
        perspective(&coord_a, &point_a);
        coord_b.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_third.x) >> 1;
        coord_b.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_third.y) >> 1;
        coord_b.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_second.z) >> 1;
        point_b.U = (polygon_data->vertex_third.U + polygon_data->vertex_second.U) >> 1;
        point_b.V = (polygon_data->vertex_third.V + polygon_data->vertex_second.V) >> 1;
        perspective(&coord_b, &point_b);
        coord_c.x = (polygon_data->coordinate_first.x + polygon_data->coordinate_third.x) >> 1;
        coord_c.y = (polygon_data->coordinate_third.y + polygon_data->coordinate_first.y) >> 1;
        coord_c.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_first.z) >> 1;
        point_c.U = (polygon_data->vertex_first.U + polygon_data->vertex_third.U) >> 1;
        point_c.V = (polygon_data->vertex_third.V + polygon_data->vertex_first.V) >> 1;
        perspective(&coord_c, &point_c);
        trig(&polygon_data->vertex_first, &point_a, &point_c);
        trig(&point_a, &polygon_data->vertex_second, &point_b);
        trig(&point_a, &point_b, &point_c);
        trig(&point_c, &point_b, &polygon_data->vertex_third);
        break;
    case 17:
        vec_mode = VM_SolidColor;
        vec_shade = (int64_t)clamp((polygon_data->vertex_third.S + polygon_data->vertex_second.S + polygon_data->vertex_first.S) / 3 >> 16, 0, 63);
        coord_a.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_first.x) >> 1;
        coord_a.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_first.y) >> 1;
        coord_a.z = (polygon_data->coordinate_first.z + polygon_data->coordinate_second.z) >> 1;
        point_a.U = (polygon_data->vertex_first.U + polygon_data->vertex_second.U) >> 1;
        point_a.V = (polygon_data->vertex_second.V + polygon_data->vertex_first.V) >> 1;
        perspective(&coord_a, &point_a);
        coord_b.x = (coord_a.x + polygon_data->coordinate_first.x) >> 1;
        coord_b.y = (coord_a.y + polygon_data->coordinate_first.y) >> 1;
        coord_b.z = (coord_a.z + polygon_data->coordinate_first.z) >> 1;
        point_b.U = (point_a.U + polygon_data->vertex_first.U) >> 1;
        point_b.V = (point_a.V + polygon_data->vertex_first.V) >> 1;
        perspective(&coord_b, &point_b);
        coord_c.x = (coord_a.x + polygon_data->coordinate_second.x) >> 1;
        coord_c.y = (coord_a.y + polygon_data->coordinate_second.y) >> 1;
        coord_c.z = (coord_a.z + polygon_data->coordinate_second.z) >> 1;
        point_c.U = (point_a.U + polygon_data->vertex_second.U) >> 1;
        point_c.V = (point_a.V + polygon_data->vertex_second.V) >> 1;
        perspective(&coord_c, &point_c);
        trig(&polygon_data->vertex_first, &point_b, &polygon_data->vertex_third);
        trig(&point_b, &point_a, &polygon_data->vertex_third);
        trig(&point_a, &point_c, &polygon_data->vertex_third);
        trig(&point_c, &polygon_data->vertex_second, &polygon_data->vertex_third);
        break;
    case 18:
        vec_mode = VM_SolidColor;
        vec_shade = (int64_t)clamp((polygon_data->vertex_third.S + polygon_data->vertex_second.S + polygon_data->vertex_first.S) / 3 >> 16, 0, 63);
        coord_a.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_third.x) >> 1;
        coord_a.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_third.y) >> 1;
        coord_a.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_second.z) >> 1;
        point_a.U = (polygon_data->vertex_third.U + polygon_data->vertex_second.U) >> 1;
        point_a.V = (polygon_data->vertex_third.V + polygon_data->vertex_second.V) >> 1;
        perspective(&coord_a, &point_a);
        coord_b.x = (coord_a.x + polygon_data->coordinate_second.x) >> 1;
        coord_b.y = (coord_a.y + polygon_data->coordinate_second.y) >> 1;
        coord_b.z = (coord_a.z + polygon_data->coordinate_second.z) >> 1;
        point_b.U = (point_a.U + polygon_data->vertex_second.U) >> 1;
        point_b.V = (point_a.V + polygon_data->vertex_second.V) >> 1;
        perspective(&coord_b, &point_b);
        coord_c.x = (coord_a.x + polygon_data->coordinate_third.x) >> 1;
        coord_c.y = (coord_a.y + polygon_data->coordinate_third.y) >> 1;
        coord_c.z = (coord_a.z + polygon_data->coordinate_third.z) >> 1;
        point_c.U = (point_a.U + polygon_data->vertex_third.U) >> 1;
        point_c.V = (point_a.V + polygon_data->vertex_third.V) >> 1;
        perspective(&coord_c, &point_c);
        trig(&polygon_data->vertex_first, &polygon_data->vertex_second, &point_b);
        trig(&polygon_data->vertex_first, &point_b, &point_a);
        trig(&polygon_data->vertex_first, &point_a, &point_c);
        trig(&polygon_data->vertex_first, &point_c, &polygon_data->vertex_third);
        break;
    case 19:
        vec_mode = VM_SolidColor;
        vec_shade = (int64_t)clamp((polygon_data->vertex_third.S + polygon_data->vertex_second.S + polygon_data->vertex_first.S) / 3 >> 16, 0, 63);
        coord_a.x = (polygon_data->coordinate_first.x + polygon_data->coordinate_third.x) >> 1;
        coord_a.y = (polygon_data->coordinate_third.y + polygon_data->coordinate_first.y) >> 1;
        coord_a.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_first.z) >> 1;
        point_a.U = (polygon_data->vertex_first.U + polygon_data->vertex_third.U) >> 1;
        point_a.V = (polygon_data->vertex_third.V + polygon_data->vertex_first.V) >> 1;
        perspective(&coord_a, &point_a);
        coord_b.x = (coord_a.x + polygon_data->coordinate_third.x) >> 1;
        coord_b.y = (coord_a.y + polygon_data->coordinate_third.y) >> 1;
        coord_b.z = (coord_a.z + polygon_data->coordinate_third.z) >> 1;
        point_b.U = (point_a.U + polygon_data->vertex_third.U) >> 1;
        point_b.V = (point_a.V + polygon_data->vertex_third.V) >> 1;
        perspective(&coord_b, &point_b);
        coord_c.x = (coord_a.x + polygon_data->coordinate_first.x) >> 1;
        coord_c.y = (coord_a.y + polygon_data->coordinate_first.y) >> 1;
        coord_c.z = (coord_a.z + polygon_data->coordinate_first.z) >> 1;
        point_c.U = (point_a.U + polygon_data->vertex_first.U) >> 1;
        point_c.V = (point_a.V + polygon_data->vertex_first.V) >> 1;
        perspective(&coord_c, &point_c);
        trig(&polygon_data->vertex_second, &polygon_data->vertex_third, &point_b);
        trig(&polygon_data->vertex_second, &point_b, &point_a);
        trig(&polygon_data->vertex_second, &point_a, &point_c);
        trig(&polygon_data->vertex_second, &point_c, &polygon_data->vertex_first);
        break;
    case 20:
        vec_mode = VM_SolidColor;
        vec_shade = (int64_t)clamp((polygon_data->vertex_third.S + polygon_data->vertex_second.S + polygon_data->vertex_first.S) / 3 >> 16, 0, 63);
        coord_a.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_first.x) >> 1;
        coord_a.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_first.y) >> 1;
        coord_a.z = (polygon_data->coordinate_first.z + polygon_data->coordinate_second.z) >> 1;
        point_a.U = (polygon_data->vertex_first.U + polygon_data->vertex_second.U) >> 1;
        point_a.V = (polygon_data->vertex_second.V + polygon_data->vertex_first.V) >> 1;
        perspective(&coord_a, &point_a);
        coord_b.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_third.x) >> 1;
        coord_b.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_third.y) >> 1;
        coord_b.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_second.z) >> 1;
        point_b.U = (polygon_data->vertex_third.U + polygon_data->vertex_second.U) >> 1;
        point_b.V = (polygon_data->vertex_third.V + polygon_data->vertex_second.V) >> 1;
        perspective(&coord_b, &point_b);
        coord_c.x = (polygon_data->coordinate_first.x + polygon_data->coordinate_third.x) >> 1;
        coord_c.y = (polygon_data->coordinate_third.y + polygon_data->coordinate_first.y) >> 1;
        coord_c.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_first.z) >> 1;
        point_c.U = (polygon_data->vertex_first.U + polygon_data->vertex_third.U) >> 1;
        point_c.V = (polygon_data->vertex_third.V + polygon_data->vertex_first.V) >> 1;
        perspective(&coord_c, &point_c);
        coord_d.x = (coord_a.x + polygon_data->coordinate_first.x) >> 1;
        coord_d.y = (coord_a.y + polygon_data->coordinate_first.y) >> 1;
        coord_d.z = (coord_a.z + polygon_data->coordinate_first.z) >> 1;
        point_d.U = (point_a.U + polygon_data->vertex_first.U) >> 1;
        point_d.V = (point_a.V + polygon_data->vertex_first.V) >> 1;
        perspective(&coord_d, &point_d);
        coord_e.x = (coord_a.x + polygon_data->coordinate_second.x) >> 1;
        coord_e.y = (coord_a.y + polygon_data->coordinate_second.y) >> 1;
        coord_e.z = (coord_a.z + polygon_data->coordinate_second.z) >> 1;
        point_e.U = (point_a.U + polygon_data->vertex_second.U) >> 1;
        point_e.V = (point_a.V + polygon_data->vertex_second.V) >> 1;
        perspective(&coord_e, &point_e);
        trig(&polygon_data->vertex_first, &point_d, &point_c);
        trig(&point_d, &point_a, &point_c);
        trig(&point_a, &point_e, &point_b);
        trig(&point_e, &polygon_data->vertex_second, &point_b);
        trig(&point_a, &point_b, &point_c);
        trig(&point_c, &point_b, &polygon_data->vertex_third);
        break;
    case 21:
        vec_mode = VM_SolidColor;
        vec_shade = (int64_t)clamp((polygon_data->vertex_third.S + polygon_data->vertex_second.S + polygon_data->vertex_first.S) / 3 >> 16, 0, 63);
        coord_a.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_first.x) >> 1;
        coord_a.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_first.y) >> 1;
        coord_a.z = (polygon_data->coordinate_first.z + polygon_data->coordinate_second.z) >> 1;
        point_a.U = (polygon_data->vertex_first.U + polygon_data->vertex_second.U) >> 1;
        point_a.V = (polygon_data->vertex_second.V + polygon_data->vertex_first.V) >> 1;
        perspective(&coord_a, &point_a);
        coord_b.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_third.x) >> 1;
        coord_b.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_third.y) >> 1;
        coord_b.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_second.z) >> 1;
        point_b.U = (polygon_data->vertex_third.U + polygon_data->vertex_second.U) >> 1;
        point_b.V = (polygon_data->vertex_third.V + polygon_data->vertex_second.V) >> 1;
        perspective(&coord_b, &point_b);
        coord_c.x = (polygon_data->coordinate_first.x + polygon_data->coordinate_third.x) >> 1;
        coord_c.y = (polygon_data->coordinate_third.y + polygon_data->coordinate_first.y) >> 1;
        coord_c.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_first.z) >> 1;
        point_c.U = (polygon_data->vertex_first.U + polygon_data->vertex_third.U) >> 1;
        point_c.V = (polygon_data->vertex_third.V + polygon_data->vertex_first.V) >> 1;
        perspective(&coord_c, &point_c);
        coord_d.x = (coord_b.x + polygon_data->coordinate_second.x) >> 1;
        coord_d.y = (coord_b.y + polygon_data->coordinate_second.y) >> 1;
        coord_d.z = (coord_b.z + polygon_data->coordinate_second.z) >> 1;
        point_d.U = (point_b.U + polygon_data->vertex_second.U) >> 1;
        point_d.V = (point_b.V + polygon_data->vertex_second.V) >> 1;
        perspective(&coord_d, &point_d);
        coord_e.x = (coord_b.x + polygon_data->coordinate_third.x) >> 1;
        coord_e.y = (coord_b.y + polygon_data->coordinate_third.y) >> 1;
        coord_e.z = (coord_b.z + polygon_data->coordinate_third.z) >> 1;
        point_e.U = (point_b.U + polygon_data->vertex_third.U) >> 1;
        point_e.V = (point_b.V + polygon_data->vertex_third.V) >> 1;
        perspective(&coord_e, &point_e);
        trig(&polygon_data->vertex_first, &point_a, &point_c);
        trig(&point_a, &point_b, &point_c);
        trig(&point_a, &polygon_data->vertex_second, &point_d);
        trig(&point_a, &point_d, &point_b);
        trig(&point_c, &point_b, &point_e);
        trig(&point_c, &point_e, &polygon_data->vertex_third);
        break;
    case 22:
        vec_mode = VM_SolidColor;
        vec_shade = (int64_t)clamp((polygon_data->vertex_third.S + polygon_data->vertex_second.S + polygon_data->vertex_first.S) / 3 >> 16, 0, 63);
        coord_a.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_first.x) >> 1;
        coord_a.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_first.y) >> 1;
        coord_a.z = (polygon_data->coordinate_first.z + polygon_data->coordinate_second.z) >> 1;
        point_a.U = (polygon_data->vertex_first.U + polygon_data->vertex_second.U) >> 1;
        point_a.V = (polygon_data->vertex_second.V + polygon_data->vertex_first.V) >> 1;
        perspective(&coord_a, &point_a);
        coord_b.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_third.x) >> 1;
        coord_b.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_third.y) >> 1;
        coord_b.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_second.z) >> 1;
        point_b.U = (polygon_data->vertex_third.U + polygon_data->vertex_second.U) >> 1;
        point_b.V = (polygon_data->vertex_third.V + polygon_data->vertex_second.V) >> 1;
        perspective(&coord_b, &point_b);
        coord_c.x = (polygon_data->coordinate_first.x + polygon_data->coordinate_third.x) >> 1;
        coord_c.y = (polygon_data->coordinate_third.y + polygon_data->coordinate_first.y) >> 1;
        coord_c.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_first.z) >> 1;
        point_c.U = (polygon_data->vertex_first.U + polygon_data->vertex_third.U) >> 1;
        point_c.V = (polygon_data->vertex_third.V + polygon_data->vertex_first.V) >> 1;
        perspective(&coord_c, &point_c);
        coord_d.x = (coord_c.x + polygon_data->coordinate_third.x) >> 1;
        coord_d.y = (coord_c.y + polygon_data->coordinate_third.y) >> 1;
        coord_d.z = (coord_c.z + polygon_data->coordinate_third.z) >> 1;
        point_d.U = (point_c.U + polygon_data->vertex_third.U) >> 1;
        point_d.V = (point_c.V + polygon_data->vertex_third.V) >> 1;
        perspective(&coord_d, &point_d);
        coord_e.x = (coord_c.x + polygon_data->coordinate_first.x) >> 1;
        coord_e.y = (coord_c.y + polygon_data->coordinate_first.y) >> 1;
        coord_e.z = (coord_c.z + polygon_data->coordinate_first.z) >> 1;
        point_e.U = (point_c.U + polygon_data->vertex_first.U) >> 1;
        point_e.V = (point_c.V + polygon_data->vertex_first.V) >> 1;
        perspective(&coord_e, &point_e);
        trig(&point_a, &polygon_data->vertex_second, &point_b);
        trig(&point_a, &point_b, &point_c);
        trig(&polygon_data->vertex_first, &point_a, &point_e);
        trig(&point_e, &point_a, &point_c);
        trig(&point_c, &point_b, &point_d);
        trig(&point_d, &point_b, &polygon_data->vertex_third);
        break;
    case 23:
        vec_mode = VM_SolidColor;
        vec_shade = (int64_t)clamp((polygon_data->vertex_third.S + polygon_data->vertex_second.S + polygon_data->vertex_first.S) / 3 >> 16, 0, 63);
        coord_a.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_first.x) >> 1;
        coord_a.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_first.y) >> 1;
        coord_a.z = (polygon_data->coordinate_first.z + polygon_data->coordinate_second.z) >> 1;
        point_a.U = (polygon_data->vertex_first.U + polygon_data->vertex_second.U) >> 1;
        point_a.V = (polygon_data->vertex_second.V + polygon_data->vertex_first.V) >> 1;
        perspective(&coord_a, &point_a);
        coord_b.x = (polygon_data->coordinate_second.x + polygon_data->coordinate_third.x) >> 1;
        coord_b.y = (polygon_data->coordinate_second.y + polygon_data->coordinate_third.y) >> 1;
        coord_b.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_second.z) >> 1;
        point_b.U = (polygon_data->vertex_third.U + polygon_data->vertex_second.U) >> 1;
        point_b.V = (polygon_data->vertex_third.V + polygon_data->vertex_second.V) >> 1;
        perspective(&coord_b, &point_b);
        coord_c.x = (polygon_data->coordinate_first.x + polygon_data->coordinate_third.x) >> 1;
        coord_c.y = (polygon_data->coordinate_third.y + polygon_data->coordinate_first.y) >> 1;
        coord_c.z = (polygon_data->coordinate_third.z + polygon_data->coordinate_first.z) >> 1;
        point_c.U = (polygon_data->vertex_first.U + polygon_data->vertex_third.U) >> 1;
        point_c.V = (polygon_data->vertex_third.V + polygon_data->vertex_first.V) >> 1;
        perspective(&coord_c, &point_c);
        coord_d.x = (coord_a.x + polygon_data->coordinate_first.x) >> 1;
        coord_d.y = (coord_a.y + polygon_data->coordinate_first.y) >> 1;
        coord_d.z = (coord_a.z + polygon_data->coordinate_first.z) >> 1;
        point_d.U = (point_a.U + polygon_data->vertex_first.U) >> 1;
        point_d.V = (point_a.V + polygon_data->vertex_first.V) >> 1;
        perspective(&coord_d, &point_d);
        coord_e.x = (coord_a.x + polygon_data->coordinate_second.x) >> 1;
        coord_e.y = (coord_a.y + polygon_data->coordinate_second.y) >> 1;
        coord_e.z = (coord_a.z + polygon_data->coordinate_second.z) >> 1;
        point_e.U = (point_a.U + polygon_data->vertex_second.U) >> 1;
        point_e.V = (point_a.V + polygon_data->vertex_second.V) >> 1;
        perspective(&coord_e, &point_e);
        coord_d.x = (coord_b.x + polygon_data->coordinate_second.x) >> 1;
        coord_d.y = (coord_b.y + polygon_data->coordinate_second.y) >> 1;
        coord_d.z = (coord_b.z + polygon_data->coordinate_second.z) >> 1;
        point_f.U = (point_b.U + polygon_data->vertex_second.U) >> 1;
        point_f.V = (point_b.V + polygon_data->vertex_second.V) >> 1;
        perspective(&coord_d, &point_f);
        coord_e.x = (coord_b.x + polygon_data->coordinate_third.x) >> 1;
        coord_e.y = (coord_b.y + polygon_data->coordinate_third.y) >> 1;
        coord_e.z = (coord_b.z + polygon_data->coordinate_third.z) >> 1;
        point_g.U = (point_b.U + polygon_data->vertex_third.U) >> 1;
        point_g.V = (point_b.V + polygon_data->vertex_third.V) >> 1;
        perspective(&coord_e, &point_g);
        coord_d.x = (coord_c.x + polygon_data->coordinate_third.x) >> 1;
        coord_d.y = (coord_c.y + polygon_data->coordinate_third.y) >> 1;
        coord_d.z = (coord_c.z + polygon_data->coordinate_third.z) >> 1;
        point_h.U = (point_c.U + polygon_data->vertex_third.U) >> 1;
        point_h.V = (point_c.V + polygon_data->vertex_third.V) >> 1;
        perspective(&coord_d, &point_h);
        coord_e.x = (coord_c.x + polygon_data->coordinate_first.x) >> 1;
        coord_e.y = (coord_c.y + polygon_data->coordinate_first.y) >> 1;
        coord_e.z = (coord_c.z + polygon_data->coordinate_first.z) >> 1;
        point_i.U = (point_c.U + polygon_data->vertex_first.U) >> 1;
        point_i.V = (point_c.V + polygon_data->vertex_first.V) >> 1;
        perspective(&coord_e, &point_i);
        coord_d.x = (coord_a.x + coord_c.x) >> 1;
        coord_d.y = (coord_a.y + coord_c.y) >> 1;
        coord_d.z = (coord_a.z + coord_c.z) >> 1;
        point_j.U = (point_a.U + point_c.U) >> 1;
        point_j.V = (point_a.V + point_c.V) >> 1;
        perspective(&coord_d, &point_j);
        coord_e.x = (coord_a.x + coord_b.x) >> 1;
        coord_e.y = (coord_a.y + coord_b.y) >> 1;
        coord_e.z = (coord_a.z + coord_b.z) >> 1;
        point_k.U = (point_a.U + point_b.U) >> 1;
        point_k.V = (point_a.V + point_b.V) >> 1;
        perspective(&coord_e, &point_k);
        coord_d.x = (coord_b.x + coord_c.x) >> 1;
        coord_d.y = (coord_b.y + coord_c.y) >> 1;
        coord_d.z = (coord_b.z + coord_c.z) >> 1;
        point_l.U = (point_b.U + point_c.U) >> 1;
        point_l.V = (point_b.V + point_c.V) >> 1;
        perspective(&coord_d, &point_l);
        trig(&polygon_data->vertex_first, &point_d, &point_i);
        trig(&point_d, &point_a, &point_j);
        trig(&point_a, &point_e, &point_k);
        trig(&point_e, &polygon_data->vertex_second, &point_f);
        trig(&point_d, &point_j, &point_i);
        trig(&point_a, &point_k, &point_j);
        trig(&point_e, &point_f, &point_k);
        trig(&point_i, &point_j, &point_c);
        trig(&point_j, &point_k, &point_l);
        trig(&point_k, &point_f, &point_b);
        trig(&point_j, &point_l, &point_c);
        trig(&point_k, &point_b, &point_l);
        trig(&point_c, &point_l, &point_h);
        trig(&point_l, &point_b, &point_g);
        trig(&point_l, &point_g, &point_h);
        trig(&point_h, &point_g, &polygon_data->vertex_third);
        break;
    default:
        render_problems++;
        render_prob_kind = polygon_data->b.kind;
        break;
    }

}
static void display_drawlist(void) // Draws isometric and 1st person view. Not frontview.
{
    struct PlayerInfo *player;
    const struct Camera *cam;
    union {
        struct BasicQ *b;
        struct BucketKindPolygonStandard *polygonStandard;
        struct BucketKindPolygonNearFP *polygonNearFP;
        struct BucketKindJontySprite *jontySprite;
        struct BucketKindCreatureShadow *creatureShadow;
        struct BucketKindSlabSelector *slabSelector;
        struct BucketKindCreatureStatus *creatureStatus;
        struct BucketKindTexturedQuad *texturedQuad;
        struct BucketKindFloatingGoldText *floatingGoldText;
        struct BucketKindRoomFlag *roomFlag;
    } item;
    int64_t bucket_num;
    SYNCDBG(9,"Starting");
    render_problems = 0;
    kfx_render_state.thing_pointed_at = 0;
    RPROF_BEGIN(RPS_DRAWLIST);
    RendererWorldFrameBegin();
    submit_perpixel_lighting();

    // The bucket list is the final step in drawing something to the screen. Visuals are added to the bucket list in previous functions.
    for (bucket_num = BUCKETS_COUNT-1; bucket_num > 0; bucket_num--)
    {
        if (buckets[bucket_num] != NULL)
            RendererWorldFrameSetDepth(bucket_num, BUCKETS_COUNT);
        for (item.b = buckets[bucket_num]; item.b != NULL; item.b = item.b->next)
        {
            //JUSTLOG("%d",(int)item.b->kind);
            switch ( item.b->kind )
            {
            case QK_PolygonStandard: // All textured polygons for isometric and 'far' textures in 1st person view
                vec_mode = VM_QuadTextured;
                vec_map = block_ptrs[item.polygonStandard->block];
                world_draw_gpoly(&item.polygonStandard->vertex_first, &item.polygonStandard->vertex_second, &item.polygonStandard->vertex_third);
                break;
            case QK_PolygonNearFP: // 'Near' textured polygons (closer to camera) in 1st person view
                draw_subdivided_near_polygon(item.polygonNearFP);
                break;
            case QK_JontySprite: // All creatures and things in isometric and 1st person view
                draw_jonty_mapwho(item.jontySprite);
                break;
            case QK_CreatureShadow: // Shadows of creatures in isometric and 1st person view
                // TODO: this could be cached
                draw_keepsprite_unscaled_in_buffer(item.creatureShadow->anim_sprite, item.creatureShadow->angle, item.creatureShadow->current_frame, big_scratch);
                vec_map = big_scratch;
                vec_mode = VM_SpriteTranslucent;
                vec_shade = (int64_t)clamp(item.creatureShadow->vertex_first.S, 0, 63);
                trig(&item.creatureShadow->vertex_first, &item.creatureShadow->vertex_second, &item.creatureShadow->vertex_third);
                trig(&item.creatureShadow->vertex_first, &item.creatureShadow->vertex_third, &item.creatureShadow->vertex_fourth);
                break;
            case QK_SlabSelector: // Selection outline box for placing/digging slabs
                draw_clipped_line(
                    item.slabSelector->p.X,
                    item.slabSelector->p.Y,
                    item.slabSelector->p.U,
                    item.slabSelector->p.V,
                    item.slabSelector->p.S);
                break;
            case QK_CreatureStatus: // Status flower above creature heads
                draw_status_sprites(item.creatureStatus->x, item.creatureStatus->y, item.creatureStatus->thing);
                break;
            case QK_FloatingGoldText: // Floating gold text when placing or selling a slab
                draw_engine_number(item.floatingGoldText);
                break;
            case QK_RoomFlagBottomPole: // The bottom pole part, doesn't affect the status sitting on top of the pole
                draw_engine_room_flagpole(item.roomFlag);
                break;
            case QK_JontyISOSprite: // Spinning key
                player = get_my_player();
                cam = get_local_active_camera(player);
                if (cam != NULL)
                {
                    if (cam->view_mode == PVM_IsoWibbleView || cam->view_mode == PVM_IsoStraightView) {
                        draw_jonty_mapwho(item.jontySprite);
                    }
                }
                break;
            case QK_RoomFlagStatusBox: // The status sitting on top of the pole
                draw_engine_room_flag_top(item.roomFlag);
                break;
            default:
                render_problems++;
                render_prob_kind = item.b->kind;
                break;
            }
        }
    }
    RendererWorldFrameEnd();
    RPROF_END(RPS_DRAWLIST);
    if (render_problems > 0)
      WARNLOG("Incurred %" PRIu64 " rendering problems; last was with poly kind %" PRId64,(uint64_t)(render_problems),(int64_t)(render_prob_kind));
}

static void prepare_draw_plane_of_engine_columns(struct Camera *cam, int64_t aposc, int64_t bposc, int64_t xcell, int64_t ycell, struct MinMax *mm)
{
    apos = aposc;
    bpos = bposc;
    back_ec = &ecs1[0];
    front_ec = &ecs2[0];
    if (lens_mode != 0)
    {
        fill_in_points_perspective(cam, xcell, ycell, mm);
    } else
    if (settings.video_cluedo_mode)
    {
        fill_in_points_cluedo(cam, xcell, ycell, mm);
    } else
    {
        fill_in_points_isometric(cam, xcell, ycell, mm);
    }
}

/**
 * Draws single plane of engine columns.
 *
 * @param aposc
 * @param bposc
 * @param xcell
 * @param ycell
 */
static void draw_plane_of_engine_columns(struct Camera *cam, int64_t aposc, int64_t bposc, int64_t xcell, int64_t ycell, struct MinMax *mm)
{
    struct EngineCol *ec;
    ec = front_ec;
    front_ec = back_ec;
    back_ec = ec;
    apos = aposc;
    bpos = bposc;
    if (lens_mode != 0)
    {
        fill_in_points_perspective(cam, xcell, ycell, mm);
        if (mm->min < mm->max)
        {
          apos = aposc;
          bpos = bposc;
          do_a_plane_of_engine_columns_perspective(xcell, ycell, mm->min, mm->max);
        }
    } else
    if ( settings.video_cluedo_mode )
    {
        fill_in_points_cluedo(cam, xcell, ycell, mm);
        if (mm->min < mm->max)
        {
          apos = aposc;
          bpos = bposc;
          do_a_plane_of_engine_columns_cluedo(xcell, ycell, mm->min, mm->max);
        }
    } else
    {
        fill_in_points_isometric(cam, xcell, ycell, mm);
        if (mm->min < mm->max)
        {
          apos = aposc;
          bpos = bposc;
          do_a_plane_of_engine_columns_isometric(xcell, ycell, mm->min, mm->max);
        }
    }
}

/**
 * Draws rectangular area of engine columns.
 * @param aposc
 * @param bposc
 * @param xcell
 * @param ycell
 */
static void draw_view_map_plane(struct Camera *cam, int64_t aposc, int64_t bposc, int64_t xcell, int64_t ycell)
{
    struct MinMax *mm;
    int64_t i;
    i = MINMAX_ALMOST_HALF-cells_away;
    if (i < 0)
        i = 0;
    mm = &minmaxs[i];
    prepare_draw_plane_of_engine_columns(cam, aposc, bposc, xcell, ycell, mm);

    for (i = 2*cells_away-1; i > 0; i--)
    {
        ycell++;
        bposc -= 256;
        mm++;
        draw_plane_of_engine_columns(cam, aposc, bposc, xcell, ycell, mm);
    }
}

void draw_view(struct Camera *cam, unsigned char a2)
{
    int64_t zoom_mem;
    int64_t xcell;
    int64_t ycell;
    int64_t i;
    int64_t aposc;
    int64_t bposc;
    SYNCDBG(9,"Starting");
    calculate_hud_scale(cam);
    camera_zoom = scale_camera_zoom_to_screen(cam->zoom);
    zoom_mem = cam->zoom;//TODO [zoom] remove when all cam->zoom will be changed to camera_zoom
    cam->zoom = camera_zoom;//TODO [zoom] remove when all cam->zoom will be changed to camera_zoom
    int64_t x = cam->mappos.x.val;
    int64_t y = cam->mappos.y.val;
    int64_t z = cam->mappos.z.val;

    // docs/refactor/editor/phase4/05-live-test-fixes.md -- second diagnostic
    // candidate for the zoom-out render dropout, now that the horizon-scan
    // clamp (compute_cells_away(), above) was confirmed NOT the cause (no
    // WARNLOG fired despite the dropout reproducing live). getpoly/
    // poly_pool_end gate dozens of separate terrain-column/polygon
    // insertion points throughout this file (get_bucket_item() plus many
    // inline "if (getpoly < poly_pool_end)" checks) -- if the pool fills up
    // partway through a frame's terrain generation, every later insertion
    // in iteration order silently no-ops, which could plausibly produce a
    // clean "everything past this point just doesn't render" cutoff.
    // Measures the frame that just finished (getpoly is about to reset) --
    // editor-only, new-peak-triggered (not a single threshold edge) so a
    // session's log shows the actual usage growth curve -- first live
    // result crossed 50% (8728656/16777216) with the dropout already
    // present, nowhere near exhaustion, so this may turn out to be a red
    // herring too; the peak curve will show whether it ever gets close.
    if (editorport_is_active() && (getpoly != NULL))
    {
        static size_t s_peak_used = 0;
        size_t used = (size_t)(getpoly - poly_pool);
        if (used > s_peak_used)
        {
            s_peak_used = used;
            WARNLOG("Editor poly pool new peak usage %" PRIu64 " / %" PRIu64 " bytes", (uint64_t)used, (uint64_t)sizeof(poly_pool));
        }
    }
    getpoly = poly_pool;
    memset(buckets, 0, sizeof(buckets));
    if (map_volume_box.visible)
    {
        poly_pool_end_reserve(14);
    }
    else
    {
        poly_pool_end_reserve(4);
    }
    i = lens_mode;
    if ((i < 0) || (i >= PERS_ROUTINES_COUNT))
    {
        i = 0;
    }

    perspective = perspective_routines[i];
    rotpers = rotpers_routines[i];
    update_fade_limits(cells_away);
    init_coords_and_rotation(&object_origin,&camera_matrix);
    rotate_base_axis(&camera_matrix, cam->rotation_angle_x, 2);
    update_normal_shade(&camera_matrix);
    rotate_base_axis(&camera_matrix, -cam->rotation_angle_y, 1);
    rotate_base_axis(&camera_matrix, -cam->rotation_angle_z, 3);
    cam_map_angle = cam->rotation_angle_x;
    map_roll = cam->rotation_angle_z;
    map_tilt = -cam->rotation_angle_y;

    frame_wibble_generate();
    view_alt = z;
    if (lens_mode != 0)
    { // 1st person
        // Clear the view to black: the terrain fades to (almost) black with distance, so anything
        // beyond the view range must match it -- the screen-wide clear colour (index 144, dark green)
        // showed through as a green rectangle behind the faded far wall.
        for (int64_t row = 0; row < vec_window_height; row++)
            memset(vec_screen + row * (int64_t)vec_screen_width, 0, (size_t)vec_window_width);
        cells_away = max_i_can_see;
        update_fade_limits(cells_away);
        fade_range = (fade_max - fade_min) >> 8;
        setup_rotate_stuff(x, y, z, fade_max, fade_min, lens, cam_map_angle, map_roll);
    }
    else
    { // isometric and straight view
        fade_min = 1000000;
        setup_rotate_stuff(x, y, z, fade_max, fade_min, camera_zoom/pixel_size, cam_map_angle, map_roll);
        do_perspective_rotation(x, y, z);
        cells_away = compute_cells_away();
    }

    xcell = (x >> 8);
    aposc = -(x & 0xFF);
    bposc = (cells_away << 8) + (y & 0xFF);
    ycell = (y >> 8) - (cells_away+1);
    find_gamut();
    fiddle_gamut(xcell, ycell + (cells_away+1));

    draw_view_map_plane(cam, aposc, bposc, xcell, ycell);

    if ( (map_volume_box.visible) && (!ui_game_is_busy_doing_gui()) )
    {
        poly_pool_end_reserve(0);
        process_isometric_map_volume_box(x, y, z, my_player_number);
    }

    display_drawlist();
    cam->zoom = zoom_mem;//TODO [zoom] remove when all cam->zoom will be changed to camera_zoom
    SYNCDBG(9,"Finished");
}

static void clear_fast_bucket_list(void)
{
    getpoly = poly_pool;
    memset(buckets, 0, sizeof(buckets));
}

static void draw_texturedquad_block(struct BucketKindTexturedQuad *txquad)
{
    struct PolyPoint point_a;
    struct PolyPoint point_b;
    struct PolyPoint point_c;
    struct PolyPoint point_d;
    vec_mode = VM_QuadTextured;
    switch (txquad->marked_mode) // Is visible/selected
    {
    case 0:
        vec_map = block_ptrs[TEXTURE_LAND_MARKED_LAND];
        break;
    case 1:
        vec_map = block_ptrs[TEXTURE_LAND_MARKED_GOLD];
        break;
    case 3:
    default:
        vec_map = block_ptrs[txquad->texture_idx];
        break;
    }
    point_a.X = (txquad->texture_x >> 8) / pixel_size;
    point_a.Y = (txquad->texture_y >> 8) / pixel_size;
    point_a.U = orient_to_mapU1[txquad->orient] + txquad->texture_scroll.x.val;
    point_a.V = orient_to_mapV1[txquad->orient] + txquad->texture_scroll.y.val;
    point_a.S = txquad->shade_intensity0;
    point_d.X = ((txquad->zoom_x + txquad->texture_x) >> 8) / pixel_size;
    point_d.Y = (txquad->texture_y >> 8) / pixel_size;
    point_d.U = orient_to_mapU2[txquad->orient] + txquad->texture_scroll.x.val;
    point_d.V = orient_to_mapV2[txquad->orient] + txquad->texture_scroll.y.val;
    point_d.S = txquad->shade_intensity1;
    point_b.X = ((txquad->zoom_x + txquad->texture_x) >> 8) / pixel_size;
    point_b.Y = ((txquad->zoom_y + txquad->texture_y) >> 8) / pixel_size;
    point_b.U = orient_to_mapU3[txquad->orient] + txquad->texture_scroll.x.val;
    point_b.V = orient_to_mapV3[txquad->orient] + txquad->texture_scroll.y.val;
    point_b.S = txquad->shade_intensity2;
    point_c.X = (txquad->texture_x >> 8) / pixel_size;
    point_c.Y = ((txquad->zoom_y + txquad->texture_y) >> 8) / pixel_size;
    point_c.U = orient_to_mapU4[txquad->orient] + txquad->texture_scroll.x.val;
    point_c.V = orient_to_mapV4[txquad->orient] + txquad->texture_scroll.y.val;
    point_c.S = txquad->shade_intensity3;
    world_draw_gpoly(&point_a, &point_d, &point_b);
    world_draw_gpoly(&point_a, &point_b, &point_c);
}

static void display_fast_drawlist(struct Camera *cam) // Draws frontview only. Not isometric or 1st person view.
{
    int64_t bucket_num;
    union {
        struct BasicQ *b;
        // Unused in display_fast_drawlist()
        struct BucketKindPolygonStandard *polygonStandard;
        struct BucketKindPolygonNearFP *polygonNearFP;
        struct BucketKindCreatureShadow *creatureShadow;
        // Used
        struct BucketKindJontySprite *jontySprite;
        struct BucketKindSlabSelector *slabSelector;
        struct BucketKindCreatureStatus *creatureStatus;
        struct BucketKindTexturedQuad *texturedQuad;
        struct BucketKindFloatingGoldText *floatingGoldText;
        struct BucketKindRoomFlag *roomFlag;
    } item;
    render_problems = 0;
    kfx_render_state.thing_pointed_at = 0;

    for (bucket_num = BUCKETS_COUNT-1; bucket_num >= 0; bucket_num--)
    {
        for (item.b = buckets[bucket_num]; item.b != NULL; item.b = item.b->next)
        {
            switch (item.b->kind)
            {
            case QK_JontySprite: // Creatures and things
                draw_fastview_mapwho(cam, item.jontySprite);
                break;
            case QK_SlabSelector: // Selection outline box for placing/digging slabs
                draw_clipped_line(
                    item.slabSelector->p.X,
                    item.slabSelector->p.Y,
                    item.slabSelector->p.U,
                    item.slabSelector->p.V,
                    item.slabSelector->p.S);
                break;
            case QK_CreatureStatus: // Status flower above creature heads
                draw_status_sprites(item.creatureStatus->x, item.creatureStatus->y, item.creatureStatus->thing);
                break;
            case QK_TextureQuad: // Textured polygons
                draw_texturedquad_block(item.texturedQuad);
                break;
            case QK_FloatingGoldText: // Floating gold text when placing or selling a slab
                draw_engine_number(item.floatingGoldText);
                break;
            case QK_RoomFlagBottomPole: // The bottom pole part, doesn't affect the status sitting on top of the pole
                draw_engine_room_flagpole(item.roomFlag);
                break;
            case QK_JontyISOSprite: // Spinning Key
                draw_iso_only_fastview_mapwho(cam, item.jontySprite);
                break;
            case QK_RoomFlagStatusBox: // The status sitting on top of the pole
                draw_engine_room_flag_top(item.roomFlag);
                break;
            default:
                render_problems++;
                render_prob_kind = item.b->kind;
                break;
            }
        }
    } // end for(bucket_num...
    if (render_problems > 0) {
        WARNLOG("Incurred %" PRIu64 " rendering problems; last was with poly kind %" PRId64,(uint64_t)(render_problems),(int64_t)(render_prob_kind));
    }
}

/**
 * sub of convert_world_coord_to_front_view_screen_coord for a single point
 *
 * @param player The player determine the point for
 * @param zoom The zoom level of the camera
 * @param vertical_delta The vertical difference between the camera and the pos
 * @param horizontal_delta The horizontal difference between the camera and the pos
 * @return true if projected point is withing player's window, false otherwise
 */

#define PPH_EVEN_ALIGN_MASK 0xFFFE
#define FRONTVIEW_BUCKET_MARGIN 1024
static TbBool project_point_helper(struct PlayerInfo *player, int64_t zoom, MapCoordDelta vertical_delta, MapCoordDelta horizontal_delta, MapCoord pos_z, int64_t *x_out, int64_t *y_out, int64_t *z_out)
{
    int64_t vertical_shift;
    int64_t new_zoom;
    int64_t window_width = local_state.engine_window_width;
    int64_t window_height = local_state.engine_window_height;

    *x_out = (zoom * horizontal_delta >> 16) + (*(int64_t *)&window_width / 2);
    vertical_shift = zoom * vertical_delta >> 8;
    *z_out = window_height - ((vertical_shift + ((int64_t)(window_height & PPH_EVEN_ALIGN_MASK) << 7)) >> 8) + FRONTVIEW_BUCKET_MARGIN;
    // prevent 32bit int overflow for the big sprites.
    new_zoom = ((int64_t)zoom * (int64_t)pos_z) << 7;
    *y_out = (int64_t)((vertical_shift + ((int64_t)(window_height & PPH_EVEN_ALIGN_MASK) << 7)
                        - (int64_t)(new_zoom >> 16)) >> 8);

    return (*x_out >= 0 && *x_out < window_width && *y_out >= 0 && *y_out < window_height);
}

/**
 * determines where on the screen an object should be drawn
 *
 * @param player The player determine the point for
 * @param cam The camera to use for the point
 * @param x_out The x position of the object relative to the camera
 * @param y_out The y position of the object relative to the camera
 * @param z_out The z position of the object relative to the camera
 * @return true if projected point is withing player's window, false otherwise
 */
static TbBool convert_world_coord_to_front_view_screen_coord(struct Coord3d* pos, struct Camera* cam, int64_t * x_out, int64_t * y_out, int64_t * z_out)
{
    int64_t zoom;
    uint64_t orientation;
    int64_t vertical_delta, horizontal_delta;
    int64_t result = 0;
    struct PlayerInfo* player = get_my_player();

    zoom = 32 * camera_zoom / 256;
    orientation = ((uint64_t)(cam->rotation_angle_x + DEGREES_45) / DEGREES_90) & 3;

    switch ( orientation )
    {
        case 0:
            vertical_delta = pos->y.val - cam->mappos.y.val;
            horizontal_delta = pos->x.val - cam->mappos.x.val;
            result = project_point_helper(player, zoom, vertical_delta, horizontal_delta, pos->z.val, x_out, y_out, z_out);
            break;

        case 1:
            vertical_delta = cam->mappos.x.val - pos->x.val;
            horizontal_delta = pos->y.val - cam->mappos.y.val;
            result = project_point_helper(player, zoom, vertical_delta, horizontal_delta, pos->z.val, x_out, y_out, z_out);
            break;

        case 2:
            vertical_delta = cam->mappos.y.val - pos->y.val;
            horizontal_delta = cam->mappos.x.val - pos->x.val;
            result = project_point_helper(player, zoom, vertical_delta, horizontal_delta, pos->z.val, x_out, y_out, z_out);
            break;

        case 3:
            vertical_delta = pos->x.val - cam->mappos.x.val;
            horizontal_delta = cam->mappos.y.val - pos->y.val;
            result = project_point_helper(player, zoom, vertical_delta, horizontal_delta, pos->z.val, x_out, y_out, z_out);
            break;
    }

    return result;
}

static void add_thing_sprite_to_polypool(struct Thing *thing, int64_t scr_x, int64_t scr_y, int64_t a4, int64_t bckt_idx)
{
    struct BucketKindJontySprite *poly;
    if (bckt_idx >= BUCKETS_COUNT)
        bckt_idx = BUCKETS_COUNT-1;
    else
    if (bckt_idx < 0)
        bckt_idx = 0;
    poly = (struct BucketKindJontySprite *)getpoly;
    getpoly += sizeof(struct BucketKindJontySprite);
    poly->b.next = buckets[bckt_idx];
    poly->b.kind = QK_JontySprite;
    buckets[bckt_idx] = (struct BasicQ *)poly;
    poly->thing = thing;
    if (pixel_size > 0)
    {
        poly->scr_x = scr_x / pixel_size;
        poly->scr_y = scr_y / pixel_size;
    }
    poly->depth_fade = a4;
}

static void add_spinning_key_to_polypool(struct Thing *thing, int64_t scr_x, int64_t scr_y, int64_t a4, int64_t bckt_idx)
{
    struct BucketKindJontySprite *poly;
    if (bckt_idx >= BUCKETS_COUNT)
      bckt_idx = BUCKETS_COUNT-1;
    else
    if (bckt_idx < 0)
      bckt_idx = 0;
    poly = (struct BucketKindJontySprite *)getpoly;
    getpoly += sizeof(struct BucketKindJontySprite);
    poly->b.next = buckets[bckt_idx];
    poly->b.kind = QK_JontyISOSprite;
    buckets[bckt_idx] = (struct BasicQ *)poly;
    poly->thing = thing;
    if (pixel_size > 0)
    {
      poly->scr_x = scr_x / pixel_size;
      poly->scr_y = scr_y / pixel_size;
    }
    poly->depth_fade = a4;
}

// Creature status flower above head in FrontView
static void create_status_box_element(struct Thing *thing, int64_t a2, int64_t a3, int64_t a4, int64_t bckt_idx) //
{
    struct BucketKindCreatureStatus *poly;
    if (bckt_idx >= BUCKETS_COUNT) {
      bckt_idx = BUCKETS_COUNT-1;
    } else
    if (bckt_idx < 0) {
      bckt_idx = 0;
    }
    poly = (struct BucketKindCreatureStatus *)getpoly;
    getpoly += sizeof(struct BucketKindCreatureStatus);
    poly->b.next = buckets[bckt_idx];
    poly->b.kind = QK_CreatureStatus;
    buckets[bckt_idx] = (struct BasicQ *)poly;
    poly->thing = thing;
    if (pixel_size > 0)
    {
        poly->x = a2 / pixel_size;
        poly->y = a3 / pixel_size;
    }
    poly->z = a4;
}

static void add_textruredquad_to_polypool(int64_t x, int64_t y, int64_t texture_idx, int64_t zoom, int64_t orient, int64_t lightness, int64_t marked_mode, int64_t bckt_idx)
{
    struct BucketKindTexturedQuad *poly;
    if (bckt_idx >= BUCKETS_COUNT) {
      bckt_idx = BUCKETS_COUNT-1;
    } else if (bckt_idx < 0) {
      bckt_idx = 0;
    }
    poly = (struct BucketKindTexturedQuad *)getpoly;
    getpoly += sizeof(struct BucketKindTexturedQuad);
    poly->b.next = buckets[bckt_idx];
    poly->b.kind = QK_TextureQuad;
    buckets[bckt_idx] = (struct BasicQ *)poly;

    poly->texture_idx = texture_idx;
    poly->texture_x = x;
    poly->texture_y = y;
    poly->texture_scroll = texture_scroll;
    poly->zoom_x = zoom;
    poly->zoom_y = zoom;
    poly->orient = orient;
    poly->shade_intensity0 = lightness;
    poly->shade_intensity1 = lightness;
    poly->shade_intensity2 = lightness;
    poly->shade_intensity3 = lightness;
    poly->marked_mode = marked_mode;
}

static void add_lgttextrdquad_to_polypool(int64_t x, int64_t y, int64_t texture_idx, int64_t zoom_x, int64_t zoom_y, int64_t orient, int64_t lg0, int64_t lg1, int64_t lg2, int64_t lg3, int64_t bckt_idx)
{
    struct BucketKindTexturedQuad *poly;
    if (bckt_idx >= BUCKETS_COUNT) {
      bckt_idx = BUCKETS_COUNT-1;
    } else if (bckt_idx < 0) {
      bckt_idx = 0;
    }
    poly = (struct BucketKindTexturedQuad *)getpoly;
    getpoly += sizeof(struct BucketKindTexturedQuad);
    poly->b.next = buckets[bckt_idx];
    poly->b.kind = QK_TextureQuad;
    buckets[bckt_idx] = (struct BasicQ *)poly;

    poly->texture_idx = texture_idx;
    poly->texture_x = x;
    poly->texture_y = y;
    poly->texture_scroll = texture_scroll;
    poly->zoom_x = zoom_x;
    poly->zoom_y = zoom_y;
    poly->orient = orient;
    poly->shade_intensity0 = lg0;
    poly->shade_intensity1 = lg1;
    poly->shade_intensity2 = lg2;
    poly->shade_intensity3 = lg3;
    poly->marked_mode = 3;
}

static void add_number_to_polypool(int64_t x, int64_t y, int64_t number, int64_t bckt_idx)
{
    struct BucketKindFloatingGoldText *poly;
    if (bckt_idx >= BUCKETS_COUNT) {
      bckt_idx = BUCKETS_COUNT-1;
    } else if (bckt_idx < 0) {
      bckt_idx = 0;
    }
    poly = (struct BucketKindFloatingGoldText *)getpoly;
    getpoly += sizeof(struct BucketKindFloatingGoldText);
    poly->b.next = buckets[bckt_idx];
    poly->b.kind = QK_FloatingGoldText;
    buckets[bckt_idx] = (struct BasicQ *)poly;
    if (pixel_size > 0)
    {
      poly->x = x / pixel_size;
      poly->y = y / pixel_size;
    }
    poly->lvl = number;
}

static void add_room_flag_pole_to_polypool(int64_t x, int64_t y, int64_t room_idx, int64_t bckt_idx)
{
    struct BucketKindRoomFlag *poly;
    if (bckt_idx >= BUCKETS_COUNT) {
      bckt_idx = BUCKETS_COUNT-1;
    } else if (bckt_idx < 0) {
      bckt_idx = 0;
    }
    poly = (struct BucketKindRoomFlag *)getpoly;
    getpoly += sizeof(struct BucketKindRoomFlag);
    poly->b.next = buckets[bckt_idx];
    poly->b.kind = QK_RoomFlagBottomPole;
    buckets[bckt_idx] = (struct BasicQ *)poly;
    if (pixel_size > 0)
    {
      poly->x = x / pixel_size;
      poly->y = y / pixel_size;
    }
    poly->lvl = room_idx;
}

static void add_room_flag_top_to_polypool(int64_t x, int64_t y, int64_t room_idx, int64_t bckt_idx)
{
    struct BucketKindRoomFlag *poly;
    if (bckt_idx >= BUCKETS_COUNT) {
      bckt_idx = BUCKETS_COUNT-1;
    } else if (bckt_idx < 0) {
      bckt_idx = 0;
    }
    poly = (struct BucketKindRoomFlag *)getpoly;
    getpoly += sizeof(struct BucketKindRoomFlag);
    poly->b.next = buckets[bckt_idx];
    poly->b.kind = QK_RoomFlagStatusBox;
    buckets[bckt_idx] = (struct BasicQ *)poly;
    if (pixel_size > 0)
    {
      poly->x = x / pixel_size;
      poly->y = y / pixel_size;
    }
    poly->lvl = room_idx;
}

static void prepare_lightness_intensity_array(int64_t stl_x, int64_t stl_y, int64_t *arrp, int64_t base_lightness)
{
    int64_t i;
    int64_t n;
    n = 4 * stl_x + 17 * stl_y;
    for (i=0; i < 9; i++)
    {
        int64_t rndi;
        int64_t nval;
        if ((base_lightness <= 256) || (base_lightness > 15872))
        {
            nval = base_lightness;
        } else
        {
            rndi = randomisors[n&RANDOMISORS_MASK];
            n++;
            nval = 32 * (rndi & 0x3F) + base_lightness - 256;
        }
        *arrp = nval << 8;
        arrp++;
    }
}

static void draw_element(struct Map *map, int64_t lightness, int64_t stl_x, int64_t stl_y, int64_t pos_x, int64_t pos_y, int64_t zoom, unsigned char qdrant, int64_t *ymax)
{
    struct PlayerInfo *myplyr;
    TbBool sibrevealed[3][3];
    struct CubeConfigStats *cube_config_stats;
    int64_t lightness_arr[4][9];
    int64_t bckt_idx;
    int64_t cube_itm;
    int64_t delta_y;
    int64_t tc; // top cube index
    int64_t x;
    int64_t y;
    int64_t i;
    myplyr = get_my_player();
    cube_itm = (qdrant + 2) & 3;
    delta_y = (zoom << 7) / 256;
    bckt_idx = local_state.engine_window_height - (pos_y >> 8) + FRONTVIEW_BUCKET_MARGIN;
    // Check if there's enough place to draw
    if (!is_free_space_in_poly_pool(8))
      return;

    // Prepare light intensity array

    for (y=0; y < 3; y++)
        for (x=0; x < 3; x++)
        {
            sibrevealed[y][x] = subtile_revealed(stl_x+x-1, stl_y+y-1, myplyr->id_number);
        }

    for (y = 0; y < 2; y++)
        for (x = 0; x < 2; x++) {
            i = 0;
            if (sibrevealed[y][x + 1] && sibrevealed[y + 1][x] && sibrevealed[y + 1][x + 1] && sibrevealed[y][x]) {
                if ((x == 0) && (y == 0)) {
                    i = lightness;
                } else {
                    i = get_subtile_lightness(&lish, stl_x + x, stl_y + y);
                }
            }
            prepare_lightness_intensity_array(stl_x + x, stl_y + y, lightness_arr[(-qdrant + x - y - 2 * x * y) & 3], i);
        }

    // Get column to be drawn on the current subtile

    struct Column *col;
    if (map_block_revealed(map, my_player_number))
      i = get_mapblk_column_index(map);
    else
      i = kfx_sim_state.unrevealed_column_idx;
    col = get_column(i);
    const struct Column *wall_col = get_abyss_wall_column(col, map, stl_x, stl_y);
    const TbBool abyss = cube_is_abyss(kfx_sim_state.top_cube[wall_col->floor_texture]);
    if (abyss)
        lightness_arr[0][0] = lightness_arr[1][0] = lightness_arr[2][0] = lightness_arr[3][0] = TO_FIXED(kfx_sim_state.light_registry.global_ambient_light);
    int64_t textr_idx;
    // Draw the columns base block

    if (!abyss && (*ymax > pos_y) && (col->floor_texture != 0) && (col->cubes[0] == 0)) {
        *ymax = pos_y;
        if ((map->flags & SlbAtFlg_Unexplored) != 0) {
            add_textruredquad_to_polypool(pos_x, pos_y, engine_remap_texture_blocks(stl_x, stl_y, col->floor_texture), zoom, 0,
                TO_FIXED(32), 0, bckt_idx);
        } else {
            textr_idx = engine_remap_top_texture_blocks(stl_x, stl_y, col->floor_texture);
            add_lgttextrdquad_to_polypool(pos_x, pos_y, textr_idx, zoom, zoom, 0,
                lightness_arr[0][0], lightness_arr[1][0], lightness_arr[2][0], lightness_arr[3][0], bckt_idx);
        }
    }

    // Draw the columns cubes

    int64_t bckt_face = bckt_idx;
    int64_t bckt_top = bckt_idx;
    MapSubtlCoord sstl_x = stl_x + x_step1[qdrant];
    MapSubtlCoord sstl_y = stl_y + y_step1[qdrant];
    struct Map *smapblk = get_map_block_at(sstl_x, sstl_y);
    int64_t cube_id = get_column_top_cube(wall_col);
    TbBool smap_revealed = map_block_revealed(smapblk, my_player_number);
    if (((map->flags & SlbAtFlg_Blocking) != 0)
     && (get_column_floor_filled_subtiles(col) >= 3))
    {
        bckt_face = bckt_idx - (zoom >> 8);
        bckt_top = bckt_face;
        if (((smapblk->flags & SlbAtFlg_Blocking) != 0)
         && (!smap_revealed
          || (get_floor_filled_subtiles_at(sstl_x, sstl_y) >= 3))) {
            bckt_top = bckt_face - 2 * (zoom >> 8);
        }
    }
    const size_t abyss_pool_size = (ABYSS_WALL_RENDER_HEIGHT + 1) * sizeof(struct BucketKindTexturedQuad) + 8 * sizeof(struct BucketKindSlabSelector);
    if (!abyss && smap_revealed && map_block_has_rendered_abyss(smapblk, sstl_x, sstl_y) && !cube_is_abyss(cube_id) && (getpoly + abyss_pool_size <= poly_pool_end)) {
        textr_idx = engine_remap_abyss_wall_texture_blocks(stl_x, stl_y, cube_id, cube_itm);
        for (i = 0; i < ABYSS_WALL_RENDER_HEIGHT; i++) {
            add_lgttextrdquad_to_polypool(pos_x, pos_y + zoom + i * delta_y, textr_idx, zoom, delta_y, 0,
                ABYSS_SHADE(lightness_arr[3][0], i), ABYSS_SHADE(lightness_arr[2][0], i),
                ABYSS_SHADE(lightness_arr[2][0], i + 1), ABYSS_SHADE(lightness_arr[3][0], i + 1), bckt_face);
        }
        add_lgttextrdquad_to_polypool(pos_x, pos_y + zoom + i * delta_y, textr_idx, zoom, (ABYSS_DEPTH - i) * delta_y, 0, 0, 0, 0, 0, bckt_face);
    }

    y = zoom + pos_y;
    cube_config_stats = NULL;
    for (tc=0; tc < COLUMN_STACK_HEIGHT; tc++)
    {
      if (col->cubes[tc] == 0)
        break;
      y -= delta_y;
      cube_config_stats = get_cube_model_stats(col->cubes[tc]);
      if (*ymax > y)
      {
        *ymax = y;
        textr_idx = engine_remap_texture_blocks(stl_x, stl_y, cube_config_stats->texture_id[cube_itm]);
        add_lgttextrdquad_to_polypool(pos_x, y, textr_idx, zoom, delta_y, 0,
            lightness_arr[3][tc+1], lightness_arr[2][tc+1], lightness_arr[2][tc], lightness_arr[3][tc], bckt_face);
      }
    }

    if (cube_config_stats != NULL)
    {
      i = y - zoom;
      if (*ymax > i)
      {
        *ymax = i;
        if ((map->flags & SlbAtFlg_TaggedValuable) != 0)
        {
          add_textruredquad_to_polypool(pos_x, i, engine_remap_texture_blocks(stl_x, stl_y, cube_config_stats->texture_id[4]), zoom, qdrant, TO_FIXED(32), 1, bckt_top);
        } else
        if ((map->flags & SlbAtFlg_Unexplored) != 0)
        {
          add_textruredquad_to_polypool(pos_x, i, engine_remap_texture_blocks(stl_x, stl_y, cube_config_stats->texture_id[4]), zoom, qdrant, TO_FIXED(32), 0, bckt_top);
        } else
        {
          textr_idx = engine_remap_top_texture_blocks(stl_x, stl_y, cube_config_stats->texture_id[4]);
          add_lgttextrdquad_to_polypool(pos_x, i, textr_idx, zoom, zoom, qdrant,
              lightness_arr[0][tc], lightness_arr[1][tc], lightness_arr[2][tc], lightness_arr[3][tc], bckt_top);
        }
      }
    }

    // If there are still some solid cubes higher than tc
    if ((get_column_ceiling_filled_subtiles(col) != 0) && (col->solidmask > (1 << tc)))
    {
        // Find any top cube separated by empty space
        for (;tc < COLUMN_STACK_HEIGHT; tc++)
        {
            if (col->cubes[tc] != 0)
              break;
            y -= delta_y;
        }

        for (;tc < COLUMN_STACK_HEIGHT; tc++)
        {
            if (col->cubes[tc] == 0)
              break;
            y -= delta_y;
            cube_config_stats = get_cube_model_stats(col->cubes[tc]);
            if (*ymax > y)
            {
              textr_idx = engine_remap_texture_blocks(stl_x, stl_y, cube_config_stats->texture_id[cube_itm]);
              add_lgttextrdquad_to_polypool(pos_x, y, textr_idx, zoom, delta_y, 0,
                  lightness_arr[3][tc+1], lightness_arr[2][tc+1], lightness_arr[2][tc], lightness_arr[3][tc], bckt_face);
            }
        }
        if (cube_config_stats != NULL)
        {
          i = y - zoom;
          if (*ymax > i)
          {
              textr_idx = engine_remap_top_texture_blocks(stl_x, stl_y, cube_config_stats->texture_id[4]);
            add_lgttextrdquad_to_polypool(pos_x, i, textr_idx, zoom, zoom, qdrant,
                lightness_arr[0][tc], lightness_arr[1][tc], lightness_arr[2][tc], lightness_arr[3][tc], bckt_top);
          }
        }
    }

}

static int64_t get_thing_shade(struct Thing* thing)
{
    MapSubtlCoord stl_x;
    MapSubtlCoord stl_y;
    int64_t minimum_lightness = kfx_config_state.conf.rules[thing->owner].gameplay.thing_minimum_illumination << 8;
    int64_t lgh[2][2]; // the dimensions are lgh[y][x]
    int64_t shval;
    int64_t fract_x;
    int64_t fract_y;
    stl_x = thing->mappos.x.stl.num;
    stl_y = thing->mappos.y.stl.num;
    fract_x = thing->mappos.x.stl.pos;
    fract_y = thing->mappos.y.stl.pos;
    lgh[0][0] = get_subtile_lightness(&lish,stl_x,  stl_y);
    lgh[0][1] = get_subtile_lightness(&lish,stl_x+1,stl_y);
    lgh[1][0] = get_subtile_lightness(&lish,stl_x,  stl_y+1);
    lgh[1][1] = get_subtile_lightness(&lish,stl_x+1,stl_y+1);
    shval = (fract_x
        * (lgh[0][1] + (fract_y * (lgh[1][1] - lgh[0][1]) >> 8)
        - (lgh[0][0] + (fract_y * (lgh[1][0] - lgh[0][0]) >> 8))) >> 8)
        + (lgh[0][0] + (fract_y * (lgh[1][0] - lgh[0][0]) >> 8));
    if (shval < minimum_lightness)
    {
        shval += (minimum_lightness >>2);
        if (shval > minimum_lightness)
            shval = minimum_lightness;
    } else
    {
        // Max lightness value - make sure it won't exceed our limits
        if (shval > 64*256+255)
            shval = 64*256+255;
    }
    if (thing_is_creature(thing) && flag_is_set(thing->state_flags, TF1_FallingIntoAbyss)) {
        shval = shval * max(subtile_coord(ABYSS_DEPTH, 0) + thing->mappos.z.val, 0) / subtile_coord(ABYSS_DEPTH, 0);
    }
    return shval;
}

static int64_t load_single_frame(TbSpriteData *data_ptr, int64_t kspr_idx)
{
    int64_t nlength;
    nlength = creature_table[kspr_idx+1].DataOffset - creature_table[kspr_idx].DataOffset;
    *data_ptr = game_he_alloc(nlength);

    LbFileSeek(jty_file_handle, creature_table[kspr_idx].DataOffset, 0);
    LbFileRead(jty_file_handle, *data_ptr, nlength);

    keepsprite[kspr_idx] = data_ptr;
    return 1;
}

static int64_t load_keepersprite_if_needed(int64_t kspr_idx)
{
    int64_t frame_num;
    int64_t frame_count;
    struct KeeperSprite *kspr_arr;
    kspr_arr = &creature_table[kspr_idx];
    if (kspr_arr->Rotable) {
        frame_count = 5 * kspr_arr->FramesCount;
    } else {
        frame_count = kspr_arr->FramesCount;
    }
    for (frame_num=0; frame_num < frame_count; frame_num++)
    {
        TbSpriteData *sprite_data_ptr = &sprite_heap_handle[kspr_idx+frame_num];
        if ((*sprite_data_ptr) == NULL)
        {
            if (!load_single_frame(sprite_data_ptr, kspr_idx+frame_num))
            {
                return 0;
            }
        }
    }
    return 1;
}

static int64_t heap_manage_keepersprite(int64_t kspr_idx)
{
    int64_t result;
    if (kspr_idx >= KEEPERSPRITE_ADD_OFFSET)
        return 1;
    result = load_keepersprite_if_needed(kspr_idx);
    return result;
}

static void draw_keepersprite(int64_t x, int64_t y, const struct KeeperSprite * kspr, int64_t kspr_idx)
{
    if ((kspr_idx < 0)
        || ((kspr_idx >= KEEPSPRITE_LENGTH) && (kspr_idx < KEEPERSPRITE_ADD_OFFSET))
        || (kspr_idx > (KEEPERSPRITE_ADD_NUM + KEEPERSPRITE_ADD_OFFSET))) {
        WARNDBG(9,"Invalid KeeperSprite %" PRId64 " at (%" PRId64 ",%" PRId64 ") size (%" PRIu64 ",%" PRIu64 ") alpha %" PRId64,
            (int64_t)(kspr_idx), (int64_t)(x), (int64_t)(y), (uint64_t)(kspr->SWidth), (uint64_t)(kspr->SHeight), (int64_t)EngineSpriteDrawUsingAlpha);
        return;
    }
    SYNCDBG(17,"Drawing %" PRId64 " at (%" PRId64 ",%" PRId64 ") size (%" PRIu64 ",%" PRIu64 ") alpha %" PRId64,
        (int64_t)(kspr_idx), (int64_t)(x), (int64_t)(y), (uint64_t)(kspr->SWidth), (uint64_t)(kspr->SHeight), (int64_t)EngineSpriteDrawUsingAlpha);
    const int64_t clipped_height = kspr->SHeight - water_source_cutoff;
    if (clipped_height <= 0) {
        return;
    }
    const TbSpriteData * sprite_data_ptr = NULL;
    if (kspr_idx >= 0) {
        if (kspr_idx >= KEEPERSPRITE_ADD_OFFSET) {
            if (kspr_idx - KEEPERSPRITE_ADD_OFFSET < KEEPERSPRITE_ADD_NUM) {
                sprite_data_ptr = &keepersprite_add[kspr_idx - KEEPERSPRITE_ADD_OFFSET];
            }
        } else if (kspr_idx < KEEPSPRITE_LENGTH) {
            sprite_data_ptr = keepsprite[kspr_idx];
        }
    }
    if (sprite_data_ptr == NULL || *sprite_data_ptr == NULL) {
        WARNDBG(9,"Unallocated KeeperSprite %" PRId64 " can't be drawn at (%" PRId64 ",%" PRId64 ")",(int64_t)(kspr_idx),(int64_t)(x),(int64_t)(y));
        return;
    }
    const struct TbSourceBuffer buffer = {
        *sprite_data_ptr,
        kspr->SWidth,
        clipped_height,
        kspr->SWidth,
    };
    if ( EngineSpriteDrawUsingAlpha ) {
        DrawAlphaSpriteUsingScalingData(x, y, &buffer);
    } else {
        LbSpriteDrawUsingScalingData(x, y, &buffer);
    }
    SYNCDBG(18,"Finished");
}

static void set_thing_pointed_at(struct Thing *thing)
{
    if (kfx_render_state.thing_pointed_at == NULL) {
        kfx_render_state.thing_pointed_at = thing;
    }
}

static void draw_single_keepersprite_omni_xflip(int64_t kspos_x, int64_t kspos_y, struct KeeperSprite *kspr, int64_t kspr_idx, int64_t scale)
{
    int64_t src_dy = (int64_t)kspr->FrameHeight;
    int64_t src_dx = (int64_t)kspr->FrameWidth;
    int64_t x = src_dx - (int64_t)kspr->FrameOffsW - (int64_t)kspr->SWidth;
    int64_t y = kspr->FrameOffsH;
    int64_t sp_dy = (src_dy * scale) >> 5;
    int64_t sp_dx = (src_dx * scale) >> 5;
    LbSpriteSetScalingData(kspos_x, kspos_y, src_dx, src_dy, sp_dx, sp_dy);
    if ( thing_being_displayed_is_creature )
    {
      if ( (kfx_render_state.pointer_x >= kspos_x) && (kfx_render_state.pointer_x <= sp_dx + kspos_x) )
      {
          if ( (kfx_render_state.pointer_y >= kspos_y) && (kfx_render_state.pointer_y <= sp_dy + kspos_y) )
          {
              set_thing_pointed_at(thing_being_displayed);
          }
      }
    }
    draw_keepersprite(x, y, kspr, kspr_idx);
}

static void draw_single_keepersprite_omni(int64_t kspos_x, int64_t kspos_y, struct KeeperSprite *kspr, int64_t kspr_idx, int64_t scale)
{
    int64_t src_dy = (int64_t)kspr->FrameHeight;
    int64_t src_dx = (int64_t)kspr->FrameWidth;
    int64_t x = kspr->FrameOffsW;
    int64_t y = kspr->FrameOffsH;
    int64_t sp_dy = (src_dy * scale) >> 5;
    int64_t sp_dx = (src_dx * scale) >> 5;
    LbSpriteSetScalingData(kspos_x, kspos_y, src_dx, src_dy, sp_dx, sp_dy);
    if ( thing_being_displayed_is_creature )
    {
      if ( (kfx_render_state.pointer_x >= kspos_x) && (kfx_render_state.pointer_x <= sp_dx + kspos_x) )
      {
          if ( (kfx_render_state.pointer_y >= kspos_y) && (kfx_render_state.pointer_y <= sp_dy + kspos_y) )
          {
              set_thing_pointed_at(thing_being_displayed);
          }
      }
    }
    draw_keepersprite(x, y, kspr, kspr_idx);
}

static void draw_single_keepersprite_xflip(int64_t kspos_x, int64_t kspos_y, struct KeeperSprite *kspr, int64_t kspr_idx, int64_t scale)
{
    SYNCDBG(18,"Starting");
    int64_t src_dy = (int64_t)kspr->SHeight;
    int64_t src_dx = (int64_t)kspr->SWidth;
    int64_t x = (int64_t)kspr->FrameWidth - (int64_t)kspr->FrameOffsW - src_dx;
    int64_t y = kspr->FrameOffsH;
    int64_t sp_x = kspos_x + ((scale * x) >> 5);
    int64_t sp_y = kspos_y + ((scale * y) >> 5);
    int64_t sp_dy = (src_dy * scale) >> 5;
    int64_t sp_dx = (src_dx * scale) >> 5;
    LbSpriteSetScalingData(sp_x, sp_y, src_dx, src_dy, sp_dx, sp_dy);
    if ( thing_being_displayed_is_creature )
    {
      if ( (kfx_render_state.pointer_x >= sp_x) && (kfx_render_state.pointer_x <= sp_dx + sp_x) )
      {
          if ( (kfx_render_state.pointer_y >= sp_y) && (kfx_render_state.pointer_y <= sp_dy + sp_y) )
          {
              set_thing_pointed_at(thing_being_displayed);
          }
      }
    }
    draw_keepersprite(0, 0, kspr, kspr_idx);
    SYNCDBG(18,"Finished");
}

static void draw_single_keepersprite(int64_t kspos_x, int64_t kspos_y, struct KeeperSprite *kspr, int64_t kspr_idx, int64_t scale)
{
    SYNCDBG(18,"Starting");
    int64_t src_dy = (int64_t)kspr->SHeight;
    int64_t src_dx = (int64_t)kspr->SWidth;
    int64_t x = kspr->FrameOffsW;
    int64_t y = kspr->FrameOffsH;
    int64_t sp_x = kspos_x + ((scale * x) >> 5);
    int64_t sp_y = kspos_y + ((scale * y) >> 5);
    int64_t sp_dy = (src_dy * scale) >> 5;
    int64_t sp_dx = (src_dx * scale) >> 5;
    LbSpriteSetScalingData(sp_x, sp_y, src_dx, src_dy, sp_dx, sp_dy);
    if ( thing_being_displayed_is_creature )
    {
        if ( (kfx_render_state.pointer_x >= x) && (kfx_render_state.pointer_x <= sp_dx + x) )
        {
            if ( (kfx_render_state.pointer_y >= y) && (kfx_render_state.pointer_y <= sp_dy + y) )
            {
                set_thing_pointed_at(thing_being_displayed);
            }
        }
    }
    draw_keepersprite(0, 0, kspr, kspr_idx);
    SYNCDBG(18,"Finished");
}

void process_keeper_sprite(int64_t x, int64_t y, int64_t kspr_base, int64_t kspr_angle, unsigned char sprgroup, int64_t scale)
{
    struct KeeperSprite *creature_sprites;
    struct PlayerInfo *player;
    struct CreatureControl *cctrl;
    struct KeeperSprite *kspr;
    int64_t kspr_idx;
    int64_t draw_idx;
    int64_t dim_ow;
    int64_t dim_oh;
    int64_t dim_th;
    int64_t dim_tw;
    int64_t scaled_x;
    int64_t scaled_y;
    TbBool needs_xflip;
    long long lltemp;
    int64_t sprite_group;
    int64_t sprite_rot;
    int64_t cutoff;
    SYNCDBG(17, "At (%" PRId64 ",%" PRId64 ") opts %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64, (int64_t)x, (int64_t)y, (int64_t)kspr_base, (int64_t)kspr_angle, (int64_t)sprgroup, (int64_t)scale);
    player = get_my_player();
    creature_sprites = keepersprite_array(kspr_base);
    if (creature_sprites == NULL) {
        return;
    }
    if (creature_sprites->FramesCount == 0) {
        return;
    }
    if (sprgroup >= creature_sprites->FramesCount) {
        sprgroup = creature_sprites->FramesCount - 1;
    }

    if (((kspr_angle & ANGLE_MASK) <= 1151) || ((kspr_angle & ANGLE_MASK) >= 1919) || (creature_sprites->Rotable != 2) )
        needs_xflip = 0;
    else
        needs_xflip = 1;

    if ( needs_xflip )
      RendererAddDrawFlags(Lb_SPRITE_FLIP_HORIZ);
    else
      RendererClearDrawFlags(Lb_SPRITE_FLIP_HORIZ);
    sprite_group = sprgroup;
    lltemp = 4 - ((((int64_t)kspr_angle + DEGREES_22_5) & ANGLE_MASK) >> 8);
    sprite_rot = llabs(lltemp);
    kspr_idx = keepersprite_index(kspr_base);
    global_scaler = scale;
    if (needs_xflip)
    {
        scaled_x = ((int64_t)x - ((scale * (int64_t)(creature_sprites->FrameWidth + creature_sprites->offset_x)) >> 5));
    }
    else
    {
        scaled_x = ((scale * (int64_t)creature_sprites->offset_x) >> 5) + (int64_t)x;
    }
    scaled_y = ((scale * (int64_t)creature_sprites->offset_y) >> 5) + (int64_t)y;
    SYNCDBG(17,"Scaled (%" PRId64 ",%" PRId64 ")",(int64_t)scaled_x,(int64_t)scaled_y);
    if (thing_is_invalid(thing_being_displayed))
    {
        water_y_offset = 0;
        water_source_cutoff = 0;
    } else
    if ( ((thing_being_displayed->movement_flags & (TMvF_IsOnWater|TMvF_IsOnLava|TMvF_BeingSacrificed)) == 0) || (object_is_buoyant(thing_being_displayed)))
    {
        water_y_offset = 0;
        water_source_cutoff = 0;
    } else
    {
        cutoff = 6;
        if (creature_sprites->shadow_offset > (2 * cutoff))
        {
            cutoff = creature_sprites->shadow_offset / 2;
        }
        if ( (thing_being_displayed->movement_flags & TMvF_BeingSacrificed) != 0 )
        {
            get_keepsprite_unscaled_dimensions(kspr_base, thing_being_displayed->move_angle_xy, sprgroup, &dim_ow, &dim_oh, &dim_tw, &dim_th);
            cctrl = creature_control_get_from_thing(thing_being_displayed);
            lltemp = dim_oh * (48 - (int64_t)cctrl->sacrifice.animation_counter);
            cutoff = ((((lltemp >> 24) & 0x1F) + (int64_t)lltemp) >> 5) / 2;
        }
        if (get_local_active_camera(player)->view_mode == PVM_CreatureView)
        {
            water_source_cutoff = cutoff;
            water_y_offset = (2 * scale * cutoff) >> 5;
        } else
        {
            water_source_cutoff = 2 * cutoff;
            water_y_offset = (scale * cutoff) >> 5;
        }
    }
    scaled_y += water_y_offset;
    if (creature_sprites->Rotable == 0)
    {
        if (!heap_manage_keepersprite(kspr_idx))
        {
            return;
        }
        kspr = &creature_sprites[sprite_group];
        draw_idx = sprite_group + kspr_idx;
        if ( needs_xflip )
        {
            draw_single_keepersprite_omni_xflip(scaled_x, scaled_y, kspr, draw_idx, scale);
        } else
        {
            draw_single_keepersprite_omni(scaled_x, scaled_y, kspr, draw_idx, scale);
        }
    } else
    if (creature_sprites->Rotable == 2)
    {
        if (!heap_manage_keepersprite(kspr_idx))
        {
            return;
        }
        kspr = &creature_sprites[sprite_group + sprite_rot * (int64_t)creature_sprites->FramesCount];
        draw_idx = sprite_group + sprite_rot * (int64_t)kspr->FramesCount + kspr_idx;
        if ( needs_xflip )
        {
            draw_single_keepersprite_xflip(scaled_x, scaled_y, kspr, draw_idx, scale);
        } else
        {
            draw_single_keepersprite(scaled_x, scaled_y, kspr, draw_idx, scale);
        }
    }
}

static void prepare_jonty_remap_and_scale(int64_t *scale, const struct BucketKindJontySprite *jspr)
{
    int64_t i;
    struct Thing *thing;
    int64_t shade;
    int64_t shade_factor;
    int64_t fade;
    thing = jspr->thing;
    int64_t minimum_lightness = kfx_config_state.conf.rules[thing->owner].gameplay.thing_minimum_illumination << 8;
    if (lens_mode == 0)
    {
        fade = 65536;
        if ((thing->rendering_flags & TRF_Unshaded) == 0)
            i = get_thing_shade(thing);
        else
            i = minimum_lightness;
        shade = i;
    } else
    if (jspr->depth_fade <= lfade_min)
    {
        fade = jspr->depth_fade;
        if ((thing->rendering_flags & TRF_Unshaded) == 0)
            i = get_thing_shade(thing);
        else
            i = minimum_lightness;
        shade = i;
    } else
    if (jspr->depth_fade < lfade_max)
    {
        fade = jspr->depth_fade;
        if ((thing->rendering_flags & TRF_Unshaded) == 0)
            i = get_thing_shade(thing);
        else
            i = minimum_lightness;
        shade = i * (long long)(lfade_max - fade) / fade_mmm;
    } else
    {
        fade = jspr->depth_fade;
        shade = 0;
    }
    shade_factor = shade >> 8;
    *scale = (thelens * (int64_t)thing->sprite_size) / fade;
    // gpu-v2 lighting pass: a shaded (not emissive) thing sprite is recorded for
    // per-pixel lighting with its base shade (8.8); see RendererSpriteLightSet. Cleared by draw_jonty_mapwho().
    // (Tinted sprites too: the tint table is kept and lit on top -- frozen, flashing, ...)
    if ((thing->rendering_flags & TRF_Unshaded) == 0)
        RendererSpriteLightSet(shade);
    if ((thing->rendering_flags & (TRF_Tint_1|TRF_Tint_2)) != 0)
    {
        RendererAddDrawFlags(Lb_SPRITE_REMAP);
        shade_factor = thing->tint_colour;
        SetupSpriteRemapGhost((uint8_t)shade_factor,
            (thing->rendering_flags & TRF_Tint_2) ? SPRITE_TINT_STRONG : SPRITE_TINT_LEGACY);
    } else
    if (shade_factor == 32)
    {
        RendererClearDrawFlags(Lb_SPRITE_REMAP);
    } else
    {
        RendererAddDrawFlags(Lb_SPRITE_REMAP);
        SetupSpriteRemapShade(shade_factor);
    }
}

static void draw_mapwho_ariadne_path(struct Thing *thing)
{
    // Don't draw debug pathfinding lines in Possession to avoid crash
    struct PlayerInfo *player = get_my_player();
    if (get_local_active_camera(player)->view_mode == PVM_CreatureView)
        return;

    struct Ariadne *arid;
    {
        struct CreatureControl *cctrl;
        cctrl = creature_control_get_from_thing(thing);
        arid = &cctrl->arid;
    }
    SYNCDBG(16, "Starting for (%" PRId64 ",%" PRId64 ") to (%" PRId64 ",%" PRId64 ")", (int64_t)arid->startpos.x.val, (int64_t)arid->startpos.y.val, (int64_t)arid->endpos.x.val, (int64_t)arid->endpos.y.val);
    int64_t i;
    struct Coord2d *wp_next;
    struct Coord2d *wp_prev;
    wp_prev = (struct Coord2d *)&arid->startpos;
    for (i = 0; i < arid->stored_waypoints; i++)
    {
        wp_next = &arid->waypoints[i];

        int64_t beg_x;
        int64_t end_x;
        int64_t beg_y;
        int64_t end_y;
        beg_x = (int64_t)wp_prev->x.val - map_x_pos;
        end_x = (int64_t)wp_next->x.val - map_x_pos;
        beg_y = map_y_pos - (int64_t)wp_prev->y.val;
        end_y = map_y_pos - (int64_t)wp_next->y.val;
        create_line_const_z(1, (int64_t)arid->startpos.z.val + COORD_PER_STL / 16 - map_z_pos, beg_x, end_x, beg_y, end_y);
        wp_prev = wp_next;
    }
}

static void draw_jonty_mapwho(struct BucketKindJontySprite *jspr)
{
    int64_t flg_mem;
    unsigned char alpha_mem;
    struct PlayerInfo *player = get_my_player();
    struct Thing *thing = jspr->thing;
    int64_t animation_sprite;
    unsigned char current_frame;
    int64_t angle;
    int64_t scaled_size;
    struct ObjectConfigStats* objst;
    flg_mem = RendererGetDrawFlags();
    alpha_mem = EngineSpriteDrawUsingAlpha;
    animation_sprite = get_render_animation_sprite(thing->anim_sprite);
    current_frame = thing->current_frame;
    if (keepersprite_rotable(animation_sprite))
    {
      angle = thing->move_angle_xy - spr_map_angle;
      angle += DEGREES_45 * (int64_t)((thing->flags & TAF_ROTATED_MASK) >> TAF_ROTATED_SHIFT);
    }
    else
      angle = thing->move_angle_xy;
    RendererSpriteLightClear();
    prepare_jonty_remap_and_scale(&scaled_size, jspr);
    EngineSpriteDrawUsingAlpha = 0;
    switch (thing->rendering_flags & (TRF_Transpar_Flags))
    {
    case TRF_Transpar_8:
        RendererAddDrawFlags(Lb_SPRITE_TRANSPAR8);
        RendererClearDrawFlags(Lb_SPRITE_REMAP);
        break;
    case TRF_Transpar_4:
        RendererAddDrawFlags(Lb_SPRITE_TRANSPAR4);
        RendererClearDrawFlags(Lb_SPRITE_REMAP);
        break;
    case TRF_Transpar_Alpha:
        EngineSpriteDrawUsingAlpha = 1;
        break;
    }

    if (!thing_is_invalid(thing))
    {
        if ((local_state.local_thing_under_hand == thing->index) && ((get_gameturn() % (4 * kfx_config_state.gui_blink_rate)) >= 2 * kfx_config_state.gui_blink_rate)) {
          struct Camera *active_cam = get_local_active_camera(player);
          if ((active_cam != NULL) && (active_cam->view_mode == PVM_IsoWibbleView || active_cam->view_mode == PVM_IsoStraightView))
          {
              RendererAddDrawFlags(Lb_SPRITE_REMAP);
              SetupSpriteRemapWhiteFlash();
          }
          else if ((active_cam != NULL) && (active_cam->view_mode == PVM_CreatureView))
          {
              struct Thing *creatng = thing_get(player->influenced_thing_idx);
              if (thing_is_creature(creatng))
              {
                  struct CreatureControl* cctrl = creature_control_get_from_thing(creatng);
                  struct Thing *dragtng = thing_get(cctrl->dragtng_idx);
                  if (!thing_exists(dragtng))
                  {
                    RendererAddDrawFlags(Lb_SPRITE_REMAP);
                    SetupSpriteRemapWhiteFlash();
                  }
                  else if (thing_is_trap_crate(dragtng))
                  {
                      struct Thing *handthing = thing_get(local_state.local_thing_under_hand);
                      if (thing_exists(handthing))
                      {
                          if (handthing->class_id == TCls_Trap)
                          {
                              RendererAddDrawFlags(Lb_SPRITE_REMAP);
                              SetupSpriteRemapWhiteFlash();
                          }
                      }
                  }
              }
          }
        } else {
            if (thing->last_turn_damaged == kfx_sim_state.play_gameturn)
            {
                RendererAddDrawFlags(Lb_SPRITE_REMAP);
                SetupSpriteRemapRedFlash();
            }
        }
        thing_being_displayed_is_creature = 1;
        thing_being_displayed = thing;
    } else
    {
        thing_being_displayed_is_creature = 0;
        thing_being_displayed = NULL;
    }
    if (render_sprite_debug_fn)
    {
        render_sprite_debug_fn(thing, jspr->scr_x, jspr->scr_y);
    }

    if (animation_sprite_id_invalid(animation_sprite))
    {
        ERRORLOG("Invalid graphic Id %" PRId64 " from model %" PRId64 ", class %" PRId64, (int64_t)animation_sprite, (int64_t)thing->model, (int64_t)thing->class_id);
    } else
    {
        struct TrapConfigStats *trapst;
        switch (thing->class_id)
        {
        case TCls_Object:
            objst = get_object_model_stats(thing->model);
            if (objst->flame.animation_id > 0)
            {
                process_keeper_flame_on_sprite(jspr, angle, scaled_size);
                break;
            }
            process_keeper_sprite(jspr->scr_x, jspr->scr_y, animation_sprite, angle, current_frame, scaled_size);
            break;
        case TCls_Trap:
            trapst = get_trap_model_stats(thing->model);
            if ((trapst->hidden == 1) && (player->id_number != thing->owner) && (thing->trap.revealed == 0))
            {
                break;
            }
            if ((trapst->flame.animation_id > 0) && (thing->trap.num_shots != 0))
            {
                process_keeper_flame_on_sprite(jspr, angle, scaled_size);
                break;
            }
            process_keeper_sprite(jspr->scr_x, jspr->scr_y, animation_sprite, angle, current_frame, scaled_size);
            break;
        default:
            process_keeper_sprite(jspr->scr_x, jspr->scr_y, animation_sprite, angle, current_frame, scaled_size);
            break;
        }
    }
    RendererSpriteLightClear();
    RendererSetDrawFlags(flg_mem);
    EngineSpriteDrawUsingAlpha = alpha_mem;
}

/** Fills solid area of the sprite in target buffer with color 255.
 *
 * @param sprdata Sprite data.
 * @param outbuf Output buffer to be filled.
 * @param lines_max Max lines to be written into output buffer.
 * @param scanln Length of scanline (length of line in output buffer).
 */
// When set, sprite_to_sbuff() copies the sprite's palette indices instead of
// writing the 0xFF silhouette mask the shadow renderer wants. Only
// render_keepsprite_indexed() turns it on, for the duration of one decode.
static TbBool sprite_to_sbuff_copy_colour = false;

static void sprite_to_sbuff(const TbSpriteData sprdata, unsigned char *outbuf, int64_t lines_max, int64_t scanln)
{
    unsigned char *out_lnstart;
    unsigned char *out;
    int64_t cval;
    const unsigned char *sprd;
    int64_t i;
    sprd = sprdata;
    out = outbuf;
    out_lnstart = outbuf;
    cval = 0;
    if (sprite_to_sbuff_copy_colour)
    {
        // Same run-length format as below: negative = skip that many
        // transparent pixels, positive = that many literal palette bytes,
        // zero = end of line.
        while (lines_max > 0)
        {
            while (1)
            {
                cval = *(const signed char *)sprd;
                sprd++;
                if (cval == 0)
                    break;
                if (cval < 0)
                {
                    out += -cval;
                }
                else
                {
                    memcpy(out, sprd, cval);
                    out += cval;
                    sprd += cval;
                }
            }
            out_lnstart += scanln;
            out = out_lnstart;
            lines_max--;
        }
        return;
    }
    while (lines_max > 0)
    {
      while ( 1 )
      {
          // Skip transparent area
          while ( 1 )
          {
              cval = *(char *)sprd;
              sprd++;
              if (cval >= 0)
                break;
              cval = -cval;
              out += cval;
          }
          if (cval == 0)
            break;
          sprd += cval;
          // Fill area per-byte until we get 32bit-aligned position
          while ((ptrdiff_t)out & 3)
          {
              *out = 0xFF;
              out++;
              cval--;
              if (cval <= 0)
                  break;
          }
          // Now fill the area faster - by writing 32-bit values
          for (i = (cval >> 2); i > 0; i--)
          {
              *(uint32_t *)out = 0xFFFFFFFF; // 32-bit pixel writes
              out += 4;
          }
          // Fill the last unaligned bytes
          cval &= 3;
          while (cval > 0)
          {
            *out = 0xFF;
            out++;
            cval--;
          }
      }
      out_lnstart += scanln;
      out = out_lnstart;
      lines_max--;
    }
}

/** Fills solid area of the sprite in target buffer with color 255, x-flipping the sprite.
 *
 * @param sprdata Sprite data.
 * @param outbuf Output buffer to be filled.
 * @param lines_max Max lines to be written into output buffer.
 * @param scanln Length of scanline (length of line in output buffer).
 */
static void sprite_to_sbuff_xflip(const TbSpriteData sprdata, unsigned char *outbuf, int64_t lines_max, int64_t scanln)
{
    unsigned char *out_lnstart;
    unsigned char *out;
    int64_t cval;
    const unsigned char *sprd;
    int64_t i;
    sprd = sprdata;
    out = outbuf;
    out_lnstart = outbuf;
    cval = 0;
    while (lines_max > 0)
    {
      while ( 1 )
      {
          // Skip transparent area
          while ( 1 )
          {
              cval = *(char *)sprd;
              sprd++;
              if (cval >= 0)
                  break;
              cval = -cval;
              out -= cval;
          }
          if (cval == 0)
            break;
          sprd += cval;
          out++;
          // Fill area per-byte until we get 32bit-aligned position
          while ((uintptr_t)out & 3)
          {
              out--;
              *out = 0xFF;
              cval--;
              if (cval <= 0)
                  break;
          }
          // Now fill the area faster - by writing 32-bit values
          for (i = (cval >> 2); i > 0; i--)
          {
              out -= 4;
              *(uint32_t *)out = 0xFFFFFFFF; // 32-bit pixel writes
          }
          out--;
          // Fill the last unaligned bytes
          cval &= 3;
          while (cval > 0)
          {
              *out = 0xFF;
              out--;
              cval--;
          }
      }
      out_lnstart += scanln;
      out = out_lnstart;
      lines_max--;
    }
}

static void draw_keepsprite_unscaled_in_buffer(int64_t kspr_n, int64_t angle, unsigned char current_frame, unsigned char *outbuf)
{
    struct KeeperSprite *kspr_arr;
    uint64_t kspr_idx;
    struct KeeperSprite *kspr;
    TbSpriteData sprite_data;
    uint64_t keepsprite_id;
    unsigned char *tmpbuf;
    int64_t skip_w;
    int64_t skip_h;
    int64_t fill_w;
    int64_t fill_h;
    TbBool flip_range;
    int64_t quarter;
    int64_t i;
    if ( ((angle & ANGLE_MASK) <= 1151) || ((angle & ANGLE_MASK) >= 1919) )
        flip_range = false;
    else
        flip_range = true;
    i = ((angle + DEGREES_22_5) & ANGLE_MASK);
    quarter = llabs(4 - (i >> 8)); // i is restricted by "&" so (i>>8) is 0..7
    kspr_arr = keepersprite_array(kspr_n);
    if (kspr_arr == NULL) {
        return;
    }
    if (kspr_arr->FramesCount == 0) {
        return;
    }
    if (current_frame >= kspr_arr->FramesCount) {
        current_frame = kspr_arr->FramesCount - 1;
    }
    kspr_idx = keepersprite_index(kspr_n);

    if (kspr_arr->Rotable == 0)
    {
        if (!heap_manage_keepersprite(kspr_idx))
        {
            return;
        }
        keepsprite_id = current_frame + kspr_idx;
        if (keepsprite_id >= KEEPERSPRITE_ADD_OFFSET)
        {
            sprite_data = keepersprite_add[keepsprite_id - KEEPERSPRITE_ADD_OFFSET];
        }
        else if (keepsprite_id >= KEEPSPRITE_LENGTH)
        {
            ERRORLOG("Sprite %" PRId64 " outside of valid range.", (int64_t)(keepsprite_id));
            return;
        }
        else
        {
            sprite_data = *keepsprite[keepsprite_id];
        }
        kspr = &kspr_arr[current_frame];
        fill_w = kspr->FrameWidth;
        fill_h = kspr->FrameHeight;
        if ( flip_range )
        {
            tmpbuf = outbuf;
            skip_w = kspr->FrameWidth - kspr->FrameOffsW;
            skip_h = kspr->FrameOffsH;
            for (i = fill_h; i > 0; i--)
            {
                memset(tmpbuf, 0, fill_w);
                tmpbuf += 256;
            }
            sprite_to_sbuff_xflip(sprite_data, &outbuf[256 * skip_h + skip_w], kspr->SHeight, 256);
        }
        else
        {

            tmpbuf = outbuf;
            skip_w = kspr->FrameOffsW;
            skip_h = kspr->FrameOffsH;
            for (i = fill_h; i > 0; i--)
            {
                memset(tmpbuf, 0, fill_w);
                tmpbuf += 256;
            }
            sprite_to_sbuff(sprite_data, &outbuf[256 * skip_h + skip_w], kspr->SHeight, 256);
        }
    }
    else if (kspr_arr->Rotable == 2)
    {
        if (!heap_manage_keepersprite(kspr_idx))
        {
            return;
        }
        kspr = &kspr_arr[current_frame + quarter * kspr_arr->FramesCount];
        fill_w = kspr->SWidth;
        fill_h = kspr->SHeight;
        keepsprite_id = current_frame + quarter * kspr->FramesCount + kspr_idx;
        if (keepsprite_id >= KEEPERSPRITE_ADD_OFFSET)
        {
            sprite_data = keepersprite_add[keepsprite_id - KEEPERSPRITE_ADD_OFFSET];
        }
        else if (keepsprite_id >= KEEPSPRITE_LENGTH)
        {
            return; // WTF?!!
        }
        else
        {
            sprite_data = *keepsprite[keepsprite_id];
        }
        if ( flip_range )
        {
            tmpbuf = outbuf;
            for (i = fill_h; i > 0; i--)
            {
                memset(tmpbuf, 0, fill_w);
                tmpbuf += 256;
            }
            sprite_to_sbuff_xflip(sprite_data, &outbuf[kspr->SWidth], kspr->SHeight, 256);
        }
        else
        {
            tmpbuf = outbuf;
            for (i = fill_h; i > 0; i--)
            {
                memset(tmpbuf, 0, fill_w);
                tmpbuf += 256;
            }
            sprite_to_sbuff(sprite_data, &outbuf[0], kspr->SHeight, 256);
        }
    }
}

/**
 * Decodes one frame of thing animation `kspr_n` (a keepersprite animation
 * number, the value a thing's anim_sprite holds) into `outbuf` as palette
 * indices, 256 bytes per row, index 0 transparent -- the routine the
 * renderer uses for creature shadows (there it writes only a silhouette
 * mask; here sprite_to_sbuff() is switched to copy the colours), exposed so
 * the editor's palette can show a thing's sprite. `outbuf` must be zeroed and hold at least
 * 256*256 bytes. Returns false if the animation does not exist.
 */
TbBool render_keepsprite_indexed(int64_t kspr_n, unsigned char frame, unsigned char *outbuf)
{
    struct KeeperSprite *kspr_arr = keepersprite_array(kspr_n);
    if ((kspr_arr == NULL) || (kspr_arr->FramesCount == 0))
        return false;
    sprite_to_sbuff_copy_colour = true;
    draw_keepsprite_unscaled_in_buffer(kspr_n, 0, frame, outbuf);
    sprite_to_sbuff_copy_colour = false;
    return true;
}

static void update_frontview_pointed_block(uint64_t laaa, unsigned char qdrant, int64_t w, int64_t h, int64_t qx, int64_t qy)
{
    TbGraphicsWindow ewnd;
    struct Column *colmn;
    uint64_t mask;
    struct Map *mapblk;
    int64_t pos_x;
    int64_t pos_y;
    int64_t stl_x;
    int64_t stl_y;
    int64_t point_a;
    int64_t point_b;
    int64_t delta;
    int64_t i;
    SYNCDBG(16,"Starting");
    store_engine_window(&ewnd,1);
    point_a = (((GetMouseX() - ewnd.x) << 8) - qx) << 8;
    point_b = (((GetMouseY() - ewnd.y) << 8) - qy) << 8;
    delta = (laaa << 7) / 256 << 8;
    for (i=0; i < 8; i++)
    {
        pos_x = (point_a / laaa) * x_step2[qdrant] + (point_b / laaa) * x_step1[qdrant] + (w << 8);
        pos_y = (point_a / laaa) * y_step2[qdrant] + (point_b / laaa) * y_step1[qdrant] + (h << 8);
        stl_x = (pos_x >> 8) + x_offs[qdrant];
        stl_y = (pos_y >> 8) + y_offs[qdrant];

        mapblk = get_map_block_at(stl_x, stl_y);
        if (!map_block_invalid(mapblk))
        {
          if (i == 0)
          {
            floor_pointed_at_x = stl_x;
            floor_pointed_at_y = stl_y;
            kfx_render_state.block_pointed_at_x = stl_x;
            kfx_render_state.block_pointed_at_y = stl_y;
            kfx_render_state.pointed_at_frac_x = pos_x & 0xFF;
            kfx_render_state.pointed_at_frac_y = pos_y & 0xFF;
            kfx_render_state.me_pointed_at = mapblk;
          } else
          {
            colmn = get_map_column(mapblk);
            mask = colmn->solidmask;
            if ( (1 << (i-1)) & mask )
            {
              kfx_render_state.pointed_at_frac_x = pos_x & 0xFF;
              kfx_render_state.pointed_at_frac_y = pos_y & 0xFF;
              kfx_render_state.block_pointed_at_x = stl_x;
              kfx_render_state.block_pointed_at_y = stl_y;
              kfx_render_state.me_pointed_at = mapblk;
            }
            if (((temp_cluedo_mode)  && (i == 2))
             || ((!temp_cluedo_mode) && (i == 5)))
            {
              kfx_render_state.top_pointed_at_frac_x = pos_x & 0xFF;
              kfx_render_state.top_pointed_at_frac_y = pos_y & 0xFF;
              kfx_render_state.top_pointed_at_x = stl_x;
              kfx_render_state.top_pointed_at_y = stl_y;
            }
          }
        }
        point_b += delta;
    }
}

static int64_t frontview_floor_line_bucket(int64_t floor_z, int64_t row_px, unsigned char stl_width)
{
    return floor_z - row_px - stl_width / 2;
}

void create_frontview_map_volume_box(struct Camera *cam, unsigned char stl_width, int64_t line_color)
{
    unsigned char orient = ((uint64_t)(cam->rotation_angle_x + DEGREES_45) / DEGREES_90) & 0x03;
    int64_t depth = ((5 - map_volume_box.floor_height_z) * ((int64_t)stl_width << 7) / 256);
    struct Coord3d pos;
    int64_t coord_x;
    int64_t coord_y;
    int64_t coord_z;
    int64_t box_width, box_height;
    pos.y.val = map_volume_box.end_y - box_lag_compensation_y;
    pos.x.val = map_volume_box.end_x - box_lag_compensation_x;
    pos.z.val = subtile_coord(5,0);
    convert_world_coord_to_front_view_screen_coord(&pos, cam, &coord_x, &coord_y, &coord_z);
    box_width = coord_x;
    box_height = coord_y;
    pos.y.val = map_volume_box.beg_y - box_lag_compensation_y;
    pos.x.val = map_volume_box.beg_x - box_lag_compensation_x;
    convert_world_coord_to_front_view_screen_coord(&pos, cam, &coord_x, &coord_y, &coord_z);
    box_width -= coord_x;
    box_height -= coord_y;
    box_width = llabs(box_width);
    box_height = llabs(box_height);
    switch ( orient )
    {
    //case 0: // North
    case 1: // East
        coord_y -= box_height;
        coord_z += box_height;
        break;
    case 2: // South
        coord_x -= box_width;
        coord_y -= box_height;
        coord_z += box_height;
        break;
    case 3: // West
        coord_x -= box_width;
        break;
    }
    int64_t floor_z = coord_z;
    coord_z -= 7 * stl_width / 2;

    create_line_element(coord_x,             coord_y,                      coord_x + box_width, coord_y,                      coord_z,                          line_color);
    create_line_element(coord_x,             coord_y + box_height,         coord_x + box_width, coord_y + box_height,         coord_z - box_height,             line_color);
    create_line_element(coord_x,             coord_y,                      coord_x,             coord_y + box_height,         coord_z - box_height,             line_color);
    create_line_element(coord_x + box_width, coord_y,                      coord_x + box_width, coord_y + box_height,         coord_z - box_height,             line_color);
    int64_t near_bckt = frontview_floor_line_bucket(floor_z, box_height - stl_width, stl_width);
    create_line_element(coord_x,             coord_y + depth,              coord_x + box_width, coord_y + depth,              frontview_floor_line_bucket(floor_z, 0, stl_width), line_color);
    create_line_element(coord_x,             coord_y + box_height + depth - pixel_size, coord_x + box_width, coord_y + box_height + depth - pixel_size, near_bckt, line_color);
    create_line_element(coord_x,             coord_y + box_height,         coord_x,             coord_y + box_height + depth, near_bckt,                        line_color);
    create_line_element(coord_x + box_width, coord_y + box_height,         coord_x + box_width, coord_y + box_height + depth, near_bckt,                        line_color);
}

void create_fancy_frontview_map_volume_box(struct RoomSpace roomspace, struct Camera *cam, unsigned char stl_width, int64_t color, TbBool show_outer_box)
{
    int64_t line_color = color;
    if (show_outer_box)
    {
        line_color = map_volume_box.color; //  set the "inner" box color to the default colour (usually red/green)
    }
    unsigned char orient = ((uint64_t)(cam->rotation_angle_x + DEGREES_45) / DEGREES_90) & 0x03;
    int64_t floor_height_z = (map_volume_box.floor_height_z == 0) ? 1 : map_volume_box.floor_height_z; // ignore "liquid height", and force it to "floor height". All fancy rooms are on the ground, and this ensures the boundboxes are drawn correctly. A different solution will be required if this function is used to draw fancy rooms over "liquid".
    int64_t depth = ((5 - floor_height_z) * ((int64_t)stl_width << 7) / 256);
    struct Coord3d pos;
    int64_t coord_x;
    int64_t coord_y;
    int64_t coord_z;
    int64_t box_width, box_height;
    struct MapVolumeBox valid_slabs = map_volume_box;
    // get the 'accurate' roomspace shape instead of the outer box
    valid_slabs.beg_x = subtile_coord((roomspace.left * 3), 0);
    valid_slabs.beg_y = subtile_coord((roomspace.top * 3), 0);
    valid_slabs.end_x = subtile_coord((3*1) + (roomspace.right * 3), 0);
    valid_slabs.end_y = subtile_coord(((3*1) + roomspace.bottom * 3), 0);
    pos.y.val = valid_slabs.end_y - box_lag_compensation_y;
    pos.x.val = valid_slabs.end_x - box_lag_compensation_x;
    pos.z.val = subtile_coord(5,0);
    convert_world_coord_to_front_view_screen_coord(&pos, cam, &coord_x, &coord_y, &coord_z);
    box_width = coord_x;
    box_height = coord_y;
    pos.y.val = valid_slabs.beg_y - box_lag_compensation_y;
    pos.x.val = valid_slabs.beg_x - box_lag_compensation_x;
    convert_world_coord_to_front_view_screen_coord(&pos, cam, &coord_x, &coord_y, &coord_z);
    box_width -= coord_x;
    box_height -= coord_y;
    box_width = llabs(box_width);
    box_height = llabs(box_height);
    int64_t room_slab_width = roomspace.width;
    int64_t room_slab_height = roomspace.height;
    if (orient % 2 == 1)
    {
        room_slab_width = roomspace.height;
        room_slab_height = roomspace.width;
    }
    TbBool rotated_roomspace[MAX_ROOMSPACE_WIDTH][MAX_ROOMSPACE_WIDTH];
    memcpy(rotated_roomspace,roomspace.slab_grid, sizeof(rotated_roomspace));
    int64_t i, j;
    switch ( orient )
    {
    //case 0: // North
    case 1: // East
        coord_y -= box_height;
        coord_z += box_height;
        for (i = 0; i < roomspace.width; i++)
        {
            for (j = 0; j < roomspace.height; j++)
            {
                rotated_roomspace[j][i] = roomspace.slab_grid[roomspace.width - 1 - i][j];
            }
        }
        break;
    case 2: // South
        coord_x -= box_width;
        coord_y -= box_height;
        coord_z += box_height;
        for (i = 0; i < roomspace.width; i++)
        {
            for (j = 0; j < roomspace.height; j++)
            {
                rotated_roomspace[i][j]  = roomspace.slab_grid[roomspace.width - 1 - i][roomspace.height - 1 - j];
            }
        }
        break;
    case 3: // West
        coord_x -= box_width;
        for (i = 0; i < roomspace.width; i++)
        {
            for (j = 0; j < roomspace.height; j++)
            {
                rotated_roomspace[j][i] = roomspace.slab_grid[i][roomspace.height - 1 - j];
            }
        }
        break;
    }
    int64_t floor_z = coord_z;
    coord_z -= 7 * stl_width / 2;
    for (int64_t roomspace_y = 0; roomspace_y < room_slab_height; roomspace_y += 1)
    {
        int64_t y_start = (box_height * roomspace_y       / room_slab_height) + ((((box_height * roomspace_y)       % room_slab_height) >= room_slab_height) ? 1 : 0);
        int64_t y_end =   (box_height * (roomspace_y + 1) / room_slab_height) + ((((box_height * (roomspace_y + 1)) % room_slab_height) >= room_slab_height) ? 1 : 0);
        int64_t bckt_idx = coord_z - y_end;
        int64_t floor_far_bckt = frontview_floor_line_bucket(floor_z, y_start, stl_width);
        int64_t floor_near_bckt = frontview_floor_line_bucket(floor_z, y_end - stl_width, stl_width);
        for (int64_t roomspace_x = 0; roomspace_x < room_slab_width; roomspace_x += 1)
        {
            int64_t x_start = (box_width * roomspace_x       / room_slab_width) + ((((box_width * roomspace_x)       % room_slab_width) >= room_slab_width) ? 1 : 0);
            int64_t x_end =   (box_width * (roomspace_x + 1) / room_slab_width) + ((((box_width * (roomspace_x + 1)) % room_slab_width) >= room_slab_width) ? 1 : 0);
            TbBool is_in_roomspace = rotated_roomspace[roomspace_x][roomspace_y];
            if (is_in_roomspace)
            {
                TbBool air_left =  (roomspace_x == 0)                    ? true : (rotated_roomspace[roomspace_x-1][roomspace_y] == false);
                TbBool air_right = (roomspace_x == room_slab_width - 1)  ? true : (rotated_roomspace[roomspace_x+1][roomspace_y] == false);
                TbBool air_above = (roomspace_y == 0)                    ? true : (rotated_roomspace[roomspace_x][roomspace_y-1] == false);
                TbBool air_below = (roomspace_y == room_slab_height - 1) ? true : (rotated_roomspace[roomspace_x][roomspace_y+1] == false);
                if (air_left)
                {
                    create_line_element(    coord_x + x_start, coord_y + y_start,         coord_x + x_start, coord_y + y_end,           bckt_idx,             line_color);
                    if (air_below)
                    {
                        create_line_element(coord_x + x_start, coord_y + y_end,           coord_x + x_start, coord_y + y_end + depth,   floor_near_bckt,      line_color);
                    }
                }
                if (air_right)
                {
                    create_line_element(    coord_x + x_end,   coord_y + y_start,         coord_x + x_end,   coord_y + y_end,           bckt_idx,             line_color);
                    if (air_below)
                    {
                        create_line_element(coord_x + x_end,   coord_y + y_end,           coord_x + x_end,   coord_y + y_end + depth,   floor_near_bckt,      line_color);
                    }
                }
                if (air_above)
                {
                    create_line_element(    coord_x + x_start, coord_y + y_start,         coord_x + x_end,   coord_y + y_start,         bckt_idx,             line_color);
                    create_line_element(    coord_x + x_start, coord_y + y_start + depth, coord_x + x_end,   coord_y + y_start + depth, floor_far_bckt,       line_color);
                }
                if (air_below)
                {
                    create_line_element(    coord_x + x_start, coord_y + y_end,           coord_x + x_end,   coord_y + y_end,           bckt_idx,             line_color);
                    create_line_element(    coord_x + x_start, coord_y + y_end + depth - pixel_size, coord_x + x_end, coord_y + y_end + depth - pixel_size, floor_near_bckt, line_color);
                }
            }
            else if (!is_in_roomspace) //this handles "inside corners"
            {
                TbBool room_left =  (roomspace_x == 0)                    ? false : rotated_roomspace[roomspace_x-1][roomspace_y];
                TbBool room_right = (roomspace_x == room_slab_width - 1)  ? false : rotated_roomspace[roomspace_x+1][roomspace_y];
                TbBool room_below = (roomspace_y == room_slab_height - 1) ? false : rotated_roomspace[roomspace_x][roomspace_y+1];
                if (room_left)
                {
                    if (room_below)
                    {
                        create_line_element(coord_x + x_start,  coord_y + y_end,          coord_x + x_start, coord_y + y_end + depth,   floor_near_bckt,      line_color);
                    }
                }
                if (room_right)
                {
                    if (room_below)
                    {
                        create_line_element(coord_x + x_end,   coord_y + y_end,           coord_x + x_end,   coord_y + y_end + depth,   floor_near_bckt,      line_color);
                    }
                }
                if (show_outer_box) // this handles the "outer line" (only when it is not in the roomspace)
                {
                    //draw 2nd line, i.e. the outer line - the one around the edge of the 5x5 cursor, not the valid slabs within the cursor
                    line_color = color; // switch to the "secondary colour" (the one passed as a variable if show_outer_box is true)
                    TbBool left_edge   = (roomspace_x == 0)                    ? true : false;
                    TbBool right_edge  = (roomspace_x == room_slab_width - 1)  ? true : false;
                    TbBool top_edge    = (roomspace_y == 0)                    ? true : false;
                    TbBool bottom_edge = (roomspace_y == room_slab_height - 1) ? true : false;
                    if (left_edge)
                    {
                        create_line_element(    coord_x + x_start, coord_y + y_start,         coord_x + x_start, coord_y + y_end,           bckt_idx,             line_color);
                        if (bottom_edge)
                        {
                            create_line_element(coord_x + x_start, coord_y + y_end,           coord_x + x_start, coord_y + y_end + depth,   floor_near_bckt,      line_color);
                        }
                    }
                    if (right_edge)
                    {
                        create_line_element(    coord_x + x_end,   coord_y + y_start,         coord_x + x_end,   coord_y + y_end,           bckt_idx,             line_color);
                        if (bottom_edge)
                        {
                            create_line_element(coord_x + x_end,   coord_y + y_end,           coord_x + x_end,   coord_y + y_end + depth,   floor_near_bckt,      line_color);
                        }
                    }
                    if (top_edge)
                    {
                        create_line_element(    coord_x + x_start, coord_y + y_start,         coord_x + x_end,   coord_y + y_start,         bckt_idx,             line_color);
                        create_line_element(    coord_x + x_start, coord_y + y_start + depth, coord_x + x_end,   coord_y + y_start + depth, floor_far_bckt,       line_color);
                    }
                    if (bottom_edge)
                    {
                        create_line_element(    coord_x + x_start, coord_y + y_end,           coord_x + x_end,   coord_y + y_end,           bckt_idx,             line_color);
                        create_line_element(    coord_x + x_start, coord_y + y_end + depth - pixel_size, coord_x + x_end, coord_y + y_end + depth - pixel_size, floor_near_bckt, line_color);
                    }
                    line_color = map_volume_box.color; // switch back to default color (red/green) for the inner line
                }
            }
        }
    }
}

static void process_frontview_map_volume_box(struct Camera *cam, unsigned char stl_width, PlayerNumber plyr_idx)
{
    unsigned char default_color = map_volume_box.color;
    unsigned char line_color = default_color;
    struct PlayerInfo* current_player = get_player(plyr_idx);
    struct RoomSpace *render_roomspace = get_local_dig_prediction_render_roomspace(&current_player->render_roomspace);
    // Check if a roomspace is currently being built
    // and if so feed this back to the user
    if ((current_player->roomspace.is_active) && ((current_player->work_state == PSt_Sell) || (current_player->work_state == PSt_BuildRoom)))
    {
        line_color = SLC_REDYELLOW; // change the cursor color to indicate to the user that nothing else can be built or sold at the moment
    }
    if (render_roomspace->render_roomspace_as_box)
    {
        if (render_roomspace->is_roomspace_a_box)
        {
            // This is a basic square box
             create_frontview_map_volume_box(cam, stl_width, line_color);
        }
        else
        {
            // This is a "2-line" square box
            // i.e. an "accurate" box with an outer square box
            map_volume_box.color = line_color;
            create_fancy_frontview_map_volume_box(*render_roomspace, cam, stl_width, (render_roomspace->slab_count == 0) ? SLC_RED : SLC_BROWN, true);
        }
    }
    else
    {
        // This is an "accurate"/"automagic" box
        create_fancy_frontview_map_volume_box(*render_roomspace, cam, stl_width, line_color, false);
    }
    map_volume_box.color = default_color;
}

TbBool cursor_on_room(RoomIndex room_index)
{
    struct UserState* ustate = get_local_user_state();
    struct SlabMap* slb = get_slabmap_for_subtile(ustate->cursor_subtile_x, ustate->cursor_subtile_y);
    if (slabmap_block_invalid(slb)) {
        return false;
    }
    if (slb->room_index != room_index) {
        return false;
    }
    return true;
}
TbBool room_is_damaged(RoomIndex room_index)
{
    struct Room* room = room_get(room_index);
    if (room->health == compute_room_max_health(room->slabs_count, room->efficiency)) {
        return false;
    }
    return true;
}
TbBool placing_same_room_type(RoomIndex room_index)
{
    struct UserState* ustate = get_local_user_state();
    if (map_volume_box.visible == 0) {
        return false;
    }
    struct Room* room = room_get(room_index);
    if (ustate->chosen_room_kind != room->kind) {
        return false;
    }
    return true;
}

static void do_map_who_for_thing(struct Thing *thing)
{
    int64_t bckt_idx;
    struct EngineCoord ecor;
    struct NearestLights nearlgt;

    const struct ThingInterpolateResult interp = interpolate_thing(thing);
    const int64_t render_pos_x = interp.mappos.x.val;
    const int64_t render_pos_y = interp.mappos.z.val;
    const int64_t render_pos_z = interp.mappos.y.val;
    const int64_t render_floorpos = interp.floor_height;

    switch (thing->draw_class)
    {
    case ODC_Default:
        ecor.clip_flags = 0;
        ecor.x = (render_pos_x - map_x_pos);
        ecor.z = (map_y_pos - render_pos_z);
        ecor.y = (render_floorpos - map_z_pos); // For shadows

        // Shadows
        if (thing_is_creature(thing) && ((thing->movement_flags & TMvF_BeingSacrificed) == 0))
        {
            int64_t count;
            int64_t i;

            int64_t animation_sprite = get_render_animation_sprite(thing->anim_sprite);
            struct KeeperSprite *spr = keepersprite_array(animation_sprite);
            if ((spr != NULL) && ((spr->frame_flags & FFL_NoShadows) == 0))
            {
                count = find_closest_lights(&thing->mappos, &nearlgt);
                for (i = 0; i < count; i++)
                {
                    create_shadows(thing, &ecor, &nearlgt.coord[i]);
                }
            }
        }
        // Height movement, falling or going up steps. This is applied after shadows, because shadows are always drawn at the floor height.
        ecor.y = (render_pos_y - map_z_pos);

        if (thing->class_id == TCls_Creature)
        {
            add_draw_status_box(thing, &ecor);
            // Draw path the creature is following
            if ((start_params.debug_flags & DFlg_CreatrPaths) != 0) {
                draw_mapwho_ariadne_path(thing);
            }
        }
        rotpers(&ecor, &camera_matrix);
        if (getpoly < poly_pool_end)
        {
            if ( lens_mode )
              bckt_idx = (ecor.z - 64) / 16;
            else
              bckt_idx = (ecor.z - 64) / 16 - 6;
            add_thing_sprite_to_polypool(thing, ecor.view_width, ecor.view_height, ecor.z, bckt_idx);
        }
        break;
    case ODC_DrawAtOrigin:
        ecor.clip_flags = 0;
        ecor.x = (render_pos_x - map_x_pos);
        ecor.z = (map_y_pos - render_pos_z);
        ecor.y = (render_pos_y - map_z_pos);
        memcpy(&object_origin, &ecor, sizeof(struct EngineCoord));
        object_origin.x = 0;
        object_origin.y = 0;
        object_origin.z = 0;
        break;
    case ODC_RoomPrice:
        ecor.x = (render_pos_x - map_x_pos);
        ecor.z = (map_y_pos - render_pos_z);
        ecor.y = (render_pos_y - map_z_pos);
        rotpers(&ecor, &camera_matrix);
        if (getpoly < poly_pool_end)
        {
            add_number_to_polypool(ecor.view_width, ecor.view_height, thing->price_effect.number, 1);
        }
        break;
    case ODC_RoomStatusFlag:
        // Hide status flags when full zoomed out, for atmospheric overview
        if (hud_scale == 0) {
            break;
        }

        RoomIndex flag_room_index = thing->lair.belongs_to;
        if (cursor_on_room(flag_room_index) == false && room_is_damaged(flag_room_index) == false && placing_same_room_type(flag_room_index) == false) {
            break;
        }

        ecor.x = (render_pos_x - map_x_pos);
        ecor.z = (map_y_pos - render_pos_z);
        ecor.y = (render_pos_y - map_z_pos);
        rotpers(&ecor, &camera_matrix);
        if (getpoly < poly_pool_end)
        {
            if (get_gameturn() - thing->roomflag.last_turn_drawn == 1)
            {
                if (thing->roomflag.display_timer < 10) {
                    thing->roomflag.display_timer++;
                }
            } else {
                if (get_gameturn() - thing->roomflag.last_turn_drawn > 1) {
                    thing->roomflag.display_timer = 0;
                }
            }
            thing->roomflag.last_turn_drawn = get_gameturn();
            if (thing->roomflag.display_timer == 10)
            {
                bckt_idx = (ecor.z - 64) / 16 - 6;
                add_room_flag_pole_to_polypool(ecor.view_width, ecor.view_height, thing->roomflag.room_idx, bckt_idx);
                if (getpoly < poly_pool_end)
                {
                    add_room_flag_top_to_polypool(ecor.view_width, ecor.view_height, thing->roomflag.room_idx, 1);
                }
            }
        }
        break;
    case ODC_SpinningKey:
        ecor.x = (render_pos_x - map_x_pos);
        ecor.z = (map_y_pos - render_pos_z);
        ecor.y = (render_pos_y - map_z_pos);
        rotpers(&ecor, &camera_matrix);
        if (getpoly < poly_pool_end) {
            add_spinning_key_to_polypool(thing, ecor.view_width, ecor.view_height, ecor.z, 1);
        }
        break;
    default:
        break;
    }
    thing->last_turn_drawn = get_gameturn();
}

static void do_map_who(int64_t tnglist_idx)
{
    int64_t i;
    uint64_t k;
    k = 0;
    i = tnglist_idx;
    while (i != 0)
    {
        struct Thing *thing;
        thing = thing_get(i);
        TRACE_THING(thing);
        if (thing_is_invalid(thing))
        {
            ERRORLOG("Jump to invalid thing detected");
            break;
        }
        i = thing->next_on_mapblk;
        // Per thing code start
        if ((thing->rendering_flags & TRF_Invisible) == 0)
        {
            do_map_who_for_thing(thing);
        }
        // Per thing code end
        k++;
        if (k > THINGS_COUNT)
        {
            ERRORLOG("Infinite loop detected when sweeping things list");
            struct Map* mapblk = get_map_block_at(thing->mappos.x.stl.num, thing->mappos.y.stl.num);
            break_mapwho_infinite_chain(mapblk);
            break;
        }
    }
}

static void draw_frontview_thing_on_element(struct Thing *thing, struct Map *map, struct Camera *cam)
{
    // The draw_frontview_thing_on_element() function is the FrontView equivalent of do_map_who_for_thing()
    struct ThingInterpolateResult interp = interpolate_thing(thing);

    int64_t cx;
    int64_t cy;
    int64_t cz;
    if ((thing->rendering_flags & TRF_Invisible) != 0)
        return;
    switch (thing->draw_class)
    {
    case ODC_Default: // Things
        convert_world_coord_to_front_view_screen_coord(&interp.mappos, cam, &cx, &cy, &cz);
        if (is_free_space_in_poly_pool(1))
        {
            int64_t size_on_screen = thing->sprite_size * (int64_t)((((int64_t)camera_zoom << 13) / 0x10000) / pixel_size) / 0x10000;
            add_thing_sprite_to_polypool(thing, cx, cy, cy, cz - 3 - (size_on_screen >> 1));
            if ((thing->class_id == TCls_Creature) && is_free_space_in_poly_pool(1))
            {
                create_status_box_element(thing, cx, cy, cy, 1);
            }
        }
        break;
    case ODC_RoomPrice: // Floating gold text when buying and selling
        convert_world_coord_to_front_view_screen_coord(&interp.mappos, cam, &cx, &cy, &cz);
        if (is_free_space_in_poly_pool(1))
        {
            add_number_to_polypool(cx, cy, thing->creature.gold_carried, 1);
        }
        break;
    case ODC_RoomStatusFlag: // Room Status flags
        // Hide status flags when full zoomed out, for atmospheric overview
        if (hud_scale == 0) {
            break;
        }

        RoomIndex flag_room_index = thing->lair.belongs_to;
        if (cursor_on_room(flag_room_index) == false && room_is_damaged(flag_room_index) == false && placing_same_room_type(flag_room_index) == false) {
            break;
        }

        convert_world_coord_to_front_view_screen_coord(&interp.mappos, cam, &cx, &cy, &cz);
        if (is_free_space_in_poly_pool(1))
        {
            if (get_gameturn() - thing->roomflag.last_turn_drawn == 1)
            {
                if (thing->roomflag.display_timer < 10) {
                    thing->roomflag.display_timer++;
                }
            } else {
                if (get_gameturn() - thing->roomflag.last_turn_drawn > 1) {
                    thing->roomflag.display_timer = 0;
                }
            }
            thing->roomflag.last_turn_drawn = get_gameturn();
            if (thing->roomflag.display_timer == 10)
            {
                add_room_flag_pole_to_polypool(cx, cy, thing->roomflag.room_idx, cz-3);
                if (is_free_space_in_poly_pool(1))
                {
                    add_room_flag_top_to_polypool(cx, cy, thing->roomflag.room_idx, 1);
                }
            }
        }
        break;
    case ODC_SpinningKey:
        convert_world_coord_to_front_view_screen_coord(&interp.mappos, cam, &cx, &cy, &cz);
        if (is_free_space_in_poly_pool(1))
        {
            add_spinning_key_to_polypool(thing, cx, cy, cy, cz - 3 - 4 * (camera_zoom >> 11));
        }
        break;
    default:
        break;
    }
    thing->last_turn_drawn = get_gameturn();
}

static void draw_frontview_things_on_element(struct Map *mapblk, struct Camera *cam)
{
    struct Thing *thing;
    int64_t i;
    uint64_t k;
    k = 0;
    i = get_mapwho_thing_index(mapblk);
    while (i != 0)
    {
        thing = thing_get(i);
        TRACE_THING(thing);
        if (thing_is_invalid(thing))
        {
            ERRORLOG("Jump to invalid thing detected");
            break;
        }
        i = thing->next_on_mapblk;
        draw_frontview_thing_on_element(thing, mapblk, cam);
        k++;
        if (k > THINGS_COUNT)
        {
            ERRORLOG("Infinite loop detected when sweeping things list");
            break_mapwho_infinite_chain(mapblk);
            break;
        }
    }
}

void draw_frontview_engine(struct Camera *cam)
{
    int64_t zoom_mem;
    struct PlayerInfo *player;
    TbGraphicsWindow grwnd;
    TbGraphicsWindow ewnd;
    unsigned char qdrant;
    int64_t px;
    int64_t py;
    int64_t qx;
    int64_t qy;
    int64_t w;
    int64_t h;
    int64_t pos_x;
    int64_t pos_y;
    MapSubtlCoord stl_x;
    MapSubtlCoord stl_y;
    int64_t lim_x;
    int64_t lim_y;
    int64_t cam_x;
    int64_t cam_y;
    long long zoom;
    long long lbbb;
    int64_t i;
    SYNCDBG(9,"Starting");
    player = get_my_player();
    if (cam->zoom > FRONTVIEW_CAMERA_ZOOM_MAX)
        cam->zoom = FRONTVIEW_CAMERA_ZOOM_MAX;
    calculate_hud_scale(cam);
    camera_zoom = scale_camera_zoom_to_screen(cam->zoom);
    frame_wibble_generate();
    zoom_mem = cam->zoom;//TODO [zoom] remove when all cam->zoom will be changed to camera_zoom
    cam->zoom = camera_zoom;//TODO [zoom] remove when all cam->zoom will be changed to camera_zoom
    cam_x = cam->mappos.x.val;
    cam_y = cam->mappos.y.val;
    kfx_render_state.pointer_x = (GetMouseX() - local_state.engine_window_x) / pixel_size;
    kfx_render_state.pointer_y = (GetMouseY() - local_state.engine_window_y) / pixel_size;
    LbScreenStoreGraphicsWindow(&grwnd);
    store_engine_window(&ewnd,pixel_size);
    LbScreenSetGraphicsWindow(ewnd.x, ewnd.y, ewnd.width, ewnd.height);
    setup_vecs(lbDisplay.GraphicsWindowPtr, NULL, lbDisplay.GraphicsScreenWidth, ewnd.width, ewnd.height);
    clear_fast_bucket_list();
    store_engine_window(&ewnd,1);
    setup_engine_window(ewnd.x, ewnd.y, ewnd.width, ewnd.height);
    qdrant = ((uint64_t)(cam->rotation_angle_x + DEGREES_45) / DEGREES_90) & 0x03;
    zoom = camera_zoom >> 3;
    w = (ewnd.width << 16) / zoom >> 1;
    h = (ewnd.height << 16) / zoom >> 1;
    switch (qdrant)
    {
    case 0:
        px = ((cam_x - w) >> 8);
        py = ((cam_y - h) >> 8);
        lbbb = cam_x - (px << 8);
        qx = (ewnd.width << 7)  - ((zoom * lbbb) >> 8);
        lbbb = cam_y - (py << 8);
        qy = (ewnd.height << 7) - ((zoom * lbbb) >> 8);
        break;
    case 1:
        px = ((cam_x + h) >> 8);
        py = ((cam_y - w) >> 8);
        lbbb = cam_y - (py << 8);
        qx = (ewnd.width << 7)  - ((zoom * lbbb) >> 8);
        lbbb = (px << 8) - cam_x;
        qy = (ewnd.height << 7) - ((zoom * lbbb) >> 8);
        px--;
        break;
    case 2:
        px = ((cam_x + w) >> 8) + 1;
        py = ((cam_y + h) >> 8);
        lbbb = (px << 8) - cam_x;
        qx = (ewnd.width << 7)  - ((zoom * lbbb) >> 8);
        lbbb = (py << 8) - cam_y;
        qy = (ewnd.height << 7) - ((zoom * lbbb) >> 8);
        px--;
        py--;
        break;
    case 3:
        px = ((cam_x - h) >> 8);
        py = ((cam_y + w) >> 8) + 1;
        lbbb = (py << 8) - cam_y;
        qx = (ewnd.width << 7)  - ((zoom * lbbb) >> 8);
        lbbb = cam_x - (px << 8);
        qy = (ewnd.height << 7) - ((zoom * lbbb) >> 8);
        py--;
        break;
    default:
        ERRORLOG("Illegal quadrant, %" PRId64 ".",(int64_t)(qdrant));
        LbScreenLoadGraphicsWindow(&grwnd);
        return;
    }

    update_frontview_pointed_block(zoom, qdrant, px, py, qx, qy);
    update_local_mouse_light();
    if ( (map_volume_box.visible) && (!ui_game_is_busy_doing_gui()) )
    {
        process_frontview_map_volume_box(cam, ((zoom >> 8) & 0xFF), player->id_number);
    }


    h = (8 * (zoom + 32 * ewnd.height) - qy) / zoom;
    w = (8 * (zoom + 32 * ewnd.height) - qy) / zoom;
    qy += zoom * h;
    px += x_step1[qdrant] * w;
    stl_x = x_step1[qdrant] * w + px;
    stl_y = y_step1[qdrant] * h + py;
    py += y_step1[qdrant] * h;
    lim_x = ewnd.width << 8;
    lim_y = -zoom - ABYSS_WALL_RENDER_HEIGHT * (zoom >> 1);
    SYNCDBG(19,"Range (%" PRId64 ",%" PRId64 ") to (%" PRId64 ",%" PRId64 "), quadrant %" PRId64,(int64_t)(px),(int64_t)(py),(int64_t)(qx),(int64_t)(qy),(int64_t)qdrant);
    for (pos_x=qx; pos_x < lim_x; pos_x += zoom)
    {
        i = (ewnd.height << 8);
        // Initialize the stl_? which will be swept by second loop
        if (x_step1[qdrant] != 0)
          stl_x = px;
        else
          stl_y = py;
        for (pos_y=qy; pos_y > lim_y; pos_y -= zoom)
        {
            struct Map *mapblk;
            mapblk = get_map_block_at(stl_x, stl_y);
            if (!map_block_invalid(mapblk))
            {
                if (get_mapblk_column_index(mapblk) > 0)
                {
                    draw_element(mapblk, get_subtile_lightness(&lish,stl_x,stl_y), stl_x, stl_y, pos_x, pos_y, zoom, qdrant, &i);
                }
                if ( subtile_revealed(stl_x, stl_y, player->id_number) )
                {
                    draw_frontview_things_on_element(mapblk, cam);
                }
            }
            stl_x -= x_step1[qdrant];
            stl_y -= y_step1[qdrant];
        }
        stl_x += x_step2[qdrant];
        stl_y += y_step2[qdrant];
    }

    display_fast_drawlist(cam);
    LbScreenLoadGraphicsWindow(&grwnd);
    cam->zoom = zoom_mem;//TODO [zoom] remove when all cam->zoom will be changed to camera_zoom
    SYNCDBG(9,"Finished");
}

static void render_sprite_debug_id(struct Thing* thing, int64_t scr_x, int64_t scr_y)
{
    if (render_sprite_debug_level < 2)
    {
        if (thing->class_id != TCls_Creature)
            return;
    }
    uint64_t flg_mem = RendererGetDrawFlags();
    RendererSetDrawFlags(Lb_TEXT_ONE_COLOR);
    const struct TbSprite *spr = get_button_sprite(GBS_fontchars_number_dig0);
    int64_t w = scale_ui_value(spr->SWidth);
    int64_t h = scale_ui_value(spr->SHeight);

    int64_t digit_counter, value = thing->index;

    // Count digits to be displayed
    int64_t ndigits=0;
    for (digit_counter = value; digit_counter > 0; digit_counter /= 10)
        ndigits++;
    // Show the digits
    scr_y -= h;
    int64_t pos_x = w * (ndigits - 1) / 2 + scr_x;
    for (digit_counter = value; digit_counter > 0; digit_counter /= 10)
    {
        spr = get_button_sprite((digit_counter%10) + GBS_fontchars_number_dig0);
        LbSpriteDrawScaled(pos_x, scr_y - h, spr, w, h);

        pos_x -= w;
    }
    RendererSetDrawFlags(flg_mem);
}

void render_set_sprite_debug(int64_t level)
{
    render_sprite_debug_level = level;
    switch (level)
    {
        case 0:
            render_sprite_debug_fn = NULL;
            break;
        default:
            render_sprite_debug_fn = &render_sprite_debug_id;
    }
}

void update_block_pointed(int64_t i,int64_t x, int64_t x_frac, int64_t y, int64_t y_frac)
{
    struct Map *mapblk;
    struct Column *colmn;
    int64_t visible;
    uint64_t smask;
    int64_t k;

    if (i > 0)
    {
      mapblk = get_map_block_at(x,y);
      visible = map_block_revealed(mapblk, my_player_number);
      if ((!visible) || (get_mapblk_column_index(mapblk) > 0))
      {
        if (visible)
          k = get_mapblk_column_index(mapblk);
        else
          k = kfx_sim_state.unrevealed_column_idx;
        colmn = get_column(k);
        smask = colmn->solidmask;
        if ((temp_cluedo_mode) && (smask != 0))
        {
          if (visible)
            k = get_mapblk_column_index(mapblk);
          else
            k = kfx_sim_state.unrevealed_column_idx;
          colmn = get_column(k);
          if (colmn->solidmask >= 8)
          {
            if ( (!visible) || (((mapblk->flags & SlbAtFlg_IsRoom) == 0)) )
              smask &= 3;
          }
        }
        if (smask & (1 << (i-1)))
        {
          kfx_render_state.pointed_at_frac_x = x_frac;
          kfx_render_state.pointed_at_frac_y = y_frac;
          kfx_render_state.block_pointed_at_x = x;
          kfx_render_state.block_pointed_at_y = y;
          kfx_render_state.me_pointed_at = mapblk;
        }
        if (((!temp_cluedo_mode) && (i == 5)) || ((temp_cluedo_mode) && (i == 2)))
        {
          kfx_render_state.top_pointed_at_frac_x = x_frac;
          kfx_render_state.top_pointed_at_frac_y = y_frac;
          kfx_render_state.top_pointed_at_x = x;
          kfx_render_state.top_pointed_at_y = y;
        }
      }
    } else
    {
        mapblk = get_map_block_at(x,y);
        floor_pointed_at_x = x;
        floor_pointed_at_y = y;
        kfx_render_state.block_pointed_at_x = x;
        kfx_render_state.block_pointed_at_y = y;
        kfx_render_state.pointed_at_frac_x = x_frac;
        kfx_render_state.pointed_at_frac_y = y_frac;
        kfx_render_state.me_pointed_at = mapblk;
    }
}

void update_blocks_pointed(void)
{
    int64_t x;
    int64_t y;
    int64_t x_frac;
    int64_t y_frac;
    int64_t hori_ptr_y;
    int64_t vert_ptr_y;
    int64_t hori_hdelta_y;
    int64_t vert_hdelta_y;
    int64_t hori_ptr_x;
    int64_t vert_ptr_x;
    int64_t hvdiv_x;
    int64_t hvdiv_y;
    int64_t lltmp;
    int64_t k;
    int64_t i;
    SYNCDBG(19,"Starting");
    if ((!vert_offset[1]) && (!hori_offset[1]))
    {
        kfx_render_state.block_pointed_at_x = 0;
        kfx_render_state.block_pointed_at_y = 0;
        kfx_render_state.me_pointed_at = INVALID_MAP_BLOCK;//get_map_block_at(0,0);
    } else
    {
        hori_ptr_y = (int64_t)hori_offset[0] * (kfx_render_state.pointer_y - y_init_off);
        vert_ptr_y = (int64_t)vert_offset[0] * (kfx_render_state.pointer_y - y_init_off);
        hori_hdelta_y = (int64_t)hori_offset[0] * ((int64_t)high_offset[1] >> 8);
        vert_hdelta_y = (int64_t)vert_offset[0] * ((int64_t)high_offset[1] >> 8);
        vert_ptr_x = ((int64_t)vert_offset[1] * (kfx_render_state.pointer_x - x_init_off)) >> 1;
        hori_ptr_x = ((int64_t)hori_offset[1] * (kfx_render_state.pointer_x - x_init_off)) >> 1;
        lltmp = hori_offset[0] * (int64_t)vert_offset[1] - vert_offset[0] * (int64_t)hori_offset[1];
        hvdiv_x = (lltmp >> 11);
        if (hvdiv_x == 0) hvdiv_x = 1;
        lltmp = vert_offset[0] * (int64_t)hori_offset[1] - hori_offset[0] * (int64_t)vert_offset[1];
        hvdiv_y = (lltmp >> 11);
        if (hvdiv_y == 0) hvdiv_y = 1;
        for (i=0; i < 8; i++)
        {
          k = (vert_ptr_x - (vert_ptr_y >> 1)) / hvdiv_x;
          x_frac = (k & 3) << 6;
          x = k >> 2;
          k = (hori_ptr_x - (hori_ptr_y >> 1)) / hvdiv_y;
          y_frac = (k & 3) << 6;
          y = k >> 2;
          if ((x >= 0) && (x < kfx_sim_state.map_subtiles_x) && (y >= 0) && (y < kfx_sim_state.map_subtiles_y))
          {
              update_block_pointed(i,x,x_frac,y,y_frac);
          }
          hori_ptr_y -= hori_hdelta_y;
          vert_ptr_y -= vert_hdelta_y;
        }
    }
    SYNCDBG(19,"Finished");
}

void engine(struct PlayerInfo *player, struct Camera *cam)
{
    TbGraphicsWindow grwnd;
    TbGraphicsWindow ewnd;
    int64_t flg_mem;

    SYNCDBG(9,"Starting");

    flg_mem = RendererGetDrawFlags();
    update_engine_settings(player);
    mx = cam->mappos.x.val;
    my = cam->mappos.y.val;
    mz = cam->mappos.z.val;
    kfx_render_state.pointer_x = (GetMouseX() - local_state.engine_window_x) / pixel_size;
    kfx_render_state.pointer_y = (GetMouseY() - local_state.engine_window_y) / pixel_size;
    lens = cam->horizontal_fov * scale_value_by_horizontal_resolution(4) / pixel_size;
    if (lens_mode == 0)
        update_blocks_pointed();
    update_local_mouse_light();
    LbScreenStoreGraphicsWindow(&grwnd);
    store_engine_window(&ewnd,pixel_size);
    view_height_over_2 = ewnd.height/2;
    view_width_over_2 = ewnd.width/2;
    LbScreenSetGraphicsWindow(ewnd.x, ewnd.y, ewnd.width, ewnd.height);
    setup_vecs(lbDisplay.GraphicsWindowPtr, 0, lbDisplay.GraphicsScreenWidth,
        ewnd.width, ewnd.height);
    camera_zoom = scale_camera_zoom_to_screen(cam->zoom);
    draw_view(cam, 0);
    RendererSetDrawFlags(flg_mem);
    thing_being_displayed = 0;
    LbScreenLoadGraphicsWindow(&grwnd);
}
/******************************************************************************/
