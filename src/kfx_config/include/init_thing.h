/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file init_thing.h
 *     Level-file thing-creation record type, shared by kfx_sim's
 *     thing_factory.h (which authors and defines the thing-creation
 *     mechanism) and kfx_config's lvl_filesdk1.c, which declares
 *     struct InitThing by value while parsing a level's legacy .tng
 *     file.
 * @par Comment:
 *     Physically split out of kfx_sim's thing_factory.h into this new
 *     low-rank header, same pattern as instance_info.h/creature_sounds.h/
 *     speech_ref.h: kfx_config needs the complete type by value, not
 *     just a pointer, and it's a plain POD struct with no
 *     kfx_sim-specific type dependencies (struct Coord3d is already
 *     kfx_platform-owned). See
 *     docs/refactor/stage-13-enforce-and-document.md.
 */
/******************************************************************************/
#ifndef DK_INIT_THING_H
#define DK_INIT_THING_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

struct InitThing {
    struct Coord3d mappos;
    unsigned char oclass;
    ThingModel model;
    unsigned char owner;
    int64_t range;
    int64_t index;
    unsigned char params[8];
};

#pragma pack()
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
