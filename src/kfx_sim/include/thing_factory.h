/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file thing_factory.h
 *     Header file for thing_factory.c.
 * @par Purpose:
 *     Things creation unified functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     25 Mar 2009 - 22 Nov 2012
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_TNGFACTORY_H
#define DK_TNGFACTORY_H

#include "globals.h"
#include "bflib_basics.h"
#include "init_thing.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// struct InitThing moved to kfx_config's init_thing.h (stage 13.3) --
// see there.

typedef struct VALUE VALUE;
/******************************************************************************/
TbBool thing_create_thing(struct InitThing *itng);
TbBool thing_create_thing_adv(VALUE *init_data);

struct Thing *create_cave_in(struct Coord3d *pos, ThingModel cimodel, int64_t owner);

struct Thing *create_thing(struct Coord3d *pos, int64_t tngclass, ThingModel model, int64_t owner, int64_t parent_idx);
struct Thing *create_thing_at_position_then_move_to_valid_and_add_light(struct Coord3d *pos, unsigned char tngclass, ThingModel tngmodel, unsigned char tngowner);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
