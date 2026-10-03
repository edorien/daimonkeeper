/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file slab_types.h
 *     struct SlabMap, one slab of the map.
 * @par Purpose:
 *     Part of kfx_model, the header-only layout library (refactor pass 2,
 *     S08, docs/refactor-pass2/stage-08-kfx-model-headers.md). Types, macros
 *     and static inline helpers only: no function prototypes, no extern data
 *     (check_layering.py --strict enforces it). The storage these types live
 *     in stays in kfx_sim, which includes this header from slab_data.h.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_KFX_MODEL_SLAB_TYPES_H
#define DK_KFX_MODEL_SLAB_TYPES_H

#include "globals.h"
#include "bflib_basics.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

struct SlabMap {
      SlabCodedCoords next_in_room;
      HitPoints health;
      SlabKind kind;
      RoomIndex room_index;
      unsigned char wlb_type;
      PlayerNumber owner;
};

#pragma pack()
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
