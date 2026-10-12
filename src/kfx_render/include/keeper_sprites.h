/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file keeper_sprites.h
 *     The thing sprites' tables: the original game's creature.tab and the custom sprites mods' zips add.
 * @par Purpose:
 *     Each animation id names a run of sprite frames (struct KeeperSprite: the frame's size, its place on its
 *     canvas, the canvas's anchor, rotations). kfx_render owns them: it loads them, draws them and measures them.
 *     The simulation needs only each animation's frame count, which these tables publish to it
 *     (creature_graphics.h, keepersprite_frames()) when they load: docs/refactor/sprites/02-design.md section 13.
 * @par Comment:
 *     Moved here from kfx_sim's creature_graphics.c/.h (HD sprite packs, Phase 1).
 * @author   Tomasz Lis
 * @date     11 Mar 2010 - 10 Oct 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_KEEPER_SPRITES_H
#define DK_KEEPER_SPRITES_H

#include <stddef.h>
#include <stdint.h>
#include "globals.h"
#include "bflib_basics.h"
#include "creature_graphics.h" // CREATURE_FRAMELIST_LENGTH, SIM_KEEPERSPRITE_ADD_OFFSET/_NUM

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

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
#pragma pack()

/******************************************************************************/
extern struct KeeperSprite *creature_table;
extern size_t creature_table_length;
/** The custom sprites (custom_sprites.c fills it), animation ids from SIM_KEEPERSPRITE_ADD_OFFSET on. */
extern struct KeeperSprite creature_table_add[SIM_KEEPERSPRITE_ADD_NUM];

/******************************************************************************/
uint64_t keepersprite_index(int64_t n);
struct KeeperSprite * keepersprite_array(int64_t n);
unsigned char keepersprite_rotable(int64_t n);
void get_keepsprite_unscaled_dimensions(int64_t kspr_anim, int64_t angle, int64_t frame, int64_t *orig_w, int64_t *orig_h, int64_t *unsc_w, int64_t *unsc_h);

size_t creature_table_load_get_size(size_t disk_size);
void creature_table_load_unpack(unsigned char *src, size_t disk_size);

/** Publishes every animation's frame count to the simulation (keepersprite_set_frames()): each original id's from
 *  creature_table (an id whose table entry isn't loaded is left unpublished), and every custom id's. Call after
 *  creature.tab loads and after the custom sprites change. */
void keeper_sprites_publish_frames(void);
/** The same for the custom ids only (custom_sprites.c, after each zip). */
void keeper_sprites_publish_custom_frames(void);
/** How many animation ids' published frame counts differ from these tables' (0: in step). For tests. */
int64_t keeper_sprites_frames_mismatches(void);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
