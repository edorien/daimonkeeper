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
struct Thing;

// The thing sprites themselves (struct KeeperSprite, creature.tab, the custom sprites' tables) are kfx_render's:
// keeper_sprites.h. The simulation needs only each animation's frame count (keepersprite_frames() below).
// The custom sprites' animation ids (kfx_render's KEEPERSPRITE_ADD_OFFSET/_NUM are these): ids from
// SIM_KEEPERSPRITE_ADD_OFFSET on are the sprites mods' zips add (custom_sprites.c).
#define SIM_KEEPERSPRITE_ADD_OFFSET 16384
#define SIM_KEEPERSPRITE_ADD_NUM 16383

/******************************************************************************/
struct PickedUpOffset *get_creature_picked_up_offset(struct Thing *thing);

/** The number of frames of animation id n: what times the animation (the original ids below
 *  CREATURE_FRAMELIST_LENGTH, the custom ones from SIM_KEEPERSPRITE_ADD_OFFSET). 0, with an error logged, for an
 *  original id no sprite table has published. */
unsigned char keepersprite_frames(int64_t n);
/** Publishes animation id n's frame count: kfx_render's sprite tables call it as they load (keeper_sprites.c). */
void keepersprite_set_frames(int64_t n, unsigned char frames);
/** Forgets animation id n's frame count (tests; an original id then reads as unpublished). */
void keepersprite_forget_frames(int64_t n);
int64_t get_lifespan_of_animation(int64_t ani, int64_t speed);
int64_t get_creature_anim(struct Thing *thing, int64_t frame);
int64_t get_creature_model_graphics(int64_t crmodel, int64_t frame);
// set_creature_model_graphics moved to kfx_config's config_creature.h (stage 13.3).
void set_creature_graphic(struct Thing *thing);
void update_creature_rendering_flags(struct Thing *thing);

void init_censorship(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
