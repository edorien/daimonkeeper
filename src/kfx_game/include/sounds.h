/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file sounds.h
 *     Header file for sounds.c.
 * @par Purpose:
 *     Sound related functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   KeeperFX Team
 * @date     11 Jul 2010 - 05 Nov 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_SOUNDS_H
#define DK_SOUNDS_H

#include "bflib_basics.h"
#include "bflib_sound.h"
#include "globals.h"

#include <SDL3_mixer/SDL_mixer.h>

#ifdef __cplusplus
extern "C" {
#endif

// FULL_LOUDNESS/NORMAL_PITCH/MIX_SPEECH_CHANNEL moved to kfx_platform's
// bflib_sound.h (stage 13.3, docs/refactor/stage-13-enforce-and-document.md)
// -- still reachable here transitively via the #include above.

/******************************************************************************/
#pragma pack(1)

struct Thing;

struct SoundSettings {
  int64_t sound_type;
  int64_t flags;
  unsigned char max_number_of_samples;
  unsigned char stereo;
  unsigned char sound_buffer_enable;
  unsigned char danger_music;
  unsigned char no_load_sounds;
  unsigned char no_load_music;
  unsigned char sound_debug_mode;
  unsigned char sound_system;
  unsigned char audio_device_enable;
  unsigned char redbook_enable;
};

enum SoundSettingsFlags {
    SndSetting_None    = 0x00,
    SndSetting_MIDI = 0x01,
    SndSetting_Sound = 0x02,
};

// atmos_sound_frequency moved to kfx_config's kfx_config_state.h (stage
// 13.3, docs/refactor/stage-13-enforce-and-document.md).
extern int64_t sdl_flags;

#pragma pack()

/******************************************************************************/
TbBool init_sound(void);
void sound_reinit_after_load(void);

void update_player_sounds(void);
void process_3d_sounds(void);

void thing_play_sample(struct Thing *, SoundSmplTblID, SoundPitch, char repeats, unsigned char ctype, unsigned char flags, int64_t priority, SoundVolume);
void play_sound_if_close_to_receiver(struct Coord3d*, SoundSmplTblID);
void stop_thing_playing_sample(struct Thing *, SoundSmplTblID smpl_idx);
void play_thing_walking(struct Thing *thing);

TbBool ambient_sound_prepare(void);
TbBool ambient_sound_stop(void);
void reset_ambient_sound_thing_idx(void);
struct Thing *create_ambient_sound(const struct Coord3d *pos, ThingModel model, PlayerNumber owner);

void mute_audio(TbBool mute);

void update_first_person_object_ambience(struct Thing *thing);

// InitialiseSDLAudio()/ShutDownSDLAudio() moved to kfx_platform's
// bflib_sndlib.h (stage 13.3) -- they're defined in bflib_sndlib.cpp,
// this was a misclassified declaration.
TbBool play_streamed_sample(const char * fname, SoundVolume);
void set_streamed_sample_volume(SoundVolume);
void stop_streamed_samples();
TbBool is_streamed_sample_playing(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
