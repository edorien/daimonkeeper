/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_render.h
 *     Header file for engine_render.c.
 * @par Purpose:
 *     Rendering the 3D view functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     20 Mar 2009 - 30 Mar 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_ENGNREND_H
#define DK_ENGNREND_H

#include "bflib_basics.h"
#include "globals.h"
#include "bflib_render.h"
#include "bflib_sprite.h"
#include "engine_lenses.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct PlayerInfo;
struct Camera;
/******************************************************************************/
// docs/refactor/editor/phase4/05-live-test-fixes.md -- bumped 16777216 ->
// 67108864 (4x) after live-testing confirmed this, not the horizon-scan
// clamp (MAX_I_CAN_SEE_OVERHEAD/MINMAX_LENGTH -- ruled out first, that fix
// stays for its own sake but didn't touch this bug), is the real cause of
// the editor's zoom-out render dropout: a diagnostic added to
// engine_render.c's draw_view() (editor_callbacks->is_active()-gated, a
// peak-usage WARNLOG) confirmed poly_pool filling to exactly
// 16777216/16777216 bytes during a reproduction, at which point every one
// of the dozens of "if (getpoly < poly_pool_end)" terrain-column insertion
// checks throughout engine_render.c starts silently no-op'ing for the rest
// of that frame -- a clean "everything past this point in iteration order
// just doesn't render" cutoff, matching the reported symptom exactly. Cost
// is a single global/static byte array (`poly_pool[]`), no stack or
// per-frame-scan-cost concern (same "measure it, it's cheap" reasoning
// already applied to MINMAX_LENGTH): going from 16MB to 64MB is trivial on
// any system this fork targets, and normal (non-editor) gameplay's own
// usage -- which the array's history says has never approached even the
// old 16MB ceiling -- is completely unaffected either way.
#define POLY_POOL_SIZE 67108864 // Originally 262144, adjusted for view distance
#define Z_DRAW_DISTANCE_MAX 65536 // Originally 11232, adjusted for view distance
#define BUCKETS_COUNT 4098 // Originally 704, adjusted for view distance. (65536/16)+2
#define BUCKETS_STEP 16 // Bucket size in Z steps

#define KEEPSPRITE_LENGTH 9149
#define KEEPERSPRITE_ADD_OFFSET 16384
#define KEEPERSPRITE_ADD_NUM 16383

struct EngineCoord { // sizeof = 28
  long view_width; // X screen position, probably not a width
  long view_height; // Y screen position, probably not a height
  unsigned short clip_flags; // Clipping and culling flags for frustum culling
  unsigned short shade_intensity; // Shading intensity for vertex lighting
  long render_distance; // Distance used for rendering calculations
  long x;
  long y;
  long z;
};

struct M31 {
    long v[4];
};

struct M33 { // sizeof = 48
    struct M31 r[3];
};

struct MapVolumeBox { // sizeof = 24
  unsigned char visible;
  unsigned char color;
  long beg_x;
  long beg_y;
  long end_x;
  long end_y;
  long floor_height_z;
};

struct ThingInterpolateResult
{
    struct Coord3d mappos;
    int32_t floor_height;
};

/******************************************************************************/
// Stripey Line Color Arrays

enum stripey_line_colors {
    SLC_RED = 0, // INVALID SELECTION
    SLC_GREEN = 1, // VALID SELECTION
    SLC_YELLOW,
    SLC_BROWN,
    SLC_GREY,
    SLC_REDYELLOW,
    SLC_GREENFLASH,
    SLC_REDFLASH,
    SLC_PURPLE,
    SLC_BLUE,
    SLC_ORANGE,
    SLC_WHITE,
    SLC_GREEN2,
    SLC_DARKGREEN,
    SLC_MIXEDGREEN,
    STRIPEY_LINE_COLOR_COUNT // Must always be the last entry (add new colours above this line)
};

struct stripey_line {
    TbPixel stripey_line_color_array[16];
    unsigned int line_color;
};

extern struct stripey_line colored_stripey_lines[];
extern unsigned char poly_pool[POLY_POOL_SIZE];
extern unsigned char *poly_pool_end;
extern long cells_away;
extern float hud_scale;
extern int creature_status_size;
extern int line_box_size;

extern struct MapVolumeBox map_volume_box;
extern long view_height_over_2;
extern long view_width_over_2;
extern long z_threshold_near;
extern long split_2;
extern long fade_max;

extern short mx;
extern short my;
extern short mz;

extern long floor_pointed_at_x;
extern long floor_pointed_at_y;
extern long box_lag_compensation_x;
extern long box_lag_compensation_y;
extern Offset vert_offset[3];
extern Offset hori_offset[3];
extern Offset high_offset[3];

extern TbSpriteData *keepsprite[KEEPSPRITE_LENGTH];
extern TbSpriteData sprite_heap_handle[KEEPSPRITE_LENGTH];
extern struct HeapMgrHeader *graphics_heap;
extern TbFileHandle jty_file_handle;

extern long x_init_off;
extern long y_init_off;
extern struct Thing *thing_being_displayed;

extern unsigned char temp_cluedo_mode;
/******************************************************************************/

extern TbSpriteData keepersprite_add[KEEPERSPRITE_ADD_NUM];
/*****************************************************************************/
float interpolate(float previous, float current);
float interpolate_angle(float previous, float current);
float interpolate_synced(float previous, float current);
struct ThingInterpolateResult interpolate_thing(struct Thing *thing);

int floor_height_for_volume_box(PlayerNumber plyr_idx, MapSlabCoord slb_x, MapSlabCoord slb_y);
void frame_wibble_generate(void);
void setup_rotate_stuff(long a1, long a2, long a3, long a4, long a5, long a6, long a7, long a8);

void process_keeper_sprite(short x, short y, unsigned short a3, short kspr_angle, unsigned char a5, long a6);
void draw_status_sprites(long a1, long a2, struct Thing *thing);
void draw_map_volume_box(long cor1_x, long cor1_y, long cor2_x, long cor2_y, long floor_height_z, unsigned char color);
// docs/refactor/editor/04-views-camera-overlays.md -- world-space overlay
// projection primitive. Unlike draw_map_volume_box() above (which just sets
// state consumed *during* this frame's own 3D render pass), this is meant
// to be called *after* that pass already ran, from an ImGui overlay drawn
// on top of the finished frame (map_x_pos/map_y_pos/map_z_pos/
// camera_matrix are already current for the frame just rendered by the
// time any ImGui callback runs -- confirmed via RendererSoftware.cpp's own
// present-step ordering). Returns false (screen_x/screen_y left
// unmodified) when the point is behind the camera or off the visible
// frustum/screen edges, mirroring rotpers()'s own clip_flags convention
// (any nonzero flag means "don't draw this").
TbBool project_world_position_to_screen(MapCoord x, MapCoord y, MapCoord z, long *screen_x, long *screen_y);

void update_engine_settings(struct PlayerInfo *player);
void draw_view(struct Camera *cam, unsigned char a2);
void draw_frontview_engine(struct Camera *cam);

void update_block_pointed(int i,long x, long x_frac, long y, long y_frac);
void update_blocks_pointed(void);
void engine(struct PlayerInfo *player, struct Camera *cam);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
