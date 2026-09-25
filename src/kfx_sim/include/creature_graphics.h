/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file creature_graphics.h
 *     Header file for creature_graphics.c.
 * @par Purpose:
 *     Creature graphics support functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     11 Mar 2010 - 23 May 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_CRTRGRAPHICS_H
#define DK_CRTRGRAPHICS_H

#include "globals.h"
#include "bflib_basics.h"

#ifdef __cplusplus
extern "C" {
#endif

// note - this is temporary value; not correct
#define CREATURE_FRAMELIST_LENGTH    982
#define CREATURE_GRAPHICS_INSTANCES   25

// enum CreatureGraphicsInstances moved to globals.h (stage 13.3) -- see there.
/******************************************************************************/
#pragma pack(1)

struct Thing;

/**
 * Enhanced TbSprite structure, with additional fields for thing animation sprites.
 */
enum FrameFlags {
    FFL_NoShadows = 1,
};

struct KeeperSprite {
  uint64_t DataOffset;

  int64_t SWidth;
  int64_t SHeight;
  int64_t FrameWidth;
  int64_t FrameHeight;
  unsigned char Rotable;
  unsigned char FramesCount;
  int64_t FrameOffsW;
  int64_t FrameOffsH;

  int64_t offset_x;
  int64_t offset_y;

  int64_t shadow_offset;
  int64_t frame_flags;
};

// Layout of one entry of the original game's data/creature.tab: a FILE FORMAT, so fixed-width on purpose (the
// in-memory struct KeeperSprite above is widened on load, see creature_table_load_unpack()).
#pragma pack(1)
struct KeeperSpriteDisk {
    uint32_t DataOffset;
    unsigned char SWidth;
    unsigned char SHeight;
    unsigned char FrameWidth;
    unsigned char FrameHeight;
    unsigned char Rotable;
    unsigned char FramesCount;
    unsigned char FrameOffsW;
    unsigned char FrameOffsH;
    int16_t offset_x;
    int16_t offset_y;
};
#pragma pack()

// Duplicated from kfx_render's engine_render.h (KEEPERSPRITE_ADD_OFFSET/
// KEEPERSPRITE_ADD_NUM) rather than included, to avoid a kfx_sim ->
// kfx_render layering violation for two stable sprite-index-range
// constants -- moved here from creature_graphics.c so creature_table_add[]'s
// extern declaration below can also use it (needs a complete array type
// for sizeof() at every #include site, e.g. kfx_render's custom_sprites.c).
// See docs/refactor/stage-13-enforce-and-document.md.
#define SIM_KEEPERSPRITE_ADD_OFFSET 16384
#define SIM_KEEPERSPRITE_ADD_NUM 16383

/******************************************************************************/
//extern unsigned short creature_graphics[][22];
extern struct KeeperSprite *creature_table;
// creature_table_add[] is now defined here too (creature_graphics.c),
// next to creature_table above -- moved down from kfx_render's
// custom_sprites.c (docs/refactor/todo/
// remove-symbol-level-layering-residuals.md), which still legitimately
// *populates* it during sprite loading from its higher-ranked layer.
extern struct KeeperSprite creature_table_add[SIM_KEEPERSPRITE_ADD_NUM];
extern size_t creature_table_length;
/******************************************************************************/

#pragma pack()
/******************************************************************************/
struct PickedUpOffset *get_creature_picked_up_offset(struct Thing *thing);

uint64_t keepersprite_index(int64_t n);
struct KeeperSprite * keepersprite_array(int64_t n);
unsigned char keepersprite_frames(int64_t n); // This returns number of frames in animation
unsigned char keepersprite_rotable(int64_t n);
void get_keepsprite_unscaled_dimensions(int64_t kspr_anim, int64_t angle, int64_t frame, int64_t *orig_w, int64_t *orig_h, int64_t *unsc_w, int64_t *unsc_h);
int64_t get_lifespan_of_animation(int64_t ani, int64_t speed);
int64_t get_creature_anim(struct Thing *thing, int64_t frame);
int64_t get_creature_model_graphics(int64_t crmodel, int64_t frame);
// set_creature_model_graphics moved to kfx_config's config_creature.h (stage 13.3).
void set_creature_graphic(struct Thing *thing);
void update_creature_rendering_flags(struct Thing *thing);

size_t creature_table_load_get_size(size_t disk_size);
void creature_table_load_unpack(unsigned char *src, size_t disk_size);

void init_censorship(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
