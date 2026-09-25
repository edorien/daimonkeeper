/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file creature_sounds.h
 *     Per-creature-model sound-index config-data type, shared by
 *     kfx_sim's creature_control.h (which authors and defines the
 *     creature-sound-playback mechanism) and kfx_platform's
 *     sound_manager.cpp, which reads and writes individual
 *     struct CreatureSound fields by address.
 * @par Comment:
 *     Physically split out of kfx_sim's creature_control.h into this
 *     new low-rank header, same pattern as instance_info.h/
 *     camera_data.h/packet_data.h/speech_ref.h: kfx_platform needs the
 *     complete type, not just a pointer, and it's a plain POD struct
 *     with no kfx_sim-specific type dependencies. kfx_platform is the
 *     true floor here (lower than kfx_config, which also embeds this
 *     type by value in struct CreatureModelConfig). See
 *     docs/refactor/stage-13-enforce-and-document.md.
 */
/******************************************************************************/
#ifndef DK_CREATURE_SOUNDS_H
#define DK_CREATURE_SOUNDS_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

struct CreatureSound {
    int64_t index;
    int64_t count;
};

struct CreatureSounds {
    struct CreatureSound foot;
    struct CreatureSound hit;
    struct CreatureSound happy;
    struct CreatureSound sad;
    struct CreatureSound die;
    struct CreatureSound hang;
    struct CreatureSound drop;
    struct CreatureSound torture;
    struct CreatureSound slap;
    struct CreatureSound fight;
    struct CreatureSound piss;
};

#pragma pack()
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
