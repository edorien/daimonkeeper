/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file thing_data.h
 *     Header file for thing_data.c.
 * @par Purpose:
 *     Thing struct support functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     17 Jun 2010 - 07 Jul 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_THING_DATA_H
#define DK_THING_DATA_H

#include "globals.h"
#include "bflib_basics.h"
#include "thing_types.h"


#ifdef __cplusplus
extern "C" {
#endif

TbBool is_non_synchronized_thing_class(unsigned char class_id);

typedef int64_t Thingid;

// struct Thing and its flag enums are in kfx_model's thing_types.h
// (refactor pass 2, S08); the storage and everything that indexes it stay here.
#define INVALID_THING (&kfx_sim_state.things_data[0])

/** Macro used for debugging problems related to things.
 * Should be executed in every function which changes a thing.
 * Can be defined to any SYNCLOG routine, making complete trace of usage on a thing.
 */
#define TRACE_THING(thing)

/******************************************************************************/
#define allocate_free_thing_structure(class_id) allocate_free_thing_structure_f(class_id, __func__)
struct Thing *allocate_free_thing_structure_f(unsigned char class_id, const char *func_name);
TbBool i_can_allocate_free_thing_structure(unsigned char class_id);
#define delete_thing_structure(thing, deleting_everything) delete_thing_structure_f(thing, deleting_everything, __func__)
void delete_thing_structure_f(struct Thing *thing, TbBool deleting_everything, const char *func_name);

#define thing_get(tng_idx) thing_get_f(tng_idx, __func__)
struct Thing *thing_get_f(ThingIndex tng_idx, const char *func_name);
TbBool thing_exists(const struct Thing *thing);
int64_t thing_is_invalid(const struct Thing *thing);
struct Thing* get_parent_thing(const struct Thing* thing);

TbBool thing_is_in_limbo(const struct Thing* thing);
TbBool thing_is_dragged_or_pulled(const struct Thing *thing);
struct PlayerInfo *get_player_thing_is_controlled_by(const struct Thing *thing);

void set_thing_animation(struct Thing *thing, int64_t animation_index, int64_t speed);
void set_thing_draw(struct Thing *thing, int64_t anim, int64_t speed, int64_t scale, char animate_once, char start_frame, unsigned char draw_class);

void query_thing(struct Thing *thing, TbBool key_itself);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
