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
#include "renderer/WorldFrame.h"

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
// engine_render.c's draw_view() (editorport_is_active()-gated, a
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
  int64_t view_width; // X screen position, probably not a width
  int64_t view_height; // Y screen position, probably not a height
  int64_t clip_flags; // Clipping and culling flags for frustum culling
  int64_t shade_intensity; // Shading intensity for vertex lighting
  int64_t render_distance; // Distance used for rendering calculations
  int64_t x;
  int64_t y;
  int64_t z;
};

struct M31 {
    int64_t v[4];
};

struct M33 { // sizeof = 48
    struct M31 r[3];
};

struct MapVolumeBox { // sizeof = 24
  unsigned char visible;
  unsigned char color;
  int64_t beg_x;
  int64_t beg_y;
  int64_t end_x;
  int64_t end_y;
  int64_t floor_height_z;
};

struct ThingInterpolateResult
{
    struct Coord3d mappos;
    int64_t floor_height;
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
    uint64_t line_color;
};

extern struct stripey_line colored_stripey_lines[];
extern unsigned char poly_pool[POLY_POOL_SIZE];
extern unsigned char *poly_pool_end;
extern int64_t cells_away;
extern double hud_scale;
extern int64_t creature_status_size;
extern int64_t line_box_size;

extern struct MapVolumeBox map_volume_box;
extern int64_t view_height_over_2;
extern int64_t view_width_over_2;
extern int64_t z_threshold_near;
extern int64_t split_2;
extern int64_t fade_max;

extern int64_t mx;
extern int64_t my;
extern int64_t mz;

extern int64_t floor_pointed_at_x;
extern int64_t floor_pointed_at_y;
extern int64_t box_lag_compensation_x;
extern int64_t box_lag_compensation_y;
extern Offset vert_offset[3];
extern Offset hori_offset[3];
extern Offset high_offset[3];

extern TbSpriteData *keepsprite[KEEPSPRITE_LENGTH];
extern TbSpriteData sprite_heap_handle[KEEPSPRITE_LENGTH];
extern struct HeapMgrHeader *graphics_heap;
extern TbFileHandle jty_file_handle;

extern int64_t x_init_off;
extern int64_t y_init_off;
extern struct Thing *thing_being_displayed;

extern unsigned char temp_cluedo_mode;
/******************************************************************************/

extern TbSpriteData keepersprite_add[KEEPERSPRITE_ADD_NUM];
/*****************************************************************************/
double interpolate(double previous, double current);
double interpolate_angle(double previous, double current);
double interpolate_synced(double previous, double current);
struct ThingInterpolateResult interpolate_thing(struct Thing *thing);

int64_t floor_height_for_volume_box(PlayerNumber plyr_idx, MapSlabCoord slb_x, MapSlabCoord slb_y);
void frame_wibble_generate(void);
void setup_rotate_stuff(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6, int64_t a7, int64_t a8);

void process_keeper_sprite(int64_t x, int64_t y, int64_t a3, int64_t kspr_angle, unsigned char a5, int64_t a6);
void draw_status_sprites(int64_t a1, int64_t a2, struct Thing *thing);
void draw_map_volume_box(int64_t cor1_x, int64_t cor1_y, int64_t cor2_x, int64_t cor2_y, int64_t floor_height_z, unsigned char color);
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
TbBool project_world_position_to_screen(MapCoord x, MapCoord y, MapCoord z, int64_t *screen_x, int64_t *screen_y);

void update_engine_settings(struct PlayerInfo *player);
void draw_view(struct Camera *cam, unsigned char a2);
void draw_frontview_engine(struct Camera *cam);
TbBool render_keepsprite_indexed(int64_t kspr_n, unsigned char frame, unsigned char *outbuf);

void update_block_pointed(int64_t i,int64_t x, int64_t x_frac, int64_t y, int64_t y_frac);
void update_blocks_pointed(void);
void engine(struct PlayerInfo *player, struct Camera *cam);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
